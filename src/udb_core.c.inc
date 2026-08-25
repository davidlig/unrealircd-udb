/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Database Core Engine & Record Manipulation
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

static int udb_boolean_record_valid(const char *value)
{
	if (!value)
		return 0;
	return !strcmp(value, "*1");
}

static int udb_channel_modes_record_valid(const char *value)
{
	char modebuf[512];
	char *modes;
	char *p, *param;
	int expected_params = 0;
	int actual_params = 0;
	int mode_letters = 0;
	int what = MODE_ADD;
	const char *c;

	if (!value || !*value || strlen(value) >= sizeof(modebuf))
		return 0;

	strlcpy(modebuf, value, sizeof(modebuf));
	modes = strtoken(&p, modebuf, " ");
	if (!modes || !*modes)
		return 0;

	for (c = modes; *c; c++)
	{
		Cmode *cm;

		if (*c == '+')
		{
			what = MODE_ADD;
			continue;
		}
		if (*c == '-')
		{
			what = MODE_DEL;
			continue;
		}

		cm = find_channel_mode_handler(*c);
		if (!cm)
			return 0;

		mode_letters++;
		if (cm->type == CMODE_MEMBER)
		{
			expected_params++;
		} else if (what == MODE_ADD && cm->paracount > 0)
		{
			expected_params++;
		} else if (what == MODE_DEL && cm->paracount > 0 && cm->unset_with_param)
		{
			expected_params++;
		}
	}

	if (mode_letters == 0)
		return 0;

	while ((param = strtoken(&p, NULL, " ")))
	{
		if (!*param)
			return 0;
		actual_params++;
	}

	return expected_params == actual_params;
}

static int udb_user_mode_letter_valid(char c)
{
	Umode *um;
	if (c == 'o') /* +o is strictly forbidden in N::modes */
		return 0;
	if (!isalpha((unsigned char)c))
		return 0;
	if (!usermodes)
		return 1;
	for (um = usermodes; um; um = um->next)
	{
		if (um->letter == c)
			return 1;
	}
	return 0;
}

static int udb_user_modes_record_valid(const char *value)
{
	const char *c;
	int letters = 0;

	if (!value || !*value || strlen(value) > 64)
		return 0;

	for (c = value; *c; c++)
	{
		if (*c == '+' || *c == '-')
			continue;
		if (!udb_user_mode_letter_valid(*c))
			return 0;
		letters++;
	}

	return letters > 0;
}

static int udb_snomasks_record_valid(const char *value)
{
	const char *c;
	int letters = 0;

	if (!value || !*value || strlen(value) > 64)
		return 0;

	for (c = value; *c; c++)
	{
		if (*c == '+' || *c == '-')
			continue;
		if (!isalpha((unsigned char)*c))
			return 0;
		letters++;
	}

	return letters > 0;
}

static int udb_oper_record_valid(const char *value)
{
	const char *c;
	if (!value || !*value || strlen(value) > 64)
		return 0;
	for (c = value; *c; c++)
	{
		if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-')
			return 0;
	}
	return 1;
}

