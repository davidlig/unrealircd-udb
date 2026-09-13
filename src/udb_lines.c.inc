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

static UdbLineExpiryPending *udb_line_expiry_pending = NULL;

static UdbRecord *udb_line_owner(UdbRecord *rec)
{
	while (rec && rec->parent && rec->parent->parent && rec->parent->parent->parent)
		rec = rec->parent;

	if (!rec || !rec->parent || !rec->parent->parent || rec->parent->parent->parent)
		return NULL;
	return rec;
}

static void udb_line_split_mask(const char *mask, char *user, size_t usersz, char *host, size_t hostsz)
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
	}
	else
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

	/* A raw regex is not a second spelling of a b64 pattern. */
	if (strncmp(stored, UDB_SPAMFILTER_B64_PREFIX, strlen(UDB_SPAMFILTER_B64_PREFIX)))
		return 0;

	encoded = stored + strlen(UDB_SPAMFILTER_B64_PREFIX);
	encoded_len = strlen(encoded);
	if (!encoded_len || encoded_len % 4 || encoded_len > ((patternsz - 1 + 2) / 3) * 4)
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
	if (!decoded_len || decoded_len > UDB_SPAMFILTER_PATTERN_MAX || decoded_len >= patternsz)
		return 0;
	n = b64_decode(encoded, (unsigned char *)pattern, patternsz);
	if (n < 0 || (size_t)n != decoded_len || memchr(pattern, '\0', decoded_len))
		return 0;
	pattern[decoded_len] = '\0';
	if (b64_encode((unsigned char *)pattern, decoded_len, canonical, sizeof(canonical)) != (int)encoded_len ||
		strcmp(canonical, encoded))
		return 0;
	return 1;
}

