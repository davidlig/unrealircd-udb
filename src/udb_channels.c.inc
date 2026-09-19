/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Channel Registration, Founder +q & Channel Options
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

typedef struct UdbChannelModeState UdbChannelModeState;
typedef struct UdbBanOwner UdbBanOwner;
typedef struct UdbBanSnapshot UdbBanSnapshot;

struct UdbChannelModeState
{
	char *value;
};

struct UdbBanOwner
{
	UdbBanOwner *next;
	char *ban;
	char *owner;
};

struct UdbBanSnapshot
{
	UdbBanSnapshot *next;
	char *ban;
};

static ModDataInfo *udb_channel_modes_md = NULL;
static ModDataInfo *udb_channel_ban_owners_md = NULL;

/* Forward declarations */
static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg);
static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key);
static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_pre_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf,
								 const char *parabuf, time_t sendts, int samode);
static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic);
CMD_OVERRIDE_FUNC(udb_override_mode);

static int udb_channel_is_identified_founder(Client *client, UdbRecord *chan_rec)
{
	UdbRecord *founder_rec;

	if (!client || !chan_rec)
		return 0;
	founder_rec = udb_record_find(udb_ctx, CKEY_FOUNDER, chan_rec);
	return founder_rec && founder_rec->data_str && !strcasecmp(client->name, founder_rec->data_str) &&
		   has_user_mode(client, 'r');
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

static void udb_channel_do_mode(Channel *channel, MessageTag *mtags, const char *modes, const char *parameters)
{
	char buf[512];
	char channel_name[CHANNELLEN + 1];
	char *p, *param;
	int myparc = 1;
	int i;
	char *myparv[MAXMODEPARAMS + 2];
	Client *source;

	if (!channel || !modes || !*modes || !do_mode)
		return;
	/* Removing +P may destroy an empty channel inside do_mode(). Keep only the
	 * immutable name needed by the post-operation debug notice. */
	strlcpy(channel_name, channel->name, sizeof(channel_name));
	source = udb_service_source(SKEY_CHANSERV);
	memset(myparv, 0, sizeof(myparv));
	myparv[0] = raw_strdup(modes);
	strlcpy(buf, parameters ? parameters : "", sizeof(buf));
	for (param = strtoken(&p, buf, " "); param; param = strtoken(&p, NULL, " "))
	{
		if (myparc > MAXMODEPARAMS)
		{
			for (i = 0; i < myparc; i++)
				safe_free(myparv[i]);
			return;
		}
		myparv[myparc++] = raw_strdup(param);
	}
	myparv[myparc] = NULL;

	do_mode(channel, source, mtags, myparc, (const char **)myparv, 0, 1);
	for (i = 0; i < myparc; i++)
		safe_free(myparv[i]);

	if (parameters && *parameters)
		udb_send_to_debugs(NULL, "Mode change on %s: %s %s", channel_name, modes,
						   strchr(modes, 'k') ? "<redacted>" : parameters);
	else
		udb_send_to_debugs(NULL, "Mode change on %s: %s", channel_name, modes);
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

static void udb_channel_remove_modes(Channel *channel)
{
	UdbChannelModeState *state;

	if (!udb_channel_modes_md)
		return;
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (state)
	{
		safe_free(state->value);
		safe_free(state);
		moddata_channel(channel, udb_channel_modes_md).ptr = NULL;
	}
}

static int udb_channel_is_persistent(UdbContext *ctx, UdbRecord *chan_rec)
{
	UdbRecord *rec = chan_rec ? udb_record_find(ctx, CKEY_OPTIONS, chan_rec) : NULL;
	return rec && !rec->data_str && (rec->data_num & UDB_CHOPT_PERSISTENT);
}

static int udb_channel_is_lock_modes(UdbContext *ctx, UdbRecord *chan_rec)
{
	UdbRecord *rec = chan_rec ? udb_record_find(ctx, CKEY_OPTIONS, chan_rec) : NULL;
	return rec && !rec->data_str && (rec->data_num & UDB_CHOPT_LOCK_MODES);
}

static int udb_channel_is_lock_topic(UdbContext *ctx, UdbRecord *chan_rec)
{
	UdbRecord *rec = chan_rec ? udb_record_find(ctx, CKEY_OPTIONS, chan_rec) : NULL;
	return rec && !rec->data_str && (rec->data_num & UDB_CHOPT_LOCK_TOPIC);
}

static int udb_channel_is_protect_bans(UdbContext *ctx, UdbRecord *chan_rec)
{
	UdbRecord *rec = chan_rec ? udb_record_find(ctx, CKEY_OPTIONS, chan_rec) : NULL;
	return rec && !rec->data_str && (rec->data_num & UDB_CHOPT_PROTECT_BANS);
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

static UdbBanOwner *udb_channel_ban_owner_find(Channel *channel, const char *ban)
{
	UdbBanOwner *owner;

	if (!udb_channel_ban_owners_md || !ban)
		return NULL;
	for (owner = moddata_channel(channel, udb_channel_ban_owners_md).ptr; owner; owner = owner->next)
		if (!mycmp(owner->ban, ban))
			return owner;
	return NULL;
}

static void udb_channel_ban_owners_prune(Channel *channel)
{
	UdbBanOwner **owner;

	if (!udb_channel_ban_owners_md)
		return;
	for (owner = (UdbBanOwner **)&moddata_channel(channel, udb_channel_ban_owners_md).ptr; *owner;)
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
		if (udb_channel_ban_was_present(snapshot, ban->banstr) || udb_channel_ban_owner_find(channel, ban->banstr))
			continue;
		owner = safe_alloc(sizeof(*owner));
		safe_strdup(owner->ban, ban->banstr);
		safe_strdup(owner->owner, client->name);
		owner->next = moddata_channel(channel, udb_channel_ban_owners_md).ptr;
		moddata_channel(channel, udb_channel_ban_owners_md).ptr = owner;
	}
}

static void udb_channel_reconcile_founder(Channel *channel, UdbRecord *chan_rec, int suspended)
{
	Member *member;

	for (member = channel->members; member; member = member->next)
	{
		/* The member's home server emits the network MODE exactly once. */
		if (!MyUser(member->client))
			continue;
		int is_founder = !suspended && udb_channel_is_identified_founder(member->client, chan_rec);
		if (is_founder)
		{
			if (!check_channel_access_member(member, "q"))
				udb_channel_do_mode(channel, NULL, "+q", member->client->name);
		}
		else if (check_channel_access_member(member, "q"))
		{
			/* UDB owns founder +q, so a profile replacement has one owner only. */
			udb_channel_do_mode(channel, NULL, "-q", member->client->name);
		}
	}
}

static void udb_channel_reconcile_registered(Channel *channel, int registered, int suspended, MessageTag *mtags)
{
	if (!channel || !find_channel_mode_handler('r'))
		return;
	if (registered && !suspended)
	{
		if (!has_channel_mode(channel, 'r'))
			udb_channel_do_mode(channel, mtags, "+r", "");
	}
	else if (has_channel_mode(channel, 'r'))
	{
		udb_channel_do_mode(channel, mtags, "-r", "");
	}
}

static void udb_channel_clear_topic(Channel *channel)
{
	Client *source = udb_service_source(SKEY_CHANSERV);

	safe_free(channel->topic);
	safe_free(channel->topic_nick);
	channel->topic_time = 0;
	if (channel->users > 0)
	{
		sendto_channel(channel, source, NULL, 0, 0, SEND_LOCAL, NULL, ":%s TOPIC %s :", source->name, channel->name);
	}
}

static void udb_channel_apply_topic(Channel *channel, UdbRecord *topic_rec)
{
	Client *source;

	if (!topic_rec || !topic_rec->data_str)
		return;
	if (channel->topic && !strcmp(channel->topic, topic_rec->data_str))
		return;
	source = udb_service_source(SKEY_CHANSERV);
	if (set_channel_topic)
		set_channel_topic(source, channel, NULL, topic_rec->data_str, source->name, TStime());
}

static void udb_channel_apply_subrecord(UdbContext *ctx, Channel *channel, UdbRecord *chan_rec, const char *subkey,
										int is_new)
{
	/* A channel-profile replacement revokes every UDB-owned channel state;
	 * mirror that removal so the surviving profile restores it all. */
	if (!strcasecmp(subkey, chan_rec->key))
	{
		UdbRecord *mode_rec = udb_record_find(ctx, CKEY_MODES, chan_rec);
		UdbRecord *topic_rec = udb_record_find(ctx, CKEY_TOPIC, chan_rec);
		int suspended = udb_record_find(ctx, CKEY_SUSPEND, chan_rec) != NULL;

		udb_channel_reconcile_founder(channel, chan_rec, suspended);
		if (mode_rec && mode_rec->data_str)
			udb_channel_apply_modes(channel, mode_rec->data_str);
		udb_channel_apply_topic(channel, topic_rec);
		/* -P may destroy an empty channel, so it is applied last. */
		udb_channel_set_persistent(channel, udb_channel_is_persistent(ctx, chan_rec));
		return;
	}

	if (!strcmp(subkey, CKEY_OPTIONS))
	{
		udb_channel_set_persistent(channel, udb_channel_is_persistent(ctx, chan_rec));
		return;
	}

	UdbRecord *sub_rec = udb_record_find(ctx, subkey, chan_rec);
	if (!sub_rec || !sub_rec->data_str)
		return;

	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, chan_rec, udb_record_find(ctx, CKEY_SUSPEND, chan_rec) != NULL);
	}
	else if (!strcmp(subkey, CKEY_MODES))
	{
		udb_channel_apply_modes(channel, sub_rec->data_str);
	}
	else if (!strcmp(subkey, CKEY_TOPIC))
	{
		udb_channel_apply_topic(channel, sub_rec);
	}
	else if (!strcmp(subkey, CKEY_SUSPEND))
	{
		udb_channel_reconcile_founder(channel, chan_rec, 1);
	}
}

