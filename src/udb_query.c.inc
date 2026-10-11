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

/* Secret classification uses the canonical block/depth/property tuple, never
 * display text or a record's parent pointers. */
static int udb_query_is_secret(const UdbStorageRow *row)
{
	if (!row || !row->depth)
		return 0;
	UdbStorageSpan part = row->components[row->depth - 1];
	return (row->block == 'N' && row->depth == 2 && part.length == sizeof(NKEY_PASS) - 1 &&
			!memcmp(part.data, NKEY_PASS, sizeof(NKEY_PASS) - 1)) ||
		   (row->block == 'S' && row->depth == 1 && part.length == sizeof(SKEY_CRYPT_KEY) - 1 &&
			!memcmp(part.data, SKEY_CRYPT_KEY, sizeof(SKEY_CRYPT_KEY) - 1)) ||
		   (row->block == 'C' && row->depth == 2 && part.length == sizeof(CKEY_MODES) - 1 &&
			!memcmp(part.data, CKEY_MODES, sizeof(CKEY_MODES) - 1));
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

	if (udb_propagator_unavailable_since > 0 && now >= udb_propagator_unavailable_since)
		time_without_propagator = (unsigned long)(now - udb_propagator_unavailable_since);
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
	sendto_one(client, NULL, ":%s 339 %s :Advertised state: HEL " UDB_PROTOCOL_HEL " %s", me.name, client->name,
			   advertised_str);
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

static void udb_query_send_operclasses(Client *client, const char *filter)
{
	UdbOclOrigin *origin;
	int shown = 0;

	if (!udb_ctx)
	{
		sendto_one(client, NULL, ":%s 339 %s :Operclass registry: unavailable", me.name, client->name);
		return;
	}
	sendto_one(client, NULL, ":%s 339 %s :Operclass registry: %s", me.name, client->name,
			   udb_ocl_registry_complete() ? "READY" : "INCOMPLETE");
	sendto_one(client, NULL, ":%s 339 %s :Local inventory: generation %lu epoch %s classes %u digest %s", me.name,
			   client->name, udb_ocl_local ? udb_ocl_local->generation : 0, udb_ocl_local ? udb_ocl_local->epoch : "-",
			   udb_ocl_local ? udb_ocl_local->count : 0, udb_ocl_local ? udb_ocl_local->inventory_digest : "-");
	if (!filter || match_simple(filter, me.name))
	{
		shown++;
		sendto_one(client, NULL, ":%s 339 %s :%s SID %s epoch %s gen %lu classes %u digest %s", me.name, client->name,
				   me.name, me.id, udb_ocl_local ? udb_ocl_local->epoch : "-",
				   udb_ocl_local ? udb_ocl_local->generation : 0, udb_ocl_local ? udb_ocl_local->count : 0,
				   udb_ocl_local ? udb_ocl_local->inventory_digest : "-");
	}
	for (origin = udb_ocl_origins; origin; origin = origin->next)
	{
		Client *acptr = udb_ocl_find_server(origin->sid);
		UdbOclOrigin *current_origin;

		if (!origin->member || !udb_ocl_is_participant(acptr) || IsMe(acptr))
			continue;
		if (filter && !match_simple(filter, acptr->name))
			continue;
		shown++;
		current_origin = udb_ocl_find_origin(acptr->id);
		if (current_origin && current_origin->current)
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s epoch %s gen %lu classes %u digest %s", me.name,
					   client->name, acptr->name, acptr->id, current_origin->current->epoch,
					   current_origin->current->generation, current_origin->current->count,
					   current_origin->current->inventory_digest);
		}
		else
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s MISSING inventory", me.name, client->name, acptr->name,
					   acptr->id);
		}
	}
	if (!shown)
		sendto_one(client, NULL, ":%s 339 %s :No servers match '%s'", me.name, client->name, filter);
}

