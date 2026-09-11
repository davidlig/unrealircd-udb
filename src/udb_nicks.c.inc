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
	int valid;
};

typedef struct UdbNickSuspendAuth UdbNickSuspendAuth;
struct UdbNickSuspendAuth
{
	char nick[NICKLEN + 1];
	char policy_digest[65];
};

static ModDataInfo *udb_nick_password_cache_md = NULL;
static ModDataInfo *udb_nick_suspend_auth_md = NULL;
static ModDataInfo *udb_nick_oper_owned_md = NULL;
/* Only set during a synchronous N-tree replacement; never retained per client. */
static UdbRecord *udb_nick_suspend_auth_replacement_tree = NULL;

static void udb_nick_password_cache_free(ModData *m)
{
	safe_free(m->ptr);
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
		safe_free(cache);
		moddata_local_client(client, udb_nick_password_cache_md).ptr = NULL;
	}
}

static void udb_nick_password_cache_set(Client *client, const char *nick)
{
	UdbNickPasswordCache *cache;

	if (!client || !nick || !udb_nick_password_cache_md)
		return;
	udb_nick_password_cache_clear(client);
	cache = safe_alloc(sizeof(*cache));
	strlcpy(cache->nick, nick, sizeof(cache->nick));
	cache->valid = 1;
	moddata_local_client(client, udb_nick_password_cache_md).ptr = cache;
}

static int udb_nick_password_cache_take(Client *client, const char *nick)
{
	UdbNickPasswordCache *cache;
	int valid;

	if (!client || !nick || !udb_nick_password_cache_md)
		return 0;
	cache = moddata_local_client(client, udb_nick_password_cache_md).ptr;
	if (!cache)
		return 0;
	if (strcasecmp(cache->nick, nick))
	{
		udb_nick_password_cache_clear(client);
		return 0;
	}
	valid = cache->valid;
	udb_nick_password_cache_clear(client);
	return valid;
}

static void udb_nick_suspend_auth_free(ModData *m)
{
	safe_free(m->ptr);
	m->ptr = NULL;
}

static void udb_nick_suspend_auth_clear(Client *client)
{
	UdbNickSuspendAuth *auth;

	if (!client || !udb_nick_suspend_auth_md)
		return;
	auth = moddata_local_client(client, udb_nick_suspend_auth_md).ptr;
	if (auth)
	{
		safe_free(auth);
		moddata_local_client(client, udb_nick_suspend_auth_md).ptr = NULL;
	}
}

/* Feed distinct labels, presence bits and fixed-width lengths to SHA-256 so
 * distinct pass/challenge/access policies cannot share an ambiguous encoding. */
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
	const char *challenge = NULL;
	const char *access = NULL;
	unsigned int i;

	if (!nick_rec || !out)
		return 0;
	rec = udb_record_find(udb_ctx, NKEY_PASS, nick_rec);
	if (rec)
		pass = rec->data_str;
	rec = udb_record_find(udb_ctx, NKEY_CHALLENGE, nick_rec);
	if (rec)
		challenge = rec->data_str;
	rec = udb_record_find(udb_ctx, NKEY_ACCESS, nick_rec);
	if (rec)
		access = rec->data_str;
	ctx = EVP_MD_CTX_new();
	if (!ctx)
		return 0;
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 || !udb_nick_auth_policy_digest_field(ctx, 'P', pass) ||
		!udb_nick_auth_policy_digest_field(ctx, 'C', challenge) ||
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

static void udb_nick_suspend_auth_set(Client *client, UdbRecord *nick_rec)
{
	UdbNickSuspendAuth *auth;

	if (!client || !MyConnect(client) || !nick_rec || BadPtr(nick_rec->key) || !udb_nick_suspend_auth_md)
		return;
	auth = safe_alloc(sizeof(*auth));
	if (!udb_nick_auth_policy_digest(nick_rec, auth->policy_digest))
	{
		safe_free(auth);
		return;
	}
	strlcpy(auth->nick, nick_rec->key, sizeof(auth->nick));
	udb_nick_suspend_auth_clear(client);
	moddata_local_client(client, udb_nick_suspend_auth_md).ptr = auth;
}