static void udb_channel_remove_subrecord(UdbContext *ctx, Channel *channel, UdbRecord *chan_rec, const char *subkey)
{
	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, NULL, 0);
	}
	else if (!strcmp(subkey, CKEY_MODES))
	{
		udb_channel_remove_modes(channel);
	}
	else if (!strcmp(subkey, CKEY_OPTIONS))
	{
		udb_channel_set_persistent(channel, 0);
	}
	else if (!strcmp(subkey, CKEY_TOPIC))
	{
		udb_channel_clear_topic(channel);
	}
	else if (!strcmp(subkey, CKEY_SUSPEND))
	{
		/* Lifting a suspension restores the registered marker and founder +q. */
		udb_channel_reconcile_founder(channel, chan_rec, 0);
		udb_channel_reconcile_registered(channel, 1, 0, NULL);
	}
	else if (!strcasecmp(subkey, chan_rec->key))
	{
		int had_topic = udb_record_find(ctx, CKEY_TOPIC, chan_rec) != NULL;

		udb_channel_reconcile_founder(channel, NULL, 0);
		udb_channel_reconcile_registered(channel, 0, 0, NULL);
		udb_channel_remove_modes(channel);
		if (had_topic)
			udb_channel_clear_topic(channel);
		/* -P may destroy an empty channel, so it is applied last. */
		udb_channel_set_persistent(channel, 0);
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
	if (!channel && udb_channel_is_persistent(ctx, chan_rec) && find_channel_mode_handler('P'))
	{
		channel = make_channel(chan_rec->key);
		channel->creationtime = TStime();
	}
	if (channel)
	{
		udb_channel_apply_subrecord(ctx, channel, chan_rec, rec->key, is_new);
		/* Dropping -P may have destroyed an empty channel. */
		channel = find_channel(chan_rec->key);
		if (channel)
			udb_channel_reconcile_registered(channel, 1, udb_record_find(ctx, CKEY_SUSPEND, chan_rec) != NULL, NULL);
	}
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
	(void)key;
	if (!chan_rec)
		return HOOK_CONTINUE;
	if (udb_record_find(udb_ctx, CKEY_FORBID, chan_rec))
		return HOOK_CONTINUE; /* Let CAN_JOIN provide the numeric. */
	if (udb_channel_is_identified_founder(client, chan_rec))
		return HOOK_ALLOW; /* Preserve the established founder exemption. */
	return HOOK_CONTINUE;
}

