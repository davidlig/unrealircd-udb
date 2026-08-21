/*
 * UDB Runtime Effects for UnrealIRCd 6
 * Routes special records to their concrete nick, channel, IP, setting, link,
 * and line effect implementations.
 */

static int udb_apply_special_record(UdbBlock *block, UdbRecord *rec, int is_new)
{
	if (!rec)
		return 0;
	if (block->letter == 'N')
	{
		UdbRecord *nick_rec = rec->parent == block->tree ? rec : rec->parent;
		Client *client = find_user(nick_rec->key, NULL);
		if (client && MyUser(client))
		{
			udb_nick_apply(client, nick_rec, is_new);
		}
	} else if (block->letter == 'C')
	{
		UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
		Channel *channel = find_channel(chan_rec->key);
		if (channel)
		{
			udb_channel_apply_record(channel, chan_rec, rec->key, is_new);
		}
	} else if (block->letter == 'I')
	{
		UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
		udb_ip_apply_record(ip_rec->key, ip_rec, rec->key, is_new);
	} else if (block->letter == 'S')
	{
		if (!udb_settings_apply_record(rec))
			udb_log(ULOG_WARNING, "UDB_SETTING_INVALID", NULL,
			        "Ignoring invalid or unsupported S::$setting", log_data_string("setting", rec->key));
	} else if (block->letter == 'L')
	{
		udb_link_apply_record(rec);
	} else if (block->letter == 'K')
	{
		udb_line_apply_record(rec, is_new);
	}
	return 1;
}

static void udb_remove_special_record(UdbBlock *block, UdbRecord *rec)
{
	if (!rec)
		return;
	if (block->letter == 'N')
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
				} else if (!strcmp(rec->key, NKEY_OPER))
				{
					udb_nick_revoke_oper(client);
				} else if (!strcmp(rec->key, NKEY_SWHOIS))
				{
					swhois_delete(client, "udb", "*", &me, NULL);
				} else if (!strcmp(rec->key, NKEY_MODES))
				{
					long old_umodes = client->umodes & ALL_UMODES;
					UdbRecord *mode_rec = udb_record_find(NKEY_MODES, nick_rec);
					if (mode_rec && mode_rec->data_str)
						client->umodes &= ~(set_usermode(mode_rec->data_str) & ~UMODE_OPER);
					send_umode_out(client, 1, old_umodes);
				} else if (!strcmp(rec->key, NKEY_SNOMASKS))
				{
					set_snomask(client, NULL);
				} else if (!strcmp(rec->key, NKEY_SUSPENDED))
				{
					long old_umodes = client->umodes & ALL_UMODES;
					client->umodes &= ~set_usermode("S");
					send_umode_out(client, 1, old_umodes);
				} else if (!strcmp(rec->key, NKEY_PASS))
				{
					udb_nick_strip(client, nick_rec);
				}
			}
		} else
		{
			Client *client = find_user(rec->key, NULL);
			if (client && MyUser(client))
			{
				udb_nick_strip(client, rec);
			}
		}
	} else if (block->letter == 'C')
	{
		UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
		Channel *channel = find_channel(chan_rec->key);
		if (channel)
		{
			udb_channel_remove_record(channel, chan_rec, rec->key);
		}
	} else if (block->letter == 'I')
	{
		UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
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
	} else if (block->letter == 'S')
	{
		udb_settings_remove_record(rec);
	} else if (block->letter == 'L')
	{
		udb_link_remove_record(rec);
	} else if (block->letter == 'K')
	{
		udb_line_remove_record(rec);
	}
}