static int udb_nick_suspend_auth_valid(Client *client, UdbRecord *nick_rec)
{
	UdbNickSuspendAuth *auth;
	char digest[65];

	if (!client || !nick_rec || BadPtr(nick_rec->key) || !udb_nick_suspend_auth_md)
		return 0;
	auth = moddata_local_client(client, udb_nick_suspend_auth_md).ptr;
	if (!auth || strcasecmp(auth->nick, nick_rec->key) || strcasecmp(client->name, auth->nick) ||
		!udb_nick_auth_policy_digest(nick_rec, digest) || strcmp(auth->policy_digest, digest) ||
		!udb_nick_access_allowed(client, nick_rec))
		return 0;
	return 1;
}

static void udb_nick_suspend_auth_prepare_tree_replace(UdbBlock *block, UdbRecord *candidate_tree)
{
	udb_nick_suspend_auth_replacement_tree = (block && block->letter == UDB_BLOCK_NICKS) ? candidate_tree : NULL;
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

static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync)
{
	UdbRecord *forbid;
	UdbRecord *suspend;

	if (!client || !nick_rec)
		return;
	forbid = udb_record_find(udb_ctx, NKEY_FORBID, nick_rec);
	if (forbid)
	{
		udb_nick_suspend_auth_clear(client);
		char notice[UDB_RECORD_VALUE_MAX + 64];
		snprintf(notice, sizeof(notice), "This nickname is forbidden. Reason: %s",
				 forbid->data_str ? forbid->data_str : "No reason given");
		udb_nick_force_rename_with_notice(client, nick_rec->key, notice, 0);
		return;
	}
	suspend = udb_record_find(udb_ctx, NKEY_SUSPEND, nick_rec);

	/* A hot replacement may trust either the public identity currently owned by
	 * this profile or a policy-bound authentication retained through suspend. */
	int matching_account = client->user && has_user_mode(client, 'r') && strcmp(client->user->account, "*") &&
						   !strcasecmp(client->user->account, nick_rec->key);
	int saved_auth = udb_nick_suspend_auth_valid(client, nick_rec);
	if (is_hot_sync && !(matching_account || saved_auth))
	{
		UdbRecord *pass_rec = udb_record_find(udb_ctx, NKEY_PASS, nick_rec);
		if (pass_rec)
			udb_nick_force_rename(client, nick_rec->key);
		return;
	}

	if (suspend)
	{
		/* Capture a verified public owner before suspension clears identity. */
		if (matching_account)
			udb_nick_suspend_auth_set(client, nick_rec);
		udb_nick_strip(client, nick_rec);
		udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s",
								suspend->data_str ? suspend->data_str : "No reason given");
		return;
	}

	if (client->user)
		strlcpy(client->user->account, nick_rec->key, sizeof(client->user->account));

	long old_umodes = client->umodes & ALL_UMODES;
	client->umodes |= UMODE_REGNICK;
	send_umode_out(client, 1, old_umodes);

	UdbRecord *vhost_rec = udb_record_find(udb_ctx, NKEY_VHOST, nick_rec);
	if (vhost_rec)
		udb_nick_set_vhost(client, vhost_rec);
	UdbRecord *oper_rec = udb_record_find(udb_ctx, NKEY_OPER, nick_rec);
	if (oper_rec)
		udb_nick_grant_oper(client, nick_rec, oper_rec);
	UdbRecord *modes_rec = udb_record_find(udb_ctx, NKEY_MODES, nick_rec);
	if (modes_rec && modes_rec->data_str)
		udb_nick_set_modes(client, nick_rec, modes_rec, modes_rec->data_str);
	UdbRecord *swhois_rec = udb_record_find(udb_ctx, NKEY_SWHOIS, nick_rec);
	if (swhois_rec)
		udb_nick_set_swhois(client, nick_rec, swhois_rec);
	UdbRecord *sno_rec = udb_record_find(udb_ctx, NKEY_SNOMASKS, nick_rec);
	if (sno_rec)
		udb_nick_set_snomasks(client, nick_rec, sno_rec);
}