static int udb_record_validate(UdbBlock *block, const char *path,
                               const char *value)
{
	const char *leaf;
	size_t i;

	if (!block || !path)
		return 1;
	leaf = strrchr(path, ':');
	leaf = leaf ? leaf + 1 : path;
	if (block->letter == 'N' || block->letter == 'C')
	{
		if (!strcasecmp(leaf, NKEY_CHALLENGE))
			return value && (!strcasecmp(value, "argon2id") ||
			                 !strcasecmp(value, "sha256") ||
			                 !strcasecmp(value, "crypt"));
		if (!strcasecmp(leaf, NKEY_PASS))
		{
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
	}
	if (block->letter == 'N')
	{
		if (!strcasecmp(leaf, NKEY_MODES))
			return udb_user_modes_record_valid(value);
		if (!strcasecmp(leaf, NKEY_SNOMASKS))
			return udb_snomasks_record_valid(value);
		if (!strcasecmp(leaf, NKEY_OPER))
			return udb_oper_record_valid(value);
	}
	if (block->letter == 'C')
	{
		if (!strcasecmp(leaf, CKEY_MODES))
			return udb_channel_modes_record_valid(value);
		if (!strcasecmp(leaf, CKEY_PERSISTENT) ||
		    !strcasecmp(leaf, CKEY_MLOCK) ||
		    !strcasecmp(leaf, CKEY_TOPICLOCK))
			return udb_boolean_record_valid(value);
	}
	return 1;
}

static void udb_block_replace_tree(UdbContext *ctx, UdbBlock *block,
                                   UdbRecord *tree, unsigned int record_count)
{
	UdbRecord *rec;
	struct stat st;

	if (ctx->total_records >= block->record_count)
		ctx->total_records -= block->record_count;
	else
		ctx->total_records = 0;
	udb_record_free_tree(block->tree);
	udb_hash_clear_block(ctx, udb_block_letter_to_index(block->letter));
	block->tree = tree;
	block->record_count = record_count;
	ctx->total_records += record_count;
	for (rec = tree->child; rec; rec = rec->sibling)
		udb_hash_insert_record(ctx, rec, udb_block_letter_to_index(block->letter), rec->key);
	udb_block_set_context_root(ctx, block);
	block->checksum = udb_compute_block_checksum(block);
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
}

static UdbRecord *udb_record_insert(UdbContext *ctx, UdbBlock *block, UdbRecord *parent, const char *key, const char *data_str, unsigned long data_num, int persist)
{
	if (persist)
	{
		UdbRecord *candidate_parent = NULL;
		UdbRecord *candidate;
		char value[32];

		if (!parent)
			parent = block->tree;
		candidate = udb_record_clone_tree(block->tree, parent, &candidate_parent);
		if (!data_str)
		{
			snprintf(value, sizeof(value), "*%lu", data_num);
			data_str = value;
		}
		if (!candidate_parent)
			candidate_parent = candidate;
		UdbRecord *rec = udb_record_find(NULL, key, candidate_parent);
		if (!rec)
		{
			rec = udb_record_create(candidate_parent);
			safe_strdup(rec->key, key);
			rec->is_dynamic_key = 1;
		}
		if (rec->data_str)
			safe_free(rec->data_str);
		if (*data_str == '*')
		{
			rec->data_num = strtoul(data_str + 1, NULL, 10);
			rec->data_str = NULL;
		} else
		{
			safe_strdup(rec->data_str, data_str);
			rec->data_num = 0;
		}
		if (!udb_file_write_snapshot(block, candidate, udb_record_count_tree(candidate)))
		{
			udb_record_free_tree(candidate);
			return NULL;
		}
		udb_block_replace_tree(ctx, block, candidate, udb_record_count_tree(candidate));
		udb_apply_special_record(ctx, block, rec, 1);
		return rec;
	}
	if (!parent)
		parent = block->tree;
	UdbRecord *rec = udb_record_find(ctx, key, parent);
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
			udb_hash_insert_record(ctx, rec, udb_block_letter_to_index(block->letter), key);
		}
		block->record_count++;
		ctx->total_records++;
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

	udb_apply_special_record(ctx, block, rec, 1);

	return rec;
}


