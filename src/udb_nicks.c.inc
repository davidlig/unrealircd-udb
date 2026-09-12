/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Nick Registration, SHA-256 Identification & Forced Nick Migrations
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

#include <openssl/evp.h>

typedef struct UdbNickPasswordCache UdbNickPasswordCache;
struct UdbNickPasswordCache
{
	char nick[NICKLEN + 1];
	char *pass;
	int validated;
};

/* Active UDB identity marker. It exists only while an authenticated profile is
 * bound to the current nick; it is destroyed by suspend, by policy changes and
 * by leaving the nick, and is never reconstructed after the fact. */
typedef struct UdbNickIdentity UdbNickIdentity;
struct UdbNickIdentity
{
	char nick[NICKLEN + 1];
	char policy_digest[65];
};

/* Preflight password/access success for a destination nick. It never grants
 * identity: only a confirmed nick change promotes it to active identity. */
typedef struct UdbNickPendingAuth UdbNickPendingAuth;
struct UdbNickPendingAuth
{
	char nick[NICKLEN + 1];
	char policy_digest[65];
};

static ModDataInfo *udb_nick_password_cache_md = NULL;
static ModDataInfo *udb_nick_identity_md = NULL;
static ModDataInfo *udb_nick_pending_auth_md = NULL;
static ModDataInfo *udb_nick_oper_owned_md = NULL;
/* Only set during a synchronous N-tree replacement; never retained per client. */
static UdbRecord *udb_nick_replacement_tree = NULL;

static void udb_nick_password_cache_entry_free(UdbNickPasswordCache *cache)
{
	if (!cache)
		return;
	safe_free(cache->pass);
	safe_free(cache);
}

static void udb_nick_password_cache_free(ModData *m)
{
	udb_nick_password_cache_entry_free(m->ptr);
	m->ptr = NULL;
}

static void udb_nick_password_cache_clear(Client *client)
{
	UdbNickPasswordCache *cache;

	if (!client || !udb_nick_password_cache_md)
		return;
	cache = moddata_local_client(client, udb_nick_password_cache_md).ptr;
	if (cache)
	{
		udb_nick_password_cache_entry_free(cache);
		moddata_local_client(client, udb_nick_password_cache_md).ptr = NULL;
	}
}

/* One-shot /NICK credential: plaintext lives only until CAN_USE_NICK consumes
 * it, and never becomes session state. */
static void udb_nick_password_cache_set(Client *client, const char *nick, const char *pass, int validated)
{
	UdbNickPasswordCache *cache;

	if (!client || !nick || !pass || !udb_nick_password_cache_md)
		return;
	udb_nick_password_cache_clear(client);
	cache = safe_alloc(sizeof(*cache));
	strlcpy(cache->nick, nick, sizeof(cache->nick));
	safe_strdup(cache->pass, pass);
	cache->validated = validated ? 1 : 0;
	moddata_local_client(client, udb_nick_password_cache_md).ptr = cache;
}

static UdbNickPasswordCache *udb_nick_password_cache_take(Client *client, const char *nick)
{
	UdbNickPasswordCache *cache;
	ModData *m;

	if (!client || !nick || !udb_nick_password_cache_md)
		return NULL;
	m = &moddata_local_client(client, udb_nick_password_cache_md);
	cache = m->ptr;
	if (!cache)
		return NULL;
	if (strcasecmp(cache->nick, nick))
	{
		udb_nick_password_cache_clear(client);
		return NULL;
	}
	m->ptr = NULL;
	return cache;
}

static void udb_nick_identity_free(ModData *m)
{
	safe_free(m->ptr);
	m->ptr = NULL;
}

static void udb_nick_identity_clear(Client *client)
{
	UdbNickIdentity *identity;

	if (!client || !udb_nick_identity_md)
		return;
	identity = moddata_local_client(client, udb_nick_identity_md).ptr;
	if (identity)
	{
		safe_free(identity);
		moddata_local_client(client, udb_nick_identity_md).ptr = NULL;
	}
}

/* Feed distinct labels, presence bits and fixed-width lengths to SHA-256 so
 * distinct pass/access policies cannot share an ambiguous encoding. */
static int udb_nick_auth_policy_digest_field(EVP_MD_CTX *ctx, char label, const char *value)
{
	unsigned char present = value ? 1 : 0;
	unsigned char lenbuf[4];
	size_t len = value ? strlen(value) : 0;

	if (len > UDB_RECORD_VALUE_MAX)
		return 0;
	lenbuf[0] = (unsigned char)(len >> 24);
	lenbuf[1] = (unsigned char)(len >> 16);
	lenbuf[2] = (unsigned char)(len >> 8);
	lenbuf[3] = (unsigned char)len;
	return EVP_DigestUpdate(ctx, &label, sizeof(label)) == 1 && EVP_DigestUpdate(ctx, &present, sizeof(present)) == 1 &&
		   EVP_DigestUpdate(ctx, lenbuf, sizeof(lenbuf)) == 1 && (!len || EVP_DigestUpdate(ctx, value, len) == 1);
}

static int udb_nick_auth_policy_digest(UdbRecord *nick_rec, char out[65])
{
	EVP_MD_CTX *ctx;
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digestlen = 0;
	UdbRecord *rec;
	const char *pass = NULL;
	const char *access = NULL;
	unsigned int i;

	if (!nick_rec || !out)
		return 0;
	rec = udb_record_find(NULL, NKEY_PASS, nick_rec);
	if (!rec || BadPtr(rec->data_str))
		return 0;
	pass = rec->data_str;
	rec = udb_record_find(udb_ctx, NKEY_ACCESS, nick_rec);
	if (rec)
		access = rec->data_str;
	ctx = EVP_MD_CTX_new();
	if (!ctx)
		return 0;
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 || !udb_nick_auth_policy_digest_field(ctx, 'P', pass) ||
		!udb_nick_auth_policy_digest_field(ctx, 'A', access) || EVP_DigestFinal_ex(ctx, digest, &digestlen) != 1 ||
		digestlen != 32)
	{
		EVP_MD_CTX_free(ctx);
		return 0;
	}
	EVP_MD_CTX_free(ctx);
	for (i = 0; i < digestlen; i++)
		snprintf(out + (i * 2), 3, "%02x", digest[i]);
	out[64] = '\0';
	return 1;
}

