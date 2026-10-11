/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Staged Synchronization Sessions & Snapshot Transfers
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static unsigned long udb_sync_txid = 0;

typedef struct UdbHelloPeer UdbHelloPeer;

struct UdbHelloPeer
{
	Client *peer;
	time_t deadline;
	int state;
	int authorizes_us;
	int auth_received;
	int local_selection_sent;
	int reconciliation_offered;
	int ocl_capable;	 /* Peer advertised the mandatory OCL token. */
	int oclg_subscribed; /* Peer requested the OCLG consumer view. */
	int ocl_replayed;	 /* Inventories already replayed to this peer. */
	int oclg_replayed;	 /* OCLG snapshot already sent for this peer instance. */
	int remote_epoch_known;
	char remote_epoch[UDB_OCL_EPOCH_LEN + 1];
	UdbHelloPeer *next;
};

#define UDB_HEL_WAITING 1
#define UDB_HEL_CONFIRMED 2
#define UDB_HEL_UNSUPPORTED 3

static UdbHelloPeer *udb_hello_peers = NULL;
static UdbHelloPeer *udb_hello_peer(Client *server, int create);
static int udb_has_active_sessions(UdbContext *ctx);
static int udb_has_pending_requests(UdbContext *ctx);

static void udb_hello_cleanup_all(void)
{
	while (udb_hello_peers)
	{
		UdbHelloPeer *next = udb_hello_peers->next;
		safe_free(udb_hello_peers);
		udb_hello_peers = next;
	}
}

static void udb_maybe_start_reconciliation(Client *server, int force_offer)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);
	UdbPropagatorSelection selected;

	if (!peer || !udb_has_hello(server) || !IsServer(server) || !MyConnect(server))
		return;
	/* A peer authorizing us identifies this side as its upstream; it does not
	 * authorize a reverse pull.  Offer our committed snapshot only while
	 * READY, never while another authority/round is active, and never to our
	 * own selected upstream: that would reverse the authority direction and
	 * was the source of bilateral RES churn. */
	if (!peer->authorizes_us || !udb_ready)
		return;
	if (udb_select_propagator(udb_ctx, 1, &selected) && !selected.is_local && selected.peer == server)
		return;
	if (udb_reconcile.active || udb_has_active_sessions(udb_ctx) || udb_has_pending_requests(udb_ctx))
		return;
	if (force_offer || !peer->reconciliation_offered)
	{
		if (udb_sync_to_server(server))
			peer->reconciliation_offered = 1;
	}
}

static void udb_offer_reconciliation_to_downstreams(void)
{
	Client *server;

	if (!udb_ready)
		return;
	list_for_each_entry(server, &server_list, special_node)
	{
		if (IsServer(server) && MyConnect(server) && udb_has_hello(server) && udb_peer_authorizes_us(server))
			udb_maybe_start_reconciliation(server, 1);
	}
}

static UdbHelloPeer *udb_hello_peer(Client *server, int create)
{
	UdbHelloPeer *peer;

	for (peer = udb_hello_peers; peer; peer = peer->next)
		if (peer->peer == server)
			return peer;
	if (!create)
		return NULL;
	peer = safe_alloc(sizeof(*peer));
	peer->peer = server;
	peer->next = udb_hello_peers;
	udb_hello_peers = peer;
	return peer;
}

static int udb_has_hello(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	return server && IsServer(server) && MyConnect(server) && peer && peer->state == UDB_HEL_CONFIRMED;
}

static int udb_has_staged_sync(Client *server)
{
	return udb_has_hello(server);
}

static int udb_peer_authorizes_us(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	return udb_has_hello(server) && peer->authorizes_us;
}

static int udb_hello_epoch_valid(const char *epoch)
{
	int i;

	if (!epoch || strlen(epoch) != UDB_OCL_EPOCH_LEN)
		return 0;
	for (i = 0; i < UDB_OCL_EPOCH_LEN; i++)
	{
		char c = epoch[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return 0;
	}
	return 1;
}

static int udb_hello_epoch_matches(Client *server, const char *epoch)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	return peer && peer->remote_epoch_known && epoch && !strcmp(peer->remote_epoch, epoch);
}

static int udb_hello_parse_propagator(const char *propagator, int *authorizes_us)
{
	if (!propagator || !*propagator || !authorizes_us)
		return 0;
	if (!strcmp(propagator, "?"))
		*authorizes_us = 1;
	else if (!strcmp(propagator, "-"))
		*authorizes_us = 0;
	else if (udb_server_name_valid(propagator))
		*authorizes_us = !strcasecmp(propagator, me.name);
	else
		return 0;
	return 1;
}

static const char *udb_sync_hello_propagator(UdbPropagatorSelection *selected)
{
	if (selected && udb_select_propagator(udb_ctx, 1, selected))
		return selected->name;
	if (!udb_propagator_policy_present(udb_ctx))
		return udb_ready ? me.name : "?";
	return "-";
}

