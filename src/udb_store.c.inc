/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Record Tree, Indexing, Hashing & File Persistence
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

/* ========================================================================
 * Block Index and Hash Operations
 * ======================================================================== */
static int udb_block_letter_to_index(char letter)
{
	switch (letter)
	{
	case 'N':
		return 0;
	case 'C':
		return 1;
	case 'I':
		return 2;
	case 'S':
		return 3;
	case 'L':
		return 4;
	case 'K':
		return 5;
	default:
		return 0;
	}
}

static void udb_hash_init(UdbContext *ctx)
{
	for (int i = 0; i < UDB_NUM_BLOCKS; i++)
		ctx->hash_table[i] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
}

static void udb_hash_destroy(UdbContext *ctx)
{
	for (int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		if (ctx->hash_table[i])
		{
			safe_free(ctx->hash_table[i]);
			ctx->hash_table[i] = NULL;
		}
	}
}

static void udb_hash_clear_block(UdbContext *ctx, int block_idx)
{
	if (block_idx < 0 || block_idx >= UDB_NUM_BLOCKS)
		return;
	safe_free(ctx->hash_table[block_idx]);
	ctx->hash_table[block_idx] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
}

static unsigned int udb_hash_str(const char *str)
{
	unsigned int hash = 5381;
	int c;

	while ((c = *str++))
	{
		if (c >= 'A' && c <= 'Z')
			c += ('a' - 'A');
		hash = ((hash << 5) + hash) + c; // djb2
	}
	return hash & UDB_HASH_MASK;
}

static void udb_hash_insert_record(UdbContext *ctx, UdbRecord *rec, int block_idx, const char *key)
{
	unsigned int h = udb_hash_str(key);

	rec->hash_next = ctx->hash_table[block_idx][h];
	ctx->hash_table[block_idx][h] = rec;
}

static int udb_hash_remove_record(UdbContext *ctx, UdbRecord *rec, int block_idx, const char *key)
{
	unsigned int h = udb_hash_str(key);
	UdbRecord *curr = ctx->hash_table[block_idx][h];
	UdbRecord *prev = NULL;

	while (curr)
	{
		if (curr == rec)
		{
			if (prev)
				prev->hash_next = curr->hash_next;
			else
				ctx->hash_table[block_idx][h] = curr->hash_next;
			return 1;
		}
		prev = curr;
		curr = curr->hash_next;
	}
	return 0;
}

static UdbRecord *udb_hash_find(UdbContext *ctx, int block_idx, const char *key)
{
	unsigned int h;
	UdbRecord *curr;

	if (!key)
		return NULL;
	h = udb_hash_str(key);
	curr = ctx->hash_table[block_idx][h];
	while (curr)
	{
		if (!strcasecmp(curr->key, key))
			return curr;
		curr = curr->hash_next;
	}
	return NULL;
}

/* ========================================================================
 * Record Tree and Path Operations
 * ======================================================================== */
static const char *udb_get_shared_subkey(const char *key)
{
	static const char *known_keys[] = {"pass",		 "vhost",	 "oper",	  "swhois",	   "snomasks",		 "modes",
									   "access",	 "forbid",	 "suspended", "challenge", "founder",		 "topic",
									   "options",	 "clones",	 "nolines",	  "host",	   "encryption_key", "suffix",
									   "nickserv",	 "chanserv", "ipserv",	  "quit_ips",  "quit_clones",	 "flood",
									   "propagator", "type",	 "action",	  "duration",  "reason",		 NULL};

	for (int i = 0; known_keys[i]; i++)
		if (!strcasecmp(known_keys[i], key))
			return known_keys[i];
	return NULL;
}

static UdbRecord *udb_record_create(UdbRecord *parent)
{
	UdbRecord *rec = safe_alloc(sizeof(UdbRecord));

	rec->parent = parent;
	if (parent)
	{
		rec->block_idx = parent->block_idx;
		rec->sibling = parent->child;
		parent->child = rec;
	}
	return rec;
}

static UdbRecord *udb_record_find(UdbContext *ctx, const char *key, UdbRecord *parent)
{
	UdbRecord *child;

	if (!parent)
		return NULL;
	if (parent->parent == NULL && ctx)
		return udb_hash_find(ctx, parent->block_idx, key);
	for (child = parent->child; child; child = child->sibling)
		if (!strcasecmp(child->key, key))
			return child;
	return NULL;
}

/* ========================================================================
 * Record Limits and Feasibility Validation
 * ======================================================================== */
