/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Distributed G/Z/S/Q Lines, Spamfilters & Expiry
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static UdbLineExpiryPending *udb_line_expiry_pending = NULL;

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

static void udb_line_remove_owned_list(TKL *head, char type, const char *pattern)
{
	for (TKL *line = head, *next; line; line = next)
	{
		next = line->next;
		if (udb_line_matches_tkl(line, type, pattern))
		{
			if (type == 'S')
				tkl_check_local_remove_shun(line);
			tkl_del_line(line);
		}
	}
}

static void udb_line_remove_owned(char type, const char *pattern)
{
	if (!strchr("GZSQF", type) || !pattern)
		return;
	int bucket = tkl_hash((unsigned char)type);
	if (bucket >= 0 && bucket < TKLISTLEN)
		udb_line_remove_owned_list(tklines[bucket], type, pattern);
	int ip_type = tkl_ip_hash_type(type);
	if (ip_type < 0 || ip_type >= TKLIPHASHLEN1)
		return;
	char user[128], host[128];
	udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
	int ip_bucket = tkl_ip_hash(host);
	if (ip_bucket >= 0 && ip_bucket < TKLIPHASHLEN2)
		udb_line_remove_owned_list(tklines_ip_hash[ip_type][ip_bucket], type, pattern);
}

static const char *const udb_native_line_fields[] = {KKEY_ACTION,	  KKEY_BAN_TIME, KKEY_EXPIRES,
													 KKEY_MATCH_TYPE, KKEY_REASON,	 KKEY_TARGETS};
static UdbStorageField *udb_line_field(UdbStorageProfile *profile, const char *name)
{
	if (!profile || profile->key.block != UDB_BLOCK_LINES || profile->key.depth != 2 || profile->count != 6)
		return NULL;
	for (unsigned int i = 0; i < 6; i++)
		if (!strcmp(name, udb_native_line_fields[i]))
			return profile->fields[i].present ? profile->fields + i : NULL;
	return NULL;
}
static int udb_line_expiry(UdbStorageProfile *profile, time_t *expires)
{
	UdbStorageField *field = udb_line_field(profile, KKEY_EXPIRES);
	if (!field)
		return 0;
	if (field->kind != UDB_STORAGE_VALUE_NUMBER || !field->number || field->number > udb_time_t_max_val())
		return -1;
	if (expires)
		*expires = (time_t)field->number;
	return 1;
}
static int udb_line_path(UdbStorageProfile *profile, char *path, size_t capacity)
{
	return profile && udb_native_key_path(&profile->key, path, capacity) == UDB_STORAGE_OK;
}
static char udb_line_type(UdbStorageProfile *profile)
{
	if (!profile || profile->key.depth != 2 || profile->key.components[0].length != 1)
		return 0;
	unsigned char type = profile->key.components[0].data[0];
	return (char)(type >= 'a' && type <= 'z' ? type - ('a' - 'A') : type);
}
static int udb_line_stored_pattern(UdbStorageProfile *profile, char *out, size_t capacity)
{
	return profile && profile->key.depth == 2 && udb_native_span_string(profile->key.components[1], out, capacity);
}
static int udb_line_runtime_pattern(UdbStorageProfile *profile, char *out, size_t capacity)
{
	char stored[UDB_STORAGE_COMPONENT_MAX + 1];
	if (!udb_line_stored_pattern(profile, stored, sizeof(stored)))
		return 0;
	if (udb_line_type(profile) == 'F')
		return udb_spamfilter_pattern(stored, out, capacity);
	if (strlen(stored) >= capacity)
		return 0;
	memcpy(out, stored, strlen(stored) + 1);
	return 1;
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
	if (!udb_ctx || !udb_ctx->storage)
		return NULL;
	unsigned int count = 0;
	for (UdbLineExpiryPending *entry = udb_line_expiry_pending; entry; entry = entry->next)
		count++;
	if (count >= UDB_LINE_EXPIRY_SWEEP_MAX || strlen(pattern) > UDB_STORAGE_COMPONENT_MAX)
		return NULL;
	pending = udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*pending));
	if (!pending)
		return NULL;
	pending->pattern = udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, strlen(pattern) + 1);
	if (!pending->pattern)
	{
		udb_storage_free(pending);
		return NULL;
	}
	pending->type = type;
	pending->expires = expires;
	memcpy(pending->pattern, pattern, strlen(pattern) + 1);
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
			udb_storage_free(pending->pattern);
			udb_storage_free(pending);
			continue;
		}
		link = &pending->next;
	}
}