static int udb_hello_peer_advertisement(Client *server, const char *propagator, const char *epoch, int oclg_subscribed)
{
	UdbHelloPeer *peer;
	int authorizes_us;
	int epoch_changed;
	int subscription_changed;

	if (!server || !udb_hello_epoch_valid(epoch) || !udb_hello_parse_propagator(propagator, &authorizes_us))
		return 0;
	peer = udb_hello_peer(server, 1);
	epoch_changed = peer->remote_epoch_known && strcmp(peer->remote_epoch, epoch);
	subscription_changed = peer->oclg_subscribed != oclg_subscribed;
	if (epoch_changed)
	{
		/* The SERVER connection survives a module reload, but all data and
		 * replay latches belonging to the old remote instance do not. */
		peer->reconciliation_offered = 0;
		peer->ocl_replayed = 0;
		peer->oclg_replayed = 0;
		/* Recompute for every other subscriber now, then replay to this peer
		 * after its HEL exchange has been acknowledged. */
		peer->oclg_subscribed = 0;
		udb_ocl_peer_instance_changed(server);
		if (udb_ctx && udb_is_propagator(udb_ctx, server))
		{
			/* A neighbor instance change invalidates in-flight ownership, not the
			 * persisted root SID/epoch or its durable sequence. */
			if (udb_ctx->storage)
			{
				uint64_t revision;
				udb_storage_engine_authority(udb_ctx->storage, &revision);
			}
			if (udb_reconcile.active && udb_reconcile.authority_peer == server)
				udb_reconcile_abort(udb_ctx, "authority peer instance changed", 1);
			udb_sync_status = UDB_SYNC_DEGRADED;
			if (!udb_degraded_since)
				udb_degraded_since = time(NULL);
		}
		udb_log(ULOG_INFO, "UDB_HEL_INSTANCE_CHANGED", server, "Direct peer announced a new UDB instance epoch $epoch",
				log_data_string("epoch", epoch));
	}
	else if (subscription_changed)
		peer->oclg_replayed = 0;
	strlcpy(peer->remote_epoch, epoch, sizeof(peer->remote_epoch));
	peer->remote_epoch_known = 1;
	peer->authorizes_us = authorizes_us;
	peer->auth_received = 1;
	peer->ocl_capable = 1;
	peer->oclg_subscribed = oclg_subscribed;
	return 1;
}

static int udb_sync_hello_start(Client *server)
{
	UdbHelloPeer *peer;
	UdbPropagatorSelection selected;
	const char *propagator;
	int ok;

	if (!server || !IsServer(server) || !MyConnect(server))
		return 0;
	peer = udb_hello_peer(server, 1);
	if (peer->state == UDB_HEL_WAITING || peer->state == UDB_HEL_CONFIRMED)
		return 0;
	peer->state = UDB_HEL_WAITING;
	peer->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	/* No policy: a fresh node is seeking a bootstrap source, while a READY
	 * node is its own standalone authority. */
	propagator = udb_sync_hello_propagator(&selected);
	ok = udb_send_db_to_one(server, ":%s DB %s HEL " UDB_PROTOCOL_HEL " %s %s OCL", me.id, server->id, propagator,
							udb_ocl_epoch_value());
	if (!ok)
	{
		peer->state = 0;
		peer->deadline = 0;
		return 0;
	}
	peer->local_selection_sent = 1;
	return 1;
}

static void udb_sync_hello_ack(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	if (!peer)
		return;

	if (peer->state == UDB_HEL_CONFIRMED)
	{
		udb_maybe_start_reconciliation(server, 0);
		udb_ocl_maybe_replay_to_peer(server);
		return;
	}

	if (peer->state == UDB_HEL_WAITING)
	{
		peer->state = UDB_HEL_CONFIRMED;
		peer->deadline = 0;
		udb_log(ULOG_INFO, "UDB_HEL_CONFIRMED", server,
				"UDB HEL " UDB_PROTOCOL_HEL " capability confirmed for directly linked server");
		udb_propagator_policy_changed(udb_ctx);
		udb_maybe_start_reconciliation(server, 0);
		udb_ocl_maybe_replay_to_peer(server);
	}
}

static int udb_has_active_sessions(UdbContext *ctx)
{
	UdbBlock *block;
	if (!ctx)
		return 0;
	for (block = ctx->block_list; block; block = block->next)
	{
		if (block->session)
			return 1;
	}
	return 0;
}

static int udb_has_pending_requests(UdbContext *ctx)
{
	UdbBlock *block;
	if (!ctx)
		return 0;
	for (block = ctx->block_list; block; block = block->next)
	{
		if (block->pending_from)
			return 1;
	}
	return 0;
}

