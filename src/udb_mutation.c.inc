/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Real-time Database Mutations & Propagator Resolution
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static const char *udb_propagator_policy(UdbContext *ctx)
{
	if (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator)
		return udb_cfg->propagator;
	if (ctx && ctx->propagator_setting && *ctx->propagator_setting)
		return ctx->propagator_setting;
	return ctx && ctx->startup_propagator_setting && *ctx->startup_propagator_setting ? ctx->startup_propagator_setting
																					  : NULL;
}

static int udb_propagator_policy_present(UdbContext *ctx)
{
	return udb_propagator_policy(ctx) != NULL;
}

/* Resolve the first locally usable candidate. Network-wide visibility is never
 * enough: a remote candidate must be a direct server peer and staged-sync use
 * additionally requires a completed HEL 4 negotiation. */
static int udb_select_propagator(UdbContext *ctx, int require_hello, UdbPropagatorSelection *selected)
{
	const char *cursor = udb_propagator_policy(ctx);
	char name[HOSTLEN + 1];
	int result;

	if (!selected)
		return 0;
	memset(selected, 0, sizeof(*selected));
	while (cursor && (result = udb_propagator_list_next(&cursor, name)) > 0)
	{
		Client *candidate;

		if (me.name[0] && !strcasecmp(name, me.name))
		{
			memcpy(selected->name, name, strlen(name) + 1);
			selected->is_local = 1;
			return 1;
		}
		candidate = find_server(name, NULL);
		if (!candidate || !IsServer(candidate) || !MyConnect(candidate))
			continue;
		if (require_hello && !udb_has_hello(candidate))
			continue;
		selected->peer = candidate;
		memcpy(selected->name, name, strlen(name) + 1);
		return 1;
	}
	return 0;
}

/* Checks if a directly connected client is our authorized upstream propagator */
static int udb_is_propagator(UdbContext *ctx, Client *server)
{
	UdbPropagatorSelection selected;

	if (!server || !IsServer(server) || !MyConnect(server))
		return 0;
	return udb_select_propagator(ctx, 1, &selected) && !selected.is_local && selected.peer == server;
}

static UdbBlock *udb_mutation_path_block(UdbContext *ctx, const char *path)
{
	size_t len;

	if (!path || !udb_record_fits_limits(path, NULL))
		return NULL;

	len = strlen(path);
	if (len < 4 || len > UDB_RECORD_PATH_MAX || path[1] != ':' || path[2] != ':' || !path[3])
		return NULL;

	return udb_block_by_letter(ctx, path[0]);
}

static const char *udb_mutation_safe_value(const char *path, const char *value)
{
	return udb_path_value_is_secret(path) ? "<redacted>" : (value ? value : "");
}

static int udb_mutation_forward_ins(Client *source, Client *except, const char *target, const char *epoch, uint64_t seq,
									const char *path, const char *data)
{
	return udb_sendto_confirmed_servers(except, ":%s DB %s INS %s %" PRIu64 " %s :%s", source->id, target, epoch, seq,
										path, data);
}

/* The stored value already equals the mutation payload, so no runtime effect
 * may change: an identical INS must never revoke and re-apply effects. */
static int udb_record_data_equals(UdbRecord *rec, const char *data)
{
	unsigned long val;

	if (!rec || !data)
		return 0;
	if (*data == '*')
		return !rec->data_str && udb_strtoul_strict(data + 1, &val) && rec->data_num == val;
	if (!rec->data_str)
		return 0;
	return !strcmp(rec->data_str, data);
}

static int udb_mutation_forward_del(Client *source, Client *except, const char *target, const char *epoch, uint64_t seq,
									const char *path)
{
	return udb_sendto_confirmed_servers(except, ":%s DB %s DEL %s %" PRIu64 " %s", source->id, target, epoch, seq,
										path);
}

static int udb_mutation_persist_error(Client *client, const char *subcmd, char letter)
{
	udb_protocol_mutation_error(client, subcmd, UDB_ERR_FATAL, letter);
	return 1;
}

