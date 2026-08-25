/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Server-to-Server (S2S) Protocol & HEL Capability Negotiation
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
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

/* Server SIDs identify servers independently of names, links, and frame order. */
static int udb_remote_wins_equal_timestamp(Client *server)
{
	return server && *server->id && *me.id &&
	       strcmp(server->id, me.id) > 0;
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
		const char *prop = (parc >= 5 && parv[4]) ? parv[4] : "?";
		udb_hello_peer(client, 1)->authorizes_us = !strcasecmp(prop, me.name) ||
		                                           !strcmp(prop, "?");
		udb_log(ULOG_INFO, "UDB_HEL_AUTHORIZATION", client,
		        "Direct peer selected $propagator as its staged-sync source",
		        log_data_string("propagator", prop));
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
				if (udb_selected_propagator(ctx) && !udb_is_propagator(ctx, client))
				{
					sendto_one(client, NULL, ":%s DB %s ERR BEGIN %d %c", me.id, client->id,
					           UDB_ERR_FORBIDDEN, parv[3] ? *parv[3] : '0');
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
				if (udb_selected_propagator(ctx) && !udb_is_propagator(ctx, client))
				{
					sendto_one(client, NULL, ":%s DB %s ERR PUT %d %c", me.id, client->id,
					           UDB_ERR_FORBIDDEN, parv[3] ? *parv[3] : '0');
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
				if (udb_selected_propagator(ctx) && !udb_is_propagator(ctx, client))
				{
					sendto_one(client, NULL, ":%s DB %s ERR END %d %c", me.id, client->id,
					           UDB_ERR_FORBIDDEN, parv[3] ? *parv[3] : '0');
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
						} else if (remote_ts == block->modified_at &&
						           udb_remote_wins_equal_timestamp(client))
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
				udb_mutation_ins(ctx, client, target, parv[3], parv[4], is_for_me, is_broadcast);
			}
			break;

		case 'R':
			if (!strcasecmp(subcmd, "RES"))
			{
				if (parc < 4)
					return;
				char letter = *parv[3];
				UdbBlock *block = udb_block_by_letter(ctx, letter);
				if (!udb_peer_authorizes_us(client))
				{
					sendto_one(client, NULL, ":%s DB %s ERR RES %d %c", me.id, client->id,
					           UDB_ERR_FORBIDDEN, letter);
					return;
				}

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

					udb_sync_send_stage(client, block);
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
				udb_mutation_del(ctx, client, target, parv[3], is_for_me, is_broadcast);
			} else if (!strcasecmp(subcmd, "DRP"))
			{
				if (parc < 4)
					return;
				udb_mutation_drp(ctx, client, target, *parv[3], is_for_me, is_broadcast);
			}
			break;

		case 'O':
			if (!strcasecmp(subcmd, "OPT"))
			{
				if (parc < 4)
					return;
				udb_mutation_opt(ctx, client, target, *parv[3], parc >= 5 ? parv[4] : NULL,
				                 is_for_me, is_broadcast);
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
