/* UDB Channels Module for UnrealIRCd 6 */

typedef struct UdbPendingChannelAuth UdbPendingChannelAuth;
typedef struct UdbChannelModeState UdbChannelModeState;
typedef struct UdbInviteGrant UdbInviteGrant;
typedef struct UdbBanOwner UdbBanOwner;
typedef struct UdbBanSnapshot UdbBanSnapshot;

struct UdbPendingChannelAuth {
	UdbPendingChannelAuth *next;
	char channel[CHANNELLEN + 1];
};

struct UdbChannelModeState {
	char *value;
};

struct UdbInviteGrant {
	UdbInviteGrant *next;
	char channel[CHANNELLEN + 1];
	time_t expires;
};

struct UdbBanOwner {
	UdbBanOwner *next;
	char *ban;
	char *owner;
};

struct UdbBanSnapshot {
	UdbBanSnapshot *next;
	char *ban;
};

#define UDB_INVITE_GRANT_TTL 300

static ModDataInfo *udb_channel_auth_pending_md = NULL;
static ModDataInfo *udb_channel_auth_member_md = NULL;
static ModDataInfo *udb_channel_modes_md = NULL;
static ModDataInfo *udb_channel_invite_grant_md = NULL;
static ModDataInfo *udb_channel_ban_owners_md = NULL;

/* Forward declarations */
static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg);
static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key);
static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_pre_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode);
static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic);
CMD_OVERRIDE_FUNC(udb_override_invite);
CMD_OVERRIDE_FUNC(udb_override_mode);

static int udb_channel_is_identified_founder(Client *client, UdbRecord *chan_rec)
{
	UdbRecord *founder_rec;

	if (!client || !chan_rec)
		return 0;
	founder_rec = udb_record_find(udb_ctx, CKEY_FOUNDER, chan_rec);
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

	if (state)
	{
		safe_free(state->value);
		safe_free(state);
	}
	m->ptr = NULL;
}

static void udb_channel_invite_grant_free(ModData *m)
{
	UdbInviteGrant *grant = m->ptr;

	while (grant)
	{
		UdbInviteGrant *next = grant->next;
		safe_free(grant);
		grant = next;
	}
	m->ptr = NULL;
}

static void udb_channel_ban_owners_free(ModData *m)
{
	UdbBanOwner *owner = m->ptr;

	while (owner)
	{
		UdbBanOwner *next = owner->next;
		safe_free(owner->ban);
		safe_free(owner->owner);
		safe_free(owner);
		owner = next;
	}
	m->ptr = NULL;
}

static void udb_channel_do_mode(Channel *channel, MessageTag *mtags,
                                const char *modes, const char *parameters)
{
	char buf[512];
	char *p, *param;
	int myparc = 1;
	int i;
	char *myparv[512];
	Client *source;

	if (!channel || !modes || !*modes || !do_mode)
		return;
	source = udb_service_source(SKEY_CHANSERV);
	memset(myparv, 0, sizeof(myparv));
	myparv[0] = raw_strdup(modes);
	strlcpy(buf, parameters ? parameters : "", sizeof(buf));
	for (param = strtoken(&p, buf, " "); param && myparc < (int)(sizeof(myparv) / sizeof(myparv[0])) - 1;
	     param = strtoken(&p, NULL, " "))
		myparv[myparc++] = raw_strdup(param);
	myparv[myparc] = NULL;

	do_mode(channel, source, mtags, myparc, (const char **)myparv, 0, 0);
	for (i = 0; i < myparc; i++)
		safe_free(myparv[i]);
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
	udb_channel_do_mode(channel, NULL, modebuf, parabuf ? parabuf : "");
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
	for (src = modebuf; *src && (size_t)(dst - inverse) < sizeof(inverse) - 1; src++)
	{
		if (*src == '+')
			*dst++ = '-';
		else if (*src == '-')
			*dst++ = '+';
		else
			*dst++ = *src;
	}
	*dst = '\0';
	udb_channel_do_mode(channel, NULL, inverse, parabuf ? parabuf : "");
}