static int udb_line_matches_tkl(TKL *tkl, char type, const char *pattern)
{
	char user[128], host[128];

	if (!tkl || !tkl->set_by || strcmp(tkl->set_by, UDB_TKL_SET_BY))
		return 0;
	if (type == 'F')
		return TKLIsSpamfilter(tkl) && (tkl->type & TKL_GLOBAL) && tkl->ptr.spamfilter && tkl->ptr.spamfilter->match &&
			   !strcmp(tkl->ptr.spamfilter->match->str, pattern);
	if (type == 'Q')
		return TKLIsNameBan(tkl) && (tkl->type & TKL_GLOBAL) && tkl->ptr.nameban &&
			   !strcasecmp(tkl->ptr.nameban->name, pattern);

	udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
	return TKLIsServerBan(tkl) && (tkl->type & TKL_GLOBAL) &&
		   ((type == 'G' && (tkl->type & TKL_KILL)) || (type == 'Z' && (tkl->type & TKL_ZAP)) ||
			(type == 'S' && (tkl->type & TKL_SHUN))) &&
		   tkl->ptr.serverban && !strcasecmp(tkl->ptr.serverban->usermask, user) &&
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

/* A numeric expires record is a single absolute timestamp.  Its absence, not
 * zero, is the only permanent representation. */
static int udb_line_expiry(UdbRecord *line_rec, time_t *expires)
{
	UdbRecord *record;

	if (!line_rec)
		return 0;
	record = udb_record_find(udb_ctx, KKEY_EXPIRES, line_rec);
	if (!record)
		return 0;
	if (!record->data_num || (unsigned long long)record->data_num > udb_time_t_max_val())
		return -1;
	if (expires)
		*expires = (time_t)record->data_num;
	return 1;
}

static int udb_line_path(UdbRecord *line_rec, char *path, size_t pathsz)
{
	if (!line_rec || !line_rec->parent || !line_rec->parent->key || !line_rec->key)
		return 0;
	strlcpy(path, "K", pathsz);
	return udb_path_append_component(path, pathsz, line_rec->parent->key) &&
		   udb_path_append_component(path, pathsz, line_rec->key);
}

static UdbLineExpiryPending *udb_line_expiry_pending_find(char type, const char *pattern, time_t expires)
{
	UdbLineExpiryPending *pending;

	for (pending = udb_line_expiry_pending; pending; pending = pending->next)
		if (pending->type == type && pending->expires == expires && !strcmp(pending->pattern, pattern))
			return pending;
	return NULL;
}

static UdbLineExpiryPending *udb_line_expiry_pending_add(char type, const char *pattern, time_t expires)
{
	UdbLineExpiryPending *pending = udb_line_expiry_pending_find(type, pattern, expires);

	if (pending)
		return pending;
	pending = safe_alloc(sizeof(*pending));
	pending->type = type;
	pending->expires = expires;
	safe_strdup(pending->pattern, pattern);
	pending->next = udb_line_expiry_pending;
	udb_line_expiry_pending = pending;
	return pending;
}

static void udb_line_expiry_pending_clear(char type, const char *pattern)
{
	UdbLineExpiryPending **link;

	for (link = &udb_line_expiry_pending; *link;)
	{
		UdbLineExpiryPending *pending = *link;
		if (pending->type == type && !strcmp(pending->pattern, pattern))
		{
			*link = pending->next;
			safe_free(pending->pattern);
			safe_free(pending);
			continue;
		}
		link = &pending->next;
	}
}

static void udb_lines_expiry_pending_clear_record(UdbRecord *rec)
{
	UdbRecord *line_rec = udb_line_owner(rec);

	if (line_rec && line_rec->parent && line_rec->parent->key)
		udb_line_expiry_pending_clear(line_rec->parent->key[0], line_rec->key);
}

static void udb_line_apply_record(UdbRecord *rec, int is_new)
{
	char type;
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	UdbRecord *line_rec, *reason_rec;
	time_t expires = 0;
	int expiry_state;
	const char *reason;

	(void)is_new;
	line_rec = udb_line_owner(rec);
	if (!line_rec || !line_rec->parent || !line_rec->parent->key)
		return;
	type = line_rec->parent->key[0];
	if (!strchr("GZSQF", type))
		return;
	if (type == 'F' && !udb_spamfilter_pattern(line_rec->key, pattern, sizeof(pattern)))
	{
		udb_log(ULOG_ERROR, "UDB_SPAMF_PATTERN", NULL, "Invalid canonical spamfilter pattern: $pattern",
				log_data_string("pattern", line_rec->key), NULL);
		return;
	}
	if (type != 'F')
		strlcpy(pattern, line_rec->key, sizeof(pattern));

	/* An invalid complete candidate must not withdraw a valid live TKL. */
	if (!udb_k_profile_valid(line_rec))
	{
		udb_log(ULOG_ERROR, "UDB_K_PROFILE_INVALID", NULL, "Ignoring invalid complete K profile: $pattern",
				log_data_string("pattern", pattern), NULL);
		return;
	}
	expiry_state = udb_line_expiry(line_rec, &expires);
	if (expiry_state < 0)
	{
		udb_log(ULOG_ERROR, "UDB_K_EXPIRES_INVALID", NULL, "Ignoring invalid K expires record for $pattern",
				log_data_string("pattern", pattern), NULL);
		return;
	}
	if (expiry_state > 0 && expires <= TStime())
	{
		udb_line_remove_owned(type, pattern);
		return;
	}
	reason_rec = udb_record_find(udb_ctx, KKEY_REASON, line_rec);
	reason = reason_rec && reason_rec->data_str ? reason_rec->data_str : NULL;
	if (!reason)
	{
		/* A valid partial profile is deliberately inert. */
		udb_line_remove_owned(type, pattern);
		return;
	}

	if (type == 'F')
	{
		UdbRecord *match_type_rec = udb_record_find(udb_ctx, KKEY_MATCH_TYPE, line_rec);
		UdbRecord *targets_rec = udb_record_find(udb_ctx, KKEY_TARGETS, line_rec);
		UdbRecord *action_rec = udb_record_find(udb_ctx, KKEY_ACTION, line_rec);
		UdbRecord *ban_time_rec = udb_record_find(udb_ctx, KKEY_BAN_TIME, line_rec);
		const char *err = NULL;
		int target, match_type;
		BanActionValue action_value;
		BanAction *action;
		Match *match;
		time_t ban_time = 0;

		if (!match_type_rec || !targets_rec || !action_rec || !match_type_rec->data_str || !targets_rec->data_str ||
			!action_rec->data_str)
		{
			udb_line_remove_owned(type, pattern);
			return;
		}
		match_type = udb_spamfilter_match_type_value(match_type_rec->data_str);
		target = spamfilter_gettargets(targets_rec->data_str, NULL);
		action_value = banact_stringtoval(action_rec->data_str);
		action = action_value && !banact_config_only(action_value) ? banact_value_to_struct(action_value) : NULL;
		if (ban_time_rec)
			ban_time = (time_t)ban_time_rec->data_num;
		/* Compile before removing the old effect. */
		match = target > 0 && action ? unreal_create_match(match_type, pattern, &err) : NULL;
		if (!match)
		{
			udb_log(ULOG_ERROR, "UDB_SPAMF_ERROR", NULL, "Failed to prepare spamfilter candidate: $pattern ($err)",
					log_data_string("pattern", pattern), log_data_string("err", err ? err : "unknown error"), NULL);
			return;
		}
		udb_line_remove_owned(type, pattern);
		if (!tkl_add_spamfilter(TKL_SPAMF | TKL_GLOBAL, pattern, (unsigned short)target, action, match, pattern, NULL,
								UDB_TKL_SET_BY, expires, TStime(), ban_time, reason, INPUT_CONVERSION_DEFAULT, 0, 0))
		{
			unreal_delete_match(match);
			udb_log(ULOG_ERROR, "UDB_SPAMF_ADD_FAILED", NULL, "Failed to materialize spamfilter candidate: $pattern",
					log_data_string("pattern", pattern), NULL);
		}
		return;
	}

	udb_line_remove_owned(type, pattern);
	if (type == 'G')
	{
		char user[128], host[128];
		udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
		tkl_add_serverban(TKL_KILL | TKL_GLOBAL, user, host, NULL, reason, UDB_TKL_SET_BY, expires, TStime(), 0, 0);
	}
	else if (type == 'Z')
		tkl_add_serverban(TKL_ZAP | TKL_GLOBAL, "*", pattern, NULL, reason, UDB_TKL_SET_BY, expires, TStime(), 0, 0);
	else if (type == 'S')
	{
		char user[128], host[128];
		udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
		tkl_add_serverban(TKL_SHUN | TKL_GLOBAL, user, host, NULL, reason, UDB_TKL_SET_BY, expires, TStime(), 0, 0);
	}
	else if (type == 'Q')
		tkl_add_nameban(TKL_NAME | TKL_GLOBAL, pattern, 0, reason, UDB_TKL_SET_BY, expires, TStime(), 0);
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
	}
	else
		strlcpy(pattern, line_rec->key, sizeof(pattern));
	udb_line_remove_owned(type, pattern);
}

