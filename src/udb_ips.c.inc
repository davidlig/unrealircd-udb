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
	return tkl && tkl->set_by && tkl->ptr.banexception && tkl->ptr.banexception->reason &&
		   !strcmp(tkl->set_by, "UDB") && !strcmp(tkl->ptr.banexception->reason, "UDB Nolines Exemption");
}

static int udb_ip_is_throttle_exempt(UdbRecord *ip_rec)
{
	UdbRecord *nolines = udb_record_find(udb_ctx, IKEY_NOLINES, ip_rec);

	/* 'c' is UnrealIRCd's connect-flood exception type. */
	return nolines && nolines->data_str && (strchr(nolines->data_str, 'c') || strchr(nolines->data_str, 'C'));
}

static int udb_ip_client_matches(Client *client, const char *ip_key)
{
	UdbIpHostState *state;

	if (!client || !MyConnect(client) || !client->user)
		return 0;
	state = udb_ip_host_md ? moddata_local_client(client, udb_ip_host_md).ptr : NULL;
	return (client->ip && !strcasecmp(client->ip, ip_key)) || (state && !strcasecmp(state->key, ip_key)) ||
		   (!state && !strcasecmp(client->user->realhost, ip_key));
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

static const char *udb_ip_explicit_vhost(Client *client)
{
	UdbRecord *nick_rec;
	UdbRecord *vhost_rec;

	if (!client || !client->user || !has_user_mode(client, 'r') || !udb_ctx || !udb_ctx->nicks)
		return NULL;
	nick_rec = udb_record_find(udb_ctx, client->name, udb_ctx->nicks);
	if (!nick_rec && strcmp(client->user->account, "*"))
		nick_rec = udb_record_find(udb_ctx, client->user->account, udb_ctx->nicks);
	vhost_rec = nick_rec ? udb_record_find(udb_ctx, NKEY_VHOST, nick_rec) : NULL;
	return vhost_rec ? vhost_rec->data_str : NULL;
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

static void udb_ip_reconcile_host(const char *ip_key, const char *host)
{
	Client *client;

	list_for_each_entry(client, &lclient_list, lclient_node)
	{
		char oldhost[HOSTLEN + 1];
		char notice[HOSTLEN + 96];

		if (!udb_ip_client_matches(client, ip_key))
			continue;
		strlcpy(oldhost, udb_ip_visible_host(client), sizeof(oldhost));
		if (host)
		{
			if (MyUser(client))
				userhost_save_current(client);
			udb_ip_apply_host(client, ip_key, host);
			if (MyUser(client) && strcmp(oldhost, udb_ip_visible_host(client)))
			{
				snprintf(notice, sizeof(notice), "*** Your explicit IP vhost is now %s", host);
				udb_ip_notify_host_change(client, notice);
			}
		}
		else
		{
			if (MyUser(client))
				userhost_save_current(client);
			udb_ip_restore_host(client, ip_key);
			if (MyUser(client) && strcmp(oldhost, udb_ip_visible_host(client)))
				udb_ip_notify_host_change(client, "*** Your explicit IP vhost has been removed");
		}
	}
}

static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey)
{
	if (!strcmp(subkey, IKEY_NOLINES))
	{
		UdbRecord *nolines = udb_record_find(udb_ctx, IKEY_NOLINES, ip_rec);
		TKL *tkl = find_tkl_banexception(TKL_EXCEPTION, "*", ip_key, 0);

		if (udb_ip_tkl_is_owned(tkl))
		{
			tkl_del_line(tkl);
			tkl = NULL;
		}
		if (!tkl && nolines && nolines->data_str && *nolines->data_str)
		{
			tkl_add_banexception(TKL_EXCEPTION, "*", ip_key, NULL, "UDB Nolines Exemption", "UDB", 0, TStime(), 0,
								 nolines->data_str, 0);
		}
	}
	else if (!strcmp(subkey, IKEY_HOST))
	{
		UdbRecord *host = udb_record_find(udb_ctx, IKEY_HOST, ip_rec);
		if (host && host->data_str && *host->data_str)
			udb_ip_reconcile_host(ip_key, host->data_str);
	}
}

static void udb_ip_remove_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey)
{
	if (!strcmp(subkey, IKEY_NOLINES))
	{
		TKL *tkl;
		if ((tkl = find_tkl_banexception(TKL_EXCEPTION, "*", ip_key, 0)) && udb_ip_tkl_is_owned(tkl))
		{
			tkl_del_line(tkl);
		}
	}
	else if (!strcmp(subkey, IKEY_HOST))
	{
		udb_ip_reconcile_host(ip_key, NULL);
		udb_ip_refresh_derived_hosts();
	}
}

static void udb_ips_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	UdbRecord *ip_rec;

	if (!ctx || !block || !rec)
		return;
	ip_rec = rec->parent == block->tree ? rec : rec->parent;
	udb_ip_apply_record(ip_rec->key, ip_rec, rec->key);
}