static void udb_reconcile_reset(void)
{
	udb_reconcile.authority_peer = NULL;
	udb_reconcile.active = 0;
	udb_reconcile.compared_blocks = 0;
	udb_reconcile.divergent_blocks = 0;
	udb_reconcile.completed_blocks = 0;
	udb_reconcile.generation = 0;
	udb_reconcile.started_at = 0;
	udb_reconcile.last_activity = 0;
	udb_reconcile.deadline = 0;
	udb_reconcile.absolute_deadline = 0;
	udb_reconcile.watermark_seq = 0;
	udb_reconcile.watermark_known = 0;
	udb_reconcile.source_known = 0;
	udb_reconcile.source_sid[0] = 0;
	udb_reconcile.source_epoch[0] = 0;
}

static void udb_reconcile_abort(UdbContext *ctx, const char *reason, int schedule_retry)
{
	UdbBlock *block;
	udb_native_receive_abort(udb_reconcile.round_id);
	udb_log(ULOG_WARNING, "UDB_RECONCILE_ABORT", udb_reconcile.authority_peer,
			"Reconciliation round $round aborted: $reason", log_data_integer("round", (int)udb_reconcile.round_id),
			log_data_string("reason", reason ? reason : "unspecified"));

	if (ctx)
	{
		for (block = ctx->block_list; block; block = block->next)
		{
			udb_block_clear_pending(block);
			if (block->session)
				udb_sync_abort(block, reason ? reason : "reconciliation aborted");
		}
	}
	if (schedule_retry && udb_reconcile.retry_count < UDB_RECONCILE_RETRY_MAX)
	{
		udb_reconcile.retry_count++;
		unsigned int shift = udb_reconcile.retry_count > 4 ? 4 : udb_reconcile.retry_count;
		udb_reconcile.next_retry_at = time(NULL) + (UDB_RECONCILE_RETRY_BASE << shift);
	}
	else
	{
		udb_reconcile.retry_count = 0;
		udb_reconcile.next_retry_at = 0;
	}
	udb_reconcile_reset();
}

/* INF, not the direct peer HEL, binds the root mutation lineage. Every block
 * in a round must agree before it can enter a private durable stage. */
static int udb_reconcile_accept_source(Client *peer, unsigned long round, const char *sid, const char *epoch,
									   uint64_t watermark)
{
	if (!udb_reconcile.active || udb_reconcile.authority_peer != peer || udb_reconcile.round_id != round ||
		!udb_storage_sid_valid(sid) || !udb_hello_epoch_valid(epoch))
		return 0;
	if (udb_reconcile.source_known &&
		(strcmp(udb_reconcile.source_sid, sid) || strcmp(udb_reconcile.source_epoch, epoch)))
	{
		udb_reconcile_abort(udb_ctx, "inconsistent reconciliation source lineage", 1);
		return 0;
	}
	if (!udb_reconcile_accept_watermark(peer, round, watermark))
		return 0;
	memcpy(udb_reconcile.source_sid, sid, 4);
	memcpy(udb_reconcile.source_epoch, epoch, UDB_OCL_EPOCH_LEN + 1);
	udb_reconcile.source_known = 1;
	return 1;
}

static int udb_reconcile_accept_watermark(Client *peer, unsigned long round_id, uint64_t watermark_seq)
{
	if (!udb_reconcile.active || udb_reconcile.authority_peer != peer || udb_reconcile.round_id != round_id)
		return 0;
	if (!udb_reconcile.watermark_known)
	{
		udb_reconcile.watermark_seq = watermark_seq;
		udb_reconcile.watermark_known = 1;
		return 1;
	}
	if (udb_reconcile.watermark_seq == watermark_seq)
		return 1;
	udb_reconcile_abort(udb_ctx, "inconsistent reconciliation watermark", 1);
	return 0;
}

/* One recovery boundary for every operation whose persistence outcome is not
 * trustworthy.  It intentionally reuses authority selection and retry bounds;
 * it does not alter HEL negotiation or hop-by-hop routing. */
static void udb_sync_round_failure(UdbBlock *block, Client *peer, unsigned long round_id, const char *reason)
{
	if (block && peer && udb_reconcile.active && udb_reconcile.authority_peer == peer &&
		udb_reconcile.round_id == round_id)
		udb_reconcile_abort(udb_ctx, reason, 1);
}