/* Having pass means UDB may own identity effects; it never means the current
 * holder is authenticated. Passless profiles can never have applied effects. */
static int udb_nick_profile_has_pass(UdbRecord *nick_rec)
{
	return nick_rec && udb_record_find(NULL, NKEY_PASS, nick_rec) != NULL;
}

static void udb_nick_prepare_tree_replace(UdbBlock *block, UdbRecord *candidate_tree)
{
	udb_nick_replacement_tree = (block && block->letter == UDB_BLOCK_NICKS) ? candidate_tree : NULL;
}

static void udb_nick_finish_tree_replace(void)
{
	udb_nick_replacement_tree = NULL;
}

/* Identity exists only while an active UDB authentication is bound to the
 * current nick. It is created after a confirmed nick change and destroyed by
 * suspend, policy changes and leaving the nick; it never carries proof across
 * a suspend. */
static void udb_nick_identity_set(Client *client, UdbRecord *nick_rec)
{
	UdbNickIdentity *identity;

	if (!client || !MyConnect(client) || !nick_rec || BadPtr(nick_rec->key) || !udb_nick_profile_has_pass(nick_rec) ||
		!udb_nick_identity_md)
		return;
	identity = safe_alloc(sizeof(*identity));
	if (!udb_nick_auth_policy_digest(nick_rec, identity->policy_digest))
	{
		safe_free(identity);
		return;
	}
	strlcpy(identity->nick, nick_rec->key, sizeof(identity->nick));
	udb_nick_identity_clear(client);
	moddata_local_client(client, udb_nick_identity_md).ptr = identity;
}

static int udb_nick_identity_valid(Client *client, UdbRecord *nick_rec)
{
	UdbNickIdentity *identity;
	char digest[65];

	if (!client || !nick_rec || BadPtr(nick_rec->key) || !udb_nick_identity_md)
		return 0;
	identity = moddata_local_client(client, udb_nick_identity_md).ptr;
	if (!identity || strcasecmp(identity->nick, nick_rec->key) || strcasecmp(client->name, identity->nick) ||
		!udb_nick_profile_has_pass(nick_rec) || !udb_nick_auth_policy_digest(nick_rec, digest) ||
		strcmp(identity->policy_digest, digest) || !udb_nick_access_allowed(client, nick_rec))
		return 0;
	return 1;
}

/* True while UDB owns the runtime effects currently applied for this nick.
 * The marker may outlive a policy digest change until the apply path revokes
 * it, which is what lets pass/access changes strip UDB effects before the
 * forced rename. */
static int udb_nick_identity_owned(Client *client, const char *nick)
{
	UdbNickIdentity *identity;

	if (!client || !nick || !udb_nick_identity_md)
		return 0;
	identity = moddata_local_client(client, udb_nick_identity_md).ptr;
	return identity && !strcasecmp(identity->nick, nick) && !strcasecmp(client->name, nick);
}

static void udb_nick_pending_auth_free(ModData *m)
{
	safe_free(m->ptr);
	m->ptr = NULL;
}

static void udb_nick_pending_auth_clear(Client *client)
{
	UdbNickPendingAuth *auth;

	if (!client || !udb_nick_pending_auth_md)
		return;
	auth = moddata_local_client(client, udb_nick_pending_auth_md).ptr;
	if (auth)
	{
		safe_free(auth);
		moddata_local_client(client, udb_nick_pending_auth_md).ptr = NULL;
	}
}

static void udb_nick_pending_auth_set(Client *client, UdbRecord *nick_rec)
{
	UdbNickPendingAuth *auth;

	if (!client || !MyConnect(client) || !nick_rec || BadPtr(nick_rec->key) || !udb_nick_profile_has_pass(nick_rec) ||
		!udb_nick_pending_auth_md)
		return;
	auth = safe_alloc(sizeof(*auth));
	if (!udb_nick_auth_policy_digest(nick_rec, auth->policy_digest))
	{
		safe_free(auth);
		return;
	}
	strlcpy(auth->nick, nick_rec->key, sizeof(auth->nick));
	udb_nick_pending_auth_clear(client);
	moddata_local_client(client, udb_nick_pending_auth_md).ptr = auth;
}

static int udb_nick_pending_auth_valid(Client *client, UdbRecord *nick_rec)
{
	UdbNickPendingAuth *auth;
	char digest[65];

	if (!client || !nick_rec || BadPtr(nick_rec->key) || !udb_nick_pending_auth_md)
		return 0;
	auth = moddata_local_client(client, udb_nick_pending_auth_md).ptr;
	if (!auth || strcasecmp(auth->nick, nick_rec->key) || strcasecmp(client->name, auth->nick) ||
		!udb_nick_profile_has_pass(nick_rec) || !udb_nick_auth_policy_digest(nick_rec, digest) ||
		strcmp(auth->policy_digest, digest) || !udb_nick_access_allowed(client, nick_rec))
		return 0;
	return 1;
}

/* The nick change is already confirmed: the preflight credential is consumed
 * and can only become active identity through the caller. Invalid or stale
 * pending state is always discarded without granting anything, and it never
 * survives as suspended proof. */
static int udb_nick_pending_auth_consume(Client *client, UdbRecord *nick_rec)
{
	if (!udb_nick_pending_auth_valid(client, nick_rec))
	{
		udb_nick_pending_auth_clear(client);
		return 0;
	}
	udb_nick_pending_auth_clear(client);
	return 1;
}

/* A pending credential only survives for the attempt that created it. Once a
 * local NICK command finishes without establishing the requested nick, the
 * credential can never be reused by a later forced change such as SVSNICK. */
