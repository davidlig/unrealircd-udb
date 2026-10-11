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
	return NULL;
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

static int udb_is_propagator(UdbContext *ctx, Client *server)
{
	UdbPropagatorSelection selected;

	if (!server || !IsServer(server) || !MyConnect(server))
		return 0;
	return udb_select_propagator(ctx, 1, &selected) && !selected.is_local && selected.peer == server;
}

/* Copied foreground delivery and replay envelopes share the engine's 8 MiB
 * queue bound. Only stable IDs and revisions cross the writer boundary. */
typedef struct UdbNativeDelivery
{
	struct UdbNativeDelivery *next;
	UdbStorageEngine *engine;
	uint64_t owner, job;
	uint64_t module_revision, authority_revision;
	uint64_t replay_round;
	size_t length, reservation, credit;
	uint32_t kind;
	char peer_sid[4], peer_epoch[17];
	int broadcast, local, replay, cancelled;
	unsigned char bytes[1];
} UdbNativeDelivery;

static UdbNativeDelivery *udb_native_deliveries;
static UdbNativeDelivery *udb_native_delivery_tail;

static int udb_native_deliveries_present(void)
{
	return udb_native_deliveries != NULL;
}

static void udb_mutation_trigger_gap_recovery(Client *peer)
{
	(void)peer;
	udb_ready = 0;
	udb_sync_status = UDB_SYNC_DEGRADED;
	if (!udb_degraded_since)
		udb_degraded_since = time(NULL);
	udb_sync_hello_refresh_all();
	if (!udb_reconcile.next_retry_at)
		udb_reconcile.next_retry_at = time(NULL) + UDB_RECONCILE_RETRY_BASE;
}

static void udb_native_delivery_drop(UdbNativeDelivery *delivery)
{
	UdbNativeDelivery **link = &udb_native_deliveries;
	while (*link && *link != delivery)
		link = &(*link)->next;
	if (!*link)
		abort();
	*link = delivery->next;
	if (udb_native_delivery_tail == delivery)
	{
		udb_native_delivery_tail = NULL;
		for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
			udb_native_delivery_tail = item;
	}
	if (udb_storage_engine_delivery_release(delivery->engine, delivery->reservation) != UDB_STORAGE_OK)
		abort();
	udb_storage_free(delivery);
}

static int udb_native_delivery_current(UdbNativeDelivery *delivery)
{
	UdbStorageEngine *engine = delivery->engine;
	udb_storage_mutex_lock(&engine->mutex);
	int current = !engine->failed && !engine->stopping && engine->admission &&
				  delivery->module_revision == engine->module_revision &&
				  delivery->authority_revision == engine->authority_revision;
	udb_storage_mutex_unlock(&engine->mutex);
	if (!current || delivery->cancelled || !udb_ctx || udb_ctx->storage != engine)
		return 0;
	if (delivery->local)
	{
		UdbPropagatorSelection selected;
		return !udb_propagator_policy_present(udb_ctx) ||
			   (udb_select_propagator(udb_ctx, 1, &selected) && selected.is_local);
	}
	Client *peer = udb_ocl_find_server(delivery->peer_sid);
	UdbHelloPeer *hello = peer ? udb_hello_peer(peer, 0) : NULL;
	return peer && !IsDead(peer) && !IsDeadSocket(peer) && MyConnect(peer) && hello && hello->remote_epoch_known &&
		   !strcmp(hello->remote_epoch, delivery->peer_epoch) && udb_is_authorized_sync_source(udb_ctx, peer);
}

static UdbStorageResult udb_native_delivery_submit(UdbNativeDelivery *delivery)
{
	if (delivery->job || !delivery->credit || !udb_native_delivery_current(delivery))
		return UDB_STORAGE_BUSY;
	UdbStorageResult result = udb_storage_engine_submit_credit(
		delivery->engine, delivery->kind, delivery->owner, delivery->module_revision, delivery->authority_revision,
		delivery->bytes, delivery->length, delivery->credit, &delivery->job);
	if (result == UDB_STORAGE_OK)
	{
		delivery->reservation -= delivery->credit;
		delivery->credit = 0;
	}
	return result;
}

