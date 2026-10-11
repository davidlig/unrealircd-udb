/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Clone Limits, Protected Base Cloaks & IP Host Overlays
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

typedef struct UdbIpHostState UdbIpHostState;
/* Track the I::host overlay UDB applied; revocation must preserve a later
 * external host replacement and otherwise recover the protected base. */
struct UdbIpHostState
{
	char key[256];
	char applied_host[HOSTLEN + 1];
};

static ModDataInfo *udb_ip_host_md = NULL;
static ModDataInfo *udb_base_cloak_md = NULL;

typedef struct UdbBaseCloak
{
	Client *client; /* Bound only to the client that owns this ModData. */
	char host[HOSTLEN + 1];
} UdbBaseCloak;

static void udb_ip_restore_host(Client *client, const char *ip_key);

static const char *udb_ip_visible_host(Client *client)
{
	if (client && client->user && IsHidden(client) && client->user->virthost)
		return client->user->virthost;
	return client && client->user ? client->user->realhost : "";
}

static void udb_ip_notify_host_change(Client *client, const char *notice)
{
	if (!MyUser(client))
		return;
	sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, udb_ip_visible_host(client));
	/* Native SETHOST implicitly adds +t on receivers. Base-host transitions
	 * must explicitly leave the custom-host flag unset there too. */
	if (!IsSetHost(client))
		sendto_server(client, 0, 0, NULL, ":%s UMODE2 -t", client->id);
	userhost_changed(client);
	if (notice)
		udb_send_service_notice(client, SKEY_IPSERV, "%s", notice);
}

static void udb_ip_host_state_free(ModData *m)
{
	safe_free(m->ptr);
}

static void udb_ip_save_host_state(Client *client, const char *key)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (state)
		return;
	state = safe_alloc(sizeof(*state));
	strlcpy(state->key, key, sizeof(state->key));
	moddata_local_client(client, udb_ip_host_md).ptr = state;
}

static int udb_ip_safe_host(Client *client, const char *host)
{
	unsigned char a[16], b[16];
	const int families[] = {AF_INET, AF_INET6};
	size_t i;

	if (!udb_vhost_valid(host))
		return 0;
	if (!client || !client->user)
		return 1;
	if (!strcasecmp(host, client->user->realhost))
		return 0;
	if (!client->ip)
		return 1;
	if (!strcasecmp(host, client->ip))
		return 0;
	for (i = 0; i < sizeof(families) / sizeof(families[0]); i++)
	{
		if (inet_pton(families[i], host, a) == 1 && inet_pton(families[i], client->ip, b) == 1 &&
			!memcmp(a, b, families[i] == AF_INET ? 4 : 16))
			return 0;
	}
	return 1;
}

static UdbBaseCloak *udb_ip_base_state(Client *client, int create)
{
	UdbBaseCloak *base;

	if (!client || !udb_base_cloak_md)
		return NULL;
	base = moddata_client(client, udb_base_cloak_md).ptr;
	if (!base && create)
		base = moddata_client(client, udb_base_cloak_md).ptr = safe_alloc(sizeof(*base));
	if (base)
		base->client = client;
	return base;
}

static int udb_ip_install_base(Client *client)
{
	UdbBaseCloak *base = udb_ip_base_state(client, 0);

	if (!base || !valid_host(base->host, 0) || !udb_ip_safe_host(client, base->host))
		return 0;
	strlcpy(client->user->cloakedhost, base->host, sizeof(client->user->cloakedhost));
	/* Existing users may have deliberately chosen -x. Keep their base
	 * available for +x without cancelling that choice during refresh/burst. */
	if (!IsUser(client))
		client->umodes |= UMODE_HIDE;
	if (!IsSetHost(client) || !udb_ip_safe_host(client, client->user->virthost))
	{
		safe_strdup(client->user->virthost, base->host);
		client->umodes &= ~UMODE_SETHOST;
	}
	return 1;
}

static const char *udb_ip_base_serialize(ModData *m)
{
	UdbBaseCloak *base = m->ptr;
	return base && valid_host(base->host, 0) && udb_ip_safe_host(base->client, base->host) ? base->host : NULL;
}

