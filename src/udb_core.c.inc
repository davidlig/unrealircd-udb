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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int udb_strtoull_strict(const char *s, unsigned long long *out)
{
	char *end;
	unsigned long long val;

	if (!s || !*s)
		return 0;
	for (const char *p = s; *p; p++)
	{
		if (!isdigit((unsigned char)*p))
			return 0;
	}
	errno = 0;
	val = strtoull(s, &end, 10);
	if (errno == ERANGE || *end != '\0')
		return 0;
	if (out)
		*out = val;
	return 1;
}

static int udb_strtoul_strict(const char *s, unsigned long *out)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val) || val > ULONG_MAX)
		return 0;
	if (out)
		*out = (unsigned long)val;
	return 1;
}

static int udb_parse_uint_strict(const char *s, unsigned int *out, unsigned int min_val, unsigned int max_val)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val) || val < min_val || val > max_val || val > UINT_MAX)
		return 0;
	if (out)
		*out = (unsigned int)val;
	return 1;
}

static int udb_parse_ulong_strict(const char *s, unsigned long *out, unsigned long min_val, unsigned long max_val)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val) || val < min_val || val > max_val || val > ULONG_MAX)
		return 0;
	if (out)
		*out = (unsigned long)val;
	return 1;
}

static int udb_parse_size_strict(const char *s, size_t *out, size_t min_val, size_t max_val)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val) || val < min_val || val > max_val || val > SIZE_MAX)
		return 0;
	if (out)
		*out = (size_t)val;
	return 1;
}

static unsigned long long udb_time_t_max_val(void)
{
	if (((time_t)-1) < 0)
	{
		if (sizeof(time_t) <= 4)
			return (unsigned long long)0x7FFFFFFFUL;
		else
			return (unsigned long long)0x7FFFFFFFFFFFFFFFLL;
	}
	else
	{
		if (sizeof(time_t) <= 4)
			return (unsigned long long)0xFFFFFFFFUL;
		else
			return (unsigned long long)0xFFFFFFFFFFFFFFFFULL;
	}
}

static int udb_parse_time_t(const char *s, time_t *out)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val))
		return 0;
	if (val > udb_time_t_max_val())
		return 0;
	if (out)
		*out = (time_t)val;
	return 1;
}

static int udb_time_add(time_t base, unsigned long duration, time_t *result)
{
	if (base < 0)
		return 0;
	unsigned long long max_t = udb_time_t_max_val();
	unsigned long long base_ull = (unsigned long long)base;
	unsigned long long dur_ull = (unsigned long long)duration;

	if (base_ull > max_t)
		return 0;
	if (dur_ull > max_t - base_ull)
		return 0;

	unsigned long long sum = base_ull + dur_ull;
	if (sum > max_t)
		return 0;
	if (result)
		*result = (time_t)sum;
	return 1;
}

static int udb_timestamp_parse(const char *s, time_t *out)
{
	return udb_parse_time_t(s, out);
}

static int udb_checksum_parse(const char *input, unsigned long *checksum)
{
	char *end;
	const char *p;

	if (!input || !*input || strlen(input) > 8)
		return 0;
	for (p = input; *p; p++)
		if (!isxdigit((unsigned char)*p))
			return 0;
	errno = 0;
	unsigned long val = strtoul(input, &end, 16);
	if (errno == ERANGE || *end != '\0')
		return 0;
	if (checksum)
		*checksum = val;
	return 1;
}

static int udb_numeric_record_valid(const char *value)
{
	unsigned long val;

	if (!value || value[0] != '*' || value[1] == '\0')
		return 0;
	return udb_strtoul_strict(value + 1, &val);
}

static int udb_options_record_valid(const char *value)
{
	return udb_numeric_record_valid(value);
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
		}
		else if (what == MODE_ADD && cm->paracount > 0)
		{
			expected_params++;
		}
		else if (what == MODE_DEL && cm->paracount > 0 && cm->unset_with_param)
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

static int udb_password_hash_valid(const char *value)
{
	size_t i;
	if (!value || !*value)
		return 0;
	if (!strncmp(value, "argon2id:$argon2id$", 19))
		return 1;
	if (!strncmp(value, "crypt:", 6))
		return value[6] != '\0';
	if (strncmp(value, "sha256:", 7) || strlen(value + 7) != 64)
		return 0;
	for (i = 7; value[i]; i++)
	{
		if (!isxdigit((unsigned char)value[i]))
			return 0;
	}
	return 1;
}

static int udb_challenge_valid(const char *value)
{
	return value && (!strcasecmp(value, "argon2id") || !strcasecmp(value, "sha256") || !strcasecmp(value, "crypt"));
}