static UdbStorageResult udb_native_delivery_create(UdbStorageEngine *engine, Client *peer, uint32_t kind,
												   const void *bytes, size_t length, int broadcast, int local,
												   int replay, UdbNativeDelivery **out)
{
	*out = NULL;
	if (!engine || !bytes || length < UDB_STORAGE_MUTATION_HEADER ||
		length > UDB_STORAGE_QUEUE_LIMIT - sizeof(UdbNativeDelivery))
		return UDB_STORAGE_REJECTED;
	size_t credit = udb_storage_engine_job_charge(length);
	size_t envelope = udb_storage_allocation_charge(sizeof(UdbNativeDelivery) + length);
	if (!credit || !envelope || envelope > UDB_STORAGE_QUEUE_LIMIT - credit)
		return UDB_STORAGE_LIMIT;
	uint64_t owner = udb_native_owner_next(), module_revision, authority_revision;
	if (!owner)
		return UDB_STORAGE_LIMIT;
	udb_storage_mutex_lock(&engine->mutex);
	module_revision = engine->module_revision;
	authority_revision = engine->authority_revision;
	udb_storage_mutex_unlock(&engine->mutex);
	UdbStorageResult result =
		udb_storage_engine_delivery_reserve(engine, module_revision, authority_revision, envelope + credit);
	if (result != UDB_STORAGE_OK)
		return result;
	UdbNativeDelivery *delivery =
		udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*delivery) + length);
	if (!delivery)
	{
		udb_storage_engine_delivery_release(engine, envelope + credit);
		return UDB_STORAGE_LIMIT;
	}
	delivery->engine = engine;
	delivery->owner = owner;
	delivery->module_revision = module_revision;
	delivery->authority_revision = authority_revision;
	delivery->reservation = envelope + credit;
	delivery->credit = credit;
	delivery->kind = kind;
	delivery->length = length;
	delivery->broadcast = broadcast;
	delivery->local = local;
	delivery->replay = replay;
	memcpy(delivery->bytes, bytes, length);
	if (peer)
	{
		UdbHelloPeer *hello = udb_hello_peer(peer, 0);
		if (!hello || !hello->remote_epoch_known)
		{
			udb_storage_engine_delivery_release(engine, delivery->reservation);
			udb_storage_free(delivery);
			return UDB_STORAGE_REJECTED;
		}
		memcpy(delivery->peer_sid, peer->id, 4);
		memcpy(delivery->peer_epoch, hello->remote_epoch, 17);
	}
	if (udb_native_delivery_tail)
		udb_native_delivery_tail->next = delivery;
	else
		udb_native_deliveries = delivery;
	udb_native_delivery_tail = delivery;
	*out = delivery;
	return UDB_STORAGE_OK;
}

/* Bind/prune copied live traffic against the first agreed INF origin and
 * watermark. Pre-INF traffic is provisional, never an origin authority. */
static int udb_native_mutation_bind(Client *peer, unsigned long round, const char *sid, const char *epoch,
									uint64_t watermark)
{
	uint64_t expected = watermark;
	uint64_t number = udb_native_epoch_number(epoch);
	for (UdbNativeDelivery *item = udb_native_deliveries, *next; item; item = next)
	{
		next = item->next;
		if (!item->replay || item->cancelled)
			continue;
		if (!udb_native_delivery_current(item) || strcmp(item->peer_sid, peer->id) || memcmp(item->bytes, sid, 4) ||
			udb_storage_get_u64(item->bytes + 4) != number || (item->replay_round && item->replay_round != round))
			return 0;
		uint64_t sequence = udb_storage_get_u64(item->bytes + 12);
		if (!item->job && sequence <= watermark)
		{
			udb_native_delivery_drop(item);
			continue;
		}
		if (expected == UINT64_MAX || sequence != expected + 1)
			return 0;
		expected = sequence;
		item->replay_round = round;
	}
	return 1;
}