static void udb_query_send_operclass(Client *client, const char *name)
{
	UdbOclOrigin *origin;
	UdbOclEntry *local_entry;
	int mismatch;

	if (!udb_ctx)
	{
		sendto_one(client, NULL, ":%s 339 %s :Operclass registry: unavailable", me.name, client->name);
		return;
	}
	if (!valid_operclass_name(name))
	{
		sendto_one(client, NULL, ":%s 339 %s :Invalid operclass name '%s'", me.name, client->name, name);
		return;
	}
	if (!udb_ocl_registry_complete())
	{
		sendto_one(client, NULL, ":%s 339 %s :Operclass registry: INCOMPLETE", me.name, client->name);
		return;
	}
	local_entry = udb_ocl_local ? udb_ocl_find_entry(udb_ocl_local, name) : NULL;
	if (!local_entry)
	{
		sendto_one(client, NULL, ":%s 339 %s :Operclass %s: NOT GLOBAL (missing locally)", me.name, client->name, name);
		return;
	}
	mismatch = 0;
	for (origin = udb_ocl_origins; origin; origin = origin->next)
	{
		Client *acptr = udb_ocl_find_server(origin->sid);
		UdbOclEntry *entry;

		if (!origin->member || !udb_ocl_is_participant(acptr) || IsMe(acptr))
			continue;
		entry = (origin && origin->current) ? udb_ocl_find_entry(origin->current, name) : NULL;
		if (!entry)
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s MISSING", me.name, client->name, acptr->name, acptr->id);
			mismatch = 1;
		}
		else if (strcmp(entry->digest, local_entry->digest))
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s MISMATCH %s", me.name, client->name, acptr->name, acptr->id,
					   entry->digest);
			mismatch = 1;
		}
		else
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s OK %s", me.name, client->name, acptr->name, acptr->id,
					   entry->digest);
		}
	}
	sendto_one(client, NULL, ":%s 339 %s :Operclass %s: %s %s", me.name, client->name, name,
			   mismatch ? "NOT GLOBAL" : "GLOBAL", local_entry->digest);
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

	if (!strcasecmp(parv[1], "OPERCLASSES"))
	{
		udb_query_send_operclasses(client, parc >= 3 ? parv[2] : NULL);
		return;
	}

	if (!strcasecmp(parv[1], "OPERCLASS"))
	{
		if (parc < 3)
			sendto_one(client, NULL, ":%s 339 %s :Syntax: /UDB OPERCLASS <name>", me.name, client->name);
		else
			udb_query_send_operclass(client, parv[2]);
		return;
	}

	sendto_one(client, NULL, ":%s 339 %s :Syntax: /UDB STATUS | /UDB OPERCLASSES [filter] | /UDB OPERCLASS <name>",
			   me.name, client->name);
}

#define UDB_QUERY_ROWS 64
#define UDB_QUERY_BYTES ((size_t)32 * 1024)

typedef struct UdbQueryScratch
{
	UdbNativeKey key;
	UdbStorageField field;
	char path[UDB_STORAGE_PATH_MAX * 3 + 1];
	char value[UDB_STORAGE_VALUE_MAX + 1];
} UdbQueryScratch;

static UdbStorageResult udb_query_send_row(Client *client, const UdbStorageRow *row, UdbQueryScratch *scratch,
										   size_t *bytes)
{
	UdbStorageResult result = udb_native_key_path(row, scratch->path, sizeof(scratch->path));
	const char *value = scratch->value;
	if (result != UDB_STORAGE_OK)
		return result;
	if (udb_query_is_secret(row))
		value = "<redacted>";
	else if (row->kind == UDB_STORAGE_VALUE_STRING)
	{
		memcpy(scratch->value, row->value.data, row->value.length);
		scratch->value[row->value.length] = 0;
	}
	else if (row->kind == UDB_STORAGE_VALUE_NUMBER)
		snprintf(scratch->value, sizeof(scratch->value), "%" PRIu64, row->number);
	else
		value = "(empty)";
	size_t size = strlen(scratch->path) + strlen(value) + UDB_S2S_OVERHEAD_MAX;
	if (size > UDB_S2S_LINE_MAX || size > UDB_QUERY_BYTES - *bytes)
		return UDB_STORAGE_LIMIT;
	*bytes += size;
	sendto_one(client, NULL, ":%s 339 %s :DBQ %s %s", me.name, client->name, scratch->path, value);
	return UDB_STORAGE_OK;
}

