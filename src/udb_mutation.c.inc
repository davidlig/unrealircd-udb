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

static void udb_mutation_forward_del(Client *source, Client *except, const char *target,
                                     const char *path)
{
	udb_sendto_confirmed_servers(except, ":%s DB %s DEL %s", source->id, target, path);
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
		int len = strlen(path) + strlen(data) + 2;
		char *line = safe_alloc(len);
		snprintf(line, len, "%s %s", path + 3, data);
		UdbRecord *rec = udb_file_parse_line(ctx, block, line);
		safe_free(line);
		if (rec)
			udb_apply_special_record(ctx, block, rec, 1);
		if (!block->syncing_from)
			udb_file_save_block(ctx, block);
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
		UdbRecord *rec = udb_record_find_path(ctx, block, path + 3);
		if (rec)
			udb_record_delete(ctx, block, rec, 1);
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
		udb_block_reset(ctx, block);
		block->checksum = 0;
		block->filesize = 0;
		if (!block->syncing_from)
			udb_file_save_block(ctx, block);
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
		if (modified_at)
			block->modified_at = atol(modified_at);
		udb_file_save_block(ctx, block);
		if (!is_broadcast)
			return;
	}
	if (modified_at)
		udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c %s", client->id, target, letter, modified_at);
	else
		udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c", client->id, target, letter);
}