typedef enum UdbSeqCheckResult
{
	UDB_SEQ_APPLY,
	UDB_SEQ_DUPLICATE,
	UDB_SEQ_GAP,
	UDB_SEQ_STALE_EPOCH
} UdbSeqCheckResult;

static void udb_mutation_trigger_gap_recovery(Client *direct_peer)
{
	(void)direct_peer;
	udb_sync_status = UDB_SYNC_DEGRADED;
	if (!udb_degraded_since)
		udb_degraded_since = time(NULL);
	udb_sync_hello_refresh_all();
	if (!udb_reconcile.next_retry_at)
		udb_reconcile.next_retry_at = time(NULL) + UDB_RECONCILE_RETRY_BASE;
}

static UdbSeqCheckResult udb_mutation_check_sequence(UdbContext *ctx, Client *source, Client *direct_peer,
													 const char *epoch, uint64_t seq)
{
	const char *source_sid = source && IsServer(source) ? source->id : NULL;

	if (!source_sid || !*source_sid)
		return UDB_SEQ_STALE_EPOCH;
	if (!ctx->authority_epoch_known)
	{
		strlcpy(ctx->authority_sid, source_sid, sizeof(ctx->authority_sid));
		strlcpy(ctx->authority_epoch, epoch, sizeof(ctx->authority_epoch));
		ctx->authority_epoch_known = 1;
	}
	else if (strcmp(source_sid, ctx->authority_sid) != 0 || strcmp(epoch, ctx->authority_epoch) != 0)
	{
		udb_log(ULOG_WARNING, "UDB_MUTATION_EPOCH_MISMATCH", direct_peer,
				"Mutation stream mismatch from $peer: got $source/$epoch, expected $expected_source/$expected_epoch",
				log_data_string("peer", direct_peer ? direct_peer->name : "none"), log_data_string("epoch", epoch),
				log_data_string("source", source_sid), log_data_string("expected_source", ctx->authority_sid),
				log_data_string("expected_epoch", ctx->authority_epoch));
		strlcpy(ctx->candidate_authority_sid, source_sid, sizeof(ctx->candidate_authority_sid));
		strlcpy(ctx->candidate_authority_epoch, epoch, sizeof(ctx->candidate_authority_epoch));
		ctx->candidate_authority_epoch_known = 1;
		udb_mutation_trigger_gap_recovery(direct_peer);
		return UDB_SEQ_STALE_EPOCH;
	}

	if (seq <= ctx->last_applied_seq)
	{
		udb_log(ULOG_DEBUG, "UDB_MUTATION_DUPLICATE", direct_peer,
				"Dropping duplicate or stale mutation seq $seq (last applied is $last)",
				log_data_integer("seq", (long)seq), log_data_integer("last", (long)ctx->last_applied_seq));
		return UDB_SEQ_DUPLICATE;
	}

	uint64_t expected_seq = ctx->last_applied_seq + 1;
	if (seq > expected_seq)
	{
		udb_log(
			ULOG_WARNING, "UDB_MUTATION_GAP_DETECTED", direct_peer,
			"Mutation sequence gap detected from $peer: got $seq, expected $expected_seq. Triggering reconciliation.",
			log_data_string("peer", direct_peer ? direct_peer->name : "none"), log_data_integer("seq", (long)seq),
			log_data_integer("expected_seq", (long)expected_seq));
		udb_mutation_trigger_gap_recovery(direct_peer);
		return UDB_SEQ_GAP;
	}

	return UDB_SEQ_APPLY;
}