static void udb_channel_apply_modes(Channel *channel, const char *value)
{
	UdbChannelModeState *state;

	if (!udb_channel_modes_md)
		return;
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (!state)
	{
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

	if (!udb_channel_modes_md)
	{
		udb_channel_reverse_modes(channel, fallback_value);
		return;
	}
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (state && state->value)
		value = state->value;
	udb_channel_reverse_modes(channel, value);
	if (state)
	{
		safe_free(state->value);
		safe_free(state);
		moddata_channel(channel, udb_channel_modes_md).ptr = NULL;
	}
}

static void udb_channel_set_persistent(Channel *channel, int enabled)
{
	/* +P is supplied by an optional native module; do not emulate it. */
	if (!find_channel_mode_handler('P'))
		return;
	if (enabled && !has_channel_mode(channel, 'P'))
		udb_channel_do_mode(channel, NULL, "+P", "");
	else if (!enabled && has_channel_mode(channel, 'P'))
		udb_channel_do_mode(channel, NULL, "-P", "");
}

static void udb_channel_invite_grant_set(Client *client, Channel *channel)
{
	UdbInviteGrant *grant;

	if (!MyUser(client) || !udb_channel_invite_grant_md)
		return;
	for (grant = moddata_local_client(client, udb_channel_invite_grant_md).ptr;
	     grant; grant = grant->next)
	{
		if (!strcasecmp(grant->channel, channel->name))
		{
			grant->expires = TStime() + UDB_INVITE_GRANT_TTL;
			return;
		}
	}
	grant = safe_alloc(sizeof(*grant));
	strlcpy(grant->channel, channel->name, sizeof(grant->channel));
	grant->expires = TStime() + UDB_INVITE_GRANT_TTL;
	grant->next = moddata_local_client(client, udb_channel_invite_grant_md).ptr;
	moddata_local_client(client, udb_channel_invite_grant_md).ptr = grant;
}

static int udb_channel_invite_grant_take(Client *client, Channel *channel, int consume)
{
	UdbInviteGrant *grant, *previous = NULL;

	if (!MyUser(client) || !udb_channel_invite_grant_md)
		return 0;
	grant = moddata_local_client(client, udb_channel_invite_grant_md).ptr;
	while (grant)
	{
		UdbInviteGrant *next = grant->next;
		if (grant->expires <= TStime() || !strcasecmp(grant->channel, channel->name))
		{
			if (grant->expires > TStime() && !strcasecmp(grant->channel, channel->name) && !consume)
				return 1;
			if (previous)
				previous->next = next;
			else
				moddata_local_client(client, udb_channel_invite_grant_md).ptr = next;
			if (grant->expires > TStime() && !strcasecmp(grant->channel, channel->name))
			{
				safe_free(grant);
				return 1;
			}
			safe_free(grant);
			grant = next;
			continue;
		}
		previous = grant;
		grant = next;
	}
	return 0;
}

static UdbBanOwner *udb_channel_ban_owner_find(Channel *channel, const char *ban)
{
	UdbBanOwner *owner;

	if (!udb_channel_ban_owners_md || !ban)
		return NULL;
	for (owner = moddata_channel(channel, udb_channel_ban_owners_md).ptr;
	     owner; owner = owner->next)
		if (!mycmp(owner->ban, ban))
			return owner;
	return NULL;
}

static void udb_channel_ban_owners_prune(Channel *channel)
{
	UdbBanOwner **owner;

	if (!udb_channel_ban_owners_md)
		return;
	for (owner = (UdbBanOwner **)&moddata_channel(channel, udb_channel_ban_owners_md).ptr;
	     *owner;)
	{
		UdbBanOwner *current = *owner;
		if (ban_exists(channel->banlist, current->ban))
		{
			owner = &current->next;
			continue;
		}
		*owner = current->next;
		safe_free(current->ban);
		safe_free(current->owner);
		safe_free(current);
	}
}

static UdbBanSnapshot *udb_channel_ban_snapshot(Channel *channel)
{
	Ban *ban;
	UdbBanSnapshot *snapshot = NULL;

	for (ban = channel->banlist; ban; ban = ban->next)
	{
		UdbBanSnapshot *entry = safe_alloc(sizeof(*entry));
		safe_strdup(entry->ban, ban->banstr);
		entry->next = snapshot;
		snapshot = entry;
	}
	return snapshot;
}

static int udb_channel_ban_was_present(UdbBanSnapshot *snapshot, const char *ban)
{
	for (; snapshot; snapshot = snapshot->next)
		if (!mycmp(snapshot->ban, ban))
			return 1;
	return 0;
}

static void udb_channel_ban_snapshot_free(UdbBanSnapshot *snapshot)
{
	while (snapshot)
	{
		UdbBanSnapshot *next = snapshot->next;
		safe_free(snapshot->ban);
		safe_free(snapshot);
		snapshot = next;
	}
}

static void udb_channel_track_new_bans(Channel *channel, Client *client, UdbBanSnapshot *snapshot)
{
	Ban *ban;

	if (!udb_channel_ban_owners_md)
		return;
	for (ban = channel->banlist; ban; ban = ban->next)
	{
		UdbBanOwner *owner;
		if (udb_channel_ban_was_present(snapshot, ban->banstr) ||
		    udb_channel_ban_owner_find(channel, ban->banstr))
			continue;
		owner = safe_alloc(sizeof(*owner));
		safe_strdup(owner->ban, ban->banstr);
		safe_strdup(owner->owner, client->name);
		owner->next = moddata_channel(channel, udb_channel_ban_owners_md).ptr;
		moddata_channel(channel, udb_channel_ban_owners_md).ptr = owner;
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
	int suspended = chan_rec && udb_record_find(udb_ctx, CKEY_SUSPENDED, chan_rec);

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
				udb_channel_do_mode(channel, NULL, "+q", member->client->name);
		} else if (check_channel_access_member(member, "q"))
		{
			/* UDB owns founder +q, so a profile replacement has one owner only. */
			udb_channel_do_mode(channel, NULL, "-q", member->client->name);
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
			udb_channel_do_mode(channel, NULL, "-a", member->client->name);
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
	udb_channel_do_mode(channel, mtags, "+a", client->name);
	moddata_member(member, udb_channel_auth_member_md).i = 1;
}

static void udb_channel_clear_topic(Channel *channel)
{
	Client *source = udb_service_source(SKEY_CHANSERV);

	safe_free(channel->topic);
	safe_free(channel->topic_nick);
	channel->topic_time = 0;
	if (channel->users > 0)
	{
		sendto_channel(channel, source, NULL, 0, 0, SEND_LOCAL, NULL,
		               ":%s TOPIC %s :", source->name, channel->name);
	}
}

static void udb_channel_apply_topic(Channel *channel, UdbRecord *topic_rec)
{
	Client *source;

	if (!topic_rec || !topic_rec->data_str)
		return;
	source = udb_service_source(SKEY_CHANSERV);
	if (channel->topic && !strcmp(channel->topic, topic_rec->data_str) &&
	    channel->topic_nick && !strcmp(channel->topic_nick, source->name))
		return;
	if (set_channel_topic)
		set_channel_topic(source, channel, NULL, topic_rec->data_str, source->name, TStime());
}

static void udb_channel_apply_subrecord(UdbContext *ctx, Channel *channel, UdbRecord *chan_rec,
                                        const char *subkey, int is_new)
{
	/* A channel-profile replacement revokes every UDB-owned channel state;
	 * mirror that removal so the surviving profile restores it all. */
	if (!strcasecmp(subkey, chan_rec->key))
	{
		UdbRecord *mode_rec = udb_record_find(ctx, CKEY_MODES, chan_rec);
		UdbRecord *topic_rec = udb_record_find(ctx, CKEY_TOPIC, chan_rec);

		udb_channel_reconcile_founder(channel, chan_rec);
		if (mode_rec && mode_rec->data_str)
			udb_channel_apply_modes(channel, mode_rec->data_str);
		if (udb_record_find(ctx, CKEY_PERSISTENT, chan_rec))
			udb_channel_set_persistent(channel, 1);
		udb_channel_apply_topic(channel, topic_rec);
		return;
	}

	UdbRecord *sub_rec = udb_record_find(ctx, subkey, chan_rec);
	if (!sub_rec || !sub_rec->data_str)
		return;

	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, chan_rec);
	} else if (!strcmp(subkey, CKEY_MODES))
	{
		udb_channel_apply_modes(channel, sub_rec->data_str);
	} else if (!strcmp(subkey, CKEY_PERSISTENT))
	{
		udb_channel_set_persistent(channel, 1);
	} else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE))
	{
		udb_channel_revoke_udb_admins(channel);
	} else if (!strcmp(subkey, CKEY_TOPIC))
	{
		udb_channel_apply_topic(channel, sub_rec);
	}
}

