/*
 * UDB Core Engine for UnrealIRCd 6
 * Implements the database structures, hash, file I/O, and record manipulation.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Forward declare prototypes from other inc files to prevent compiler warnings */
static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync);
static void udb_nick_strip(Client *client, UdbRecord *nick_rec);
static void udb_nick_revoke_oper(Client *client);
static void udb_channel_apply_record(Channel *channel, UdbRecord *chan_rec, const char *subkey, int is_new);
static void udb_channel_remove_record(Channel *channel, UdbRecord *chan_rec, const char *subkey);
static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey, int is_new);
static void udb_ip_remove_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey);
static void udb_ips_shutdown(void);
static void udb_line_apply_record(UdbRecord *line_rec, int is_new);
static void udb_line_remove_record(UdbRecord *line_rec);
static void udb_block_set_context_root(UdbBlock *block);

static int udb_setting_string_valid(const char *value)
{
	return value && *value && !strpbrk(value, "\r\n");
}

static int udb_suffix_valid(const char *value)
{
	const unsigned char *p = (const unsigned char *)value;
	const char *label;

	/* The derived label is 32 hexadecimal characters. */
	if (!value || !*value || strlen(value) > HOSTLEN - 32 || value[0] != '.')
		return 0;
	label = value + 1;
	for (; *p; p++)
	{
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || *p == '.' || *p == '-'))
			return 0;
		if (*p == '.')
		{
			if (p == (const unsigned char *)label || p[-1] == '-')
				return 0;
			label = (const char *)p + 1;
		} else if (*p == '-' && p == (const unsigned char *)label)
		{
			return 0;
		}
	}
	return value[1] && *label && value[strlen(value) - 1] != '-' &&
	       value[strlen(value) - 1] != '.';
}

static int udb_encryption_key_valid(const char *value)
{
	size_t i;

	/* A fixed-size hexadecimal key keeps the setting portable and unambiguous. */
	if (!value || strlen(value) != 64)
		return 0;
	for (i = 0; value[i]; i++)
		if (!isxdigit((unsigned char)value[i]))
			return 0;
	return 1;
}

static int udb_service_mask_valid(const char *value)
{
	const char *bang;
	const char *at;

	if (!udb_setting_string_valid(value) || strpbrk(value, " \t"))
		return 0;
	bang = strchr(value, '!');
	at = bang ? strchr(bang + 1, '@') : NULL;
	return bang && bang != value && at && at != bang + 1 && at[1];
}

static int udb_flood_valid(const char *value, int *attempts, int *period)
{
	char *end;
	long parsed_attempts;
	long parsed_period;

	if (!value || !*value)
		return 0;
	parsed_attempts = strtol(value, &end, 10);
	if (end == value || *end++ != ':')
		return 0;
	parsed_period = strtol(end, &end, 10);
	if (*end || parsed_attempts <= 0 || parsed_period <= 0 ||
	    parsed_attempts > INT_MAX || parsed_period > INT_MAX)
		return 0;
	*attempts = (int)parsed_attempts;
	*period = (int)parsed_period;
	return 1;
}

static int udb_password_record_valid(UdbBlock *block, const char *path,
                                     const char *value)
{
	const char *leaf;
	size_t i;

	if (!block || (block->letter != 'N' && block->letter != 'C') || !path)
		return 1;
	leaf = strrchr(path, ':');
	leaf = leaf ? leaf + 1 : path;
	if (!strcasecmp(leaf, NKEY_CHALLENGE))
		return value && (!strcasecmp(value, "argon2id") ||
		                 !strcasecmp(value, "sha256") ||
		                 !strcasecmp(value, "crypt"));
	if (strcasecmp(leaf, NKEY_PASS))
		return 1;
	if (!value)
		return 0;
	if (!strncmp(value, "argon2id:$argon2id$", 19))
		return 1;
	if (!strncmp(value, "crypt:", 6))
		return value[6] != '\0';
	if (strncmp(value, "sha256:", 7) || strlen(value + 7) != 64)
		return 0;
	for (i = 7; value[i]; i++)
		if (!isxdigit((unsigned char)value[i]))
			return 0;
	return 1;
}

static void udb_settings_replace(char **destination, const char *value)
{
	safe_free(*destination);
	if (value)
		safe_strdup(*destination, value);
}

static void udb_settings_restore_flood(void)
{
	if (!udb_cfg)
		return;
	udb_cfg->flood_attempts = udb_cfg->config_flood_attempts;
	udb_cfg->flood_period = udb_cfg->config_flood_period;
}