static void udb_reconcile_begin(Client *authority, unsigned long round_id)
{
	unsigned int retry_count = udb_reconcile.retry_count;
	udb_native_mutation_round_begin(authority);
	udb_ready = 0;
	udb_reconcile.authority_peer = authority;
	udb_reconcile.round_id = round_id;
	udb_reconcile.generation = round_id ? round_id : 1;
	udb_reconcile.active = 1;
	udb_reconcile.compared_blocks = 0;
	udb_reconcile.divergent_blocks = 0;
	udb_reconcile.completed_blocks = 0;
	udb_reconcile.retry_count = retry_count;
	udb_reconcile.next_retry_at = 0;
	udb_reconcile.started_at = time(NULL);
	udb_reconcile.last_activity = udb_reconcile.started_at;
	udb_reconcile.deadline =
		udb_reconcile.started_at +
		((udb_cfg && udb_cfg->sync_inactivity_timeout > 0) ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_TIMEOUT);
	/* The absolute deadline is never refreshed by traffic: a broken authority
	 * drip-feeding valid frames cannot keep a round alive indefinitely. */
	udb_reconcile.absolute_deadline =
		udb_reconcile.started_at +
		((udb_cfg && udb_cfg->sync_absolute_timeout > 0) ? udb_cfg->sync_absolute_timeout : UDB_SYNC_ABSOLUTE_TIMEOUT);
	udb_reconcile.watermark_seq = 0;
	udb_reconcile.watermark_known = 0;
	udb_reconcile.source_known = 0;
	udb_reconcile.source_sid[0] = 0;
	udb_reconcile.source_epoch[0] = 0;
	udb_log(ULOG_INFO, "UDB_RECONCILE_ROUND_START", authority,
			"Started reconciliation round $round with authority peer $peer",
			log_data_integer("round", (int)udb_reconcile.round_id), log_data_client("peer", authority));
}

static void udb_reconcile_start(Client *authority, unsigned long round_id)
{
	if (udb_reconcile.authority_peer != authority || !udb_reconcile.active || udb_reconcile.round_id != round_id)
	{
		if (udb_reconcile.active)
			udb_reconcile_abort(udb_ctx, "new reconciliation round", 0);
		udb_reconcile_begin(authority, round_id);
	}
}

/* Latch READY health to DEGRADED the moment divergence with the authority is
 * confirmed. Idempotent: only the OK -> DEGRADED transition is acted upon, and
 * only udb_reconcile_check() may ever perform the reverse transition. */
static void udb_sync_mark_degraded(Client *peer, const char *reason)
{
	if (!udb_ready || udb_sync_status != UDB_SYNC_OK)
		return;
	udb_sync_status = UDB_SYNC_DEGRADED;
	udb_log(ULOG_WARNING, "UDB_SYNC_DIVERGED", peer,
			"Database divergence confirmed ($reason); recovery required before health returns to OK",
			log_data_string("reason", reason ? reason : "unknown"));
}

static void udb_reconcile_record_res(Client *peer, unsigned long round_id, char letter)
{
	unsigned int mask = udb_block_letter_to_mask(letter);
	if (!mask)
		return;

	if (!udb_reconcile.active || udb_reconcile.authority_peer != peer || udb_reconcile.round_id != round_id)
		return;
	udb_reconcile.divergent_blocks |= mask;
	udb_reconcile.completed_blocks &= ~mask;
	udb_sync_mark_degraded(peer, "block recovery required");
	udb_reconcile.last_activity = time(NULL);
	udb_reconcile.deadline =
		udb_reconcile.last_activity +
		((udb_cfg && udb_cfg->sync_inactivity_timeout > 0) ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_TIMEOUT);
}

static void udb_reconcile_record_end(Client *peer, char letter, unsigned long round_id)
{
	unsigned int mask = udb_block_letter_to_mask(letter);
	if (!mask)
		return;

	if (!udb_reconcile.active || udb_reconcile.authority_peer != peer || udb_reconcile.round_id != round_id)
	{
		udb_log(ULOG_WARNING, "UDB_RECONCILE_STALE_END", peer,
				"Ignored staged END for block $block with round $session_round (current round is $current_round)",
				log_data_string("block", (char[]){letter, '\0'}), log_data_integer("session_round", (int)round_id),
				log_data_integer("current_round", (int)udb_reconcile.round_id));
		return;
	}

	udb_reconcile.compared_blocks |= mask;
	udb_reconcile.completed_blocks |= mask;
	udb_reconcile.divergent_blocks &= ~mask;
	udb_reconcile.last_activity = time(NULL);
	udb_reconcile.deadline =
		udb_reconcile.last_activity +
		((udb_cfg && udb_cfg->sync_inactivity_timeout > 0) ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_TIMEOUT);
}

static int udb_reconcile_check(UdbContext *ctx)
{
	return udb_native_receive_promote(ctx);
}

static int udb_is_authorized_sync_source(UdbContext *ctx, Client *direct_peer)
{
	if (!direct_peer || !udb_has_hello(direct_peer))
		return 0;

	if (udb_propagator_policy_present(ctx))
	{
		return udb_is_propagator(ctx, direct_peer);
	}
	else
	{
		/* Without a policy there is no remote authority once READY: this node
		 * is a standalone authority and accepts no imports.  Before READY only
		 * the exclusive bootstrap owner may feed us. */
		if (!udb_ready)
		{
			if (!udb_bootstrap_peer)
				udb_bootstrap_peer = direct_peer;

			return (udb_bootstrap_peer == direct_peer);
		}
		return 0;
	}
}

