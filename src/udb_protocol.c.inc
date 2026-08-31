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

static unsigned long udb_error_correlation_sequence = 0;

static unsigned long udb_protocol_next_error_correlation(void)
{
	if (!++udb_error_correlation_sequence)
		++udb_error_correlation_sequence;
	return udb_error_correlation_sequence;
}

static void udb_protocol_round_error(Client *client, const char *subcmd, int error, unsigned long round_id, char letter)
{
	if (!client || !subcmd || !round_id)
		return;
	udb_send_db_to_one(client, ":%s DB %s ERR %s %d %lu %c", me.id, client->id, subcmd, error, round_id,
					   letter ? letter : '0');
}

static void udb_protocol_mutation_error(Client *client, const char *subcmd, int error, char letter)
{
	udb_protocol_round_error(client, subcmd, error, udb_protocol_next_error_correlation(), letter);
}

static int udb_protocol_error_affects_reconciliation(const char *subcmd)
{
	return subcmd && (!strcasecmp(subcmd, "INF") || !strcasecmp(subcmd, "RES") || !strcasecmp(subcmd, "BEGIN") ||
					  !strcasecmp(subcmd, "PUT") || !strcasecmp(subcmd, "END"));
}

static int udb_sync_to_server(Client *server)
{
	UdbBlock *block = udb_ctx->block_list;
	static unsigned long round_sequence = 0;
	unsigned long round_id = (unsigned long)time(NULL) + ++round_sequence;
	if (!round_id)
		round_id = ++round_sequence;
	if (!udb_has_hello(server))
		return 0;
	while (block)
	{
		if (!udb_send_db_to_one(server, ":%s DB %s INF %lu %c %lX %lu", me.id, server->id, round_id, block->letter,
								block->checksum, (unsigned long)block->modified_at))
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

static int udb_hook_server_sync(Client *client)
{
	if (!client || !IsServer(client) || !MyConnect(client))
		return 0;
	udb_sync_hello_start(client);
	return 0;
}

static int udb_hook_server_connect(Client *client)
{
	if (!client || !IsServer(client))
		return 0;
	/* The OCL participant set follows the full server list, including servers
	 * that arrive remotely during a netburst. */
	udb_ocl_membership_changed();
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
		return;

	const char *target = parv[1];
	const char *subcmd = parv[2];
	UdbContext *ctx = udb_ctx;
	Client *direct_peer = udb_direct_peer(client);
	char logbuf[512];

	if (!target || !*target || !subcmd || !*subcmd)
		return;

	/* HEL is the sole DB frame accepted before UDB capability confirmation. */
	if (!strcasecmp(subcmd, "HEL"))
	{
		if (!IsServer(client) || !MyConnect(client) || (strcmp(target, me.id) && strcmp(target, me.name)) || parc < 4 ||
			strcmp(parv[3], "4"))
			return;
		snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
		unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));
		if (parc == 6 && !strcasecmp(parv[4], "ACK") && !strcasecmp(parv[5], "OCL"))
		{
			udb_sync_hello_ack(client);
			return;
		}
		/* OCL is part of the mandatory HEL 4 contract: a request without the
		 * OCL token belongs to a legacy peer that can never be confirmed. */
		if (parc == 5 || (parc >= 6 && strcasecmp(parv[5], "OCL")))
		{
			udb_log(ULOG_ERROR, "UDB_HEL_OCL_REQUIRED", client,
					"Link aborted: HEL 4 request without the mandatory OCL capability token");
			exit_client_fmt(client, NULL, "Link aborted: server does not support UDB OCL capability");
			return;
		}
		if (parc > 7 || (parc == 7 && strcasecmp(parv[6], "OCLG")))
			return;
		if (parc < 6 || !parv[4] || !*parv[4])
			return;
		{
			const char *prop = parv[4];
			int oclg_subscribe = (parc == 7 && !strcasecmp(parv[6], "OCLG"));
			int new_auth = 0;
			UdbHelloPeer *hello_peer;

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
				return;

			hello_peer = udb_hello_peer(client, 1);
			hello_peer->authorizes_us = new_auth;
			hello_peer->auth_received = 1;
			hello_peer->ocl_capable = 1;
			hello_peer->oclg_subscribed = oclg_subscribe;

			udb_log(ULOG_INFO, "UDB_HEL_AUTHORIZATION", client,
					"Direct peer selected $propagator as its staged-sync source (authorizes_us=$auth, oclg=$oclg)",
					log_data_string("propagator", prop), log_data_integer("auth", new_auth),
					log_data_integer("oclg", oclg_subscribe));

			/* Each side sends its own request, so only an ACK confirms outbound data. */
			if (!udb_has_hello(client))
				udb_sync_hello_start(client);
			udb_send_db_to_one(client, ":%s DB %s HEL 4 ACK OCL", me.id, client->id);
			udb_ocl_maybe_replay_to_peer(client);
			if (oclg_subscribe && udb_has_hello(client))
				udb_oclg_push(client);
			/* A repeated authorized selection is also the explicit retry trigger. */
			udb_maybe_start_reconciliation(client, new_auth);
		}
		return;
	}

	if (!direct_peer || !udb_has_hello(direct_peer))
		return;

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
			unsigned long round_id = 0;
			if (parc != 7 || !udb_strtoul_strict(parv[3], &round_id) || !round_id || client != direct_peer ||
				is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer))
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (!udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_protocol_round_error(client, "BEGIN", UDB_ERR_FORBIDDEN, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[4]);
			if (!block)
			{
				if (is_for_me)
					udb_protocol_round_error(client, "BEGIN", UDB_ERR_NO_BLOCK, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (is_for_me)
			{
				if (block->session)
				{
					udb_protocol_round_error(client, "BEGIN", UDB_ERR_SYNC_ACTIVE, round_id, parv[4] ? *parv[4] : '0');
					return;
				}
				if (block->pending_from != direct_peer || block->pending_round_id != round_id)
				{
					udb_protocol_round_error(client, "BEGIN", UDB_ERR_NO_SYNC, round_id, parv[4] ? *parv[4] : '0');
					return;
				}
				int error = udb_sync_begin(block, direct_peer, round_id, parv[5]);
				if (error)
				{
					udb_protocol_round_error(client, "BEGIN", error, round_id, parv[4] ? *parv[4] : '0');
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
			unsigned long round_id = 0;
			if (parc != 8 || !udb_strtoul_strict(parv[3], &round_id) || !round_id || client != direct_peer ||
				is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer))
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (!udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_protocol_round_error(client, "PUT", UDB_ERR_FORBIDDEN, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[4]);
			if (!block)
			{
				if (is_for_me)
					udb_protocol_round_error(client, "PUT", UDB_ERR_NO_BLOCK, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (is_for_me)
			{
				error = udb_sync_put(block, direct_peer, round_id, parv[5], parv[6], parv[7]);
				if (error)
				{
					udb_protocol_round_error(client, "PUT", error, round_id, parv[4] ? *parv[4] : '0');
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
			unsigned long round_id = 0;
			if (parc != 7 || !udb_strtoul_strict(parv[3], &round_id) || !round_id || client != direct_peer ||
				is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer))
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (!udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_protocol_round_error(client, "END", UDB_ERR_FORBIDDEN, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[4]);
			if (!block)
			{
				if (is_for_me)
					udb_protocol_round_error(client, "END", UDB_ERR_NO_BLOCK, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (is_for_me)
			{
				unsigned long session_round = block->session ? block->session->round_id : 0;
				error = udb_sync_end(ctx, block, direct_peer, round_id, parv[5], parv[6], &digest);
				if (error)
				{
					udb_protocol_round_error(client, "END", error, round_id, parv[4] ? *parv[4] : '0');
					return;
				}
				udb_send_db_to_one(client, ":%s DB %s ACK %lu %c %s %08lX", me.id, client->id, round_id, *parv[4],
								   parv[5], digest);
				udb_reconcile_record_end(direct_peer, block->letter, session_round);
				if (udb_reconcile_check(ctx))
					udb_offer_reconciliation_to_downstreams();
				if (!is_broadcast)
					return;
			}
			return;
		}
		else if (!strcasecmp(subcmd, "ERR"))
		{
			unsigned long errcode;
			unsigned long error_round;
			UdbBlock *eblock;
			int active_authority_matches;

			if (parc != 7 || !udb_strtoul_strict(parv[4], &errcode) || errcode > 255 ||
				!udb_strtoul_strict(parv[5], &error_round) || !error_round || !parv[6] || !*parv[6] || parv[6][1])
				return;
			if (is_for_me)
			{
				udb_log(ULOG_INFO, "UDB_EVENT", client, "Error from $client: cmd=$cmd err=$errcode",
						log_data_client("client", client), log_data_string("cmd", parv[3]),
						log_data_integer("errcode", (int)errcode));

				eblock = udb_block_by_letter(ctx, *parv[6]);
				active_authority_matches = client == direct_peer && !is_broadcast && eblock &&
										   udb_protocol_error_affects_reconciliation(parv[3]) && udb_reconcile.active &&
										   udb_reconcile.authority_peer == direct_peer &&
										   udb_reconcile.round_id == error_round;
				if (!active_authority_matches)
				{
					udb_log(ULOG_WARNING, "UDB_STALE_ERR_IGNORED", client,
							"Ignored ERR for stale round $round (active round is $active_round)",
							log_data_integer("round", (int)error_round),
							log_data_integer("active_round", (int)udb_reconcile.round_id));
					return;
				}
				udb_reconcile_abort(ctx, "authority returned ERR", 1);
				if (!is_broadcast)
					return;
			}
		}
		break;

	case 'A':
		if (!strcasecmp(subcmd, "ACK"))
		{
			unsigned long round_id = 0;
			if (parc != 7 || !udb_strtoul_strict(parv[3], &round_id) || !round_id || client != direct_peer ||
				is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer))
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (is_for_me)
			{
				udb_sync_ack(direct_peer, parv[4]);
				if (!is_broadcast)
					return;
			}
			return;
		}
		break;

	case 'I':
		if (!strcasecmp(subcmd, "INF"))
		{
			unsigned long round_id = 0;
			if (parc != 7 || !udb_strtoul_strict(parv[3], &round_id) || !round_id)
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			char letter = *parv[4];
			UdbBlock *block = udb_block_by_letter(ctx, letter);
			unsigned long crc32 = 0;
			time_t remote_ts = 0;

			if (!block)
			{
				if (is_for_me)
					udb_protocol_round_error(client, "INF", UDB_ERR_NO_BLOCK, round_id, letter);
				return;
			}
			if (!udb_checksum_parse(parv[5], &crc32) || !udb_timestamp_parse(parv[6], &remote_ts))
			{
				udb_protocol_round_error(client, "INF", UDB_ERR_PARAMS, round_id, letter);
				return;
			}
			(void)remote_ts;

			if (client == direct_peer && is_for_me && udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_reconcile_record_inf(direct_peer, round_id, letter, crc32);
				if (!udb_reconcile.active || udb_reconcile.round_id != round_id)
					return;
				if (crc32 != block->checksum)
				{
					if (udb_send_db_to_one(client, ":%s DB %s RES %lu %c", me.id, client->id, round_id, letter))
					{
						udb_reconcile_record_res(direct_peer, round_id, letter);
						block->pending_from = direct_peer;
						block->pending_deadline = time(NULL) + ((udb_cfg && udb_cfg->sync_inactivity_timeout > 0)
																	? udb_cfg->sync_inactivity_timeout
																	: UDB_SYNC_TIMEOUT);
						block->pending_round_id = udb_reconcile.round_id;
					}
				}
			}
			udb_reconcile_check(ctx);
			if (!is_broadcast)
				return;
		}
		/* Reconciliation inventory is hop-by-hop and is never forwarded. */
		else if (!strcasecmp(subcmd, "INS"))
		{
			if (parc < 5)
			{
				udb_protocol_mutation_error(client, subcmd, UDB_ERR_PARAMS, '0');
				return;
			}
			udb_mutation_ins(ctx, client, direct_peer, target, parv[3], parv[4], is_for_me, is_broadcast);
		}
		break;

	case 'R':
		if (!strcasecmp(subcmd, "RES"))
		{
			unsigned long round_id = 0;
			if (parc != 5 || !udb_strtoul_strict(parv[3], &round_id) || !round_id)
				return;
			char letter = *parv[4];
			UdbBlock *block = udb_block_by_letter(ctx, letter);
			if (client != direct_peer || is_broadcast || !is_for_me || !udb_ready)
			{
				udb_protocol_round_error(client, "RES", UDB_ERR_FORBIDDEN, round_id, letter);
				return;
			}
			if (!udb_peer_authorizes_us(direct_peer))
			{
				udb_protocol_round_error(client, "RES", UDB_ERR_FORBIDDEN, round_id, letter);
				return;
			}

			if (is_for_me)
			{
				if (!block)
				{
					udb_protocol_round_error(client, "RES", UDB_ERR_NO_BLOCK, round_id, letter);
					return;
				}
				if (block->session || (block->syncing_from && block->syncing_from != direct_peer))
				{
					udb_protocol_round_error(client, "RES", UDB_ERR_SYNC_ACTIVE, round_id, letter);
					return;
				}

				if (!udb_sync_send_stage(direct_peer, block, round_id))
					udb_protocol_round_error(client, "RES", UDB_ERR_FATAL, round_id, letter);

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
				udb_protocol_mutation_error(client, subcmd, UDB_ERR_PARAMS, '0');
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
		else if (!strcasecmp(subcmd, "OCL"))
		{
			udb_ocl_handle(client, direct_peer, parc, parv, is_broadcast);
		}
		else if (!strcasecmp(subcmd, "OCLG"))
		{
			udb_oclg_handle(client, is_for_me);
		}
		break;
	}
}

static int udb_protocol_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DB", cmd_db, MAXPARA, CMD_SERVER | CMD_BIGLINES);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, udb_hook_server_sync);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_CONNECT, 0, udb_hook_server_connect);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, udb_config_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_POSTCONF, 0, udb_config_postconf);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH_COMPLETE, 0, udb_config_postconf);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, udb_hook_server_quit);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, -100, udb_hook_readiness_pre_connect);
	EventAdd(modinfo->handle, "udb_sync_timeout", udb_sync_timeout_event, NULL, 1000, 0);

	return 0;
}