static int udb_settings_apply_record(UdbRecord *rec)
{
	int attempts;
	int period;

	if (!rec || !rec->key)
		return 0;
	if (!strcmp(rec->key, SKEY_QUIT_IPS))
	{
		if (!udb_setting_string_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->quit_ips, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_QUIT_CLONES))
	{
		if (!udb_setting_string_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->quit_clones, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_FLOOD))
	{
		if (!udb_flood_valid(rec->data_str, &attempts, &period))
			return 0;
		if (udb_cfg)
		{
			udb_cfg->flood_attempts = attempts;
			udb_cfg->flood_period = period;
		}
	} else if (!strcmp(rec->key, SKEY_SUFFIX))
	{
		if (!udb_suffix_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->suffix, rec->data_str);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_CRYPT_KEY))
	{
		if (!udb_encryption_key_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->encryption_key, rec->data_str);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_NICKSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->nickserv_mask, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_CHANSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->chanserv_mask, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_IPSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->ipserv_mask, rec->data_str);
	} else
	{
		return 0;
	}
	return 1;
}

static void udb_settings_remove_record(UdbRecord *rec)
{
	if (!rec || !rec->key)
		return;
	if (!strcmp(rec->key, SKEY_QUIT_IPS))
		udb_settings_replace(&udb_ctx->quit_ips, NULL);
	else if (!strcmp(rec->key, SKEY_QUIT_CLONES))
		udb_settings_replace(&udb_ctx->quit_clones, NULL);
	else if (!strcmp(rec->key, SKEY_FLOOD))
		udb_settings_restore_flood();
	else if (!strcmp(rec->key, SKEY_SUFFIX))
	{
		udb_settings_replace(&udb_ctx->suffix, NULL);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_CRYPT_KEY))
	{
		udb_settings_replace(&udb_ctx->encryption_key, NULL);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_NICKSERV))
		udb_settings_replace(&udb_ctx->nickserv_mask, NULL);
	else if (!strcmp(rec->key, SKEY_CHANSERV))
		udb_settings_replace(&udb_ctx->chanserv_mask, NULL);
	else if (!strcmp(rec->key, SKEY_IPSERV))
		udb_settings_replace(&udb_ctx->ipserv_mask, NULL);
}

static void udb_link_apply_record(UdbRecord *rec)
{
	if (!rec || !rec->parent || !rec->key || strcmp(rec->key, LKEY_OPTIONS))
		return;
	udb_ctx->propagator = NULL;
	if (rec->data_str || (rec->data_num & ~(UDB_LNKOPT_DEBUG | UDB_LNKOPT_PROPAGATOR | UDB_LNKOPT_ALLOW_CLIENTS)))
		udb_log(ULOG_WARNING, "UDB_LINK_OPTIONS", NULL,
		        "Ignoring invalid L::options for $server", log_data_string("server", rec->parent->key));
	if (rec->data_num & UDB_LNKOPT_ALLOW_CLIENTS)
		udb_log(ULOG_WARNING, "UDB_LINK_ALLOW_CLIENTS", NULL,
		        "L::options allow-clients is validated but not enforced: no safe UnrealIRCd hook is available");
}

static void udb_link_remove_record(UdbRecord *rec)
{
	if (rec && rec->key && !strcmp(rec->key, LKEY_OPTIONS))
		udb_ctx->propagator = NULL;
}

/* ========================================================================
 * Block Index Helpers
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

/* ========================================================================
 * Hash Operations
 * ======================================================================== */
static void udb_hash_init(void)
{
	for (int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		udb_ctx->hash_table[i] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
	}
}

static void udb_hash_destroy(void)
{
	for (int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		if (udb_ctx->hash_table[i])
		{
			safe_free(udb_ctx->hash_table[i]);
			udb_ctx->hash_table[i] = NULL;
		}
	}
}