/* Observability-only tracker: how long no eligible propagator has been
 * usable. It never touches udb_ready, udb_sync_status or client admission;
 * the bootstrap ladder uses udb_degraded_since instead. */
static void udb_propagator_availability_refresh(void)
{
	UdbContext *ctx = udb_ctx;
	UdbPropagatorSelection selected;

	if (!udb_propagator_policy_present(ctx) || udb_select_propagator(ctx, 1, &selected))
	{
		udb_propagator_unavailable_since = 0;
		return;
	}
	if (udb_propagator_unavailable_since == 0)
		udb_propagator_unavailable_since = time(NULL);
}

static void udb_sync_status_refresh(void)
{
	UdbContext *ctx = udb_ctx;
	time_t now = time(NULL);
	int timeout = (udb_cfg && udb_cfg->stale_timeout > 0) ? udb_cfg->stale_timeout : 300;

	if (udb_ready)
	{
		udb_degraded_since = 0;
		/* READY health is latched by reconciliation (only udb_reconcile_check()
		 * may return DEGRADED to OK); STALE is invalid while READY. */
		if (udb_sync_status == UDB_SYNC_STALE)
			udb_sync_status = UDB_SYNC_OK;
		return;
	}

	/* Bootstrap-pending nodes age toward STALE regardless of propagator
	 * availability: the clock measures time without a READY database. */
	if (udb_degraded_since == 0)
		udb_degraded_since = now;

	time_t elapsed = (now >= udb_degraded_since) ? (now - udb_degraded_since) : 0;

	if (elapsed >= timeout)
	{
		if (udb_sync_status != UDB_SYNC_STALE)
		{
			udb_log(ULOG_ERROR, "UDB_SYNC_STALE", NULL,
					"UDB bootstrap is stale ($seconds seconds without a READY database); new local clients remain "
					"denied until bootstrap completes",
					log_data_integer("seconds", (int)elapsed));
			udb_sync_status = UDB_SYNC_STALE;
		}
	}
	else if (udb_sync_status != UDB_SYNC_DEGRADED && udb_sync_status != UDB_SYNC_STALE)
	{
		const char *policy = udb_propagator_policy(ctx);
		udb_log(ULOG_WARNING, "UDB_SYNC_DEGRADED", NULL,
				"UDB bootstrap pending; no READY database yet. Policy: $policy",
				log_data_string("policy", policy ? policy : "none"));
		udb_sync_status = UDB_SYNC_DEGRADED;
	}
}

static int udb_hook_readiness_pre_connect(Client *client)
{
	if (!client || !MyConnect(client) || IsServer(client))
		return HOOK_CONTINUE;

	if (!udb_ready)
	{
		udb_log(ULOG_WARNING, "UDB_NOT_READY_DENY_CLIENT", client,
				"Rejecting new local client connection: UDB initialization pending");
		exit_client(client, NULL,
					"UDB synchronization unavailable; this server is temporarily not accepting new connections");
		return HOOK_DENY;
	}
	return HOOK_CONTINUE;
}

static void udb_sync_hello_refresh_all(void)
{
	Client *server;
	UdbPropagatorSelection selected;
	const char *propagator;

	propagator = udb_sync_hello_propagator(&selected);
	list_for_each_entry(server, &server_list, special_node)
	{
		if (IsServer(server) && MyConnect(server) && udb_has_hello(server))
		{
			UdbHelloPeer *hello = udb_hello_peer(server, 0);
			if (udb_send_db_to_one(server, ":%s DB %s HEL " UDB_PROTOCOL_HEL " %s %s OCL", me.id, server->id,
								   propagator, udb_ocl_epoch_value()) &&
				hello)
				hello->local_selection_sent = 1;
		}
	}
}

static void udb_sync_abort(UdbBlock *block, const char *reason)
{
	if (!block)
		return;
	if (block->session)
	{
		udb_log(ULOG_WARNING, "UDB_SYNC_ABORT", udb_ocl_find_server(block->session->peer_sid),
				"Aborted staged sync of block $block: $reason", log_data_string("block", (char[]){block->letter, '\0'}),
				log_data_string("reason", reason));
		udb_sync_session_free(block);
	}
	block->syncing_from = NULL;
	if (block->pending_from)
	{
		block->pending_from = NULL;
		block->pending_deadline = 0;
		block->pending_round_id = 0;
	}
}

