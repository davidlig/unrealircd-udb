/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Module and Database Lifecycle Coordination
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static char *udb_block_filepath(char letter)
{
	const char *directory = PERMDATADIR;
	if (!directory || !*directory || strlen(directory) > UDB_BLOCK_PATH_MAX - 32)
		return NULL;
	size_t length = strlen(directory) + 32;
	char *path = safe_alloc(length);
	snprintf(path, length, "%s/udb/udb_%c.db", directory, letter);
	return path;
}

static UdbBlock *udb_block_create(UdbContext *ctx, char letter, const char *name)
{
	(void)name;
	if (!ctx || udb_storage_stage_block_index((unsigned char)letter) < 0)
		return NULL;
	UdbBlock *block = safe_alloc(sizeof(*block));
	block->letter = letter;
	block->filepath = udb_block_filepath(letter);
	if (!block->filepath)
	{
		safe_free(block);
		return NULL;
	}
	block->load_state = UDB_LOAD_UNINITIALIZED;
	ctx->blocks[(unsigned char)letter] = block;
	block->next = ctx->block_list;
	ctx->block_list = block;
	ctx->block_count++;
	return block;
}

static UdbBlock *udb_block_by_letter(UdbContext *ctx, char letter)
{
	return ctx ? ctx->blocks[(unsigned char)letter] : NULL;
}

static void udb_block_clear_pending(UdbBlock *block)
{
	if (!block)
		return;
	block->pending_from = NULL;
	block->pending_deadline = 0;
	block->pending_round_id = 0;
}

static int udb_rehash_reload;
static int udb_module_reload_pending(ModuleInfo *modinfo)
{
	if (!modinfo || !modinfo->handle || !modinfo->handle->header)
		return 0;
	for (Module *module = Modules; module; module = module->next)
		if (module != modinfo->handle && module->header && module->header->name &&
			!strcmp(module->header->name, modinfo->handle->header->name))
			return 1;
	return 0;
}

static int udb_native_metadata_restore(UdbContext *ctx)
{
	UdbStorageGeneration *pin = NULL;
	if (udb_storage_engine_pin(ctx->storage, &pin) != UDB_STORAGE_OK)
		return 0;
	const UdbStorageState *state = &pin->state;
	memcpy(ctx->authority_sid, state->source_sid, 4);
	snprintf(ctx->authority_epoch, sizeof(ctx->authority_epoch), "%016" PRIx64, state->source_epoch);
	ctx->authority_epoch_known = state->source_sid[0] != 0;
	ctx->last_applied_seq = state->source_sequence;
	ctx->current_seq = !strcmp(state->source_sid, me.id) ? state->source_sequence : 0;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbBlock *block = udb_block_by_letter(ctx, (char)state->blocks[i].block);
		if (!block)
		{
			udb_storage_engine_release(ctx->storage, &pin);
			return 0;
		}
		udb_native_digest_hex(state->blocks[i].summary.digest, block->sha256);
		block->record_count = state->blocks[i].summary.count;
		block->generation = state->generation;
		block->load_state = state->blocks[i].summary.count ? UDB_LOAD_SUCCESS : UDB_LOAD_EMPTY;
	}
	return udb_storage_engine_release(ctx->storage, &pin) == UDB_STORAGE_OK;
}

static void udb_engine_cleanup(UdbContext *ctx)
{
	if (!ctx)
		return;
	udb_ready = 0;
	UdbStorageEngine *engine = ctx->storage;
	/* Cancel private work and join before releasing callbacks, schema, pins,
	 * copied envelopes or any allocator whose budget the writer still uses. */
	if (engine && engine->budget.initialized && udb_storage_engine_stop(engine, 0) != UDB_STORAGE_OK)
		abort();
	udb_native_sync_cleanup(NULL);
	udb_native_receive_cleanup();
	udb_native_mutation_cleanup();
	udb_nick_preserve_on_rehash(udb_rehash_reload);
	udb_nick_shutdown();
	udb_channels_shutdown();
	udb_nick_preserve_on_rehash(0);
	udb_ips_shutdown();
	udb_lines_shutdown();
	udb_hello_cleanup_all();
	udb_ocl_shutdown();
	for (UdbBlock *block = ctx->block_list, *next; block; block = next)
	{
		next = block->next;
		udb_sync_session_free(block);
		safe_free(block->filepath);
		safe_free(block);
	}
	/* Derived settings are charged to this budget; free them before close. */
	udb_config_free(ctx);
	if (engine)
	{
		if (ctx->native_charge)
		{
			udb_storage_budget_uncharge(&engine->budget, UDB_STORAGE_MEMORY_AUX, ctx->native_charge);
			ctx->native_charge = 0;
		}
		if (engine->budget.initialized && udb_storage_engine_close(engine) != UDB_STORAGE_OK)
			abort();
		safe_free(engine);
	}
	udb_reconcile_reset();
	udb_bootstrap_peer = NULL;
	safe_free(ctx);
	udb_ctx = NULL;
}

