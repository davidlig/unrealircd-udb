/* UDB Channels Module for UnrealIRCd 6 */

typedef struct UdbPendingChannelAuth UdbPendingChannelAuth;
typedef struct UdbChannelModeState UdbChannelModeState;

struct UdbPendingChannelAuth {
	UdbPendingChannelAuth *next;
	char channel[CHANNELLEN + 1];
};

struct UdbChannelModeState {
	char *value;
};

static ModDataInfo *udb_channel_auth_pending_md = NULL;
static ModDataInfo *udb_channel_auth_member_md = NULL;
static ModDataInfo *udb_channel_modes_md = NULL;

/* Forward declarations */
static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg);
static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key);
static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_pre_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode);
static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic);

static int udb_channel_is_identified_founder(Client *client, UdbRecord *chan_rec)
{
	UdbRecord *founder_rec;

	if (!client || !chan_rec)
		return 0;
	founder_rec = udb_record_find(CKEY_FOUNDER, chan_rec);
	return founder_rec && founder_rec->data_str &&
	       !strcasecmp(client->name, founder_rec->data_str) &&
	       has_user_mode(client, 'r');
}

static void udb_channel_pending_auth_free(ModData *m)
{
	UdbPendingChannelAuth *entry = m->ptr;

	while (entry)
	{
		UdbPendingChannelAuth *next = entry->next;
		safe_free(entry);
		entry = next;
	}
	m->ptr = NULL;
}

static void udb_channel_modes_free(ModData *m)
{
	UdbChannelModeState *state = m->ptr;

	if (state) {
		safe_free(state->value);
		safe_free(state);
	}
	m->ptr = NULL;
}

static void udb_channel_set_modes(Channel *channel, const char *value)
{
	char modebuf[512];
	char *parabuf;

	if (!value || !*value)
		return;
	strlcpy(modebuf, value, sizeof(modebuf));
	parabuf = strchr(modebuf, ' ');
	if (parabuf)
		*parabuf++ = '\0';
	set_channel_mode(channel, NULL, modebuf, parabuf ? parabuf : "");
}

static void udb_channel_reverse_modes(Channel *channel, const char *value)
{
	char modebuf[512];
	char inverse[512];
	char *parabuf;
	char *src;
	char *dst = inverse;

	if (!value || !*value)
		return;
	strlcpy(modebuf, value, sizeof(modebuf));
	parabuf = strchr(modebuf, ' ');
	if (parabuf)
		*parabuf++ = '\0';
	for (src = modebuf; *src && (size_t)(dst - inverse) < sizeof(inverse) - 1; src++) {
		if (*src == '+')
			*dst++ = '-';
		else if (*src == '-')
			*dst++ = '+';
		else
			*dst++ = *src;
	}
	*dst = '\0';
	set_channel_mode(channel, NULL, inverse, parabuf ? parabuf : "");
}

static void udb_channel_apply_modes(Channel *channel, const char *value)
{
	UdbChannelModeState *state;

	if (!udb_channel_modes_md)
		return;
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (!state) {
		state = safe_alloc(sizeof(*state));
		moddata_channel(channel, udb_channel_modes_md).ptr = state;
	}
	if (state->value && !strcmp(state->value, value))
		return;
	udb_channel_reverse_modes(channel, state->value);
	udb_channel_set_modes(channel, value);
	safe_strdup(state->value, value);
}

static void udb_channel_remove_modes(Channel *channel, const char *fallback_value)
{
	UdbChannelModeState *state;
	const char *value = fallback_value;

	if (!udb_channel_modes_md) {
		udb_channel_reverse_modes(channel, fallback_value);
		return;
	}
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (state && state->value)
		value = state->value;
	udb_channel_reverse_modes(channel, value);
	if (state) {
		safe_free(state->value);
		safe_free(state);
		moddata_channel(channel, udb_channel_modes_md).ptr = NULL;
	}
}

