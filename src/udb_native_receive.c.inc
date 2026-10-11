/* Foreground receive ownership is independent of writer-owned disk stages.
 * Network frames enqueue copied canonical jobs; only durable completions may
 * acknowledge END or publish a complete six-block import. */

typedef struct UdbNativeReceiveRound
{
	struct UdbNativeReceiveRound *next;
	UdbStorageEngine *engine;
	UdbStorageStageSpec specs[6];
	uint64_t owner;
	uint64_t module_revision;
	uint64_t authority_revision;
	uint64_t promote_job;
	uint64_t abort_job;
	char peer_sid[4];
	char peer_epoch[17];
	unsigned long round;
	unsigned int inventory;
	unsigned int durable;
	int abandoned;
	int promoted;
} UdbNativeReceiveRound;

typedef struct UdbNativeReceiveScratch
{
	UdbNativeKey key;
	unsigned char value[UDB_STORAGE_VALUE_MAX + 3];
	unsigned char bytes[UDB_STORAGE_ROW_MAX];
} UdbNativeReceiveScratch;

static UdbNativeReceiveRound *udb_native_receive_rounds;

static uint64_t udb_native_epoch_number(const char epoch[17])
{
	uint64_t value = 0;
	for (unsigned int i = 0; i < 16; i++)
		value = (value << 4) | (uint64_t)(epoch[i] <= '9' ? epoch[i] - '0' : epoch[i] - 'a' + 10);
	return value;
}

static UdbNativeReceiveRound *udb_native_receive_owner(uint64_t owner)
{
	for (UdbNativeReceiveRound *round = udb_native_receive_rounds; round; round = round->next)
		if (round->owner == owner)
			return round;
	return NULL;
}

static int udb_native_receive_peer(UdbNativeReceiveRound *round, Client *peer)
{
	if (!round)
		return 0;
	udb_storage_mutex_lock(&round->engine->mutex);
	int revision_valid = round->module_revision == round->engine->module_revision &&
						 round->authority_revision == round->engine->authority_revision && !round->engine->failed &&
						 !round->engine->stopping;
	udb_storage_mutex_unlock(&round->engine->mutex);
	if (!revision_valid)
		return 0;
	UdbHelloPeer *hello = peer ? udb_hello_peer(peer, 0) : NULL;
	return round && !round->abandoned && peer && !IsDead(peer) && !IsDeadSocket(peer) && MyConnect(peer) &&
		   !strcmp(peer->id, round->peer_sid) && hello && hello->remote_epoch_known &&
		   !strcmp(hello->remote_epoch, round->peer_epoch) && udb_has_staged_sync(peer) &&
		   udb_is_authorized_sync_source(udb_ctx, peer) && udb_reconcile.active &&
		   udb_reconcile.authority_peer == peer && udb_reconcile.round_id == round->round;
}

static UdbNativeReceiveRound *udb_native_receive_find(Client *peer, unsigned long id)
{
	for (UdbNativeReceiveRound *round = udb_native_receive_rounds; round; round = round->next)
		if (round->round == id && udb_native_receive_peer(round, peer))
			return round;
	return NULL;
}

static void udb_native_receive_drop(UdbNativeReceiveRound *round)
{
	UdbNativeReceiveRound **link = &udb_native_receive_rounds;
	while (*link && *link != round)
		link = &(*link)->next;
	if (!*link)
		abort();
	*link = round->next;
	udb_storage_free(round);
}

static UdbStorageResult udb_native_receive_submit(UdbNativeReceiveRound *round, uint32_t kind, const void *bytes,
												  size_t length, uint64_t *id)
{
	return udb_storage_engine_submit(round->engine, kind, round->owner, round->module_revision,
									 round->authority_revision, bytes, length, id);
}

/* An abort may encounter a full copied queue. Retain its small charged owner
 * and retry admission once per event; never leak a durable pending-root owner. */
