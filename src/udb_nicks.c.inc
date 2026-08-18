#include <openssl/evp.h>

static void udb_nick_set_vhost(Client *client, UdbRecord *vhost_rec) {
    if (!vhost_rec || !vhost_rec->data_str) return;
    userhost_save_current(client);
    safe_strdup(client->user->virthost, vhost_rec->data_str);
    userhost_changed(client);
}

static void udb_nick_grant_oper(Client *client, UdbRecord *nick_rec, UdbRecord *oper_rec) {
    if (!oper_rec) return;
    if (IsOper(client)) return; /* Already oper, do not add to oper_list again! */
    
    unsigned long level = oper_rec->data_num;
    const char *operclass = NULL;
    
    if (level & UDB_OPER_ROOT) {
        operclass = "netadmin";
    } else if (level & UDB_OPER_ADMIN) {
        operclass = "admin";
    } else if (level & UDB_OPER_HELPER) {
        operclass = "locop";
    }
    
    if (operclass) {
        make_oper(client, "UDB", operclass, NULL, UMODE_OPER, NULL, NULL, NULL);
    }
}

static void udb_nick_set_modes(Client *client, UdbRecord *nick_rec, UdbRecord *mode_rec, const char *modes) {
    if (!modes) return;
    long m = set_usermode(modes);
    long old_umodes = client->umodes & ALL_UMODES;
    client->umodes |= m;
    send_umode_out(client, 1, old_umodes);
}

static void udb_nick_set_swhois(Client *client, UdbRecord *nick_rec, UdbRecord *swhois_rec) {
    if (!swhois_rec || !swhois_rec->data_str) return;
    swhois_add(client, "udb", 100, swhois_rec->data_str, &me, NULL);
}

static void udb_nick_set_snomasks(Client *client, UdbRecord *nick_rec, UdbRecord *snomask_rec) {
    if (!snomask_rec || !snomask_rec->data_str) return;
    set_snomask(client, snomask_rec->data_str);
}

static void udb_nick_force_rename(Client *client, const char *nick_in_db) {
    char newnick[32];
    char rand_suffix[6];
    
    gen_random_alnum(rand_suffix, 5);
    rand_suffix[5] = '\0';
    snprintf(newnick, sizeof(newnick), "Guest%s", rand_suffix);
    
    sendnotice(client, "This nickname (%s) has been registered or synced in the UDB database.", nick_in_db);
    sendnotice(client, "You have been renamed. If you are the owner, please identify: /NICK %s:Password", nick_in_db);

    const char *args[5];
    char tsbuf[32];
    snprintf(tsbuf, sizeof(tsbuf), "%lld", (long long)TStime());
    args[0] = NULL;
    args[1] = client->name;
    args[2] = newnick;
    args[3] = tsbuf;
    args[4] = NULL;

    do_cmd(&me, NULL, "SVSNICK", 4, args);
}

static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync) {
    if (!client || !nick_rec) return;

    /* If this is a hot sync, check if the user is identified */
    if (is_hot_sync) {
        UdbRecord *pass_rec = udb_record_find(NKEY_PASS, nick_rec);
        if (pass_rec && !has_user_mode(client, 'r')) {
            udb_nick_force_rename(client, nick_rec->key);
            return; /* Abort applying vhosts/opers to this unauthorized user */
        }
    }
    
    if (client->user) {
        strlcpy(client->user->account, nick_rec->key, sizeof(client->user->account));
    }
    
    long old_umodes = client->umodes & ALL_UMODES;
    client->umodes |= UMODE_REGNICK;
    
    UdbRecord *susp = udb_record_find(NKEY_SUSPENDED, nick_rec);
    if (susp) {
        client->umodes |= set_usermode("S");
    }
    
    send_umode_out(client, 1, old_umodes);
    
    UdbRecord *vhost_rec = udb_record_find(NKEY_VHOST, nick_rec);
    if (vhost_rec) udb_nick_set_vhost(client, vhost_rec);
    
    UdbRecord *oper_rec = udb_record_find(NKEY_OPER, nick_rec);
    if (oper_rec) udb_nick_grant_oper(client, nick_rec, oper_rec);
    
    UdbRecord *modes_rec = udb_record_find(NKEY_MODES, nick_rec);
    if (modes_rec && modes_rec->data_str) {
        udb_nick_set_modes(client, nick_rec, modes_rec, modes_rec->data_str);
    }
    
    UdbRecord *swhois_rec = udb_record_find(NKEY_SWHOIS, nick_rec);
    if (swhois_rec) udb_nick_set_swhois(client, nick_rec, swhois_rec);
    
    UdbRecord *sno_rec = udb_record_find(NKEY_SNOMASKS, nick_rec);
    if (sno_rec) udb_nick_set_snomasks(client, nick_rec, sno_rec);
}