static void udb_channel_pending_auth_set(Client *client, Channel *channel)
{
	UdbPendingChannelAuth *entry;

	if (!MyUser(client) || !udb_channel_auth_pending_md)
		return;
	for (entry = moddata_local_client(client, udb_channel_auth_pending_md).ptr;
	     entry; entry = entry->next)
	{
		if (!strcasecmp(entry->channel, channel->name))
			return;
	}
	entry = safe_alloc(sizeof(*entry));
	strlcpy(entry->channel, channel->name, sizeof(entry->channel));
	entry->next = moddata_local_client(client, udb_channel_auth_pending_md).ptr;
	moddata_local_client(client, udb_channel_auth_pending_md).ptr = entry;
}

static int udb_channel_pending_auth_take(Client *client, Channel *channel)
{
	UdbPendingChannelAuth *entry;
	UdbPendingChannelAuth *previous = NULL;

	if (!MyUser(client) || !udb_channel_auth_pending_md)
		return 0;
	entry = moddata_local_client(client, udb_channel_auth_pending_md).ptr;
	while (entry)
	{
		if (!strcasecmp(entry->channel, channel->name))
		{
			if (previous)
				previous->next = entry->next;
			else
				moddata_local_client(client, udb_channel_auth_pending_md).ptr = entry->next;
			safe_free(entry);
			return 1;
		}
		previous = entry;
		entry = entry->next;
	}
	return 0;
}

static void udb_channel_reconcile_founder(Channel *channel, UdbRecord *chan_rec)
{
	Member *member;
	int suspended = chan_rec && udb_record_find(CKEY_SUSPENDED, chan_rec);

	for (member = channel->members; member; member = member->next)
	{
		/* The member's home server emits the network MODE exactly once. */
		if (!MyUser(member->client))
			continue;
		int is_founder = !suspended &&
		                 udb_channel_is_identified_founder(member->client, chan_rec);
		if (is_founder)
		{
			if (!check_channel_access_member(member, "q"))
				set_channel_mode(channel, NULL, "+q", member->client->name);
		}
		else if (check_channel_access_member(member, "q"))
		{
			/* UDB owns founder +q, so a profile replacement has one owner only. */
			set_channel_mode(channel, NULL, "-q", member->client->name);
		}
	}
}

static void udb_channel_revoke_udb_admins(Channel *channel)
{
	Member *member;

	if (!udb_channel_auth_member_md)
		return;
	for (member = channel->members; member; member = member->next)
	{
		if (!moddata_member(member, udb_channel_auth_member_md).i)
			continue;
		if (check_channel_access_member(member, "a"))
			set_channel_mode(channel, NULL, "-a", member->client->name);
		moddata_member(member, udb_channel_auth_member_md).i = 0;
	}
}

static void udb_channel_grant_pending_admin(Client *client, Channel *channel,
	                                             MessageTag *mtags)
{
	Member *member;

	if (!udb_channel_pending_auth_take(client, channel) ||
	    !udb_channel_auth_member_md || !find_channel_mode_handler('a'))
		return;
	member = find_member_link(channel->members, client);
	if (!member || check_channel_access_member(member, "a"))
		return;
	set_channel_mode(channel, mtags, "+a", client->name);
	moddata_member(member, udb_channel_auth_member_md).i = 1;
}

static void udb_channel_clear_topic(Channel *channel)
{
	safe_free(channel->topic);
	safe_free(channel->topic_nick);
	channel->topic_time = 0;
	if (channel->users > 0)
	{
		sendto_channel(channel, &me, NULL, 0, 0, SEND_LOCAL, NULL,
		               ":%s TOPIC %s :", me.name, channel->name);
	}
}