static void udb_channel_remove_subrecord(UdbContext *ctx, Channel *channel, UdbRecord *chan_rec,
                                         const char *subkey)
{
	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, NULL);
	} else if (!strcmp(subkey, CKEY_MODES))
	{
		UdbRecord *mode_rec = udb_record_find(ctx, CKEY_MODES, chan_rec);
		udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
	} else if (!strcmp(subkey, CKEY_PERSISTENT))
	{
		udb_channel_set_persistent(channel, 0);
	} else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE))
	{
		udb_channel_revoke_udb_admins(channel);
	} else if (!strcmp(subkey, CKEY_TOPIC))
	{
		udb_channel_clear_topic(channel);
	} else if (!strcasecmp(subkey, chan_rec->key))
	{
		UdbRecord *mode_rec = udb_record_find(ctx, CKEY_MODES, chan_rec);
		udb_channel_reconcile_founder(channel, NULL);
		udb_channel_revoke_udb_admins(channel);
		udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
		udb_channel_set_persistent(channel, 0);
		if (udb_record_find(ctx, CKEY_TOPIC, chan_rec))
			udb_channel_clear_topic(channel);
	}
}

static void udb_channel_apply_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new)
{
	UdbRecord *chan_rec;
	Channel *channel;

	if (!ctx || !block || !rec)
		return;
	chan_rec = rec->parent == block->tree ? rec : rec->parent;
	channel = find_channel(chan_rec->key);
	if (channel)
		udb_channel_apply_subrecord(ctx, channel, chan_rec, rec->key, is_new);
}