static void udb_hash_clear_block(int block_idx)
{
	if (block_idx < 0 || block_idx >= UDB_NUM_BLOCKS)
		return;
	safe_free(udb_ctx->hash_table[block_idx]);
	udb_ctx->hash_table[block_idx] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
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

static void udb_hash_insert_record(UdbRecord *rec, int block_idx, const char *key)
{
	unsigned int h = udb_hash_str(key);
	rec->hash_next = udb_ctx->hash_table[block_idx][h];
	udb_ctx->hash_table[block_idx][h] = rec;
}

static int udb_hash_remove_record(UdbRecord *rec, int block_idx, const char *key)
{
	unsigned int h = udb_hash_str(key);
	UdbRecord *curr = udb_ctx->hash_table[block_idx][h];
	UdbRecord *prev = NULL;

	while (curr)
	{
		if (curr == rec)
		{
			if (prev)
				prev->hash_next = curr->hash_next;
			else
				udb_ctx->hash_table[block_idx][h] = curr->hash_next;
			return 1;
		}
		prev = curr;
		curr = curr->hash_next;
	}
	return 0;
}

static UdbRecord *udb_hash_find(int block_idx, const char *key)
{
	if (!key)
		return NULL;
	unsigned int h = udb_hash_str(key);
	UdbRecord *curr = udb_ctx->hash_table[block_idx][h];
	while (curr)
	{
		if (!strcasecmp(curr->key, key))
			return curr;
		curr = curr->hash_next;
	}
	return NULL;
}

/* ========================================================================
 * Record Operations
 * ======================================================================== */
static const char *udb_get_shared_subkey(const char *key)
{
	static const char *known_keys[] = {
	    "pass", "vhost", "oper", "swhois", "snomasks", "modes", "access",
	    "forbid", "suspended", "challenge", "founder", "topic", "options",
	    "clones", "nolines", "host", "encryption_key", "suffix", "nickserv",
	    "chanserv", "ipserv", "quit_ips", "quit_clones", "flood", "prefixes",
	    "type", "action", "duration", "reason", NULL};
	for (int i = 0; known_keys[i]; i++)
	{
		if (!strcasecmp(known_keys[i], key))
			return known_keys[i];
	}
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

static UdbRecord *udb_record_find(const char *key, UdbRecord *parent)
{
	if (!parent)
		return NULL;

	if (parent->parent == NULL && udb_ctx)
	{
		return udb_hash_find(parent->block_idx, key);
	}

	UdbRecord *child = parent->child;
	while (child)
	{
		if (!strcasecmp(child->key, key))
		{
			return child;
		}
		child = child->sibling;
	}
	return NULL;
}

static UdbRecord *udb_record_find_path(UdbBlock *block, const char *path)
{
	if (!block || !block->tree || !path)
		return NULL;
	char pathbuf[512];
	strlcpy(pathbuf, path, sizeof(pathbuf));
	char *cur = pathbuf;
	char *ds;
	UdbRecord *rec = block->tree;
	while ((ds = strstr(cur, "::")))
	{
		*ds = '\0';
		rec = udb_record_find(cur, rec);
		if (!rec)
			return NULL;
		cur = ds + 2;
	}
	return udb_record_find(cur, rec);
}

static UdbRecord *udb_record_insert(UdbBlock *block, UdbRecord *parent, const char *key, const char *data_str, unsigned long data_num, int persist)
{
	if (!parent)
		parent = block->tree;
	UdbRecord *rec = udb_record_find(key, parent);
	if (!rec)
	{
		rec = udb_record_create(parent);
		if (key)
		{
			if (parent == block->tree)
			{
				safe_strdup(rec->key, key);
				rec->is_dynamic_key = 1;
			} else
			{
				const char *shared = udb_get_shared_subkey(key);
				if (shared)
				{
					rec->key = (char *)shared;
					rec->is_dynamic_key = 0;
				} else
				{
					safe_strdup(rec->key, key);
					rec->is_dynamic_key = 1;
				}
			}
		}
		if (parent == block->tree)
		{
			udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), key);
		}
		block->record_count++;
		udb_ctx->total_records++;
	}

	if (rec->data_str)
	{
		safe_free(rec->data_str);
	}

	// Auto-detect numeric data if it starts with *
	if (data_str && *data_str == '*')
	{
		rec->data_num = atoi(data_str + 1);
		rec->data_str = NULL;
	} else if (data_str)
	{
		safe_strdup(rec->data_str, data_str);
		rec->data_num = 0;
	} else
	{
		rec->data_str = NULL;
		rec->data_num = data_num;
	}

	if (persist)
	{
		udb_file_save_block(block);
	}

	udb_apply_special_record(block, rec, 1);

	return rec;
}

static void udb_record_free_tree(UdbRecord *rec)
{
	if (!rec)
		return;

	UdbRecord *child = rec->child;
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

static UdbRecord *udb_record_delete(UdbBlock *block, UdbRecord *rec, int persist)
{
	UdbRecord *line_rec = NULL;
	if (!rec)
		return NULL;

	/* A K property deletion changes its owning pattern, not a line of its own. */
	if (block->letter == 'K' && rec->parent && rec->parent->parent &&
	    rec->parent->parent != block->tree)
		line_rec = rec->parent;

	udb_remove_special_record(block, rec);

	if (rec->parent)
	{
		if (rec->parent->parent == NULL)
		{
			udb_hash_remove_record(rec, udb_block_letter_to_index(block->letter), rec->key);
		}

		UdbRecord *curr = rec->parent->child;
		UdbRecord *prev = NULL;
		while (curr)
		{
			if (curr == rec)
			{
				if (prev)
					prev->sibling = curr->sibling;
				else
					rec->parent->child = curr->sibling;
				break;
			}
			prev = curr;
			curr = curr->sibling;
		}
	}

	if (block->record_count > 0)
		block->record_count--;
	if (udb_ctx->total_records > 0)
		udb_ctx->total_records--;

	udb_record_free_tree(rec);

	/* Rebuild a surviving K pattern after one of its properties was removed. */
	if (line_rec)
		udb_line_apply_record(line_rec, 0);

	if (persist)
	{
		udb_file_save_block(block);
	}
	return NULL;
}

/* ========================================================================
 * Checksum Operations
 * ======================================================================== */
static unsigned long udb_crc32_step(unsigned long crc, const char *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		crc ^= (unsigned char)data[i];
		for (int j = 0; j < 8; j++)
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320UL;
			else
				crc >>= 1;
		}
	}
	return crc;
}

static unsigned long udb_crc32(const char *data, size_t len)
{
	return udb_crc32_step(0xFFFFFFFFUL, data, len) ^ 0xFFFFFFFFUL;
}

