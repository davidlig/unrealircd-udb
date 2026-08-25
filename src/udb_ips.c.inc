/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: IP Management, Cloaks, Clone Limits & Virtual Hosts
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

typedef struct UdbIpHostState UdbIpHostState;
struct UdbIpHostState {
	char key[256];
	char realhost[HOSTLEN + 1];
	char cloakedhost[HOSTLEN + 1];
	char *virthost;
	long host_umodes;
	int derived_vhost;
};

static ModDataInfo *udb_ip_host_md = NULL;

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
	sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id,
	              udb_ip_visible_host(client));
	userhost_changed(client);
	if (notice)
		udb_send_service_notice(client, SKEY_IPSERV, "%s", notice);
}

static void udb_ip_host_state_free(ModData *m)
{
	UdbIpHostState *state = m->ptr;

	if (!state)
		return;
	safe_free(state->virthost);
	safe_free(state);
}

static void udb_ip_save_host_state(Client *client, const char *key)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (state)
		return;
	state = safe_alloc(sizeof(*state));
	strlcpy(state->key, key, sizeof(state->key));
	strlcpy(state->realhost, client->user->realhost, sizeof(state->realhost));
	strlcpy(state->cloakedhost, client->user->cloakedhost, sizeof(state->cloakedhost));
	safe_strdup(state->virthost, client->user->virthost);
	state->host_umodes = client->umodes & (UMODE_HIDE | UMODE_SETHOST);
	moddata_local_client(client, udb_ip_host_md).ptr = state;
}

static int udb_ip_tkl_is_owned(TKL *tkl)
{
	return tkl && tkl->set_by && tkl->ptr.banexception &&
	       tkl->ptr.banexception->reason &&
	       !strcmp(tkl->set_by, "UDB") &&
	       !strcmp(tkl->ptr.banexception->reason, "UDB Nolines Exemption");
}

static int udb_ip_is_throttle_exempt(UdbRecord *ip_rec)
{
	UdbRecord *nolines = udb_record_find(udb_ctx, IKEY_NOLINES, ip_rec);

	/* 'c' is UnrealIRCd's connect-flood exception type. */
	return nolines && nolines->data_str &&
	       (strchr(nolines->data_str, 'c') || strchr(nolines->data_str, 'C'));
}

static int udb_ip_client_matches(Client *client, const char *ip_key)
{
	UdbIpHostState *state;

	if (!client || !MyConnect(client) || !client->user)
		return 0;
	state = udb_ip_host_md ? moddata_local_client(client, udb_ip_host_md).ptr : NULL;
	return (client->ip && !strcasecmp(client->ip, ip_key)) ||
	       (state && !strcasecmp(state->key, ip_key)) ||
	       (!state && !strcasecmp(client->user->realhost, ip_key));
}

static void udb_ip_apply_host(Client *client, const char *ip_key, const char *host)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (state && state->derived_vhost)
		udb_ip_restore_host(client, state->key);
	udb_ip_save_host_state(client, ip_key);

	strlcpy(client->user->realhost, host, sizeof(client->user->realhost));
	strlcpy(client->user->cloakedhost, host, sizeof(client->user->cloakedhost));
	safe_strdup(client->user->virthost, host);
	client->umodes |= UMODE_HIDE | UMODE_SETHOST;
}

static void udb_ip_restore_host(Client *client, const char *ip_key)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (!state || strcasecmp(state->key, ip_key))
		return;
	strlcpy(client->user->realhost, state->realhost, sizeof(client->user->realhost));
	strlcpy(client->user->cloakedhost, state->cloakedhost, sizeof(client->user->cloakedhost));
	safe_strdup(client->user->virthost, state->virthost);
	client->umodes = (client->umodes & ~(UMODE_HIDE | UMODE_SETHOST)) |
	                 state->host_umodes;
	udb_ip_host_state_free(&moddata_local_client(client, udb_ip_host_md));
	moddata_local_client(client, udb_ip_host_md).ptr = NULL;
}

static const char *udb_ip_explicit_vhost(Client *client)
{
	UdbRecord *nick_rec;
	UdbRecord *vhost_rec;

	if (!client || !client->user || !has_user_mode(client, 'r') ||
	    !udb_ctx || !udb_ctx->nicks)
		return NULL;
	nick_rec = udb_record_find(udb_ctx, client->name, udb_ctx->nicks);
	if (!nick_rec && strcmp(client->user->account, "*"))
		nick_rec = udb_record_find(udb_ctx, client->user->account, udb_ctx->nicks);
	vhost_rec = nick_rec ? udb_record_find(udb_ctx, NKEY_VHOST, nick_rec) : NULL;
	return vhost_rec ? vhost_rec->data_str : NULL;
}