static void udb_channel_remove_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	UdbRecord *chan_rec;
	Channel *channel;

	if (!ctx || !block || !rec)
		return;
	chan_rec = rec->parent == block->tree ? rec : rec->parent;
	channel = find_channel(chan_rec->key);
	if (channel)
		udb_channel_remove_subrecord(ctx, channel, chan_rec, rec->key);
}

static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key)
{
	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	UdbRecord *pass_rec;
	if (!chan_rec)
		return HOOK_CONTINUE;

	UdbRecord *forbid_rec = udb_record_find(udb_ctx, CKEY_FORBID, chan_rec);
	if (forbid_rec)
	{
		return HOOK_CONTINUE; /* Let can_join handle the reject with proper numeric */
	}

	if (udb_channel_is_identified_founder(client, chan_rec))
		return HOOK_ALLOW; /* Bypass bans/keys/invite */

	/* CAN_JOIN already verified this credential. Record it only after all
     * regular join checks have succeeded, immediately before membership. */
	pass_rec = udb_record_find(udb_ctx, CKEY_PASS, chan_rec);
	if (pass_rec && pass_rec->data_str && *pass_rec->data_str && key &&
	    udb_check_password(key, chan_rec, client))
	{
		/* A supplied password remains an admin authentication, even if invited. */
		udb_channel_invite_grant_take(client, channel, 1);
		udb_channel_pending_auth_set(client, channel);
	} else
	{
		udb_channel_invite_grant_take(client, channel, 1);
	}
	return HOOK_CONTINUE;
}

