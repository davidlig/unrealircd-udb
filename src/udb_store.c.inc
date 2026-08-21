/*
 * UDB record store primitives.
 *
 * These helpers only manage record trees, indexes, paths, and on-disk
 * serialization. Runtime effects remain in udb_core.c.inc.
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
	static const char *known_keys[] = {
	    "pass", "vhost", "oper", "swhois", "snomasks", "modes",
	    "access", "forbid", "suspended", "challenge", "founder", "topic",
	    "options", "clones", "nolines", "host", "encryption_key", "suffix",
	    "nickserv", "chanserv", "ipserv", "quit_ips", "quit_clones", "flood",
	    "type", "action", "duration", "reason", NULL};

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

static UdbRecord *udb_record_find_path(UdbContext *ctx, UdbBlock *block, const char *path)
{
	char pathbuf[512];
	char *cur;
	char *ds;
	UdbRecord *rec;

	if (!block || !block->tree || !path)
		return NULL;
	strlcpy(pathbuf, path, sizeof(pathbuf));
	cur = pathbuf;
	rec = block->tree;
	while ((ds = strstr(cur, "::")))
	{
		*ds = '\0';
		rec = udb_record_find(ctx, cur, rec);
		if (!rec)
			return NULL;
		cur = ds + 2;
	}
	return udb_record_find(ctx, cur, rec);
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

/* ========================================================================
 * File Persistence
 * ======================================================================== */
static void udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf, int pathlen)
{
	UdbRecord *child;
	size_t old_len;

	if (!rec)
		return;
	old_len = strlen(pathbuf);
	if (depth > 0)
		strlcat(pathbuf, "::", pathlen);
	strlcat(pathbuf, rec->key, pathlen);
	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		if (rec->data_str)
			fprintf(fp, "%s %s\n", pathbuf, rec->data_str);
		else if (rec->data_num > 0 || !rec->child)
			fprintf(fp, "%s *%lu\n", pathbuf, rec->data_num);
	}
	for (child = rec->child; child; child = child->sibling)
		udb_serialize_tree(child, depth + 1, fp, pathbuf, pathlen);
	pathbuf[old_len] = '\0';
}

static int udb_file_write_snapshot(UdbBlock *block, UdbRecord *tree,
                                   unsigned int record_count)
{
	char tmp_path[512];
	char pathbuf[4096] = "";
	FILE *fp;
	UdbRecord *rec;

	if (!block || !block->filepath)
		return 0;
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath);
	fp = fopen(tmp_path, "w");
	if (!fp)
		return 0;
	fprintf(fp, "; UDB Block %c - Version %d\n", block->letter, block->version);
	fprintf(fp, "; Saved: %ld\n", (long)time(NULL));
	fprintf(fp, "; Records: %u\n", record_count);
	if (tree)
		for (rec = tree->child; rec; rec = rec->sibling)
			udb_serialize_tree(rec, 0, fp, pathbuf, sizeof(pathbuf));
	if (fclose(fp) != 0 || rename(tmp_path, block->filepath) != 0)
	{
		unlink(tmp_path);
		return 0;
	}
	return 1;
}

static int udb_file_save_block(UdbContext *ctx, UdbBlock *block)
{
	struct stat st;

	if (!block || !udb_file_write_snapshot(block, block->tree, block->record_count))
		return 0;
	(void)ctx;
	block->checksum = udb_compute_block_checksum(block);
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
	return 1;
}