static void udb_lines_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new)
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

static void udb_lines_expiry_sweep(time_t now)
{
	UdbBlock *block;
	UdbRecord *type_rec, *line_rec;
	char paths[UDB_LINE_EXPIRY_SWEEP_MAX][UDB_RECORD_PATH_MAX + 1];
	time_t expires[UDB_LINE_EXPIRY_SWEEP_MAX];
	unsigned int count = 0, i;

	if (!udb_ctx || !udb_ready || !(block = udb_block_by_letter(udb_ctx, UDB_BLOCK_LINES)) || !block->tree)
		return;
	for (type_rec = block->tree->child; type_rec && count < UDB_LINE_EXPIRY_SWEEP_MAX; type_rec = type_rec->sibling)
	{
		if (!type_rec->key || !udb_tkl_type_valid(type_rec->key))
			continue;
		for (line_rec = type_rec->child; line_rec && count < UDB_LINE_EXPIRY_SWEEP_MAX; line_rec = line_rec->sibling)
		{
			time_t expiry;
			if (udb_line_expiry(line_rec, &expiry) == 1 && expiry <= now &&
				udb_line_path(line_rec, paths[count], sizeof(paths[count])))
				expires[count++] = expiry;
		}
	}
	for (i = 0; i < count; i++)
	{
		UdbPropagatorSelection selected;
		UdbRecord *record = udb_record_find_path(udb_ctx, block, paths[i] + 3);
		UdbRecord *line = record && record->parent && record->parent->parent == block->tree ? record : NULL;
		UdbLineExpiryPending *pending;
		char type;

		if (!line || udb_line_expiry(line, NULL) != 1)
			continue;
		type = line->parent->key[0];
		udb_line_remove_record(line);
		if (!udb_propagator_policy_present(udb_ctx) ||
			(udb_select_propagator(udb_ctx, 1, &selected) && selected.is_local))
		{
			if (udb_mutation_expire_local(udb_ctx, paths[i], expires[i]))
				udb_log(ULOG_INFO, "UDB_K_EXP_REMOVED", NULL, "Expired K profile removed authoritatively: $path",
						log_data_string("path", paths[i]), NULL);
			continue;
		}
		pending = udb_line_expiry_pending_add(type, line->key, expires[i]);
		if (udb_select_propagator(udb_ctx, 1, &selected) && selected.peer && !pending->sent)
		{
			if (udb_send_db_to_one(selected.peer, ":%s DB %s EXP %s %lu", me.id, selected.peer->id, paths[i],
								   (unsigned long)expires[i]))
			{
				pending->sent = 1;
				udb_log(ULOG_INFO, "UDB_K_EXP_REQUEST", selected.peer, "Requested authoritative expiry of $path",
						log_data_string("path", paths[i]), NULL);
			}
		}
	}
}

static void udb_lines_expiry_peer_disconnected(Client *client)
{
	UdbLineExpiryPending *pending;
	(void)client;
	/* A reconnect is an explicit controlled retry boundary. */
	for (pending = udb_line_expiry_pending; pending; pending = pending->next)
		pending->sent = 0;
}

static void udb_lines_shutdown(void)
{
	while (udb_line_expiry_pending)
	{
		UdbLineExpiryPending *next = udb_line_expiry_pending->next;
		safe_free(udb_line_expiry_pending->pattern);
		safe_free(udb_line_expiry_pending);
		udb_line_expiry_pending = next;
	}
}

static void udb_lines_init(ModuleInfo *modinfo)
{
	(void)modinfo;
	/* The shared one-second sync event invokes the lightweight K expiry sweep. */
}