static int udb_native_mutation_pending(unsigned long round)
{
	for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
		if (!item->cancelled && item->replay && item->replay_round == round)
			return 1;
	return 0;
}

static int udb_native_mutation_replay(unsigned long round)
{
	for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
	{
		if (item->cancelled || !item->replay || item->replay_round != round || item->job)
			continue;
		if (udb_native_delivery_submit(item) != UDB_STORAGE_OK)
			return 0;
	}
	return 1;
}

static void udb_native_mutation_abort(unsigned long round)
{
	for (UdbNativeDelivery *item = udb_native_deliveries, *next; item; item = next)
	{
		next = item->next;
		if (round && (!item->replay || (item->replay_round && item->replay_round != round)))
			continue;
		if (item->job)
		{
			item->cancelled = 1;
			udb_storage_engine_cancel_owner(item->engine, item->owner);
		}
		else
			udb_native_delivery_drop(item);
	}
}

static void udb_native_mutation_round_begin(Client *peer)
{
	if (!udb_ctx || !udb_ctx->storage)
		return;
	UdbStorageEngine *engine = udb_ctx->storage;
	uint64_t old_revision, revision;
	udb_storage_mutex_lock(&engine->mutex);
	old_revision = engine->authority_revision;
	udb_storage_mutex_unlock(&engine->mutex);
	if (udb_storage_engine_authority(engine, &revision) != UDB_STORAGE_OK)
		return;
	for (UdbNativeDelivery *item = udb_native_deliveries, *next; item; item = next)
	{
		next = item->next;
		/* Only a provisional, unsubmitted tail from this still-selected peer
		 * survives into the first INF-bound round. It grants no root lineage. */
		UdbHelloPeer *hello = peer ? udb_hello_peer(peer, 0) : NULL;
		if (!item->job && item->replay && !item->replay_round && !item->cancelled &&
			item->authority_revision == old_revision && peer && !strcmp(peer->id, item->peer_sid) && hello &&
			hello->remote_epoch_known && !strcmp(hello->remote_epoch, item->peer_epoch))
		{
			item->authority_revision = revision;
			continue;
		}
		if (item->job)
		{
			item->cancelled = 1;
			udb_storage_engine_cancel_owner(engine, item->owner);
		}
		else
			udb_native_delivery_drop(item);
	}
}

static void udb_native_mutation_cleanup(void)
{
	/* Lifecycle stops/joins the writer before dropping submitted envelopes. */
	while (udb_native_deliveries)
	{
		udb_storage_engine_cancel_owner(udb_native_deliveries->engine, udb_native_deliveries->owner);
		udb_native_delivery_drop(udb_native_deliveries);
	}
}