static void udb_ip_base_unserialize(const char *host, ModData *m)
{
	UdbBaseCloak *base = m->ptr;
	char oldhost[HOSTLEN + 1];
	Client *client = base ? base->client : NULL;

	/* Invalid/empty updates never erase the last accepted protected base. */
	if (!udb_ip_safe_host(client, host) || !valid_host(host, 0))
		return;
	if (!base)
		base = m->ptr = safe_alloc(sizeof(*base));
	if (client && client->user)
	{
		strlcpy(oldhost, udb_ip_visible_host(client), sizeof(oldhost));
		userhost_save_current(client);
	}
	strlcpy(base->host, host, sizeof(base->host));
	if (client && client->user && udb_ip_install_base(client) && strcmp(oldhost, udb_ip_visible_host(client)))
		userhost_changed(client);
}

static int udb_ip_tkl_is_owned(TKL *tkl)
{
	return tkl && TKLIsBanException(tkl) && tkl->set_by && tkl->ptr.banexception && tkl->ptr.banexception->reason &&
		   !strcmp(tkl->set_by, "UDB") && !strcmp(tkl->ptr.banexception->reason, "UDB Nolines Exemption");
}

static void udb_ip_apply_host(Client *client, const char *ip_key, const char *host)
{
	UdbIpHostState *state;

	if (!udb_ip_safe_host(client, host))
		return;
	udb_ip_save_host_state(client, ip_key);
	state = moddata_local_client(client, udb_ip_host_md).ptr;
	strlcpy(state->applied_host, host, sizeof(state->applied_host));
	safe_strdup(client->user->virthost, host);
	client->umodes |= UMODE_HIDE | UMODE_SETHOST;
}

static void udb_ip_restore_host(Client *client, const char *ip_key)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (!state || strcasecmp(state->key, ip_key))
		return;
	/* Release only this I::host overlay, never an external replacement. */
	if (client->user && client->user->virthost && !strcmp(client->user->virthost, state->applied_host))
	{
		UdbBaseCloak *base = udb_ip_base_state(client, 0);
		if (base && udb_ip_safe_host(client, base->host))
		{
			safe_strdup(client->user->virthost, base->host);
			client->umodes = (client->umodes | UMODE_HIDE) & ~UMODE_SETHOST;
		}
	}
	udb_ip_host_state_free(&moddata_local_client(client, udb_ip_host_md));
	moddata_local_client(client, udb_ip_host_md).ptr = NULL;
}

static int udb_ip_derive_vhost(Client *client, char *host, size_t hostlen)
{
	const char *realhost = client->user->realhost;
	unsigned char key[32], digest[EVP_MAX_MD_SIZE];
	unsigned int digestlen;
	char input[HOSTLEN + INET6_ADDRSTRLEN + 32];
	size_t i;

	if (!udb_ctx || !udb_ctx->encryption_key || !udb_ctx->suffix || !client->ip)
		return 0;
	for (i = 0; i < sizeof(key); i++)
		sscanf(udb_ctx->encryption_key + (i * 2), "%2hhx", &key[i]);
	snprintf(input, sizeof(input), "UDB-vhost-v1|%s|%s", client->ip, realhost);
	if (!HMAC(EVP_sha256(), key, sizeof(key), (unsigned char *)input, strlen(input), digest, &digestlen) ||
		digestlen < 16)
		return 0;
	for (i = 0; i < 16; i++)
		snprintf(host + (i * 2), hostlen - (i * 2), "%02x", digest[i]);
	strlcat(host, udb_ctx->suffix, hostlen);
	return 1;
}

static int udb_ip_reapply_vhost(Client *client)
{
	UdbBaseCloak *base;
	if (!client || !MyUser(client))
		return 0;
	base = udb_ip_base_state(client, 0);
	if (!base || !valid_host(base->host, 0) || !udb_ip_safe_host(client, base->host))
		return 0;
	userhost_save_current(client);
	safe_strdup(client->user->virthost, base->host);
	client->umodes = (client->umodes | UMODE_HIDE) & ~UMODE_SETHOST;
	udb_ip_notify_host_change(client, "*** Your base cloak has been restored");
	return 1;
}