static unsigned long udb_compute_block_checksum(UdbBlock *block)
{
	return block ? udb_compute_tree_checksum(block->tree) : 0;
}

typedef struct UdbDigestLine {
	char *line;
	struct UdbDigestLine *next;
} UdbDigestLine;

static void udb_digest_collect(UdbRecord *rec, int depth, char *pathbuf, int pathlen,
                               UdbDigestLine **lines)
{
	size_t old_len;
	UdbRecord *child;

	if (!rec || !rec->key)
		return;
	old_len = strlen(pathbuf);
	if (depth > 0)
		strlcat(pathbuf, "::", pathlen);
	strlcat(pathbuf, rec->key, pathlen);
	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		UdbDigestLine *line = safe_alloc(sizeof(*line));
		size_t len = strlen(pathbuf) + 2 + (rec->data_str ? strlen(rec->data_str) : 32);
		line->line = safe_alloc(len);
		if (rec->data_str)
			snprintf(line->line, len, "%s %s", pathbuf, rec->data_str);
		else
			snprintf(line->line, len, "%s *%lu", pathbuf, rec->data_num);
		line->next = *lines;
		*lines = line;
	}
	for (child = rec->child; child; child = child->sibling)
		udb_digest_collect(child, depth + 1, pathbuf, pathlen, lines);
	pathbuf[old_len] = '\0';
}

static int udb_digest_line_cmp(const void *a, const void *b)
{
	const UdbDigestLine *const *left = a;
	const UdbDigestLine *const *right = b;
	return strcmp((*left)->line, (*right)->line);
}

/* The digest covers sorted logical records, never save-time file headers/order. */
static unsigned long udb_compute_tree_checksum(UdbRecord *tree)
{
	UdbDigestLine *lines = NULL;
	UdbDigestLine *line;
	UdbDigestLine **sorted;
	unsigned long crc = 0xFFFFFFFFUL;
	unsigned int count = 0;
	unsigned int i = 0;
	char pathbuf[4096] = "";

	if (!tree)
		return 0;
	for (UdbRecord *rec = tree->child; rec; rec = rec->sibling)
		udb_digest_collect(rec, 0, pathbuf, sizeof(pathbuf), &lines);
	for (line = lines; line; line = line->next)
		count++;
	sorted = count ? safe_alloc(sizeof(*sorted) * count) : NULL;
	for (line = lines; line; line = line->next)
		sorted[i++] = line;
	if (count)
		qsort(sorted, count, sizeof(*sorted), udb_digest_line_cmp);
	for (i = 0; i < count; i++)
	{
		crc = udb_crc32_step(crc, sorted[i]->line, strlen(sorted[i]->line));
		crc = udb_crc32_step(crc, "\n", 1);
	}
	for (line = lines; line;)
	{
		UdbDigestLine *next = line->next;
		safe_free(line->line);
		safe_free(line);
		line = next;
	}
	safe_free(sorted);
	return crc ^ 0xFFFFFFFFUL;
}

static UdbRecord *udb_stage_find(UdbRecord *parent, const char *key)
{
	UdbRecord *rec;
	for (rec = parent ? parent->child : NULL; rec; rec = rec->sibling)
		if (!strcasecmp(rec->key, key))
			return rec;
	return NULL;
}

static UdbRecord *udb_stage_insert(UdbRecord *parent, const char *key,
                                   UdbSyncSession *session)
{
	UdbRecord *rec = udb_stage_find(parent, key);
	if (!rec)
	{
		rec = udb_record_create(parent);
		safe_strdup(rec->key, key);
		rec->is_dynamic_key = 1;
		session->record_count++;
	}
	return rec;
}

static int udb_stage_parse_line(UdbBlock *block, UdbSyncSession *session, const char *input)
{
	char line[4096];
	char *value, *part, *next;
	UdbRecord *parent;

	if (!block || !session || !session->tree || !input || !*input || strlen(input) >= sizeof(line))
		return 0;
	strlcpy(line, input, sizeof(line));
	value = strchr(line, ' ');
	if (value)
		*value++ = '\0';
	if (!*line)
		return 0;
	if (!udb_password_record_valid(block, line, value))
		return 0;
	parent = session->tree;
	part = line;
	while (part && *part)
	{
		next = strstr(part, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
			if (!*next)
				return 0;
		}
		if (!*part)
			return 0;
		parent = udb_stage_insert(parent, part, session);
		part = next;
	}
	if (value && *value == '*')
	{
		char *end;
		unsigned long n = strtoul(value + 1, &end, 10);
		if (end == value + 1 || *end)
			return 0;
		safe_free(parent->data_str);
		parent->data_num = n;
	} else if (value)
	{
		safe_free(parent->data_str);
		safe_strdup(parent->data_str, value);
		parent->data_num = 0;
	} else
	{
		return 0;
	}
	return 1;
}