static int udb_native_mutation_ingress(UdbContext *ctx, Client *source, Client *peer, uint32_t kind, const char *epoch,
									   uint64_t sequence, const char *path, const char *value, char block,
									   int broadcast)
{
	if (!ctx || !ctx->storage || !source || !IsServer(source) || strlen(source->id) != 3 ||
		!udb_storage_sid_valid(source->id) || !udb_hello_epoch_valid(epoch) || !sequence ||
		!udb_is_authorized_sync_source(ctx, peer))
		return UDB_ERR_FORBIDDEN;
	UdbStorageEngine *engine = ctx->storage;
	UdbNativeReceiveScratch *scratch = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	unsigned char *payload = NULL;
	if (!scratch)
		return UDB_ERR_FATAL;
	size_t row_length = 0;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (kind == UDB_STORAGE_JOB_DROP)
	{
		if (udb_storage_stage_block_index((unsigned char)block) < 0)
			result = UDB_STORAGE_REJECTED;
	}
	else
	{
		result = udb_native_key_parse(path, &scratch->key);
		if (result == UDB_STORAGE_OK && kind == UDB_STORAGE_JOB_POINT)
			result = udb_native_value_decode(value, &scratch->key.row, scratch->value, sizeof(scratch->value));
		UdbStorageRecord record = {0};
		record.row = scratch->key.row;
		record.operation = kind == UDB_STORAGE_JOB_POINT ? UDB_STORAGE_PUT : UDB_STORAGE_PREFIX_DELETE;
		record.stamp.sequence = 1;
		if (result == UDB_STORAGE_OK)
			result = udb_native_row_validate(&record, &ctx->native_schema);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_row_encode(&record.row, scratch->bytes, sizeof(scratch->bytes), &row_length);
		block = (char)record.row.block;
	}
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_free(scratch);
		return result == UDB_STORAGE_REJECTED ? UDB_ERR_PARAMS : UDB_ERR_FATAL;
	}
	int replay = udb_reconcile.active || !udb_ready;
	uint64_t number = udb_native_epoch_number(epoch);
	uint64_t expected = ctx->last_applied_seq;
	if (!replay)
	{
		if (!ctx->authority_epoch_known || strcmp(source->id, ctx->authority_sid) ||
			strcmp(epoch, ctx->authority_epoch))
		{
			udb_storage_free(scratch);
			udb_mutation_trigger_gap_recovery(peer);
			return UDB_ERR_NO_SYNC;
		}
		if (sequence <= expected)
		{
			udb_storage_free(scratch);
			return 0;
		}
	}
	else if (udb_reconcile.source_known)
	{
		if (udb_reconcile.authority_peer != peer || strcmp(source->id, udb_reconcile.source_sid) ||
			strcmp(epoch, udb_reconcile.source_epoch))
		{
			udb_storage_free(scratch);
			return UDB_ERR_FORBIDDEN;
		}
		expected = udb_reconcile.watermark_seq;
		if (sequence <= expected)
		{
			udb_storage_free(scratch);
			return 0;
		}
	}
	else
		expected = sequence - 1; /* Provisional until INF validates/prunes it. */
	for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
	{
		if (item->cancelled || item->local || item->replay != replay)
			continue;
		if (memcmp(item->bytes, source->id, 4) || udb_storage_get_u64(item->bytes + 4) != number ||
			strcmp(item->peer_sid, peer->id) || !udb_native_delivery_current(item))
		{
			udb_storage_free(scratch);
			return UDB_ERR_FORBIDDEN;
		}
		expected = udb_storage_get_u64(item->bytes + 12);
	}
	if (expected == UINT64_MAX || sequence != expected + 1)
	{
		udb_storage_free(scratch);
		udb_mutation_trigger_gap_recovery(peer);
		return UDB_ERR_NO_SYNC;
	}
	size_t length = UDB_STORAGE_MUTATION_HEADER + row_length;
	payload = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, length);
	if (!payload)
	{
		udb_storage_free(scratch);
		return UDB_ERR_FATAL;
	}
	memcpy(payload, source->id, 4);
	udb_storage_put_u64(payload + 4, number);
	udb_storage_put_u64(payload + 12, sequence);
	payload[20] = (unsigned char)block;
	if (row_length)
		memcpy(payload + UDB_STORAGE_MUTATION_HEADER, scratch->bytes, row_length);
	UdbNativeDelivery *delivery = NULL;
	result = udb_native_delivery_create(engine, peer, kind, payload, length, broadcast, 0, replay, &delivery);
	udb_storage_free(payload);
	udb_storage_free(scratch);
	if (result != UDB_STORAGE_OK)
		return UDB_ERR_FATAL;
	if (replay && udb_reconcile.source_known)
		delivery->replay_round = udb_reconcile.round_id;
	if (!replay && udb_native_delivery_submit(delivery) != UDB_STORAGE_OK)
	{
		udb_native_delivery_drop(delivery);
		return UDB_ERR_FATAL;
	}
	if (replay && delivery->replay_round)
		udb_native_receive_replay_pump();
	return 0;
}

