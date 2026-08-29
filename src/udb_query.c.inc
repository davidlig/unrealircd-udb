/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: DBQ Query Command & Secret Redaction
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static int udb_query_is_secret(const UdbRecord *rec)
{
	return rec && rec->key &&
		   (!strcmp(rec->key, NKEY_PASS) || !strcmp(rec->key, NKEY_CHALLENGE) || !strcmp(rec->key, SKEY_CRYPT_KEY));
}

static void udb_query_send_status(Client *client)
{
	UdbContext *ctx = udb_ctx;
	UdbPropagatorSelection selected;
	int has_selected = ctx ? udb_select_propagator(ctx, 1, &selected) : 0;
	int policy_present = udb_propagator_policy_present(ctx);
	const char *sync_status_str;
	const char *selected_str;
	const char *advertised_str;
	const char *policy_source_str;
	const char *policy_str = udb_propagator_policy(ctx);
	const char *clients_str;
	unsigned long time_without_propagator = 0;
	time_t now = time(NULL);

	switch (udb_sync_status)
	{
	case UDB_SYNC_OK:
		sync_status_str = "OK";
		break;
	case UDB_SYNC_DEGRADED:
		sync_status_str = "DEGRADED";
		break;
	case UDB_SYNC_STALE:
		sync_status_str = "STALE";
		break;
	default:
		sync_status_str = "UNKNOWN";
		break;
	}

	if (has_selected)
		selected_str = selected.name;
	else
		selected_str = "none";

	if (has_selected)
		advertised_str = selected.name;
	else if (policy_present)
		advertised_str = "-";
	else
		advertised_str = "?";

	if (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator)
		policy_source_str = "local";
	else if (ctx && ctx->propagator_setting && *ctx->propagator_setting)
		policy_source_str = "S";
	else
		policy_source_str = "none";

	if (!policy_str || !*policy_str)
		policy_str = "none";

	if (udb_degraded_since > 0 && now >= udb_degraded_since)
		time_without_propagator = (unsigned long)(now - udb_degraded_since);
	else
		time_without_propagator = 0;

	if (!udb_ready)
		clients_str = "DENIED";
	else
		clients_str = "ALLOWED";

	sendto_one(client, NULL, ":%s 339 %s :Database readiness: %s", me.name, client->name,
			   udb_ready ? "READY" : "BOOTSTRAPPING");
	sendto_one(client, NULL, ":%s 339 %s :UDB synchronization: %s", me.name, client->name, sync_status_str);
	sendto_one(client, NULL, ":%s 339 %s :Recovery: %s", me.name, client->name,
			   (udb_reconcile.active || udb_reconcile.next_retry_at) ? "ACTIVE" : "IDLE");
	sendto_one(client, NULL, ":%s 339 %s :Selected propagator: %s", me.name, client->name, selected_str);
	sendto_one(client, NULL, ":%s 339 %s :Selected direct source: %s", me.name, client->name, selected_str);
	sendto_one(client, NULL, ":%s 339 %s :Advertised state: HEL 4 %s", me.name, client->name, advertised_str);
	sendto_one(client, NULL, ":%s 339 %s :Serving downstream: %s", me.name, client->name, udb_ready ? "YES" : "NO");
	sendto_one(client, NULL, ":%s 339 %s :Policy source: %s", me.name, client->name, policy_source_str);
	sendto_one(client, NULL, ":%s 339 %s :Policy: %s", me.name, client->name, policy_str);
	sendto_one(client, NULL, ":%s 339 %s :Configured authority: %s", me.name, client->name, policy_str);
	sendto_one(client, NULL, ":%s 339 %s :Time without propagator: %lu", me.name, client->name,
			   time_without_propagator);
	sendto_one(client, NULL, ":%s 339 %s :New local clients: %s", me.name, client->name, clients_str);

	if (udb_last_successful_sync > 0)
	{
		char timebuf[64];
		snprintf(timebuf, sizeof(timebuf), "%lu", (unsigned long)udb_last_successful_sync);
		sendto_one(client, NULL, ":%s 339 %s :Last successful synchronization: %s", me.name, client->name, timebuf);
	}
	else
	{
		sendto_one(client, NULL, ":%s 339 %s :Last successful synchronization: none", me.name, client->name);
	}
}