static int udb_stage_persist_block(UdbBlock *block, UdbSyncSession *session)
{
	char tmp_path[512];
	char pathbuf[4096] = "";
	FILE *fp;
	UdbRecord *rec;

	if (!block || !session || !session->tree || !block->filepath)
		return 0;
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath);
	fp = fopen(tmp_path, "w");
	if (!fp)
		return 0;
	fprintf(fp, "; UDB Block %c - Version %d\n", block->letter, block->version);
	fprintf(fp, "; Saved: %ld\n", (long)time(NULL));
	fprintf(fp, "; Records: %u\n", session->record_count);
	for (rec = session->tree->child; rec; rec = rec->sibling)
		udb_serialize_tree(rec, 0, fp, pathbuf, sizeof(pathbuf));
	if (fclose(fp) != 0 || rename(tmp_path, block->filepath) != 0)
		return 0;
	return 1;
}

static void udb_sync_session_free(UdbBlock *block)
{
	if (!block || !block->session)
		return;
	udb_record_free_tree(block->session->tree);
	safe_free(block->session);
	block->session = NULL;
	block->syncing_from = NULL;
}

static int udb_block_commit_stage(UdbBlock *block, UdbSyncSession *session,
                                  unsigned long checksum)
{
	UdbRecord *rec;
	struct stat st;

	if (!block || !session || block->session != session)
		return 0;
	/* Persistence succeeded before this point; only now may active state move. */
	udb_block_reset(block);
	udb_record_free_tree(block->tree);
	block->tree = session->tree;
	session->tree = NULL;
	block->record_count = session->record_count;
	udb_ctx->total_records += block->record_count;
	for (rec = block->tree->child; rec; rec = rec->sibling)
		udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), rec->key);
	udb_block_set_context_root(block);
	block->checksum = checksum;
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
	udb_sync_session_free(block);

	for (rec = block->tree->child; rec; rec = rec->sibling)
	{
		UdbRecord *child;
		udb_apply_special_record(block, rec, 1);
		for (child = rec->child; child; child = child->sibling)
			udb_apply_special_record(block, child, 1);
	}
	return 1;
}

/* ========================================================================
 * File I/O Operations
 * ======================================================================== */
static UdbRecord *udb_file_parse_line(UdbBlock *block, char *line)
{
	if (!line || !*line || *line == ';')
		return NULL;

	char *value = strchr(line, ' ');
	char *data_str = NULL;
	unsigned long data_num = 0;

	if (value)
	{
		*value++ = '\0';
		if (*value == '*')
		{
			data_num = strtoul(value + 1, NULL, 10);
		} else
		{
			data_str = value;
		}
	}
	if (!udb_password_record_valid(block, line, data_str))
		return NULL;

	char *p = line;
	UdbRecord *parent = block->tree;
	UdbRecord *leaf_rec = NULL;
	while (p && *p)
	{
		char *next = strstr(p, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
		}

		UdbRecord *rec = udb_record_find(p, parent);
		if (!rec)
		{
			rec = udb_record_create(parent);
			if (p && *p)
			{
				if (parent == block->tree)
				{
					safe_strdup(rec->key, p);
					rec->is_dynamic_key = 1;
				} else
				{
					const char *shared = udb_get_shared_subkey(p);
					if (shared)
					{
						rec->key = (char *)shared;
						rec->is_dynamic_key = 0;
					} else
					{
						safe_strdup(rec->key, p);
						rec->is_dynamic_key = 1;
					}
				}
			}
			if (parent == block->tree)
			{
				udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), p);
			}
			block->record_count++;
			udb_ctx->total_records++;
		}

		if (!next)
		{
			if (data_str && *data_str == '*')
			{
				rec->data_num = atoi(data_str + 1);
				if (rec->data_str)
				{
					safe_free(rec->data_str);
					rec->data_str = NULL;
				}
			} else if (data_str)
			{
				safe_strdup(rec->data_str, data_str);
				rec->data_num = 0;
			} else
			{
				if (rec->data_str)
				{
					safe_free(rec->data_str);
					rec->data_str = NULL;
				}
				rec->data_num = data_num;
			}
			leaf_rec = rec;
		}

		parent = rec;
		p = next;
	}
	return leaf_rec;
}

static void udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf, int pathlen)
{
	if (!rec)
		return;

	size_t old_len = strlen(pathbuf);

	if (depth > 0)
	{
		strlcat(pathbuf, "::", pathlen);
	}
	strlcat(pathbuf, rec->key, pathlen);

	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		if (rec->data_str)
		{
			fprintf(fp, "%s %s\n", pathbuf, rec->data_str);
		} else if (rec->data_num > 0 || !rec->child)
		{
			fprintf(fp, "%s *%lu\n", pathbuf, rec->data_num);
		}
	}

	if (rec->child)
	{
		UdbRecord *child = rec->child;
		while (child)
		{
			udb_serialize_tree(child, depth + 1, fp, pathbuf, pathlen);
			child = child->sibling;
		}
	}

	// Restore pathbuf
	pathbuf[old_len] = '\0';
}

