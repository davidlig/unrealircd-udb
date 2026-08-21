/*
 * UDB Runtime Effects for UnrealIRCd 6
 * Dispatches special records to their concrete per-block effect implementations.
 */

static int udb_apply_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new)
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
		udb_channel_apply_record(ctx, block, rec, is_new);
	} else if (block->letter == 'I')
	{
		udb_ips_apply_effect(ctx, block, rec, is_new);
	} else if (block->letter == 'S')
	{
		if (!udb_settings_apply_record(ctx, rec))
			udb_log(ULOG_WARNING, "UDB_SETTING_INVALID", NULL,
			        "Ignoring invalid or unsupported S::$setting", log_data_string("setting", rec->key));
	} else if (block->letter == 'L')
	{
		udb_link_apply_record(ctx, rec);
	} else if (block->letter == 'K')
	{
		udb_lines_apply_effect(ctx, block, rec, is_new);
	}
	return 1;
}

static void udb_remove_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (!rec)
		return;
	if (block->letter == 'N')
	{
		udb_nick_remove_record(block, rec);
	} else if (block->letter == 'C')
	{
		udb_channel_remove_record(ctx, block, rec);
	} else if (block->letter == 'I')
	{
		udb_ips_remove_effect(ctx, block, rec);
	} else if (block->letter == 'S')
	{
		udb_settings_remove_record(ctx, rec);
	} else if (block->letter == 'L')
	{
		udb_link_remove_record(ctx, rec);
	} else if (block->letter == 'K')
	{
		udb_lines_remove_effect(ctx, block, rec);
	}
}