static void udb_native_receive_abort(unsigned long id)
{
	udb_native_mutation_abort(id);
	for (UdbNativeReceiveRound *round = udb_native_receive_rounds; round; round = round->next)
		if (!round->abandoned && (!id || round->round == id))
		{
			round->abandoned = 1;
			udb_storage_engine_cancel_owner(round->engine, round->owner);
		}
}

static void udb_native_receive_cleanup(void)
{
	while (udb_native_receive_rounds)
	{
		udb_storage_engine_cancel_owner(udb_native_receive_rounds->engine, udb_native_receive_rounds->owner);
		udb_native_receive_drop(udb_native_receive_rounds);
	}
}

/* Strict INF admission captures origin before any block comparison. Duplicate
 * inventory cannot replace a spec whose BEGIN/PUT jobs are already queued. */
static int udb_native_receive_inf(Client *peer, unsigned long id, char block, const char *digest, uint64_t count,
								  const char *sid, const char *epoch, uint64_t watermark)
{
	int index = udb_storage_stage_block_index((unsigned char)block);
	UdbStorageGeneration *pin = NULL;
	UdbNativeReceiveRound *round;
	char normalized[65];
	if (!udb_ctx || !udb_ctx->storage || index < 0 || !id || !peer || !udb_has_staged_sync(peer) ||
		!udb_is_authorized_sync_source(udb_ctx, peer) || !sid || strlen(sid) != 3 || !udb_storage_sid_valid(sid) ||
		!udb_hello_epoch_valid(epoch) || !udb_digest_parse(digest, normalized))
		return UDB_ERR_PARAMS;
	if (udb_reconcile.active && udb_reconcile.authority_peer == peer && id < udb_reconcile.round_id)
		return UDB_ERR_NO_SYNC;
	udb_reconcile_start(peer, id);
	if (!udb_reconcile_accept_source(peer, id, sid, epoch, watermark) ||
		!udb_native_mutation_bind(peer, id, sid, epoch, watermark))
		return UDB_ERR_PARAMS;
	round = udb_native_receive_find(peer, id);
	if (!round)
	{
		unsigned int owners = 0;
		for (UdbNativeReceiveRound *entry = udb_native_receive_rounds; entry; entry = entry->next)
			owners++;
		UdbHelloPeer *hello = udb_hello_peer(peer, 0);
		if (owners >= 8 || udb_native_owner_sequence == UINT64_MAX || !hello || !hello->remote_epoch_known)
			return UDB_ERR_FATAL;
		round = udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*round));
		if (!round)
			return UDB_ERR_FATAL;
		round->engine = udb_ctx->storage;
		round->owner = udb_native_owner_next();
		round->round = id;
		memcpy(round->peer_sid, peer->id, 4);
		memcpy(round->peer_epoch, hello->remote_epoch, 17);
		udb_storage_mutex_lock(&round->engine->mutex);
		round->module_revision = round->engine->module_revision;
		round->authority_revision = round->engine->authority_revision;
		udb_storage_mutex_unlock(&round->engine->mutex);
		round->next = udb_native_receive_rounds;
		udb_native_receive_rounds = round;
	}
	unsigned int mask = 1U << index;
	if (round->inventory & mask)
		return UDB_ERR_PARAMS;
	UdbStorageStageSpec *spec = round->specs + index;
	spec->head.owner = round->owner;
	spec->head.round = id;
	spec->head.source_sequence = watermark;
	spec->head.source_epoch = udb_native_epoch_number(epoch);
	memcpy(spec->head.source_sid, sid, 4);
	spec->block = (unsigned char)block;
	spec->count = count;
	spec->rows_limit = udb_cfg ? udb_cfg->max_staged_records : UDB_DEFAULT_MAX_STAGED_RECORDS;
	spec->physical_limit = udb_cfg ? udb_cfg->max_staged_bytes : UDB_DEFAULT_MAX_STAGED_BYTES;
	if (count > spec->rows_limit)
		return UDB_ERR_PARAMS;
	for (unsigned int i = 0; i < 32; i++)
	{
		int hi = normalized[2 * i] <= '9' ? normalized[2 * i] - '0' : normalized[2 * i] - 'a' + 10;
		int lo = normalized[2 * i + 1] <= '9' ? normalized[2 * i + 1] - '0' : normalized[2 * i + 1] - 'a' + 10;
		spec->digest[i] = (unsigned char)((hi << 4) | lo);
	}
	if (udb_storage_engine_pin(round->engine, &pin) != UDB_STORAGE_OK)
		return UDB_ERR_FATAL;
	int equal = pin->state.blocks[index].summary.count == count &&
				!memcmp(pin->state.blocks[index].summary.digest, spec->digest, 32);
	if (udb_storage_engine_release(round->engine, &pin) != UDB_STORAGE_OK)
		return UDB_ERR_FATAL;
	round->inventory |= mask;
	udb_reconcile.compared_blocks |= mask;
	udb_reconcile.last_activity = time(NULL);
	udb_reconcile.deadline =
		udb_reconcile.last_activity + (udb_cfg ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT);
	if (equal)
	{
		uint64_t job;
		if (udb_native_receive_submit(round, UDB_STORAGE_STAGE_REUSE, spec, sizeof(*spec), &job) != UDB_STORAGE_OK)
			return UDB_ERR_FATAL;
	}
	else
	{
		UdbBlock *descriptor = udb_block_by_letter(udb_ctx, block);
		if (!descriptor || !udb_send_db_to_one(peer, ":%s DB %s RES %lu %c", me.id, peer->id, id, block))
			return UDB_ERR_FATAL;
		udb_reconcile_record_res(peer, id, block);
		descriptor->pending_from = peer;
		descriptor->pending_round_id = id;
		descriptor->pending_deadline = udb_reconcile.deadline;
	}
	return 0;
}