static void udb_ip_refresh_derived_host(Client *client)
{
	UdbBaseCloak *base;
	char host[HOSTLEN + 1], oldhost[HOSTLEN + 1];
	int changed;

	if (!udb_base_cloak_md || !client || !client->user || !MyConnect(client))
		return;
	base = udb_ip_base_state(client, 1);
	if (!udb_ip_derive_vhost(client, host, sizeof(host)))
	{
		if (udb_ip_safe_host(client, base->host))
			strlcpy(host, base->host, sizeof(host));
		else
			make_cloakedhost(client, client->user->realhost, host, sizeof(host));
	}
	if (!udb_ip_safe_host(client, host))
		return;
	changed = strcmp(base->host, host);
	strlcpy(oldhost, udb_ip_visible_host(client), sizeof(oldhost));
	if (MyUser(client))
		userhost_save_current(client);
	strlcpy(base->host, host, sizeof(base->host));
	udb_ip_install_base(client);
	if (MyUser(client) && changed)
		broadcast_md_client_cmd(NULL, &me, client, "udb_base_cloak", base->host);
	if (MyUser(client) && strcmp(oldhost, udb_ip_visible_host(client)))
		udb_ip_notify_host_change(client, "*** Your base cloak is now active");
}

static void udb_ip_refresh_derived_hosts(void)
{
	Client *client;

	if (!udb_ip_host_md)
		return;
	list_for_each_entry(client, &lclient_list, lclient_node) udb_ip_refresh_derived_host(client);
}

static void udb_ip_set_nolines(const char *key, const char *value)
{
	TKL *tkl = find_tkl_banexception(TKL_EXCEPTION, "*", key, 0);
	if (tkl && !udb_ip_tkl_is_owned(tkl))
		return; /* Never replace an externally owned exemption. */
	if (tkl && value && tkl->ptr.banexception->bantypes && !strcmp(tkl->ptr.banexception->bantypes, value))
		return;
	if (tkl)
		tkl_del_line(tkl);
	if (value && *value)
		tkl_add_banexception(TKL_EXCEPTION, "*", key, NULL, "UDB Nolines Exemption", "UDB", 0, TStime(), 0, value, 0);
}

