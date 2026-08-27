/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Real-time Database Mutations & Propagator Resolution
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static const char *udb_propagator_policy(UdbContext *ctx)
{
	if (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator)
		return udb_cfg->propagator;
	return ctx && ctx->propagator_setting && *ctx->propagator_setting ? ctx->propagator_setting : NULL;
}

static int udb_propagator_in_policy(UdbContext *ctx, const char *server_name)
{
	const char *cursor = udb_propagator_policy(ctx);
	char name[HOSTLEN + 1];
	int result;

	if (!server_name || !*server_name)
		return 0;
	while (cursor && (result = udb_propagator_list_next(&cursor, name)) > 0)
	{
		if (!strcasecmp(name, server_name))
			return 1;
	}
	return 0;
}

static int udb_propagator_policy_present(UdbContext *ctx)
{
	return udb_propagator_policy(ctx) != NULL;
}

/* Resolve the first locally usable candidate. Network-wide visibility is never
 * enough: a remote candidate must be a direct server peer and staged-sync use
 * additionally requires a completed HEL 4 negotiation. */
static int udb_select_propagator(UdbContext *ctx, int require_hello, UdbPropagatorSelection *selected)
{
	const char *cursor = udb_propagator_policy(ctx);
	char name[HOSTLEN + 1];
	int result;

	if (!selected)
		return 0;
	memset(selected, 0, sizeof(*selected));
	while (cursor && (result = udb_propagator_list_next(&cursor, name)) > 0)
	{
		Client *candidate;

		if (me.name[0] && !strcasecmp(name, me.name))
		{
			memcpy(selected->name, name, strlen(name) + 1);
			selected->is_local = 1;
			return 1;
		}
		candidate = find_server(name, NULL);
		if (!candidate || !IsServer(candidate) || !MyConnect(candidate))
			continue;
		if (require_hello && !udb_has_hello(candidate))
			continue;
		selected->peer = candidate;
		memcpy(selected->name, name, strlen(name) + 1);
		return 1;
	}
	return 0;
}

/* Checks if a directly connected client is our authorized upstream propagator */
static int udb_is_propagator(UdbContext *ctx, Client *server)
{
	UdbPropagatorSelection selected;

	if (!server || !IsServer(server) || !MyConnect(server))
		return 0;
	return udb_select_propagator(ctx, 1, &selected) && !selected.is_local && selected.peer == server;
}

static UdbBlock *udb_mutation_path_block(UdbContext *ctx, const char *path)
{
	const char *component;
	const char *separator;
	size_t len;
	UdbBlock *block;

	if (!path || !udb_record_fits_limits(path, NULL))
		return NULL;

	len = strlen(path);
	if (len < 4 || len > UDB_RECORD_PATH_MAX || path[1] != ':' || path[2] != ':' || !path[3])
		return NULL;

	block = udb_block_by_letter(ctx, path[0]);
	if (!block)
		return NULL;

	component = path + 3;
	while ((separator = strstr(component, "::")))
	{
		if (separator == component || !separator[2])
			return NULL;
		char decoded[UDB_COMPONENT_RAW_MAX + 1];
		char comp_buf[UDB_COMPONENT_ENCODED_MAX + 1];
		size_t clen = separator - component;
		if (clen >= sizeof(comp_buf))
			return NULL;
		memcpy(comp_buf, component, clen);
		comp_buf[clen] = '\0';
		if (!udb_path_decode_component(comp_buf, decoded, sizeof(decoded)))
			return NULL;
		component = separator + 2;
	}
	char decoded[UDB_COMPONENT_RAW_MAX + 1];
	if (!*component || !udb_path_decode_component(component, decoded, sizeof(decoded)))
		return NULL;

	return block;
}

static int udb_mutation_forward_ins(Client *source, Client *except, const char *target, const char *path,
									const char *data)
{
	return udb_sendto_confirmed_servers(except, ":%s DB %s INS %s %s", source->id, target, path, data);
}

/* The stored value already equals the mutation payload, so no runtime effect
 * may change: an identical INS must never revoke and re-apply effects. */
