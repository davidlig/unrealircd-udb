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
 * bound to the current nick; it is destroyed by suspend and by leaving the
 * nick, and is never reconstructed after the fact. Credential and access
 * values are re-checked live on every use: changing pass, or an access list
 * that still permits the holder, does not revoke an active identity. */
typedef struct UdbNickIdentity UdbNickIdentity;
struct UdbNickIdentity
{
	char nick[NICKLEN + 1];
};

/* Preflight password/access success for a destination nick. It never grants
 * identity: only a confirmed nick change promotes it to active identity. */
typedef struct UdbNickPendingAuth UdbNickPendingAuth;
struct UdbNickPendingAuth
{
	char nick[NICKLEN + 1];
	char policy_digest[65];
};

/* Runtime effect ownership marker. It records exactly which runtime state UDB
 * changed for the current nick. Profile records describe desired state and can
 * never be used to reconstruct what UDB applied in the past: external sources
 * may have replaced a value UDB once set. */
typedef struct UdbNickEffects UdbNickEffects;
struct UdbNickEffects
{
	char nick[NICKLEN + 1];
	long modes_added;
	int vhost_owned;
	char applied_vhost[HOSTLEN + 1];
	int oper_vhost_owned;
	char *oper_previous_vhost;
	char *oper_applied_vhost;
	char oper_previous_username[USERLEN + 1];
	char oper_applied_username[USERLEN + 1];
	int oper_username_owned;
	int oper_previous_sethost;
	int oper_vhost_attempted;
	int snomask_owned;
	char *previous_snomask;
	char *applied_snomask;
	char *applied_snomask_expr;
	int swhois_owned;
};

static ModDataInfo *udb_nick_password_cache_md = NULL;
static ModDataInfo *udb_nick_identity_md = NULL;
static ModDataInfo *udb_nick_pending_auth_md = NULL;
static ModDataInfo *udb_nick_effects_md = NULL;
static ModDataInfo *udb_nick_oper_owned_md = NULL;
/* Only set during a synchronous N-tree replacement; never retained per client. */
static UdbRecord *udb_nick_replacement_tree = NULL;

static void udb_nick_password_cache_entry_free(UdbNickPasswordCache *cache)
{
	if (!cache)
		return;
	safe_free_sensitive(cache->pass);
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
	safe_strdup_sensitive(cache->pass, pass);
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

/* Binds a one-shot pending credential to the exact pass/access policy that was
 * validated. Active identity deliberately does not use this digest: changing
 * pass, or an access list that still permits the holder, must not revoke it. */
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
	strlcpy(identity->nick, nick_rec->key, sizeof(identity->nick));
	udb_nick_identity_clear(client);
	moddata_local_client(client, udb_nick_identity_md).ptr = identity;
}

static int udb_nick_identity_valid(Client *client, UdbRecord *nick_rec)
{
	UdbNickIdentity *identity;

	if (!client || !nick_rec || BadPtr(nick_rec->key) || !udb_nick_identity_md)
		return 0;
	identity = moddata_local_client(client, udb_nick_identity_md).ptr;
	if (!identity || strcasecmp(identity->nick, nick_rec->key) || strcasecmp(client->name, identity->nick) ||
		!udb_nick_profile_has_pass(nick_rec) || !udb_nick_access_allowed(client, nick_rec))
		return 0;
	return 1;
}

static int udb_nick_identity_present(Client *client)
{
	if (!client || !MyUser(client) || !udb_nick_identity_md)
		return 0;
	return moddata_local_client(client, udb_nick_identity_md).ptr != NULL;
}

/* Identity revocation removes only the public projection UDB materialized:
 * account, +r and the identity marker. It never touches effects, and it leaves
 * externally supplied account/+r alone when UDB never held an identity. */