static UdbStorageResult udb_line_apply_profile(UdbStorageProfile *line_rec)
{
	char type;
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	UdbStorageField *reason_rec;
	time_t expires = 0;
	int expiry_state;
	const char *reason;

	type = udb_line_type(line_rec);
	if (!type || !strchr("GZSQF", type) || !udb_line_runtime_pattern(line_rec, pattern, sizeof(pattern)))
		return UDB_STORAGE_CORRUPT;
	/* Complete native profile validation preceded durable publication. */
	expiry_state = udb_line_expiry(line_rec, &expires);
	if (expiry_state < 0)
	{
		udb_log(ULOG_ERROR, "UDB_K_EXPIRES_INVALID", NULL, "Ignoring invalid K expires record for $pattern",
				log_data_string("pattern", pattern), NULL);
		return UDB_STORAGE_CORRUPT;
	}
	if (expiry_state > 0 && expires <= TStime())
	{
		udb_line_remove_owned(type, pattern);
		return UDB_STORAGE_OK;
	}
	reason_rec = udb_line_field(line_rec, KKEY_REASON);
	reason = reason_rec && reason_rec->kind == UDB_STORAGE_VALUE_STRING ? reason_rec->string : NULL;
	if (!reason)
	{
		/* A valid partial profile is deliberately inert. */
		udb_line_remove_owned(type, pattern);
		return UDB_STORAGE_OK;
	}

	if (type == 'F')
	{
		UdbStorageField *match_type_rec = udb_line_field(line_rec, KKEY_MATCH_TYPE);
		UdbStorageField *targets_rec = udb_line_field(line_rec, KKEY_TARGETS);
		UdbStorageField *action_rec = udb_line_field(line_rec, KKEY_ACTION);
		UdbStorageField *ban_time_rec = udb_line_field(line_rec, KKEY_BAN_TIME);
		const char *err = NULL;
		int target, match_type;
		BanActionValue action_value;
		BanAction *action;
		Match *match;
		time_t ban_time = 0;

		if (!match_type_rec || !targets_rec || !action_rec || match_type_rec->kind != UDB_STORAGE_VALUE_STRING ||
			targets_rec->kind != UDB_STORAGE_VALUE_STRING || action_rec->kind != UDB_STORAGE_VALUE_STRING)
		{
			udb_line_remove_owned(type, pattern);
			return UDB_STORAGE_OK;
		}
		match_type = udb_spamfilter_match_type_value(match_type_rec->string);
		target = spamfilter_gettargets(targets_rec->string, NULL);
		action_value = banact_stringtoval(action_rec->string);
		action = action_value && !banact_config_only(action_value) ? banact_value_to_struct(action_value) : NULL;
		if (ban_time_rec)
			ban_time = (time_t)ban_time_rec->number;
		/* Compile before removing the old effect. */
		match = target > 0 && action ? unreal_create_match(match_type, pattern, &err) : NULL;
		if (!match)
		{
			udb_log(ULOG_ERROR, "UDB_SPAMF_ERROR", NULL, "Failed to prepare spamfilter candidate: $pattern ($err)",
					log_data_string("pattern", pattern), log_data_string("err", err ? err : "unknown error"), NULL);
			safe_free_all_ban_actions(action);
			return UDB_STORAGE_ERROR;
		}
		udb_line_remove_owned(type, pattern);
		if (!tkl_add_spamfilter(TKL_SPAMF | TKL_GLOBAL, pattern, (unsigned short)target, action, match, pattern, NULL,
								UDB_TKL_SET_BY, expires, TStime(), ban_time, reason, INPUT_CONVERSION_DEFAULT, 0, 0))
		{
			unreal_delete_match(match);
			safe_free_all_ban_actions(action);
			udb_log(ULOG_ERROR, "UDB_SPAMF_ADD_FAILED", NULL, "Failed to materialize spamfilter candidate: $pattern",
					log_data_string("pattern", pattern), NULL);
			return UDB_STORAGE_ERROR;
		}
		return UDB_STORAGE_OK;
	}

	udb_line_remove_owned(type, pattern);
	TKL *added = NULL;
	if (type == 'G')
	{
		char user[128], host[128];
		udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
		added =
			tkl_add_serverban(TKL_KILL | TKL_GLOBAL, user, host, NULL, reason, UDB_TKL_SET_BY, expires, TStime(), 0, 0);
	}
	else if (type == 'Z')
		added = tkl_add_serverban(TKL_ZAP | TKL_GLOBAL, "*", pattern, NULL, reason, UDB_TKL_SET_BY, expires, TStime(),
								  0, 0);
	else if (type == 'S')
	{
		char user[128], host[128];
		udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
		added =
			tkl_add_serverban(TKL_SHUN | TKL_GLOBAL, user, host, NULL, reason, UDB_TKL_SET_BY, expires, TStime(), 0, 0);
	}
	else if (type == 'Q')
		added = tkl_add_nameban(TKL_NAME | TKL_GLOBAL, pattern, 0, reason, UDB_TKL_SET_BY, expires, TStime(), 0);
	return added ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
}