static int udb_record_fits_limits(const char *path, const char *value)
{
	size_t path_len;
	size_t val_len = 0;
	size_t serialized_line_len;
	const char *comp;
	const char *sep;

	if (!path || !*path)
		return 0;

	path_len = strlen(path);
	if (path_len > UDB_RECORD_PATH_MAX)
		return 0;

	/* Validate each encoded component in path */
	comp = path;
	while ((sep = strstr(comp, "::")))
	{
		if (sep == comp || !sep[2])
			return 0;
		size_t clen = sep - comp;
		if (clen > UDB_COMPONENT_ENCODED_MAX)
			return 0;
		char enc_buf[UDB_COMPONENT_ENCODED_MAX + 1];
		char dec_buf[UDB_COMPONENT_RAW_MAX + 1];
		memcpy(enc_buf, comp, clen);
		enc_buf[clen] = '\0';
		if (!udb_path_decode_component(enc_buf, dec_buf, sizeof(dec_buf)))
			return 0;
		if (strlen(dec_buf) > UDB_COMPONENT_RAW_MAX)
			return 0;
		comp = sep + 2;
	}
	if (!*comp)
		return 0;
	size_t last_clen = strlen(comp);
	if (last_clen > UDB_COMPONENT_ENCODED_MAX)
		return 0;
	char dec_buf[UDB_COMPONENT_RAW_MAX + 1];
	if (!udb_path_decode_component(comp, dec_buf, sizeof(dec_buf)))
		return 0;
	if (strlen(dec_buf) > UDB_COMPONENT_RAW_MAX)
		return 0;

	if (value)
	{
		val_len = strlen(value);
		if (val_len > UDB_RECORD_VALUE_MAX)
			return 0;
		if (strpbrk(value, "\r\n"))
			return 0;
	}

	/* Serialized line check: path + ' ' + value + '\n' */
	serialized_line_len = path_len + (value && *value ? 1 + val_len : 0) + 1;
	if (serialized_line_len > UDB_RECORD_LINE_MAX)
		return 0;

	/* S2S feasibility check */
	if (path_len + (value ? val_len : 0) + UDB_S2S_OVERHEAD_MAX > UDB_S2S_LINE_MAX)
		return 0;

	return 1;
}

/* ========================================================================
 * Path Component Encoding & Decoding
 * ======================================================================== */
static int udb_path_encode_component(const char *raw, char *buf, size_t bufsz)
{
	static const char hex[] = "0123456789ABCDEF";
	const unsigned char *p;
	size_t out_len = 0;

	if (!raw || !buf || bufsz == 0)
		return 0;

	for (p = (const unsigned char *)raw; *p; p++)
	{
		if (*p == ':' || *p == '%' || *p <= 32 || *p >= 127)
		{
			if (out_len + 3 >= bufsz)
				return 0;
			buf[out_len++] = '%';
			buf[out_len++] = hex[(*p >> 4) & 0x0F];
			buf[out_len++] = hex[*p & 0x0F];
		}
		else
		{
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = (char)*p;
		}
	}
	buf[out_len] = '\0';
	return 1;
}

static int udb_path_decode_component(const char *encoded, char *buf, size_t bufsz)
{
	const char *p;
	size_t out_len = 0;

	if (!encoded || !buf || bufsz == 0)
		return 0;

	for (p = encoded; *p; p++)
	{
		if (*p == '%')
		{
			int high, low;
			if (!p[1] || !p[2])
				return 0;
			if (p[1] >= '0' && p[1] <= '9')
				high = p[1] - '0';
			else if (p[1] >= 'a' && p[1] <= 'f')
				high = p[1] - 'a' + 10;
			else if (p[1] >= 'A' && p[1] <= 'F')
				high = p[1] - 'A' + 10;
			else
				return 0;

			if (p[2] >= '0' && p[2] <= '9')
				low = p[2] - '0';
			else if (p[2] >= 'a' && p[2] <= 'f')
				low = p[2] - 'a' + 10;
			else if (p[2] >= 'A' && p[2] <= 'F')
				low = p[2] - 'A' + 10;
			else
				return 0;

			unsigned char val = (unsigned char)((high << 4) | low);
			if (val == 0) /* Embedded null byte is rejected */
				return 0;
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = (char)val;
			p += 2;
		}
		else
		{
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = *p;
		}
	}
	buf[out_len] = '\0';
	return 1;
}

