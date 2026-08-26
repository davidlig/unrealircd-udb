/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Distributed *lines (K, Z, Shun, Q) and Spamfilter Rules
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static UdbRecord *udb_line_owner(UdbRecord *rec)
{
	while (rec && rec->parent && rec->parent->parent &&
	       rec->parent->parent->parent)
		rec = rec->parent;

	if (!rec || !rec->parent || !rec->parent->parent ||
	    rec->parent->parent->parent)
		return NULL;
	return rec;
}

static void udb_line_split_mask(const char *mask, char *user, size_t usersz,
                                char *host, size_t hostsz)
{
	const char *at = strchr(mask, '@');

	if (at)
	{
		size_t userlen = at - mask;
		if (userlen >= usersz)
			userlen = usersz - 1;
		memcpy(user, mask, userlen);
		user[userlen] = '\0';
		strlcpy(host, at + 1, hostsz);
	} else
	{
		strlcpy(user, "*", usersz);
		strlcpy(host, mask, hostsz);
	}
}

static int udb_spamfilter_pattern(const char *stored, char *pattern, size_t patternsz)
{
	const char *encoded;
	size_t encoded_len, padding = 0, decoded_len, i;
	char canonical[UDB_SPAMFILTER_PATTERN_MAX * 4 / 3 + 5];
	int n;

	if (strncmp(stored, UDB_SPAMFILTER_B64_PREFIX,
	            strlen(UDB_SPAMFILTER_B64_PREFIX)))
	{
		if (!*stored || strlen(stored) >= patternsz)
			return 0;
		strlcpy(pattern, stored, patternsz);
		return 1;
	}

	encoded = stored + strlen(UDB_SPAMFILTER_B64_PREFIX);
	encoded_len = strlen(encoded);
	if (!encoded_len || encoded_len % 4 ||
	    encoded_len > ((patternsz - 1 + 2) / 3) * 4)
		return 0;
	if (encoded[encoded_len - 1] == '=')
		padding++;
	if (encoded_len > 1 && encoded[encoded_len - 2] == '=')
		padding++;
	if (padding > 2)
		return 0;
	for (i = 0; i < encoded_len - padding; i++)
	{
		if (!isalnum((unsigned char)encoded[i]) && encoded[i] != '+' && encoded[i] != '/')
			return 0;
	}
	for (; i < encoded_len; i++)
	{
		if (encoded[i] != '=')
			return 0;
	}
	decoded_len = (encoded_len / 4) * 3 - padding;
	if (!decoded_len || decoded_len >= patternsz)
		return 0;
	n = b64_decode(encoded, (unsigned char *)pattern, patternsz - 1);
	if (n < 0 || (size_t)n != decoded_len || memchr(pattern, '\0', decoded_len))
		return 0;
	pattern[decoded_len] = '\0';
	if (b64_encode((unsigned char *)pattern, decoded_len, canonical,
	               sizeof(canonical)) != (int)encoded_len ||
	    strcmp(canonical, encoded))
		return 0;
	return 1;
}

static int udb_line_matches_tkl(TKL *tkl, char type, const char *pattern)
{
	char user[128], host[128];

	if (!tkl || !tkl->set_by || strcmp(tkl->set_by, "UDB"))
		return 0;
	if (type == 'F')
		return TKLIsSpamfilter(tkl) && (tkl->type & TKL_GLOBAL) &&
		       tkl->ptr.spamfilter && tkl->ptr.spamfilter->match &&
		       !strcmp(tkl->ptr.spamfilter->match->str, pattern);
	if (type == 'Q')
		return TKLIsNameBan(tkl) && (tkl->type & TKL_GLOBAL) &&
		       tkl->ptr.nameban && !strcasecmp(tkl->ptr.nameban->name, pattern);

	udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
	return TKLIsServerBan(tkl) && (tkl->type & TKL_GLOBAL) &&
	       ((type == 'G' && (tkl->type & TKL_KILL)) ||
	        (type == 'Z' && (tkl->type & TKL_ZAP)) ||
	        (type == 'S' && (tkl->type & TKL_SHUN))) &&
	       tkl->ptr.serverban &&
	       !strcasecmp(tkl->ptr.serverban->usermask, user) &&
	       !strcasecmp(tkl->ptr.serverban->hostmask, host);
}

static void udb_line_remove_owned(char type, const char *pattern)
{
	TKL *tkl, *next;
	int i, j;

	for (i = 0; i < TKLISTLEN; i++)
	{
		for (tkl = tklines[i]; tkl; tkl = next)
		{
			next = tkl->next;
			if (udb_line_matches_tkl(tkl, type, pattern))
			{
				if (type == 'S')
					tkl_check_local_remove_shun(tkl);
				tkl_del_line(tkl);
			}
		}
	}
	for (i = 0; i < TKLIPHASHLEN1; i++)
	{
		for (j = 0; j < TKLIPHASHLEN2; j++)
		{
			for (tkl = tklines_ip_hash[i][j]; tkl; tkl = next)
			{
				next = tkl->next;
				if (udb_line_matches_tkl(tkl, type, pattern))
					tkl_del_line(tkl);
			}
		}
	}
}

