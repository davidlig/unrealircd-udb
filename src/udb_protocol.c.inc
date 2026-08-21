/* UDB - Unreal Database System for UnrealIRCd 6
 * Protocol implementation (S2S DB command and sync)
 */

static unsigned long udb_sync_txid = 0;

typedef struct UdbHelloPeer UdbHelloPeer;

struct UdbHelloPeer {
	Client *peer;
	time_t deadline;
	int state;
	UdbHelloPeer *next;
};

#define UDB_HEL_WAITING     1
#define UDB_HEL_CONFIRMED   2
#define UDB_HEL_UNSUPPORTED 3

static UdbHelloPeer *udb_hello_peers = NULL;

static UdbHelloPeer *udb_hello_peer(Client *server, int create)
{
	UdbHelloPeer *peer;

	for (peer = udb_hello_peers; peer; peer = peer->next)
		if (peer->peer == server)
			return peer;
	if (!create)
		return NULL;
	peer = safe_alloc(sizeof(*peer));
	peer->peer = server;
	peer->next = udb_hello_peers;
	udb_hello_peers = peer;
	return peer;
}

static int udb_has_hello(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	return server && IsServer(server) && MyConnect(server) && peer &&
	       peer->state == UDB_HEL_CONFIRMED;
}

static int udb_has_staged_sync(Client *server)
{
	return udb_has_hello(server);
}

static void udb_hello_start(Client *server)
{
	UdbHelloPeer *peer;

	if (!server || !IsServer(server) || !MyConnect(server))
		return;
	peer = udb_hello_peer(server, 1);
	if (peer->state)
		return;
	peer->state = UDB_HEL_WAITING;
	peer->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	sendto_one(server, NULL, ":%s DB %s HEL 4", me.id, server->id);
}

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

static void udb_sync_abort(UdbBlock *block, const char *reason)
{
	if (!block || !block->session)
		return;
	udb_log(ULOG_WARNING, "UDB_SYNC_ABORT", block->session->peer,
	        "Aborted staged sync of block $block: $reason",
	        log_data_string("block", (char[]){block->letter, '\0'}),
	        log_data_string("reason", reason));
	udb_sync_session_free(block);
}

static int udb_sync_begin(UdbBlock *block, Client *peer, const char *txid)
{
	UdbSyncSession *session;

	if (!block || !peer || !txid || !*txid || strlen(txid) >= sizeof(session->txid))
		return 0;
	if (block->session)
		return 0;
	session = safe_alloc(sizeof(*session));
	session->peer = peer;
	strlcpy(session->txid, txid, sizeof(session->txid));
	session->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	session->tree = udb_record_create(NULL);
	session->tree->block_idx = (unsigned char)udb_block_letter_to_index(block->letter);
	safe_strdup(session->tree->key, "UDB");
	session->tree->is_dynamic_key = 1;
	block->session = session;
	block->syncing_from = peer;
	return 1;
}

static void udb_sync_send_tree(Client *server, UdbRecord *rec, int depth,
                               char *pathbuf, int pathlen, char letter, const char *txid)
{
	size_t old_len;
	UdbRecord *child;

	if (!rec || !rec->key)
		return;
	old_len = strlen(pathbuf);
	if (depth > 0)
		strlcat(pathbuf, "::", pathlen);
	strlcat(pathbuf, rec->key, pathlen);
	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		if (rec->data_str)
			sendto_one(server, NULL, ":%s DB %s PUT %c %s %s :%s", me.id, server->id,
			           letter, txid, pathbuf, rec->data_str);
		else
			sendto_one(server, NULL, ":%s DB %s PUT %c %s %s *%lu", me.id, server->id,
			           letter, txid, pathbuf, rec->data_num);
	}
	for (child = rec->child; child; child = child->sibling)
		udb_sync_send_tree(server, child, depth + 1, pathbuf, pathlen, letter, txid);
	pathbuf[old_len] = '\0';
}

static void udb_sync_send_stage(Client *server, UdbBlock *block)
{
	char txid[32];
	char pathbuf[4096] = "";
	UdbRecord *rec;

	if (!udb_has_hello(server))
		return;
	snprintf(txid, sizeof(txid), "%08lx", ++udb_sync_txid);
	sendto_one(server, NULL, ":%s DB %s BEGIN %c %s %08lX", me.id, server->id,
	           block->letter, txid, block->checksum);
	for (rec = block->tree->child; rec; rec = rec->sibling)
		udb_sync_send_tree(server, rec, 0, pathbuf, sizeof(pathbuf), block->letter, txid);
	sendto_one(server, NULL, ":%s DB %s END %c %s %08lX", me.id, server->id,
	           block->letter, txid, block->checksum);
}