static void udb_nick_pending_auth_expire(Client *client, const char *requested_nick)
{
	if (!client || !MyConnect(client) || IsDead(client) || !IsUser(client))
		return;
	if (!requested_nick || strcasecmp(client->name, requested_nick))
		udb_nick_pending_auth_clear(client);
}

static void udb_nick_set_vhost(Client *client, UdbRecord *vhost_rec)
{
	if (!client || !client->user || !vhost_rec || !vhost_rec->data_str)
		return;

	/* If the vhost is already active and set to this exact value, nothing to do */
	if (client->user->virthost && !strcmp(client->user->virthost, vhost_rec->data_str) && IsHidden(client) &&
		IsSetHost(client))
		return;

	userhost_save_current(client);
	safe_strdup(client->user->virthost, vhost_rec->data_str);
	client->umodes |= UMODE_HIDE;
	client->umodes |= UMODE_SETHOST;

	if (IsUser(client))
	{
		sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, client->user->virthost);
	}

	if (MyConnect(client))
	{
		sendto_one(client, NULL, ":%s MODE %s :+tx", client->name, client->name);
		udb_send_service_notice(client, SKEY_IPSERV, "*** Your vhost is now %s", client->user->virthost);
	}

	userhost_changed(client);
}

static void udb_nick_remove_vhost(Client *client)
{
	if (!client || !client->user)
		return;
	if (udb_ip_reapply_vhost(client))
		return;

	userhost_save_current(client);

	if (*client->user->cloakedhost)
	{
		safe_strdup(client->user->virthost, client->user->cloakedhost);
	}
	else
	{
		safe_strdup(client->user->virthost, client->user->realhost);
	}

	client->umodes &= ~UMODE_SETHOST;

	if (IsUser(client))
	{
		sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, client->user->virthost);
	}
	if (MyConnect(client))
	{
		sendto_one(client, NULL, ":%s MODE %s :-t", client->name, client->name);
		udb_send_service_notice(client, SKEY_IPSERV, "*** Your vhost has been removed");
	}

	userhost_changed(client);
}

static void udb_nick_grant_oper(Client *client, UdbRecord *nick_rec, UdbRecord *oper_rec)
{
	if (!client || !oper_rec)
		return;

	const char *operclass = oper_rec->data_str;
	if (BadPtr(operclass))
		return;

	if (!find_operclass(operclass))
	{
		udb_log(ULOG_WARNING, "UDB_OPERCLASS_NOT_FOUND", client,
				"operclass '$operclass' for $client.details does not exist in unrealircd.conf",
				log_data_string("operclass", operclass));
		return;
	}

	if (IsOper(client))
	{
		const char *curr_class = get_operclass(client);
		if (curr_class && !strcmp(curr_class, operclass))
			return;
		udb_nick_revoke_oper(client);
	}

	make_oper(client, "UDB", operclass, NULL, 0, NULL, NULL, NULL);
	if (IsOper(client) && udb_nick_oper_owned_md)
		moddata_local_client(client, udb_nick_oper_owned_md).i = 1;
}

static void udb_nick_revoke_oper(Client *client)
{
	long old_umodes;

	if (!client || !IsOper(client) || !udb_nick_oper_owned_md ||
		!moddata_local_client(client, udb_nick_oper_owned_md).i)
		return;

	old_umodes = client->umodes & ALL_UMODES;
	client->umodes &= ~UMODE_OPER;
	if (MyUser(client) && !list_empty(&client->special_node))
	{
		list_del(&client->special_node);
		INIT_LIST_HEAD(&client->special_node);
	}
	if (irccounts.operators > 0)
		irccounts.operators--;
	remove_oper_privileges(client, 0);
	moddata_local_client(client, udb_nick_oper_owned_md).i = 0;
	send_umode_out(client, 1, old_umodes);
}

static void udb_nick_set_modes(Client *client, UdbRecord *nick_rec, UdbRecord *mode_rec, const char *modes)
{
	if (!modes)
		return;
	long m = set_usermode(modes);
	long old_umodes = client->umodes & ALL_UMODES;
	/* Oper status is controlled exclusively by N::oper. */
	client->umodes |= m & ~UMODE_OPER;
	send_umode_out(client, 1, old_umodes);
}

static void udb_nick_set_swhois(Client *client, UdbRecord *nick_rec, UdbRecord *swhois_rec)
{
	if (!client || !client->user || !swhois_rec || !swhois_rec->data_str)
		return;
	swhois_delete(client, "udb", "*", &me, NULL);
	swhois_add(client, "udb", 100, swhois_rec->data_str, &me, NULL);
}

static void udb_nick_set_snomasks(Client *client, UdbRecord *nick_rec, UdbRecord *snomask_rec)
{
	if (!client || !client->user || !snomask_rec || !snomask_rec->data_str)
		return;

	long old_umodes = client->umodes & ALL_UMODES;
	set_snomask(client, snomask_rec->data_str);

	if (client->user->snomask && *client->user->snomask)
	{
		client->umodes |= UMODE_SERVNOTICE;
		if (MyUser(client))
			sendnumeric(client, RPL_SNOMASK, client->user->snomask);
	}
	else
	{
		client->umodes &= ~UMODE_SERVNOTICE;
	}

	send_umode_out(client, 1, old_umodes);
}

static void udb_nick_force_rename_with_notice(Client *client, const char *nick_in_db, const char *notice,
											  int offer_identify)
{
	char newnick[32];
	char rand_suffix[6];
	const char *args[5];
	char tsbuf[32];

	gen_random_alnum(rand_suffix, 5);
	rand_suffix[5] = '\0';
	snprintf(newnick, sizeof(newnick), "Guest%s", rand_suffix);
	if (notice)
		udb_send_service_notice(client, SKEY_NICKSERV, "%s", notice);
	else
		udb_send_service_notice(client, SKEY_NICKSERV,
								"This nickname (%s) has been registered or synced in the UDB database.", nick_in_db);
	if (offer_identify)
		udb_send_service_notice(client, SKEY_NICKSERV,
								"You have been renamed. If you are the owner, please identify: /NICK %s:Password",
								nick_in_db);

	tsbuf[0] = '\0';
	snprintf(tsbuf, sizeof(tsbuf), "%lld", (long long)TStime());
	args[0] = NULL;
	args[1] = client->name;
	args[2] = newnick;
	args[3] = tsbuf;
	args[4] = NULL;
	do_cmd(&me, NULL, "SVSNICK", 4, args);
}