static UdbRecord *udb_record_delete(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int persist)
{
	UdbRecord *line_rec = NULL;
	if (!rec)
		return NULL;
	if (persist)
	{
		UdbRecord *candidate_rec = NULL;
		UdbRecord *candidate_line = NULL;
		UdbRecord *candidate = udb_record_clone_tree(block->tree, rec, &candidate_rec);
		if (!candidate_rec)
		{
			udb_record_free_tree(candidate);
			return rec;
		}
		if (block->letter == 'K' && candidate_rec->parent && candidate_rec->parent->parent &&
		    candidate_rec->parent->parent != candidate)
			candidate_line = candidate_rec->parent;
		udb_record_delete_tree(candidate_rec);
		if (!udb_file_write_snapshot(block, candidate, udb_record_count_tree(candidate)))
		{
			udb_record_free_tree(candidate);
			return rec;
		}
		udb_remove_special_record(ctx, block, rec);
		udb_block_replace_tree(ctx, block, candidate, udb_record_count_tree(candidate));
		if (candidate_line)
			udb_lines_apply_effect(ctx, block, candidate_line, 0);
		return NULL;
	}

	/* A K property deletion changes its owning pattern, not a line of its own. */
	if (block->letter == 'K' && rec->parent && rec->parent->parent &&
	    rec->parent->parent != block->tree)
		line_rec = rec->parent;

	udb_remove_special_record(ctx, block, rec);

	if (rec->parent)
	{
		if (rec->parent->parent == NULL)
		{
			udb_hash_remove_record(ctx, rec, udb_block_letter_to_index(block->letter), rec->key);
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
	if (ctx->total_records > 0)
		ctx->total_records--;

	udb_record_free_tree(rec);

	/* Rebuild a surviving K pattern after one of its properties was removed. */
	if (line_rec)
		udb_lines_apply_effect(ctx, block, line_rec, 0);

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
	if (!udb_record_validate(block, line, value))
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

static void udb_sync_session_free(UdbBlock *block)
{
	if (!block || !block->session)
		return;
	udb_record_free_tree(block->session->tree);
	safe_free(block->session);
	block->session = NULL;
	block->syncing_from = NULL;
}

static int udb_block_commit_stage(UdbContext *ctx, UdbBlock *block, UdbSyncSession *session,
                                  unsigned long checksum)
{
	UdbRecord *rec;
	struct stat st;

	if (!block || !session || block->session != session)
		return 0;
	/* Persistence succeeded before this point; only now may active state move. */
	udb_block_reset(ctx, block);
	udb_record_free_tree(block->tree);
	block->tree = session->tree;
	session->tree = NULL;
	block->record_count = session->record_count;
	ctx->total_records += block->record_count;
	for (rec = block->tree->child; rec; rec = rec->sibling)
		udb_hash_insert_record(ctx, rec, udb_block_letter_to_index(block->letter), rec->key);
	udb_block_set_context_root(ctx, block);
	block->checksum = checksum;
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
	udb_sync_session_free(block);

	udb_apply_tree_effects(ctx, block);
	if (block->letter == 'L')
		udb_sync_snomask_filter();
	return 1;
}

/* ========================================================================
 * File I/O Operations
 * ======================================================================== */
static int udb_record_path_valid(const char *path)
{
	const char *component;
	const char *p;

	if (!path || !*path || strlen(path) >= UDB_RECORD_PATH_MAX)
		return 0;
	component = path;
	for (p = path; *p; p++)
	{
		if (*p == ':')
		{
			if (p == component || p[1] != ':' || !p[2])
				return 0;
			p++;
			component = p + 1;
		}
	}
	return 1;
}

static UdbRecord *udb_file_parse_line(UdbContext *ctx, UdbBlock *block, char *line)
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
	/* Validate before lookup/allocation: an empty component would leave rec->key NULL. */
	if (!udb_record_path_valid(line))
		return NULL;
	if (!udb_record_validate(block, line, value))
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

		UdbRecord *rec = udb_record_find(ctx, p, parent);
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
				udb_hash_insert_record(ctx, rec, udb_block_letter_to_index(block->letter), p);
			}
			block->record_count++;
			ctx->total_records++;
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


static int udb_file_load_block(UdbContext *ctx, UdbBlock *block)
{
	if (!block || !block->filepath)
		return 0;
	FILE *fp = fopen(block->filepath, "r");
	if (!fp)
		return 0;

	char line[4096];
	unsigned int line_number = 0;
	while (fgets(line, sizeof(line), fp))
	{
		int overlong = !strchr(line, '\n') && !feof(fp);
		line_number++;
		if (overlong)
		{
			int ch;
			char logbuf[512];
			while ((ch = fgetc(fp)) != '\n' && ch != EOF)
				;
			snprintf(logbuf, sizeof(logbuf),
			         "Skipping overlong persisted record in block %c at line %u",
			         block->letter, line_number);
			udb_log(ULOG_WARNING, "UDB_FILE_LINE_REJECTED", NULL,
			        "$msg", log_data_string("msg", logbuf));
			continue;
		}
		char *p = strchr(line, '\n');
		if (p)
			*p = '\0';
		p = strchr(line, '\r');
		if (p)
			*p = '\0';

		if (line[0] == ';' || line[0] == '\0')
			continue;

		if (!udb_file_parse_line(ctx, block, line))
		{
			char logbuf[512];
			snprintf(logbuf, sizeof(logbuf),
			         "Skipping malformed persisted record in block %c at line %u",
			         block->letter, line_number);
			udb_log(ULOG_WARNING, "UDB_FILE_LINE_REJECTED", NULL,
			        "$msg", log_data_string("msg", logbuf));
		}
	}
	fclose(fp);

	UdbRecord *curr = block->tree->child;
	while (curr)
	{
		UdbRecord *sub = curr->child;
		while (sub)
		{
			udb_apply_special_record(ctx, block, sub, 1);
			sub = sub->sibling;
		}
		udb_apply_special_record(ctx, block, curr, 1);
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
	snprintf(logbuf, sizeof(logbuf), "Loaded block %c from %s (%u records)", block->letter, block->filepath, block->record_count);
	udb_log(ULOG_INFO, "UDB_FILE_LOADED", NULL, "$msg", log_data_string("msg", logbuf));

	if (block->letter == 'L')
		udb_sync_snomask_filter();

	return 1;
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

static void udb_send_to_debugs(Client *source, const char *fmt, ...)
{
	char buf[1024];
	va_list va;

	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);

	Client *client;
	list_for_each_entry(client, &client_list, client_node)
	{
		if (IsServer(client) && client != source)
		{
			if (udb_ctx && udb_ctx->links)
			{
				UdbRecord *srv_rec = udb_record_find(udb_ctx, client->name, udb_ctx->links);
				if (srv_rec)
				{
					UdbRecord *opt_rec = udb_record_find(udb_ctx, LKEY_OPTIONS, srv_rec);
					if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG))
					{
						sendto_one(client, NULL, ":%s NOTICE %s :[UDB Debug] %s", me.id, client->id, buf);
					}
				}
			}
		}
	}

	if (udb_is_debug_enabled())
	{
		udb_log(ULOG_INFO, "UDB_DEBUG", source, "$msg", log_data_string("msg", buf));
	} else
	{
		unreal_log(ULOG_DEBUG, "udb", "UDB_DEBUG", source, "[UDB Debug] $msg", log_data_string("msg", buf));
	}
}