EVENT(udb_sync_timeout_event)
{
	UdbBlock *block;
	UdbHelloPeer *peer;
	time_t now = time(NULL);

	for (block = udb_ctx ? udb_ctx->block_list : NULL; block; block = block->next)
		if (block->session && block->session->deadline <= now)
			udb_sync_abort(block, "timeout");

	for (peer = udb_hello_peers; peer; peer = peer->next)
	{
		if (peer->state == UDB_HEL_WAITING && peer->deadline <= now)
		{
			peer->state = UDB_HEL_UNSUPPORTED;
			udb_log(ULOG_INFO, "UDB_HEL_TIMEOUT", peer->peer,
			        "No UDB HEL 4 acknowledgement from directly linked server; capability disabled for this link");
		}
	}
}

static const char *udb_selected_propagator(void)
{
	const char *selected = NULL;
	UdbRecord *link;
	int sources = 0;

	if (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator)
	{
		selected = udb_cfg->propagator;
		sources++;
	}
	if (udb_ctx && udb_ctx->links)
	{
		for (link = udb_ctx->links->child; link; link = link->sibling)
		{
			UdbRecord *options = udb_record_find(LKEY_OPTIONS, link);
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
		udb_ctx->propagator = NULL;
		udb_send_to_debugs(NULL, "propagator selection rejected");
		udb_log(ULOG_WARNING, "UDB_PROPAGATOR_SELECTION", NULL,
		        "Rejecting UDB writes: exactly one propagator source is required");
		return NULL;
	}
	return selected;
}

static int udb_is_propagator(Client *server)
{
	const char *selected;

	if (!server || !IsServer(server))
		return 0;
	selected = udb_selected_propagator();
	if (selected && !strcasecmp(server->name, selected))
	{
		udb_ctx->propagator = server;
		return 1;
	}
	return 0;
}

/* Paths are passed to the file parser without the block prefix. */
static UdbBlock *udb_protocol_path_block(const char *path)
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

	block = udb_block_by_letter(path[0]);
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
	udb_hello_start(client);
	return 0;
}