static int udb_record_data_equals(UdbRecord *rec, const char *data)
{
	unsigned long val;

	if (!rec || !data)
		return 0;
	if (*data == '*')
		return !rec->data_str && udb_strtoul_strict(data + 1, &val) && rec->data_num == val;
	if (!rec->data_str)
		return 0;
	return !strcmp(rec->data_str, data);
}

static int udb_mutation_forward_del(Client *source, Client *except, const char *target, const char *path)
{
	return udb_sendto_confirmed_servers(except, ":%s DB %s DEL %s", source->id, target, path);
}

static int udb_mutation_persist_error(Client *client, const char *subcmd, char letter)
{
	return udb_send_db_to_one(client, ":%s DB %s ERR %s %d %c", me.id, client->id, subcmd, UDB_ERR_FATAL, letter);
}

static void udb_mutation_ins(UdbContext *ctx, Client *client, Client *direct_peer, const char *target, const char *path,
							 const char *data, int is_for_me, int is_broadcast)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	char letter = path && *path ? path[0] : '0';

	if (!block || !udb_record_fits_limits(path, data))
	{
		udb_protocol_params_error(client, "INS");
		return;
	}
	if (is_for_me)
	{
		if (block->session || (block->syncing_from && block->syncing_from != direct_peer))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (block->syncing_from != direct_peer && !udb_is_propagator(ctx, direct_peer))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		UdbRecord *old_rec;
		UdbRecord *tree;
		UdbRecord *rec = NULL;
		int unchanged;

		if (!udb_record_validate(block, path + 3, data))
		{
			udb_log(ULOG_WARNING, "UDB_INS_SCHEMA_REJECT", client,
					"Rejected S2S INS for invalid or unknown record: $path", log_data_string("path", path));
			udb_send_db_to_one(client, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_PARAMS, letter);
			return;
		}
		old_rec = udb_record_find_path(ctx, block, path + 3);
		unchanged = old_rec && udb_record_data_equals(old_rec, data);
		if (unchanged)
		{
			if (!is_broadcast)
				return;
			udb_mutation_forward_ins(client, direct_peer, target, path, data);
			return;
		}
		tree = udb_record_clone_tree(block->tree, old_rec, &rec);
		rec = udb_record_insert_path(tree, path + 3, data);
		if (!rec)
		{
			udb_record_free_tree(tree);
			udb_mutation_persist_error(client, "INS", letter);
			return;
		}
		UdbSnapshotResult snap_res = udb_file_write_snapshot(block, tree, udb_record_count_tree(tree));
		if (snap_res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_record_free_tree(tree);
			udb_mutation_persist_error(client, "INS", letter);
			return;
		}
		/* Revoke the old value's effects only when the value actually changes:
		 * an identical INS must not churn channel modes nor revoke +q ranks. */
		if (old_rec && !unchanged)
		{
			if (block->letter != 'C' || old_rec->parent == block->tree ||
				(strcmp(old_rec->key, CKEY_MODES) && strcmp(old_rec->key, CKEY_TOPIC) &&
				 strcmp(old_rec->key, CKEY_OPTIONS)))
				udb_remove_special_record(ctx, block, old_rec);
		}
		udb_block_replace_tree(ctx, block, tree, udb_record_count_tree(tree));
		if (!unchanged)
		{
			udb_apply_special_record(ctx, block, rec, 1);
			if (block->letter == 'L')
				udb_sync_snomask_filter();
		}
		char logbuf[512];
		snprintf(logbuf, sizeof(logbuf), "Inserted record via S2S: %s -> %s", path, data);
		udb_log(ULOG_INFO, "UDB_INS_RECEIVED", client, "$msg", log_data_string("msg", logbuf));
		if (!is_broadcast)
			return;
	}
	udb_mutation_forward_ins(client, direct_peer, target, path, data);
}