static int udb_channel_modes_key(const char *value, char *key_out, size_t key_out_size)
{
	char modebuf[512];
	char *p, *param;
	const char *modes;
	int what = MODE_ADD;

	if (key_out && key_out_size)
		key_out[0] = '\0';
	if (!value || !*value || strlen(value) >= sizeof(modebuf))
		return 0;
	strlcpy(modebuf, value, sizeof(modebuf));
	modes = strtoken(&p, modebuf, " ");
	if (!modes)
		return 0;
	for (; *modes; modes++)
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
		if (!handler)
			return 0;
		takes_parameter = handler->paracount && (what == MODE_ADD || handler->unset_with_param);
		if (!takes_parameter)
			continue;
		param = strtoken(&p, NULL, " ");
		if (!param)
			return 0;
		if (*modes == 'k' && what == MODE_ADD)
		{
			if (!key_out || !key_out_size || strlcpy(key_out, param, key_out_size) >= key_out_size)
				return 0;
			return 1;
		}
	}
	return 0;
}

static void udb_escape_percent(const char *src, char *dst, size_t dst_size)
{
	size_t pos = 0;

	if (!dst || dst_size == 0)
		return;
	if (src)
	{
		for (; *src && pos + 1 < dst_size; src++)
		{
			if (*src == '%')
			{
				if (pos + 2 >= dst_size)
					break;
				dst[pos++] = '%';
			}
			dst[pos++] = *src;
		}
	}
	dst[pos] = '\0';
}