static int udb_file_save_block(UdbBlock *block)
{
	if (!block || !block->filepath)
		return 0;

	char tmp_path[512];
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath);
	FILE *fp = fopen(tmp_path, "w");
	if (!fp)
		return 0;

	fprintf(fp, "; UDB Block %c - Version %d\n", block->letter, block->version);
	fprintf(fp, "; Saved: %ld\n", (long)time(NULL));
	fprintf(fp, "; Records: %u\n", block->record_count);

	char pathbuf[4096];
	pathbuf[0] = '\0';

	if (block->tree)
	{
		UdbRecord *rec = block->tree->child;
		while (rec)
		{
			udb_serialize_tree(rec, 0, fp, pathbuf, sizeof(pathbuf));
			rec = rec->sibling;
		}
	}

	fclose(fp);

	rename(tmp_path, block->filepath);
	block->checksum = udb_compute_block_checksum(block);
	block->modified_at = time(NULL);

	struct stat st;
	if (stat(block->filepath, &st) == 0)
	{
		block->filesize = st.st_size;
	}

	return 1;
}

static int udb_file_load_block(UdbBlock *block)
{
	if (!block || !block->filepath)
		return 0;
	FILE *fp = fopen(block->filepath, "r");
	if (!fp)
		return 0;

	char line[4096];
	while (fgets(line, sizeof(line), fp))
	{
		char *p = strchr(line, '\n');
		if (p)
			*p = '\0';
		p = strchr(line, '\r');
		if (p)
			*p = '\0';

		if (line[0] == ';' || line[0] == '\0')
			continue;

		udb_file_parse_line(block, line);
	}
	fclose(fp);

	UdbRecord *curr = block->tree->child;
	while (curr)
	{
		UdbRecord *sub = curr->child;
		while (sub)
		{
			udb_apply_special_record(block, sub, 1);
			sub = sub->sibling;
		}
		udb_apply_special_record(block, curr, 1);
		curr = curr->sibling;
	}

	block->checksum = udb_compute_block_checksum(block);

	struct stat st;
	if (stat(block->filepath, &st) == 0)
	{
		block->filesize = st.st_size;
		block->modified_at = st.st_mtime;
	}
	char logbuf[512];
	snprintf(logbuf, sizeof(logbuf), "[UDB] Loaded block %c from %s (%u records)", block->letter, block->filepath, block->record_count);
	unreal_log(ULOG_INFO, "udb", "UDB_FILE_LOADED", NULL, "$msg", log_data_string("msg", logbuf));

	return 1;
}

/* ========================================================================
 * Block Management
 * ======================================================================== */
static UdbBlock *udb_block_create(char letter, const char *name)
{
	UdbBlock *b = safe_alloc(sizeof(UdbBlock));
	b->letter = letter;
	b->version = 1;
	b->tree = udb_record_create(NULL);
	b->tree->block_idx = (unsigned char)udb_block_letter_to_index(letter);
	safe_strdup(b->tree->key, name);
	b->tree->data_num = 1;

	char path[512];
	snprintf(path, sizeof(path), "udb_%c.db", letter);
	safe_strdup(b->filepath, path);
	convert_to_absolute_path(&b->filepath, PERMDATADIR);

	udb_ctx->blocks[(unsigned char)letter] = b;
	b->next = udb_ctx->block_list;
	udb_ctx->block_list = b;

	udb_ctx->block_count++;
	return b;
}

static void udb_block_set_context_root(UdbBlock *block)
{
	if (!udb_ctx || !block)
		return;
	switch (block->letter)
	{
		case 'N':
			udb_ctx->nicks = block->tree;
			break;
		case 'C':
			udb_ctx->channels = block->tree;
			break;
		case 'I':
			udb_ctx->ips = block->tree;
			break;
		case 'S':
			udb_ctx->settings = block->tree;
			break;
		case 'L':
			udb_ctx->links = block->tree;
			break;
		case 'K':
			udb_ctx->lines = block->tree;
			break;
	}
}

static void udb_block_reset(UdbBlock *block)
{
	char *name = NULL;
	int block_idx;

	if (!block)
		return;

	if (block->letter == 'I' && block->tree)
	{
		UdbRecord *rec;
		for (rec = block->tree->child; rec; rec = rec->sibling)
		{
			UdbRecord *child;
			for (child = rec->child; child; child = child->sibling)
				udb_ip_remove_record(rec->key, rec, child->key);
		}
	}
	if ((block->letter == 'S' || block->letter == 'L') && block->tree)
	{
		UdbRecord *rec;
		for (rec = block->tree->child; rec; rec = rec->sibling)
			udb_remove_special_record(block, rec);
	}

	if (block->tree && block->tree->key)
		safe_strdup(name, block->tree->key);
	else
		safe_strdup(name, "UDB");
	block_idx = udb_block_letter_to_index(block->letter);

	if (block->tree)
	{
		if (udb_ctx->total_records >= block->record_count)
			udb_ctx->total_records -= block->record_count;
		else
			udb_ctx->total_records = 0;
		udb_record_free_tree(block->tree);
	}
	udb_hash_clear_block(block_idx);

	block->tree = udb_record_create(NULL);
	block->tree->block_idx = (unsigned char)block_idx;
	safe_strdup(block->tree->key, name);
	block->tree->is_dynamic_key = 1;
	block->tree->data_num = 1;
	block->record_count = 0;
	udb_block_set_context_root(block);
	safe_free(name);
}