static void udb_mutation_ins(UdbContext *ctx, Client *client, Client *direct_peer, const char *target,
							 const char *epoch, uint64_t seq, const char *path, const char *data, int is_for_me,
							 int is_broadcast)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	char letter = path && *path ? path[0] : '0';

	if (is_for_me)
	{
		if (!udb_is_propagator(ctx, direct_peer))
		{
			udb_protocol_mutation_error(client, "INS", UDB_ERR_FORBIDDEN, letter);
			return;
		}

		UdbSeqCheckResult seq_res = udb_mutation_check_sequence(ctx, client, direct_peer, epoch, seq);
		if (seq_res == UDB_SEQ_DUPLICATE || seq_res == UDB_SEQ_STALE_EPOCH)
			return;
		if (seq_res == UDB_SEQ_GAP)
			return;

		if (!block || !udb_record_fits_limits(path, data))
		{
			ctx->last_applied_seq = seq;
			udb_protocol_mutation_error(client, "INS", UDB_ERR_PARAMS, letter);
			return;
		}

		if (block->session || (block->syncing_from && block->syncing_from != direct_peer))
		{
			ctx->last_applied_seq = seq;
			udb_protocol_mutation_error(client, "INS", UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}

		UdbRecord *old_rec;
		UdbRecord *tree;
		UdbRecord *rec = NULL;
		UdbHashIndex hash_index;
		int unchanged;
		int nick_pass_added;
		int nick_pass_changed;

		if (!udb_record_validate(block, path + 3, data))
		{
			ctx->last_applied_seq = seq;
			udb_log(ULOG_WARNING, "UDB_INS_SCHEMA_REJECT", client,
					"Rejected S2S INS for invalid or unknown record: $path", log_data_string("path", path));
			udb_protocol_mutation_error(client, "INS", UDB_ERR_PARAMS, letter);
			return;
		}
		old_rec = udb_record_find_path(ctx, block, path + 3);
		unchanged = old_rec && udb_record_data_equals(old_rec, data);
		if (unchanged)
		{
			ctx->last_applied_seq = seq;
			if (!is_broadcast)
				return;
			udb_mutation_forward_ins(client, direct_peer, target, epoch, seq, path, data);
			return;
		}
		tree = udb_record_clone_tree(block->tree, old_rec, &rec);
		rec = udb_record_insert_path(tree, path + 3, data);
		nick_pass_added = rec && block->letter == UDB_BLOCK_NICKS && rec->parent && rec->parent != tree &&
						  !strcmp(rec->key, NKEY_PASS) && !old_rec;
		nick_pass_changed = old_rec && block->letter == UDB_BLOCK_NICKS && old_rec->parent &&
							old_rec->parent != block->tree && !strcmp(old_rec->key, NKEY_PASS);
		if (rec && block->letter == UDB_BLOCK_NICKS && rec->parent && rec->parent != tree)
		{
			if (!strcmp(rec->key, NKEY_FORBID))
				udb_n_profile_canonicalize_forbid(rec->parent);
			else if (udb_record_find(NULL, NKEY_FORBID, rec->parent))
				rec = NULL;
		}
		if (!rec || (block->letter == UDB_BLOCK_LINES && !udb_k_profile_valid(rec->parent)) ||
			(block->letter == UDB_BLOCK_NICKS && !udb_n_tree_profiles_valid(tree)))
		{
			const char *profile_kind = block->letter == UDB_BLOCK_NICKS ? "N" : "K";

			ctx->last_applied_seq = seq;
			udb_record_free_tree(tree);
			udb_log(ULOG_WARNING, "UDB_INS_PROFILE_REJECT", client,
					"Rejected INS that would create an invalid complete $profile_kind profile: $path",
					log_data_string("profile_kind", profile_kind), log_data_string("path", path));
			udb_protocol_mutation_error(client, "INS", UDB_ERR_PARAMS, letter);
			return;
		}
		if (!udb_hash_prepare_tree(tree, &hash_index))
		{
			ctx->last_applied_seq = seq;
			udb_record_free_tree(tree);
			udb_protocol_mutation_error(client, "INS", UDB_ERR_PARAMS, letter);
			return;
		}
		UdbSnapshotResult snap_res = udb_file_write_snapshot(block, tree, udb_record_count_tree(tree));
		if (snap_res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_hash_dispose_prepared(&hash_index);
			udb_record_free_tree(tree);
			udb_handle_persistence_failure(ctx, direct_peer, block, "INS snapshot", 0);
			udb_mutation_trigger_gap_recovery(direct_peer);
			udb_mutation_persist_error(client, "INS", letter);
			return;
		}
		/* Revoke the old value's effects only when the value actually changes:
		 * an identical INS must not churn channel modes nor revoke +q ranks. */
		if (!unchanged && block->letter == UDB_BLOCK_NICKS && rec && !strcmp(rec->key, NKEY_FORBID))
		{
			UdbRecord *old_profile = old_rec ? old_rec->parent : udb_record_find(ctx, rec->parent->key, block->tree);
			if (old_profile)
				udb_remove_special_record(ctx, block, old_profile);
		}
		else if (old_rec && !unchanged)
		{
			/* Replacing an existing credential must not revoke the holder's
			 * identity or effects: the active proof is checked live against the
			 * new policy and the following apply reconciles effects. */
			if (!nick_pass_changed && (block->letter != UDB_BLOCK_CHANNELS || old_rec->parent == block->tree ||
									   (strcmp(old_rec->key, CKEY_MODES) && strcmp(old_rec->key, CKEY_TOPIC) &&
										strcmp(old_rec->key, CKEY_OPTIONS) && strcmp(old_rec->key, CKEY_SUSPEND))))
				udb_remove_special_record(ctx, block, old_rec);
		}
		if (block->letter == UDB_BLOCK_LINES)
			udb_lines_expiry_pending_clear_record(old_rec ? old_rec : rec);
		udb_block_replace_tree(ctx, block, tree, udb_record_count_tree(tree), &hash_index);
		/* A newly added credential cannot inherit identity from an earlier
		 * passless profile, a matching nick, or externally residual account/+r.
		 * The following apply sees no identity marker and renames the holder.
		 * Changing an existing credential does not come through here: live
		 * policy checks preserve the holder's active identity. A passless
		 * profile never applied runtime effects, so it must not revoke
		 * externally supplied state either. */
		if (nick_pass_added)
		{
			Client *profile_client = find_user(rec->parent->key, NULL);

			if (profile_client && MyUser(profile_client))
			{
				udb_nick_identity_clear(profile_client);
				udb_nick_pending_auth_clear(profile_client);
			}
		}
		if (!unchanged)
		{
			udb_apply_special_record(ctx, block, rec, 1);
			if (block->letter == UDB_BLOCK_LINKS)
				udb_sync_snomask_filter();
		}
		if (snap_res == UDB_SNAPSHOT_COMMITTED_DURABILITY_UNCERTAIN)
		{
			udb_handle_persistence_failure(ctx, direct_peer, block, "INS snapshot", 1);
			udb_mutation_trigger_gap_recovery(direct_peer);
			udb_mutation_persist_error(client, "INS", letter);
			return;
		}
		ctx->last_applied_seq = seq;
		char logbuf[512];
		snprintf(logbuf, sizeof(logbuf), "Inserted record via S2S: %s -> %s (seq=%" PRIu64 ")", path,
				 udb_mutation_safe_value(path, data), seq);
		udb_log(ULOG_INFO, "UDB_INS_RECEIVED", client, "$msg", log_data_string("msg", logbuf));
		if (!is_broadcast)
			return;
	}
	else
	{
		if (!block || !udb_record_fits_limits(path, data))
		{
			udb_protocol_mutation_error(client, "INS", UDB_ERR_PARAMS, letter);
			return;
		}
	}
	udb_mutation_forward_ins(client, direct_peer, target, epoch, seq, path, data);
}