CMD_FUNC(cmd_udb)
{
	if (!IsUser(client) && !IsServer(client))
		return;

	if (IsUser(client) && !IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if (parc < 2 || !strcasecmp(parv[1], "STATUS"))
	{
		udb_query_send_status(client);
		return;
	}

	sendto_one(client, NULL, ":%s 339 %s :Syntax: /UDB STATUS", me.name, client->name);
}

CMD_FUNC(cmd_dbq)
{
	char *query_str = NULL;
	char *cur, *ds;
	UdbBlock *block;
	UdbRecord *rec;

	if (!IsUser(client) && !IsServer(client))
		return;

	if (IsUser(client) && !IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if (parc < 2)
	{
		sendto_one(client, NULL, ":%s 339 %s :Insufficient parameters. Syntax: /DBQ [server] <block>[::path]", me.name,
				   client->name);
		return;
	}

	if (parc >= 3)
	{
		if (!match_simple(parv[1], me.name))
		{
			Client *target_server = find_server_quick(parv[1]);
			if (target_server)
				sendto_one(target_server, NULL, ":%s DBQ %s %s", client->id, parv[1], parv[2]);
			else
				sendnumeric(client, ERR_NOSUCHSERVER, parv[1]);
			return;
		}
		safe_strdup(query_str, parv[2]);
	}
	else
	{
		safe_strdup(query_str, parv[1]);
	}

	if (!strcasecmp(query_str, "STATUS"))
	{
		udb_query_send_status(client);
		safe_free(query_str);
		return;
	}

	block = udb_block_by_letter(udb_ctx, query_str[0]);
	if (!block)
	{
		sendto_one(client, NULL, ":%s 339 %s :Block %c does not exist.", me.name, client->name, query_str[0]);
		safe_free(query_str);
		return;
	}

	/* Query for block summary only (e.g. "/DBQ N") */
	if (query_str[1] == '\0')
	{
		sendto_one(client, NULL, ":%s 339 %s :%c %u %lu %lu %lX %s", me.name, client->name, block->letter,
				   block->record_count, block->filesize, (unsigned long)block->modified_at, block->checksum,
				   block->syncing_from ? "*" : "");
		safe_free(query_str);
		return;
	}

	/* Parse path (e.g. "N::davidlig::vhost") */
	cur = query_str + 1;
	if (cur[0] != ':' || cur[1] != ':' || cur[2] == '\0')
	{
		sendto_one(client, NULL, ":%s 339 %s :Invalid block format.", me.name, client->name);
		safe_free(query_str);
		return;
	}
	cur += 2;

	rec = block->tree;
	while ((ds = strstr(cur, "::")))
	{
		*ds = '\0';
		rec = udb_record_find(udb_ctx, cur, rec);
		if (!rec)
			goto notfound;
		cur = ds + 2;
	}
	rec = udb_record_find(udb_ctx, cur, rec);

	if (!rec)
	{
	notfound:
		sendto_one(client, NULL, ":%s 339 %s :Block not found: %s", me.name, client->name, query_str);
		safe_free(query_str);
		return;
	}

	/* Display the found record */
	if (rec->data_str)
	{
		sendto_one(client, NULL, ":%s 339 %s :DBQ %s %s", me.name, client->name, query_str,
				   udb_query_is_secret(rec) ? "<redacted>" : rec->data_str);
	}
	else if (rec->data_num)
	{
		sendto_one(client, NULL, ":%s 339 %s :DBQ %s %lu", me.name, client->name, query_str, rec->data_num);
	}
	else
	{
		UdbRecord *child;
		for (child = rec->child; child; child = child->sibling)
		{
			if (child->data_str)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s %s", me.name, client->name, query_str, child->key,
						   udb_query_is_secret(child) ? "<redacted>" : child->data_str);
			else if (child->data_num)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s %lu", me.name, client->name, query_str, child->key,
						   child->data_num);
			else if (child->child)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s (has sub-records)", me.name, client->name, query_str,
						   child->key);
			else
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s (empty)", me.name, client->name, query_str,
						   child->key);
		}
	}

	safe_free(query_str);
}

static void udb_query_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DBQ", cmd_dbq, MAXPARA, CMD_USER | CMD_SERVER);
	CommandAdd(modinfo->handle, "UDB", cmd_udb, MAXPARA, CMD_USER | CMD_SERVER);
}