static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg)
{
	static char errbuf[512];
	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	UdbRecord *forbid_rec;
	UdbRecord *access_rec;
	int is_founder;

	if (!chan_rec)
		return 0;
	forbid_rec = udb_record_find(udb_ctx, CKEY_FORBID, chan_rec);
	if (forbid_rec)
	{
		char escaped_reason[512];

		udb_escape_percent(forbid_rec->data_str ? forbid_rec->data_str : "No reason given", escaped_reason,
						   sizeof(escaped_reason));
		snprintf(errbuf, sizeof(errbuf), "%%s :This channel is forbidden. Reason: %s", escaped_reason);
		*errmsg = errbuf;
		return ERR_FORBIDDENCHANNEL;
	}
	is_founder = udb_channel_is_identified_founder(client, chan_rec);
	/* Native +k normally materializes after the first successful JOIN, but a
	 * persistent empty channel can already have one.  Never let the persisted
	 * C::modes key compete with the native mode when it is materialized. */
	if (channel->users == 0 && !cm_getparameter(channel, 'k') && !is_founder)
	{
		UdbRecord *modes_rec = udb_record_find(udb_ctx, CKEY_MODES, chan_rec);
		char configured_key[512];
		if (modes_rec && modes_rec->data_str &&
			udb_channel_modes_key(modes_rec->data_str, configured_key, sizeof(configured_key)) &&
			(!key || strcmp(key, configured_key)))
		{
			*errmsg = STR_ERR_BADCHANNELKEY;
			return ERR_BADCHANNELKEY;
		}
	}
	access_rec = udb_record_find(udb_ctx, CKEY_ACCESS, chan_rec);
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
	UdbRecord *suspend_rec;
	int suspended;

	if (!chan_rec)
		return;

	suspend_rec = udb_record_find(udb_ctx, CKEY_SUSPEND, chan_rec);
	suspended = suspend_rec != NULL;
	if (suspended && MyUser(client) && !IsULine(client))
		udb_send_service_notice(client, SKEY_CHANSERV, "This channel is suspended. Reason: %s",
								suspend_rec->data_str ? suspend_rec->data_str : "No reason given");

	if (channel->users == 1)
	{
		/* A registered channel assigns founder authority exclusively as +q. */
		if (!IsServer(client) && !IsULine(client))
		{
			udb_channel_do_mode(channel, mtags, "-o", client->name);
		}

		udb_channel_reconcile_registered(channel, 1, suspended, mtags);
		udb_channel_apply_subrecord(udb_ctx, channel, chan_rec, CKEY_MODES, 0);
		udb_channel_apply_subrecord(udb_ctx, channel, chan_rec, CKEY_TOPIC, 0);
		udb_channel_set_persistent(channel, udb_channel_is_persistent(udb_ctx, chan_rec));
	}

	/* Founder +q is UDB-owned and must have exactly one current holder. */
	udb_channel_reconcile_founder(channel, chan_rec, suspended);
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

static int udb_channel_mode_has_change(const char *modes)
{
	int what = 0;

	for (; modes && *modes; modes++)
	{
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
		if (what != 0)
		{
			/* List modes b, e, I are channel lists and are not locked by lock_modes */
			if (*modes == 'b' || *modes == 'e' || *modes == 'I')
				continue;
			return 1;
		}
	}
	return 0;
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

static int udb_channel_blocks_ban_removal(Client *client, Channel *channel, int parc, const char *parv[])
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
		if (*modes == 'b' || *modes == 'e' || *modes == 'I')
		{
			takes_parameter = 1;
		}
		else
		{
			handler = find_channel_mode_handler(*modes);
			takes_parameter = handler && handler->paracount && (what == MODE_ADD || handler->unset_with_param);
		}
		if (!takes_parameter || param >= parc)
			continue;
		if (*modes == 'b' && what == MODE_DEL)
		{
			const char *ban = clean_ban_mask(parv[param], MODE_DEL, EXBTYPE_BAN, client, channel, 0);
			UdbBanOwner *owner = udb_channel_ban_owner_find(channel, ban);
			if (owner && strcasecmp(owner->owner, client->name))
			{
				udb_send_service_notice(client, SKEY_CHANSERV, "You may not remove the UDB-protected ban %s",
										ban ? ban : parv[param]);
				return 1;
			}
		}
		param++;
	}
	return 0;
}

