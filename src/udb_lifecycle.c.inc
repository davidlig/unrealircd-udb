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
	int success = 1;
	if (!ctx)
		return 0;
	for (b = ctx->block_list; b; b = b->next)
	{
		if (!udb_block_load(ctx, b))
			success = 0;
	}
	return success;
}

static int udb_fsync_parent_directory(const char *path)
{
	char dir_path[UDB_BLOCK_PATH_MAX];
	char *slash;
	int dir_fd;
	int result;

	if (!path || !*path || snprintf(dir_path, sizeof(dir_path), "%s", path) >= (int)sizeof(dir_path))
		return 0;
	slash = strrchr(dir_path, '/');
	if (!slash)
		strlcpy(dir_path, ".", sizeof(dir_path));
	else if (slash == dir_path)
		slash[1] = '\0';
	else
		*slash = '\0';
	dir_fd = open(dir_path, O_RDONLY
#ifdef O_DIRECTORY
								| O_DIRECTORY
#endif
	);
	if (dir_fd < 0)
		return 0;
	result = fsync(dir_fd) == 0;
	if (close(dir_fd) != 0)
		result = 0;
	return result;
}

static int udb_blocks_save_all(UdbContext *ctx)
{
	UdbBlock *b;
	UdbBlock *saved_blocks[UDB_NUM_BLOCKS];
	unsigned int saved_count = 0;
	unsigned int i;
	int success = 1;
	int cleanup_ok = 1;
	char backup_path[UDB_BLOCK_PATH_MAX];
	struct stat st;

	if (!ctx)
		return 0;
	/* A READY generation is a set, not six independent files.  Keep the
	 * previous set physically available until every member has committed; a
	 * failure after any rename must therefore restore the complete old set. */
	for (b = ctx->block_list; b; b = b->next)
	{
		if (saved_count >= UDB_NUM_BLOCKS || !b->filepath ||
			snprintf(backup_path, sizeof(backup_path), "%s.udb_previous", b->filepath) >= (int)sizeof(backup_path))
			return 0;
		if (lstat(b->filepath, &st) == 0)
		{
			if (!S_ISREG(st.st_mode) || rename(b->filepath, backup_path) != 0)
				goto rollback;
		}
		else if (errno != ENOENT)
			goto rollback;
		saved_blocks[saved_count++] = b;
	}
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
	if (!success)
		goto rollback;
	for (i = 0; i < saved_count; i++)
	{
		if (snprintf(backup_path, sizeof(backup_path), "%s.udb_previous", saved_blocks[i]->filepath) >=
			(int)sizeof(backup_path))
			return 0;
		if (unlink(backup_path) != 0 && errno != ENOENT)
			cleanup_ok = 0;
	}
	if (!cleanup_ok || (saved_count && !udb_fsync_parent_directory(saved_blocks[0]->filepath)))
	{
		udb_log(ULOG_ERROR, "UDB_BACKUP_CLEANUP_FAILED", NULL,
				"Cannot durably clean previous UDB snapshots; refusing READY");
		return 0;
	}
	return success;

rollback:
	/* Restore in reverse order.  The active files are disposable because the
	 * caller remains NOT_READY until the state file is committed. */
	for (i = saved_count; i > 0; i--)
	{
		b = saved_blocks[i - 1];
		if (snprintf(backup_path, sizeof(backup_path), "%s.udb_previous", b->filepath) >= (int)sizeof(backup_path))
			continue;
		if (unlink(b->filepath) != 0 && errno != ENOENT)
			success = 0;
		if (rename(backup_path, b->filepath) != 0)
			success = 0;
	}
	if (saved_count && !udb_fsync_parent_directory(saved_blocks[0]->filepath))
		success = 0;
	if (!success)
		udb_log(ULOG_ERROR, "UDB_BACKUP_RESTORE_FAILED", NULL,
				"Cannot durably restore the previous UDB snapshot set; refusing READY");
	return 0;
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
	udb_hello_cleanup_all();
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

static UdbStatePersistResult udb_persistence_set_state(UdbPersistentState state, UdbPersistenceOrigin origin,
													   unsigned long generation, time_t last_sync)
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
		return UDB_STATE_FAILED_BEFORE_COMMIT;

	if (snprintf(tmp_path, sizeof(tmp_path), "%s%s.udb_state.tmp", directory,
				 directory[strlen(directory) - 1] == '/' ? "" : "/") >= (int)sizeof(tmp_path))
		return UDB_STATE_FAILED_BEFORE_COMMIT;

	if (snprintf(final_path, sizeof(final_path), "%s%s.udb_state", directory,
				 directory[strlen(directory) - 1] == '/' ? "" : "/") >= (int)sizeof(final_path))
		return UDB_STATE_FAILED_BEFORE_COMMIT;

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
		return UDB_STATE_FAILED_BEFORE_COMMIT;
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
	const char *origin_str = origin == UDB_ORIGIN_FRESH ? "FRESH" : "RECOVERY";
	if (fprintf(fp, "FORMAT=%d\nSTATE=%s\nORIGIN=%s\nGENERATION=%lu\nLAST_SYNC=%ld\n", UDB_STATE_FORMAT, state_str,
				origin_str, generation, (long)last_sync) < 0)
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
		/* rename(2) is the irreversible point. A visible READY file with uncertain
		 * durability must never authorize readiness, so replace it with a visible
		 * BOOTSTRAPPING marker before reporting the uncertain commit. */
		if (state == UDB_PERSIST_READY)
			(void)udb_persistence_set_state(UDB_PERSIST_BOOTSTRAPPING, UDB_ORIGIN_RECOVERY, generation, 0);
		return UDB_STATE_COMMITTED_DURABILITY_UNCERTAIN;
	}
	if (close(dir_fd) != 0)
	{
		saved_errno = errno;
		udb_log(ULOG_ERROR, "UDB_STATE_DIR_CLOSE_FAILED", NULL,
				"Failed to close directory descriptor for state file $path after commit: $error",
				log_data_string("path", final_path), log_data_string("error", strerror(saved_errno)));
		if (state == UDB_PERSIST_READY)
			(void)udb_persistence_set_state(UDB_PERSIST_BOOTSTRAPPING, UDB_ORIGIN_RECOVERY, generation, 0);
		return UDB_STATE_COMMITTED_DURABILITY_UNCERTAIN;
	}

	udb_log(ULOG_INFO, "UDB_STATE_PERSISTED", NULL, "Persistent UDB state updated to $state",
			log_data_string("state", state_str));
	return UDB_STATE_COMMITTED;

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
	return UDB_STATE_FAILED_BEFORE_COMMIT;
}