static void udb_nick_force_rename(Client *client, const char *nick_in_db)
{
	udb_nick_force_rename_with_notice(client, nick_in_db, NULL, 1);
}

static void udb_nick_apply(Client *client, UdbRecord *nick_rec, UdbNickApplyReason reason)
{
	UdbRecord *forbid;
	UdbRecord *pass_rec;
	UdbRecord *suspend;
	UdbRecord *effect_rec;
	char notice[UDB_RECORD_VALUE_MAX + 64];

	if (!client || !nick_rec)
		return;
	forbid = udb_record_find(udb_ctx, NKEY_FORBID, nick_rec);
	if (forbid)
	{
		/* A forbid always wins: revoke UDB-owned state before the rename. */
		if (udb_nick_identity_owned(client, nick_rec->key))
			udb_nick_strip(client, nick_rec);
		udb_nick_identity_clear(client);
		snprintf(notice, sizeof(notice), "This nickname is forbidden. Reason: %s",
				 forbid->data_str ? forbid->data_str : "No reason given");
		udb_nick_force_rename_with_notice(client, nick_rec->key, notice, 0);
		return;
	}
	pass_rec = udb_record_find(NULL, NKEY_PASS, nick_rec);
	suspend = udb_record_find(udb_ctx, NKEY_SUSPEND, nick_rec);

	/* Access denial is fail-closed for adoption and refresh alike. UDB only
	 * removes effects it actually owned; a passless profile never had any. */
	if (!udb_nick_access_allowed(client, nick_rec))
	{
		udb_nick_password_cache_clear(client);
		if (udb_nick_identity_owned(client, nick_rec->key))
			udb_nick_strip(client, nick_rec);
		udb_nick_identity_clear(client);
		snprintf(notice, sizeof(notice), "Access to %s is not permitted from your IP address.", nick_rec->key);
		udb_nick_force_rename_with_notice(client, nick_rec->key, notice, 0);
		return;
	}

	/* A profile without a credential controls only nick use. It can never turn
	 * a matching nick, account or +r into UDB identity or profile effects. */
	if (!pass_rec)
	{
		udb_nick_password_cache_clear(client);
		udb_nick_identity_clear(client);
		if (suspend)
			udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s",
									suspend->data_str ? suspend->data_str : "No reason given");
		return;
	}

	/* A refreshed suspension revokes identity/effects without requiring a new
	 * authentication: the nick may stay occupied but no auth survives it. */
	if (reason == UDB_NICK_APPLY_REFRESH && suspend)
	{
		if (udb_nick_identity_owned(client, nick_rec->key))
			udb_nick_strip(client, nick_rec);
		udb_nick_identity_clear(client);
		udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s",
								suspend->data_str ? suspend->data_str : "No reason given");
		return;
	}

	/* A protected profile materializes identity only with a valid active UDB
	 * proof. SVSNICK and forced changes never carry one, and a pass/access
	 * change invalidates the digest, so both are renamed away. */
	if (!udb_nick_identity_valid(client, nick_rec))
	{
		if (udb_nick_identity_owned(client, nick_rec->key))
			udb_nick_strip(client, nick_rec);
		udb_nick_identity_clear(client);
		udb_nick_force_rename(client, nick_rec->key);
		return;
	}

	/* Adoption of a suspended profile validates pass/access but materializes
	 * nothing: no identity, account/+r or effects, and no strip of state UDB
	 * never applied. */
	if (reason == UDB_NICK_APPLY_ADOPT && suspend)
	{
		udb_nick_identity_clear(client);
		udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s",
								suspend->data_str ? suspend->data_str : "No reason given");
		return;
	}

	if (client->user)
		strlcpy(client->user->account, nick_rec->key, sizeof(client->user->account));

	long old_umodes = client->umodes & ALL_UMODES;
	client->umodes |= UMODE_REGNICK;
	send_umode_out(client, 1, old_umodes);

	effect_rec = udb_record_find(udb_ctx, NKEY_VHOST, nick_rec);
	if (effect_rec)
		udb_nick_set_vhost(client, effect_rec);
	effect_rec = udb_record_find(udb_ctx, NKEY_OPER, nick_rec);
	if (effect_rec)
		udb_nick_grant_oper(client, nick_rec, effect_rec);
	effect_rec = udb_record_find(udb_ctx, NKEY_MODES, nick_rec);
	if (effect_rec && effect_rec->data_str)
		udb_nick_set_modes(client, nick_rec, effect_rec, effect_rec->data_str);
	effect_rec = udb_record_find(udb_ctx, NKEY_SWHOIS, nick_rec);
	if (effect_rec)
		udb_nick_set_swhois(client, nick_rec, effect_rec);
	effect_rec = udb_record_find(udb_ctx, NKEY_SNOMASKS, nick_rec);
	if (effect_rec)
		udb_nick_set_snomasks(client, nick_rec, effect_rec);
}