static int udb_ip_derive_vhost(Client *client, char *host, size_t hostlen)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;
	const char *realhost = state ? state->realhost : client->user->realhost;
	unsigned char key[32], digest[EVP_MAX_MD_SIZE];
	unsigned int digestlen;
	char input[HOSTLEN + INET6_ADDRSTRLEN + 32];
	size_t i;

	if (!udb_ctx || !udb_ctx->encryption_key || !udb_ctx->suffix || !client->ip)
		return 0;
	for (i = 0; i < sizeof(key); i++)
		sscanf(udb_ctx->encryption_key + (i * 2), "%2hhx", &key[i]);
	snprintf(input, sizeof(input), "UDB-vhost-v1|%s|%s", client->ip, realhost);
	if (!HMAC(EVP_sha256(), key, sizeof(key), (unsigned char *)input, strlen(input),
	          digest, &digestlen) ||
	    digestlen < 16)
		return 0;
	for (i = 0; i < 16; i++)
		snprintf(host + (i * 2), hostlen - (i * 2), "%02x", digest[i]);
	strlcat(host, udb_ctx->suffix, hostlen);
	return 1;
}

static int udb_ip_reapply_vhost(Client *client)
{
	UdbIpHostState *state;
	UdbRecord *ip_rec;
	UdbRecord *host_rec;
	char host[HOSTLEN + 1];
	char notice[HOSTLEN + 96];

	if (!client || !MyUser(client) || !udb_ip_host_md)
		return 0;
	state = moddata_local_client(client, udb_ip_host_md).ptr;
	if (!state)
	{
		if (!udb_ip_derive_vhost(client, host, sizeof(host)))
			return 0;
		userhost_save_current(client);
		udb_ip_save_host_state(client, client->ip);
		state = moddata_local_client(client, udb_ip_host_md).ptr;
		safe_strdup(client->user->virthost, host);
		client->umodes |= UMODE_HIDE | UMODE_SETHOST;
		state->derived_vhost = 1;
		snprintf(notice, sizeof(notice), "*** Your IP-derived vhost has been restored: %s", host);
		udb_ip_notify_host_change(client, notice);
		return 1;
	}
	if (state->derived_vhost)
	{
		if (!udb_ip_derive_vhost(client, host, sizeof(host)))
			return 0;
		userhost_save_current(client);
		safe_strdup(client->user->virthost, host);
		client->umodes |= UMODE_HIDE | UMODE_SETHOST;
		snprintf(notice, sizeof(notice), "*** Your IP-derived vhost has been restored: %s", host);
		udb_ip_notify_host_change(client, notice);
		return 1;
	}

	ip_rec = udb_hash_find(udb_ctx, udb_block_letter_to_index('I'), state->key);
	host_rec = ip_rec ? udb_record_find(udb_ctx, IKEY_HOST, ip_rec) : NULL;
	if (!host_rec || !host_rec->data_str || !*host_rec->data_str)
		return 0;
	userhost_save_current(client);
	udb_ip_apply_host(client, state->key, host_rec->data_str);
	snprintf(notice, sizeof(notice), "*** Your explicit IP vhost has been restored: %s",
	         host_rec->data_str);
	udb_ip_notify_host_change(client, notice);
	return 1;
}

static void udb_ip_refresh_derived_hosts(void)
{
	Client *client;

	if (!udb_ip_host_md)
		return;
	list_for_each_entry(client, &lclient_list, lclient_node)
	{
		UdbIpHostState *state;
		const char *explicit_vhost;
		char host[HOSTLEN + 1];

		if (!client->user || !MyConnect(client))
			continue;
		state = moddata_local_client(client, udb_ip_host_md).ptr;
		explicit_vhost = udb_ip_explicit_vhost(client);
		if (explicit_vhost)
		{
			/* A nick vhost supersedes a derived vhost without restoring over it.
			 * Keep the original state so removing the nick vhost can restore the
			 * derived vhost instead of losing the precedence relationship. */
			if ((!state || state->derived_vhost) &&
			    strcmp(udb_ip_visible_host(client), explicit_vhost))
			{
				char notice[HOSTLEN + 96];
				userhost_save_current(client);
				safe_strdup(client->user->virthost, explicit_vhost);
				client->umodes |= UMODE_HIDE | UMODE_SETHOST;
				snprintf(notice, sizeof(notice), "*** Your vhost is now %s", explicit_vhost);
				udb_ip_notify_host_change(client, notice);
			}
			continue;
		}
		if (!udb_ip_derive_vhost(client, host, sizeof(host)))
		{
			if (state && state->derived_vhost)
			{
				userhost_save_current(client);
				udb_ip_restore_host(client, state->key);
				udb_ip_notify_host_change(client, "*** Your IP-derived vhost has been removed");
			}
			continue;
		}
		if (state && !state->derived_vhost)
			continue; /* I::host remains a stronger, explicit IP override. */
		if (state && state->derived_vhost && !strcmp(client->user->virthost, host))
			continue;
		userhost_save_current(client);
		udb_ip_save_host_state(client, client->ip);
		state = moddata_local_client(client, udb_ip_host_md).ptr;
		safe_strdup(client->user->virthost, host);
		client->umodes |= UMODE_HIDE | UMODE_SETHOST;
		state->derived_vhost = 1;
		{
			char notice[HOSTLEN + 96];
			snprintf(notice, sizeof(notice), "*** Your IP-derived vhost is now %s", host);
			udb_ip_notify_host_change(client, notice);
		}
	}
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
		} else
		{
			if (MyUser(client))
				userhost_save_current(client);
			udb_ip_restore_host(client, ip_key);
			if (MyUser(client) && strcmp(oldhost, udb_ip_visible_host(client)))
				udb_ip_notify_host_change(client, "*** Your explicit IP vhost has been removed");
		}
	}
}