static int udb_path_append(char *dst, size_t dst_size, size_t *used, const char *component)
{
	char encoded[UDB_COMPONENT_ENCODED_MAX + 1];
	size_t cur_len;
	size_t enc_len;

	if (!dst || !component || !udb_path_encode_component(component, encoded, sizeof(encoded)))
		return 0;
	cur_len = used ? *used : strlen(dst);
	enc_len = strlen(encoded);
	if (cur_len > 0)
	{
		if (cur_len + 2 + enc_len >= dst_size)
			return 0;
		memcpy(dst + cur_len, "::", 2);
		cur_len += 2;
	}
	else
	{
		if (enc_len >= dst_size)
			return 0;
	}
	memcpy(dst + cur_len, encoded, enc_len + 1);
	cur_len += enc_len;
	if (used)
		*used = cur_len;
	return 1;
}

static int udb_path_append_component(char *pathbuf, size_t bufsz, const char *raw_component)
{
	return udb_path_append(pathbuf, bufsz, NULL, raw_component);
}

static UdbRecord *udb_record_find_path(UdbContext *ctx, UdbBlock *block, const char *path)
{
	char decoded_part[UDB_COMPONENT_RAW_MAX + 1];
	char pathbuf[UDB_RECORD_PATH_MAX + 1];
	char *cur;
	char *ds;
	UdbRecord *rec;

	if (!block || !block->tree || !path || strlen(path) > UDB_RECORD_PATH_MAX || !udb_record_fits_limits(path, NULL))
		return NULL;
	strlcpy(pathbuf, path, sizeof(pathbuf));
	cur = pathbuf;
	rec = block->tree;
	while ((ds = strstr(cur, "::")))
	{
		*ds = '\0';
		if (!udb_path_decode_component(cur, decoded_part, sizeof(decoded_part)))
			return NULL;
		rec = udb_record_find(ctx, decoded_part, rec);
		if (!rec)
			return NULL;
		cur = ds + 2;
	}
	if (!udb_path_decode_component(cur, decoded_part, sizeof(decoded_part)))
		return NULL;
	return udb_record_find(ctx, decoded_part, rec);
}

static void udb_record_free_tree(UdbRecord *rec)
{
	UdbRecord *child;

	if (!rec)
		return;
	child = rec->child;
	while (child)
	{
		UdbRecord *next = child->sibling;

		udb_record_free_tree(child);
		child = next;
	}
	if (rec->key && rec->is_dynamic_key)
		safe_free(rec->key);
	if (rec->data_str)
		safe_free(rec->data_str);
	safe_free(rec);
}

/* Candidate trees are deliberately unindexed: they are private until persisted. */
static UdbRecord *udb_record_clone_tree(UdbRecord *rec, UdbRecord *needle, UdbRecord **needle_clone)
{
	UdbRecord *copy;
	UdbRecord **child;
	UdbRecord *source_child;

	if (!rec)
		return NULL;
	copy = safe_alloc(sizeof(*copy));
	if (rec->key)
	{
		safe_strdup(copy->key, rec->key);
		copy->is_dynamic_key = 1;
	}
	if (rec->data_str)
		safe_strdup(copy->data_str, rec->data_str);
	copy->id = rec->id;
	copy->data_num = rec->data_num;
	copy->block_idx = rec->block_idx;
	copy->is_b64 = rec->is_b64;
	if (rec == needle && needle_clone)
		*needle_clone = copy;
	child = &copy->child;
	for (source_child = rec->child; source_child; source_child = source_child->sibling)
	{
		*child = udb_record_clone_tree(source_child, needle, needle_clone);
		(*child)->parent = copy;
		child = &(*child)->sibling;
	}
	return copy;
}

static unsigned int udb_record_count_tree(UdbRecord *rec)
{
	unsigned int count = 0;
	UdbRecord *child;

	if (!rec)
		return 0;
	for (child = rec->child; child; child = child->sibling)
		count += 1 + udb_record_count_tree(child);
	return count;
}