static void udb_nick_revoke_identity(Client *client)
{
	long old_umodes;

	if (!client || !MyUser(client))
		return;
	if (!udb_nick_identity_present(client))
		return;
	if (client->user)
		strlcpy(client->user->account, "*", sizeof(client->user->account));
	old_umodes = client->umodes & ALL_UMODES;
	client->umodes &= ~UMODE_REGNICK;
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
	udb_nick_identity_clear(client);
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

static UdbNickEffects *udb_nick_effects_get(Client *client)
{
	if (!client || !MyUser(client) || !udb_nick_effects_md)
		return NULL;
	return moddata_local_client(client, udb_nick_effects_md).ptr;
}

static UdbNickEffects *udb_nick_effects_ensure(Client *client)
{
	UdbNickEffects *fx;

	if (!client || !MyUser(client) || !udb_nick_effects_md)
		return NULL;
	fx = moddata_local_client(client, udb_nick_effects_md).ptr;
	if (!fx)
	{
		fx = safe_alloc(sizeof(*fx));
		moddata_local_client(client, udb_nick_effects_md).ptr = fx;
	}
	strlcpy(fx->nick, client->name, sizeof(fx->nick));
	return fx;
}

static void udb_nick_effects_free(ModData *m)
{
	UdbNickEffects *fx = m ? m->ptr : NULL;

	if (!fx)
		return;
	safe_free(fx->previous_snomask);
	safe_free(fx->applied_snomask);
	safe_free(fx->applied_snomask_expr);
	safe_free(fx->oper_previous_vhost);
	safe_free(fx->oper_applied_vhost);
	safe_free(fx);
	m->ptr = NULL;
}

static void udb_nick_effects_clear(Client *client)
{
	if (!client || !MyUser(client) || !udb_nick_effects_md)
		return;
	if (moddata_local_client(client, udb_nick_effects_md).ptr)
	{
		udb_nick_effects_free(&moddata_local_client(client, udb_nick_effects_md));
		moddata_local_client(client, udb_nick_effects_md).ptr = NULL;
	}
}

static void udb_nick_effects_revoke_modes(Client *client);
static void udb_nick_effects_revoke_vhost(Client *client);
static void udb_nick_effects_revoke_snomasks(Client *client);
static void udb_nick_effects_revoke_swhois(Client *client);

/* Ownership of a user mode is exactly the set of bits UDB changed from 0 to 1.
 * Bits that were already active belong to another source and must survive
 * every revoke, even when the profile also lists them. */
static void udb_nick_effects_apply_modes(Client *client, UdbRecord *mode_rec)
{
	UdbNickEffects *fx;
	long desired;
	long old_umodes;
	long removed;
	long added;

	if (!client || !mode_rec || BadPtr(mode_rec->data_str) || !MyUser(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	desired = set_usermode(mode_rec->data_str) & ~UMODE_OPER;
	old_umodes = client->umodes & ALL_UMODES;
	removed = fx->modes_added & ~desired;
	added = desired & ~old_umodes;
	client->umodes = (client->umodes & ~removed) | desired;
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
	fx->modes_added = (fx->modes_added & desired) | added;
}

static void udb_nick_effects_revoke_modes(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);
	long old_umodes;

	if (!fx || !fx->modes_added)
		return;
	old_umodes = client->umodes & ALL_UMODES;
	client->umodes &= ~fx->modes_added;
	fx->modes_added = 0;
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
}

/* UnrealIRCd stores snomasks as a sorted, de-duplicated letter set. Canonical
 * form lets us compare a configured value with the live value without churn. */
static void udb_nick_snomask_materialize(Client *client, const char *value)
{
	long old_umodes = client->umodes & ALL_UMODES;

	set_snomask(client, NULL);
	if (value && *value)
		set_snomask(client, value);
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
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
}

static void udb_nick_snomask_apply_expression(Client *client, const char *expr)
{
	long old_umodes = client->umodes & ALL_UMODES;

	if (expr && *expr)
		set_snomask(client, expr);
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
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
}

/* The previous value is preserved so revocation can restore an external
 * snomask source; a value another source changed while UDB owned it is never
 * overwritten on revoke. */
static void udb_nick_effects_apply_snomasks(Client *client, UdbRecord *snomask_rec)
{
	UdbNickEffects *fx;
	const char *expr;
	const char *current;

	if (!client || !client->user || !snomask_rec || BadPtr(snomask_rec->data_str) || !MyUser(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	expr = snomask_rec->data_str;
	current = client->user->snomask ? client->user->snomask : "";

	if (fx->snomask_owned && fx->applied_snomask_expr && !strcmp(fx->applied_snomask_expr, expr))
	{
		const char *applied = fx->applied_snomask ? fx->applied_snomask : "";
		if (!strcmp(current, applied))
			return;
	}

	if (fx->snomask_owned)
	{
		/* The profile changed its desired value: release the old ownership
		 * first so an external replacement is preserved, not destroyed. */
		udb_nick_effects_revoke_snomasks(client);
		fx = udb_nick_effects_ensure(client);
		if (!fx)
			return;
	}

	safe_free(fx->previous_snomask);
	fx->previous_snomask = client->user->snomask ? strdup(client->user->snomask) : NULL;
	udb_nick_snomask_apply_expression(client, expr);
	safe_free(fx->applied_snomask);
	fx->applied_snomask = client->user->snomask ? strdup(client->user->snomask) : NULL;
	safe_free(fx->applied_snomask_expr);
	fx->applied_snomask_expr = strdup(expr);
	fx->snomask_owned = 1;
}

static void udb_nick_effects_revoke_snomasks(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);
	const char *current;
	const char *applied;

	if (!fx || !fx->snomask_owned)
		return;
	current = client->user && client->user->snomask ? client->user->snomask : "";
	applied = fx->applied_snomask ? fx->applied_snomask : "";
	if (!strcmp(current, applied))
		udb_nick_snomask_materialize(client, fx->previous_snomask ? fx->previous_snomask : "");
	/* Otherwise another source changed the value: preserve it. */
	safe_free(fx->previous_snomask);
	safe_free(fx->applied_snomask);
	safe_free(fx->applied_snomask_expr);
	fx->previous_snomask = NULL;
	fx->applied_snomask = NULL;
	fx->applied_snomask_expr = NULL;
	fx->snomask_owned = 0;
}

static void udb_nick_effects_apply_vhost(Client *client, UdbRecord *vhost_rec)
{
	UdbNickEffects *fx;
	const char *desired;

	if (!client || !client->user || !vhost_rec || BadPtr(vhost_rec->data_str) || !MyUser(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	desired = vhost_rec->data_str;
	udb_nick_effects_revoke_oper_vhost(client);

	if (fx->vhost_owned && !strcmp(fx->applied_vhost, desired))
	{
		/* Still materialized as UDB left it: nothing to do. If an external
		 * source replaced it, the profile still desires this value, so fall
		 * through and reassert it. */
		if (client->user->virthost && !strcmp(client->user->virthost, desired))
		{
			fx->oper_vhost_attempted = 0;
			return;
		}
	}
	else if (!fx->vhost_owned && client->user->virthost && !strcmp(client->user->virthost, desired) &&
			 IsHidden(client) && IsSetHost(client))
	{
		/* Desired vhost already active without a UDB mutation: no claim. */
		fx->oper_vhost_attempted = 1;
		return;
	}
	else if (fx->vhost_owned)
	{
		/* The profile changed its desired vhost: release the old value first,
		 * preserving it only when it is still the UDB-applied one. */
		udb_nick_effects_revoke_vhost(client);
		fx = udb_nick_effects_ensure(client);
		if (!fx)
			return;
	}

	udb_nick_set_vhost(client, desired);
	fx->vhost_owned = 1;
	fx->oper_vhost_attempted = 0;
	strlcpy(fx->applied_vhost, desired, sizeof(fx->applied_vhost));
}

static void udb_nick_effects_revoke_vhost(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);

	if (!fx || !fx->vhost_owned)
		return;
	if (client->user && client->user->virthost && fx->applied_vhost[0] &&
		!strcmp(client->user->virthost, fx->applied_vhost))
		udb_nick_remove_vhost(client);
	/* An external replacement is preserved. */
	fx->vhost_owned = 0;
	fx->applied_vhost[0] = '\0';
}

static int udb_nick_nullable_equal(const char *left, const char *right)
{
	if (!left || !*left)
		return !right || !*right;
	return right && !strcmp(left, right);
}

static int udb_nick_expected_oper_vhost(Client *client, const char *format, char *expected_vhost, size_t vhost_size,
										char *expected_username, size_t username_size, int *expected_sethost)
{
	char expanded[HOSTLEN + 1];
	char uhost[HOSTLEN + USERLEN + 1];
	char *hostpart;

	if (!client || !client->user || !expected_vhost || !expected_username || !expected_sethost)
		return 0;
	expected_vhost[0] = '\0';
	strlcpy(expected_username, client->user->username, username_size);
	*expected_sethost = IsSetHost(client) ? 1 : 0;

	if (format)
	{
		*expanded = '\0';
		unreal_expand_string(format, expanded, sizeof(expanded), NULL, 0, client);
		if (!valid_vhost(expanded))
			return 0;
		strlcpy(uhost, expanded, sizeof(uhost));
		hostpart = strchr(uhost, '@');
		if (hostpart)
		{
			*hostpart++ = '\0';
			strlcpy(expected_username, uhost, username_size);
		}
		else
		{
			hostpart = uhost;
		}
		strlcpy(expected_vhost, hostpart, vhost_size);
		*expected_sethost = 1;
		return 1;
	}

	if (((client->umodes | OPER_MODES) & UMODE_HIDE) && !client->user->virthost)
	{
		strlcpy(expected_vhost, client->user->cloakedhost, vhost_size);
		return 1;
	}
	return 0;
}

static int udb_nick_expand_oper_vhost_default(Client *client, const char *operclass, char *expanded, size_t size)
{
	NameValuePrioList *nvp = NULL;

	if (!client || BadPtr(operclass) || !expanded || !size || !iConf.oper_vhost)
		return 0;
	/* A regular /OPER stores its oper login before make_oper(), so these
	 * variables are already available when set::oper-vhost is expanded. UDB
	 * has no oper block to store, therefore provide the pending identity
	 * explicitly without publishing it as live client state before the grant. */
	add_nvplist(&nvp, -1, "operlogin", "UDB");
	add_nvplist(&nvp, -1, "operclass", operclass);
	unreal_expand_string(iConf.oper_vhost, expanded, size, nvp, 0, client);
	return 1;
}

static void udb_nick_effects_capture_oper_vhost(Client *client, const char *previous_vhost,
												const char *previous_username, int previous_sethost,
												const char *expected_vhost, const char *expected_username,
												int expected_sethost)
{
	UdbNickEffects *fx;
	int host_owned;
	int username_owned;

	if (!client || !client->user || !MyUser(client))
		return;
	host_owned = (!udb_nick_nullable_equal(previous_vhost, expected_vhost) || previous_sethost != expected_sethost) &&
				 udb_nick_nullable_equal(client->user->virthost, expected_vhost) &&
				 (IsSetHost(client) ? 1 : 0) == expected_sethost;
	username_owned = strcmp(previous_username, expected_username) && !strcmp(client->user->username, expected_username);
	if (!host_owned && !username_owned)
		return;

	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	if (host_owned)
	{
		safe_strdup(fx->oper_previous_vhost, previous_vhost);
		safe_strdup(fx->oper_applied_vhost, expected_vhost);
		fx->oper_previous_sethost = previous_sethost;
		fx->oper_vhost_owned = 1;
	}
	strlcpy(fx->oper_previous_username, previous_username, sizeof(fx->oper_previous_username));
	strlcpy(fx->oper_applied_username, expected_username, sizeof(fx->oper_applied_username));
	fx->oper_username_owned = username_owned;
}

static void udb_nick_effects_revoke_oper_vhost(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);
	const char *current_vhost;

	if (!fx || (!fx->oper_vhost_owned && !fx->oper_username_owned) || !client->user)
		return;
	current_vhost = client->user->virthost;
	if (fx->oper_vhost_owned && udb_nick_nullable_equal(current_vhost, fx->oper_applied_vhost))
	{
		if (fx->oper_previous_vhost)
		{
			userhost_save_current(client);
			safe_strdup(client->user->virthost, fx->oper_previous_vhost);
			if (fx->oper_previous_sethost)
				client->umodes |= UMODE_SETHOST;
			else
				client->umodes &= ~UMODE_SETHOST;
			sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, client->user->virthost);
			userhost_changed(client);
		}
		else
		{
			udb_nick_remove_vhost(client);
		}
	}
	if (fx->oper_username_owned && !strcmp(client->user->username, fx->oper_applied_username))
	{
		userhost_save_current(client);
		strlcpy(client->user->username, fx->oper_previous_username, sizeof(client->user->username));
		sendto_server(NULL, 0, 0, NULL, ":%s SETIDENT %s", client->id, client->user->username);
		userhost_changed(client);
	}

	safe_free(fx->oper_previous_vhost);
	safe_free(fx->oper_applied_vhost);
	fx->oper_previous_vhost = NULL;
	fx->oper_applied_vhost = NULL;
	fx->oper_previous_username[0] = '\0';
	fx->oper_applied_username[0] = '\0';
	fx->oper_username_owned = 0;
	fx->oper_previous_sethost = 0;
	fx->oper_vhost_owned = 0;
}

static void udb_nick_set_oper_vhost(Client *client, const char *format)
{
	char uhost[HOSTLEN + USERLEN + 1];
	char newhost[HOSTLEN + 1];
	char *hostpart;

	if (!client || !client->user || BadPtr(format))
		return;
	*newhost = '\0';
	unreal_expand_string(format, newhost, sizeof(newhost), NULL, 0, client);
	if (!valid_vhost(newhost))
		return;
	strlcpy(uhost, newhost, sizeof(uhost));
	hostpart = strchr(uhost, '@');
	if (hostpart)
	{
		*hostpart++ = '\0';
		strlcpy(client->user->username, uhost, sizeof(client->user->username));
		sendto_server(NULL, 0, 0, NULL, ":%s SETIDENT %s", client->id, client->user->username);
	}
	else
	{
		hostpart = uhost;
	}
	safe_strdup(client->user->virthost, hostpart);
	sendto_server(NULL, 0, 0, NULL, ":%s SETHOST :%s", client->id, client->user->virthost);
	client->umodes |= UMODE_SETHOST | UMODE_HIDE;
}

static void udb_nick_effects_apply_oper_vhost_default(Client *client)
{
	UdbNickEffects *fx;
	char *previous_vhost;
	char previous_username[USERLEN + 1];
	char expected_vhost[HOSTLEN + 1];
	char expected_username[USERLEN + 1];
	char expanded_default[HOSTLEN + 1];
	const char *operclass;
	const char *format;
	long old_umodes;
	int previous_sethost;
	int expected_sethost;

	if (!client || !client->user || !MyUser(client) || !IsOper(client) || !udb_oper_owned(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx || fx->oper_vhost_attempted || fx->oper_vhost_owned || fx->oper_username_owned)
		return;
	/* One default attempt belongs to the oper grant (or to the transition
	 * that removed an owned N::vhost). An external replacement must not be
	 * overwritten by unrelated later profile reconciliation. */
	fx->oper_vhost_attempted = 1;
	operclass = get_operclass(client);
	format = udb_nick_expand_oper_vhost_default(client, operclass, expanded_default, sizeof(expanded_default))
				 ? expanded_default
				 : NULL;
	if (!udb_nick_expected_oper_vhost(client, format, expected_vhost, sizeof(expected_vhost), expected_username,
									  sizeof(expected_username), &expected_sethost))
		return;

	previous_vhost = client->user->virthost ? strdup(client->user->virthost) : NULL;
	strlcpy(previous_username, client->user->username, sizeof(previous_username));
	previous_sethost = IsSetHost(client) ? 1 : 0;
	old_umodes = client->umodes & ALL_UMODES;
	userhost_save_current(client);
	if (format)
		udb_nick_set_oper_vhost(client, format);
	else if (IsHidden(client) && !client->user->virthost)
		safe_strdup(client->user->virthost, client->user->cloakedhost);
	userhost_changed(client);
	udb_nick_effects_capture_oper_vhost(client, previous_vhost, previous_username, previous_sethost, expected_vhost,
										expected_username, expected_sethost);
	safe_free(previous_vhost);
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
}

static void udb_nick_effects_apply_swhois(Client *client, UdbRecord *swhois_rec)
{
	UdbNickEffects *fx;

	if (!client || !client->user || !swhois_rec || BadPtr(swhois_rec->data_str) || !MyUser(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	swhois_delete(client, "udb", "*", &me, NULL);
	swhois_add(client, "udb", 100, swhois_rec->data_str, &me, NULL);
	fx->swhois_owned = 1;
}

static void udb_nick_effects_revoke_swhois(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);

	if (!fx || !fx->swhois_owned)
		return;
	swhois_delete(client, "udb", "*", &me, NULL);
	fx->swhois_owned = 0;
}

/* Revocation uses only runtime ownership metadata; it never inspects the
 * current profile to decide what UDB may remove. */
static void udb_nick_revoke_effects(Client *client)
{
	if (!client || !MyUser(client))
		return;
	udb_nick_revoke_oper(client);
	udb_nick_effects_revoke_modes(client);
	udb_nick_effects_revoke_vhost(client);
	udb_nick_effects_revoke_swhois(client);
	udb_nick_effects_revoke_snomasks(client);
	udb_nick_effects_clear(client);
}

/* Desired state comes from the profile; owned state comes from UdbNickEffects.
 * Reconcile removes owned effects no longer desired, preserves external state
 * UDB never owned, and records exactly what UDB changes. */
static void udb_nick_reconcile_effects(Client *client, UdbRecord *nick_rec)
{
	UdbRecord *effect_rec;

	if (!client || !MyUser(client) || !nick_rec)
		return;

	effect_rec = udb_record_find(udb_ctx, NKEY_VHOST, nick_rec);
	if (effect_rec && effect_rec->data_str)
		udb_nick_effects_apply_vhost(client, effect_rec);
	else
	{
		udb_nick_effects_revoke_vhost(client);
		udb_nick_effects_apply_oper_vhost_default(client);
	}

	effect_rec = udb_record_find(udb_ctx, NKEY_OPER, nick_rec);
	if (effect_rec)
	{
		if (!udb_nick_grant_oper(client, nick_rec, effect_rec))
			return;
	}
	else
		udb_nick_revoke_oper(client);

	effect_rec = udb_record_find(udb_ctx, NKEY_MODES, nick_rec);
	if (effect_rec && effect_rec->data_str)
		udb_nick_effects_apply_modes(client, effect_rec);
	else
		udb_nick_effects_revoke_modes(client);

	effect_rec = udb_record_find(udb_ctx, NKEY_SWHOIS, nick_rec);
	if (effect_rec && effect_rec->data_str)
		udb_nick_effects_apply_swhois(client, effect_rec);
	else
		udb_nick_effects_revoke_swhois(client);

	effect_rec = udb_record_find(udb_ctx, NKEY_SNOMASKS, nick_rec);
	if (effect_rec && effect_rec->data_str)
		udb_nick_effects_apply_snomasks(client, effect_rec);
	else
		udb_nick_effects_revoke_snomasks(client);
}

static void udb_nick_set_vhost(Client *client, const char *vhost)
{
	if (!client || !client->user || !vhost || !*vhost)
		return;

	userhost_save_current(client);
	safe_strdup(client->user->virthost, vhost);
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

static int udb_oper_owned(Client *client)
{
	if (!client || !udb_nick_oper_owned_md)
		return 0;
	return moddata_local_client(client, udb_nick_oper_owned_md).i ? 1 : 0;
}

static int udb_nick_grant_oper(Client *client, UdbRecord *nick_rec, UdbRecord *oper_rec)
{
	UdbNickEffects *fx;
	const char *operclass;
	UdbRecord *vhost_rec;
	const char *oper_vhost;
	char *previous_vhost;
	char previous_username[USERLEN + 1];
	char expected_vhost[HOSTLEN + 1];
	char expected_username[USERLEN + 1];
	char expanded_default[HOSTLEN + 1];
	int previous_sethost;
	int expected_sethost = 0;
	int expect_oper_vhost = 0;
	int has_profile_vhost;

	if (!client || !oper_rec)
		return 1;

	operclass = oper_rec->data_str;
	if (BadPtr(operclass))
		return 1;

	if (!find_operclass(operclass))
	{
		udb_log(ULOG_WARNING, "UDB_OPERCLASS_NOT_FOUND", client,
				"operclass '$operclass' for $client.details does not exist in unrealircd.conf",
				log_data_string("operclass", operclass));
		return 1;
	}

	if (IsOper(client) && !udb_oper_owned(client))
	{
		/* external oper: do not touch */
		return 1;
	}

	if (udb_oper_owned(client))
	{
		const char *curr_class = get_operclass(client);
		if (curr_class && !strcmp(curr_class, operclass))
			return 1;
		udb_nick_revoke_oper(client);
	}

	if (!IsOper(client))
	{
		vhost_rec = nick_rec ? udb_record_find(udb_ctx, NKEY_VHOST, nick_rec) : NULL;
		has_profile_vhost = vhost_rec && !BadPtr(vhost_rec->data_str);
		if (has_profile_vhost)
			oper_vhost = vhost_rec->data_str;
		else if (udb_nick_expand_oper_vhost_default(client, operclass, expanded_default, sizeof(expanded_default)))
			oper_vhost = expanded_default;
		else
			oper_vhost = NULL;
		previous_vhost = (client->user && client->user->virthost) ? strdup(client->user->virthost) : NULL;
		strlcpy(previous_username, client->user->username, sizeof(previous_username));
		previous_sethost = IsSetHost(client) ? 1 : 0;
		expect_oper_vhost =
			udb_nick_expected_oper_vhost(client, oper_vhost, expected_vhost, sizeof(expected_vhost), expected_username,
										 sizeof(expected_username), &expected_sethost);
		if (!make_oper(client, "UDB", operclass, NULL, 0, NULL, oper_vhost, NULL))
		{
			safe_free(previous_vhost);
			return 0;
		}
		if (IsOper(client) && udb_nick_oper_owned_md)
		{
			moddata_local_client(client, udb_nick_oper_owned_md).i = 1;
			fx = udb_nick_effects_ensure(client);
			if (fx && !has_profile_vhost)
				fx->oper_vhost_attempted = 1;
			if (expect_oper_vhost)
				udb_nick_effects_capture_oper_vhost(client, previous_vhost, previous_username, previous_sethost,
													expected_vhost, expected_username, expected_sethost);
			/* N::vhost is a literal profile effect. make_oper() treats its vhost
			 * argument as an expandable ident@host template, so undo only the
			 * transformation that survived synchronous hooks. The normal profile
			 * marker remains responsible for the literal value. */
			if (has_profile_vhost)
				udb_nick_effects_revoke_oper_vhost(client);
		}
		safe_free(previous_vhost);
	}
	return 1;
}

static void udb_nick_revoke_oper(Client *client)
{
	UdbNickEffects *fx;
	long old_umodes;

	if (!client || !MyUser(client) || !udb_oper_owned(client))
		return;

	if (!IsOper(client))
	{
		/* Oper status was removed elsewhere: UDB keeps no claim over it. */
		old_umodes = client->umodes & ALL_UMODES;
		moddata_local_client(client, udb_nick_oper_owned_md).i = 0;
		udb_nick_effects_revoke_oper_vhost(client);
		fx = udb_nick_effects_get(client);
		if (fx)
			fx->oper_vhost_attempted = 0;
		if ((client->umodes & ALL_UMODES) != old_umodes)
			send_umode_out(client, 1, old_umodes);
		return;
	}

	old_umodes = client->umodes & ALL_UMODES;
	client->umodes &= ~UMODE_OPER;
	if (client->umodes & UMODE_HIDEOPER)
	{
		client->umodes &= ~UMODE_HIDEOPER;
	}
	else if (irccounts.operators > 0)
	{
		irccounts.operators--;
	}

	if (MyUser(client) && !list_empty(&client->special_node))
	{
		list_del(&client->special_node);
		INIT_LIST_HEAD(&client->special_node);
	}
	if (MyUser(client))
		RunHook(HOOKTYPE_LOCAL_OPER, client, 0, NULL, NULL);
	remove_oper_privileges(client, 0);
	moddata_local_client(client, udb_nick_oper_owned_md).i = 0;
	udb_nick_effects_revoke_oper_vhost(client);
	fx = udb_nick_effects_get(client);
	if (fx)
		fx->oper_vhost_attempted = 0;
	send_umode_out(client, 1, old_umodes);
}

static void udb_nick_force_rename_with_notice(Client *client, const char *nick_in_db, const char *notice,
											  int announce_profile, int offer_identify)
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
	else if (announce_profile)
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
	udb_nick_force_rename_with_notice(client, nick_in_db, NULL, 1, 1);
}

static void udb_nick_force_rename_after_unsuspend(Client *client, const char *nick_in_db)
{
	char notice[256];

	snprintf(notice, sizeof(notice), "The nickname %s is no longer suspended.", nick_in_db);
	udb_nick_force_rename_with_notice(client, nick_in_db, notice, 0, 1);
}

static void udb_nick_apply(Client *client, UdbRecord *nick_rec, UdbNickApplyReason reason)
{
	UdbRecord *forbid;
	UdbRecord *pass_rec;
	UdbRecord *suspend;
	char notice[UDB_RECORD_VALUE_MAX + 64];

	if (!client || !nick_rec)
		return;
	forbid = udb_record_find(udb_ctx, NKEY_FORBID, nick_rec);
	if (forbid)
	{
		/* A forbid always wins: revoke UDB-owned state before the rename. */
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		snprintf(notice, sizeof(notice), "This nickname is forbidden. Reason: %s",
				 forbid->data_str ? forbid->data_str : "No reason given");
		udb_nick_force_rename_with_notice(client, nick_rec->key, notice, 0, 0);
		return;
	}
	pass_rec = udb_record_find(NULL, NKEY_PASS, nick_rec);
	suspend = udb_record_find(udb_ctx, NKEY_SUSPEND, nick_rec);

	/* Access denial is fail-closed for adoption and refresh alike. UDB only
	 * removes effects it actually owned; a passless profile never had any. */
	if (!udb_nick_access_allowed(client, nick_rec))
	{
		udb_nick_password_cache_clear(client);
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		snprintf(notice, sizeof(notice), "Access to %s is not permitted from your IP address.", nick_rec->key);
		udb_nick_force_rename_with_notice(client, nick_rec->key, notice, 0, 0);
		return;
	}

	/* A profile without a credential controls only nick use. It can never turn
	 * a matching nick, account or +r into UDB identity or profile effects, and
	 * it never creates an ownership marker of its own. */
	if (!pass_rec)
	{
		udb_nick_password_cache_clear(client);
		udb_nick_revoke_identity(client);
		if (suspend)
			udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s",
									suspend->data_str ? suspend->data_str : "No reason given");
		return;
	}

	/* A refreshed suspension revokes identity/effects without requiring a new
	 * authentication: the nick may stay occupied but no auth survives it. */
	if (reason == UDB_NICK_APPLY_REFRESH && suspend)
	{
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s",
								suspend->data_str ? suspend->data_str : "No reason given");
		return;
	}

	/* A protected profile materializes identity only with a valid active UDB
	 * proof. SVSNICK and forced changes never carry one, and credential/access
	 * values are checked live, so a holder without proof is renamed away. */
	if (!udb_nick_identity_valid(client, nick_rec))
	{
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		if (reason == UDB_NICK_APPLY_UNSUSPEND)
			udb_nick_force_rename_after_unsuspend(client, nick_rec->key);
		else
			udb_nick_force_rename(client, nick_rec->key);
		return;
	}

	/* Adoption of a suspended profile validates pass/access but materializes
	 * nothing: no identity, account/+r or effects, and no revoke of state UDB
	 * never applied. */
	if (reason == UDB_NICK_APPLY_ADOPT && suspend)
	{
		udb_nick_revoke_identity(client);
		udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s",
								suspend->data_str ? suspend->data_str : "No reason given");
		return;
	}

	/* Materialize the public identity projection before its effects, then let
	 * the reconciler decide effect-by-effect against runtime ownership. */
	if (client->user)
		strlcpy(client->user->account, nick_rec->key, sizeof(client->user->account));

	long old_umodes = client->umodes & ALL_UMODES;
	client->umodes |= UMODE_REGNICK;
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);

	udb_nick_reconcile_effects(client, nick_rec);
}