static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey, int is_new)
{
	(void)is_new;
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
			tkl_add_banexception(TKL_EXCEPTION, "*", ip_key, NULL,
			                     "UDB Nolines Exemption", "UDB", 0, TStime(), 0,
			                     nolines->data_str, 0);
		}
	} else if (!strcmp(subkey, IKEY_HOST))
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
		if ((tkl = find_tkl_banexception(TKL_EXCEPTION, "*", ip_key, 0)) &&
		    udb_ip_tkl_is_owned(tkl))
		{
			tkl_del_line(tkl);
		}
	} else if (!strcmp(subkey, IKEY_HOST))
	{
		udb_ip_reconcile_host(ip_key, NULL);
		udb_ip_refresh_derived_hosts();
	}
}

static void udb_ips_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec,
                                 int is_new)
{
	UdbRecord *ip_rec;

	if (!ctx || !block || !rec)
		return;
	ip_rec = rec->parent == block->tree ? rec : rec->parent;
	udb_ip_apply_record(ip_rec->key, ip_rec, rec->key, is_new);
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
	} else
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

	/* Lookup IP or Host in UDB */
	ip_rec = udb_hash_find(udb_ctx, udb_block_letter_to_index('I'), client->ip);
	if (!ip_rec && client->user)
		ip_rec = udb_hash_find(udb_ctx, udb_block_letter_to_index('I'), client->user->realhost);

	if (ip_rec)
	{
		/* Apply Clone Limit */
		sub_rec = udb_record_find(udb_ctx, IKEY_CLONES, ip_rec);
		if (sub_rec && sub_rec->data_num > 0)
			limit = (int)sub_rec->data_num;

		/* Apply Host Override */
		sub_rec = udb_record_find(udb_ctx, IKEY_HOST, ip_rec);
		if (sub_rec && sub_rec->data_str && *sub_rec->data_str && client->user)
			udb_ip_apply_host(client, ip_rec->key, sub_rec->data_str);

		if (udb_ip_is_throttle_exempt(ip_rec))
			return HOOK_CONTINUE;
	}
	udb_ip_refresh_derived_hosts();

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
	list_for_each_entry(c, &lclient_list, lclient_node)
	{
		if (c->ip && !strcmp(c->ip, client->ip))
			clone_count++;
	}

	if (clone_count < limit)
		return 0;

	const char *quit_msg = (udb_ctx && udb_ctx->quit_clones) ? udb_ctx->quit_clones : "Too many connections from your IP";

	udb_log(ULOG_INFO, "UDB_CLONES", client,
	        "Rejecting $client.ip (Exceeds UDB clone limit of $limit)",
	        log_data_integer("limit", limit));
	exit_client(client, NULL, quit_msg);
	return HOOK_DENY;
}

static int udb_hook_local_quit(Client *client, MessageTag *mtags, const char *comment)
{
	UdbIpHostState *state;

	(void)mtags;
	(void)comment;
	if (!client || !client->user || !udb_ip_host_md)
		return HOOK_CONTINUE;
	state = moddata_local_client(client, udb_ip_host_md).ptr;
	if (state)
		udb_ip_restore_host(client, state->key);
	return HOOK_CONTINUE;
}

static void udb_ips_shutdown(void)
{
	Client *client;
	UdbRecord *ip_rec;

	if (udb_ip_host_md)
	{
		list_for_each_entry(client, &lclient_list, lclient_node)
		{
			UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;
			if (state && client->user)
				udb_ip_restore_host(client, state->key);
		}
	}
	if (udb_ctx && udb_ctx->ips)
	{
		for (ip_rec = udb_ctx->ips->child; ip_rec; ip_rec = ip_rec->sibling)
			udb_ip_remove_record(ip_rec->key, ip_rec, IKEY_NOLINES);
	}
}

static void udb_ips_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_ip_host_state";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_ip_host_state_free;
	udb_ip_host_md = ModDataAdd(modinfo->handle, mreq);
	if (!udb_ip_host_md)
		return;
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, udb_hook_pre_connect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, udb_hook_local_quit);
}