static void udb_ips_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	UdbRecord *ip_rec;

	if (!ctx || !block || !rec)
		return;
	ip_rec = rec->parent == block->tree ? rec : rec->parent;
	if (rec->parent == block->tree)
	{
		/* Deleting I::<key> must remove effects owned by every child. */
		UdbRecord *child;

		for (child = ip_rec->child; child; child = child->sibling)
			udb_ip_remove_record(ip_rec->key, ip_rec, child->key);
	}
	else
	{
		udb_ip_remove_record(ip_rec->key, ip_rec, rec->key);
	}
}

static int udb_hook_pre_connect(Client *client)
{
	UdbRecord *ip_rec;
	UdbRecord *sub_rec;
	int limit = 0;

	if (!client || !client->ip || !client->local)
		return 0;

	ip_rec = udb_hash_find(udb_ctx, udb_block_letter_to_index(UDB_BLOCK_IPS), client->ip);
	if (!ip_rec && client->user)
		ip_rec = udb_hash_find(udb_ctx, udb_block_letter_to_index(UDB_BLOCK_IPS), client->user->realhost);

	if (ip_rec)
	{
		sub_rec = udb_record_find(udb_ctx, IKEY_CLONES, ip_rec);
		if (sub_rec && sub_rec->data_num > 0)
			limit = (int)sub_rec->data_num;

		sub_rec = udb_record_find(udb_ctx, IKEY_HOST, ip_rec);
		if (sub_rec && sub_rec->data_str && *sub_rec->data_str && client->user)
			udb_ip_apply_host(client, ip_rec->key, sub_rec->data_str);

		if (udb_ip_is_throttle_exempt(ip_rec))
			return HOOK_CONTINUE;
	}
	udb_ip_refresh_derived_host(client);
	if (!udb_ip_base_state(client, 0) || !udb_ip_safe_host(client, udb_ip_base_state(client, 0)->host))
	{
		exit_client(client, NULL, "No safe cloak is available");
		return HOOK_DENY;
	}

	/* Fallback to global clones if no specific IP limit */
	if (limit == 0 && udb_ctx && udb_ctx->settings)
	{
		UdbRecord *g_clones = udb_record_find(udb_ctx, SKEY_CLONES, udb_ctx->settings);
		if (g_clones && g_clones->data_num > 0)
			limit = (int)g_clones->data_num;
	}

	if (limit <= 0)
		return 0;

	int clone_count = 0;
	Client *c;
	list_for_each_entry(c, &client_list, client_node)
	{
		if (!IsUser(c) || IsDead(c) || c == client)
			continue;
		if (c->ip && !strcmp(c->ip, client->ip))
			clone_count++;
	}

	if (clone_count < limit)
		return 0;

	const char *quit_msg =
		(udb_ctx && udb_ctx->quit_clones) ? udb_ctx->quit_clones : "Too many connections from your IP";

	udb_log(ULOG_INFO, "UDB_CLONES", client, "Rejecting $client.ip (Exceeds UDB clone limit of $limit)",
			log_data_integer("limit", limit));
	exit_client(client, NULL, quit_msg);
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
	UdbRecord *ip_rec;

	if (udb_ctx && udb_ctx->ips)
	{
		for (ip_rec = udb_ctx->ips->child; ip_rec; ip_rec = ip_rec->sibling)
			udb_ip_remove_record(ip_rec->key, ip_rec, IKEY_NOLINES);
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
	UdbRecord *nick, *vhost;

	if (!client || !MyUser(client) || IsDead(client))
		return;
	nick = udb_ready && udb_ctx ? udb_record_find(udb_ctx, client->name, udb_ctx->nicks) : NULL;
	vhost = nick ? udb_record_find(udb_ctx, NKEY_VHOST, nick) : NULL;
	if (!has_user_mode(client, 'r') || !nick || !udb_nick_identity_valid(client, nick) ||
		udb_record_find(udb_ctx, NKEY_SUSPEND, nick) || udb_record_find(udb_ctx, NKEY_FORBID, nick) || !vhost ||
		!udb_ip_safe_host(client, vhost->data_str))
	{
		sendnotice(client, "UDB: +t requires an authenticated nickname with an authorized N vhost");
		return;
	}
	if (IsSetHost(client) && client->user->virthost && !strcmp(client->user->virthost, vhost->data_str))
		return;
	if (UHOST_ALLOWED == UHALLOW_NEVER || (UHOST_ALLOWED == UHALLOW_NOCHANS && client->user->joined) ||
		(!ValidatePermissionsForPath("immune:vhost-flood", client, NULL, NULL, NULL) &&
		 flood_limit_exceeded(client, FLD_VHOST)))
	{
		sendnotice(client, "UDB: vhost change is currently restricted");
		return;
	}
	udb_nick_effects_apply_vhost(client, vhost);
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
