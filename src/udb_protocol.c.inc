/* UDB - Unreal Database System for UnrealIRCd 6
 * Protocol implementation (S2S DB command and sync)
 */

static void udb_sendto_confirmed_servers(Client *except, const char *fmt, ...)
{
	Client *server;
	char line[4096];
	va_list args;

	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	list_for_each_entry(server, &client_list, client_node) if (server != except && IsServer(server) && MyConnect(server) && udb_has_hello(server))
	    sendto_one(server, NULL, "%s", line);
}

static const char *udb_selected_propagator(UdbContext *ctx)
{
	const char *selected = NULL;
	UdbRecord *link;
	int sources = 0;

	if (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator)
	{
		selected = udb_cfg->propagator;
		sources++;
	}
	if (ctx && ctx->links)
	{
		for (link = ctx->links->child; link; link = link->sibling)
		{
			UdbRecord *options = udb_record_find(ctx, LKEY_OPTIONS, link);
			if (options && !options->data_str &&
			    !(options->data_num & ~(UDB_LNKOPT_DEBUG | UDB_LNKOPT_PROPAGATOR)) &&
			    (options->data_num & UDB_LNKOPT_PROPAGATOR))
			{
				selected = link->key;
				sources++;
			}
		}
	}
	if (sources != 1)
	{
		ctx->propagator = NULL;
		udb_send_to_debugs(NULL, "propagator selection rejected");
		udb_log(ULOG_WARNING, "UDB_PROPAGATOR_SELECTION", NULL,
		        "Rejecting UDB writes: exactly one propagator source is required");
		return NULL;
	}
	return selected;
}

static int udb_is_propagator(UdbContext *ctx, Client *server)
{
	const char *selected;

	if (!server || !IsServer(server))
		return 0;
	selected = udb_selected_propagator(ctx);
	if (selected && !strcasecmp(server->name, selected))
	{
		ctx->propagator = server;
		return 1;
	}
	return 0;
}

/* Paths are passed to the file parser without the block prefix. */
static UdbBlock *udb_protocol_path_block(UdbContext *ctx, const char *path)
{
	const char *component;
	const char *separator;
	size_t len;
	UdbBlock *block;

	if (!path)
		return NULL;

	len = strlen(path);
	if (len < 4 || len >= 512 || path[1] != ':' || path[2] != ':' || !path[3])
		return NULL;

	block = udb_block_by_letter(ctx, path[0]);
	if (!block)
		return NULL;

	component = path + 3;
	while ((separator = strstr(component, "::")))
	{
		if (separator == component || !separator[2])
			return NULL;
		component = separator + 2;
	}

	return block;
}

static void udb_protocol_params_error(Client *client, const char *subcmd)
{
	sendto_one(client, NULL, ":%s DB %s ERR %s %d 0", me.id, client->id,
	           subcmd ? subcmd : "0", UDB_ERR_PARAMS);
}

static void udb_sync_to_server(Client *server)
{
	UdbBlock *block = udb_ctx->block_list;
	if (!udb_has_hello(server))
		return;
	while (block)
	{
		sendto_one(server, NULL, ":%s DB %s INF %c %lX %lu",
		           me.id, server->id, block->letter, block->checksum, (unsigned long)block->modified_at);
		block = block->next;
	}
}

static int udb_hook_server_sync(Client *client)
{
	if (!client || !IsServer(client) || !MyConnect(client))
		return 0;
	udb_sync_hello_start(client);
	return 0;
}

static int udb_hook_server_quit(Client *client, MessageTag *mtags)
{
	if (!udb_ctx || !client)
		return 0;

	if (udb_ctx->propagator == client)
		udb_ctx->propagator = NULL;
	udb_sync_server_quit(client);
	return 0;
}