static void udb_nick_strip(Client *client, UdbRecord *nick_rec)
{
	UdbRecord *vhost_rec;
	UdbRecord *snomask_rec;

	if (!client)
		return;

	if (client->user)
	{
		strlcpy(client->user->account, "*", sizeof(client->user->account));
	}

	udb_nick_revoke_oper(client);

	long old_umodes = client->umodes & ALL_UMODES;
	if (nick_rec)
	{
		UdbRecord *mode_rec = udb_record_find(udb_ctx, NKEY_MODES, nick_rec);
		if (mode_rec && mode_rec->data_str)
			client->umodes &= ~(set_usermode(mode_rec->data_str) & ~UMODE_OPER);
	}
	client->umodes &= ~UMODE_REGNICK;
	send_umode_out(client, 1, old_umodes);

	/* UDB only removes effects its active profile had actually applied. An
	 * absent vhost or snomask record means UDB never owned that state, so
	 * externally supplied values must survive the strip. */
	vhost_rec = nick_rec ? udb_record_find(udb_ctx, NKEY_VHOST, nick_rec) : NULL;
	snomask_rec = nick_rec ? udb_record_find(udb_ctx, NKEY_SNOMASKS, nick_rec) : NULL;

	if (snomask_rec)
		set_snomask(client, NULL);

	if (vhost_rec)
		udb_nick_remove_vhost(client);

	if (nick_rec)
	{
		UdbRecord *swhois_rec = udb_record_find(udb_ctx, NKEY_SWHOIS, nick_rec);
		if (swhois_rec && swhois_rec->data_str)
		{
			swhois_delete(client, "udb", "*", &me, NULL);
		}
	}
}

static void udb_nick_remove_record(UdbBlock *block, UdbRecord *rec)
{
	if (rec->parent != block->tree)
	{
		UdbRecord *nick_rec = rec->parent;
		Client *client = find_user(nick_rec->key, NULL);
		if (client && MyUser(client))
		{
			if (!strcmp(rec->key, NKEY_PASS))
			{
				/* Removing the credential revokes UDB identity/effects while
				 * leaving externally supplied state untouched. */
				if (udb_nick_identity_owned(client, nick_rec->key))
					udb_nick_strip(client, nick_rec);
				udb_nick_identity_clear(client);
				udb_nick_pending_auth_clear(client);
			}
			else if (!strcmp(rec->key, NKEY_ACCESS))
			{
				/* The candidate profile is reapplied after this old tree is
				 * retired; the policy digest change revokes the identity. */
			}
			else if (udb_nick_identity_owned(client, nick_rec->key))
			{
				if (!strcmp(rec->key, NKEY_VHOST))
				{
					udb_nick_remove_vhost(client);
				}
				else if (!strcmp(rec->key, NKEY_OPER))
				{
					udb_nick_revoke_oper(client);
				}
				else if (!strcmp(rec->key, NKEY_SWHOIS))
				{
					swhois_delete(client, "udb", "*", &me, NULL);
				}
				else if (!strcmp(rec->key, NKEY_MODES))
				{
					long old_umodes = client->umodes & ALL_UMODES;
					if (rec->data_str)
						client->umodes &= ~(set_usermode(rec->data_str) & ~UMODE_OPER);
					UdbRecord *oper_rec = udb_record_find(udb_ctx, NKEY_OPER, nick_rec);
					if (oper_rec && IsOper(client))
						client->umodes |= OPER_MODES;
					send_umode_out(client, 1, old_umodes);
				}
				else if (!strcmp(rec->key, NKEY_SNOMASKS))
				{
					long old_umodes = client->umodes & ALL_UMODES;
					set_snomask(client, NULL);
					UdbRecord *oper_rec = udb_record_find(udb_ctx, NKEY_OPER, nick_rec);
					if (oper_rec && IsOper(client))
					{
						set_snomask(client, OPER_SNOMASK);
						if (client->user->snomask && *client->user->snomask)
						{
							client->umodes |= UMODE_SERVNOTICE;
							sendnumeric(client, RPL_SNOMASK, client->user->snomask);
						}
					}
					else
					{
						client->umodes &= ~UMODE_SERVNOTICE;
					}
					send_umode_out(client, 1, old_umodes);
				}
				else if (!strcmp(rec->key, NKEY_SUSPEND))
				{
					/* The candidate profile is reapplied after this old tree is retired. */
				}
			}
		}
	}
	else
	{
		Client *client = find_user(rec->key, NULL);
		if (client && MyUser(client))
		{
			UdbRecord *candidate =
				udb_nick_replacement_tree ? udb_record_find(NULL, rec->key, udb_nick_replacement_tree) : NULL;
			/* Active identity survives a full replacement only for an existing
			 * candidate with the same pass/access policy, without forbid or
			 * suspend, and with a still-permitted access check. A deleted
			 * profile, a changed policy or a normal->suspend transition
			 * revokes it; UDB only removes effects it actually owned. */
			int keep_identity = candidate && !udb_record_find(udb_ctx, NKEY_FORBID, candidate) &&
								!udb_record_find(udb_ctx, NKEY_SUSPEND, candidate) &&
								udb_nick_identity_valid(client, candidate);

			udb_nick_pending_auth_clear(client);
			if (udb_nick_identity_owned(client, rec->key))
				udb_nick_strip(client, rec);
			if (!keep_identity)
				udb_nick_identity_clear(client);
		}
	}
}

static UdbPasswordFailure *udb_password_failure_find(UdbRecord *profile_rec, Client *client, int create)
{
	UdbPasswordFailure *oldest = NULL;
	const char *ip = client ? client->ip : NULL;
	time_t now = TStime();
	int period = udb_cfg ? udb_cfg->flood_period : 60;
	unsigned int i;

	if (period <= 0)
		period = 60;
	if (!profile_rec || !profile_rec->key || BadPtr(ip))
		return NULL;
	for (i = 0; i < UDB_PASSWORD_FAILURE_SLOTS; i++)
	{
		UdbPasswordFailure *entry = &udb_password_failures[i];
		if (entry->since && now - entry->since >= period)
			memset(entry, 0, sizeof(*entry));
		if (entry->since && entry->block_idx == profile_rec->block_idx && !strcmp(entry->profile, profile_rec->key) &&
			!strcmp(entry->ip, ip))
			return entry;
		if (!entry->since)
			oldest = entry;
		else if (!oldest || entry->since < oldest->since)
			oldest = entry;
	}
	if (!create || !oldest)
		return NULL;
	memset(oldest, 0, sizeof(*oldest));
	strlcpy(oldest->profile, profile_rec->key, sizeof(oldest->profile));
	strlcpy(oldest->ip, ip, sizeof(oldest->ip));
	oldest->block_idx = profile_rec->block_idx;
	oldest->since = now;
	return oldest;
}