static int udb_block_load(UdbBlock *block)
{
	return udb_file_load_block(block);
}

static void udb_block_unload(UdbBlock *block)
{
	if (block->tree)
	{
		udb_record_free_tree(block->tree);
		block->tree = NULL;
	}
}

static void udb_blocks_load_all(void)
{
	UdbBlock *b = udb_ctx->block_list;
	while (b)
	{
		udb_block_load(b);
		b = b->next;
	}
}

static void udb_blocks_save_all(void)
{
	UdbBlock *b = udb_ctx->block_list;
	while (b)
	{
		udb_file_save_block(b);
		b = b->next;
	}
}

static UdbBlock *udb_block_by_letter(char letter)
{
	return udb_ctx ? udb_ctx->blocks[(unsigned char)letter] : NULL;
}

/* ========================================================================
 * Initialization and Shutdown
 * ======================================================================== */
static int udb_engine_init(void)
{
	udb_ctx = safe_alloc(sizeof(UdbContext));
	udb_hash_init();

	struct stat st = {0};
	const char *dir = udb_cfg && udb_cfg->db_directory ? udb_cfg->db_directory : "data/udb";
	if (stat(dir, &st) == -1)
	{
		mkdir(dir, 0700);
	}

	udb_block_create('N', "Nicks");
	udb_block_create('C', "Channels");
	udb_block_create('I', "IPs");
	udb_block_create('S', "Settings");
	udb_block_create('L', "Links");
	udb_block_create('K', "Lines");

	udb_ctx->nicks = udb_ctx->blocks['N']->tree;
	udb_ctx->channels = udb_ctx->blocks['C']->tree;
	udb_ctx->ips = udb_ctx->blocks['I']->tree;
	udb_ctx->settings = udb_ctx->blocks['S']->tree;
	udb_ctx->links = udb_ctx->blocks['L']->tree;
	udb_ctx->lines = udb_ctx->blocks['K']->tree;

	udb_blocks_load_all();
	return 1;
}