/* Visit exactly one nolines scalar per key, with no resident mask inventory. */
static UdbStorageResult udb_ip_nolines_scan(UdbStorageRead *source, UdbStorageRead *replacement, int remove_missing)
{
	UdbStorageBlockIterator *iterator = NULL;
	UdbStorageField *field = NULL;
	UdbStorageRecord record;
	UdbStorageResult result = udb_storage_read_iterator(source, UDB_BLOCK_IPS, NULL, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	if (remove_missing)
	{
		field = udb_storage_alloc(&source->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*field));
		if (!field)
		{
			result = UDB_STORAGE_LIMIT;
			goto finish;
		}
	}
	while ((result = udb_storage_block_iterator_next(iterator, &record)) == UDB_STORAGE_OK)
	{
		const UdbStorageRow *row = &record.row;
		if (row->depth != 2 || row->components[1].length != strlen(IKEY_NOLINES) ||
			memcmp(row->components[1].data, IKEY_NOLINES, row->components[1].length))
			continue;
		char key[HOSTLEN + 1], value[UDB_STORAGE_VALUE_MAX + 1];
		if (row->kind != UDB_STORAGE_VALUE_STRING || !udb_native_span_string(row->components[0], key, sizeof(key)) ||
			!udb_native_span_string(row->value, value, sizeof(value)))
		{
			result = UDB_STORAGE_CORRUPT;
			break;
		}
		if (remove_missing)
		{
			result = udb_storage_read_field(replacement, row, field);
			if (result < UDB_STORAGE_NOT_FOUND)
				break;
			if (result == UDB_STORAGE_NOT_FOUND)
				udb_ip_set_nolines(key, NULL);
		}
		else
			udb_ip_set_nolines(key, value);
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = UDB_STORAGE_OK;
finish:
	udb_storage_free(field);
	if (udb_storage_block_iterator_close(&iterator) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_ip_client_profile(UdbStorageRead *read, Client *client, UdbStorageProfile **out)
{
	static const char *const fields[] = {IKEY_CLONES, IKEY_HOST, IKEY_NOLINES};
	*out = NULL;
	if (!client || !client->ip || !client->user)
		return UDB_STORAGE_NOT_FOUND;
	UdbStorageRow entity = {0};
	entity.block = UDB_BLOCK_IPS;
	entity.depth = 1;
	entity.components[0].data = (const unsigned char *)client->ip;
	entity.components[0].length = (uint32_t)strlen(client->ip);
	UdbStorageResult result = udb_storage_profile_load(read, &entity, fields, 3, out);
	if (result == UDB_STORAGE_NOT_FOUND && client->user->realhost[0])
	{
		entity.components[0].data = (const unsigned char *)client->user->realhost;
		entity.components[0].length = (uint32_t)strlen(client->user->realhost);
		result = udb_storage_profile_load(read, &entity, fields, 3, out);
	}
	return result;
}

static UdbStorageResult udb_ip_reconcile_scope(UdbStorageRead *before, UdbStorageRead *after, const char *entity)
{
	UdbStorageResult result = UDB_STORAGE_OK;
	if (entity)
	{
		UdbStorageField *field = udb_storage_alloc(&after->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*field));
		if (!field)
			return UDB_STORAGE_LIMIT;
		result = udb_native_read_scalar(after, UDB_BLOCK_IPS, entity, IKEY_NOLINES, field);
		if (result == UDB_STORAGE_OK && field->kind != UDB_STORAGE_VALUE_STRING)
			result = UDB_STORAGE_CORRUPT;
		if (result >= UDB_STORAGE_NOT_FOUND)
		{
			udb_ip_set_nolines(entity, result == UDB_STORAGE_OK ? field->string : NULL);
			result = UDB_STORAGE_OK;
		}
		udb_storage_free(field);
	}
	else
		result = udb_ip_nolines_scan(before, after, 1);
	if (result == UDB_STORAGE_OK && !entity)
		result = udb_ip_nolines_scan(after, NULL, 0);
	if (result != UDB_STORAGE_OK)
		return result;
	Client *client, *next;
	list_for_each_entry_safe(client, next, &lclient_list, lclient_node)
	{
		if (!MyUser(client) || (entity && (!client->ip || strcasecmp(entity, client->ip)) &&
								(!client->user || strcasecmp(entity, client->user->realhost))))
			continue;
		UdbStorageProfile *old = NULL, *current = NULL;
		UdbStorageResult prior = udb_ip_client_profile(before, client, &old);
		UdbStorageResult loaded = udb_ip_client_profile(after, client, &current);
		if (prior < UDB_STORAGE_NOT_FOUND || loaded < UDB_STORAGE_NOT_FOUND)
		{
			result = prior < UDB_STORAGE_NOT_FOUND ? prior : loaded;
			udb_storage_profile_free(&old);
			udb_storage_profile_free(&current);
			continue;
		}
		const UdbStorageField *oldhost = old && old->fields[1].present ? old->fields + 1 : NULL;
		const UdbStorageField *host = current && current->fields[1].present ? current->fields + 1 : NULL;
		int changed = (oldhost != NULL) != (host != NULL) ||
					  (oldhost && host &&
					   (oldhost->kind != host->kind || strcmp(oldhost->string, host->string) ||
						strcmp(old->entity, current->entity)));
		if (changed)
		{
			char visible[HOSTLEN + 1];
			strlcpy(visible, udb_ip_visible_host(client), sizeof(visible));
			userhost_save_current(client);
			UdbIpHostState *owned = udb_ip_host_md ? moddata_local_client(client, udb_ip_host_md).ptr : NULL;
			if (owned)
			{
				char key[HOSTLEN + 1];
				strlcpy(key, owned->key, sizeof(key));
				udb_ip_restore_host(client, key);
			}
			if (host && host->kind == UDB_STORAGE_VALUE_STRING)
				udb_ip_apply_host(client, current->entity, host->string);
			else if (host)
				result = UDB_STORAGE_CORRUPT;
			if (strcmp(visible, udb_ip_visible_host(client)))
				udb_ip_notify_host_change(client, "*** Your IP host policy has changed");
		}
		udb_storage_profile_free(&old);
		udb_storage_profile_free(&current);
	}
	return result;
}

static UdbStorageResult udb_ip_reconcile_generations(UdbStorageRead *before, UdbStorageRead *after)
{
	return udb_ip_reconcile_scope(before, after, NULL);
}

static int udb_hook_pre_connect(Client *client)
{
	static const char *const fields[] = {IKEY_CLONES, IKEY_HOST, IKEY_NOLINES};
	UdbStorageRead *read = NULL;
	UdbStorageProfile *profile = NULL;
	UdbStorageField *globals = NULL;
	UdbStorageResult result;
	int limit = 0, exempt = 0;
	char host[HOSTLEN + 1] = "", ip_key[HOSTLEN + 1] = "";
	char quit[UDB_STORAGE_VALUE_MAX + 1] = "Too many connections from your IP";

	if (!client || !client->ip || !client->local)
		return HOOK_CONTINUE;
	if (!udb_ctx || !udb_ctx->storage || !udb_ready)
		goto unavailable;
	result = udb_storage_read_open(udb_ctx->storage, &read);
	if (result != UDB_STORAGE_OK)
		goto unavailable;
	UdbStorageRow entity = {0};
	entity.block = 'I';
	entity.depth = 1;
	entity.components[0].data = (const unsigned char *)client->ip;
	entity.components[0].length = (uint32_t)strlen(client->ip);
	result = udb_storage_profile_load(read, &entity, fields, 3, &profile);
	if (result == UDB_STORAGE_NOT_FOUND && client->user && client->user->realhost[0])
	{
		entity.components[0].data = (const unsigned char *)client->user->realhost;
		entity.components[0].length = (uint32_t)strlen(client->user->realhost);
		result = udb_storage_profile_load(read, &entity, fields, 3, &profile);
	}
	if (result < UDB_STORAGE_NOT_FOUND)
		goto unavailable;
	if (profile)
	{
		UdbStorageField *clones = profile->fields, *vhost = profile->fields + 1, *nolines = profile->fields + 2;
		if ((clones->present && (clones->kind != UDB_STORAGE_VALUE_NUMBER || clones->number > INT_MAX)) ||
			(vhost->present && (vhost->kind != UDB_STORAGE_VALUE_STRING || vhost->length > HOSTLEN)) ||
			(nolines->present && nolines->kind != UDB_STORAGE_VALUE_STRING) || entity.components[0].length > HOSTLEN)
			goto unavailable;
		limit = clones->present ? (int)clones->number : 0;
		if (vhost->present)
			memcpy(host, vhost->string, vhost->length + 1);
		memcpy(ip_key, entity.components[0].data, entity.components[0].length);
		ip_key[entity.components[0].length] = 0;
		exempt = nolines->present && (strchr(nolines->string, 'c') || strchr(nolines->string, 'C'));
	}
	if (!exempt)
	{
		globals = udb_storage_alloc(&read->engine->budget, UDB_STORAGE_MEMORY_AUX, 2 * sizeof(*globals));
		if (!globals)
			goto unavailable;
		if (!limit)
		{
			result = udb_native_read_scalar(read, 'S', NULL, SKEY_CLONES, globals);
			if (result < UDB_STORAGE_NOT_FOUND ||
				(result == UDB_STORAGE_OK &&
				 (globals[0].kind != UDB_STORAGE_VALUE_NUMBER || globals[0].number > INT_MAX)))
				goto unavailable;
			limit = result == UDB_STORAGE_OK ? (int)globals[0].number : 0;
		}
		if (limit)
		{
			result = udb_native_read_scalar(read, 'S', NULL, SKEY_QUIT_CLONES, globals + 1);
			if (result < UDB_STORAGE_NOT_FOUND ||
				(result == UDB_STORAGE_OK && globals[1].kind != UDB_STORAGE_VALUE_STRING))
				goto unavailable;
			if (result == UDB_STORAGE_OK)
				memcpy(quit, globals[1].string, globals[1].length + 1);
		}
	}
	udb_storage_profile_free(&profile);
	udb_storage_free(globals);
	globals = NULL;
	if (udb_storage_read_close(&read) != UDB_STORAGE_OK)
		goto unavailable;
	/* Privacy is mandatory even for a connect-flood-exempt entity. */
	if (*host && client->user)
		udb_ip_apply_host(client, ip_key, host);
	udb_ip_refresh_derived_host(client);
	if (!udb_ip_base_state(client, 0) || !udb_ip_safe_host(client, udb_ip_base_state(client, 0)->host))
	{
		exit_client(client, NULL, "No safe cloak is available");
		return HOOK_DENY;
	}
	if (exempt || !limit)
		return HOOK_CONTINUE;
	int clone_count = 0;
	Client *other;
	list_for_each_entry(other, &client_list, client_node)
	{
		if (IsUser(other) && !IsDead(other) && other != client && other->ip && !strcmp(other->ip, client->ip))
		{
			/* Stop at the validated INT_MAX-bounded threshold. */
			if (++clone_count >= limit)
				break;
		}
	}
	if (clone_count < limit)
		return HOOK_CONTINUE;
	udb_log(ULOG_INFO, "UDB_CLONES", client, "Rejecting $client.ip (Exceeds UDB clone limit of $limit)",
			log_data_integer("limit", limit));
	exit_client(client, NULL, quit);
	return HOOK_DENY;
unavailable:
	udb_storage_profile_free(&profile);
	udb_storage_free(globals);
	if (read)
		udb_storage_read_close(&read);
	exit_client(client, NULL, "UDB policy data unavailable");
	return HOOK_DENY;
}

static int udb_hook_local_quit(Client *client, MessageTag *mtags, const char *comment)
{
	(void)client;
	(void)mtags;
	(void)comment;
	/* Client ModData callbacks free state after the QUIT has been serialized. */
	return HOOK_CONTINUE;
}

static void udb_ips_shutdown(void)
{
	TKL *tkl, *next;
	for (unsigned int i = 0; i < TKLISTLEN; i++)
		for (tkl = tklines[i]; tkl; tkl = next)
		{
			next = tkl->next;
			if (udb_ip_tkl_is_owned(tkl))
				tkl_del_line(tkl);
		}
	for (unsigned int i = 0; i < TKLIPHASHLEN1; i++)
		for (unsigned int j = 0; j < TKLIPHASHLEN2; j++)
			for (tkl = tklines_ip_hash[i][j]; tkl; tkl = next)
			{
				next = tkl->next;
				if (udb_ip_tkl_is_owned(tkl))
					tkl_del_line(tkl);
			}
	Client *client, *following;
	list_for_each_entry_safe(client, following, &lclient_list, lclient_node)
	{
		UdbIpHostState *owned = udb_ip_host_md ? moddata_local_client(client, udb_ip_host_md).ptr : NULL;
		if (!owned || !client->user)
			continue;
		char key[HOSTLEN + 1], visible[HOSTLEN + 1];
		strlcpy(key, owned->key, sizeof(key));
		strlcpy(visible, udb_ip_visible_host(client), sizeof(visible));
		if (MyUser(client))
			userhost_save_current(client);
		udb_ip_restore_host(client, key);
		if (strcmp(visible, udb_ip_visible_host(client)))
			udb_ip_notify_host_change(client, NULL);
	}
}

static int udb_ip_welcome(Client *client, int after_numeric)
{
	if (!after_numeric)
		udb_ip_install_base(client);
	return 0;
}

static int udb_ip_remote_connect(Client *client)
{
	/* EARLY metadata has been extracted before this hook. */
	udb_ip_install_base(client);
	return 0;
}

static int udb_ip_restore_requested(const char *modes)
{
	int add = 1, restore = 0;
	const char *p;
	if (BadPtr(modes))
		return 0;
	for (p = modes; *p; p++)
	{
		if (*p == '+')
			add = 1;
		else if (*p == '-')
			add = 0;
		else if (*p == 't')
			restore = add;
	}
	return restore;
}

CMD_OVERRIDE_FUNC(udb_override_safe_sethost)
{
	if (IsUser(client) && parc > 1 && !udb_ip_safe_host(client, parv[1]))
		return;
	CALL_NEXT_COMMAND_OVERRIDE();
}

CMD_OVERRIDE_FUNC(udb_override_safe_chghost)
{
	Client *target = parc > 2 ? find_user(parv[1], NULL) : NULL;
	if (target && !udb_ip_safe_host(target, parv[2]))
		return;
	CALL_NEXT_COMMAND_OVERRIDE();
}

static void udb_ip_restore_nick_vhost(Client *client)
{
	UdbStorageProfile *nick = NULL;
	if (!client || !MyUser(client) || IsDead(client))
		return;
	UdbStorageResult result = udb_native_nick_load(client->name, &nick);
	if (result < UDB_STORAGE_NOT_FOUND)
	{
		sendnotice(client, "UDB: nickname policy data unavailable");
		return;
	}
	UdbStorageField *vhost = udb_nick_field(nick, NKEY_VHOST);
	if (!has_user_mode(client, 'r') || !nick || !udb_nick_identity_valid(client, nick) ||
		udb_nick_field(nick, NKEY_SUSPEND) || udb_nick_field(nick, NKEY_FORBID) || !vhost ||
		!udb_ip_safe_host(client, vhost->string))
	{
		sendnotice(client, "UDB: +t requires an authenticated nickname with an authorized N vhost");
		goto cleanup;
	}
	if (IsSetHost(client) && client->user->virthost && !strcmp(client->user->virthost, vhost->string))
		goto cleanup;
	if (UHOST_ALLOWED == UHALLOW_NEVER || (UHOST_ALLOWED == UHALLOW_NOCHANS && client->user->joined) ||
		(!ValidatePermissionsForPath("immune:vhost-flood", client, NULL, NULL, NULL) &&
		 flood_limit_exceeded(client, FLD_VHOST)))
	{
		sendnotice(client, "UDB: vhost change is currently restricted");
		goto cleanup;
	}
	udb_nick_effects_apply_vhost(client, vhost);
cleanup:
	udb_storage_profile_free(&nick);
}

CMD_OVERRIDE_FUNC(udb_override_host_mode)
{
	Client *target = parc > 2 ? find_user(parv[1], NULL) : NULL;
	int was_hidden = IsHidden(client);
	int restore = udb_ip_restore_requested(parc > 2 ? parv[2] : NULL);
	CALL_NEXT_COMMAND_OVERRIDE();
	/* Native +x emits SETHOST, which adds +t on remote receivers. */
	if (target == client && MyUser(client) && !was_hidden && IsHidden(client) && !IsSetHost(client))
		sendto_server(NULL, 0, 0, NULL, ":%s UMODE2 -t", client->id);
	if (target == client && restore && MyUser(client))
		udb_ip_restore_nick_vhost(client);
}

CMD_OVERRIDE_FUNC(udb_override_host_umode2)
{
	int was_hidden = IsHidden(client);
	int restore = udb_ip_restore_requested(parc > 1 ? parv[1] : NULL);
	CALL_NEXT_COMMAND_OVERRIDE();
	if (MyUser(client) && !was_hidden && IsHidden(client) && !IsSetHost(client))
		sendto_server(NULL, 0, 0, NULL, ":%s UMODE2 -t", client->id);
	if (restore && MyUser(client))
		udb_ip_restore_nick_vhost(client);
}

CMD_OVERRIDE_FUNC(udb_override_base_md)
{
	if (parc > 3 && !strcmp(parv[1], "client") && !strcmp(parv[3], "udb_base_cloak"))
	{
		Client *target = find_user(parv[2], NULL);
		/* Native MD blocks remote writes to local clients, but does not enforce
		 * the origin of writes to remote clients. Protect that boundary here. */
		if (!target || target->uplink != client || parc < 5 || !udb_ip_safe_host(target, parv[4]) ||
			!valid_host(parv[4], 0))
			return;
	}
	CALL_NEXT_COMMAND_OVERRIDE();
}

static int udb_ips_load(ModuleInfo *modinfo)
{
	Client *client;
	const char *commands[] = {"MODE", "UMODE2", "MD", "SETHOST", "CHGHOST"};
	OverrideCmdFunc handlers[] = {udb_override_host_mode, udb_override_host_umode2, udb_override_base_md,
								  udb_override_safe_sethost, udb_override_safe_chghost};

	size_t i;
	for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
	{
		if (!CommandExists(commands[i]))
		{
			if (i == 0 || i == 2)
				return 0;
			continue;
		}
		if (!CommandOverrideAdd(modinfo->handle, commands[i], -100, handlers[i]))
			return 0;
	}
	udb_ip_refresh_derived_hosts();
	list_for_each_entry(client, &client_list, client_node)
	{
		if (IsUser(client) && !MyUser(client))
			udb_ip_install_base(client);
	}
	return 1;
}

static int udb_ips_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_ip_host_state";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_ip_host_state_free;
	udb_ip_host_md = ModDataAdd(modinfo->handle, mreq);
	if (!udb_ip_host_md)
		return 0;
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_base_cloak";
	mreq.type = MODDATATYPE_CLIENT;
	mreq.sync = MODDATA_SYNC_EARLY;
	mreq.free = udb_ip_host_state_free;
	mreq.serialize = udb_ip_base_serialize;
	mreq.unserialize = udb_ip_base_unserialize;
	udb_base_cloak_md = ModDataAdd(modinfo->handle, mreq);
	if (!udb_base_cloak_md)
		return 0;
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, udb_hook_pre_connect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, udb_hook_local_quit);
	HookAdd(modinfo->handle, HOOKTYPE_WELCOME, -100, udb_ip_welcome);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_CONNECT, -100, udb_ip_remote_connect);
	return 1;
}