static int udb_password_flooded(UdbRecord *profile_rec, Client *client)
{
	UdbPasswordFailure *entry = udb_password_failure_find(profile_rec, client, 0);
	int attempts = udb_cfg ? udb_cfg->flood_attempts : 5;

	if (attempts <= 0)
		attempts = 5;
	return entry && entry->attempts >= (unsigned int)attempts;
}

static void udb_password_failure_record(UdbRecord *profile_rec, Client *client, int success)
{
	UdbPasswordFailure *entry = udb_password_failure_find(profile_rec, client, !success);

	if (!entry)
		return;
	if (success)
		memset(entry, 0, sizeof(*entry));
	else if (entry->attempts != (unsigned int)-1)
		entry->attempts++;
}

static void udb_nick_password_failure_notice(Client *client, UdbRecord *profile_rec, const char *nick)
{
	if (udb_password_flooded(profile_rec, client))
		udb_send_service_notice(client, SKEY_NICKSERV, "Too many failed password attempts for %s; try again later.",
								nick);
	else
		udb_send_service_notice(client, SKEY_NICKSERV, "Invalid password for %s.", nick);
}

static int udb_password_type(const char *stored_pass, const char **hash)
{
	if (!strncmp(stored_pass, "argon2id:", 9))
	{
		*hash = stored_pass + 9;
		return AUTHTYPE_ARGON2;
	}
	if (!strncmp(stored_pass, "sha256:", 7))
	{
		*hash = stored_pass + 7;
		return UDB_AUTHTYPE_SHA256;
	}
	if (!strncmp(stored_pass, "crypt:", 6))
	{
		*hash = stored_pass + 6;
		return AUTHTYPE_UNIXCRYPT;
	}
	*hash = stored_pass;
	return AUTHTYPE_INVALID;
}

static int udb_is_sha256_hash(const char *hash)
{
	size_t i;

	if (!hash || strlen(hash) != 64)
		return 0;
	for (i = 0; hash[i]; i++)
		if (!isxdigit((unsigned char)hash[i]))
			return 0;
	return 1;
}

static int udb_check_password(const char *pass, UdbRecord *profile_rec, Client *client)
{
	UdbRecord *pass_rec;
	const char *stored_pass;
	const char *hash;
	AuthConfig as;
	int type;
	int success;

	if (!pass || !profile_rec || udb_password_flooded(profile_rec, client))
		return 0;
	pass_rec = udb_record_find(udb_ctx, NKEY_PASS, profile_rec);
	if (!pass_rec || BadPtr(pass_rec->data_str))
	{
		udb_password_failure_record(profile_rec, client, 0);
		return 0;
	}
	stored_pass = pass_rec->data_str;
	type = udb_password_type(stored_pass, &hash);
	if (type == AUTHTYPE_ARGON2 && strncmp(hash, "$argon2id$", 10))
		type = AUTHTYPE_INVALID;
	if (type == UDB_AUTHTYPE_SHA256 && !udb_is_sha256_hash(hash))
		type = AUTHTYPE_INVALID;
	if (type == AUTHTYPE_INVALID)
	{
		udb_password_failure_record(profile_rec, client, 0);
		return 0;
	}
	if (type == UDB_AUTHTYPE_SHA256)
	{
		char digest[65];
		sha256hash(digest, pass, strlen(pass));
		success = !strcasecmp(digest, hash);
	}
	else
	{
		memset(&as, 0, sizeof(as));
		as.type = type;
		as.data = (char *)hash;
		success = Auth_Check(client, &as, pass);
	}
	udb_password_failure_record(profile_rec, client, success);
	return success;
}

static int udb_cidr_matches(const char *ip, const char *cidr)
{
	unsigned char address[16], network[16];
	char network_text[INET6_ADDRSTRLEN];
	const char *slash = strchr(cidr, '/');
	unsigned int prefix;
	int family;
	int bytes;

	if (!slash || (size_t)(slash - cidr) >= sizeof(network_text))
		return 0;
	memcpy(network_text, cidr, slash - cidr);
	network_text[slash - cidr] = '\0';
	if (!udb_parse_uint_strict(slash + 1, &prefix, 0, 128))
		return 0;
	if (inet_pton(AF_INET, ip, address) == 1)
		family = AF_INET;
	else if (inet_pton(AF_INET6, ip, address) == 1)
		family = AF_INET6;
	else
		return 0;
	if (inet_pton(family, network_text, network) != 1 || prefix > (family == AF_INET ? 32 : 128))
		return 0;
	bytes = prefix / 8;
	if (bytes && memcmp(address, network, bytes))
		return 0;
	if (prefix % 8 &&
		(address[bytes] & (0xff << (8 - (prefix % 8)))) != (network[bytes] & (0xff << (8 - (prefix % 8)))))
		return 0;
	return 1;
}

static int udb_nick_access_allowed(Client *client, UdbRecord *nick_rec)
{
	UdbRecord *access_rec = udb_record_find(udb_ctx, NKEY_ACCESS, nick_rec);
	const char *p;

	if (!access_rec)
		return 1;
	if (BadPtr(access_rec->data_str) || !client || BadPtr(client->ip))
		return 0;
	for (p = access_rec->data_str; *p;)
	{
		char cidr[INET6_ADDRSTRLEN + 5];
		size_t len = 0;
		while (*p == ',' || isspace((unsigned char)*p))
			p++;
		while (*p && *p != ',' && !isspace((unsigned char)*p) && len + 1 < sizeof(cidr))
			cidr[len++] = *p++;
		cidr[len] = '\0';
		while (*p && *p != ',' && !isspace((unsigned char)*p))
			p++;
		if (len && udb_cidr_matches(client->ip, cidr))
			return 1;
	}
	return 0;
}