static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg)
{
	static char errbuf[512];
	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	if (!chan_rec)
		return 0;

	UdbRecord *forbid_rec = udb_record_find(udb_ctx, CKEY_FORBID, chan_rec);
	if (forbid_rec)
	{
		snprintf(errbuf, sizeof(errbuf), "%%s :%s", forbid_rec->data_str ? forbid_rec->data_str : "Channel is forbidden");
		*errmsg = errbuf;
		return ERR_FORBIDDENCHANNEL;
	}

	int is_founder = udb_channel_is_identified_founder(client, chan_rec);
	int has_invite_grant = udb_channel_invite_grant_take(client, channel, 0);

	UdbRecord *pass_rec = udb_record_find(udb_ctx, CKEY_PASS, chan_rec);
	if (pass_rec && pass_rec->data_str && *pass_rec->data_str && !is_founder && !has_invite_grant)
	{
		if (!key || !udb_check_password(key, chan_rec, client))
		{
			*errmsg = STR_ERR_BADCHANNELKEY;
			return ERR_BADCHANNELKEY;
		}
	}

	UdbRecord *access_rec = udb_record_find(udb_ctx, CKEY_ACCESS, chan_rec);
	if (access_rec && !is_founder)
	{
		UdbRecord *acc_entry = udb_record_find(udb_ctx, client->name, access_rec);
		if (!acc_entry || !has_user_mode(client, 'r'))
		{
			snprintf(errbuf, sizeof(errbuf), "%%s :You must be authorized and identified to join this channel");
			*errmsg = errbuf;
			return ERR_FORBIDDENCHANNEL;
		}
	}
	return 0;
}