CMD_OVERRIDE_FUNC(udb_override_mode)
{
	Channel *channel;
	char channel_name[CHANNELLEN + 1];
	UdbRecord *chan_rec;
	UdbBanSnapshot *snapshot = NULL;
	int is_founder;
	int had_ban_add = 0;

	if (!MyUser(client) || parc < 3 || !IsChannelName(parv[1]))
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	channel = find_channel(parv[1]);
	chan_rec =
		(channel && udb_ctx && udb_ctx->channels) ? udb_record_find(udb_ctx, channel->name, udb_ctx->channels) : NULL;
	if (!chan_rec)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	strlcpy(channel_name, channel->name, sizeof(channel_name));
	is_founder = udb_channel_is_identified_founder(client, chan_rec);
	if (udb_channel_is_lock_modes(udb_ctx, chan_rec) && udb_channel_mode_has_change(parv[2]))
	{
		udb_send_service_notice(client, SKEY_CHANSERV,
								"You do not have permission to change modes in %s (locked by UDB)", channel->name);
		return;
	}
	udb_channel_ban_owners_prune(channel);
	if (udb_channel_is_protect_bans(udb_ctx, chan_rec) && !is_founder && !IsOper(client) &&
		udb_channel_blocks_ban_removal(client, channel, parc, parv))
		return;
	/* Retain local ban ownership even before the protection option is enabled. */
	if (udb_channel_mode_has_ban_add(parv[2]))
	{
		had_ban_add = 1;
		snapshot = udb_channel_ban_snapshot(channel);
	}
	CALL_NEXT_COMMAND_OVERRIDE();
	channel = find_channel(channel_name);
	if (had_ban_add)
	{
		if (channel)
			udb_channel_track_new_bans(channel, client, snapshot);
		udb_channel_ban_snapshot_free(snapshot);
	}
	if (channel)
		udb_channel_ban_owners_prune(channel);
}

CMD_OVERRIDE_FUNC(udb_override_topic)
{
	Channel *channel;
	UdbRecord *chan_rec;

	if (!MyUser(client) || parc < 2 || !IsChannelName(parv[1]))
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	if (parc < 3 || BadPtr(parv[2]))
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	channel = find_channel(parv[1]);
	chan_rec =
		(channel && udb_ctx && udb_ctx->channels) ? udb_record_find(udb_ctx, channel->name, udb_ctx->channels) : NULL;
	if (!chan_rec)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	if (udb_channel_is_lock_topic(udb_ctx, chan_rec))
	{
		udb_send_service_notice(client, SKEY_CHANSERV,
								"You do not have permission to change the topic in %s (locked by UDB)", channel->name);
		return;
	}
	UdbRecord *topic_rec = udb_record_find(udb_ctx, CKEY_TOPIC, chan_rec);
	if (topic_rec)
	{
		int is_founder = udb_channel_is_identified_founder(client, chan_rec);
		if (!is_founder)
		{
			sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
			return;
		}
	}
	CALL_NEXT_COMMAND_OVERRIDE();
}

static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic)
{
	if (IsServer(client))
		return topic;

	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	if (!chan_rec)
		return topic;

	if (udb_channel_is_lock_topic(udb_ctx, chan_rec))
	{
		udb_send_service_notice(client, SKEY_CHANSERV,
								"You do not have permission to change the topic in %s (locked by UDB)", channel->name);
		return NULL;
	}

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
	mreq.name = "udb_channel_modes";
	mreq.type = MODDATATYPE_CHANNEL;
	mreq.free = udb_channel_modes_free;
	udb_channel_modes_md = ModDataAdd(modinfo->handle, mreq);

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
	CommandOverrideAdd(modinfo->handle, "MODE", 0, udb_override_mode);
	CommandOverrideAdd(modinfo->handle, "SAMODE", 0, udb_override_mode);
	CommandOverrideAdd(modinfo->handle, "TOPIC", 0, udb_override_topic);
	return 0;
}
