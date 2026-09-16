/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Runtime Effects Dispatch & Live State Reconciliation
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static void udb_nick_apply_effect(UdbBlock *block, UdbRecord *rec, int is_new)
{
	UdbRecord *nick_rec = rec->parent == block->tree ? rec : rec->parent;
	UdbNickApplyReason reason = is_new ? UDB_NICK_APPLY_REFRESH : UDB_NICK_APPLY_ADOPT;
	Client *client = find_user(nick_rec->key, NULL);

	if (nick_rec->nick_unsuspend_transition)
	{
		nick_rec->nick_unsuspend_transition = 0;
		reason = UDB_NICK_APPLY_UNSUSPEND;
	}
	if (client && MyUser(client))
		udb_nick_apply(client, nick_rec, reason);
}

static void udb_apply_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new)
{
	if (!rec)
		return;
	switch (block->letter)
	{
	case UDB_BLOCK_NICKS:
		udb_nick_apply_effect(block, rec, is_new);
		break;
	case UDB_BLOCK_CHANNELS:
		udb_channel_apply_record(ctx, block, rec, is_new);
		break;
	case UDB_BLOCK_IPS:
		udb_ips_apply_effect(ctx, block, rec);
		break;
	case UDB_BLOCK_SETTINGS:
	case UDB_BLOCK_LINKS:
		udb_config_apply_effect(ctx, block, rec);
		break;
	case UDB_BLOCK_LINES:
		udb_lines_apply_effect(ctx, block, rec);
		break;
	default:
		break;
	}
}

static void udb_remove_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (!rec)
		return;
	switch (block->letter)
	{
	case UDB_BLOCK_NICKS:
		udb_nick_remove_record(block, rec);
		break;
	case UDB_BLOCK_CHANNELS:
		udb_channel_remove_record(ctx, block, rec);
		break;
	case UDB_BLOCK_IPS:
		udb_ips_remove_effect(ctx, block, rec);
		break;
	case UDB_BLOCK_SETTINGS:
	case UDB_BLOCK_LINKS:
		udb_config_remove_effect(ctx, block, rec);
		break;
	case UDB_BLOCK_LINES:
		udb_lines_remove_effect(ctx, block, rec);
		break;
	default:
		break;
	}
}

static void udb_tree_effects_walk(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, unsigned int depth, int remove)
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
	case UDB_BLOCK_NICKS:
		apply = depth == 1;
		break;
	case UDB_BLOCK_CHANNELS:
	case UDB_BLOCK_IPS:
		apply = depth == (remove ? 1 : 2);
		break;
	case UDB_BLOCK_LINES:
		apply = depth == 2;
		break;
	case UDB_BLOCK_SETTINGS:
	case UDB_BLOCK_LINKS:
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