static int udb_engine_init(void)
{
	if (!udb_block_descriptors_validate() || !PERMDATADIR || !*PERMDATADIR)
		return 0;
	if (!udb_cfg)
		udb_cfg = safe_alloc(sizeof(*udb_cfg));
	udb_config_defaults();
	udb_ctx = safe_alloc(sizeof(*udb_ctx));
	udb_ctx->storage = safe_alloc(sizeof(*udb_ctx->storage));
	udb_native_schema_capture(&udb_ctx->native_schema, &udb_ctx->storage->budget);
	for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		const UdbBlockDescriptor *descriptor = udb_block_descriptor(i);
		if (!descriptor || !udb_block_create(udb_ctx, descriptor->letter, descriptor->name))
		{
			udb_engine_cleanup(udb_ctx);
			return 0;
		}
	}
	udb_ready = 0;
	UdbStorageResult result = udb_storage_engine_open(
		udb_ctx->storage, PERMDATADIR, udb_cfg->cache_size, udb_storage_jobs_prepare, udb_storage_jobs_finish,
		udb_storage_jobs_cleanup, udb_native_row_validate, udb_native_stage_validate, udb_native_mutation_validate,
		udb_native_mutation_normalize, udb_native_block_profiles_validate, &udb_ctx->native_schema);
	if (result != UDB_STORAGE_OK || !udb_native_metadata_restore(udb_ctx))
	{
		udb_log(ULOG_ERROR, "UDB_NATIVE_OPEN_FAILED", NULL,
				"Native storage recovery failed; refusing admission (result $result)",
				log_data_integer("result", result));
		udb_engine_cleanup(udb_ctx);
		return 0;
	}
	/* Fixed foreground context/descriptors and their allocator overhead are
	 * included in the managed budget, even though daemon allocation owns them. */
	udb_ctx->native_charge = udb_storage_allocation_charge(sizeof(*udb_ctx));
	for (UdbBlock *block = udb_ctx->block_list; block; block = block->next)
		udb_ctx->native_charge +=
			udb_storage_allocation_charge(sizeof(*block)) + udb_storage_allocation_charge(strlen(PERMDATADIR) + 32);
	if (udb_storage_budget_charge(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, udb_ctx->native_charge) !=
		UDB_STORAGE_OK)
	{
		udb_ctx->native_charge = 0;
		udb_engine_cleanup(udb_ctx);
		return 0;
	}
	udb_policy_notify_deferred = 1;
	result = udb_native_runtime_startup(udb_ctx);
	udb_policy_notify_deferred = 0;
	if (result != UDB_STORAGE_OK)
	{
		udb_engine_cleanup(udb_ctx);
		return 0;
	}
	udb_propagator_policy_flush(udb_ctx);
	udb_propagator_policy_changed(udb_ctx);
	UdbPropagatorSelection selected;
	int local =
		!udb_propagator_policy_present(udb_ctx) || (udb_select_propagator(udb_ctx, 1, &selected) && selected.is_local);
	if (local && (!udb_ctx->authority_epoch_known || strcmp(udb_ctx->authority_sid, me.id)))
	{
		if (!udb_native_local_bind(udb_ctx))
		{
			udb_engine_cleanup(udb_ctx);
			return 0;
		}
	}
	else if (udb_ctx->authority_epoch_known)
	{
		/* Recovery has validated all six active roots and source metadata. */
		udb_ready = 1;
		udb_last_successful_sync = time(NULL);
	}
	udb_propagator_availability_refresh();
	udb_sync_status_refresh();
	return 1;
}

static void udb_engine_shutdown(void)
{
	udb_engine_cleanup(udb_ctx);
}

extern MODVAR Log *logs[NUM_LOG_DESTINATIONS];