static void udb_mutation_del(UdbContext *ctx, Client *client, Client *direct_peer, const char *target, const char *path,
							 int is_for_me, int is_broadcast)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	char letter = path && *path ? path[0] : '0';

	if (!block || !udb_record_fits_limits(path, NULL))
	{
		udb_protocol_params_error(client, "DEL");
		return;
	}
	if (is_for_me)
	{
		if (block->session || (block->syncing_from && block->syncing_from != direct_peer))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (block->syncing_from != direct_peer && !udb_is_propagator(ctx, direct_peer))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		UdbRecord *old_rec = udb_record_find_path(ctx, block, path + 3);
		if (!old_rec)
		{
			if (!is_broadcast)
				return;
			udb_mutation_forward_del(client, direct_peer, target, path);
			return;
		}
		UdbRecord *candidate_rec = NULL;
		UdbRecord *candidate_line = NULL;
		UdbRecord *tree = udb_record_clone_tree(block->tree, old_rec, &candidate_rec);
		unsigned int record_count;
		if (block->letter == 'K' && candidate_rec && candidate_rec->parent && candidate_rec->parent->parent &&
			candidate_rec->parent->parent != tree)
			candidate_line = candidate_rec->parent;
		if (candidate_rec)
			udb_record_delete_tree(candidate_rec);
		record_count = udb_record_count_tree(tree);
		UdbSnapshotResult snap_res = udb_file_write_snapshot(block, tree, record_count);
		if (snap_res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_record_free_tree(tree);
			udb_mutation_persist_error(client, "DEL", letter);
			return;
		}
		if (old_rec)
			udb_remove_special_record(ctx, block, old_rec);
		udb_block_replace_tree(ctx, block, tree, record_count);
		if (candidate_line)
			udb_lines_apply_effect(ctx, block, candidate_line, 0);
		if (block->letter == 'L')
			udb_sync_snomask_filter();
		char logbuf[512];
		snprintf(logbuf, sizeof(logbuf), "Deleted record via S2S: %s", path);
		udb_log(ULOG_INFO, "UDB_DEL_RECEIVED", client, "$msg", log_data_string("msg", logbuf));
		if (!is_broadcast)
			return;
	}
	udb_mutation_forward_del(client, direct_peer, target, path);
}

static void udb_mutation_drp(UdbContext *ctx, Client *client, Client *direct_peer, const char *target, char letter,
							 int is_for_me, int is_broadcast)
{
	if (is_for_me)
	{
		UdbBlock *block = udb_block_by_letter(ctx, letter);
		if (!block)
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
			return;
		}
		if (block->session || (block->syncing_from && block->syncing_from != direct_peer))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (!udb_is_propagator(ctx, direct_peer))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		if (!block->tree->child)
		{
			if (!is_broadcast)
				return;
			udb_sendto_confirmed_servers(direct_peer, ":%s DB %s DRP %c", client->id, target, letter);
			return;
		}
		UdbRecord *tree = udb_record_clone_tree(block->tree, NULL, NULL);
		while (tree->child)
			udb_record_delete_tree(tree->child);
		UdbSnapshotResult snap_res = udb_file_write_snapshot(block, tree, 0);
		if (snap_res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_record_free_tree(tree);
			udb_mutation_persist_error(client, "DRP", letter);
			return;
		}
		/* Reset removes the old block's runtime effects only after persistence. */
		udb_block_reset(ctx, block);
		udb_block_replace_tree(ctx, block, tree, 0);
		if (!is_broadcast)
			return;
	}
	udb_sendto_confirmed_servers(direct_peer, ":%s DB %s DRP %c", client->id, target, letter);
}

static void udb_mutation_opt(UdbContext *ctx, Client *client, Client *direct_peer, const char *target, char letter,
							 const char *modified_at, int is_for_me, int is_broadcast)
{
	if (is_for_me)
	{
		UdbBlock *block = udb_block_by_letter(ctx, letter);
		if (!block)
		{
			udb_send_db_to_one(client, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
			return;
		}
		if (block->session)
		{
			udb_send_db_to_one(client, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (!udb_is_propagator(ctx, direct_peer))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		if (!udb_file_save_block(ctx, block))
		{
			udb_mutation_persist_error(client, "OPT", letter);
			return;
		}
		if (!is_broadcast)
			return;
	}
	if (modified_at)
		udb_sendto_confirmed_servers(direct_peer, ":%s DB %s OPT %c %s", client->id, target, letter, modified_at);
	else
		udb_sendto_confirmed_servers(direct_peer, ":%s DB %s OPT %c", client->id, target, letter);
}