static int udb_mutation_delete_local(UdbContext *ctx, UdbBlock *block, UdbRecord *old_rec, Client *direct_peer,
									 const char *operation)
{
	UdbRecord *candidate_rec = NULL;
	UdbRecord *candidate_line = NULL;
	UdbRecord *candidate_nick_profile = NULL;
	UdbRecord *tree;
	UdbHashIndex hash_index;
	unsigned int record_count;
	UdbSnapshotResult snap_res;

	if (!ctx || !block || !old_rec)
		return 0;
	tree = udb_record_clone_tree(block->tree, old_rec, &candidate_rec);
	if (!tree || !candidate_rec)
	{
		udb_record_free_tree(tree);
		return 0;
	}
	if (block->letter == UDB_BLOCK_LINES && candidate_rec->parent && candidate_rec->parent->parent &&
		candidate_rec->parent->parent != tree)
		candidate_line = candidate_rec->parent;
	if (block->letter == UDB_BLOCK_NICKS && candidate_rec->parent && candidate_rec->parent != tree)
		candidate_nick_profile = candidate_rec->parent;
	if (candidate_nick_profile && !strcmp(old_rec->key, NKEY_SUSPEND))
		candidate_nick_profile->nick_unsuspend_transition = 1;
	udb_record_delete_tree(candidate_rec);
	record_count = udb_record_count_tree(tree);
	if (!udb_hash_prepare_tree(tree, &hash_index))
	{
		udb_record_free_tree(tree);
		return 0;
	}
	snap_res = udb_file_write_snapshot(block, tree, record_count);
	if (snap_res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
	{
		udb_hash_dispose_prepared(&hash_index);
		udb_record_free_tree(tree);
		udb_handle_persistence_failure(ctx, direct_peer, block, operation, 0);
		return 0;
	}
	/* This is the common DEL transaction: persisted candidate first, then old
	 * runtime effect removal, active-tree replacement, and candidate reapply. */
	udb_remove_special_record(ctx, block, old_rec);
	if (block->letter == UDB_BLOCK_LINES)
		udb_lines_expiry_pending_clear_record(old_rec);
	udb_block_replace_tree(ctx, block, tree, record_count, &hash_index);
	if (candidate_nick_profile)
		udb_apply_special_record(ctx, block, candidate_nick_profile, 1);
	if (candidate_line)
		udb_lines_apply_effect(ctx, block, candidate_line);
	if (block->letter == UDB_BLOCK_LINKS)
		udb_sync_snomask_filter();
	if (snap_res == UDB_SNAPSHOT_COMMITTED_DURABILITY_UNCERTAIN)
	{
		udb_handle_persistence_failure(ctx, direct_peer, block, operation, 1);
		return 2;
	}
	return 1;
}

static void udb_mutation_del(UdbContext *ctx, Client *client, Client *direct_peer, const char *target,
							 const char *epoch, uint64_t seq, const char *path, int is_for_me, int is_broadcast)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	char letter = path && *path ? path[0] : '0';

	if (is_for_me)
	{
		UdbRecord *old_rec;
		int result;
		if (!udb_is_propagator(ctx, direct_peer))
		{
			udb_protocol_mutation_error(client, "DEL", UDB_ERR_FORBIDDEN, letter);
			return;
		}

		UdbSeqCheckResult seq_res = udb_mutation_check_sequence(ctx, client, direct_peer, epoch, seq);
		if (seq_res == UDB_SEQ_DUPLICATE || seq_res == UDB_SEQ_STALE_EPOCH)
			return;
		if (seq_res == UDB_SEQ_GAP)
			return;

		if (!block || !udb_record_fits_limits(path, NULL))
		{
			ctx->last_applied_seq = seq;
			udb_protocol_mutation_error(client, "DEL", UDB_ERR_PARAMS, letter);
			return;
		}

		if (block->session || (block->syncing_from && block->syncing_from != direct_peer))
		{
			ctx->last_applied_seq = seq;
			udb_protocol_mutation_error(client, "DEL", UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}

		old_rec = udb_record_find_path(ctx, block, path + 3);
		if (!old_rec)
		{
			ctx->last_applied_seq = seq;
			if (!is_broadcast)
				return;
			udb_mutation_forward_del(client, direct_peer, target, epoch, seq, path);
			return;
		}
		result = udb_mutation_delete_local(ctx, block, old_rec, direct_peer, "DEL snapshot");
		if (result != 1)
		{
			udb_mutation_trigger_gap_recovery(direct_peer);
			udb_mutation_persist_error(client, "DEL", letter);
			return;
		}
		ctx->last_applied_seq = seq;
		{
			char logbuf[512];
			snprintf(logbuf, sizeof(logbuf), "Deleted record via S2S: %s (seq=%" PRIu64 ")", path, seq);
			udb_log(ULOG_INFO, "UDB_DEL_RECEIVED", client, "$msg", log_data_string("msg", logbuf));
		}
		if (!is_broadcast)
			return;
	}
	else
	{
		if (!block || !udb_record_fits_limits(path, NULL))
		{
			udb_protocol_mutation_error(client, "DEL", UDB_ERR_PARAMS, letter);
			return;
		}
	}
	udb_mutation_forward_del(client, direct_peer, target, epoch, seq, path);
}

static int udb_mutation_k_expiry_matches(UdbBlock *block, const char *path, time_t expected_expires, UdbRecord **record)
{
	UdbRecord *line;
	UdbRecord *expires;

	if (!block || block->letter != UDB_BLOCK_LINES || !path || strncmp(path, "K::", 3))
		return 0;
	line = udb_record_find_path(udb_ctx, block, path + 3);
	if (!line || !line->parent || line->parent->parent != block->tree)
		return 0;
	expires = udb_record_find(udb_ctx, KKEY_EXPIRES, line);
	if (!expires || expires->data_str || !expires->data_num || expires->data_num != (unsigned long)expected_expires ||
		expected_expires > TStime())
		return 0;
	if (record)
		*record = line;
	return 1;
}

/* Only an authority performs this operation.  It deliberately calls the same
 * transactional subtree DEL primitive used by the normal mutation path. */
static int udb_mutation_expire_local(UdbContext *ctx, const char *path, time_t expected_expires)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	UdbRecord *line;
	uint64_t high;

	if (!block || !udb_mutation_k_expiry_matches(block, path, expected_expires, &line))
		return 0;
	high = ctx->current_seq > ctx->last_applied_seq ? ctx->current_seq : ctx->last_applied_seq;
	if (high == UINT64_MAX)
		return 0;
	if (udb_mutation_delete_local(ctx, block, line, NULL, "EXP snapshot") != 1)
		return 0;
	uint64_t seq = high + 1;
	ctx->current_seq = seq;
	const char *epoch = udb_ocl_epoch_value();
	ctx->last_applied_seq = seq;
	udb_sendto_confirmed_servers(NULL, ":%s DB * DEL %s %" PRIu64 " %s", me.id, epoch, seq, path);
	return 1;
}