static void udb_nick_strip(Client *client, UdbRecord *nick_rec) {
    if (!client) return;
    
    if (client->user) {
        strlcpy(client->user->account, "*", sizeof(client->user->account));
    }
    
    if (IsOper(client)) {
        if (MyUser(client) && !list_empty(&client->special_node)) {
            list_del(&client->special_node);
            INIT_LIST_HEAD(&client->special_node);
        }
        if (irccounts.operators > 0)
            irccounts.operators--;
        remove_oper_privileges(client, 1);
    }
    
    long old_umodes = client->umodes & ALL_UMODES;
    client->umodes &= ~UMODE_REGNICK;
    send_umode_out(client, 1, old_umodes);
    
    if (client->user) {
        userhost_save_current(client);
        safe_strdup(client->user->virthost, client->user->realhost);
        userhost_changed(client);
    }
    
    if (nick_rec) {
        UdbRecord *swhois_rec = udb_record_find(NKEY_SWHOIS, nick_rec);
        if (swhois_rec && swhois_rec->data_str) {
            swhois_delete(client, "udb", swhois_rec->data_str, &me, NULL);
        }
    }
}

static int udb_nick_check_password(const char *nick, const char *pass, UdbRecord *nick_rec, Client *client) {
    UdbRecord *pass_rec = udb_record_find(NKEY_PASS, nick_rec);
    if (!pass_rec || !pass_rec->data_str) return 0;
    
    const char *challenge = "plain";
    UdbRecord *chall_rec = udb_record_find(NKEY_CHALLENGE, nick_rec);
    if (chall_rec && chall_rec->data_str) {
        challenge = chall_rec->data_str;
    } else {
        if (udb_ctx && udb_ctx->settings) {
            UdbRecord *g_chall = udb_record_find(SKEY_CHALLENGE, udb_ctx->settings);
            if (g_chall && g_chall->data_str) challenge = g_chall->data_str;
        }
    }
    
    const char *stored_pass = pass_rec->data_str;
    if (!strncmp(stored_pass, "plain:", 6)) {
        return !strcmp(pass, stored_pass + 6);
    } else if (!strncmp(stored_pass, "sha256:", 7)) {
        challenge = "sha256";
        stored_pass += 7;
    } else if (!strncmp(stored_pass, "md5:", 4)) {
        challenge = "md5";
        stored_pass += 4;
    } else if (!strncmp(stored_pass, "crypt:", 6)) {
        challenge = "crypt";
        stored_pass += 6;
    }

    if (!strcasecmp(challenge, "plain")) {
        return !strcmp(pass, stored_pass);
    } else if (!strcasecmp(challenge, "crypt")) {
        AuthConfig as;
        memset(&as, 0, sizeof(as));
        as.type = AUTHTYPE_UNIXCRYPT;
        as.data = pass_rec->data_str;
        return Auth_Check(client, &as, pass);
    } else if (!strcasecmp(challenge, "md5") || !strcasecmp(challenge, "sha256")) {
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int md_len;
        const EVP_MD *evp_md = EVP_get_digestbyname(challenge);
        if (!evp_md) return 0;
        
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (!ctx) return 0;
        EVP_DigestInit_ex(ctx, evp_md, NULL);
        EVP_DigestUpdate(ctx, pass, strlen(pass));
        EVP_DigestFinal_ex(ctx, md, &md_len);
        EVP_MD_CTX_free(ctx);
        
        char hex[EVP_MAX_MD_SIZE * 2 + 1];
        for (unsigned int i = 0; i < md_len; i++) {
            sprintf(&hex[i*2], "%02x", md[i]);
        }
        return !strcasecmp(stored_pass, hex);
    }
    return 0;
}

CMD_FUNC(cmd_ghost) {
    if (parc < 3) {
        sendnumeric(client, ERR_NEEDMOREPARAMS, "GHOST");
        return;
    }
    const char *target_nick = parv[1];
    const char *pass = parv[2];
    
    if (!udb_ctx || !udb_ctx->nicks) {
        sendnotice(client, "UDB is not fully initialized.");
        return;
    }
    
    UdbRecord *nick_rec = udb_record_find(target_nick, udb_ctx->nicks);
    if (!nick_rec) {
        sendnotice(client, "Nick %s is not registered.", target_nick);
        return;
    }
    
    if (!udb_nick_check_password(target_nick, pass, nick_rec, client)) {
        sendnotice(client, "Invalid password for %s.", target_nick);
        return;
    }
    
    Client *target = find_client(target_nick, NULL);
    if (target) {
        if (target == client) {
            sendnotice(client, "You cannot ghost yourself.");
            return;
        }
        sendnotice(client, "Ghosting %s...", target_nick);
        exit_client(target, NULL, "GHOST command used");
    } else {
        sendnotice(client, "%s is not online.", target_nick);
    }
}