static int udb_vhost_valid(const char *value)
{
	return value && *value && strlen(value) <= HOSTLEN && !strpbrk(value, " \t\r\n");
}

static int udb_nolines_record_valid(const char *value)
{
	const char *c;
	if (!value || !*value || strlen(value) > 16)
		return 0;
	for (c = value; *c; c++)
	{
		if (!strchr("GZQSTmc", *c))
			return 0;
	}
	return 1;
}

static int udb_non_empty_string_valid(const char *value)
{
	return value && *value && strlen(value) <= UDB_RECORD_VALUE_MAX && !strpbrk(value, "\r\n");
}

static int udb_nick_name_valid(const char *name)
{
	const char *c;
	if (!name || !*name || strlen(name) > NICKLEN)
		return 0;
	if (isdigit((unsigned char)name[0]) || name[0] == '-')
		return 0;
	for (c = name; *c; c++)
	{
		if (!isalnum((unsigned char)*c) && !strchr("[]\\`_^{|}-", *c))
			return 0;
	}
	return 1;
}

static int udb_channel_name_valid(const char *name)
{
	const char *c;
	if (!name || strlen(name) < 2 || strlen(name) > CHANNELLEN)
		return 0;
	if (name[0] != '#' && name[0] != '&')
		return 0;
	for (c = name; *c; c++)
	{
		if (*c <= ' ' || *c == ',' || *c == ':' || *c == 7)
			return 0;
	}
	return 1;
}

static int udb_ip_host_mask_valid(const char *mask)
{
	const char *c;
	if (!mask || !*mask || strlen(mask) > HOSTLEN)
		return 0;
	for (c = mask; *c; c++)
	{
		if (*c <= ' ' || (unsigned char)*c > 126)
			return 0;
	}
	return 1;
}

static int udb_server_name_valid(const char *srv)
{
	return srv && *srv && strlen(srv) <= HOSTLEN && !strpbrk(srv, " \t\r\n") && valid_server_name(srv);
}

/* Iterate a comma-separated propagator list without copying or truncating it.
 * Spaces are accepted only around commas; tabs and line breaks are rejected. */
static int udb_propagator_list_next(const char **cursor, char name[HOSTLEN + 1])
{
	const char *start, *end, *p;
	size_t len;

	if (!cursor || !(p = *cursor) || !*p)
		return 0;
	while (*p == ' ')
		p++;
	if (!*p || *p == ',')
		return -1;
	start = p;
	while (*p && *p != ',')
		p++;
	end = p;
	while (end > start && end[-1] == ' ')
		end--;
	len = (size_t)(end - start);
	if (!len || len > HOSTLEN)
		return -1;
	memcpy(name, start, len);
	name[len] = '\0';
	if (!udb_server_name_valid(name))
		return -1;
	if (*p == ',')
	{
		p++;
		if (!*p)
			return -1;
	}
	*cursor = p;
	return 1;
}

static int udb_propagator_list_valid(const char *value)
{
	const char *cursor = value;
	char name[HOSTLEN + 1];
	int result, count = 0;

	if (!value || !*value || strlen(value) > UDB_RECORD_VALUE_MAX || strpbrk(value, "\r\n\t"))
		return 0;
	while ((result = udb_propagator_list_next(&cursor, name)) > 0)
		count++;
	return result == 0 && count > 0;
}

static int udb_flood_setting_valid(const char *value)
{
	int attempts = 0, period = 0;
	return udb_flood_valid(value, &attempts, &period);
}

static int udb_tkl_type_valid(const char *type)
{
	return type && strlen(type) == 1 && strchr("GZSQF", type[0]);
}

static int udb_spamfilter_type_valid(const char *value)
{
	return value && *value && spamfilter_getconftargets(value) > 0;
}

static int udb_spamfilter_action_valid(const char *value)
{
	return value && *value && banact_stringtoval(value) > 0;
}