static void udb_mutation_exp(UdbContext *ctx, Client *client, Client *direct_peer, const char *target, const char *path,
							 time_t expected_expires, int is_for_me, int is_broadcast)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	UdbRecord *line;
	UdbPropagatorSelection selected;
	int has_selected;
	uint64_t high;

	(void)target;
	if (!block || !path || expected_expires <= 0 || is_broadcast || !is_for_me || client != direct_peer ||
		!udb_peer_authorizes_us(direct_peer))
	{
		udb_protocol_mutation_error(client, "EXP", UDB_ERR_FORBIDDEN, 'K');
		return;
	}
	if (block->session || block->syncing_from)
	{
		udb_protocol_mutation_error(client, "EXP", UDB_ERR_SYNC_ACTIVE, 'K');
		return;
	}
	if (!udb_mutation_k_expiry_matches(block, path, expected_expires, &line))
	{
		udb_log(ULOG_INFO, "UDB_K_EXP_STALE", client, "Ignored stale K expiry request for $path",
				log_data_string("path", path), NULL);
		return;
	}
	has_selected = udb_select_propagator(ctx, 1, &selected);
	if (udb_propagator_policy_present(ctx) && (!has_selected || !selected.is_local))
	{
		UdbLineExpiryPending *pending = udb_line_expiry_pending_add(line->parent->key[0], line->key, expected_expires);

		if (has_selected && selected.peer && pending && !pending->sent &&
			udb_send_db_to_one(selected.peer, ":%s DB %s EXP %s %lu", me.id, selected.peer->id, path,
							   (unsigned long)expected_expires))
			pending->sent = 1;
		return;
	}
	high = ctx->current_seq > ctx->last_applied_seq ? ctx->current_seq : ctx->last_applied_seq;
	if (high == UINT64_MAX)
	{
		udb_protocol_mutation_error(client, "EXP", UDB_ERR_FATAL, 'K');
		return;
	}
	if (udb_mutation_delete_local(ctx, block, line, direct_peer, "EXP snapshot") != 1)
	{
		udb_protocol_mutation_error(client, "EXP", UDB_ERR_FATAL, 'K');
		return;
	}
	uint64_t seq = high + 1;
	ctx->current_seq = seq;
	const char *epoch = udb_ocl_epoch_value();
	ctx->last_applied_seq = seq;
	udb_sendto_confirmed_servers(NULL, ":%s DB * DEL %s %" PRIu64 " %s", me.id, epoch, seq, path);
	udb_log(ULOG_INFO, "UDB_K_EXP_ACCEPTED", client, "Accepted K expiry request and removed $path",
			log_data_string("path", path), NULL);
}