CMD_FUNC(cmd_ghost)
{
	if (parc < 3)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "GHOST");
		return;
	}
	const char *target_nick = parv[1];
	const char *pass = parv[2];

	if (!udb_ctx || !udb_ctx->nicks)
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "UDB is not fully initialized.");
		return;
	}

	UdbRecord *nick_rec = udb_record_find(udb_ctx, target_nick, udb_ctx->nicks);
	if (!nick_rec)
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Nick %s is not registered.", target_nick);
		return;
	}
	if (!udb_record_find(NULL, NKEY_PASS, nick_rec))
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Nick %s has no UDB password.", target_nick);
		return;
	}

	if (!udb_check_password(pass, nick_rec, client))
	{
		udb_nick_password_failure_notice(client, nick_rec, target_nick);
		return;
	}
	if (!udb_nick_access_allowed(client, nick_rec))
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
								target_nick);
		return;
	}

	Client *target = find_client(target_nick, NULL);
	if (target)
	{
		if (target == client)
		{
			udb_send_service_notice(client, SKEY_NICKSERV, "You cannot ghost yourself.");
			return;
		}
		udb_send_service_notice(client, SKEY_NICKSERV, "Ghosting %s...", target_nick);
		exit_client(target, NULL, "GHOST command used");
	}
	else
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "%s is not online.", target_nick);
	}
}

CMD_OVERRIDE_FUNC(udb_override_nick)
{
	if (parc <= 1 || !MyConnect(client))
		goto passthrough;

	/* A /NICK password belongs only to this attempt; it is never session
	 * state and is consumed by CAN_USE_NICK or discarded here. */
	udb_nick_password_cache_clear(client);

	const char *nick = parv[1];
	char clean_nick[NICKLEN + 64];
	strlcpy(clean_nick, nick, sizeof(clean_nick));
	char *pass_colon = strchr(clean_nick, ':');
	char *pass_bang = strchr(clean_nick, '!');

	char *pass = NULL;
	int force_ghost = 0;

	if (pass_colon && (!pass_bang || pass_colon < pass_bang))
	{
		*pass_colon = '\0';
		pass = pass_colon + 1;
		force_ghost = 0;
	}
	else if (pass_bang && (!pass_colon || pass_bang < pass_colon))
	{
		*pass_bang = '\0';
		pass = pass_bang + 1;
		force_ghost = 1;
	}

	UdbRecord *rec = (udb_ctx && udb_ctx->nicks) ? udb_record_find(udb_ctx, clean_nick, udb_ctx->nicks) : NULL;
	if (rec)
	{
		UdbRecord *forbid = udb_record_find(udb_ctx, NKEY_FORBID, rec);
		if (forbid)
		{
			udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is forbidden. Reason: %s",
									forbid->data_str ? forbid->data_str : "No reason given");
			return;
		}
	}
	if (!pass)
		goto passthrough;
	if (rec && !udb_record_find(NULL, NKEY_PASS, rec))
		goto dispatch_clean_nick;

	Client *acptr = rec ? find_client(clean_nick, NULL) : NULL;
	int validated = 0;

	/* The core NICK path checks the password too. Only pre-check when a
	 * collision needs the password for ghost/recovery, and stop on failure so
	 * the flood tracker is consumed exactly once. */
	if (rec && acptr && acptr != client)
	{
		if (!udb_check_password(pass, rec, client))
		{
			udb_nick_password_failure_notice(client, rec, clean_nick);
			sendnumeric(client, ERR_ERRONEUSNICKNAME, clean_nick, "Nickname requires a valid UDB password.");
			return;
		}
		if (!udb_nick_access_allowed(client, rec))
		{
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									clean_nick);
			sendnumeric(client, ERR_ERRONEUSNICKNAME, clean_nick,
						"Nickname requires a valid UDB password and authorized IP.");
			return;
		}
		validated = 1;
		if (force_ghost)
		{
			char quit_msg[128];
			snprintf(quit_msg, sizeof(quit_msg), "Ghosted (Nick taken by %s)", client->name);
			exit_client(acptr, NULL, quit_msg);
		}
		else
		{
			udb_send_service_notice(client, SKEY_NICKSERV,
									"This nickname is currently in use. If you are the owner, you can recover it by "
									"typing /NICK %s!Password",
									clean_nick);
		}
	}
	if (rec)
		udb_nick_password_cache_set(client, clean_nick, pass, validated);

dispatch_clean_nick:;
	const char *new_parv[MAXPARA + 1];
	for (int i = 0; i < parc; i++)
		new_parv[i] = parv[i];
	new_parv[1] = clean_nick;
	CallCommandOverride(ovr, clictx, client, recv_mtags, parc, new_parv);
	/* The credential is bound to this attempt: a rejected change must not
	 * leave a promvable pending proof behind for a later forced rename. */
	udb_nick_pending_auth_expire(client, clean_nick);
	return;

passthrough:
	CALL_NEXT_COMMAND_OVERRIDE();
	udb_nick_pending_auth_expire(client, parc > 1 ? parv[1] : NULL);
}

