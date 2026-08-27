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
	b->load_state = UDB_LOAD_UNINITIALIZED;
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
	snprintf(path, path_length, "%s%sudb_%c.db", directory, directory[directory_length - 1] == '/' ? "" : "/", letter);
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

static int udb_blocks_load_all(UdbContext *ctx)
{
	UdbBlock *b;
	if (!ctx)
		return 0;
	for (b = ctx->block_list; b; b = b->next)
	{
		if (!udb_block_load(ctx, b))
			return 0;
	}
	return 1;
}

static int udb_blocks_save_all(UdbContext *ctx)
{
	UdbBlock *b;
	int success = 1;

	if (!ctx)
		return 0;
	for (b = ctx->block_list; b; b = b->next)
	{
		if (b->load_state != UDB_LOAD_SUCCESS && b->load_state != UDB_LOAD_EMPTY)
		{
			udb_log(ULOG_ERROR, "UDB_BLOCK_SAVE_SKIPPED", NULL,
					"Skipping save of block $block because it is not in safe initialized state",
					log_data_string("block", (char[]){b->letter, '\0'}));
			success = 0;
			continue;
		}
		if (!udb_file_save_block(ctx, b))
			success = 0;
	}
	return success;
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

static void udb_engine_cleanup(UdbContext *ctx)
{
	UdbBlock *b;

	if (!ctx)
		return;
	udb_ips_shutdown();
	for (b = ctx->block_list; b;)
	{
		UdbBlock *next = b->next;
		udb_block_clear_pending(b);
		udb_sync_session_free(b);
		udb_block_unload(ctx, b);
		safe_free(b->filepath);
		safe_free(b);
		b = next;
	}
	udb_hash_destroy(ctx);
	udb_config_free(ctx);
	safe_free(ctx);
	if (udb_ctx == ctx)
		udb_ctx = NULL;
}

static int udb_persistence_set_state(UdbPersistentState state, time_t last_sync)
{
	const char *directory = udb_cfg ? udb_cfg->db_directory : NULL;
	char tmp_path[UDB_BLOCK_PATH_MAX];
	char final_path[UDB_BLOCK_PATH_MAX];
	char dir_path[UDB_BLOCK_PATH_MAX];
	char *slash;
	FILE *fp = NULL;
	int fd = -1;
	int dir_fd = -1;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int tmp_created = 0;
	int saved_errno;

	if (!directory || !*directory)
		return 0;

	if (snprintf(tmp_path, sizeof(tmp_path), "%s%s.udb_state.tmp", directory,
				 directory[strlen(directory) - 1] == '/' ? "" : "/") >= (int)sizeof(tmp_path))
		return 0;

	if (snprintf(final_path, sizeof(final_path), "%s%s.udb_state", directory,
				 directory[strlen(directory) - 1] == '/' ? "" : "/") >= (int)sizeof(final_path))
		return 0;

	strlcpy(dir_path, final_path, sizeof(dir_path));
	slash = strrchr(dir_path, '/');
	if (!slash)
		strlcpy(dir_path, ".", sizeof(dir_path));
	else if (slash == dir_path)
		slash[1] = '\0';
	else
		*slash = '\0';

#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	fd = open(tmp_path, flags, 0600);
	if (fd < 0)
	{
		saved_errno = errno;
		udb_log(ULOG_ERROR, "UDB_STATE_PERSIST_FAILED", NULL, "Failed to open temporary state file $path: $error",
				log_data_string("path", tmp_path), log_data_string("error", strerror(saved_errno)));
		return 0;
	}
	tmp_created = 1;

	dir_fd = open(dir_path, O_RDONLY
#ifdef O_DIRECTORY
								| O_DIRECTORY
#endif
	);
	if (dir_fd < 0)
		goto cleanup;

	if (fchmod(fd, 0600) != 0)
		goto cleanup;

	fp = fdopen(fd, "w");
	if (!fp)
		goto cleanup;
	fd = -1;

	const char *state_str = (state == UDB_PERSIST_READY) ? "READY" : "BOOTSTRAPPING";
	if (fprintf(fp, "STATE=%s\nLAST_SYNC=%ld\n", state_str, (long)last_sync) < 0)
		goto cleanup;

	if (fflush(fp) != 0 || fsync(fileno(fp)) != 0)
		goto cleanup;

	if (fclose(fp) != 0)
	{
		fp = NULL;
		goto cleanup;
	}
	fp = NULL;

	if (rename(tmp_path, final_path) != 0)
		goto cleanup;
	tmp_created = 0;

	if (fsync(dir_fd) != 0)
	{
		saved_errno = errno;
		udb_log(ULOG_ERROR, "UDB_STATE_DIR_FSYNC_FAILED", NULL,
				"Failed to fsync directory for state file $path after commit: $error",
				log_data_string("path", final_path), log_data_string("error", strerror(saved_errno)));
		close(dir_fd);
		return 0;
	}
	close(dir_fd);

	udb_log(ULOG_INFO, "UDB_STATE_PERSISTED", NULL, "Persistent UDB state updated to $state",
			log_data_string("state", state_str));
	return 1;

cleanup:
	saved_errno = errno;
	udb_log(ULOG_ERROR, "UDB_STATE_PERSIST_FAILED", NULL, "Failed to persist state to $path: $error",
			log_data_string("path", final_path), log_data_string("error", strerror(saved_errno)));
	if (fp)
		fclose(fp);
	else if (fd >= 0)
		close(fd);
	if (dir_fd >= 0)
		close(dir_fd);
	if (tmp_created)
		unlink(tmp_path);
	return 0;
}

static UdbPersistentState udb_persistence_load_state(time_t *last_sync_out)
{
	const char *directory = udb_cfg ? udb_cfg->db_directory : NULL;
	char state_path[UDB_BLOCK_PATH_MAX];
	char line[256];
	FILE *fp;
	int state_parsed = 0;
	int sync_parsed = 0;
	UdbPersistentState state = UDB_PERSIST_BOOTSTRAPPING;
	time_t parsed_sync = 0;

	if (last_sync_out)
		*last_sync_out = 0;

	if (!directory || !*directory)
		return UDB_PERSIST_BOOTSTRAPPING;

	if (snprintf(state_path, sizeof(state_path), "%s%s.udb_state", directory,
				 directory[strlen(directory) - 1] == '/' ? "" : "/") >= (int)sizeof(state_path))
		return UDB_PERSIST_BOOTSTRAPPING;

	fp = fopen(state_path, "r");
	if (!fp)
	{
		/* Missing .udb_state */
		return UDB_PERSIST_BOOTSTRAPPING;
	}

	while (fgets(line, sizeof(line), fp))
	{
		char *nl = strpbrk(line, "\r\n");
		if (nl)
			*nl = '\0';

		if (line[0] == '#' || line[0] == ';' || line[0] == '\0')
			continue;

		if (!strncmp(line, "STATE=", 6))
		{
			const char *val = line + 6;
			if (!strcmp(val, "READY"))
			{
				state = UDB_PERSIST_READY;
				state_parsed = 1;
			}
			else if (!strcmp(val, "BOOTSTRAPPING"))
			{
				state = UDB_PERSIST_BOOTSTRAPPING;
				state_parsed = 1;
			}
			else
			{
				/* Invalid state string */
				state_parsed = -1;
				break;
			}
		}
		else if (!strncmp(line, "LAST_SYNC=", 10))
		{
			if (udb_parse_time_t(line + 10, &parsed_sync))
				sync_parsed = 1;
			else
			{
				state_parsed = -1;
				break;
			}
		}
		else
		{
			/* Unrecognized line in .udb_state */
			state_parsed = -1;
			break;
		}
	}
	fclose(fp);

	if (state_parsed != 1)
	{
		udb_log(ULOG_ERROR, "UDB_STATE_INVALID", NULL,
				"Corrupted or invalid .udb_state file in $directory; treating as BOOTSTRAPPING",
				log_data_string("directory", directory));
		return UDB_PERSIST_BOOTSTRAPPING;
	}

	if (last_sync_out)
		*last_sync_out = parsed_sync;

	return state;
}

static int udb_persistence_has_state_file(void)
{
	const char *directory = udb_cfg ? udb_cfg->db_directory : NULL;
	char state_path[UDB_BLOCK_PATH_MAX];
	struct stat st;

	if (!directory || !*directory)
		return 0;
	if (snprintf(state_path, sizeof(state_path), "%s%s.udb_state", directory,
				 directory[strlen(directory) - 1] == '/' ? "" : "/") >= (int)sizeof(state_path))
		return 0;
	return stat(state_path, &st) == 0;
}

static int udb_persistence_migrate_legacy(UdbContext *ctx, time_t *last_sync_out)
{
	const char *directory = udb_cfg ? udb_cfg->db_directory : NULL;
	char legacy_path[UDB_BLOCK_PATH_MAX];
	struct stat st;
	UdbBlock *b;
	time_t sync_time = 0;

	if (!directory || !*directory || !ctx)
		return 0;

	if (snprintf(legacy_path, sizeof(legacy_path), "%s%s.udb_ready", directory,
				 directory[strlen(directory) - 1] == '/' ? "" : "/") >= (int)sizeof(legacy_path))
		return 0;

	if (stat(legacy_path, &st) != 0)
		return 0;

	for (b = ctx->block_list; b; b = b->next)
	{
		if (b->load_state == UDB_LOAD_FAILED)
		{
			udb_log(ULOG_ERROR, "UDB_MIGRATION_FAILED", NULL,
					"Cannot migrate legacy database: block $block failed to load",
					log_data_string("block", (char[]){b->letter, '\0'}));
			return 0;
		}
	}

	sync_time = (st.st_mtime > 0) ? st.st_mtime : time(NULL);

	if (!udb_blocks_save_all(ctx))
	{
		udb_log(ULOG_ERROR, "UDB_MIGRATION_FAILED", NULL,
				"Cannot migrate legacy database: failed to materialize block snapshots");
		return 0;
	}

	if (!udb_persistence_set_state(UDB_PERSIST_READY, sync_time))
	{
		udb_log(ULOG_ERROR, "UDB_MIGRATION_FAILED", NULL,
				"Cannot migrate legacy database: failed to persist .udb_state");
		return 0;
	}

	if (last_sync_out)
		*last_sync_out = sync_time;

	udb_log(ULOG_INFO, "UDB_LEGACY_MIGRATION", NULL,
			"Migrated legacy database state (.udb_ready) to persistent READY (.udb_state)");
	return 1;
}

static int udb_transition_to_ready(UdbContext *ctx, time_t sync_time)
{
	UdbBlock *b;

	if (!ctx)
		return 0;

	for (b = ctx->block_list; b; b = b->next)
	{
		if (b->load_state != UDB_LOAD_SUCCESS && b->load_state != UDB_LOAD_EMPTY)
		{
			udb_log(ULOG_ERROR, "UDB_READY_SAVE_SKIPPED", NULL,
					"Cannot enter READY: block $block is not in safe initialized state",
					log_data_string("block", (char[]){b->letter, '\0'}));
			return 0;
		}
	}

	if (!udb_blocks_save_all(ctx))
	{
		udb_log(ULOG_ERROR, "UDB_READY_SAVE_FAILED", NULL,
				"Cannot enter READY: failed to save block snapshots to disk");
		return 0;
	}

	if (!udb_persistence_set_state(UDB_PERSIST_READY, sync_time))
	{
		udb_log(ULOG_ERROR, "UDB_STATE_PERSIST_FAILED", NULL,
				"Failed to persist READY state to disk; database remains NOT_READY");
		udb_ready = 0;
		return 0;
	}

	udb_ready = 1;
	udb_last_successful_sync = sync_time;
	return 1;
}

static int udb_engine_init(void)
{
	struct stat st = {0};
	const char *dir;

	if (!udb_cfg)
		udb_cfg = safe_alloc(sizeof(UdbConfig));
	if (!udb_cfg->db_directory)
		safe_strdup(udb_cfg->db_directory, UDB_DEFAULT_DB_DIRECTORY);
	if (udb_cfg->max_staged_records == 0)
		udb_cfg->max_staged_records = UDB_DEFAULT_MAX_STAGED_RECORDS;
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
					"Cannot create database directory $directory: $error", log_data_string("directory", dir),
					log_data_string("error", strerror(errno)));
			return 0;
		}
	}
	else if (!S_ISDIR(st.st_mode))
	{
		udb_log(ULOG_ERROR, "UDB_DIRECTORY_INVALID", NULL, "Database directory $directory is not a directory",
				log_data_string("directory", dir));
		return 0;
	}

	if (!udb_block_create(udb_ctx, 'N', "Nicks") || !udb_block_create(udb_ctx, 'C', "Channels") ||
		!udb_block_create(udb_ctx, 'I', "IPs") || !udb_block_create(udb_ctx, 'S', "Settings") ||
		!udb_block_create(udb_ctx, 'L', "Links") || !udb_block_create(udb_ctx, 'K', "Lines"))
	{
		udb_engine_cleanup(udb_ctx);
		return 0;
	}
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['N']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['C']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['I']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['S']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['L']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['K']);
	if (!udb_blocks_load_all(udb_ctx))
	{
		udb_engine_cleanup(udb_ctx);
		return 0;
	}

	int policy_present = udb_propagator_policy_present(udb_ctx);
	time_t persisted_last_sync = 0;
	UdbPersistentState persisted_state = UDB_PERSIST_BOOTSTRAPPING;

	if (udb_persistence_has_state_file())
	{
		persisted_state = udb_persistence_load_state(&persisted_last_sync);
		if (persisted_state == UDB_PERSIST_READY)
		{
			/* Invariant R2: Validate that all required block snapshots were materialized on disk */
			int missing_block = 0;
			UdbBlock *b;
			for (b = udb_ctx->block_list; b; b = b->next)
			{
				if (b->load_state == UDB_LOAD_EMPTY)
				{
					udb_log(ULOG_ERROR, "UDB_READY_MISSING_BLOCK", NULL,
							"Persisted READY state found, but snapshot for block $block is missing from disk; refusing "
							"to start READY",
							log_data_string("block", (char[]){b->letter, '\0'}));
					missing_block = 1;
				}
			}
			if (missing_block)
			{
				persisted_state = UDB_PERSIST_BOOTSTRAPPING;
				persisted_last_sync = 0;
			}
		}
	}
	else
	{
		/* Attempt legacy migration if .udb_ready exists */
		if (udb_persistence_migrate_legacy(udb_ctx, &persisted_last_sync))
		{
			persisted_state = UDB_PERSIST_READY;
		}
		else
		{
			persisted_state = UDB_PERSIST_BOOTSTRAPPING;
		}
	}

	UdbPropagatorSelection selected;
	int has_propagator = udb_select_propagator(udb_ctx, 0, &selected);
	int is_primary_local = (has_propagator && selected.is_local);

	if (is_primary_local)
	{
		if (persisted_state != UDB_PERSIST_READY)
		{
			/* Local primary authoritative propagator starting clean: enter durable READY */
			if (udb_transition_to_ready(udb_ctx, time(NULL)))
			{
				udb_sync_status = UDB_SYNC_OK;
			}
			else
			{
				udb_ready = 0;
				udb_last_successful_sync = 0;
			}
		}
		else
		{
			udb_ready = 1;
			udb_last_successful_sync = persisted_last_sync ? persisted_last_sync : time(NULL);
			udb_sync_status = UDB_SYNC_OK;
		}
	}
	else if (persisted_state == UDB_PERSIST_READY)
	{
		udb_ready = 1;
		udb_last_successful_sync = persisted_last_sync;
		if (!policy_present)
			udb_sync_status = UDB_SYNC_OK;
		else
			udb_sync_status_refresh();
	}
	else
	{
		udb_ready = 0;
		udb_last_successful_sync = 0;
		udb_persistence_set_state(UDB_PERSIST_BOOTSTRAPPING, 0);
	}

	udb_sync_status_refresh();
	return 1;
}

static void udb_engine_shutdown(void)
{
	if (!udb_ctx)
		return;
	udb_blocks_save_all(udb_ctx);
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
	Client *server;

	if (udb_engine_init() == 0)
	{
		config_error("[UDB] Failed to initialize database engine");
		return MOD_FAILED;
	}
	udb_sync_snomask_filter();
	udb_nicks_load(modinfo);
	udb_channels_load(modinfo);
	udb_log(ULOG_INFO, "UDB_LOADED", NULL, "Unreal Database System v" UDB_VERSION " loaded successfully");

	list_for_each_entry(server, &server_list, special_node)
	{
		if (IsServer(server) && MyConnect(server) && !IsMe(server))
			udb_sync_hello_start(server);
	}

	return MOD_SUCCESS;
}

static int udb_module_unload(void)
{
	udb_log(ULOG_INFO, "UDB_UNLOADING", NULL, "Saving databases and shutting down...");
	udb_log_snomask_filter_free();
	udb_engine_shutdown();
	return MOD_SUCCESS;
}