static void udb_nick_strip(Client *client, UdbRecord *nick_rec)
{
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

	set_snomask(client, NULL);

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
			else if (!strcmp(rec->key, NKEY_PASS))
			{
				udb_nick_suspend_auth_clear(client);
				udb_nick_strip(client, nick_rec);
			}
			else if (!strcmp(rec->key, NKEY_CHALLENGE) || !strcmp(rec->key, NKEY_ACCESS))
			{
				udb_nick_suspend_auth_clear(client);
			}
		}
	}
	else
	{
		Client *client = find_user(rec->key, NULL);
		if (client && MyUser(client))
		{
			UdbRecord *candidate = udb_nick_suspend_auth_replacement_tree
									   ? udb_record_find(NULL, rec->key, udb_nick_suspend_auth_replacement_tree)
									   : NULL;
			/* A deleted profile revokes auth. A full replacement retains it only
			 * when the candidate has the same valid policy-bound proof. */
			if (!candidate || !udb_nick_suspend_auth_valid(client, candidate))
				udb_nick_suspend_auth_clear(client);
			udb_nick_strip(client, rec);
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

static int udb_password_type(const char *challenge, const char *stored_pass, const char **hash)
{
	if (!strncmp(stored_pass, "argon2id:", 9))
	{
		*hash = stored_pass + 9;
		return !strcasecmp(challenge, "argon2id") || !*challenge ? AUTHTYPE_ARGON2 : AUTHTYPE_INVALID;
	}
	if (!strncmp(stored_pass, "sha256:", 7))
	{
		*hash = stored_pass + 7;
		return !strcasecmp(challenge, "sha256") || !*challenge ? UDB_AUTHTYPE_SHA256 : AUTHTYPE_INVALID;
	}
	if (!strncmp(stored_pass, "crypt:", 6))
	{
		*hash = stored_pass + 6;
		return !strcasecmp(challenge, "crypt") || !*challenge ? AUTHTYPE_UNIXCRYPT : AUTHTYPE_INVALID;
	}
	*hash = stored_pass;
	if (!strcasecmp(challenge, "argon2id"))
		return AUTHTYPE_ARGON2;
	if (!strcasecmp(challenge, "sha256"))
		return UDB_AUTHTYPE_SHA256;
	if (!strcasecmp(challenge, "crypt"))
		return AUTHTYPE_UNIXCRYPT;
	if (!*challenge && !strncmp(stored_pass, "$argon2id$", 10))
		return AUTHTYPE_ARGON2;
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
	UdbRecord *chall_rec;
	const char *challenge = "";
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
	chall_rec = udb_record_find(udb_ctx, NKEY_CHALLENGE, profile_rec);
	if (chall_rec && chall_rec->data_str)
		challenge = chall_rec->data_str;
	stored_pass = pass_rec->data_str;
	type = udb_password_type(challenge, stored_pass, &hash);
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
	if (parc <= 1)
		goto passthrough;

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

	if (client->local)
		safe_strdup(client->local->passwd, pass);

	Client *acptr = rec ? find_client(clean_nick, NULL) : NULL;

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
		udb_nick_password_cache_set(client, clean_nick);
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

	const char *new_parv[MAXPARA + 1];
	for (int i = 0; i < parc; i++)
		new_parv[i] = parv[i];
	new_parv[1] = clean_nick;
	CallCommandOverride(ovr, clictx, client, recv_mtags, parc, new_parv);
	return;

passthrough:
	CALL_NEXT_COMMAND_OVERRIDE();
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
		if (forbid)
		{
			static char forbid_reject[UDB_RECORD_VALUE_MAX + 64];
			snprintf(forbid_reject, sizeof(forbid_reject), "This nickname is forbidden. Reason: %s",
					 forbid->data_str ? forbid->data_str : "No reason given");
			*reject_reason = forbid_reject;
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

		if (udb_nick_password_cache_take(client, newnick))
		{
			udb_nick_suspend_auth_set(client, nick_rec);
			return HOOK_CONTINUE;
		}

		const char *pass = client->local ? client->local->passwd : NULL;
		if (!pass)
		{
			udb_send_service_notice(client, SKEY_NICKSERV,
									"Nickname is unavailable: This nick is registered and requires a password and an "
									"authorized IP. Use /NICK %s:Password",
									newnick);
		}
		else if (udb_check_password(pass, nick_rec, client))
		{
			if (udb_nick_access_allowed(client, nick_rec))
			{
				udb_nick_suspend_auth_set(client, nick_rec);
				return HOOK_CONTINUE;
			}
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									newnick);
		}
		else
		{
			udb_nick_password_failure_notice(client, nick_rec, newnick);
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
		udb_nick_suspend_auth_clear(client);
		udb_nick_strip(client, old_rec);
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
		udb_nick_apply(client, new_rec, 0);
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
		udb_nick_apply(client, nick_rec, 0);
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
	mreq.name = "udb_nick_suspend_auth";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_suspend_auth_free;
	udb_nick_suspend_auth_md = ModDataAdd(modinfo->handle, mreq);
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