static void udb_channel_apply_record(Channel *channel, UdbRecord *chan_rec, const char *subkey, int is_new)
{
    UdbRecord *sub_rec = udb_record_find(subkey, chan_rec);
    if (!sub_rec || !sub_rec->data_str) return;

    if (!strcmp(subkey, CKEY_FOUNDER)) {
        udb_channel_reconcile_founder(channel, chan_rec);
    } else if (!strcmp(subkey, CKEY_MODES)) {
        udb_channel_apply_modes(channel, sub_rec->data_str);
    } else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE)) {
        udb_channel_revoke_udb_admins(channel);
    } else if (!strcmp(subkey, CKEY_TOPIC)) {
        if (!channel->topic || strcmp(channel->topic, sub_rec->data_str)) {
            safe_strdup(channel->topic, sub_rec->data_str);
            channel->topic_time = TStime();
            safe_strdup(channel->topic_nick, udb_get_bot_nick(SKEY_CHANSERV, 0));
            if (channel->users > 0) {
                sendto_channel(channel, &me, NULL, 0, 0, SEND_LOCAL, NULL,
                               ":%s TOPIC %s :%s", me.name, channel->name, channel->topic);
            }
        }
    }
}

static void udb_channel_remove_record(Channel *channel, UdbRecord *chan_rec, const char *subkey)
{
    if (!strcmp(subkey, CKEY_FOUNDER)) {
        udb_channel_reconcile_founder(channel, NULL);
    } else if (!strcmp(subkey, CKEY_MODES)) {
        UdbRecord *mode_rec = udb_record_find(CKEY_MODES, chan_rec);
        udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
    } else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE)) {
        udb_channel_revoke_udb_admins(channel);
    } else if (!strcmp(subkey, CKEY_TOPIC)) {
        udb_channel_clear_topic(channel);
    } else if (!strcasecmp(subkey, chan_rec->key)) {
        UdbRecord *mode_rec = udb_record_find(CKEY_MODES, chan_rec);
        udb_channel_reconcile_founder(channel, NULL);
        udb_channel_revoke_udb_admins(channel);
        udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
        if (udb_record_find(CKEY_TOPIC, chan_rec))
            udb_channel_clear_topic(channel);
    }
}

static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key)
{
    UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
    UdbRecord *pass_rec;
    if (!chan_rec) return HOOK_CONTINUE;

    UdbRecord *forbid_rec = udb_record_find(CKEY_FORBID, chan_rec);
    if (forbid_rec) {
        return HOOK_CONTINUE; /* Let can_join handle the reject with proper numeric */
    }

    if (udb_channel_is_identified_founder(client, chan_rec))
        return HOOK_ALLOW; /* Bypass bans/keys/invite */

    /* CAN_JOIN already verified this credential. Record it only after all
     * regular join checks have succeeded, immediately before membership. */
    pass_rec = udb_record_find(CKEY_PASS, chan_rec);
    if (pass_rec && pass_rec->data_str && *pass_rec->data_str && key &&
        udb_check_password(key, chan_rec, client))
        udb_channel_pending_auth_set(client, channel);
    return HOOK_CONTINUE;
}

static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg)
{
    static char errbuf[512];
    UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
    if (!chan_rec) return 0;

    UdbRecord *forbid_rec = udb_record_find(CKEY_FORBID, chan_rec);
    if (forbid_rec) {
        snprintf(errbuf, sizeof(errbuf), "%%s :%s", forbid_rec->data_str ? forbid_rec->data_str : "Channel is forbidden");
        *errmsg = errbuf;
        return ERR_FORBIDDENCHANNEL;
    }

    int is_founder = udb_channel_is_identified_founder(client, chan_rec);

    UdbRecord *pass_rec = udb_record_find(CKEY_PASS, chan_rec);
    if (pass_rec && pass_rec->data_str && *pass_rec->data_str && !is_founder) {
        if (!key || !udb_check_password(key, chan_rec, client)) {
            *errmsg = STR_ERR_BADCHANNELKEY;
            return ERR_BADCHANNELKEY;
        }
    }

    UdbRecord *access_rec = udb_record_find(CKEY_ACCESS, chan_rec);
    if (access_rec && !is_founder) {
        UdbRecord *acc_entry = udb_record_find(client->name, access_rec);
        if (!acc_entry || !has_user_mode(client, 'r')) {
            snprintf(errbuf, sizeof(errbuf), "%%s :You must be authorized and identified to join this channel");
            *errmsg = errbuf;
            return ERR_FORBIDDENCHANNEL;
        }
    }
    return 0;
}