static void handle_join(Client *client, Channel *channel, MessageTag *mtags)
{
	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	if (!chan_rec)
		return;

	int is_founder = udb_channel_is_identified_founder(client, chan_rec);

	if (channel->users == 1)
	{
		UdbRecord *susp_rec = udb_record_find(udb_ctx, CKEY_SUSPENDED, chan_rec);

		/* A registered channel assigns founder authority exclusively as +q. */
		if (!IsServer(client) && !IsULine(client))
		{
			udb_channel_do_mode(channel, mtags, "-o", client->name);
		}

		if (!susp_rec)
		{
			udb_channel_do_mode(channel, mtags, "+r", "");
		}

		udb_channel_apply_subrecord(udb_ctx, channel, chan_rec, CKEY_MODES, 0);
		udb_channel_apply_subrecord(udb_ctx, channel, chan_rec, CKEY_TOPIC, 0);
		if (udb_record_find(udb_ctx, CKEY_PERSISTENT, chan_rec))
			udb_channel_set_persistent(channel, 1);
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

static int udb_channel_mode_has_change(const char *modes)
{
	return modes && (strchr(modes, '+') || strchr(modes, '-'));
}

static int udb_channel_mode_has_ban_add(const char *modes)
{
	int what = 0;

	for (; modes && *modes; modes++)
	{
		if (*modes == '+')
			what = MODE_ADD;
		else if (*modes == '-')
			what = MODE_DEL;
		else if (*modes == 'b' && what == MODE_ADD)
			return 1;
	}
	return 0;
}

static int udb_channel_blocks_ban_removal(Client *client, Channel *channel,
                                          int parc, const char *parv[])
{
	const char *modes = parv[2];
	int what = 0;
	int param = 3;

	for (; modes && *modes; modes++)
	{
		Cmode *handler;
		int takes_parameter;

		if (*modes == '+')
		{
			what = MODE_ADD;
			continue;
		}
		if (*modes == '-')
		{
			what = MODE_DEL;
			continue;
		}
		handler = find_channel_mode_handler(*modes);
		takes_parameter = handler && handler->paracount &&
		                  (what == MODE_ADD || handler->unset_with_param);
		if (!takes_parameter || param >= parc)
			continue;
		if (*modes == 'b' && what == MODE_DEL)
		{
			const char *ban = clean_ban_mask(parv[param], MODE_DEL, EXBTYPE_BAN,
			                                 client, channel, 0);
			UdbBanOwner *owner = udb_channel_ban_owner_find(channel, ban);
			if (owner && strcasecmp(owner->owner, client->name))
			{
				sendnotice(client, "You may not remove the UDB-protected ban %s", ban);
				return 1;
			}
		}
		param++;
	}
	return 0;
}

CMD_OVERRIDE_FUNC(udb_override_invite)
{
	Channel *channel;
	Client *target;
	UdbRecord *chan_rec, *pass_rec;

	if (!MyUser(client) || parc < 4 || !udb_ctx || !udb_ctx->channels)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	channel = find_channel(parv[2]);
	target = find_user(parv[1], NULL);
	chan_rec = channel ? udb_record_find(udb_ctx, channel->name, udb_ctx->channels) : NULL;
	pass_rec = chan_rec ? udb_record_find(udb_ctx, CKEY_PASS, chan_rec) : NULL;
	if (!pass_rec || !pass_rec->data_str || !*pass_rec->data_str)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	if (!channel || !target)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	if (!MyUser(target))
	{
		sendnotice(client, "Password INVITE requires a local target");
		return;
	}
	if (!udb_check_password(parv[3], chan_rec, client))
	{
		sendnumeric(client, ERR_BADCHANNELKEY, channel->name);
		return;
	}
	CALL_NEXT_COMMAND_OVERRIDE();
	if (is_invited(target, channel))
		udb_channel_invite_grant_set(target, channel);
}

CMD_OVERRIDE_FUNC(udb_override_mode)
{
	Channel *channel;
	char channel_name[CHANNELLEN + 1];
	UdbRecord *chan_rec, *options_rec;
	UdbBanSnapshot *snapshot = NULL;
	int is_founder;
	int protect_bans;

	if (!MyUser(client) || parc < 3 || !IsChannelName(parv[1]))
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	channel = find_channel(parv[1]);
	chan_rec = (channel && udb_ctx && udb_ctx->channels) ? udb_record_find(udb_ctx, channel->name, udb_ctx->channels) : NULL;
	options_rec = chan_rec ? udb_record_find(udb_ctx, CKEY_OPTIONS, chan_rec) : NULL;
	if (!chan_rec)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	strlcpy(channel_name, channel->name, sizeof(channel_name));
	is_founder = udb_channel_is_identified_founder(client, chan_rec);
	if (options_rec && (options_rec->data_num & UDB_CHOPT_LOCK_MODES) &&
	    udb_channel_mode_has_change(parv[2]) && !is_founder)
	{
		sendnotice(client, "You do not have permission to change modes in %s (locked by UDB)", channel->name);
		return;
	}
	protect_bans = options_rec && (options_rec->data_num & UDB_CHOPT_PROTECT_BANS);
	udb_channel_ban_owners_prune(channel);
	if (protect_bans && !is_founder && !IsOper(client) &&
	    udb_channel_blocks_ban_removal(client, channel, parc, parv))
		return;
	/* Retain local ban ownership even before the protection option is enabled. */
	if (udb_channel_mode_has_ban_add(parv[2]))
		snapshot = udb_channel_ban_snapshot(channel);
	CALL_NEXT_COMMAND_OVERRIDE();
	channel = find_channel(channel_name);
	if (snapshot)
	{
		if (channel)
			udb_channel_track_new_bans(channel, client, snapshot);
		udb_channel_ban_snapshot_free(snapshot);
	}
	if (channel)
		udb_channel_ban_owners_prune(channel);
}

static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic)
{
	if (IsServer(client))
		return topic;

	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	if (!chan_rec)
		return topic;

	UdbRecord *topic_rec = udb_record_find(udb_ctx, CKEY_TOPIC, chan_rec);
	if (topic_rec)
	{
		int is_founder = udb_channel_is_identified_founder(client, chan_rec);
		if (!is_founder)
		{
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

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_invite_grant";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_channel_invite_grant_free;
	udb_channel_invite_grant_md = ModDataAdd(modinfo->handle, mreq);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_ban_owners";
	mreq.type = MODDATATYPE_CHANNEL;
	mreq.free = udb_channel_ban_owners_free;
	udb_channel_ban_owners_md = ModDataAdd(modinfo->handle, mreq);

	HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, udb_hook_can_join);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, udb_hook_pre_local_join);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, udb_hook_local_join);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_JOIN, 0, udb_hook_remote_join);
	HookAddConstString(modinfo->handle, HOOKTYPE_PRE_LOCAL_TOPIC, 0, udb_hook_pre_topic);
}

static int udb_channels_load(ModuleInfo *modinfo)
{
	CommandOverrideAdd(modinfo->handle, "INVITE", 0, udb_override_invite);
	CommandOverrideAdd(modinfo->handle, "MODE", 0, udb_override_mode);
	CommandOverrideAdd(modinfo->handle, "SAMODE", 0, udb_override_mode);
	return 0;
}
