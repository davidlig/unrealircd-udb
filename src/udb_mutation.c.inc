/* UDB - authorized real-time database mutations. */

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

static UdbBlock *udb_mutation_path_block(UdbContext *ctx, const char *path)
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

static void udb_mutation_forward_ins(Client *source, Client *except, const char *target,
                                     const char *path, const char *data)
{
	udb_sendto_confirmed_servers(except, ":%s DB %s INS %s %s", source->id, target, path, data);
}

/* The stored value already equals the mutation payload, so no runtime effect
 * may change: an identical INS must never revoke and re-apply effects. */
static int udb_record_data_equals(UdbRecord *rec, const char *data)
{
	if (!rec || !data)
		return 0;
	if (*data == '*')
		return !rec->data_str && rec->data_num == strtoul(data + 1, NULL, 10);
	if (!rec->data_str)
		return 0;
	return !strcmp(rec->data_str, data);
}

static void udb_mutation_forward_del(Client *source, Client *except, const char *target,
                                     const char *path)
{
	udb_sendto_confirmed_servers(except, ":%s DB %s DEL %s", source->id, target, path);
}

static void udb_mutation_persist_error(Client *client, const char *subcmd, char letter)
{
	sendto_one(client, NULL, ":%s DB %s ERR %s %d %c", me.id, client->id,
	           subcmd, UDB_ERR_FATAL, letter);
}

static void udb_mutation_ins(UdbContext *ctx, Client *client, const char *target,
                             const char *path, const char *data, int is_for_me, int is_broadcast)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	char letter = path && *path ? path[0] : '0';

	if (!block)
	{
		udb_protocol_params_error(client, "INS");
		return;
	}
	if (is_for_me)
	{
		if (block->session || (block->syncing_from && block->syncing_from != client))
		{
			sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (block->syncing_from != client && !udb_is_propagator(ctx, client))
		{
			sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		UdbRecord *old_rec;
		UdbRecord *tree;
		UdbRecord *rec = NULL;
		int unchanged;

		if (!udb_record_validate(block, path + 3, data))
		{
			udb_protocol_params_error(client, "INS");
			return;
		}
		old_rec = udb_record_find_path(ctx, block, path + 3);
		unchanged = old_rec && udb_record_data_equals(old_rec, data);
		tree = udb_record_clone_tree(block->tree, old_rec, &rec);
		rec = udb_record_insert_path(tree, path + 3, data);
		if (!rec)
		{
			udb_record_free_tree(tree);
			udb_mutation_persist_error(client, "INS", letter);
			return;
		}
		if (!udb_file_write_snapshot(block, tree, udb_record_count_tree(tree)))
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
			    (strcmp(old_rec->key, CKEY_MODES) && strcmp(old_rec->key, CKEY_TOPIC)))
				udb_remove_special_record(ctx, block, old_rec);
		}
		udb_block_replace_tree(ctx, block, tree, udb_record_count_tree(tree));
		if (!unchanged)
			udb_apply_special_record(ctx, block, rec, 1);
		char logbuf[512];
		snprintf(logbuf, sizeof(logbuf), "[UDB] Inserted record via S2S: %s -> %s", path, data);
		unreal_log(ULOG_INFO, "udb", "UDB_INS_RECEIVED", client, "$msg", log_data_string("msg", logbuf));
		if (ctx->propagator && block->syncing_from == client)
		{
			udb_mutation_forward_ins(ctx->propagator, client, target, path, data);
			return;
		}
		if (!is_broadcast)
			return;
	}
	udb_mutation_forward_ins(client, client, target, path, data);
}

static void udb_mutation_del(UdbContext *ctx, Client *client, const char *target,
                             const char *path, int is_for_me, int is_broadcast)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	char letter = path && *path ? path[0] : '0';

	if (!block)
	{
		udb_protocol_params_error(client, "DEL");
		return;
	}
	if (is_for_me)
	{
		if (block->session || (block->syncing_from && block->syncing_from != client))
		{
			sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (!block->syncing_from && !udb_is_propagator(ctx, client))
		{
			sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		UdbRecord *old_rec = udb_record_find_path(ctx, block, path + 3);
		UdbRecord *candidate_rec = NULL;
		UdbRecord *candidate_line = NULL;
		UdbRecord *tree = udb_record_clone_tree(block->tree, old_rec, &candidate_rec);
		unsigned int record_count;
		if (block->letter == 'K' && candidate_rec && candidate_rec->parent &&
		    candidate_rec->parent->parent && candidate_rec->parent->parent != tree)
			candidate_line = candidate_rec->parent;
		if (candidate_rec)
			udb_record_delete_tree(candidate_rec);
		record_count = udb_record_count_tree(tree);
		if (!udb_file_write_snapshot(block, tree, record_count))
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
		if (ctx->propagator && block->syncing_from == client)
		{
			udb_mutation_forward_del(ctx->propagator, client, target, path);
			return;
		}
		if (!is_broadcast)
			return;
	}
	udb_mutation_forward_del(client, client, target, path);
}

static void udb_mutation_drp(UdbContext *ctx, Client *client, const char *target,
                             char letter, int is_for_me, int is_broadcast)
{
	if (is_for_me)
	{
		UdbBlock *block = udb_block_by_letter(ctx, letter);
		if (!block)
		{
			sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
			return;
		}
		if (block->session || (block->syncing_from && block->syncing_from != client))
		{
			sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (!udb_is_propagator(ctx, client))
		{
			sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		UdbRecord *tree = udb_record_clone_tree(block->tree, NULL, NULL);
		while (tree->child)
			udb_record_delete_tree(tree->child);
		if (!udb_file_write_snapshot(block, tree, 0))
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
	udb_sendto_confirmed_servers(client, ":%s DB %s DRP %c", client->id, target, letter);
}

static void udb_mutation_opt(UdbContext *ctx, Client *client, const char *target,
                             char letter, const char *modified_at, int is_for_me, int is_broadcast)
{
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
		if (!udb_file_save_block(ctx, block))
		{
			udb_mutation_persist_error(client, "OPT", letter);
			return;
		}
		if (!is_broadcast)
			return;
	}
	if (modified_at)
		udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c %s", client->id, target, letter, modified_at);
	else
		udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c", client->id, target, letter);
}