static void udb_line_remove_profile(UdbStorageProfile *profile)
{
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	char type = udb_line_type(profile);
	if (type && strchr("GZSQF", type) && udb_line_runtime_pattern(profile, pattern, sizeof(pattern)))
		udb_line_remove_owned(type, pattern);
}

static int udb_line_profiles_equal(UdbStorageProfile *a, UdbStorageProfile *b)
{
	if (!a || !b)
		return a == b;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageField *left = a->fields + i, *right = b->fields + i;
		if (left->present != right->present ||
			(left->present &&
			 (left->kind != right->kind || left->number != right->number || left->length != right->length ||
			  (left->kind == UDB_STORAGE_VALUE_STRING && memcmp(left->string, right->string, left->length)))))
			return 0;
	}
	return 1;
}

/* One representative scalar per complete owner. Radix leaves need not keep
 * all of an entity's keys adjacent, so no stream adjacency assumption is used. */
static UdbStorageResult udb_line_profile_scan(UdbStorageRead *source, UdbStorageRead *other, int old_pass,
											  const UdbStorageRow *prefix)
{
	UdbStorageBlockIterator *iterator = NULL;
	UdbStorageRecord record;
	UdbStorageResult result = udb_storage_read_iterator(source, UDB_BLOCK_LINES, prefix, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	while ((result = udb_storage_block_iterator_next(iterator, &record)) == UDB_STORAGE_OK)
	{
		if (record.row.depth < 2)
			continue;
		UdbStorageRow key = record.row;
		key.depth = 2;
		key.kind = UDB_STORAGE_VALUE_NONE;
		key.number = 0;
		key.value.data = NULL;
		key.value.length = 0;
		UdbStorageProfile *profile = NULL, *replacement = NULL;
		result = udb_storage_profile_load(source, &key, udb_native_line_fields, 6, &profile);
		if (result != UDB_STORAGE_OK)
			break;
		unsigned int representative = 0;
		while (representative < 6 && !profile->fields[representative].present)
			representative++;
		int chosen = representative == 6
						 ? record.row.depth == 2
						 : record.row.depth == 3 &&
							   record.row.components[2].length == strlen(udb_native_line_fields[representative]) &&
							   !memcmp(record.row.components[2].data, udb_native_line_fields[representative],
									   record.row.components[2].length);
		if (chosen)
		{
			result = other ? udb_storage_profile_load(other, &key, udb_native_line_fields, 6, &replacement)
						   : UDB_STORAGE_NOT_FOUND;
			if (result < UDB_STORAGE_NOT_FOUND)
			{
				udb_storage_profile_free(&profile);
				break;
			}
			if (old_pass && !udb_line_profiles_equal(profile, replacement))
			{
				if (replacement)
					result = udb_line_apply_profile(replacement);
				else
					udb_line_remove_profile(profile);
			}
			else if (!old_pass && !replacement)
				result = udb_line_apply_profile(profile);
		}
		udb_storage_profile_free(&profile);
		udb_storage_profile_free(&replacement);
		if (result < UDB_STORAGE_NOT_FOUND)
			break;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = UDB_STORAGE_OK;
	if (udb_storage_block_iterator_close(&iterator) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_lines_reconcile_scope(UdbStorageRead *before, UdbStorageRead *after,
												  const UdbStorageRow *changed)
{
	UdbStorageRow key = *changed;
	key.kind = UDB_STORAGE_VALUE_NONE;
	key.number = 0;
	key.value.data = NULL;
	key.value.length = 0;
	if (key.depth < 2)
	{
		UdbStorageResult result = udb_line_profile_scan(before, after, 1, &key);
		return result == UDB_STORAGE_OK ? udb_line_profile_scan(after, before, 0, &key) : result;
	}
	key.depth = 2;
	UdbStorageProfile *old = NULL, *current = NULL;
	UdbStorageResult prior = udb_storage_profile_load(before, &key, udb_native_line_fields, 6, &old);
	UdbStorageResult result = udb_storage_profile_load(after, &key, udb_native_line_fields, 6, &current);
	if (prior < UDB_STORAGE_NOT_FOUND || result < UDB_STORAGE_NOT_FOUND)
		result = prior < UDB_STORAGE_NOT_FOUND ? prior : result;
	else
	{
		result = UDB_STORAGE_OK;
		if (!udb_line_profiles_equal(old, current))
		{
			if (current)
				result = udb_line_apply_profile(current);
			else if (old)
				udb_line_remove_profile(old);
			char stored[UDB_STORAGE_COMPONENT_MAX + 1];
			if (udb_native_span_string(key.components[1], stored, sizeof(stored)))
				udb_line_expiry_pending_clear((char)(key.components[0].data[0] - 'a' + 'A'), stored);
		}
	}
	udb_storage_profile_free(&old);
	udb_storage_profile_free(&current);
	return result;
}

static UdbStorageResult udb_lines_reconcile_generations(UdbStorageRead *before, UdbStorageRead *after)
{
	UdbStorageResult result = udb_line_profile_scan(before, after, 1, NULL);
	if (result == UDB_STORAGE_OK)
		result = udb_line_profile_scan(after, before, 0, NULL);
	return result;
}

static UdbStorageRead *udb_line_expiry_read;
static UdbStorageBlockIterator *udb_line_expiry_iterator;
static void udb_line_expiry_cursor_close(void)
{
	if (udb_line_expiry_iterator)
		udb_storage_block_iterator_close(&udb_line_expiry_iterator);
	if (udb_line_expiry_read)
		udb_storage_read_close(&udb_line_expiry_read);
}

static void udb_lines_expiry_sweep(time_t now)
{
	if (!udb_ctx || !udb_ctx->storage || !udb_ready || udb_reconcile.active)
	{
		udb_line_expiry_cursor_close();
		return;
	}
	if (!udb_line_expiry_read && udb_storage_read_open(udb_ctx->storage, &udb_line_expiry_read) != UDB_STORAGE_OK)
		return;
	if (!udb_line_expiry_iterator && udb_storage_read_iterator(udb_line_expiry_read, UDB_BLOCK_LINES, NULL,
															   &udb_line_expiry_iterator) != UDB_STORAGE_OK)
	{
		udb_line_expiry_cursor_close();
		return;
	}
	unsigned int expired = 0;
	for (unsigned int visited = 0; visited < 8192 && expired < UDB_LINE_EXPIRY_SWEEP_MAX; visited++)
	{
		UdbStorageRecord record;
		UdbStorageResult result = udb_storage_block_iterator_next(udb_line_expiry_iterator, &record);
		if (result != UDB_STORAGE_OK)
		{
			udb_line_expiry_cursor_close();
			break;
		}
		UdbStorageRow key = record.row;
		if (key.depth != 3 || key.components[2].length != strlen(KKEY_EXPIRES) ||
			memcmp(key.components[2].data, KKEY_EXPIRES, key.components[2].length) ||
			key.kind != UDB_STORAGE_VALUE_NUMBER || !key.number || key.number > (uint64_t)now)
			continue;
		time_t expiry = (time_t)key.number;
		key.depth = 2;
		key.kind = UDB_STORAGE_VALUE_NONE;
		key.number = 0;
		UdbStorageProfile *profile = NULL;
		if (udb_storage_profile_load(udb_line_expiry_read, &key, udb_native_line_fields, 6, &profile) != UDB_STORAGE_OK)
		{
			udb_line_expiry_cursor_close();
			break;
		}
		char path[UDB_RECORD_PATH_MAX + 1], pattern[UDB_STORAGE_COMPONENT_MAX + 1];
		UdbPropagatorSelection selected;
		if (udb_line_path(profile, path, sizeof(path)) && udb_line_stored_pattern(profile, pattern, sizeof(pattern)))
		{
			UdbStorageRead *current = NULL;
			UdbStorageProfile *live = NULL;
			UdbStorageResult checked = udb_storage_read_open(udb_ctx->storage, &current);
			if (checked == UDB_STORAGE_OK)
				checked = udb_storage_profile_load(current, &key, udb_native_line_fields, 6, &live);
			time_t live_expiry = 0;
			int matching =
				checked == UDB_STORAGE_OK && udb_line_expiry(live, &live_expiry) == 1 && live_expiry == expiry;
			udb_storage_profile_free(&live);
			if (current && udb_storage_read_close(&current) != UDB_STORAGE_OK)
				matching = 0;
			if (!matching)
			{
				udb_storage_profile_free(&profile);
				continue;
			}
			udb_line_remove_profile(profile);
			if (!udb_propagator_policy_present(udb_ctx) ||
				(udb_select_propagator(udb_ctx, 1, &selected) && selected.is_local))
				udb_mutation_expire_local(udb_ctx, path, expiry);
			else
			{
				UdbLineExpiryPending *pending = udb_line_expiry_pending_add(udb_line_type(profile), pattern, expiry);
				if (pending && !pending->sent && udb_select_propagator(udb_ctx, 1, &selected) && selected.peer &&
					udb_send_db_to_one(selected.peer, ":%s DB %s EXP %s %" PRIu64, me.id, selected.peer->id, path,
									   (uint64_t)expiry))
					pending->sent = 1;
			}
			expired++;
		}
		udb_storage_profile_free(&profile);
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
	/* Revoke only this module's global lines, not externally replaced TKLs. */
	for (unsigned int i = 0; i < TKLISTLEN; i++)
		for (TKL *line = tklines[i], *next; line; line = next)
		{
			next = line->next;
			if (line->set_by && !strcmp(line->set_by, UDB_TKL_SET_BY) && (line->type & TKL_GLOBAL) &&
				(TKLIsServerBan(line) || TKLIsNameBan(line) || TKLIsSpamfilter(line)))
			{
				if (line->type & TKL_SHUN)
					tkl_check_local_remove_shun(line);
				tkl_del_line(line);
			}
		}
	for (unsigned int i = 0; i < TKLIPHASHLEN1; i++)
		for (unsigned int j = 0; j < TKLIPHASHLEN2; j++)
			for (TKL *line = tklines_ip_hash[i][j], *next; line; line = next)
			{
				next = line->next;
				if (line->set_by && !strcmp(line->set_by, UDB_TKL_SET_BY) && (line->type & TKL_GLOBAL) &&
					TKLIsServerBan(line))
					tkl_del_line(line);
			}
	udb_line_expiry_cursor_close();
	while (udb_line_expiry_pending)
	{
		UdbLineExpiryPending *next = udb_line_expiry_pending->next;
		udb_storage_free(udb_line_expiry_pending->pattern);
		udb_storage_free(udb_line_expiry_pending);
		udb_line_expiry_pending = next;
	}
}

static void udb_lines_init(ModuleInfo *modinfo)
{
	(void)modinfo;
	/* The shared one-second sync event invokes the lightweight K expiry sweep. */
}