static void udb_mutation_drp(UdbContext *ctx, Client *client, Client *direct_peer, const char *target,
							 const char *epoch, uint64_t seq, char letter, int is_for_me, int is_broadcast)
{
	if (is_for_me)
	{
		UdbBlock *block = udb_block_by_letter(ctx, letter);
		if (!block)
		{
			udb_protocol_mutation_error(client, "DRP", UDB_ERR_NO_BLOCK, letter);
			return;
		}
		if (block->session || (block->syncing_from && block->syncing_from != direct_peer))
		{
			udb_protocol_mutation_error(client, "DRP", UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (!udb_is_propagator(ctx, direct_peer))
		{
			udb_protocol_mutation_error(client, "DRP", UDB_ERR_FORBIDDEN, letter);
			return;
		}

		UdbSeqCheckResult seq_res = udb_mutation_check_sequence(ctx, client, direct_peer, epoch, seq);
		if (seq_res == UDB_SEQ_DUPLICATE || seq_res == UDB_SEQ_STALE_EPOCH)
			return;
		if (seq_res == UDB_SEQ_GAP)
			return;

		if (!block->tree->child)
		{
			ctx->last_applied_seq = seq;
			if (!is_broadcast)
				return;
			udb_sendto_confirmed_servers(direct_peer, ":%s DB %s DRP %s %" PRIu64 " %c", client->id, target, epoch, seq,
										 letter);
			return;
		}
		UdbRecord *tree = udb_record_clone_tree(block->tree, NULL, NULL);
		UdbHashIndex hash_index;
		while (tree->child)
			udb_record_delete_tree(tree->child);
		if (!udb_hash_prepare_tree(tree, &hash_index))
		{
			ctx->last_applied_seq = seq;
			udb_record_free_tree(tree);
			udb_protocol_mutation_error(client, "DRP", UDB_ERR_PARAMS, letter);
			return;
		}
		UdbSnapshotResult snap_res = udb_file_write_snapshot(block, tree, 0);
		if (snap_res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_hash_dispose_prepared(&hash_index);
			udb_record_free_tree(tree);
			udb_handle_persistence_failure(ctx, direct_peer, block, "DRP snapshot", 0);
			udb_mutation_trigger_gap_recovery(direct_peer);
			udb_mutation_persist_error(client, "DRP", letter);
			return;
		}
		/* Reset removes the old block's runtime effects only after persistence. */
		udb_block_reset(ctx, block);
		udb_block_replace_tree(ctx, block, tree, 0, &hash_index);
		if (snap_res == UDB_SNAPSHOT_COMMITTED_DURABILITY_UNCERTAIN)
		{
			udb_handle_persistence_failure(ctx, direct_peer, block, "DRP snapshot", 1);
			udb_mutation_trigger_gap_recovery(direct_peer);
			udb_mutation_persist_error(client, "DRP", letter);
			return;
		}
		ctx->last_applied_seq = seq;
		if (!is_broadcast)
			return;
	}
	udb_sendto_confirmed_servers(direct_peer, ":%s DB %s DRP %s %" PRIu64 " %c", client->id, target, epoch, seq,
								 letter);
}