static int udb_sync_begin(UdbBlock *block, Client *peer, unsigned long id, const char *txid, const char *digest,
						  uint64_t watermark)
{
	UdbNativeReceiveRound *round = udb_native_receive_find(peer, id);
	int index = block ? udb_storage_stage_block_index((unsigned char)block->letter) : -1;
	char expected[65], normalized[65];
	if (!round || index < 0 || !txid || !*txid || strlen(txid) > UDB_TXID_MAX || block->session ||
		block->pending_from != peer || block->pending_round_id != id || !(round->inventory & (1U << index)))
		return UDB_ERR_NO_SYNC;
	for (const unsigned char *p = (const unsigned char *)txid; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' ||
			  *p == '_'))
			return UDB_ERR_PARAMS;
	udb_native_digest_hex(round->specs[index].digest, expected);
	if (watermark != round->specs[index].head.source_sequence || !udb_digest_parse(digest, normalized) ||
		strcmp(normalized, expected))
		return UDB_ERR_PARAMS;
	UdbSyncSession *session = udb_storage_alloc(&round->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*session));
	if (!session)
		return UDB_ERR_FATAL;
	uint64_t job;
	if (udb_native_receive_submit(round, UDB_STORAGE_STAGE_BEGIN, round->specs + index, sizeof(round->specs[index]),
								  &job) != UDB_STORAGE_OK)
	{
		udb_storage_free(session);
		return UDB_ERR_FATAL;
	}
	session->owner = round->owner;
	memcpy(session->peer_sid, round->peer_sid, 4);
	session->round_id = id;
	memcpy(session->txid, txid, strlen(txid) + 1);
	session->started_at = session->last_activity = time(NULL);
	session->deadline =
		session->started_at + (udb_cfg ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT);
	session->absolute_deadline =
		session->started_at + (udb_cfg ? udb_cfg->sync_absolute_timeout : UDB_SYNC_ABSOLUTE_TIMEOUT);
	session->watermark_seq = watermark;
	block->session = session;
	block->syncing_from = peer;
	udb_block_clear_pending(block);
	return 0;
}