static void handle_join(Client *client, Channel *channel, MessageTag *mtags)
{
    UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
    if (!chan_rec) return;

    int is_founder = udb_channel_is_identified_founder(client, chan_rec);

    if (channel->users == 1) {
        UdbRecord *susp_rec = udb_record_find(CKEY_SUSPENDED, chan_rec);
        
        /* A registered channel assigns founder authority exclusively as +q. */
        if (!IsServer(client) && !IsULine(client)) {
            set_channel_mode(channel, mtags, "-o", client->name);
        }
        
        if (!susp_rec) {
            set_channel_mode(channel, mtags, "+r", "");
        }

        udb_channel_apply_record(channel, chan_rec, CKEY_MODES, 0);
        udb_channel_apply_record(channel, chan_rec, CKEY_TOPIC, 0);
    }
    
    /* Founder +q is UDB-owned and must have exactly one current holder. */
    udb_channel_reconcile_founder(channel, chan_rec);
}

static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags)
{
    handle_join(client, channel, mtags);
    udb_channel_grant_pending_admin(client, channel, mtags);
    return 0;
}

static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags)
{
    handle_join(client, channel, mtags);
    return 0;
}

static int udb_hook_pre_command(Client *client, MessageTag *mtags, const char *buf)
{
    if (IsServer(client)) return HOOK_CONTINUE;

    if (strncasecmp(buf, "MODE ", 5) && strncasecmp(buf, "SAMODE ", 7))
        return HOOK_CONTINUE;

    const char *p = strchr(buf, '#');
    if (!p) return HOOK_CONTINUE;

    const char *space_after = strchr(p, ' ');
    if (!space_after) return HOOK_CONTINUE;

    char chan_name[64];
    int i = 0;
    while (p[i] && p[i] != ' ' && i < 63) {
        chan_name[i] = p[i];
        i++;
    }
    chan_name[i] = '\0';

    UdbRecord *chan_rec = udb_record_find(chan_name, udb_ctx->channels);
    if (!chan_rec) return HOOK_CONTINUE;

    UdbRecord *opt_rec = udb_record_find(CKEY_OPTIONS, chan_rec);
    if (!opt_rec || !(opt_rec->data_num & UDB_CHOPT_LOCK_MODES))
        return HOOK_CONTINUE;

    int is_founder = udb_channel_is_identified_founder(client, chan_rec);
    
    if (!is_founder) {
        sendnotice(client, "You do not have permission to change modes in %s (locked by UDB)", chan_name);
        return HOOK_DENY;
    }

    return HOOK_CONTINUE;
}

static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic)
{
    if (IsServer(client)) return topic;

    UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
    if (!chan_rec) return topic;

    UdbRecord *topic_rec = udb_record_find(CKEY_TOPIC, chan_rec);
    if (topic_rec) {
        int is_founder = udb_channel_is_identified_founder(client, chan_rec);
        if (!is_founder) {
            sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
            return NULL;
        }
    }
    return topic;
}

static void udb_channels_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_auth_pending";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_channel_pending_auth_free;
	udb_channel_auth_pending_md = ModDataAdd(modinfo->handle, mreq);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_auth_admin";
	mreq.type = MODDATATYPE_MEMBER;
	udb_channel_auth_member_md = ModDataAdd(modinfo->handle, mreq);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_modes";
	mreq.type = MODDATATYPE_CHANNEL;
	mreq.free = udb_channel_modes_free;
	udb_channel_modes_md = ModDataAdd(modinfo->handle, mreq);

    HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, udb_hook_can_join);
    HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, udb_hook_pre_local_join);
    HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, udb_hook_local_join);
    HookAdd(modinfo->handle, HOOKTYPE_REMOTE_JOIN, 0, udb_hook_remote_join);
    HookAdd(modinfo->handle, HOOKTYPE_PRE_COMMAND, 0, udb_hook_pre_command);
    HookAddConstString(modinfo->handle, HOOKTYPE_PRE_LOCAL_TOPIC, 0, udb_hook_pre_topic);
}
