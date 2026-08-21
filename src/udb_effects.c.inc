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
	} else if (block->letter == 'S' || block->letter == 'L')
	{
		udb_config_apply_effect(ctx, block, rec);
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
	} else if (block->letter == 'S' || block->letter == 'L')
	{
		udb_config_remove_effect(ctx, block, rec);
	} else if (block->letter == 'K')
	{
		udb_lines_remove_effect(ctx, block, rec);
	}
}

static void udb_tree_effects_walk(UdbContext *ctx, UdbBlock *block, UdbRecord *rec,
                                  unsigned int depth, int remove)
{
	UdbRecord *child;
	int apply = 0;

	if (!rec)
		return;
	for (child = rec->child; child; child = child->sibling)
		udb_tree_effects_walk(ctx, block, child, depth + 1, remove);

	/* Select each runtime owner once; K patterns are below their line type. */
	switch (block->letter)
	{
		case 'N':
			apply = depth == 1;
			break;
		case 'C':
		case 'I':
			apply = depth == (remove ? 1 : 2);
			break;
		case 'K':
			apply = depth == 2;
			break;
		case 'S':
		case 'L':
			apply = 1;
			break;
	}
	if (apply)
	{
		if (remove)
			udb_remove_special_record(ctx, block, rec);
		else
			udb_apply_special_record(ctx, block, rec, 1);
	}
}

static void udb_apply_tree_effects(UdbContext *ctx, UdbBlock *block)
{
	if (ctx && block && block->tree)
		udb_tree_effects_walk(ctx, block, block->tree, 0, 0);
}

static void udb_remove_tree_effects(UdbContext *ctx, UdbBlock *block)
{
	if (ctx && block && block->tree)
		udb_tree_effects_walk(ctx, block, block->tree, 0, 1);
}