static int udb_native_receive_session(UdbBlock *block, Client *peer, unsigned long id, const char *txid,
									  UdbNativeReceiveRound **out)
{
	UdbSyncSession *session = block ? block->session : NULL;
	*out = udb_native_receive_find(peer, id);
	time_t now = time(NULL);
	return *out && session && session->owner == (*out)->owner && !session->end_job && session->round_id == id && txid &&
		   !strcmp(session->txid, txid) && !strcmp(session->peer_sid, peer->id) && now < session->deadline &&
		   now < session->absolute_deadline;
}

static int udb_sync_put(UdbBlock *block, Client *peer, unsigned long id, const char *txid, const char *path,
						const char *value)
{
	UdbNativeReceiveRound *round;
	if (!udb_native_receive_session(block, peer, id, txid, &round))
		return UDB_ERR_NO_SYNC;
	int index = udb_storage_stage_block_index((unsigned char)block->letter);
	if (block->session->received_puts >= round->specs[index].count)
		return UDB_ERR_PARAMS;
	UdbNativeReceiveScratch *scratch =
		udb_storage_alloc(&round->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_ERR_FATAL;
	UdbStorageResult result = udb_native_key_parse(path, &scratch->key);
	if (result == UDB_STORAGE_OK && scratch->key.row.block != (unsigned char)block->letter)
		result = UDB_STORAGE_REJECTED;
	if (result == UDB_STORAGE_OK)
		result = udb_native_value_decode(value, &scratch->key.row, scratch->value, sizeof(scratch->value));
	size_t length = 0;
	if (result == UDB_STORAGE_OK)
		result = udb_storage_row_encode(&scratch->key.row, scratch->bytes, sizeof(scratch->bytes), &length);
	uint64_t job;
	if (result == UDB_STORAGE_OK)
		result = udb_native_receive_submit(round, UDB_STORAGE_STAGE_PUT, scratch->bytes, length, &job);
	udb_storage_free(scratch);
	if (result != UDB_STORAGE_OK)
		return result == UDB_STORAGE_REJECTED ? UDB_ERR_PARAMS : UDB_ERR_FATAL;
	block->session->received_puts++;
	block->session->last_activity = time(NULL);
	block->session->deadline =
		block->session->last_activity + (udb_cfg ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT);
	return 0;
}

static int udb_sync_end(UdbContext *ctx, UdbBlock *block, Client *peer, unsigned long id, const char *txid,
						const char *digest, uint64_t watermark)
{
	UdbNativeReceiveRound *round;
	char expected[65], normalized[65];
	if (!ctx || !udb_native_receive_session(block, peer, id, txid, &round))
		return UDB_ERR_NO_SYNC;
	int index = udb_storage_stage_block_index((unsigned char)block->letter);
	udb_native_digest_hex(round->specs[index].digest, expected);
	if (!udb_digest_parse(digest, normalized) || strcmp(expected, normalized) ||
		watermark != round->specs[index].head.source_sequence ||
		block->session->received_puts != round->specs[index].count)
		return UDB_ERR_PARAMS;
	unsigned char letter = (unsigned char)block->letter;
	if (udb_native_receive_submit(round, UDB_STORAGE_STAGE_END, &letter, 1, &block->session->end_job) != UDB_STORAGE_OK)
		return UDB_ERR_FATAL;
	return 0;
}

static int udb_native_receive_promote(UdbContext *ctx)
{
	UdbNativeReceiveRound *round =
		ctx ? udb_native_receive_find(udb_reconcile.authority_peer, udb_reconcile.round_id) : NULL;
	if (!round || round->inventory != 63 || round->durable != 63 || round->promote_job || !udb_reconcile.source_known ||
		!udb_reconcile.watermark_known || udb_has_active_sessions(ctx) || udb_has_pending_requests(ctx))
		return 0;
	if (udb_native_receive_submit(round, UDB_STORAGE_STAGE_PROMOTE, NULL, 0, &round->promote_job) != UDB_STORAGE_OK)
		udb_reconcile_abort(ctx, "native promotion admission failed", 1);
	return 0; /* READY belongs to the durable completion, never to admission. */
}

static void udb_native_receive_complete(UdbStorageJob *job)
{
	UdbNativeReceiveRound *round = udb_native_receive_owner(job->owner);
	if (!round || round->engine != job->engine)
		return;
	Client *peer = udb_ocl_find_server(round->peer_sid);
	if (job->kind == UDB_STORAGE_STAGE_ABORT)
	{
		if (round->abandoned && round->abort_job == job->id)
		{
			if (job->result == UDB_STORAGE_OK || job->result == UDB_STORAGE_STAGED)
				udb_native_receive_drop(round);
			else
				round->abort_job = 0;
		}
		return;
	}
	/* Cancellation after the writer's final barrier cannot undo durability.
	 * Apply this receipt in order even if its network round has since vanished;
	 * only a still-authorized matching round may restore readiness. */
	if (job->kind == UDB_STORAGE_STAGE_PROMOTE && job->result == UDB_STORAGE_OK && job->committed_generation &&
		job->after && round->promote_job == job->id)
	{
		int current = udb_native_receive_peer(round, peer);
		UdbStorageResult effects = udb_native_receive_publication(job, round->round, current ? peer : NULL);
		if (effects != UDB_STORAGE_OK || !current)
		{
			udb_ready = 0;
			udb_sync_status = UDB_SYNC_DEGRADED;
			if (udb_reconcile.active && udb_reconcile.round_id == round->round)
				udb_reconcile_abort(udb_ctx, "native promotion effects or authority unavailable", 1);
			else
				round->abandoned = 1;
		}
		else
		{
			round->promoted = 1;
			udb_native_receive_replay_pump();
		}
		return;
	}
	if (round->abandoned || !udb_native_receive_peer(round, peer))
		return;
	if (job->result != UDB_STORAGE_OK && job->result != UDB_STORAGE_STAGED)
	{
		udb_log(ULOG_WARNING, "UDB_NATIVE_STAGE_FAILED", peer,
				"Native stage job $kind failed with storage result $result", log_data_integer("kind", job->kind),
				log_data_integer("result", job->result));
		udb_reconcile_abort(udb_ctx, "native stage preparation or persistence failed", 1);
		return;
	}
	if (job->kind == UDB_STORAGE_STAGE_BEGIN || job->kind == UDB_STORAGE_STAGE_PUT)
		return;
	if (job->result != UDB_STORAGE_OK || !job->committed_generation || !job->after)
	{
		udb_reconcile_abort(udb_ctx, "missing native durable stage receipt", 1);
		return;
	}
	if (job->kind == UDB_STORAGE_STAGE_END || job->kind == UDB_STORAGE_STAGE_REUSE)
	{
		unsigned char block;
		if (job->kind == UDB_STORAGE_STAGE_END)
			block = job->payload[0];
		else
		{
			UdbStorageStageSpec spec;
			memcpy(&spec, job->payload, sizeof(spec));
			block = spec.block;
		}
		int index = udb_storage_stage_block_index(block);
		if (index < 0 || (round->durable & (1U << index)))
		{
			udb_reconcile_abort(udb_ctx, "duplicate native stage completion", 1);
			return;
		}
		if (job->kind == UDB_STORAGE_STAGE_END)
		{
			UdbBlock *descriptor = udb_block_by_letter(udb_ctx, (char)block);
			UdbSyncSession *session = descriptor ? descriptor->session : NULL;
			char digest[65];
			udb_native_digest_hex(round->specs[index].digest, digest);
			if (!session || session->owner != round->owner || session->end_job != job->id ||
				!udb_send_db_to_one(peer, ":%s DB %s ACK %lu %c %s %s %" PRIu64, me.id, peer->id, round->round, block,
									session->txid, digest, session->watermark_seq))
			{
				udb_reconcile_abort(udb_ctx, "native END acknowledgement failed", 1);
				return;
			}
			udb_sync_session_free(descriptor);
		}
		round->durable |= 1U << index;
		udb_reconcile_record_end(peer, (char)block, round->round);
		udb_reconcile_check(udb_ctx);
	}
}

/* READY is delayed until every accepted post-watermark mutation has its own
 * durable receipt and ordered effects. New traffic joins this bounded tail. */
static void udb_native_receive_replay_pump(void)
{
	if (!udb_reconcile.active)
		return;
	UdbNativeReceiveRound *round = udb_native_receive_find(udb_reconcile.authority_peer, udb_reconcile.round_id);
	if (!round || !round->promoted)
		return;
	if (!udb_native_mutation_replay(round->round))
	{
		udb_reconcile_abort(udb_ctx, "native replay admission failed", 1);
		return;
	}
	if (udb_native_mutation_pending(round->round))
		return;
	unsigned long id = round->round;
	udb_reconcile_reset();
	udb_reconcile.retry_count = 0;
	udb_reconcile.next_retry_at = 0;
	udb_native_receive_drop(round);
	UdbPropagatorSelection selected;
	int local =
		!udb_propagator_policy_present(udb_ctx) || (udb_select_propagator(udb_ctx, 1, &selected) && selected.is_local);
	if (local && strcmp(udb_ctx->authority_sid, me.id))
	{
		/* Bootstrap does not make a relay's root epoch this node's own epoch.
		 * Becoming standalone is a separate durable origin bind. */
		udb_ready = 0;
		udb_native_local_bind(udb_ctx);
		return;
	}
	(void)id;
	udb_ready = 1;
	udb_last_successful_sync = time(NULL);
	udb_sync_status = UDB_SYNC_OK;
	udb_degraded_since = 0;
	udb_bootstrap_peer = NULL;
	udb_offer_reconciliation_to_downstreams();
}

EVENT(udb_native_receive_tick_event)
{
	if (!udb_ctx || !udb_ctx->storage)
		return;
	UdbStorageEngine *engine = udb_ctx->storage;
	/* Completion work is bounded even when a large PUT burst has accumulated. */
	for (unsigned int i = 0; i < 1024; i++)
	{
		UdbStorageJob *job = NULL;
		if (udb_storage_engine_poll(engine, &job) != UDB_STORAGE_OK)
			break;
		if (job->kind >= UDB_STORAGE_STAGE_BEGIN)
			udb_native_receive_complete(job);
		else
			udb_native_mutation_complete(job);
		if (udb_storage_engine_job_free(engine, &job) != UDB_STORAGE_OK)
			abort();
	}
	udb_storage_mutex_lock(&engine->mutex);
	int failed = engine->failed || engine->stopping || !engine->admission;
	udb_storage_mutex_unlock(&engine->mutex);
	if (failed)
	{
		udb_ready = 0;
		udb_sync_status = UDB_SYNC_DEGRADED;
		udb_native_mutation_abort(0);
		if (udb_reconcile.active)
			udb_reconcile_abort(udb_ctx, "native writer unavailable", 1);
	}
	else if (udb_ctx->selection_kind == 1 && !udb_reconcile.active && !udb_ready && !udb_native_deliveries_present())
		udb_native_local_bind(udb_ctx);
	udb_native_receive_replay_pump();
	for (UdbNativeReceiveRound *round = udb_native_receive_rounds, *next; round; round = next)
	{
		next = round->next;
		if (!round->abandoned || round->abort_job)
			continue;
		udb_storage_mutex_lock(&engine->mutex);
		round->module_revision = engine->module_revision;
		round->authority_revision = engine->authority_revision;
		int unavailable = engine->failed || engine->stopping || !engine->admission;
		udb_storage_mutex_unlock(&engine->mutex);
		if (unavailable)
			udb_native_receive_drop(round); /* recovery validates and discards pending roots */
		else
			udb_native_receive_submit(round, UDB_STORAGE_STAGE_ABORT, NULL, 0, &round->abort_job);
	}
}
