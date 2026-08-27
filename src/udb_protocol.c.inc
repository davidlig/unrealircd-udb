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

static int udb_send_db_to_one(Client *to, const char *fmt, ...)
{
	char *line;
	va_list args;
	int n;

	if (!to)
		return 0;

	line = safe_alloc(UDB_S2S_LINE_MAX);
	va_start(args, fmt);
	n = vsnprintf(line, UDB_S2S_LINE_MAX, fmt, args);
	va_end(args);

	if (n < 0 || (size_t)n >= UDB_S2S_LINE_MAX)
	{
		udb_log(ULOG_ERROR, "UDB_S2S_OVERSIZE_FRAME", to,
				"Oversized S2S frame ($length bytes) discarded to prevent truncation", log_data_integer("length", n));
		safe_free(line);
		return 0;
	}

	sendto_one(to, NULL, "%s", line);
	safe_free(line);
	return 1;
}

static int udb_send_db_to_confirmed_servers(Client *except, const char *fmt, ...)
{
	Client *server;
	char *line;
	va_list args;
	int n;
	int sent = 0;

	line = safe_alloc(UDB_S2S_LINE_MAX);
	va_start(args, fmt);
	n = vsnprintf(line, UDB_S2S_LINE_MAX, fmt, args);
	va_end(args);

	if (n < 0 || (size_t)n >= UDB_S2S_LINE_MAX)
	{
		udb_log(ULOG_ERROR, "UDB_S2S_OVERSIZE_BROADCAST", except,
				"Oversized broadcast S2S frame ($length bytes) discarded to prevent truncation",
				log_data_integer("length", n));
		safe_free(line);
		return 0;
	}

	list_for_each_entry(server, &server_list, special_node)
	{
		if (server == except || (except && server == except->direction))
			continue;
		if (IsServer(server) && MyConnect(server) && udb_has_hello(server))
		{
			sendto_one(server, NULL, "%s", line);
			sent++;
		}
	}
	safe_free(line);
	return 1;
}

static int udb_sendto_confirmed_servers(Client *except, const char *fmt, ...)
{
	char *line;
	va_list args;
	int n;
	int sent = 0;

	line = safe_alloc(UDB_S2S_LINE_MAX);
	va_start(args, fmt);
	n = vsnprintf(line, UDB_S2S_LINE_MAX, fmt, args);
	va_end(args);

	if (n >= 0 && (size_t)n < UDB_S2S_LINE_MAX)
	{
		Client *server;
		list_for_each_entry(server, &server_list, special_node)
		{
			if (server == except || (except && server == except->direction))
				continue;
			if (IsServer(server) && MyConnect(server) && udb_has_hello(server))
			{
				sendto_one(server, NULL, "%s", line);
				sent++;
			}
		}
	}
	else
	{
		udb_log(ULOG_ERROR, "UDB_S2S_OVERSIZE_BROADCAST", except,
				"Oversized broadcast S2S frame ($length bytes) discarded to prevent truncation",
				log_data_integer("length", n));
		safe_free(line);
		return 0;
	}
	safe_free(line);
	return 1;
}

static void udb_protocol_params_error(Client *client, const char *subcmd)
{
	udb_send_db_to_one(client, ":%s DB %s ERR %s %d 0", me.id, client->id, subcmd ? subcmd : "0", UDB_ERR_PARAMS);
}

static int udb_sync_to_server(Client *server)
{
	UdbBlock *block = udb_ctx->block_list;
	if (!udb_has_hello(server))
		return 0;
	while (block)
	{
		if (!udb_send_db_to_one(server, ":%s DB %s INF %c %lX %lu", me.id, server->id, block->letter, block->checksum,
								(unsigned long)block->modified_at))
			return 0;
		block = block->next;
	}
	return 1;
}
static Client *udb_direct_peer(Client *client)
{
	if (!client)
		return NULL;
	if (MyConnect(client))
		return client;
	if (client->direction && MyConnect(client->direction))
		return client->direction;
	return NULL;
}