CMD_FUNC(cmd_dbq)
{
	const char *query;
	UdbStorageRead *read = NULL;
	UdbStorageBlockIterator *iterator = NULL;
	UdbQueryScratch *scratch = NULL;
	UdbStorageResult result;
	UdbStorageRecord record;
	size_t bytes = 0;
	unsigned int shown = 0;
	int index;

	if (!IsUser(client) && !IsServer(client))
		return;
	if (IsUser(client) && !IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}
	if (parc < 2)
	{
		sendto_one(client, NULL, ":%s 339 %s :Syntax: /DBQ [server] <block>[::path]", me.name, client->name);
		return;
	}
	if (parc >= 3 && !match_simple(parv[1], me.name))
	{
		Client *server = find_server_quick(parv[1]);
		if (server)
			sendto_one(server, NULL, ":%s DBQ %s %s", client->id, parv[1], parv[2]);
		else
			sendnumeric(client, ERR_NOSUCHSERVER, parv[1]);
		return;
	}
	query = parv[parc >= 3 ? 2 : 1];
	if (!strcasecmp(query, "STATUS"))
	{
		udb_query_send_status(client);
		return;
	}
	index = *query ? udb_storage_stage_block_index((unsigned char)*query) : -1;
	if (index < 0)
	{
		sendto_one(client, NULL, ":%s 339 %s :Invalid block", me.name, client->name);
		return;
	}
	if (!udb_ctx || !udb_ctx->storage)
	{
		sendto_one(client, NULL, ":%s 339 %s :Native database unavailable", me.name, client->name);
		return;
	}
	result = udb_storage_read_open(udb_ctx->storage, &read);
	if (result != UDB_STORAGE_OK)
		goto finish;
	if (!query[1])
	{
		const UdbStorageBlockRoot *root = read->generation->state.blocks + index;
		char digest[65];
		for (unsigned int i = 0; i < 32; i++)
			snprintf(digest + 2 * i, 3, "%02x", root->summary.digest[i]);
		sendto_one(client, NULL, ":%s 339 %s :%c rows=%" PRIu64 " generation=%" PRIu64 " watermark=%" PRIu64 " %s",
				   me.name, client->name, root->block, root->summary.count, read->generation->state.generation,
				   read->generation->state.source_sequence, digest);
		goto finish;
	}
	scratch = udb_storage_alloc(&read->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
	{
		result = UDB_STORAGE_LIMIT;
		goto finish;
	}
	result = udb_native_key_parse(query, &scratch->key);
	if (result != UDB_STORAGE_OK)
		goto finish;
	result = udb_storage_read_field(read, &scratch->key.row, &scratch->field);
	if (result < UDB_STORAGE_NOT_FOUND)
		goto finish;
	if (result == UDB_STORAGE_OK && scratch->field.kind != UDB_STORAGE_VALUE_NONE)
	{
		UdbStorageRow row = scratch->key.row;
		row.kind = scratch->field.kind;
		row.number = scratch->field.number;
		row.value.data = (const unsigned char *)scratch->field.string;
		row.value.length = scratch->field.kind == UDB_STORAGE_VALUE_STRING ? scratch->field.length : 0;
		result = udb_query_send_row(client, &row, scratch, &bytes);
		goto finish;
	}
	result = udb_storage_read_iterator(read, scratch->key.row.block, &scratch->key.row, &iterator);
	if (result != UDB_STORAGE_OK)
		goto finish;
	while ((result = udb_storage_block_iterator_next(iterator, &record)) == UDB_STORAGE_OK)
	{
		if (shown == UDB_QUERY_ROWS)
		{
			result = UDB_STORAGE_LIMIT;
			break;
		}
		result = udb_query_send_row(client, &record.row, scratch, &bytes);
		if (result != UDB_STORAGE_OK)
			break;
		shown++;
	}
	if (result == UDB_STORAGE_NOT_FOUND && shown)
		result = UDB_STORAGE_OK;
finish:
	if (iterator)
	{
		UdbStorageResult closed = udb_storage_block_iterator_close(&iterator);
		if (closed != UDB_STORAGE_OK)
			result = closed;
	}
	udb_storage_free(scratch);
	if (read)
	{
		UdbStorageResult closed = udb_storage_read_close(&read);
		if (closed != UDB_STORAGE_OK)
			result = closed;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		sendto_one(client, NULL, ":%s 339 %s :No records found", me.name, client->name);
	else if (result == UDB_STORAGE_LIMIT)
		sendto_one(client, NULL, ":%s 339 %s :Query limit reached; narrow the prefix (maximum 64 rows / 32 KiB)",
				   me.name, client->name);
	else if (result != UDB_STORAGE_OK)
		sendto_one(client, NULL, ":%s 339 %s :Database query failed (%d); results may be incomplete", me.name,
				   client->name, (int)result);
}

static void udb_query_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DBQ", cmd_dbq, MAXPARA, CMD_USER | CMD_SERVER);
	CommandAdd(modinfo->handle, "UDB", cmd_udb, MAXPARA, CMD_USER | CMD_SERVER);
}