CMD_OVERRIDE_FUNC(udb_override_nick) {
    if (parc <= 1)
        goto passthrough;

    const char *nick = parv[1];
    char clean_nick[NICKLEN+64];
    strlcpy(clean_nick, nick, sizeof(clean_nick));
    char *pass_colon = strchr(clean_nick, ':');
    char *pass_bang = strchr(clean_nick, '!');
    
    char *pass = NULL;
    int force_ghost = 0;
    
    if (pass_colon && (!pass_bang || pass_colon < pass_bang)) {
        pass = pass_colon;
        force_ghost = 0;
    } else if (pass_bang && (!pass_colon || pass_bang < pass_colon)) {
        pass = pass_bang;
        force_ghost = 1;
    }
    
    if (!pass)
        goto passthrough;

    *pass++ = '\0';
    if (client->local)
        safe_strdup(client->local->passwd, pass);

    UdbRecord *rec = (udb_ctx && udb_ctx->nicks) ? udb_record_find(clean_nick, udb_ctx->nicks) : NULL;
    
    if (rec && udb_nick_check_password(clean_nick, pass, rec, client)) {
        Client *acptr = find_client(clean_nick, NULL);
        if (acptr && acptr != client) {
            if (force_ghost) {
                char quit_msg[128];
                snprintf(quit_msg, sizeof(quit_msg), "Ghosted (Nick taken by %s)", client->name);
                exit_client(acptr, NULL, quit_msg);
            } else {
                sendnotice(client, "This nickname is currently in use. If you are the owner, you can recover it by typing /NICK %s!Password", clean_nick);
            }
        }
    }
    
    const char *new_parv[MAXPARA+1];
    for (int i = 0; i < parc; i++) new_parv[i] = parv[i];
    new_parv[1] = clean_nick;
    CallCommandOverride(ovr, clictx, client, recv_mtags, parc, new_parv);
    return;

passthrough:
    CALL_NEXT_COMMAND_OVERRIDE();
}

static int udb_hook_can_use_nick(Client *client, const char *newnick, const char **reject_reason) {
    if (!udb_ctx || !udb_ctx->nicks) return HOOK_CONTINUE;
    if (!MyConnect(client)) return HOOK_CONTINUE;
    
    UdbRecord *nick_rec = udb_record_find(newnick, udb_ctx->nicks);
    if (nick_rec) {
        UdbRecord *forbid = udb_record_find(NKEY_FORBID, nick_rec);
        if (forbid) {
            *reject_reason = "This nick is forbidden.";
            return HOOK_DENY;
        }
        
        /* If client is already this nick and identified with +r, allow without re-entering password */
        if (!strcasecmp(client->name, newnick) && has_user_mode(client, 'r')) {
            return HOOK_CONTINUE;
        }
        
        const char *pass = client->local ? client->local->passwd : NULL;
        if (pass && udb_nick_check_password(newnick, pass, nick_rec, client)) {
            return HOOK_CONTINUE;
        }
        
        static char reject_buf[256];
        snprintf(reject_buf, sizeof(reject_buf), "This nick is registered and requires a password. Use /NICK %s:Password", newnick);
        *reject_reason = reject_buf;
        return HOOK_DENY;
    }
    return HOOK_CONTINUE;
}

static int udb_hook_nick_change(Client *client, MessageTag *mtags, const char *newnick) {
    if (!udb_ctx || !udb_ctx->nicks) return 0;
    if (!MyConnect(client)) return 0;
    
    UdbRecord *old_rec = udb_record_find(client->name, udb_ctx->nicks);
    UdbRecord *new_rec = udb_record_find(newnick, udb_ctx->nicks);
    
    if (old_rec && old_rec != new_rec) {
        udb_nick_strip(client, old_rec);
    }
    
    return 0;
}

static int udb_hook_post_nick_change(Client *client, MessageTag *recv_mtags, const char *oldnick) {
    if (!udb_ctx || !udb_ctx->nicks) return 0;
    if (!MyConnect(client)) return 0;
    
    UdbRecord *new_rec = udb_record_find(client->name, udb_ctx->nicks);
    if (new_rec) {
        udb_nick_apply(client, new_rec, 0);
    }
    return 0;
}

static int udb_hook_local_connect(Client *client) {
    if (!udb_ctx || !udb_ctx->nicks) return 0;
    
    UdbRecord *nick_rec = udb_record_find(client->name, udb_ctx->nicks);
    if (nick_rec) {
        udb_nick_apply(client, nick_rec, 0);
    }
    return 0;
}

int udb_nicks_init(ModuleInfo *modinfo) {
    CommandAdd(modinfo->handle, "GHOST", cmd_ghost, 3, CMD_USER);
    HookAdd(modinfo->handle, HOOKTYPE_CAN_USE_NICK, 0, udb_hook_can_use_nick);
    HookAdd(modinfo->handle, HOOKTYPE_LOCAL_NICKCHANGE, 0, udb_hook_nick_change);
    HookAdd(modinfo->handle, HOOKTYPE_POST_LOCAL_NICKCHANGE, 0, udb_hook_post_nick_change);
    HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, udb_hook_local_connect);
    return MOD_SUCCESS;
}

int udb_nicks_load(ModuleInfo *modinfo) {
    CommandOverrideAdd(modinfo->handle, "NICK", 0, udb_override_nick);
    return 0;
}