static UdbRecord *udb_record_insert_path(UdbRecord *tree, const char *path, const char *data)
{
	char pathbuf[UDB_RECORD_PATH_MAX + 1];
	char decoded_part[UDB_COMPONENT_RAW_MAX + 1];
	char *part;
	char *next;
	UdbRecord *parent = tree;

	if (!tree || !path || !*path || strlen(path) > UDB_RECORD_PATH_MAX || !udb_record_fits_limits(path, data))
		return NULL;
	strlcpy(pathbuf, path, sizeof(pathbuf));
	for (part = pathbuf; part; part = next)
	{
		next = strstr(part, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
			if (!*next)
				return NULL;
		}
		if (!*part)
			return NULL;
		if (!udb_path_decode_component(part, decoded_part, sizeof(decoded_part)))
			return NULL;
		UdbRecord *rec = udb_record_find(NULL, decoded_part, parent);
		if (!rec)
		{
			rec = udb_record_create(parent);
			safe_strdup(rec->key, decoded_part);
			rec->is_dynamic_key = 1;
		}
		parent = rec;
	}
	if (parent->data_str)
		safe_free(parent->data_str);
	if (data && *data == '*')
	{
		if (!udb_strtoul_strict(data + 1, &parent->data_num))
			return NULL;
		parent->data_str = NULL;
	}
	else if (data)
	{
		safe_strdup(parent->data_str, data);
		parent->data_num = 0;
	}
	return parent;
}

static void udb_record_delete_tree(UdbRecord *rec)
{
	UdbRecord **link;

	if (!rec || !rec->parent)
		return;
	for (link = &rec->parent->child; *link; link = &(*link)->sibling)
		if (*link == rec)
		{
			*link = rec->sibling;
			udb_record_free_tree(rec);
			return;
		}
}

/* ========================================================================
 * File Persistence
 * ======================================================================== */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf, size_t pathlen)
{
	UdbRecord *child;
	size_t old_len;
	char encoded_key[UDB_COMPONENT_ENCODED_MAX + 1];

	if (!rec || !rec->key)
		return 1;
	old_len = strlen(pathbuf);
	if (!udb_path_encode_component(rec->key, encoded_key, sizeof(encoded_key)))
		return 0;
	size_t enc_len = strlen(encoded_key);
	if (depth > 0)
	{
		if (old_len + 2 + enc_len >= pathlen)
			return 0;
		strlcat(pathbuf, "::", pathlen);
	}
	else
	{
		if (enc_len >= pathlen)
			return 0;
	}
	strlcat(pathbuf, encoded_key, pathlen);
	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		int ret;
		size_t line_len;
		char numbuf[32];
		const char *val_str;

		if (rec->data_str)
		{
			val_str = rec->data_str;
		}
		else
		{
			snprintf(numbuf, sizeof(numbuf), "*%lu", rec->data_num);
			val_str = numbuf;
		}

		if (!udb_record_fits_limits(pathbuf, val_str))
		{
			pathbuf[old_len] = '\0';
			return 0;
		}

		/* path + space + value + newline */
		line_len = strlen(pathbuf) + 1 + strlen(val_str) + 1;
		if (line_len > UDB_RECORD_LINE_MAX)
		{
			pathbuf[old_len] = '\0';
			return 0;
		}

		ret = fprintf(fp, "%s %s\n", pathbuf, val_str);
		if (ret < 0 || (size_t)ret != line_len || ferror(fp))
		{
			pathbuf[old_len] = '\0';
			return 0;
		}
	}
	for (child = rec->child; child; child = child->sibling)
	{
		if (!udb_serialize_tree(child, depth + 1, fp, pathbuf, pathlen))
		{
			pathbuf[old_len] = '\0';
			return 0;
		}
	}
	pathbuf[old_len] = '\0';
	return 1;
}

static int udb_file_cleanup_snapshot_temp(UdbBlock *block)
{
	char tmp_path[UDB_BLOCK_PATH_MAX];
	struct stat st;
	int saved_errno;

	if (!block || !block->filepath)
		return 1;
	if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath) >= (int)sizeof(tmp_path))
	{
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL, "Temporary snapshot path is too long for block $block",
				log_data_string("block", block->filepath));
		return 0;
	}

	if (lstat(tmp_path, &st) != 0)
	{
		if (ENOENT == errno)
			return 1;
		saved_errno = errno;
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL, "Cannot inspect temporary snapshot $path: $error",
				log_data_string("path", tmp_path), log_data_string("error", strerror(saved_errno)));
		return 0;
	}
	if (S_ISLNK(st.st_mode))
	{
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL, "Refusing to remove symlink temporary snapshot $path",
				log_data_string("path", tmp_path));
		return 0;
	}
	if (!S_ISREG(st.st_mode))
	{
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL,
				"Refusing to remove non-regular temporary snapshot $path", log_data_string("path", tmp_path));
		return 0;
	}

	if (unlink(tmp_path) != 0)
	{
		if (ENOENT == errno)
			return 1;
		saved_errno = errno;
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL, "Cannot remove temporary snapshot $path: $error",
				log_data_string("path", tmp_path), log_data_string("error", strerror(saved_errno)));
		return 0;
	}

	return 1;
}