CMD_FUNC(cmd_db)
{
	/* Process DB protocol messages sent via server-to-server connection */

	if (parc < 4)
	{
		sendto_one(client, NULL, ":%s DB %s ERR 0 %i 0", me.id, client->id, UDB_ERR_PARAMS);
		return;
	}

	const char *target = parv[1];
	const char *subcmd = parv[2];
	UdbContext *ctx = udb_ctx;
	char logbuf[512];

	if (!target || !*target || !subcmd || !*subcmd)
	{
		udb_protocol_params_error(client, subcmd);
		return;
	}

	/* HEL is the sole DB frame accepted before UDB capability confirmation. */
	if (!strcasecmp(subcmd, "HEL"))
	{
		if (!IsServer(client) || !MyConnect(client) ||
		    (strcmp(target, me.id) && strcmp(target, me.name)) ||
		    parc < 4 || strcmp(parv[3], "4"))
			return;
		snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
		unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));
		if (parc == 5 && !strcasecmp(parv[4], "ACK"))
		{
			udb_sync_hello_ack(client);
			return;
		}
		if (parc != 4)
			return;
		/* Each side sends its own request, so only an ACK confirms outbound data. */
		if (!udb_has_hello(client))
			udb_sync_hello_start(client);
		sendto_one(client, NULL, ":%s DB %s HEL 4 ACK", me.id, client->id);
		return;
	}

	if (!udb_has_hello(client))
	{
		sendto_one(client, NULL, ":%s DB %s ERR %s %d 0", me.id, client->id,
		           subcmd, UDB_ERR_FORBIDDEN);
		return;
	}

	snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
	unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));

	int is_broadcast = !strcmp(target, "*");
	int is_for_me = is_broadcast || !strcmp(target, me.id) || !strcmp(target, me.name);

	switch (toupper((unsigned char)subcmd[0]))
	{
		case 'B':
			if (!strcasecmp(subcmd, "BEGIN"))
			{
				UdbBlock *block;
				if (parc < 6 || !udb_has_staged_sync(client))
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				block = udb_block_by_letter(ctx, *parv[3]);
				if (is_for_me)
				{
					if (!block || !udb_sync_begin(block, client, parv[4]))
					{
						sendto_one(client, NULL, ":%s DB %s ERR BEGIN %d %c", me.id, client->id,
						           UDB_ERR_SYNC_ACTIVE, parv[3] ? *parv[3] : '0');
						return;
					}
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s BEGIN %s %s %s", client->id,
				                             target, parv[3], parv[4], parv[5]);
			}
			break;

		case 'P':
			if (!strcasecmp(subcmd, "PUT"))
			{
				UdbBlock *block;
				int error;
				if (parc < 7 || !udb_has_staged_sync(client))
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				block = udb_block_by_letter(ctx, *parv[3]);
				if (is_for_me)
				{
					error = udb_sync_put(block, client, parv[4], parv[5], parv[6]);
					if (error)
					{
						sendto_one(client, NULL, ":%s DB %s ERR PUT %d %c", me.id, client->id,
						           error, parv[3] ? *parv[3] : '0');
						return;
					}
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s PUT %s %s %s :%s", client->id,
				                             target, parv[3], parv[4], parv[5], parv[6]);
			}
			break;

		case 'E':
			if (!strcasecmp(subcmd, "END"))
			{
				UdbBlock *block;
				unsigned long digest;
				int error;
				if (parc < 6 || !udb_has_staged_sync(client))
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				block = udb_block_by_letter(ctx, *parv[3]);
				if (is_for_me)
				{
					error = udb_sync_end(ctx, block, client, parv[4], parv[5], &digest);
					if (error)
					{
						sendto_one(client, NULL, ":%s DB %s ERR END %d %c", me.id, client->id,
						           error, parv[3] ? *parv[3] : '0');
						return;
					}
					sendto_one(client, NULL, ":%s DB %s ACK %c %s %08lX", me.id, client->id,
					           *parv[3], parv[4], digest);
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s END %s %s %s", client->id,
				                             target, parv[3], parv[4], parv[5]);
			} else if (!strcasecmp(subcmd, "ERR"))
			{
				if (parc < 5)
					return;
				if (is_for_me)
				{
					int errcode = atoi(parv[4]);
					udb_log(ULOG_INFO, "UDB_EVENT", client, "Error from $client: cmd=$cmd err=$errcode",
					        log_data_client("client", client), log_data_string("cmd", parv[3]), log_data_integer("errcode", errcode));
					if (!is_broadcast)
						return;
				}
				if (parc >= 6)
				{
					udb_sendto_confirmed_servers(client, ":%s DB %s ERR %s %s %s", client->id, target, parv[3], parv[4], parv[5]);
				} else
				{
					udb_sendto_confirmed_servers(client, ":%s DB %s ERR %s %s", client->id, target, parv[3], parv[4]);
				}
			}
			break;

		case 'A':
			if (!strcasecmp(subcmd, "ACK"))
			{
				if (parc < 6 || !udb_has_staged_sync(client))
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				if (is_for_me)
				{
					udb_sync_ack(client, parv[3]);
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s ACK %s %s %s", client->id,
				                             target, parv[3], parv[4], parv[5]);
			}
			break;

		case 'I':
			if (!strcasecmp(subcmd, "INF"))
			{
				if (parc < 6)
					return;
				char letter = *parv[3];
				UdbBlock *block = udb_block_by_letter(ctx, letter);

				if (is_for_me)
				{
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR INF %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					unsigned long crc32 = strtoul(parv[4], NULL, 16);
					time_t remote_ts = atol(parv[5]);

					if (crc32 != block->checksum)
					{
						if (remote_ts > block->modified_at)
						{
							sendto_one(client, NULL, ":%s DB %s RES %c", me.id, client->id, letter);
						} else if (remote_ts == block->modified_at)
						{
							sendto_one(client, NULL, ":%s DB %s RES %c", me.id, client->id, letter);
						}
					}
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s INF %c %s %s", client->id, target, letter, parv[4], parv[5]);
			} else if (!strcasecmp(subcmd, "INS"))
			{
				if (parc < 5)
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				const char *path = parv[3];
				const char *data = parv[4];
				UdbBlock *block = udb_protocol_path_block(ctx, path);
				char letter = path && *path ? path[0] : '0';

				if (!block)
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}

				if (is_for_me)
				{
					if (block->session)
					{
						sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from && block->syncing_from != client)
					{
						sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from != client && !udb_is_propagator(ctx, client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}

					int len = strlen(path) + strlen(data) + 2;
					char *line = safe_alloc(len);
					snprintf(line, len, "%s %s", path + 3, data);
					UdbRecord *rec = udb_file_parse_line(ctx, block, line);
					safe_free(line);

					if (rec)
					{
						udb_apply_special_record(ctx, block, rec, 1);
					}
					if (!block->syncing_from)
						udb_file_save_block(ctx, block);

					char logbuf[512];
					snprintf(logbuf, sizeof(logbuf), "[UDB] Inserted record via S2S: %s -> %s", path, data);
					unreal_log(ULOG_INFO, "udb", "UDB_INS_RECEIVED", client, "$msg", log_data_string("msg", logbuf));

					if (udb_ctx->propagator && block->syncing_from == client)
					{
						udb_sendto_confirmed_servers(client, ":%s DB %s INS %s %s", udb_ctx->propagator->id, target, path, data);
						return;
					}

					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s INS %s %s", client->id, target, path, data);
			}
			break;

		case 'R':
			if (!strcasecmp(subcmd, "RES"))
			{
				if (parc < 4)
					return;
				char letter = *parv[3];
				UdbBlock *block = udb_block_by_letter(ctx, letter);

				if (is_for_me)
				{
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					if (block->syncing_from && block->syncing_from != client)
					{
						sendto_one(client, NULL, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}

					if (udb_has_staged_sync(client))
					{
						udb_sync_send_stage(client, block);
					} else
					{
						FILE *fp = fopen(block->filepath, "r");
						if (fp)
						{
							char line[1024];
							while (fgets(line, sizeof(line), fp))
							{
								size_t len = strlen(line);
								while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
								{
									line[--len] = '\0';
								}
								if (len > 0)
								{
									if (strchr(line, ' '))
										sendto_one(client, NULL, ":%s DB * INS %c::%s", me.id, letter, line);
									else
										sendto_one(client, NULL, ":%s DB * DEL %c::%s", me.id, letter, line);
								}
							}
							fclose(fp);
						}
						sendto_one(client, NULL, ":%s DB %s FDR %c", me.id, client->id, letter);
					}
					block->syncing_from = NULL;

					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s RES %c", client->id, target, letter);
			}
			break;

		case 'D':
			if (!strcasecmp(subcmd, "DEL"))
			{
				if (parc < 4)
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				const char *path = parv[3];
				UdbBlock *block = udb_protocol_path_block(ctx, path);
				char letter = path && *path ? path[0] : '0';

				if (!block)
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}

				if (is_for_me)
				{
					if (block->session)
					{
						sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from)
					{
						if (block->syncing_from != client)
						{
							sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
							return;
						}
					} else if (!udb_is_propagator(ctx, client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}

					UdbRecord *rec = udb_record_find_path(ctx, block, path + 3);
					if (rec)
					{
						udb_record_delete(ctx, block, rec, 1);
					}

					if (udb_ctx->propagator && block->syncing_from == client)
					{
						udb_sendto_confirmed_servers(client, ":%s DB %s DEL %s", udb_ctx->propagator->id, target, path);
						return;
					}
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s DEL %s", client->id, target, path);
			} else if (!strcasecmp(subcmd, "DRP"))
			{
				if (parc < 4)
					return;
				char letter = *parv[3];

				if (is_for_me)
				{
					UdbBlock *block = udb_block_by_letter(ctx, letter);
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					if (block->session)
					{
						sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from && block->syncing_from != client)
					{
						sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (!udb_is_propagator(ctx, client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}

					udb_block_reset(ctx, block);
					block->checksum = 0;
					block->filesize = 0;
					if (!block->syncing_from)
						udb_file_save_block(ctx, block);

					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s DRP %c", client->id, target, *parv[3]);
			}
			break;

		case 'F':
			if (!strcasecmp(subcmd, "FDR"))
			{
				if (parc < 4)
					return;
				char letter = *parv[3];

				if (is_for_me)
				{
					UdbBlock *block = udb_block_by_letter(ctx, letter);
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					if (block->session)
					{
						if (block->session->peer == client)
							udb_sync_abort(block, "legacy FDR during staged sync");
						sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from != client)
					{
						sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_NO_SYNC, letter);
						return;
					}

					udb_file_save_block(ctx, block);
					block->syncing_from = NULL;

					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s FDR %c", client->id, target, *parv[3]);
			}
			break;

		case 'O':
			if (!strcasecmp(subcmd, "OPT"))
			{
				if (parc < 4)
					return;
				char letter = *parv[3];

				if (is_for_me)
				{
					UdbBlock *block = udb_block_by_letter(ctx, letter);
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					if (block->session)
					{
						sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (!udb_is_propagator(ctx, client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}
					if (parc >= 5)
					{
						block->modified_at = atol(parv[4]);
					}
					udb_file_save_block(ctx, block);

					if (!is_broadcast)
						return;
				}
				if (parc >= 5)
					udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c %s", client->id, target, *parv[3], parv[4]);
				else
					udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c", client->id, target, *parv[3]);
			}
			break;
	}
}

static int udb_protocol_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DB", cmd_db, MAXPARA, CMD_SERVER);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, udb_hook_server_sync);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, udb_hook_server_quit);
	EventAdd(modinfo->handle, "udb_sync_timeout", udb_sync_timeout_event, NULL, 1000, 0);

	return 0;
}