static void udb_mark_durability_uncertain(UdbContext *ctx, UdbBlock *block, const char *operation)
{
	unsigned long generation = block ? block->generation : 0;

	(void)ctx;
	udb_ready = 0;
	udb_last_successful_sync = 0;
	udb_log(ULOG_ERROR, "UDB_DURABILITY_UNCERTAIN", NULL,
			"The $operation rename for block $block is visible, so active state was committed, but durability is "
			"uncertain; "
			"forcing BOOTSTRAPPING",
			log_data_string("operation", operation ? operation : "snapshot"),
			log_data_string("block", block ? (char[]){block->letter, '\0'} : "0"));
	if (udb_persistence_set_state(UDB_PERSIST_BOOTSTRAPPING, UDB_ORIGIN_RECOVERY, generation, 0) != UDB_STATE_COMMITTED)
		udb_log(ULOG_ERROR, "UDB_DURABILITY_STATE_FAILED", NULL,
				"Failed to durably record BOOTSTRAPPING after a visible but durability-uncertain snapshot commit");
}

static int udb_persistence_load_state(UdbPersistentState *state_out, UdbPersistenceOrigin *origin_out,
									  unsigned long *generation_out, time_t *last_sync_out)
{
	const char *directory = udb_cfg ? udb_cfg->db_directory : NULL;
	char state_path[UDB_BLOCK_PATH_MAX];
	char line[256];
	FILE *fp;
	unsigned int seen = 0;
	UdbPersistentState state = UDB_PERSIST_BOOTSTRAPPING;
	UdbPersistenceOrigin origin = UDB_ORIGIN_RECOVERY;
	unsigned long generation = 0;
	time_t parsed_sync = 0;
#define UDB_STATE_SEEN_FORMAT (1U << 0)
#define UDB_STATE_SEEN_STATE (1U << 1)
#define UDB_STATE_SEEN_ORIGIN (1U << 2)
#define UDB_STATE_SEEN_GENERATION (1U << 3)
#define UDB_STATE_SEEN_LAST_SYNC (1U << 4)

	if (last_sync_out)
		*last_sync_out = 0;

	if (!directory || !*directory)
		return 0;

	if (snprintf(state_path, sizeof(state_path), "%s%s.udb_state", directory,
				 directory[strlen(directory) - 1] == '/' ? "" : "/") >= (int)sizeof(state_path))
		return 0;

	fp = fopen(state_path, "r");
	if (!fp)
	{
		/* Missing .udb_state */
		return 0;
	}

	while (fgets(line, sizeof(line), fp))
	{
		char *nl = strpbrk(line, "\r\n");
		if (nl)
			*nl = '\0';

		if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
			goto invalid;
		if (!strncmp(line, "FORMAT=", 7))
		{
			unsigned int format;
			if ((seen & UDB_STATE_SEEN_FORMAT) || !udb_parse_uint_strict(line + 7, &format, 1, UINT_MAX) ||
				format != UDB_STATE_FORMAT)
				goto invalid;
			seen |= UDB_STATE_SEEN_FORMAT;
		}
		else if (!strncmp(line, "STATE=", 6))
		{
			const char *val = line + 6;
			if (seen & UDB_STATE_SEEN_STATE)
				goto invalid;
			if (!strcmp(val, "READY"))
			{
				state = UDB_PERSIST_READY;
			}
			else if (!strcmp(val, "BOOTSTRAPPING"))
			{
				state = UDB_PERSIST_BOOTSTRAPPING;
			}
			else
			{
				/* Invalid state string */
				goto invalid;
			}
			seen |= UDB_STATE_SEEN_STATE;
		}
		else if (!strncmp(line, "ORIGIN=", 7))
		{
			const char *val = line + 7;
			if (seen & UDB_STATE_SEEN_ORIGIN)
				goto invalid;
			if (!strcmp(val, "FRESH"))
				origin = UDB_ORIGIN_FRESH;
			else if (!strcmp(val, "RECOVERY"))
				origin = UDB_ORIGIN_RECOVERY;
			else
				goto invalid;
			seen |= UDB_STATE_SEEN_ORIGIN;
		}
		else if (!strncmp(line, "GENERATION=", 11))
		{
			if ((seen & UDB_STATE_SEEN_GENERATION) || !udb_strtoul_strict(line + 11, &generation))
				goto invalid;
			seen |= UDB_STATE_SEEN_GENERATION;
		}
		else if (!strncmp(line, "LAST_SYNC=", 10))
		{
			if ((seen & UDB_STATE_SEEN_LAST_SYNC) || !udb_parse_time_t(line + 10, &parsed_sync))
				goto invalid;
			seen |= UDB_STATE_SEEN_LAST_SYNC;
		}
		else
			goto invalid;
	}
	if (ferror(fp))
		goto invalid;
	if (seen != (UDB_STATE_SEEN_FORMAT | UDB_STATE_SEEN_STATE | UDB_STATE_SEEN_ORIGIN | UDB_STATE_SEEN_GENERATION |
				  UDB_STATE_SEEN_LAST_SYNC))
		goto invalid;
	if (state == UDB_PERSIST_READY && generation == 0)
		goto invalid;
	fclose(fp);
	if (state_out)
		*state_out = state;
	if (origin_out)
		*origin_out = origin;
	if (generation_out)
		*generation_out = generation;
	if (last_sync_out)
		*last_sync_out = parsed_sync;
	return 1;

invalid:
	fclose(fp);
	{
		udb_log(ULOG_ERROR, "UDB_STATE_INVALID", NULL,
				"Corrupted or invalid .udb_state file in $directory; refusing automatic READY",
				log_data_string("directory", directory));
		return 0;
	}
#undef UDB_STATE_SEEN_FORMAT
#undef UDB_STATE_SEEN_STATE
#undef UDB_STATE_SEEN_ORIGIN
#undef UDB_STATE_SEEN_GENERATION
#undef UDB_STATE_SEEN_LAST_SYNC
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

static int udb_transition_to_ready(UdbContext *ctx, time_t sync_time)
{
	UdbBlock *b;
	UdbBlock *saved_blocks[UDB_NUM_BLOCKS];
	unsigned long saved_generations[UDB_NUM_BLOCKS];
	unsigned int saved_count = 0;
	unsigned long generation;

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

	generation = udb_reconcile.active && udb_reconcile.generation ? udb_reconcile.generation : (unsigned long)sync_time;
	if (!generation)
		generation = 1;
	for (b = ctx->block_list; b; b = b->next)
	{
		if (saved_count < UDB_NUM_BLOCKS)
		{
			saved_blocks[saved_count] = b;
			saved_generations[saved_count++] = b->generation;
		}
		b->generation = generation;
	}

	if (!udb_blocks_save_all(ctx))
	{
		for (unsigned int i = 0; i < saved_count; i++)
			saved_blocks[i]->generation = saved_generations[i];
		udb_ready = 0;
		udb_last_successful_sync = 0;
		(void)udb_persistence_set_state(UDB_PERSIST_BOOTSTRAPPING, UDB_ORIGIN_RECOVERY, generation, 0);
		udb_log(ULOG_ERROR, "UDB_READY_SAVE_FAILED", NULL,
				"Cannot enter READY: failed to save block snapshots to disk");
		return 0;
	}

	if (udb_persistence_set_state(UDB_PERSIST_READY, UDB_ORIGIN_FRESH, generation, sync_time) != UDB_STATE_COMMITTED)
	{
		udb_log(ULOG_ERROR, "UDB_STATE_PERSIST_FAILED", NULL,
				"Failed to persist READY state to disk; database remains NOT_READY");
		udb_ready = 0;
		/* Snapshot publication succeeded, but READY did not.  The only safe
		 * durable interpretation after restart is explicitly BOOTSTRAPPING. */
		(void)udb_persistence_set_state(UDB_PERSIST_BOOTSTRAPPING, UDB_ORIGIN_RECOVERY, generation, 0);
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
	(void)udb_blocks_load_all(udb_ctx);

	int policy_present = udb_propagator_policy_present(udb_ctx);
	time_t persisted_last_sync = 0;
	UdbPersistentState persisted_state = UDB_PERSIST_BOOTSTRAPPING;
	unsigned long persisted_generation = 0;
	UdbStartupState startup_state = UDB_STARTUP_FRESH;
	int all_present_valid = 1;
	int all_missing = 1;
	int backup_present = 0;
	UdbBlock *startup_block;

	for (startup_block = udb_ctx->block_list; startup_block; startup_block = startup_block->next)
	{
		char backup_path[UDB_BLOCK_PATH_MAX];
		struct stat backup_st;
		if (snprintf(backup_path, sizeof(backup_path), "%s.udb_previous", startup_block->filepath) >=
			(int)sizeof(backup_path))
			backup_present = 1;
		else if (lstat(backup_path, &backup_st) == 0 || errno != ENOENT)
			backup_present = 1;
		if (startup_block->load_state != UDB_LOAD_SUCCESS)
			all_present_valid = 0;
		if (startup_block->load_state != UDB_LOAD_EMPTY)
			all_missing = 0;
	}

	if (backup_present)
	{
		startup_state = UDB_STARTUP_PERSISTED_INVALID;
		udb_log(ULOG_ERROR, "UDB_BACKUP_PRESENT", NULL,
				"Found an unfinished UDB snapshot backup; refusing automatic READY");
	}
	else if (udb_persistence_has_state_file())
	{
		if (!udb_persistence_load_state(&persisted_state, NULL, &persisted_generation, &persisted_last_sync))
		{
			startup_state = UDB_STARTUP_PERSISTED_INVALID;
		}
		else if (persisted_state == UDB_PERSIST_READY)
		{
			int generation_matches = 1;
			for (startup_block = udb_ctx->block_list; startup_block; startup_block = startup_block->next)
				if (startup_block->generation != persisted_generation)
					generation_matches = 0;
			if (!all_present_valid || !generation_matches)
			{
				startup_state = UDB_STARTUP_PERSISTED_READY_INCOMPLETE;
				udb_log(ULOG_ERROR, "UDB_READY_INCOMPLETE", NULL,
						"Persisted READY does not have six valid snapshots from its generation; refusing READY");
			}
			else
				startup_state = UDB_STARTUP_PERSISTED_READY_VALID;
		}
		else
			startup_state = UDB_STARTUP_PERSISTED_BOOTSTRAPPING;
	}
	else if (!all_missing)
	{
		startup_state = UDB_STARTUP_PERSISTED_INVALID;
		if (all_present_valid)
			udb_log(ULOG_ERROR, "UDB_ORPHANED_SNAPSHOTS", NULL,
					"Complete snapshots exist but no valid .udb_state; refusing automatic READY");
	}

	UdbPropagatorSelection selected;
	int has_propagator = udb_select_propagator(udb_ctx, 0, &selected);
	int is_primary_local = (has_propagator && selected.is_local);

	/* With no propagator policy this process is the standalone authority.  It
	 * must not remain permanently BOOTSTRAPPING merely because no upstream was
	 * selected. */
	if ((is_primary_local || !policy_present) && startup_state == UDB_STARTUP_FRESH)
	{
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
	else if (startup_state == UDB_STARTUP_PERSISTED_READY_VALID)
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
		/* Preserve malformed state verbatim, but downgrade a valid READY marker whose snapshots are incomplete. */
		if (startup_state == UDB_STARTUP_FRESH)
			udb_persistence_set_state(UDB_PERSIST_BOOTSTRAPPING, UDB_ORIGIN_FRESH, 0, 0);
		else if (startup_state == UDB_STARTUP_PERSISTED_READY_INCOMPLETE)
			udb_persistence_set_state(UDB_PERSIST_BOOTSTRAPPING, UDB_ORIGIN_RECOVERY, persisted_generation, 0);
	}

	udb_sync_status_refresh();
	return 1;
}

static void udb_engine_shutdown(void)
{
	if (!udb_ctx)
		return;
	if (udb_ready)
		udb_blocks_save_all(udb_ctx);
	else
		udb_log(ULOG_WARNING, "UDB_SHUTDOWN_NOT_READY", NULL,
				"Skipping shutdown snapshot save because the database is not READY");
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