/* Server SIDs identify servers independently of names, links, and frame order. */
static int udb_remote_wins_equal_timestamp(Client *server)
{
	return server && *server->id && *me.id && strcmp(server->id, me.id) > 0;
}

static int udb_hook_server_sync(Client *client)
{
	if (!client || !IsServer(client) || !MyConnect(client))
		return 0;
	udb_sync_hello_start(client);
	return 0;
}

int udb_hook_server_quit(Client *client, MessageTag *mtags)
{
	udb_sync_server_quit(client);
	return 0;
}

CMD_FUNC(cmd_db)
{
	/* Process DB protocol messages sent via server-to-server connection */

	if (parc < 4)
	{
		udb_send_db_to_one(client, ":%s DB %s ERR 0 %i 0", me.id, client->id, UDB_ERR_PARAMS);
		return;
	}

	const char *target = parv[1];
	const char *subcmd = parv[2];
	UdbContext *ctx = udb_ctx;
	Client *direct_peer = udb_direct_peer(client);
	char logbuf[512];

	if (!target || !*target || !subcmd || !*subcmd)
	{
		udb_protocol_params_error(client, subcmd);
		return;
	}

	/* HEL is the sole DB frame accepted before UDB capability confirmation. */
	if (!strcasecmp(subcmd, "HEL"))
	{
		if (!IsServer(client) || !MyConnect(client) || (strcmp(target, me.id) && strcmp(target, me.name)) || parc < 4 ||
			strcmp(parv[3], "4"))
			return;
		snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
		unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));
		if (parc == 5 && !strcasecmp(parv[4], "ACK"))
		{
			udb_sync_hello_ack(client);
			return;
		}
		if (parc < 5 || !parv[4] || !*parv[4])
		{
			udb_protocol_params_error(client, "HEL");
			return;
		}
		const char *prop = parv[4];
		int new_auth = 0;
		if (!strcmp(prop, "?"))
		{
			new_auth = 1;
		}
		else if (!strcmp(prop, "-"))
		{
			new_auth = 0;
		}
		else if (udb_server_name_valid(prop))
		{
			new_auth = !strcasecmp(prop, me.name);
		}
		else
		{
			udb_protocol_params_error(client, "HEL");
			return;
		}

		UdbHelloPeer *hello_peer = udb_hello_peer(client, 1);
		int dynamic_transition = (hello_peer->state == UDB_HEL_CONFIRMED && hello_peer->auth_received &&
								  !hello_peer->authorizes_us && new_auth);
		hello_peer->authorizes_us = new_auth;
		hello_peer->auth_received = 1;

		udb_log(ULOG_INFO, "UDB_HEL_AUTHORIZATION", client,
				"Direct peer selected $propagator as its staged-sync source (authorizes_us=$auth)",
				log_data_string("propagator", prop), log_data_integer("auth", new_auth));

		/* Each side sends its own request, so only an ACK confirms outbound data. */
		if (!udb_has_hello(client))
			udb_sync_hello_start(client);
		udb_send_db_to_one(client, ":%s DB %s HEL 4 ACK", me.id, client->id);

		if (dynamic_transition)
		{
			udb_sync_to_server(client);
		}
		return;
	}

	if (!direct_peer || !udb_has_hello(direct_peer))
	{
		udb_send_db_to_one(client, ":%s DB %s ERR %s %d 0", me.id, client->id, subcmd, UDB_ERR_FORBIDDEN);
		return;
	}

	snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
	unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));

	int is_broadcast = !strcmp(target, "*");
	int is_for_me = is_broadcast || !strcmp(target, me.id) || !strcmp(target, me.name);

	if (!is_broadcast && !is_for_me)
		return;

	switch (toupper((unsigned char)subcmd[0]))
	{
	case 'B':
		if (!strcasecmp(subcmd, "BEGIN"))
		{
			UdbBlock *block;
			if (parc < 6 || client != direct_peer || is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer))
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			if (!udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_send_db_to_one(client, ":%s DB %s ERR BEGIN %d %c", me.id, client->id, UDB_ERR_FORBIDDEN,
								   parv[3] ? *parv[3] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[3]);
			if (!block)
			{
				if (is_for_me)
					udb_send_db_to_one(client, ":%s DB %s ERR BEGIN %d %c", me.id, client->id, UDB_ERR_NO_BLOCK,
									   parv[3] ? *parv[3] : '0');
				return;
			}
			if (is_for_me)
			{
				int error = udb_sync_begin(block, direct_peer, parv[4]);
				if (error)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR BEGIN %d %c", me.id, client->id, error,
									   parv[3] ? *parv[3] : '0');
					return;
				}
				if (!is_broadcast)
					return;
			}
			return;
		}
		break;

	case 'P':
		if (!strcasecmp(subcmd, "PUT"))
		{
			UdbBlock *block;
			int error;
			if (parc < 7 || client != direct_peer || is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer))
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			if (!udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_send_db_to_one(client, ":%s DB %s ERR PUT %d %c", me.id, client->id, UDB_ERR_FORBIDDEN,
								   parv[3] ? *parv[3] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[3]);
			if (!block)
			{
				if (is_for_me)
					udb_send_db_to_one(client, ":%s DB %s ERR PUT %d %c", me.id, client->id, UDB_ERR_NO_BLOCK,
									   parv[3] ? *parv[3] : '0');
				return;
			}
			if (is_for_me)
			{
				error = udb_sync_put(block, direct_peer, parv[4], parv[5], parv[6]);
				if (error)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR PUT %d %c", me.id, client->id, error,
									   parv[3] ? *parv[3] : '0');
					return;
				}
				if (!is_broadcast)
					return;
			}
			return;
		}
		break;

	case 'E':
		if (!strcasecmp(subcmd, "END"))
		{
			UdbBlock *block;
			unsigned long digest;
			int error;
			if (parc < 6 || client != direct_peer || is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer))
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			if (!udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_send_db_to_one(client, ":%s DB %s ERR END %d %c", me.id, client->id, UDB_ERR_FORBIDDEN,
								   parv[3] ? *parv[3] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[3]);
			if (!block)
			{
				if (is_for_me)
					udb_send_db_to_one(client, ":%s DB %s ERR END %d %c", me.id, client->id, UDB_ERR_NO_BLOCK,
									   parv[3] ? *parv[3] : '0');
				return;
			}
			if (is_for_me)
			{
				error = udb_sync_end(ctx, block, direct_peer, parv[4], parv[5], &digest);
				if (error)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR END %d %c", me.id, client->id, error,
									   parv[3] ? *parv[3] : '0');
					return;
				}
				udb_send_db_to_one(client, ":%s DB %s ACK %c %s %08lX", me.id, client->id, *parv[3], parv[4], digest);
				udb_sendto_confirmed_servers(client, ":%s DB * INF %c %08lX %lu", me.id, block->letter, block->checksum,
											 (unsigned long)block->modified_at);
				udb_reconcile_record_end(direct_peer, block->letter);
				udb_reconcile_check(ctx);
				if (!is_broadcast)
					return;
			}
			return;
		}
		else if (!strcasecmp(subcmd, "ERR"))
		{
			if (parc < 5)
				return;
			if (is_for_me)
			{
				unsigned long errcode = 0;
				if (!udb_strtoul_strict(parv[4], &errcode) || errcode > 255)
					errcode = UDB_ERR_FATAL;
				udb_log(ULOG_INFO, "UDB_EVENT", client, "Error from $client: cmd=$cmd err=$errcode",
						log_data_client("client", client), log_data_string("cmd", parv[3]),
						log_data_integer("errcode", (int)errcode));
				if (!is_broadcast)
					return;
			}
			if (parc >= 6)
			{
				udb_sendto_confirmed_servers(client, ":%s DB %s ERR %s %s %s", client->id, target, parv[3], parv[4],
											 parv[5]);
			}
			else
			{
				udb_sendto_confirmed_servers(client, ":%s DB %s ERR %s %s", client->id, target, parv[3], parv[4]);
			}
		}
		break;

	case 'A':
		if (!strcasecmp(subcmd, "ACK"))
		{
			if (parc < 6 || client != direct_peer || is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer))
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			if (is_for_me)
			{
				udb_sync_ack(direct_peer, parv[3]);
				if (!is_broadcast)
					return;
			}
			return;
		}
		break;

	case 'I':
		if (!strcasecmp(subcmd, "INF"))
		{
			if (parc < 6)
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			char letter = *parv[3];
			UdbBlock *block = udb_block_by_letter(ctx, letter);
			unsigned long crc32 = 0;
			time_t remote_ts = 0;

			if (!block)
			{
				if (is_for_me)
					udb_send_db_to_one(client, ":%s DB %s ERR INF %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
				return;
			}
			if (!udb_checksum_parse(parv[4], &crc32) || !udb_timestamp_parse(parv[5], &remote_ts))
			{
				udb_protocol_params_error(client, "INF");
				return;
			}

			if (client == direct_peer && is_for_me && udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_reconcile_record_inf(direct_peer, letter, crc32);
				if (crc32 != block->checksum)
				{
					if (remote_ts > block->modified_at)
					{
						udb_send_db_to_one(client, ":%s DB %s RES %c", me.id, client->id, letter);
						udb_reconcile_record_res(direct_peer, letter);
					}
					else if (remote_ts == block->modified_at && udb_remote_wins_equal_timestamp(client))
					{
						udb_send_db_to_one(client, ":%s DB %s RES %c", me.id, client->id, letter);
						udb_reconcile_record_res(direct_peer, letter);
					}
				}
				udb_reconcile_check(ctx);
				if (!is_broadcast)
					return;
			}
			udb_sendto_confirmed_servers(client, ":%s DB %s INF %c %08lX %lu", client->id, target, letter, crc32,
										 (unsigned long)remote_ts);
		}
		else if (!strcasecmp(subcmd, "INS"))
		{
			if (parc < 5)
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			udb_mutation_ins(ctx, client, direct_peer, target, parv[3], parv[4], is_for_me, is_broadcast);
		}
		break;

	case 'R':
		if (!strcasecmp(subcmd, "RES"))
		{
			if (parc < 4)
				return;
			char letter = *parv[3];
			UdbBlock *block = udb_block_by_letter(ctx, letter);
			if (client != direct_peer || is_broadcast || !is_for_me)
			{
				udb_send_db_to_one(client, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
				return;
			}
			if (!udb_peer_authorizes_us(direct_peer))
			{
				udb_send_db_to_one(client, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
				return;
			}

			if (is_for_me)
			{
				if (!block)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
					return;
				}
				if (block->syncing_from && block->syncing_from != direct_peer)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE,
									   letter);
					return;
				}

				udb_sync_send_stage(direct_peer, block);
				block->syncing_from = NULL;

				if (!is_broadcast)
					return;
			}
			return;
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
			udb_mutation_del(ctx, client, direct_peer, target, parv[3], is_for_me, is_broadcast);
		}
		else if (!strcasecmp(subcmd, "DRP"))
		{
			if (parc < 4)
				return;
			udb_mutation_drp(ctx, client, direct_peer, target, *parv[3], is_for_me, is_broadcast);
		}
		break;

	case 'O':
		if (!strcasecmp(subcmd, "OPT"))
		{
			if (parc < 4)
				return;
			udb_mutation_opt(ctx, client, direct_peer, target, *parv[3], parc >= 5 ? parv[4] : NULL, is_for_me,
							 is_broadcast);
		}
		break;
	}
}

static int udb_protocol_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DB", cmd_db, MAXPARA, CMD_SERVER | CMD_BIGLINES);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, udb_hook_server_sync);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, udb_config_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_POSTCONF, 0, udb_config_postconf);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH_COMPLETE, 0, udb_config_postconf);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, udb_hook_server_quit);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, -100, udb_hook_stale_pre_connect);
	EventAdd(modinfo->handle, "udb_sync_timeout", udb_sync_timeout_event, NULL, 1000, 0);

	return 0;
}