static void udb_engine_shutdown(void)
{
	if (!udb_ctx)
		return;

	udb_blocks_save_all();
	udb_ips_shutdown();

	UdbBlock *b = udb_ctx->block_list;
	while (b)
	{
		UdbBlock *next = b->next;
		udb_sync_session_free(b);
		udb_block_unload(b);
		safe_free(b->filepath);
		safe_free(b);
		b = next;
	}

	udb_hash_destroy();
	safe_free(udb_ctx->quit_ips);
	safe_free(udb_ctx->quit_clones);
	safe_free(udb_ctx->encryption_key);
	safe_free(udb_ctx->suffix);
	safe_free(udb_ctx->nickserv_mask);
	safe_free(udb_ctx->chanserv_mask);
	safe_free(udb_ctx->ipserv_mask);
	safe_free(udb_ctx);
	udb_ctx = NULL;
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */
static const char *udb_get_bot_nick(const char *service_key, int force_default)
{
	static char buf[64];
	const char *mask = udb_get_bot_mask(service_key, force_default);
	char *p;

	strlcpy(buf, mask, sizeof(buf));
	p = strchr(buf, '!');
	if (p)
		*p = '\0';
	return buf;
}

static const char *udb_get_bot_mask(const char *service_key, int force_default)
{
	if (!force_default && udb_ctx)
	{
		if (!strcasecmp(service_key, SKEY_NICKSERV) && udb_ctx->nickserv_mask)
			return udb_ctx->nickserv_mask;
		if (!strcasecmp(service_key, SKEY_CHANSERV) && udb_ctx->chanserv_mask)
			return udb_ctx->chanserv_mask;
		if (!strcasecmp(service_key, SKEY_IPSERV) && udb_ctx->ipserv_mask)
			return udb_ctx->ipserv_mask;
	}
	if (!strcasecmp(service_key, SKEY_NICKSERV))
		return "NickServ!*@*";
	if (!strcasecmp(service_key, SKEY_CHANSERV))
		return "ChanServ!*@*";
	if (!strcasecmp(service_key, SKEY_IPSERV))
		return "IpServ!*@*";
	return "UDB!*@*";
}

static int udb_apply_special_record(UdbBlock *block, UdbRecord *rec, int is_new)
{
	if (!rec)
		return 0;
	if (block->letter == 'N')
	{
		UdbRecord *nick_rec = rec->parent == block->tree ? rec : rec->parent;
		Client *client = find_user(nick_rec->key, NULL);
		if (client && MyUser(client))
		{
			udb_nick_apply(client, nick_rec, is_new);
		}
	} else if (block->letter == 'C')
	{
		UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
		Channel *channel = find_channel(chan_rec->key);
		if (channel)
		{
			udb_channel_apply_record(channel, chan_rec, rec->key, is_new);
		}
	} else if (block->letter == 'I')
	{
		UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
		udb_ip_apply_record(ip_rec->key, ip_rec, rec->key, is_new);
	} else if (block->letter == 'S')
	{
		if (!udb_settings_apply_record(rec))
			udb_log(ULOG_WARNING, "UDB_SETTING_INVALID", NULL,
			        "Ignoring invalid or unsupported S::$setting", log_data_string("setting", rec->key));
	} else if (block->letter == 'L')
	{
		udb_link_apply_record(rec);
	} else if (block->letter == 'K')
	{
		udb_line_apply_record(rec, is_new);
	}
	return 1;
}

static void udb_remove_special_record(UdbBlock *block, UdbRecord *rec)
{
	if (!rec)
		return;
	if (block->letter == 'N')
	{
		if (rec->parent != block->tree)
		{
			UdbRecord *nick_rec = rec->parent;
			Client *client = find_user(nick_rec->key, NULL);
			if (client && MyUser(client))
			{
				if (!strcmp(rec->key, NKEY_VHOST))
				{
					udb_nick_remove_vhost(client);
				} else if (!strcmp(rec->key, NKEY_OPER))
				{
					udb_nick_revoke_oper(client);
				} else if (!strcmp(rec->key, NKEY_SWHOIS))
				{
					swhois_delete(client, "udb", "*", &me, NULL);
				} else if (!strcmp(rec->key, NKEY_MODES))
				{
					long old_umodes = client->umodes & ALL_UMODES;
					UdbRecord *mode_rec = udb_record_find(NKEY_MODES, nick_rec);
					if (mode_rec && mode_rec->data_str)
						client->umodes &= ~(set_usermode(mode_rec->data_str) & ~UMODE_OPER);
					send_umode_out(client, 1, old_umodes);
				} else if (!strcmp(rec->key, NKEY_SNOMASKS))
				{
					set_snomask(client, NULL);
				} else if (!strcmp(rec->key, NKEY_SUSPENDED))
				{
					long old_umodes = client->umodes & ALL_UMODES;
					client->umodes &= ~set_usermode("S");
					send_umode_out(client, 1, old_umodes);
				} else if (!strcmp(rec->key, NKEY_PASS))
				{
					udb_nick_strip(client, nick_rec);
				}
			}
		} else
		{
			Client *client = find_user(rec->key, NULL);
			if (client && MyUser(client))
			{
				udb_nick_strip(client, rec);
			}
		}
	} else if (block->letter == 'C')
	{
		UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
		Channel *channel = find_channel(chan_rec->key);
		if (channel)
		{
			udb_channel_remove_record(channel, chan_rec, rec->key);
		}
	} else if (block->letter == 'I')
	{
		UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
		if (rec->parent == block->tree)
		{
			/* Deleting I::<key> must remove effects owned by every child. */
			UdbRecord *child;
			for (child = ip_rec->child; child; child = child->sibling)
				udb_ip_remove_record(ip_rec->key, ip_rec, child->key);
		} else
		{
			udb_ip_remove_record(ip_rec->key, ip_rec, rec->key);
		}
	} else if (block->letter == 'S')
	{
		udb_settings_remove_record(rec);
	} else if (block->letter == 'L')
	{
		udb_link_remove_record(rec);
	} else if (block->letter == 'K')
	{
		udb_line_remove_record(rec);
	}
}

static void udb_send_to_debugs(Client *source, const char *fmt, ...)
{
	const char *buf = "diagnostic detail redacted";

	(void)fmt;

	Client *client;
	list_for_each_entry(client, &client_list, client_node)
	{
		if (IsServer(client) && client != source)
		{
			if (udb_ctx && udb_ctx->links)
			{
				UdbRecord *srv_rec = udb_record_find(client->name, udb_ctx->links);
				if (srv_rec)
				{
					UdbRecord *opt_rec = udb_record_find(LKEY_OPTIONS, srv_rec);
					if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG))
					{
						sendto_one(client, NULL, ":%s NOTICE %s :[UDB Debug] %s", me.id, client->id, buf);
					}
				}
			}
		}
	}

	// Also send to local opers if our own server has the debug option
	if (udb_ctx && udb_ctx->links)
	{
		UdbRecord *me_rec = udb_record_find(me.name, udb_ctx->links);
		if (me_rec)
		{
			UdbRecord *opt_rec = udb_record_find(LKEY_OPTIONS, me_rec);
			if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG))
			{
				unreal_log(ULOG_INFO, "udb", "UDB_DEBUG_OPER", source,
				           "[UDB Debug] $msg", log_data_string("msg", buf));
			}
		}
	}

	unreal_log(ULOG_DEBUG, "udb", "UDB_DEBUG", source, "[UDB Debug] $msg", log_data_string("msg", buf));
}