static void udb_propagator_policy_changed(UdbContext *ctx)
{
	UdbBlock *block;
	UdbPropagatorSelection selected;
	Client *peer = NULL;

	if (!ctx)
		return;
	if (udb_policy_notify_deferred)
	{
		/* A block commit is mid-flight; replay once the new tree is installed. */
		udb_policy_notify_pending = 1;
		return;
	}
	int policy = udb_propagator_policy_present(ctx);
	int available = policy ? udb_select_propagator(ctx, 1, &selected) : 0;
	if (policy && available && !selected.is_local)
		peer = selected.peer;
	int kind = !policy || (available && selected.is_local) ? 1 : peer ? 2 : 3;
	char sid[4] = {0}, epoch[17] = {0};
	if (kind == 1)
		memcpy(sid, me.id, 4);
	else if (peer)
	{
		UdbHelloPeer *hello = udb_hello_peer(peer, 0);
		memcpy(sid, peer->id, 4);
		if (hello && hello->remote_epoch_known)
			memcpy(epoch, hello->remote_epoch, 17);
	}
	int selection_changed =
		ctx->selection_kind != kind || memcmp(ctx->selected_sid, sid, 4) || memcmp(ctx->selected_epoch, epoch, 17);
	if (ctx->storage && selection_changed)
	{
		uint64_t revision;
		udb_storage_engine_authority(ctx->storage, &revision);
		udb_native_mutation_abort(0);
		ctx->selection_kind = kind;
		memcpy(ctx->selected_sid, sid, 4);
		memcpy(ctx->selected_epoch, epoch, 17);
		if (kind != 1)
		{
			udb_ready = 0;
			udb_sync_status = UDB_SYNC_DEGRADED;
		}
		if (udb_reconcile.active)
			udb_reconcile_abort(ctx, "selected authority or instance changed", 1);
	}

	/* Without a policy the exclusive bootstrap owner stays valid until it
	 * disconnects or READY completes; a later HEL from another peer must not
	 * steal the ownership. */
	if (udb_propagator_policy_present(ctx) && udb_bootstrap_peer && udb_bootstrap_peer != peer)
		udb_bootstrap_peer = NULL;
	if (udb_reconcile.authority_peer && policy && udb_reconcile.authority_peer != peer)
	{
		if (ctx->storage)
		{
			uint64_t revision;
			udb_storage_engine_authority(ctx->storage, &revision);
		}
		udb_reconcile_abort(ctx, "propagator policy changed", 1);
	}

	for (block = ctx->block_list; block; block = block->next)
	{
		/* Replacing block S removes the old tree's effects while the S session
		 * still owns its replacement tree. Do not abort that session from its
		 * own policy callback; the new policy is applied after commit. */
		if (block->session && block->letter != UDB_BLOCK_SETTINGS &&
			(!peer || strcmp(block->session->peer_sid, peer->id)))
			udb_sync_abort(block, "propagator policy changed");
		else if (!block->session && block->syncing_from && block->syncing_from != peer)
			block->syncing_from = NULL;

		if (block->pending_from && block->pending_from != peer)
		{
			block->pending_from = NULL;
			block->pending_deadline = 0;
			block->pending_round_id = 0;
		}
	}
	if (kind == 1 && ctx->storage && !udb_reconcile.active &&
		(!ctx->authority_epoch_known || strcmp(ctx->authority_sid, me.id)))
	{
		udb_ready = 0;
		udb_native_local_bind(ctx);
	}
	/* Confirming an unrelated downstream is not an upstream retry. Refreshing
	 * unchanged selections provokes fresh inventories and withdraws READY for
	 * no authority change; explicit retry paths still refresh HEL themselves.
	 * Without a policy the advertised bootstrap role also depends on READY. */
	if (selection_changed || !policy)
		udb_sync_hello_refresh_all();
	udb_propagator_availability_refresh();
	udb_sync_status_refresh();

	if (peer && udb_has_hello(peer) && (!udb_ready || udb_sync_status != UDB_SYNC_OK))
		udb_maybe_start_reconciliation(peer, 0);
}

static void udb_propagator_policy_flush(UdbContext *ctx)
{
	if (!udb_policy_notify_pending || udb_policy_notify_deferred)
		return;
	udb_policy_notify_pending = 0;
	udb_propagator_policy_changed(ctx);
}

static void udb_sync_ack(Client *peer, const char *block)
{
	udb_log(ULOG_INFO, "UDB_SYNC_ACK", peer, "Staged sync acknowledged for block $block",
			log_data_string("block", block));
}

static int udb_sync_send_stage(Client *server, UdbBlock *block, unsigned long round_id)
{
	return block && udb_native_sync_send(server, block->letter, round_id);
}

static time_t udb_anti_entropy_calc_interval(void)
{
	int base = (udb_cfg && udb_cfg->anti_entropy_interval > 0) ? udb_cfg->anti_entropy_interval
															   : UDB_DEFAULT_ANTI_ENTROPY_INTERVAL;
	int jitter_range = base / 10;
	if (jitter_range > 0)
	{
		int delta = (rand() % (2 * jitter_range + 1)) - jitter_range;
		int res = base + delta;
		return res > 0 ? (time_t)res : 1;
	}
	return base > 0 ? (time_t)base : 1;
}

static void udb_anti_entropy_schedule_next(time_t now)
{
	udb_anti_entropy.next_check_at = now + udb_anti_entropy_calc_interval();
}