static void udb_nick_adopt(Client *client, UdbRecord *nick_rec)
{
	int authenticated_now = udb_nick_pending_auth_consume(client, nick_rec);

	if (authenticated_now)
		udb_nick_identity_set(client, nick_rec);
	udb_nick_apply(client, nick_rec, UDB_NICK_APPLY_ADOPT);
	if (authenticated_now && udb_nick_identity_valid(client, nick_rec))
		udb_send_service_notice(client, SKEY_NICKSERV, "You are now identified for nickname %s.", nick_rec->key);
}

/* Common DEL/removal primitive for a child record. Effect removal is expressed
 * through the ownership marker, never through the record being deleted; the
 * candidate profile is reapplied right after this by the common transaction. */
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
				udb_nick_revoke_effects(client);
				udb_nick_revoke_identity(client);
				udb_nick_pending_auth_clear(client);
			}
			else if (!strcmp(rec->key, NKEY_VHOST))
			{
				udb_nick_effects_revoke_vhost(client);
			}
			else if (!strcmp(rec->key, NKEY_OPER))
			{
				udb_nick_revoke_oper(client);
			}
			else if (!strcmp(rec->key, NKEY_SWHOIS))
			{
				udb_nick_effects_revoke_swhois(client);
			}
			else if (!strcmp(rec->key, NKEY_MODES))
			{
				udb_nick_effects_revoke_modes(client);
			}
			else if (!strcmp(rec->key, NKEY_SNOMASKS))
			{
				udb_nick_effects_revoke_snomasks(client);
			}
			/* NKEY_ACCESS and NKEY_SUSPEND are policy markers: the candidate
			 * reapply revokes identity when the policy no longer matches. */
		}
	}
	else
	{
		Client *client = find_user(rec->key, NULL);
		if (client && MyUser(client))
		{
			UdbRecord *candidate =
				udb_nick_replacement_tree ? udb_record_find(NULL, rec->key, udb_nick_replacement_tree) : NULL;
			int unsuspended = candidate && udb_record_find(udb_ctx, NKEY_SUSPEND, rec) &&
							  !udb_record_find(udb_ctx, NKEY_SUSPEND, candidate);
			/* Active identity survives a full replacement only for an existing
			 * candidate with a credential, without forbid or suspend, and with
			 * a still-permitted access check. A deleted profile, a removed
			 * credential or a normal->suspend transition revokes it; changed
			 * pass/access values that still permit the holder do not.
			 * Equivalent candidates keep both markers and the candidate
			 * reapply reconciles effects. */
			int keep_identity = candidate && !udb_record_find(udb_ctx, NKEY_FORBID, candidate) &&
								!udb_record_find(udb_ctx, NKEY_SUSPEND, candidate) &&
								udb_nick_identity_valid(client, candidate);

			if (unsuspended)
				candidate->nick_unsuspend_transition = 1;

			udb_nick_pending_auth_clear(client);
			if (!keep_identity)
			{
				udb_nick_revoke_effects(client);
				udb_nick_revoke_identity(client);
			}
		}
	}
}