static void udb_mutation_ins(UdbContext *ctx, Client *source, Client *peer, const char *target, const char *epoch,
							 uint64_t sequence, const char *path, const char *value, int is_for_me, int broadcast)
{
	(void)target;
	int error = is_for_me ? udb_native_mutation_ingress(ctx, source, peer, UDB_STORAGE_JOB_POINT, epoch, sequence, path,
														value, 0, broadcast)
						  : UDB_ERR_FORBIDDEN;
	if (error)
		udb_protocol_mutation_error(source, "INS", error, path && *path ? *path : '0');
}

static void udb_mutation_del(UdbContext *ctx, Client *source, Client *peer, const char *target, const char *epoch,
							 uint64_t sequence, const char *path, int is_for_me, int broadcast)
{
	(void)target;
	int error = is_for_me ? udb_native_mutation_ingress(ctx, source, peer, UDB_STORAGE_JOB_PREFIX, epoch, sequence,
														path, NULL, 0, broadcast)
						  : UDB_ERR_FORBIDDEN;
	if (error)
		udb_protocol_mutation_error(source, "DEL", error, path && *path ? *path : '0');
}

static void udb_mutation_drp(UdbContext *ctx, Client *source, Client *peer, const char *target, const char *epoch,
							 uint64_t sequence, char block, int is_for_me, int broadcast)
{
	(void)target;
	int error = is_for_me ? udb_native_mutation_ingress(ctx, source, peer, UDB_STORAGE_JOB_DROP, epoch, sequence, NULL,
														NULL, block, broadcast)
						  : UDB_ERR_FORBIDDEN;
	if (error)
		udb_protocol_mutation_error(source, "DRP", error, block);
}

static int udb_mutation_expire_local(UdbContext *ctx, const char *path, time_t expected)
{
	UdbPropagatorSelection selected;
	if (!ctx || !ctx->storage || !udb_ready || udb_reconcile.active || expected <= 0 || expected > TStime() ||
		!ctx->authority_epoch_known || strcmp(ctx->authority_sid, me.id) ||
		(udb_propagator_policy_present(ctx) && (!udb_select_propagator(ctx, 1, &selected) || !selected.is_local)))
		return 0;
	UdbNativeReceiveScratch *scratch =
		udb_storage_alloc(&ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return 0;
	size_t row_length = 0;
	UdbStorageResult result = udb_native_key_parse(path, &scratch->key);
	if (result == UDB_STORAGE_OK && (scratch->key.row.block != 'K' || scratch->key.row.depth != 2))
		result = UDB_STORAGE_REJECTED;
	if (result == UDB_STORAGE_OK)
		result = udb_storage_row_encode(&scratch->key.row, scratch->bytes, sizeof(scratch->bytes), &row_length);
	size_t length = UDB_STORAGE_MUTATION_HEADER + 8 + row_length;
	unsigned char *bytes =
		result == UDB_STORAGE_OK ? udb_storage_alloc(&ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, length) : NULL;
	UdbNativeDelivery *delivery = NULL;
	if (bytes)
	{
		memcpy(bytes, me.id, 4);
		udb_storage_put_u64(bytes + 4, udb_native_epoch_number(ctx->authority_epoch));
		bytes[20] = 'K';
		bytes[21] = 1;
		udb_storage_put_u64(bytes + UDB_STORAGE_MUTATION_HEADER, (uint64_t)expected);
		memcpy(bytes + UDB_STORAGE_MUTATION_HEADER + 8, scratch->bytes, row_length);
		result =
			udb_native_delivery_create(ctx->storage, NULL, UDB_STORAGE_JOB_EXPIRE, bytes, length, 1, 1, 0, &delivery);
	}
	else
		result = UDB_STORAGE_LIMIT;
	udb_storage_free(bytes);
	udb_storage_free(scratch);
	if (result != UDB_STORAGE_OK)
		return 0;
	if (udb_native_delivery_submit(delivery) != UDB_STORAGE_OK)
	{
		udb_native_delivery_drop(delivery);
		return 0;
	}
	return 1;
}

static void udb_mutation_exp(UdbContext *ctx, Client *client, Client *peer, const char *target, const char *path,
							 time_t expected, int is_for_me, int broadcast)
{
	(void)target;
	if (!is_for_me || broadcast || client != peer || !udb_peer_authorizes_us(peer) || expected <= 0)
	{
		udb_protocol_mutation_error(client, "EXP", UDB_ERR_FORBIDDEN, 'K');
		return;
	}
	UdbPropagatorSelection selected = {0};
	if (udb_propagator_policy_present(ctx) && (!udb_select_propagator(ctx, 1, &selected) || !selected.is_local))
	{
		if (!selected.peer)
			return;
		UdbNativeKey *key =
			ctx && ctx->storage ? udb_storage_alloc(&ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*key)) : NULL;
		char pattern[UDB_STORAGE_COMPONENT_MAX + 1];
		if (key && udb_native_key_parse(path, key) == UDB_STORAGE_OK && key->row.block == 'K' && key->row.depth == 2 &&
			udb_native_span_string(key->row.components[1], pattern, sizeof(pattern)))
		{
			char type = (char)(key->row.components[0].data[0] - 'a' + 'A');
			UdbLineExpiryPending *pending = udb_line_expiry_pending_add(type, pattern, expected);
			if (pending && !pending->sent &&
				udb_send_db_to_one(selected.peer, ":%s DB %s EXP %s %" PRIu64, me.id, selected.peer->id, path,
								   (uint64_t)expected))
				pending->sent = 1;
		}
		udb_storage_free(key);
		return;
	}
	udb_mutation_expire_local(ctx, path, expected);
}