static int udb_hook_server_quit(Client *client, MessageTag *mtags)
{
	UdbBlock *block;
	UdbHelloPeer **peer;

	if (!udb_ctx || !client)
		return 0;

	if (udb_ctx->propagator == client)
		udb_ctx->propagator = NULL;
	for (peer = &udb_hello_peers; *peer; peer = &(*peer)->next)
		if ((*peer)->peer == client)
		{
			UdbHelloPeer *old = *peer;
			*peer = old->next;
			safe_free(old);
			break;
		}

	for (block = udb_ctx->block_list; block; block = block->next)
	{
		if (block->session && block->session->peer == client)
			udb_sync_abort(block, "peer quit");
		else if (block->syncing_from == client)
			block->syncing_from = NULL;
	}
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
	char logbuf[512];

	if (!target || !*target || !subcmd || !*subcmd)
	{
		udb_protocol_params_error(client, subcmd);
		return;
	}

	/* HEL is the sole DB frame accepted before UDB capability confirmation. */
	if (!strcasecmp(subcmd, "HEL"))
	{
		UdbHelloPeer *peer;

		if (!IsServer(client) || !MyConnect(client) ||
		    (strcmp(target, me.id) && strcmp(target, me.name)) ||
		    parc < 4 || strcmp(parv[3], "4"))
			return;
		snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
		unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));
		peer = udb_hello_peer(client, 1);
		if (parc == 5 && !strcasecmp(parv[4], "ACK"))
		{
			if (peer->state != UDB_HEL_WAITING)
				return;
			peer->state = UDB_HEL_CONFIRMED;
			udb_log(ULOG_INFO, "UDB_HEL_CONFIRMED", client,
			        "UDB HEL 4 capability confirmed for directly linked server");
			udb_sync_to_server(client);
			return;
		}
		if (parc != 4)
			return;
		/* Each side sends its own request, so only an ACK confirms outbound data. */
		if (!peer->state)
			udb_hello_start(client);
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
				block = udb_block_by_letter(*parv[3]);
				if (is_for_me)
				{
					if (block && block->session && block->session->peer == client)
					{
						udb_sync_abort(block, "duplicate BEGIN");
						sendto_one(client, NULL, ":%s DB %s ERR BEGIN %d %c", me.id, client->id,
						           UDB_ERR_SYNC_ACTIVE, *parv[3]);
						return;
					}
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
				UdbSyncSession *session;
				char line[4096];
				if (parc < 7 || !udb_has_staged_sync(client))
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				block = udb_block_by_letter(*parv[3]);
				if (is_for_me)
				{
					session = block ? block->session : NULL;
					if (!session || session->peer != client || strcmp(session->txid, parv[4]))
					{
						if (session && session->peer == client)
							udb_sync_abort(block, "invalid PUT sequence");
						sendto_one(client, NULL, ":%s DB %s ERR PUT %d %c", me.id, client->id,
						           UDB_ERR_NO_SYNC, parv[3] ? *parv[3] : '0');
						return;
					}
					if (snprintf(line, sizeof(line), "%s %s", parv[5], parv[6]) >= (int)sizeof(line) ||
					    !udb_stage_parse_line(block, session, line))
					{
						udb_sync_abort(block, "invalid PUT payload");
						sendto_one(client, NULL, ":%s DB %s ERR PUT %d %c", me.id, client->id,
						           UDB_ERR_PARAMS, *parv[3]);
						return;
					}
					session->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
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
				UdbSyncSession *session;
				unsigned long digest;
				if (parc < 6 || !udb_has_staged_sync(client))
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				block = udb_block_by_letter(*parv[3]);
				if (is_for_me)
				{
					session = block ? block->session : NULL;
					if (!session || session->peer != client || strcmp(session->txid, parv[4]))
					{
						if (session && session->peer == client)
							udb_sync_abort(block, "invalid END sequence");
						sendto_one(client, NULL, ":%s DB %s ERR END %d %c", me.id, client->id,
						           UDB_ERR_NO_SYNC, parv[3] ? *parv[3] : '0');
						return;
					}
					digest = udb_compute_tree_checksum(session->tree);
					if (digest != strtoul(parv[5], NULL, 16) ||
					    !udb_stage_persist_block(block, session) ||
					    !udb_block_commit_stage(block, session, digest))
					{
						udb_sync_abort(block, "digest or persistence failure");
						sendto_one(client, NULL, ":%s DB %s ERR END %d %c", me.id, client->id,
						           UDB_ERR_FATAL, *parv[3]);
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
					udb_log(ULOG_INFO, "UDB_SYNC_ACK", client,
					        "Staged sync acknowledged for block $block",
					        log_data_string("block", parv[3]));
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
				UdbBlock *block = udb_block_by_letter(letter);

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
				UdbBlock *block = udb_protocol_path_block(path);
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
					if (block->syncing_from != client && !udb_is_propagator(client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}

					int len = strlen(path) + strlen(data) + 2;
					char *line = safe_alloc(len);
					snprintf(line, len, "%s %s", path + 3, data);
					UdbRecord *rec = udb_file_parse_line(block, line);
					safe_free(line);

					if (rec)
					{
						udb_apply_special_record(block, rec, 1);
					}
					if (!block->syncing_from)
						udb_file_save_block(block);

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
				UdbBlock *block = udb_block_by_letter(letter);

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
				UdbBlock *block = udb_protocol_path_block(path);
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
					} else if (!udb_is_propagator(client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}

					UdbRecord *rec = udb_record_find_path(block, path + 3);
					if (rec)
					{
						udb_record_delete(block, rec, 1);
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
					UdbBlock *block = udb_block_by_letter(letter);
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
					if (!udb_is_propagator(client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}

					udb_block_reset(block);
					block->checksum = 0;
					block->filesize = 0;
					if (!block->syncing_from)
						udb_file_save_block(block);

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
					UdbBlock *block = udb_block_by_letter(letter);
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

					udb_file_save_block(block);
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
					UdbBlock *block = udb_block_by_letter(letter);
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
					if (!udb_is_propagator(client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}
					if (parc >= 5)
					{
						block->modified_at = atol(parv[4]);
					}
					udb_file_save_block(block);

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