static UdbPasswordFailure *udb_password_failure_find(UdbRecord *profile_rec, Client *client, int create, int *saturated)
{
	UdbPasswordFailure *free_slot = NULL;
	const char *ip = client ? client->ip : NULL;
	time_t now = TStime();
	int period = udb_cfg ? udb_cfg->flood_period : 60;
	unsigned int i;

	if (period <= 0)
		period = 60;
	if (saturated)
		*saturated = 0;
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
		if (!entry->since && !free_slot)
			free_slot = entry;
	}
	if (!free_slot)
	{
		if (saturated)
			*saturated = 1;
		return NULL;
	}
	if (!create)
		return NULL;
	memset(free_slot, 0, sizeof(*free_slot));
	strlcpy(free_slot->profile, profile_rec->key, sizeof(free_slot->profile));
	strlcpy(free_slot->ip, ip, sizeof(free_slot->ip));
	free_slot->block_idx = profile_rec->block_idx;
	free_slot->since = now;
	return free_slot;
}

static int udb_password_flooded(UdbRecord *profile_rec, Client *client)
{
	int saturated = 0;
	UdbPasswordFailure *entry = udb_password_failure_find(profile_rec, client, 0, &saturated);
	int attempts = udb_cfg ? udb_cfg->flood_attempts : 5;

	if (attempts <= 0)
		attempts = 5;
	return saturated || (entry && entry->attempts >= (unsigned int)attempts);
}

static void udb_password_failure_record(UdbRecord *profile_rec, Client *client, int success)
{
	UdbPasswordFailure *entry = udb_password_failure_find(profile_rec, client, !success, NULL);

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
		/* Leaving a profile revokes exactly the runtime state UDB owns. A
		 * dormant/passless/suspended profile owns nothing and must never
		 * revoke externally supplied state on departure. */
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
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
		udb_nick_adopt(client, new_rec);
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
		udb_nick_adopt(client, nick_rec);
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
	mreq.name = "udb_nick_effects";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_effects_free;
	udb_nick_effects_md = ModDataAdd(modinfo->handle, mreq);
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