static int udb_hook_can_use_nick(Client *client, const char *newnick, const char **reject_reason)
{
	if (!udb_ctx || !udb_ctx->nicks)
		return HOOK_CONTINUE;
	if (!MyConnect(client))
		return HOOK_CONTINUE;

	UdbRecord *nick_rec = udb_record_find(udb_ctx, newnick, udb_ctx->nicks);
	if (nick_rec)
	{
		UdbRecord *forbid = udb_record_find(udb_ctx, NKEY_FORBID, nick_rec);
		UdbRecord *pass_rec;
		if (forbid)
		{
			static char forbid_reject[UDB_RECORD_VALUE_MAX + 64];
			snprintf(forbid_reject, sizeof(forbid_reject), "This nickname is forbidden. Reason: %s",
					 forbid->data_str ? forbid->data_str : "No reason given");
			*reject_reason = forbid_reject;
			return HOOK_DENY;
		}
		pass_rec = udb_record_find(NULL, NKEY_PASS, nick_rec);
		if (!pass_rec)
		{
			udb_nick_password_cache_clear(client);
			if (udb_nick_access_allowed(client, nick_rec))
				return HOOK_CONTINUE;
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									newnick);
			*reject_reason = "Access to this nickname is not permitted from your IP address.";
			return HOOK_DENY;
		}

		/* If client is already this nick and identified with +r, allow without re-entering password */
		if (!strcasecmp(client->name, newnick) && has_user_mode(client, 'r'))
		{
			udb_nick_password_cache_clear(client);
			if (udb_nick_access_allowed(client, nick_rec))
				return HOOK_CONTINUE;
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									newnick);
			return HOOK_DENY;
		}

		UdbNickPasswordCache *cred = udb_nick_password_cache_take(client, newnick);
		if (cred)
		{
			int password_ok = cred->validated || udb_check_password(cred->pass, nick_rec, client);
			int access_ok = password_ok && udb_nick_access_allowed(client, nick_rec);

			if (password_ok && access_ok)
			{
				udb_nick_password_cache_entry_free(cred);
				udb_nick_pending_auth_set(client, nick_rec);
				return HOOK_CONTINUE;
			}
			if (password_ok)
				udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
										newnick);
			else
				udb_nick_password_failure_notice(client, nick_rec, newnick);
			udb_nick_password_cache_entry_free(cred);
		}
		else
		{
			udb_send_service_notice(client, SKEY_NICKSERV,
									"Nickname is unavailable: This nick is registered and requires a password and an "
									"authorized IP. Use /NICK %s:Password",
									newnick);
		}

		static char reject_buf[256];
		snprintf(reject_buf, sizeof(reject_buf),
				 "This nick is registered and requires a password and an authorized IP. Use /NICK %s:Password",
				 newnick);
		*reject_reason = reject_buf;
		return HOOK_DENY;
	}
	udb_nick_password_cache_clear(client);
	return HOOK_CONTINUE;
}

static int udb_hook_nick_change(Client *client, MessageTag *mtags, const char *newnick)
{
	if (!udb_ctx || !udb_ctx->nicks)
		return 0;
	if (!MyConnect(client))
		return 0;

	UdbRecord *old_rec = udb_record_find(udb_ctx, client->name, udb_ctx->nicks);
	UdbRecord *new_rec = udb_record_find(udb_ctx, newnick, udb_ctx->nicks);

	if (old_rec && old_rec != new_rec)
	{
		/* UDB owns runtime effects only while the identity marker for the old
		 * nick is present. A dormant/passless/suspended profile must never
		 * revoke externally supplied state on departure. */
		if (udb_nick_identity_owned(client, old_rec->key))
			udb_nick_strip(client, old_rec);
		udb_nick_identity_clear(client);
		udb_nick_password_cache_clear(client);
		/* The pending credential for the destination nick survives here; only
		 * a confirmed POST_NICKCHANGE may consume it. */
	}

	return 0;
}

static int udb_hook_post_nick_change(Client *client, MessageTag *recv_mtags, const char *oldnick)
{
	if (!udb_ctx || !udb_ctx->nicks)
		return 0;
	if (!MyConnect(client))
		return 0;

	UdbRecord *new_rec = udb_record_find(udb_ctx, client->name, udb_ctx->nicks);
	if (new_rec)
	{
		/* A confirmed nick change consumes the preflight credential and only
		 * then activates identity. SVSNICK and other forced changes own no
		 * pending credential, so they never create identity. */
		if (udb_nick_pending_auth_consume(client, new_rec))
			udb_nick_identity_set(client, new_rec);
		udb_nick_apply(client, new_rec, UDB_NICK_APPLY_ADOPT);
	}
	else
	{
		udb_nick_pending_auth_clear(client);
		udb_nick_identity_clear(client);
	}
	return 0;
}

static int udb_hook_local_connect(Client *client)
{
	if (!udb_ctx || !udb_ctx->nicks)
		return 0;

	UdbRecord *nick_rec = udb_record_find(udb_ctx, client->name, udb_ctx->nicks);
	if (nick_rec)
	{
		/* Initial registration follows the same rule as a later nick change:
		 * the preflight credential is consumed and becomes active identity
		 * before the profile is applied. */
		if (udb_nick_pending_auth_consume(client, nick_rec))
			udb_nick_identity_set(client, nick_rec);
		udb_nick_apply(client, nick_rec, UDB_NICK_APPLY_ADOPT);
	}
	else
	{
		udb_nick_pending_auth_clear(client);
		udb_nick_identity_clear(client);
	}
	return 0;
}

int udb_nicks_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_password_cache";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_password_cache_free;
	udb_nick_password_cache_md = ModDataAdd(modinfo->handle, mreq);
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_identity";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_identity_free;
	udb_nick_identity_md = ModDataAdd(modinfo->handle, mreq);
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_pending_auth";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_pending_auth_free;
	udb_nick_pending_auth_md = ModDataAdd(modinfo->handle, mreq);
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_oper_owned";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	udb_nick_oper_owned_md = ModDataAdd(modinfo->handle, mreq);

	CommandAdd(modinfo->handle, "GHOST", cmd_ghost, 3, CMD_USER);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_USE_NICK, 0, udb_hook_can_use_nick);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_NICKCHANGE, 0, udb_hook_nick_change);
	HookAdd(modinfo->handle, HOOKTYPE_POST_LOCAL_NICKCHANGE, 0, udb_hook_post_nick_change);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, udb_hook_local_connect);
	return MOD_SUCCESS;
}

int udb_nicks_load(ModuleInfo *modinfo)
{
	CommandOverrideAdd(modinfo->handle, "NICK", 0, udb_override_nick);
	return 0;
}