static void udb_native_mutation_relay(UdbNativeDelivery *delivery, UdbStorageJob *job)
{
	if (!delivery->broadcast || !udb_native_delivery_current(delivery))
		return;
	Client *except = delivery->local ? NULL : udb_ocl_find_server(delivery->peer_sid);
	char epoch[17];
	snprintf(epoch, sizeof(epoch), "%016" PRIx64, job->committed_source_epoch);
	if (job->kind == UDB_STORAGE_JOB_DROP)
	{
		udb_sendto_confirmed_servers(except, ":%s DB * DRP %s %" PRIu64 " %c", job->committed_source_sid, epoch,
									 job->committed_source_sequence,
									 *((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 20));
		return;
	}
	size_t offset = UDB_STORAGE_MUTATION_HEADER + (job->kind == UDB_STORAGE_JOB_EXPIRE ? 8 : 0);
	UdbStorageRow row;
	if (job->length <= offset ||
		udb_storage_row_decode(job->payload + offset, job->length - offset, &row) != UDB_STORAGE_OK)
		return;
	UdbNativeSendScratch *scratch = udb_storage_alloc(&job->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
	{
		udb_mutation_trigger_gap_recovery(except);
		return;
	}
	if (udb_native_key_path(&row, scratch->path, sizeof(scratch->path)) == UDB_STORAGE_OK)
	{
		if (job->kind == UDB_STORAGE_JOB_POINT &&
			udb_native_value_encode(&row, scratch->value, sizeof(scratch->value)) == UDB_STORAGE_OK)
			udb_sendto_confirmed_servers(except, ":%s DB * INS %s %" PRIu64 " %s :%s", job->committed_source_sid, epoch,
										 job->committed_source_sequence, scratch->path, scratch->value);
		else if (job->kind != UDB_STORAGE_JOB_POINT)
			udb_sendto_confirmed_servers(except, ":%s DB * DEL %s %" PRIu64 " %s", job->committed_source_sid, epoch,
										 job->committed_source_sequence, scratch->path);
	}
	udb_storage_free(scratch);
}

static int udb_native_local_bind(UdbContext *ctx)
{
	if (!ctx || !ctx->storage || udb_reconcile.active)
		return 0;
	UdbPropagatorSelection selected;
	if (udb_propagator_policy_present(ctx) && (!udb_select_propagator(ctx, 1, &selected) || !selected.is_local))
		return 0;
	for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
		if (!item->cancelled && item->kind == UDB_STORAGE_JOB_BIND)
			return 1;
	unsigned char bytes[UDB_STORAGE_MUTATION_HEADER] = {0};
	uint64_t epoch = 0;
	if (RAND_bytes((unsigned char *)&epoch, sizeof(epoch)) != 1 || !epoch)
		return 0;
	memcpy(bytes, me.id, 4);
	udb_storage_put_u64(bytes + 4, epoch);
	UdbNativeDelivery *delivery = NULL;
	if (udb_native_delivery_create(ctx->storage, NULL, UDB_STORAGE_JOB_BIND, bytes, sizeof(bytes), 0, 1, 0,
								   &delivery) != UDB_STORAGE_OK)
		return 0;
	if (udb_native_delivery_submit(delivery) != UDB_STORAGE_OK)
	{
		udb_native_delivery_drop(delivery);
		return 0;
	}
	return 1;
}

static void udb_native_mutation_complete(UdbStorageJob *job)
{
	UdbNativeDelivery *delivery = udb_native_deliveries;
	while (delivery && (delivery->owner != job->owner || delivery->job != job->id))
		delivery = delivery->next;
	if (!delivery || delivery->engine != job->engine)
		return;
	int current = udb_native_delivery_current(delivery);
	if (job->result == UDB_STORAGE_OK && job->committed_generation && job->before && job->after)
	{
		udb_policy_notify_deferred = 1;
		UdbStorageResult result = udb_native_runtime_publication(job);
		const UdbStorageState *state = &job->after->state;
		memcpy(udb_ctx->authority_sid, state->source_sid, 4);
		snprintf(udb_ctx->authority_epoch, sizeof(udb_ctx->authority_epoch), "%016" PRIx64, state->source_epoch);
		udb_ctx->authority_epoch_known = 1;
		udb_ctx->last_applied_seq = state->source_sequence;
		udb_ctx->current_seq = !strcmp(state->source_sid, me.id) ? state->source_sequence : 0;
		udb_policy_notify_deferred = 0;
		udb_propagator_policy_flush(udb_ctx);
		if (result == UDB_STORAGE_OK && current && udb_native_delivery_current(delivery))
		{
			if (job->kind == UDB_STORAGE_JOB_BIND && !udb_reconcile.active)
			{
				udb_ready = 1;
				udb_last_successful_sync = time(NULL);
				udb_sync_status = UDB_SYNC_OK;
				udb_degraded_since = 0;
				udb_bootstrap_peer = NULL;
				udb_offer_reconciliation_to_downstreams();
			}
			else
				udb_native_mutation_relay(delivery, job);
		}
		else
		{
			udb_ready = 0;
			udb_sync_status = UDB_SYNC_DEGRADED;
			udb_native_mutation_abort(0);
			if (udb_reconcile.active)
				udb_reconcile_abort(udb_ctx, "native durable mutation effects unavailable", 1);
			udb_mutation_trigger_gap_recovery(NULL);
		}
	}
	else if (job->result != UDB_STORAGE_STAGED && !delivery->cancelled)
	{
		Client *peer = udb_ocl_find_server(delivery->peer_sid);
		udb_protocol_mutation_error(peer, job->kind == UDB_STORAGE_JOB_POINT ? "INS" : "DEL", UDB_ERR_FATAL,
									*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 20));
		udb_native_mutation_abort(0);
		if (udb_reconcile.active)
			udb_reconcile_abort(udb_ctx, "native live mutation failed", 1);
		udb_mutation_trigger_gap_recovery(peer);
	}
	udb_native_delivery_drop(delivery);
}