static void udb_line_apply_record(UdbRecord *rec, int is_new)
{
	char type;
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	UdbRecord *line_rec, *raz, *dur;
	time_t expires;

	(void)is_new;
	line_rec = udb_line_owner(rec);
	if (!line_rec || !line_rec->parent || !line_rec->parent->key)
		return;
	type = line_rec->parent->key[0];
	if (!strchr("GZSQF", type))
		return;
	if (type == 'F' && !udb_spamfilter_pattern(line_rec->key, pattern, sizeof(pattern)))
	{
		udb_log(ULOG_ERROR, "UDB_SPAMF_PATTERN", NULL,
		        "Invalid spamfilter pattern: $pattern",
		        log_data_string("pattern", line_rec->key), NULL);
		return;
	}
	if (type != 'F')
		strlcpy(pattern, line_rec->key, sizeof(pattern));

	udb_line_remove_owned(type, pattern);

	const char *reason = NULL;
	raz = udb_record_find(udb_ctx, KKEY_REASON, line_rec);
	if (raz && raz->data_str)
	{
		reason = raz->data_str;
	} else if (line_rec->data_str)
	{
		reason = line_rec->data_str;
	}

	if (!reason)
		return;

	dur = udb_record_find(udb_ctx, KKEY_DURATION, line_rec);
	if (dur && dur->data_num)
	{
		if (!udb_time_add(TStime(), dur->data_num, &expires))
			expires = 0;
	} else
	{
		expires = 0;
	}

	if (type == 'F')
	{
		UdbRecord *tip = udb_record_find(udb_ctx, KKEY_TYPE, line_rec);
		UdbRecord *acc = udb_record_find(udb_ctx, KKEY_ACTION, line_rec);

		if (tip && acc && tip->data_str && acc->data_str)
		{
			int target = spamfilter_getconftargets(tip->data_str);
			BanActionValue act_val = banact_stringtoval(acc->data_str);
			BanAction *action = banact_value_to_struct(act_val);

			const char *err = NULL;
			Match *match = target > 0 && action ? unreal_create_match(MATCH_PCRE_REGEX, pattern, &err) : NULL;
			if (match)
			{
				tkl_add_spamfilter(TKL_SPAMF | TKL_GLOBAL, pattern, target, action, match,
				                   pattern, NULL, "UDB", expires, TStime(),
				                   dur ? (time_t)dur->data_num : 0, reason, 0, 0, 0);
			} else
			{
				udb_log(ULOG_ERROR, "UDB_SPAMF_ERROR", NULL, "Failed to compile spamfilter regex: $regex ($err)",
				        log_data_string("regex", pattern),
				        log_data_string("err", err ? err : "unknown error"), NULL);
			}
		}
	} else
	{
		char user[128];
		char host[128];
		udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));

		if (type == 'G')
		{
			tkl_add_serverban(TKL_KILL | TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", expires, TStime(), 0, 0);
		} else if (type == 'Z')
		{
			tkl_add_serverban(TKL_ZAP | TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", expires, TStime(), 0, 0);
		} else if (type == 'S')
		{
			tkl_add_serverban(TKL_SHUN | TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", expires, TStime(), 0, 0);
		} else if (type == 'Q')
		{
			tkl_add_nameban(TKL_NAME | TKL_GLOBAL, pattern, 0, reason, "UDB",
			                expires, TStime(), 0);
		}
	}
}

static void udb_line_remove_record(UdbRecord *rec)
{
	char type;
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	UdbRecord *line_rec = udb_line_owner(rec);

	if (!line_rec || !line_rec->parent || !line_rec->parent->key)
		return;

	type = line_rec->parent->key[0];
	if (!strchr("GZSQF", type))
		return;
	if (type == 'F')
	{
		if (!udb_spamfilter_pattern(line_rec->key, pattern, sizeof(pattern)))
			return;
	} else
	{
		strlcpy(pattern, line_rec->key, sizeof(pattern));
	}
	udb_line_remove_owned(type, pattern);
}

static void udb_lines_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec,
                                   int is_new)
{
	if (!ctx || !block || !rec)
		return;
	udb_line_apply_record(rec, is_new);
}

static void udb_lines_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (!ctx || !block || !rec)
		return;
	udb_line_remove_record(rec);
}

static void udb_lines_init(ModuleInfo *modinfo)
{
	/* TKL system handles network bans automatically, no hooks required here */
}