static UdbSnapshotResult udb_file_write_snapshot(UdbBlock *block, UdbRecord *tree, unsigned int record_count)
{
	char dir_path[UDB_BLOCK_PATH_MAX];
	char tmp_path[UDB_BLOCK_PATH_MAX];
	char pathbuf[UDB_RECORD_PATH_MAX + 1] = "";
	char *slash;
	FILE *fp = NULL;
	UdbRecord *rec;
	int dir_fd = -1;
	int fd = -1;
	int flags = O_WRONLY | O_CREAT | O_EXCL;
	int tmp_created = 0;
	int saved_errno;

	if (!block || !block->filepath)
		return UDB_SNAPSHOT_FAILED_BEFORE_COMMIT;
	if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath) >= (int)sizeof(tmp_path))
		return UDB_SNAPSHOT_FAILED_BEFORE_COMMIT;
	strlcpy(dir_path, block->filepath, sizeof(dir_path));
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
		return UDB_SNAPSHOT_FAILED_BEFORE_COMMIT;
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
	if (fprintf(fp, "; UDB Block %c - Version %d\n", block->letter, block->version) < 0 ||
		fprintf(fp, "; Saved: %ld\n", (long)time(NULL)) < 0 || fprintf(fp, "; Records: %u\n", record_count) < 0)
		goto cleanup;
	if (tree)
	{
		for (rec = tree->child; rec; rec = rec->sibling)
		{
			if (!udb_serialize_tree(rec, 0, fp, pathbuf, sizeof(pathbuf)))
				goto cleanup;
		}
	}
	if (fflush(fp) != 0 || ferror(fp))
		goto cleanup;
	if (fsync(fileno(fp)) != 0)
		goto cleanup;
	if (fclose(fp) != 0)
	{
		fp = NULL;
		goto cleanup;
	}
	fp = NULL;
	if (rename(tmp_path, block->filepath) != 0)
		goto cleanup;
	tmp_created = 0;
	if (fsync(dir_fd) != 0)
	{
		saved_errno = errno;
		udb_log(ULOG_ERROR, "UDB_SNAPSHOT_DIR_FSYNC_FAILED", NULL,
				"Failed to fsync directory for block $block after commit: $error",
				log_data_string("block", block->filepath), log_data_string("error", strerror(saved_errno)));
		if (close(dir_fd) != 0)
		{
			saved_errno = errno;
			udb_log(ULOG_ERROR, "UDB_SNAPSHOT_DIR_CLOSE_FAILED", NULL,
					"Failed to close directory descriptor for block $block after commit: $error",
					log_data_string("block", block->filepath), log_data_string("error", strerror(saved_errno)));
		}
		return UDB_SNAPSHOT_COMMITTED_DURABILITY_UNCERTAIN;
	}
	if (close(dir_fd) != 0)
	{
		saved_errno = errno;
		udb_log(ULOG_ERROR, "UDB_SNAPSHOT_DIR_CLOSE_FAILED", NULL,
				"Failed to close directory descriptor for block $block after commit: $error",
				log_data_string("block", block->filepath), log_data_string("error", strerror(saved_errno)));
		return UDB_SNAPSHOT_COMMITTED_DURABILITY_UNCERTAIN;
	}
	return UDB_SNAPSHOT_COMMITTED;

cleanup:
	if (fp)
		fclose(fp);
	else if (fd >= 0)
		close(fd);
	if (dir_fd >= 0)
		close(dir_fd);
	if (tmp_created)
		unlink(tmp_path);
	return UDB_SNAPSHOT_FAILED_BEFORE_COMMIT;
}

static int udb_file_save_block(UdbContext *ctx, UdbBlock *block)
{
	struct stat st;
	UdbSnapshotResult res;

	if (!block)
		return 0;
	res = udb_file_write_snapshot(block, block->tree, block->record_count);
	if (res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		return 0;
	(void)ctx;
	if (!udb_compute_block_checksum(block, &block->checksum))
	{
		block->checksum = 0;
		udb_log(ULOG_ERROR, "UDB_CHECKSUM_CALC_FAILED", NULL, "Failed to compute checksum for block $block after save",
				log_data_string("block", (char[]){block->letter, '\0'}));
	}
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
	return 1;
}
