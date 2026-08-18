/* UDB Channels Module for UnrealIRCd 6 */

/* Forward declarations */
static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg);
static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key);
static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_pre_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode);
static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic);

static void udb_channel_apply_record(Channel *channel, UdbRecord *chan_rec, const char *subkey, int is_new)
{
    UdbRecord *sub_rec = udb_record_find(subkey, chan_rec);
    if (!sub_rec || !sub_rec->data_str) return;

    if (!strcmp(subkey, CKEY_FOUNDER)) {
        Member *m;
        for (m = channel->members; m; m = m->next) {
            if (!strcasecmp(m->client->name, sub_rec->data_str) && has_user_mode(m->client, 'r')) {
                if (!udb_record_find(CKEY_SUSPENDED, chan_rec)) {
                    set_channel_mode(channel, NULL, "+q", m->client->name);
                }
            }
        }
    } else if (!strcmp(subkey, CKEY_MODES)) {
        set_channel_mode(channel, NULL, sub_rec->data_str, "");
    } else if (!strcmp(subkey, CKEY_TOPIC)) {
        if (!channel->topic || strcmp(channel->topic, sub_rec->data_str)) {
            safe_strdup(channel->topic, sub_rec->data_str);
            channel->topic_time = TStime();
            safe_strdup(channel->topic_nick, udb_get_bot_nick(SKEY_CHANSERV, 0));
            if (channel->users > 0) {
                sendto_channel(channel, &me, NULL, 0, 0, SEND_ALL, NULL, ":%s TOPIC %s :%s", udb_get_bot_mask(SKEY_CHANSERV, 0), channel->name, channel->topic);
            }
        }
    }
}

static void udb_channel_remove_record(Channel *channel, UdbRecord *chan_rec, const char *subkey)
{
    if (!strcmp(subkey, CKEY_FOUNDER)) {
        UdbRecord *sub_rec = udb_record_find(subkey, chan_rec);
        if (sub_rec && sub_rec->data_str) {
            Member *m;
            for (m = channel->members; m; m = m->next) {
                if (!strcasecmp(m->client->name, sub_rec->data_str)) {
                    set_channel_mode(channel, NULL, "-q", m->client->name);
                }
            }
        }
    } else if (!strcmp(subkey, CKEY_TOPIC)) {
        safe_free(channel->topic);
        safe_free(channel->topic_nick);
        channel->topic_time = 0;
        if (channel->users > 0) {
            sendto_channel(channel, &me, NULL, 0, 0, SEND_ALL, NULL, ":%s TOPIC %s :", udb_get_bot_mask(SKEY_CHANSERV, 0), channel->name);
        }
    }
}

static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key)
{
    UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
    if (!chan_rec) return HOOK_CONTINUE;

    UdbRecord *forbid_rec = udb_record_find(CKEY_FORBID, chan_rec);
    if (forbid_rec) {
        return HOOK_CONTINUE; /* Let can_join handle the reject with proper numeric */
    }

    UdbRecord *founder_rec = udb_record_find(CKEY_FOUNDER, chan_rec);
    if (founder_rec && founder_rec->data_str && !strcasecmp(client->name, founder_rec->data_str)) {
        if (has_user_mode(client, 'r')) {
            return HOOK_ALLOW; /* Bypass bans/keys/invite */
        }
    }
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

    UdbRecord *founder_rec = udb_record_find(CKEY_FOUNDER, chan_rec);
    int is_founder = (founder_rec && founder_rec->data_str && !strcasecmp(client->name, founder_rec->data_str) && has_user_mode(client, 'r'));

    UdbRecord *pass_rec = udb_record_find(CKEY_PASS, chan_rec);
    if (pass_rec && pass_rec->data_str && *pass_rec->data_str && !is_founder) {
        if (!key || strcmp(key, pass_rec->data_str)) {
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

    int is_founder = 0;
    UdbRecord *founder_rec = udb_record_find(CKEY_FOUNDER, chan_rec);
    if (founder_rec && founder_rec->data_str && !strcasecmp(client->name, founder_rec->data_str)) {
        is_founder = 1;
    }

    if (channel->users == 1) {
        UdbRecord *susp_rec = udb_record_find(CKEY_SUSPENDED, chan_rec);
        
        // Strip auto-op if it's not the founder, OR if the channel is suspended
        if ((!is_founder || susp_rec) && !IsServer(client) && !IsULine(client)) {
            set_channel_mode(channel, mtags, "-o", client->name);
        }
        
        if (!susp_rec) {
            set_channel_mode(channel, mtags, "+r", "");
        }

        udb_channel_apply_record(channel, chan_rec, CKEY_MODES, 0);
        udb_channel_apply_record(channel, chan_rec, CKEY_TOPIC, 0);
    }
    
    // Always grant +q to the founder when they join, even if the channel already existed
    if (is_founder && has_user_mode(client, 'r') && !udb_record_find(CKEY_SUSPENDED, chan_rec)) {
        set_channel_mode(channel, mtags, "+q", client->name);
    }
}

static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags)
{
    handle_join(client, channel, mtags);
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

    UdbRecord *founder_rec = udb_record_find(CKEY_FOUNDER, chan_rec);
    int is_founder = (founder_rec && founder_rec->data_str && !strcasecmp(client->name, founder_rec->data_str) && has_user_mode(client, 'r'));
    
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
        UdbRecord *founder_rec = udb_record_find(CKEY_FOUNDER, chan_rec);
        int is_founder = (founder_rec && founder_rec->data_str && !strcasecmp(client->name, founder_rec->data_str) && has_user_mode(client, 'r'));
        if (!is_founder) {
            sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
            return NULL;
        }
    }
    return topic;
}

static void udb_channels_init(ModuleInfo *modinfo)
{
    HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, udb_hook_can_join);
    HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, udb_hook_pre_local_join);
    HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, udb_hook_local_join);
    HookAdd(modinfo->handle, HOOKTYPE_REMOTE_JOIN, 0, udb_hook_remote_join);
    HookAdd(modinfo->handle, HOOKTYPE_PRE_COMMAND, 0, udb_hook_pre_command);
    HookAddConstString(modinfo->handle, HOOKTYPE_PRE_LOCAL_TOPIC, 0, udb_hook_pre_topic);
}