static const UdbKeyDescriptor udb_schema_n_subkeys[] = {
	{NKEY_ACCESS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_PASS, UDB_VAL_STRING, udb_password_hash_valid, 0, NULL},
	{NKEY_VHOST, UDB_VAL_STRING, udb_vhost_valid, 0, NULL},
	{NKEY_FORBID, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_SUSPENDED, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_OPER, UDB_VAL_STRING, udb_oper_record_valid, 0, NULL},
	{NKEY_CHALLENGE, UDB_VAL_STRING, udb_challenge_valid, 0, NULL},
	{NKEY_MODES, UDB_VAL_STRING, udb_user_modes_record_valid, 0, NULL},
	{NKEY_SNOMASKS, UDB_VAL_STRING, udb_snomasks_record_valid, 0, NULL},
	{NKEY_SWHOIS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_c_subkeys[] = {
	{CKEY_FOUNDER, UDB_VAL_STRING, udb_nick_name_valid, 0, NULL},
	{CKEY_MODES, UDB_VAL_STRING, udb_channel_modes_record_valid, 0, NULL},
	{CKEY_TOPIC, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_ACCESS, UDB_VAL_ANY, NULL, 1, udb_nick_name_valid},
	{CKEY_FORBID, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_SUSPENDED, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_PASS, UDB_VAL_STRING, udb_password_hash_valid, 0, NULL},
	{CKEY_CHALLENGE, UDB_VAL_STRING, udb_challenge_valid, 0, NULL},
	{CKEY_OPTIONS, UDB_VAL_NUMERIC, udb_options_record_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_i_subkeys[] = {
	{IKEY_CLONES, UDB_VAL_NUMERIC, udb_numeric_record_valid, 0, NULL},
	{IKEY_NOLINES, UDB_VAL_STRING, udb_nolines_record_valid, 0, NULL},
	{IKEY_HOST, UDB_VAL_STRING, udb_vhost_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_s_subkeys[] = {
	{SKEY_CLONES, UDB_VAL_NUMERIC, udb_numeric_record_valid, 0, NULL},
	{SKEY_QUIT_IPS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{SKEY_QUIT_CLONES, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{SKEY_FLOOD, UDB_VAL_STRING, udb_flood_setting_valid, 0, NULL},
	{SKEY_CRYPT_KEY, UDB_VAL_STRING, udb_encryption_key_valid, 0, NULL},
	{SKEY_SUFFIX, UDB_VAL_STRING, udb_suffix_valid, 0, NULL},
	{SKEY_NICKSERV, UDB_VAL_STRING, udb_service_mask_valid, 0, NULL},
	{SKEY_CHANSERV, UDB_VAL_STRING, udb_service_mask_valid, 0, NULL},
	{SKEY_IPSERV, UDB_VAL_STRING, udb_service_mask_valid, 0, NULL},
	{SKEY_PROPAGATOR, UDB_VAL_STRING, udb_propagator_list_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_l_subkeys[] = {
	{LKEY_OPTIONS, UDB_VAL_NUMERIC, udb_options_record_valid, 0, NULL}};

static const UdbBlockSchema udb_block_schemas[] = {{UDB_BLOCK_NICKS, 1, 2, udb_nick_name_valid, udb_schema_n_subkeys,
													sizeof(udb_schema_n_subkeys) / sizeof(udb_schema_n_subkeys[0])},
												   {UDB_BLOCK_CHANNELS, 1, 3, udb_channel_name_valid,
													udb_schema_c_subkeys,
													sizeof(udb_schema_c_subkeys) / sizeof(udb_schema_c_subkeys[0])},
												   {UDB_BLOCK_IPS, 1, 2, udb_ip_host_mask_valid, udb_schema_i_subkeys,
													sizeof(udb_schema_i_subkeys) / sizeof(udb_schema_i_subkeys[0])},
												   {UDB_BLOCK_SETTINGS, 1, 1, NULL, udb_schema_s_subkeys,
													sizeof(udb_schema_s_subkeys) / sizeof(udb_schema_s_subkeys[0])},
												   {UDB_BLOCK_LINKS, 1, 2, udb_server_name_valid, udb_schema_l_subkeys,
													sizeof(udb_schema_l_subkeys) / sizeof(udb_schema_l_subkeys[0])},
												   {UDB_BLOCK_LINES, 2, 3, udb_tkl_type_valid, NULL, 0}};

static const UdbBlockSchema *udb_get_block_schema(char letter)
{
	for (size_t i = 0; i < sizeof(udb_block_schemas) / sizeof(udb_block_schemas[0]); i++)
	{
		if (udb_block_schemas[i].letter == letter)
			return &udb_block_schemas[i];
	}
	return NULL;
}

static int udb_record_validate(UdbBlock *block, const char *path, const char *value)
{
	char pathbuf[UDB_RECORD_PATH_MAX + 1];
	char *decoded_parts = NULL;
	char *parts[8];
	int depth = 0;
	char *p, *next;
	const UdbBlockSchema *schema;
	const UdbKeyDescriptor *desc = NULL;
	int result = 0;

	if (!block || !path || !*path || strlen(path) > UDB_RECORD_PATH_MAX)
		return 0;

	if (!udb_record_fits_limits(path, value))
		return 0;

	schema = udb_get_block_schema(block->letter);
	if (!schema)
		return 0;

	decoded_parts = safe_alloc(8 * (UDB_COMPONENT_RAW_MAX + 1));
	for (int i = 0; i < 8; i++)
		parts[i] = decoded_parts + (i * (UDB_COMPONENT_RAW_MAX + 1));

	strlcpy(pathbuf, path, sizeof(pathbuf));
	p = pathbuf;
	while (p && *p)
	{
		next = strstr(p, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
			if (!*next)
				goto done;
		}
		if (depth >= 8)
			goto done;
		if (!udb_path_decode_component(p, parts[depth], UDB_COMPONENT_RAW_MAX + 1))
			goto done;
		depth++;
		p = next;
	}

	if (depth < schema->min_depth || depth > schema->max_depth)
		goto done;

	/* Special handling for Block S: depth == 1 */
	if (block->letter == UDB_BLOCK_SETTINGS)
	{
		const char *key = parts[0];
		for (size_t i = 0; i < schema->subkey_count; i++)
		{
			if (!strcasecmp(schema->subkeys[i].key, key))
			{
				desc = &schema->subkeys[i];
				break;
			}
		}
		if (!desc)
			goto done;

		if (desc->val_type == UDB_VAL_NUMERIC)
		{
			if (!udb_numeric_record_valid(value))
				goto done;
		}
		else if (desc->val_type == UDB_VAL_STRING)
		{
			if (!value || value[0] == '*' || !*value)
				goto done;
		}
		if (desc->validator && !desc->validator(value))
			goto done;
		result = 1;
		goto done;
	}

	/* Special handling for Block K (TKL / Spamfilter) */
	if (block->letter == UDB_BLOCK_LINES)
	{
		const char *type = parts[0];
		if (!udb_tkl_type_valid(type))
			goto done;

		if (type[0] == 'F') /* Spamfilter */
		{
			if (depth != 3)
				goto done;
			const char *pattern = parts[1];
			const char *subkey = parts[2];
			char pat_buf[UDB_SPAMFILTER_PATTERN_MAX + 1];
			if (!pattern || !*pattern || !udb_spamfilter_pattern(pattern, pat_buf, sizeof(pat_buf)))
				goto done;
			if (!strcasecmp(subkey, KKEY_TYPE))
			{
				result = udb_spamfilter_type_valid(value);
				goto done;
			}
			if (!strcasecmp(subkey, KKEY_ACTION))
			{
				result = udb_spamfilter_action_valid(value);
				goto done;
			}
			if (!strcasecmp(subkey, KKEY_DURATION))
			{
				result = udb_numeric_record_valid(value);
				goto done;
			}
			if (!strcasecmp(subkey, KKEY_REASON))
			{
				result = udb_non_empty_string_valid(value) && value[0] != '*';
				goto done;
			}
			goto done;
		}
		else /* G, Z, S, Q */
		{
			const char *pattern = parts[1];
			if (!pattern || !*pattern)
				goto done;
			if (depth == 2)
			{
				result = udb_non_empty_string_valid(value) && value[0] != '*';
				goto done;
			}
			else if (depth == 3)
			{
				const char *subkey = parts[2];
				if (!strcasecmp(subkey, KKEY_REASON))
				{
					result = udb_non_empty_string_valid(value) && value[0] != '*';
					goto done;
				}
				if (!strcasecmp(subkey, KKEY_DURATION))
				{
					result = udb_numeric_record_valid(value);
					goto done;
				}
				goto done;
			}
			goto done;
		}
	}

	/* Validate root container node */
	if (schema->root_key_validator && !schema->root_key_validator(parts[0]))
		goto done;

	/* Depth 1 is a valid root profile/container node for N, C, I, L */
	if (depth == 1)
	{
		result = 1;
		goto done;
	}

	const char *subkey = parts[1];
	for (size_t i = 0; i < schema->subkey_count; i++)
	{
		if (!strcasecmp(schema->subkeys[i].key, subkey))
		{
			desc = &schema->subkeys[i];
			break;
		}
	}
	if (!desc)
		goto done;

	if (depth == 2)
	{
		if (desc->val_type == UDB_VAL_NUMERIC)
		{
			if (!udb_numeric_record_valid(value))
				goto done;
		}
		else if (desc->val_type == UDB_VAL_STRING)
		{
			if (!value || value[0] == '*' || !*value)
				goto done;
		}
		else if (desc->val_type == UDB_VAL_NONE)
		{
			if (value && *value)
				goto done;
		}
		if (desc->validator && !desc->validator(value))
			goto done;
		result = 1;
		goto done;
	}
	else if (depth == 3)
	{
		if (!desc->allow_children)
			goto done;
		if (desc->child_validator && !desc->child_validator(parts[2]))
			goto done;
		if (value && *value)
		{
			if (value[0] == '*')
			{
				if (!udb_numeric_record_valid(value))
					goto done;
			}
			else
			{
				if (!udb_non_empty_string_valid(value))
					goto done;
			}
		}
		result = 1;
		goto done;
	}

done:
	if (decoded_parts)
		safe_free(decoded_parts);
	return result;
}

static void udb_block_replace_tree(UdbContext *ctx, UdbBlock *block, UdbRecord *tree, unsigned int record_count)
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
	if (!udb_compute_block_checksum(block, &block->checksum))
	{
		block->checksum = 0;
		udb_log(ULOG_ERROR, "UDB_CHECKSUM_CALC_FAILED", NULL,
				"Failed to compute checksum for block $block after commit",
				log_data_string("block", (char[]){block->letter, '\0'}));
	}
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
}

static UdbRecord *udb_record_insert(UdbContext *ctx, UdbBlock *block, UdbRecord *parent, const char *key,
									const char *data_str, unsigned long data_num, int persist)
{
	if (persist)
	{
		UdbRecord *candidate_parent = NULL;
		UdbRecord *candidate;
		char value[32];

		if (!parent)
			parent = block->tree;
		if (!data_str)
		{
			snprintf(value, sizeof(value), "*%lu", data_num);
			data_str = value;
		}
		if (!udb_record_fits_limits(key, data_str))
			return NULL;
		candidate = udb_record_clone_tree(block->tree, parent, &candidate_parent);
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
			if (!udb_strtoul_strict(data_str + 1, &rec->data_num))
			{
				udb_record_free_tree(candidate);
				return NULL;
			}
			rec->data_str = NULL;
		}
		else
		{
			safe_strdup(rec->data_str, data_str);
			rec->data_num = 0;
		}
		UdbSnapshotResult res = udb_file_write_snapshot(block, candidate, udb_record_count_tree(candidate));
		if (res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
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
			}
			else
			{
				const char *shared = udb_get_shared_subkey(key);
				if (shared)
				{
					rec->key = (char *)shared;
					rec->is_dynamic_key = 0;
				}
				else
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
		udb_strtoul_strict(data_str + 1, &rec->data_num);
		rec->data_str = NULL;
	}
	else if (data_str)
	{
		safe_strdup(rec->data_str, data_str);
		rec->data_num = 0;
	}
	else
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
		UdbSnapshotResult res = udb_file_write_snapshot(block, candidate, udb_record_count_tree(candidate));
		if (res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
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
	if (block->letter == 'K' && rec->parent && rec->parent->parent && rec->parent->parent != block->tree)
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

typedef struct UdbDigestLine
{
	char *line;
	struct UdbDigestLine *next;
} UdbDigestLine;

static void udb_digest_lines_free(UdbDigestLine *lines)
{
	while (lines)
	{
		UdbDigestLine *next = lines->next;
		if (lines->line)
			safe_free(lines->line);
		safe_free(lines);
		lines = next;
	}
}

static int udb_digest_collect(UdbRecord *rec, int depth, char *pathbuf, size_t pathlen, UdbDigestLine **lines)
{
	size_t orig_len;
	UdbRecord *child;
	char encoded_key[UDB_COMPONENT_ENCODED_MAX + 1];

	if (!rec || !rec->key)
		return 1;
	orig_len = strlen(pathbuf);
	if (!udb_path_encode_component(rec->key, encoded_key, sizeof(encoded_key)))
		return 0;
	size_t enc_len = strlen(encoded_key);
	if (depth > 0)
	{
		if (orig_len + 2 + enc_len >= pathlen)
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
			pathbuf[orig_len] = '\0';
			return 0;
		}

		UdbDigestLine *line = safe_alloc(sizeof(*line));
		size_t len = strlen(pathbuf) + 1 + strlen(val_str) + 1;
		line->line = safe_alloc(len);
		int n = snprintf(line->line, len, "%s %s", pathbuf, val_str);
		if (n < 0 || (size_t)n >= len)
		{
			safe_free(line->line);
			safe_free(line);
			pathbuf[orig_len] = '\0';
			return 0;
		}
		line->next = *lines;
		*lines = line;
	}
	for (child = rec->child; child; child = child->sibling)
	{
		if (!udb_digest_collect(child, depth + 1, pathbuf, pathlen, lines))
		{
			pathbuf[orig_len] = '\0';
			return 0;
		}
	}
	pathbuf[orig_len] = '\0';
	return 1;
}

static int udb_digest_line_cmp(const void *a, const void *b)
{
	const UdbDigestLine *const *left = a;
	const UdbDigestLine *const *right = b;
	return strcmp((*left)->line, (*right)->line);
}

/* The digest covers sorted logical records, never save-time file headers/order. */
static int udb_compute_tree_checksum(UdbRecord *tree, unsigned long *checksum)
{
	UdbDigestLine *lines = NULL;
	UdbDigestLine *line;
	UdbDigestLine **sorted;
	unsigned long crc = 0xFFFFFFFFUL;
	unsigned int count = 0;
	unsigned int i = 0;
	char pathbuf[UDB_RECORD_PATH_MAX + 1] = "";

	if (checksum)
		*checksum = 0;
	if (!tree)
		return 1;

	for (UdbRecord *rec = tree->child; rec; rec = rec->sibling)
	{
		if (!udb_digest_collect(rec, 0, pathbuf, sizeof(pathbuf), &lines))
		{
			udb_digest_lines_free(lines);
			return 0;
		}
	}
	for (line = lines; line; line = line->next)
		count++;
	if (count == 0)
	{
		if (checksum)
			*checksum = 0;
		return 1;
	}
	sorted = safe_alloc(sizeof(*sorted) * count);
	for (line = lines; line; line = line->next)
		sorted[i++] = line;
	qsort(sorted, count, sizeof(*sorted), udb_digest_line_cmp);
	for (i = 0; i < count; i++)
	{
		crc = udb_crc32_step(crc, sorted[i]->line, strlen(sorted[i]->line));
		crc = udb_crc32_step(crc, "\n", 1);
		safe_free(sorted[i]->line);
		safe_free(sorted[i]);
	}
	safe_free(sorted);
	if (checksum)
		*checksum = crc ^ 0xFFFFFFFFUL;
	return 1;
}

static int udb_compute_block_checksum(UdbBlock *block, unsigned long *checksum)
{
	if (!block || !block->tree)
	{
		if (checksum)
			*checksum = 0;
		return 1;
	}
	return udb_compute_tree_checksum(block->tree, checksum);
}

static UdbRecord *udb_stage_find(UdbRecord *parent, const char *key)
{
	UdbRecord *rec;
	for (rec = parent ? parent->child : NULL; rec; rec = rec->sibling)
		if (!strcasecmp(rec->key, key))
			return rec;
	return NULL;
}

static UdbRecord *udb_stage_insert(UdbRecord *parent, const char *key, UdbSyncSession *session)
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
	char *line;
	char decoded_part[UDB_COMPONENT_RAW_MAX + 1];
	char *value, *part, *next;
	UdbRecord *parent;
	int result = 0;

	if (!block || !session || !session->tree || !input || !*input || strlen(input) > UDB_RECORD_LINE_MAX)
		return 0;
	line = safe_alloc(UDB_RECORD_LINE_MAX + 2);
	strlcpy(line, input, UDB_RECORD_LINE_MAX + 2);
	value = strchr(line, ' ');
	if (value)
		*value++ = '\0';
	if (!*line)
		goto done;
	if (!udb_record_fits_limits(line, value))
		goto done;
	if (!udb_record_validate(block, line, value))
		goto done;
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
				goto done;
		}
		if (!*part)
			goto done;
		if (!udb_path_decode_component(part, decoded_part, sizeof(decoded_part)))
			goto done;
		parent = udb_stage_insert(parent, decoded_part, session);
		part = next;
	}
	if (value && *value == '*')
	{
		if (!udb_strtoul_strict(value + 1, &parent->data_num))
			goto done;
		safe_free(parent->data_str);
	}
	else if (value)
	{
		safe_free(parent->data_str);
		safe_strdup(parent->data_str, value);
		parent->data_num = 0;
	}
	else
	{
		goto done;
	}
	result = 1;

done:
	safe_free(line);
	return result;
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

static int udb_block_commit_stage(UdbContext *ctx, UdbBlock *block, UdbSyncSession *session, unsigned long checksum)
{
	UdbRecord *rec;
	struct stat st;

	if (!block || !session || block->session != session)
		return 0;
	/* Persistence succeeded before this point; only now may active state move. */
	unsigned int real_count = udb_record_count_tree(session->tree);
	udb_block_reset(ctx, block);
	udb_record_free_tree(block->tree);
	block->tree = session->tree;
	session->tree = NULL;
	block->record_count = real_count;
	ctx->total_records += block->record_count;
	for (rec = block->tree->child; rec; rec = rec->sibling)
		udb_hash_insert_record(ctx, rec, udb_block_letter_to_index(block->letter), rec->key);
	udb_block_set_context_root(ctx, block);
	block->checksum = checksum;
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
	udb_sync_session_free(block);

	udb_last_successful_sync = time(NULL);
	udb_apply_tree_effects(ctx, block);
	if (block->letter == 'L')
		udb_sync_snomask_filter();
	udb_sync_status_refresh();
	return 1;
}

/* ========================================================================
 * File I/O Operations
 * ======================================================================== */
static int udb_record_path_valid(const char *path)
{
	const char *component;
	const char *p;

	if (!path || !*path || strlen(path) > UDB_RECORD_PATH_MAX || !udb_record_fits_limits(path, NULL))
		return 0;
	component = path;
	for (p = path; *p; p++)
	{
		if (*p == ':')
		{
			if (p == component || p[1] != ':' || !p[2])
				return 0;
			char decoded[UDB_COMPONENT_RAW_MAX + 1];
			char comp_buf[UDB_COMPONENT_ENCODED_MAX + 1];
			size_t clen = p - component;
			if (clen >= sizeof(comp_buf))
				return 0;
			memcpy(comp_buf, component, clen);
			comp_buf[clen] = '\0';
			if (!udb_path_decode_component(comp_buf, decoded, sizeof(decoded)))
				return 0;
			p++;
			component = p + 1;
		}
	}
	if (!*component)
		return 0;
	char decoded[UDB_COMPONENT_RAW_MAX + 1];
	if (!udb_path_decode_component(component, decoded, sizeof(decoded)))
		return 0;
	return 1;
}

static UdbRecord *udb_file_parse_line_to_tree(UdbBlock *block, UdbRecord *tree, char *line)
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
			if (!udb_strtoul_strict(value + 1, &data_num))
				return NULL;
		}
		else
		{
			data_str = value;
		}
	}
	const char *val_for_validation = value ? (*value == '*' ? value : data_str) : NULL;
	if (!udb_record_fits_limits(line, val_for_validation))
		return NULL;
	if (!udb_record_path_valid(line))
		return NULL;
	if (!udb_record_validate(block, line, val_for_validation))
		return NULL;

	char *p = line;
	UdbRecord *parent = tree;
	UdbRecord *leaf_rec = NULL;
	while (p && *p)
	{
		char decoded_part[UDB_COMPONENT_RAW_MAX + 1];
		char *next = strstr(p, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
			if (!*next)
				return NULL;
		}
		if (!*p)
			return NULL;
		if (!udb_path_decode_component(p, decoded_part, sizeof(decoded_part)))
			return NULL;

		UdbRecord *rec = udb_record_find(NULL, decoded_part, parent);
		if (!rec)
		{
			rec = udb_record_create(parent);
			if (parent == tree)
			{
				safe_strdup(rec->key, decoded_part);
				rec->is_dynamic_key = 1;
			}
			else
			{
				const char *shared = udb_get_shared_subkey(decoded_part);
				if (shared)
				{
					rec->key = (char *)shared;
					rec->is_dynamic_key = 0;
				}
				else
				{
					safe_strdup(rec->key, decoded_part);
					rec->is_dynamic_key = 1;
				}
			}
		}

		if (!next)
		{
			if (value && *value == '*')
			{
				rec->data_num = data_num;
				if (rec->data_str)
				{
					safe_free(rec->data_str);
					rec->data_str = NULL;
				}
			}
			else if (data_str)
			{
				if (rec->data_str)
					safe_free(rec->data_str);
				safe_strdup(rec->data_str, data_str);
				rec->data_num = 0;
			}
			else
			{
				if (rec->data_str)
				{
					safe_free(rec->data_str);
					rec->data_str = NULL;
				}
				rec->data_num = 0;
			}
			leaf_rec = rec;
		}

		parent = rec;
		p = next;
	}
	return leaf_rec;
}

static UdbRecord *udb_file_parse_line(UdbContext *ctx, UdbBlock *block, char *line)
{
	(void)ctx;
	return udb_file_parse_line_to_tree(block, block->tree, line);
}

static int udb_file_load_block(UdbContext *ctx, UdbBlock *block)
{
	if (!block || !block->filepath)
		return 0;

	FILE *fp = fopen(block->filepath, "r");
	if (!fp)
	{
		if (errno == ENOENT)
		{
			udb_block_reset(ctx, block);
			block->load_state = UDB_LOAD_EMPTY;
			block->checksum = 0;
			block->modified_at = 0;
			block->filesize = 0;
			return 1;
		}
		int saved_errno = errno;
		block->load_state = UDB_LOAD_FAILED;
		udb_log(ULOG_ERROR, "UDB_FILE_OPEN_FAILED", NULL, "Cannot open database file $file for block $block: $error",
				log_data_string("file", block->filepath), log_data_string("block", (char[]){block->letter, '\0'}),
				log_data_string("error", strerror(saved_errno)));
		return 0;
	}

	char *line = safe_alloc(UDB_RECORD_LINE_MAX + 2);
	unsigned int line_number = 0;
	int parse_failed = 0;
	UdbRecord *candidate = udb_record_create(NULL);
	candidate->block_idx = (unsigned char)udb_block_letter_to_index(block->letter);
	safe_strdup(candidate->key, block->tree && block->tree->key ? block->tree->key : "UDB");
	candidate->is_dynamic_key = 1;
	candidate->data_num = 1;

	while (fgets(line, UDB_RECORD_LINE_MAX + 2, fp))
	{
		size_t l_len = strlen(line);
		int has_newline = (l_len > 0 && line[l_len - 1] == '\n');
		int overlong = !has_newline && !feof(fp);
		line_number++;
		if (overlong || l_len > UDB_RECORD_LINE_MAX + 1)
		{
			char logbuf[512];
			snprintf(logbuf, sizeof(logbuf), "Overlong persisted record in block %c at line %u", block->letter,
					 line_number);
			udb_log(ULOG_ERROR, "UDB_FILE_LINE_REJECTED", NULL, "$msg", log_data_string("msg", logbuf));
			parse_failed = 1;
			break;
		}
		char *p = strchr(line, '\n');
		if (p)
			*p = '\0';
		p = strchr(line, '\r');
		if (p)
			*p = '\0';

		if (line[0] == ';' || line[0] == '\0')
			continue;

		if (!udb_file_parse_line_to_tree(block, candidate, line))
		{
			char logbuf[512];
			snprintf(logbuf, sizeof(logbuf), "Malformed persisted record in block %c at line %u", block->letter,
					 line_number);
			udb_log(ULOG_ERROR, "UDB_FILE_LINE_REJECTED", NULL, "$msg", log_data_string("msg", logbuf));
			parse_failed = 1;
			break;
		}
	}

	if (parse_failed || ferror(fp))
	{
		int saved_errno = errno;
		if (ferror(fp))
		{
			udb_log(ULOG_ERROR, "UDB_FILE_READ_ERROR", NULL, "Read error occurred on block $block file $file: $error",
					log_data_string("block", (char[]){block->letter, '\0'}), log_data_string("file", block->filepath),
					log_data_string("error", strerror(saved_errno)));
		}
		safe_free(line);
		udb_record_free_tree(candidate);
		block->load_state = UDB_LOAD_FAILED;
		fclose(fp);
		return 0;
	}

	safe_free(line);

	if (fclose(fp) != 0)
	{
		int saved_errno = errno;
		udb_log(ULOG_ERROR, "UDB_FILE_CLOSE_ERROR", NULL, "Close error occurred on block $block file $file: $error",
				log_data_string("block", (char[]){block->letter, '\0'}), log_data_string("file", block->filepath),
				log_data_string("error", strerror(saved_errno)));
		udb_record_free_tree(candidate);
		block->load_state = UDB_LOAD_FAILED;
		return 0;
	}

	unsigned int record_count = udb_record_count_tree(candidate);
	unsigned long checksum = 0;
	if (!udb_compute_tree_checksum(candidate, &checksum))
	{
		udb_log(ULOG_ERROR, "UDB_FILE_CHECKSUM_FAILED", NULL, "Failed to compute checksum for block $block file $file",
				log_data_string("block", (char[]){block->letter, '\0'}), log_data_string("file", block->filepath));
		udb_record_free_tree(candidate);
		block->load_state = UDB_LOAD_FAILED;
		return 0;
	}

	udb_block_replace_tree(ctx, block, candidate, record_count);
	block->checksum = checksum;

	for (UdbRecord *curr = block->tree->child; curr; curr = curr->sibling)
	{
		for (UdbRecord *sub = curr->child; sub; sub = sub->sibling)
			udb_apply_special_record(ctx, block, sub, 1);
		udb_apply_special_record(ctx, block, curr, 1);
	}

	block->load_state = UDB_LOAD_SUCCESS;

	struct stat st;
	if (stat(block->filepath, &st) == 0)
	{
		block->filesize = st.st_size;
		block->modified_at = st.st_mtime;
	}

	char logbuf[512];
	snprintf(logbuf, sizeof(logbuf), "Loaded block %c from %s (%u records)", block->letter, block->filepath,
			 block->record_count);
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
	}
	else
	{
		unreal_log(ULOG_DEBUG, "udb", "UDB_DEBUG", source, "[UDB Debug] $msg", log_data_string("msg", buf));
	}
}