static void udb_anti_entropy_trigger(time_t now, Client *authority_peer)
{
	static unsigned long ae_round_sequence = 0;
	unsigned long round_id = (unsigned long)now + ++ae_round_sequence;
	if (!round_id)
		round_id = ++ae_round_sequence;

	udb_anti_entropy.round_id = round_id;
	udb_anti_entropy.pending = 1;
	udb_anti_entropy.acked_blocks = 0;
	udb_anti_entropy.divergent_blocks = 0;
	udb_anti_entropy.watermark_seq = 0;
	udb_anti_entropy.watermark_known = 0;
	udb_anti_entropy.last_check = now;
	udb_anti_entropy.deadline = now + UDB_ANTI_ENTROPY_TIMEOUT;

	udb_send_db_to_one(authority_peer, ":%s DB %s MANIFEST REQ %lu", me.id, authority_peer->id, round_id);
}

static void udb_anti_entropy_timer_check(time_t now)
{
	if (!udb_ready || udb_sync_status != UDB_SYNC_OK)
		return;

	UdbPropagatorSelection selected;
	if (!udb_select_propagator(udb_ctx, 1, &selected) || selected.is_local || !selected.peer ||
		!udb_has_hello(selected.peer))
		return;

	if (udb_anti_entropy.pending)
	{
		if (now >= udb_anti_entropy.deadline)
		{
			udb_anti_entropy.pending = 0;
			udb_anti_entropy.fail_count++;
			unsigned int shift = udb_anti_entropy.fail_count > 4 ? 4 : udb_anti_entropy.fail_count;
			time_t delay = (time_t)(UDB_ANTI_ENTROPY_MIN_GAP << shift);
			udb_anti_entropy.next_check_at = now + delay;
		}
		return;
	}

	if (udb_reconcile.active || udb_has_active_sessions(udb_ctx) || udb_has_pending_requests(udb_ctx))
		return;

	if (udb_anti_entropy.next_check_at == 0)
	{
		udb_anti_entropy_schedule_next(now);
		return;
	}

	int base_interval = (udb_cfg && udb_cfg->anti_entropy_interval > 0) ? udb_cfg->anti_entropy_interval
																		: UDB_DEFAULT_ANTI_ENTROPY_INTERVAL;
	time_t min_gap = (base_interval < UDB_ANTI_ENTROPY_MIN_GAP) ? (time_t)base_interval : UDB_ANTI_ENTROPY_MIN_GAP;
	if (udb_anti_entropy.last_check && (now - udb_anti_entropy.last_check < min_gap))
		return;

	if (now >= udb_anti_entropy.next_check_at)
	{
		udb_anti_entropy_trigger(now, selected.peer);
	}
}

/* MANIFEST ACK remains request correlation/diagnostics only. Extended INF
 * from the same pinned generation owns lineage and native stage admission. */
static void udb_anti_entropy_record_ack(Client *peer, unsigned long id, char block, uint64_t count, const char *digest,
										uint64_t watermark)
{
	unsigned int mask = udb_block_letter_to_mask(block);
	if (!udb_anti_entropy.pending || udb_anti_entropy.round_id != id || !mask ||
		!udb_is_authorized_sync_source(udb_ctx, peer) || (udb_anti_entropy.acked_blocks & mask))
		return;
	if (udb_anti_entropy.watermark_known && udb_anti_entropy.watermark_seq != watermark)
	{
		udb_anti_entropy_handle_err(peer, id, UDB_ERR_PARAMS);
		udb_reconcile_abort(udb_ctx, "inconsistent anti-entropy watermark", 1);
		return;
	}
	udb_anti_entropy.watermark_seq = watermark;
	udb_anti_entropy.watermark_known = 1;
	udb_anti_entropy.acked_blocks |= mask;
	(void)count;
	(void)digest;
	if (udb_anti_entropy.acked_blocks == UDB_ALL_BLOCKS_MASK)
	{
		udb_anti_entropy.pending = 0;
		udb_anti_entropy_schedule_next(time(NULL));
	}
}

static void udb_anti_entropy_handle_err(Client *peer, unsigned long round_id, int errcode)
{
	if (udb_anti_entropy.pending && udb_anti_entropy.round_id == round_id)
	{
		time_t now = time(NULL);
		udb_anti_entropy.pending = 0;
		udb_anti_entropy.fail_count++;
		unsigned int shift = udb_anti_entropy.fail_count > 4 ? 4 : udb_anti_entropy.fail_count;
		time_t delay = (time_t)(UDB_ANTI_ENTROPY_MIN_GAP << shift);
		udb_anti_entropy.next_check_at = now + delay;
		(void)peer;
		(void)errcode;
	}
}

static void udb_anti_entropy_peer_quit(Client *client)
{
	(void)client;
	if (udb_anti_entropy.pending)
	{
		udb_anti_entropy.pending = 0;
		udb_anti_entropy.next_check_at = time(NULL) + UDB_ANTI_ENTROPY_MIN_GAP;
	}
}

