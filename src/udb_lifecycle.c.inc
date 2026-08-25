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

static UdbBlock *udb_block_create(UdbContext *ctx, char letter, const char *name)
{
	UdbBlock *b = safe_alloc(sizeof(UdbBlock));

	b->letter = letter;
	b->version = 1;
	b->tree = udb_record_create(NULL);
	b->tree->block_idx = (unsigned char)udb_block_letter_to_index(letter);
	safe_strdup(b->tree->key, name);
	b->tree->data_num = 1;

	b->filepath = udb_block_filepath(letter);
	if (!b->filepath)
	{
		udb_record_free_tree(b->tree);
		safe_free(b);
		return NULL;
	}

	ctx->blocks[(unsigned char)letter] = b;
	b->next = ctx->block_list;
	ctx->block_list = b;
	ctx->block_count++;
	return b;
}

static char *udb_block_filepath(char letter)
{
	const char *directory = udb_cfg ? udb_cfg->db_directory : NULL;
	char *path;
	size_t directory_length;
	size_t path_length;

	if (!directory || !*directory)
		return NULL;
	directory_length = strlen(directory);
	path_length = directory_length + sizeof("/udb_N.db");
	if (path_length > UDB_BLOCK_PATH_MAX)
		return NULL;
	path = safe_alloc(path_length);
	snprintf(path, path_length, "%s%sudb_%c.db", directory,
	         directory[directory_length - 1] == '/' ? "" : "/", letter);
	return path;
}

static void udb_block_set_context_root(UdbContext *ctx, UdbBlock *block)
{
	if (!ctx || !block)
		return;
	switch (block->letter)
	{
		case 'N':
			ctx->nicks = block->tree;
			break;
		case 'C':
			ctx->channels = block->tree;
			break;
		case 'I':
			ctx->ips = block->tree;
			break;
		case 'S':
			ctx->settings = block->tree;
			break;
		case 'L':
			ctx->links = block->tree;
			break;
		case 'K':
			ctx->lines = block->tree;
			break;
	}
}

static void udb_block_reset(UdbContext *ctx, UdbBlock *block)
{
	char *name = NULL;
	int block_idx;

	if (!block)
		return;
	udb_remove_tree_effects(ctx, block);

	if (block->tree && block->tree->key)
		safe_strdup(name, block->tree->key);
	else
		safe_strdup(name, "UDB");
	block_idx = udb_block_letter_to_index(block->letter);
	if (block->tree)
	{
		if (ctx->total_records >= block->record_count)
			ctx->total_records -= block->record_count;
		else
			ctx->total_records = 0;
		udb_record_free_tree(block->tree);
	}
	udb_hash_clear_block(ctx, block_idx);

	block->tree = udb_record_create(NULL);
	block->tree->block_idx = (unsigned char)block_idx;
	safe_strdup(block->tree->key, name);
	block->tree->is_dynamic_key = 1;
	block->tree->data_num = 1;
	block->record_count = 0;
	udb_block_set_context_root(ctx, block);
	safe_free(name);
}

static int udb_block_load(UdbContext *ctx, UdbBlock *block)
{
	(void)udb_file_cleanup_snapshot_temp(block);

	return udb_file_load_block(ctx, block);
}

static void udb_block_unload(UdbContext *ctx, UdbBlock *block)
{
	(void)ctx;
	if (block->tree)
	{
		udb_record_free_tree(block->tree);
		block->tree = NULL;
	}
}

static void udb_blocks_load_all(UdbContext *ctx)
{
	UdbBlock *b;
	for (b = ctx->block_list; b; b = b->next)
		udb_block_load(ctx, b);
}

static void udb_blocks_save_all(UdbContext *ctx)
{
	UdbBlock *b;
	for (b = ctx->block_list; b; b = b->next)
		udb_file_save_block(ctx, b);
}

static UdbBlock *udb_block_by_letter(UdbContext *ctx, char letter)
{
	return ctx ? ctx->blocks[(unsigned char)letter] : NULL;
}

static int udb_engine_init(void)
{
	struct stat st = {0};
	const char *dir;

	if (!udb_cfg)
		udb_cfg = safe_alloc(sizeof(UdbConfig));
	if (!udb_cfg->db_directory)
		safe_strdup(udb_cfg->db_directory, UDB_DEFAULT_DB_DIRECTORY);
	udb_ctx = safe_alloc(sizeof(UdbContext));
	udb_hash_init(udb_ctx);
	dir = udb_cfg && udb_cfg->db_directory ? udb_cfg->db_directory : NULL;
	if (!dir)
		return 0;
	if (stat(dir, &st) == -1)
	{
		if (mkdir(dir, 0700) != 0)
		{
			udb_log(ULOG_ERROR, "UDB_DIRECTORY_CREATE_FAILED", NULL,
			        "Cannot create database directory $directory: $error",
			        log_data_string("directory", dir), log_data_string("error", strerror(errno)));
			return 0;
		}
	} else if (!S_ISDIR(st.st_mode))
	{
		udb_log(ULOG_ERROR, "UDB_DIRECTORY_INVALID", NULL,
		        "Database directory $directory is not a directory", log_data_string("directory", dir));
		return 0;
	}

	if (!udb_block_create(udb_ctx, 'N', "Nicks") || !udb_block_create(udb_ctx, 'C', "Channels") ||
	    !udb_block_create(udb_ctx, 'I', "IPs") || !udb_block_create(udb_ctx, 'S', "Settings") ||
	    !udb_block_create(udb_ctx, 'L', "Links") || !udb_block_create(udb_ctx, 'K', "Lines"))
	{
		udb_engine_shutdown();
		return 0;
	}
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['N']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['C']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['I']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['S']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['L']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['K']);
	udb_blocks_load_all(udb_ctx);
	return 1;
}

static void udb_engine_shutdown(void)
{
	UdbBlock *b;

	if (!udb_ctx)
		return;
	udb_blocks_save_all(udb_ctx);
	udb_ips_shutdown();
	for (b = udb_ctx->block_list; b;)
	{
		UdbBlock *next = b->next;
		udb_sync_session_free(b);
		udb_block_unload(udb_ctx, b);
		safe_free(b->filepath);
		safe_free(b);
		b = next;
	}
	udb_hash_destroy(udb_ctx);
	udb_config_free(udb_ctx);
	safe_free(udb_ctx);
	udb_ctx = NULL;
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
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, udb_config_posttest);
	return MOD_SUCCESS;
}

static int udb_module_init(ModuleInfo *modinfo)
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, udb_config_run);
	udb_sync_snomask_filter();
	udb_protocol_init(modinfo);
	udb_nicks_init(modinfo);
	udb_channels_init(modinfo);
	udb_ips_init(modinfo);
	udb_lines_init(modinfo);
	udb_query_init(modinfo);
	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

static int udb_module_load(ModuleInfo *modinfo)
{
	if (udb_engine_init() == 0)
	{
		config_error("[UDB] Failed to initialize database engine");
		return MOD_FAILED;
	}
	udb_sync_snomask_filter();
	udb_nicks_load(modinfo);
	udb_channels_load(modinfo);
	udb_log(ULOG_INFO, "UDB_LOADED", NULL,
	        "Unreal Database System v" UDB_VERSION " loaded successfully");
	return MOD_SUCCESS;
}

static int udb_module_unload(void)
{
	udb_log(ULOG_INFO, "UDB_UNLOADING", NULL,
	        "Saving databases and shutting down...");
	udb_log_snomask_filter_free();
	udb_blocks_save_all(udb_ctx);
	udb_engine_shutdown();
	return MOD_SUCCESS;
}
