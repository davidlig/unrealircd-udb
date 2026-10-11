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

static int udb_parse_uint64_strict(const char *s, uint64_t *out)
{
	unsigned long long val;

	if (!udb_strtoull_strict(s, &val) || val > UINT64_MAX)
		return 0;
	if (out)
		*out = (uint64_t)val;
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

static int udb_digest_parse(const char *input, char out_hex[UDB_SHA256_HEX_LEN + 1])
{
	if (!input || strlen(input) != UDB_SHA256_HEX_LEN)
		return 0;
	for (int i = 0; i < UDB_SHA256_HEX_LEN; i++)
	{
		unsigned char c = (unsigned char)input[i];
		if (!isdigit(c) && (c < 'a' || c > 'f'))
			return 0;
		if (out_hex)
			out_hex[i] = (char)c;
	}
	if (out_hex)
		out_hex[UDB_SHA256_HEX_LEN] = '\0';
	return 1;
}

static int udb_numeric_record_valid(const char *value)
{
	unsigned long val;

	if (!value || value[0] != '*' || value[1] == '\0')
		return 0;
	return udb_strtoul_strict(value + 1, &val);
}

static int udb_clone_limit_valid(const char *value)
{
	unsigned long val;

	return udb_numeric_record_valid(value) && udb_strtoul_strict(value + 1, &val) && val <= INT_MAX;
}

static int udb_k_expires_valid(const char *value)
{
	time_t expires;

	/* Zero is deliberately not a second spelling of permanent. */
	return udb_numeric_record_valid(value) && udb_parse_time_t(value + 1, &expires) && expires > 0;
}

static int udb_zline_mask_valid(const char *mask)
{
	char address[INET6_ADDRSTRLEN], canonical[INET6_ADDRSTRLEN];
	const char *slash;
	unsigned int prefix;
	int family;
	size_t length;
	struct in_addr v4, v4_network;
	struct in6_addr v6, v6_network;

	if (!mask || !*mask || strchr(mask, '@'))
		return 0;
	slash = strchr(mask, '/');
	length = slash ? (size_t)(slash - mask) : strlen(mask);
	if (!length || length >= sizeof(address) || (slash && strchr(slash + 1, '/')))
		return 0;
	memcpy(address, mask, length);
	address[length] = '\0';
	if (inet_pton(AF_INET, address, &v4) == 1)
	{
		family = AF_INET;
		if (!inet_ntop(family, &v4, canonical, sizeof(canonical)) || strcmp(address, canonical))
			return 0;
	}
	else if (inet_pton(AF_INET6, address, &v6) == 1)
	{
		family = AF_INET6;
		if (!inet_ntop(family, &v6, canonical, sizeof(canonical)) || strcmp(address, canonical))
			return 0;
	}
	else
		return 0;
	if (!slash)
		return 1;
	if (!udb_parse_uint_strict(slash + 1, &prefix, 0, family == AF_INET ? 32 : 128))
		return 0;
	/* CIDR identities are network identities: host bits would be an alias. */
	if (family == AF_INET)
	{
		uint32_t raw = ntohl(v4.s_addr);
		uint32_t network = prefix ? raw & (0xffffffffU << (32 - prefix)) : 0;
		v4_network.s_addr = htonl(network);
		return !memcmp(&v4, &v4_network, sizeof(v4));
	}
	memcpy(&v6_network, &v6, sizeof(v6));
	for (unsigned int i = prefix; i < 128; i++)
		if (v6_network.s6_addr[i / 8] & (0x80U >> (i % 8)))
			return 0;
	return 1;
}

static int udb_spamfilter_match_type_valid(const char *value)
{
	return value && (!strcmp(value, "regex") || !strcmp(value, "simple"));
}

static int udb_spamfilter_match_type_value(const char *value)
{
	return value && !strcmp(value, "simple") ? MATCH_SIMPLE : MATCH_PCRE_REGEX;
}

static int udb_spamfilter_pattern_valid(const char *stored, const char *match_type)
{
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	Match *match;

	if (!udb_spamfilter_pattern(stored, pattern, sizeof(pattern)))
		return 0;
	if (!match_type)
		return 1; /* A partial F profile is stored but never materialized. */
	match = unreal_create_match(udb_spamfilter_match_type_value(match_type), pattern, NULL);
	if (!match)
		return 0;
	unreal_delete_match(match);
	return 1;
}

static int udb_spamfilter_targets_valid(const char *value)
{
	static const char canonical[] = "cpnNPqduatTR";
	const char *p;
	unsigned short targets;

	if (!value || !*value || strlen(value) >= sizeof(canonical))
		return 0;
	for (p = value; *p; p++)
	{
		const char *pos = strchr(canonical, *p);
		if (!pos || (p > value && p[-1] >= *p))
			return 0;
	}
	targets = (unsigned short)spamfilter_gettargets(value, NULL);
	return targets && !strcmp(value, spamfilter_target_inttostring(targets));
}

static int udb_k_ban_time_valid(const char *value)
{
	time_t duration;
	return udb_numeric_record_valid(value) && udb_parse_time_t(value + 1, &duration) && duration > 0;
}

static int udb_line_reason_valid(const char *value)
{
	const unsigned char *p;
	if (!value || !*value || value[0] == '*')
		return 0;
	for (p = (const unsigned char *)value; *p; p++)
		if (*p == '\r' || *p == '\n')
			return 0;
	return 1;
}

static int udb_line_mask_valid(const char *mask)
{
	const char *at;
	size_t userlen, hostlen;

	if (!mask || !*mask || !(at = strchr(mask, '@')) || strchr(at + 1, '@'))
		return 0;
	userlen = (size_t)(at - mask);
	hostlen = strlen(at + 1);
	return userlen > 0 && userlen <= UDB_TKL_MASK_COMPONENT_MAX && hostlen > 0 && hostlen <= UDB_TKL_MASK_COMPONENT_MAX;
}

static int udb_qline_mask_valid(const char *mask)
{
	const unsigned char *p;
	size_t len;

	if (!mask || !(len = strlen(mask)) || len > NICKLEN)
		return 0;
	for (p = (const unsigned char *)mask; *p; p++)
		if (iscntrl(*p) || *p == ':' || *p == '@')
			return 0;
	return 1;
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

		/* +r is UDB-owned registration state, +P and +O are expressed by
		 * C::options bits, and member ranks never live here. */
		if (*c == 'r' || *c == 'P' || *c == 'O')
			return 0;

		cm = find_channel_mode_handler(*c);
		if (!cm)
			return 0;

		if (cm->type == CMODE_MEMBER)
			return 0;

		mode_letters++;
		if (what == MODE_ADD && cm->paracount > 0)
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

	return expected_params <= MAXMODEPARAMS && expected_params == actual_params;
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
		if (!isalpha((unsigned char)*c) || !is_valid_snomask(*c))
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

static int udb_spamfilter_action_valid(const char *value)
{
	BanActionValue action = value && *value ? banact_stringtoval(value) : 0;
	return action && !banact_config_only(action);
}

/* Admission, staged snapshots, disk load and runtime apply all use this
 * candidate-level check.  Partial profiles are safe and inert; a complete
 * profile must be materializable before it can replace an active one. */
static const UdbKeyDescriptor udb_schema_n_subkeys[] = {
	{NKEY_ACCESS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_PASS, UDB_VAL_STRING, udb_password_hash_valid, 0, NULL},
	{NKEY_VHOST, UDB_VAL_STRING, udb_vhost_valid, 0, NULL},
	{NKEY_FORBID, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_SUSPEND, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_OPER, UDB_VAL_STRING, udb_oper_record_valid, 0, NULL},
	{NKEY_MODES, UDB_VAL_STRING, udb_user_modes_record_valid, 0, NULL},
	{NKEY_SNOMASKS, UDB_VAL_STRING, udb_snomasks_record_valid, 0, NULL},
	{NKEY_SWHOIS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_c_subkeys[] = {
	{CKEY_FOUNDER, UDB_VAL_STRING, udb_nick_name_valid, 0, NULL},
	{CKEY_MODES, UDB_VAL_STRING, udb_channel_modes_record_valid, 0, NULL},
	{CKEY_TOPIC, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_ACCESS, UDB_VAL_ANY, NULL, 1, udb_nick_name_valid},
	{CKEY_FORBID, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_SUSPEND, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_OPTIONS, UDB_VAL_NUMERIC, udb_options_record_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_i_subkeys[] = {
	{IKEY_CLONES, UDB_VAL_NUMERIC, udb_clone_limit_valid, 0, NULL},
	{IKEY_NOLINES, UDB_VAL_STRING, udb_nolines_record_valid, 0, NULL},
	{IKEY_HOST, UDB_VAL_STRING, udb_vhost_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_s_subkeys[] = {
	{SKEY_CLONES, UDB_VAL_NUMERIC, udb_clone_limit_valid, 0, NULL},
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

static const UdbBlockDescriptor udb_block_descriptors[] = {
	{UDB_BLOCK_NICKS, "Nicks", UDB_BLOCK_MASK_N, &udb_block_schemas[0]},
	{UDB_BLOCK_CHANNELS, "Channels", UDB_BLOCK_MASK_C, &udb_block_schemas[1]},
	{UDB_BLOCK_IPS, "IPs", UDB_BLOCK_MASK_I, &udb_block_schemas[2]},
	{UDB_BLOCK_SETTINGS, "Settings", UDB_BLOCK_MASK_S, &udb_block_schemas[3]},
	{UDB_BLOCK_LINKS, "Links", UDB_BLOCK_MASK_L, &udb_block_schemas[4]},
	{UDB_BLOCK_LINES, "Lines", UDB_BLOCK_MASK_K, &udb_block_schemas[5]},
};

_Static_assert(sizeof(udb_block_descriptors) / sizeof(udb_block_descriptors[0]) == UDB_NUM_BLOCKS,
			   "block descriptor table must cover every UDB block");

static const UdbBlockDescriptor *udb_block_descriptor(unsigned int index)
{
	return index < UDB_NUM_BLOCKS ? &udb_block_descriptors[index] : NULL;
}

static int udb_block_descriptors_validate(void)
{
	for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		const UdbBlockDescriptor *descriptor = &udb_block_descriptors[i];

		if (!descriptor->name || !descriptor->schema || descriptor->schema->letter != descriptor->letter ||
			descriptor->mask != (1u << i))
			return 0;
	}
	return 1;
}

static int udb_block_letter_to_index(char letter)
{
	for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
		if (udb_block_descriptors[i].letter == letter)
			return (int)i;
	return -1;
}

static unsigned int udb_block_letter_to_mask(char letter)
{
	int index = udb_block_letter_to_index(letter);

	return index < 0 ? 0 : udb_block_descriptors[index].mask;
}

static const UdbBlockSchema *udb_get_block_schema(char letter)
{
	int index = udb_block_letter_to_index(letter);

	return index < 0 ? NULL : udb_block_descriptors[index].schema;
}

static void udb_sync_session_free(UdbBlock *block)
{
	if (!block || !block->session)
		return;
	udb_storage_free(block->session);
	block->session = NULL;
	block->syncing_from = NULL;
}

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

	UdbStorageRead *read = NULL;
	if (udb_ctx && udb_ctx->storage && udb_storage_read_open(udb_ctx->storage, &read) == UDB_STORAGE_OK)
	{
		Client *client;
		list_for_each_entry(client, &server_list, special_node) if (IsServer(client) && client != source &&
																	udb_native_link_debug(read, client->name))
			sendto_one(client, NULL, ":%s NOTICE %s :[UDB Debug] %s", me.id, client->id, buf);
		udb_storage_read_close(&read);
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