EVENT(udb_sync_timeout_event)
{
	UdbBlock *block;
	UdbHelloPeer *peer, *next;
	time_t now = time(NULL);
	const char *abort_reason = NULL;

	for (block = udb_ctx ? udb_ctx->block_list : NULL; block; block = block->next)
	{
		if (block->session)
		{
			if (block->session->deadline <= now)
				abort_reason = "snapshot inactivity timeout";
			else if (block->session->absolute_deadline <= now)
				abort_reason = "snapshot absolute timeout";
		}
		if (block->pending_from && block->pending_deadline <= now)
		{
			udb_log(ULOG_WARNING, "UDB_PENDING_RES_TIMEOUT", block->pending_from,
					"Pending RES request for block $block timed out waiting for BEGIN",
					log_data_string("block", (char[]){block->letter, '\0'}));
			abort_reason = "RES timeout";
		}
	}
	if (udb_reconcile.active)
	{
		if (udb_reconcile.deadline <= now)
			abort_reason = "reconciliation inventory timeout";
		else if (udb_reconcile.absolute_deadline <= now)
			abort_reason = "reconciliation absolute timeout";
	}
	if (abort_reason)
		udb_reconcile_abort(udb_ctx, abort_reason, 1);
	udb_ocl_stage_timeout_check(now);
	udb_lines_expiry_sweep(now);
	for (peer = udb_hello_peers; peer; peer = next)
	{
		next = peer->next;
		if (peer->state == UDB_HEL_WAITING && peer->deadline <= now)
		{
			Client *c = peer->peer;
			peer->state = UDB_HEL_UNSUPPORTED;
			udb_log(ULOG_ERROR, "UDB_HEL_TIMEOUT", c,
					"No UDB HEL " UDB_PROTOCOL_HEL " acknowledgement from directly linked server; link aborted");
			if (c)
			{
				exit_client_fmt(c, NULL, "Link aborted: server does not support UDB protocol (HEL timeout)");
			}
		}
	}
	if (!udb_reconcile.active && udb_reconcile.next_retry_at && udb_reconcile.next_retry_at <= now)
	{
		UdbPropagatorSelection selected;
		udb_reconcile.next_retry_at = 0;
		if (udb_select_propagator(udb_ctx, 1, &selected) && !selected.is_local && selected.peer)
		{
			udb_sync_hello_refresh_all();
			udb_maybe_start_reconciliation(selected.peer, 1);
			/* Keep retrying if the HEL refresh or resulting inventory is lost. Any
			 * accepted INF starts the round and clears this deadline. */
			if (!udb_reconcile.active && udb_reconcile.retry_count < UDB_RECONCILE_RETRY_MAX)
			{
				udb_reconcile.retry_count++;
				unsigned int shift = udb_reconcile.retry_count > 4 ? 4 : udb_reconcile.retry_count;
				udb_reconcile.next_retry_at = now + (UDB_RECONCILE_RETRY_BASE << shift);
			}
		}
	}
	udb_propagator_availability_refresh();
	udb_sync_status_refresh();
	udb_anti_entropy_timer_check(now);
}

static void udb_sync_server_quit(Client *client)
{
	if (udb_ctx && udb_ctx->storage && client &&
		(udb_is_propagator(udb_ctx, client) || udb_reconcile.authority_peer == client))
	{
		uint64_t revision;
		udb_storage_engine_authority(udb_ctx->storage, &revision);
	}
	udb_lines_expiry_peer_disconnected(client);
	udb_anti_entropy_peer_quit(client);
	UdbBlock *block;
	UdbHelloPeer **peer;

	if (!udb_ctx || !client)
		return;
	for (peer = &udb_hello_peers; *peer; peer = &(*peer)->next)
		if ((*peer)->peer == client)
		{
			UdbHelloPeer *old = *peer;
			*peer = old->next;
			safe_free(old);
			break;
		}
	for (block = udb_ctx->block_list; block; block = block->next)
	{
		if (block->session && !strcmp(block->session->peer_sid, client->id))
			udb_sync_abort(block, "peer quit");
		else if (block->syncing_from == client)
			block->syncing_from = NULL;

		if (block->pending_from == client)
		{
			block->pending_from = NULL;
			block->pending_deadline = 0;
			block->pending_round_id = 0;
		}
	}

	if (udb_bootstrap_peer == client)
		udb_bootstrap_peer = NULL;
	if (udb_reconcile.authority_peer == client)
	{
		udb_log(ULOG_INFO, "UDB_AUTHORITY_FAILOVER", client,
				"Authority peer $client disconnected; resetting reconciliation round",
				log_data_client("client", client));
		udb_reconcile_abort(udb_ctx, "authority disconnected", 1);
	}
	udb_ocl_origin_quit(client);

	udb_propagator_policy_changed(udb_ctx);
}