static void udb_log_snomask_filter_init(void)
{
	Log *ld;
	for (ld = logs[LOG_DEST_SNOMASK]; ld; ld = ld->next)
	{
		LogSource *ls;
		int already_set = 0;
		for (ls = ld->sources; ls; ls = ls->next)
		{
			if (ls->negative && !strcmp(ls->subsystem, "udb"))
			{
				already_set = 1;
				break;
			}
		}
		if (!already_set)
		{
			ls = safe_alloc(sizeof(LogSource));
			ls->loglevel = ULOG_INVALID;
			ls->negative = 1;
			strlcpy(ls->subsystem, "udb", sizeof(ls->subsystem));
			AddListItem(ls, ld->sources);
		}
	}
	for (ld = logs[LOG_DEST_OPER]; ld; ld = ld->next)
	{
		LogSource *ls;
		int already_set = 0;
		for (ls = ld->sources; ls; ls = ls->next)
		{
			if (ls->negative && !strcmp(ls->subsystem, "udb"))
			{
				already_set = 1;
				break;
			}
		}
		if (!already_set)
		{
			ls = safe_alloc(sizeof(LogSource));
			ls->loglevel = ULOG_INVALID;
			ls->negative = 1;
			strlcpy(ls->subsystem, "udb", sizeof(ls->subsystem));
			AddListItem(ls, ld->sources);
		}
	}
}

static void udb_log_snomask_filter_free(void)
{
	Log *ld;
	for (ld = logs[LOG_DEST_SNOMASK]; ld; ld = ld->next)
	{
		LogSource *ls, *ls_next;
		for (ls = ld->sources; ls; ls = ls_next)
		{
			ls_next = ls->next;
			if (ls->negative && !strcmp(ls->subsystem, "udb"))
			{
				DelListItem(ls, ld->sources);
				safe_free(ls);
			}
		}
	}
	for (ld = logs[LOG_DEST_OPER]; ld; ld = ld->next)
	{
		LogSource *ls, *ls_next;
		for (ls = ld->sources; ls; ls = ls_next)
		{
			ls_next = ls->next;
			if (ls->negative && !strcmp(ls->subsystem, "udb"))
			{
				DelListItem(ls, ld->sources);
				safe_free(ls);
			}
		}
	}
}

static void udb_sync_snomask_filter(void)
{
	if (udb_is_debug_enabled())
		udb_log_snomask_filter_free();
	else
		udb_log_snomask_filter_init();
}

static int udb_module_test(ModuleInfo *modinfo)
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, udb_config_test);
	return MOD_SUCCESS;
}

static int udb_module_init(ModuleInfo *modinfo)
{
	ModuleSetOptions(modinfo->handle, MOD_OPT_PERM_RELOADABLE, 1);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, udb_config_run);
	udb_sync_snomask_filter();
	udb_protocol_init(modinfo);
	udb_nicks_init(modinfo);
	udb_channels_init(modinfo);
	if (!udb_ips_init(modinfo))
		return MOD_FAILED;
	udb_lines_init(modinfo);
	udb_query_init(modinfo);
	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

static int udb_module_load(ModuleInfo *modinfo)
{
	Client *server;

	if (udb_engine_init() == 0)
	{
		config_error("[UDB] Failed to initialize database engine");
		udb_nick_reconcile_after_reload();
		return MOD_FAILED;
	}
	udb_nick_reconcile_after_reload();
	udb_sync_snomask_filter();
	udb_ocl_membership_rebuild();
	udb_ocl_local_rebuild();
	udb_nicks_load(modinfo);
	udb_channels_load(modinfo);
	if (!udb_ips_load(modinfo))
	{
		config_error("[UDB] Failed to install host privacy protections");
		udb_engine_cleanup(udb_ctx);
		return MOD_FAILED;
	}
	udb_log(ULOG_INFO, "UDB_LOADED", NULL, "Unreal Database System v" UDB_VERSION " loaded successfully");

	list_for_each_entry(server, &server_list, special_node)
	{
		if (IsServer(server) && MyConnect(server) && !IsMe(server))
			udb_sync_hello_start(server);
	}

	return MOD_SUCCESS;
}

static int udb_module_unload(ModuleInfo *modinfo)
{
	udb_rehash_reload = (loop.rehashing && udb_module_reload_pending(modinfo)) ? 1 : 0;
	udb_log_snomask_filter_init();
	udb_log(ULOG_INFO, "UDB_UNLOADING", NULL, "Stopping native writer and shutting down...");
	udb_engine_shutdown();
	udb_log_snomask_filter_free();
	udb_rehash_reload = 0;
	return MOD_SUCCESS;
}
