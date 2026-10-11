/* Foreground snapshot senders pin disk generations, not resident record lists.
 * Each peer/round is bound to its negotiated instance and expires finitely. */

#define UDB_NATIVE_SEND_ROWS 1024
#define UDB_NATIVE_SEND_BYTES ((size_t)1024 * 1024)

typedef struct UdbNativeSendRound
{
	struct UdbNativeSendRound *next;
	UdbStorageEngine *engine;
	UdbStorageGeneration *generation;
	UdbStorageBlockIterator *iterator;
	char peer_sid[4];
	char peer_epoch[UDB_OCL_EPOCH_LEN + 1];
	char txid[UDB_NUM_BLOCKS][UDB_TXID_MAX + 1];
	unsigned long round;
	uint64_t started;
	uint64_t activity;
	uint64_t emitted;
	unsigned int requested;
	unsigned int sent;
	unsigned int acknowledged;
	int active;
} UdbNativeSendRound;

typedef struct UdbNativeSendScratch
{
	char path[UDB_STORAGE_PATH_MAX * 3 + 1];
	char value[4 * ((UDB_STORAGE_VALUE_MAX + 2) / 3) + 3];
} UdbNativeSendScratch;

static UdbNativeSendRound *udb_native_send_rounds;
static unsigned long udb_native_send_sequence;

static void udb_native_digest_hex(const unsigned char digest[32], char out[65])
{
	static const char hex[] = "0123456789abcdef";
	for (unsigned int i = 0; i < 32; i++)
	{
		out[2 * i] = hex[digest[i] >> 4];
		out[2 * i + 1] = hex[digest[i] & 15];
	}
	out[64] = 0;
}

static void udb_native_send_drop(UdbNativeSendRound **link)
{
	UdbNativeSendRound *round = *link;
	if (round->iterator && udb_storage_block_iterator_close(&round->iterator) != UDB_STORAGE_OK)
		abort();
	if (udb_storage_engine_release(round->engine, &round->generation) != UDB_STORAGE_OK)
		abort();
	*link = round->next;
	udb_storage_free(round);
}

static int udb_native_send_peer(UdbNativeSendRound *round, Client *server)
{
	UdbHelloPeer *peer = server ? udb_hello_peer(server, 0) : NULL;
	return server && !IsDead(server) && !IsDeadSocket(server) && MyConnect(server) && udb_ready &&
		   udb_peer_authorizes_us(server) && !strcmp(round->peer_sid, server->id) && peer && peer->remote_epoch_known &&
		   !strcmp(round->peer_epoch, peer->remote_epoch);
}

static UdbNativeSendRound *udb_native_send_find(Client *server, unsigned long round)
{
	for (UdbNativeSendRound *entry = udb_native_send_rounds; entry; entry = entry->next)
		if (entry->round == round && udb_native_send_peer(entry, server))
			return entry;
	return NULL;
}

static void udb_native_sync_cleanup(Client *server)
{
	UdbNativeSendRound **link = &udb_native_send_rounds;
	while (*link)
		if (!server || !strcmp((*link)->peer_sid, server->id))
			udb_native_send_drop(link);
		else
			link = &(*link)->next;
}

static int udb_native_sync_offer_round(Client *server, unsigned long requested, int manifest)
{
	UdbNativeSendRound *round;
	UdbStorageGeneration *generation = NULL;
	UdbHelloPeer *peer = server ? udb_hello_peer(server, 0) : NULL;
	if (!udb_ctx || !udb_ctx->storage || !udb_ready || !udb_peer_authorizes_us(server) || !peer ||
		!peer->remote_epoch_known || !udb_storage_sid_valid(server->id) ||
		(!requested && udb_native_send_sequence == ULONG_MAX))
		return 0;
	if (udb_storage_engine_pin(udb_ctx->storage, &generation) != UDB_STORAGE_OK)
		return 0;
	if (!udb_storage_sid_valid(generation->state.source_sid))
	{
		udb_storage_engine_release(udb_ctx->storage, &generation);
		return 0;
	}
	round = udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*round));
	if (!round)
	{
		udb_storage_engine_release(udb_ctx->storage, &generation);
		return 0;
	}
	round->engine = udb_ctx->storage;
	round->generation = generation;
	if (!udb_native_send_sequence)
	{
		time_t seed = time(NULL);
		if (seed > 0 && (uint64_t)seed < ULONG_MAX)
			udb_native_send_sequence = (unsigned long)seed;
	}
	round->round = requested ? requested : ++udb_native_send_sequence;
	round->started = round->activity = udb_storage_monotonic_ms();
	if (round->started == UINT64_MAX)
	{
		udb_storage_engine_release(round->engine, &round->generation);
		udb_storage_free(round);
		return 0;
	}
	round->active = -1;
	memcpy(round->peer_sid, server->id, 4);
	memcpy(round->peer_epoch, peer->remote_epoch, sizeof(round->peer_epoch));
	udb_native_sync_cleanup(server);
	round->next = udb_native_send_rounds;
	udb_native_send_rounds = round;
	char epoch[17];
	snprintf(epoch, sizeof(epoch), "%016" PRIx64, generation->state.source_epoch);
	for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		const UdbStorageBlockRoot *root = generation->state.blocks + i;
		char digest[65];
		udb_native_digest_hex(root->summary.digest, digest);
		if (manifest && !udb_send_db_to_one(server, ":%s DB %s MANIFEST ACK %lu %c %" PRIu64 " %s %" PRIu64, me.id,
											server->id, round->round, root->block, root->summary.count, digest,
											generation->state.source_sequence))
		{
			udb_native_sync_cleanup(server);
			return 0;
		}
		if (!udb_send_db_to_one(server, ":%s DB %s INF %lu %c %s %" PRIu64 " 0 %" PRIu64 " %s %s", me.id, server->id,
								round->round, root->block, digest, root->summary.count,
								generation->state.source_sequence, generation->state.source_sid, epoch))
		{
			udb_native_sync_cleanup(server);
			return 0;
		}
	}
	return 1;
}

static int udb_native_sync_offer(Client *server)
{
	return udb_native_sync_offer_round(server, 0, 0);
}

static int udb_native_sync_send(Client *server, char block, unsigned long round_id)
{
	UdbNativeSendRound *round = udb_native_send_find(server, round_id);
	int index = udb_storage_stage_block_index((unsigned char)block);
	if (!round || index < 0 || (round->requested & (1U << index)))
		return 0;
	round->requested |= 1U << index;
	round->activity = udb_storage_monotonic_ms();
	return 1;
}

static int udb_native_sync_ack(Client *server, unsigned long round_id, char block, const char *txid, const char *digest,
							   uint64_t watermark)
{
	UdbNativeSendRound *round = udb_native_send_find(server, round_id);
	int index = udb_storage_stage_block_index((unsigned char)block);
	char expected[65];
	if (!round || index < 0 || !txid || !digest || !(round->sent & (1U << index)) ||
		(round->acknowledged & (1U << index)) || strcmp(round->txid[index], txid) ||
		watermark != round->generation->state.source_sequence)
		return 0;
	udb_native_digest_hex(round->generation->state.blocks[index].summary.digest, expected);
	if (strcmp(expected, digest))
		return 0;
	round->acknowledged |= 1U << index;
	round->activity = udb_storage_monotonic_ms();
	return 1;
}

/* A bounded turn never materializes a block or fills an unbounded SendQ. The
 * iterator owns the last returned row until the next call; no row copy list. */
static int udb_native_send_step(UdbNativeSendRound *round, Client *server, UdbNativeSendScratch *scratch,
								unsigned int *rows, size_t *bytes, unsigned int row_ceiling, size_t byte_ceiling)
{
	UdbStorageResult result;
	UdbStorageRecord record;
	if (round->active < 0)
	{
		/* END only queues the receiver's durable rebuild. Keep its single
		 * writer free of the next block until the validated ACK grants credit. */
		if (round->sent & ~round->acknowledged)
			return 1;
		for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
			if ((round->requested & ~round->sent) & (1U << i))
			{
				round->active = (int)i;
				break;
			}
		if (round->active < 0)
			return 1;
		int index = round->active;
		const UdbStorageBlockRoot *root = round->generation->state.blocks + index;
		if (udb_storage_block_iterator_create(round->engine->cache, round->engine->files + index, root, NULL,
											  &round->iterator) != UDB_STORAGE_OK)
			return 0;
		char digest[65];
		udb_native_digest_hex(root->summary.digest, digest);
		snprintf(round->txid[index], sizeof(round->txid[index]), "%lx-%c", round->round, root->block);
		if (!udb_send_db_to_one(server, ":%s DB %s BEGIN %lu %c %s %s %" PRIu64, me.id, server->id, round->round,
								root->block, round->txid[index], digest, round->generation->state.source_sequence))
			return 0;
		round->emitted = 0;
	}
	while (*rows < row_ceiling && *bytes <= byte_ceiling && byte_ceiling - *bytes >= UDB_S2S_LINE_MAX)
	{
		if (!udb_native_send_peer(round, server))
			return 0;
		size_t queued = DBufLength(&server->local->sendQ);
		size_t maximum = get_sendq(server);
		/* Reserve one maximum-sized row before advancing the iterator. */
		if (queued >= maximum || maximum - queued < UDB_S2S_LINE_MAX)
			return 1;
		result = udb_storage_block_iterator_next(round->iterator, &record);
		const UdbStorageBlockRoot *root = round->generation->state.blocks + round->active;
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			char digest[65];
			udb_native_digest_hex(root->summary.digest, digest);
			if (round->emitted != root->summary.count ||
				udb_storage_block_iterator_close(&round->iterator) != UDB_STORAGE_OK ||
				!udb_send_db_to_one(server, ":%s DB %s END %lu %c %s %s %" PRIu64, me.id, server->id, round->round,
									root->block, round->txid[round->active], digest,
									round->generation->state.source_sequence))
				return 0;
			round->sent |= 1U << round->active;
			round->active = -1;
			round->activity = udb_storage_monotonic_ms();
			return 1;
		}
		if (result != UDB_STORAGE_OK ||
			udb_native_key_path(&record.row, scratch->path, sizeof(scratch->path)) != UDB_STORAGE_OK ||
			udb_native_value_encode(&record.row, scratch->value, sizeof(scratch->value)) != UDB_STORAGE_OK)
			return 0;
		size_t size = strlen(scratch->path) + strlen(scratch->value) + UDB_S2S_OVERHEAD_MAX;
		if (size > UDB_S2S_LINE_MAX || round->emitted == UINT64_MAX ||
			!udb_send_db_to_one(server, ":%s DB %s PUT %lu %c %s %s :%s", me.id, server->id, round->round, root->block,
								round->txid[round->active], scratch->path, scratch->value))
			return 0;
		round->emitted++;
		(*rows)++;
		*bytes += size;
		round->activity = udb_storage_monotonic_ms();
	}
	return 1;
}

static void udb_native_sync_pump(void)
{
	if (!udb_ctx || !udb_ctx->storage || !udb_native_send_rounds)
		return;
	uint64_t now = udb_storage_monotonic_ms();
	uint64_t inactivity = (uint64_t)(udb_cfg ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT) * 1000;
	uint64_t absolute = (uint64_t)(udb_cfg ? udb_cfg->sync_absolute_timeout : UDB_SYNC_ABSOLUTE_TIMEOUT) * 1000;
	UdbNativeSendRound **link = &udb_native_send_rounds;
	size_t remaining = 0;
	/* Expiry needs no allocation; memory pressure must not retain stale pins. */
	while (*link)
	{
		UdbNativeSendRound *round = *link;
		Client *server = udb_ocl_find_server(round->peer_sid);
		if (!udb_native_send_peer(round, server) || now == UINT64_MAX || now < round->started ||
			now - round->started >= absolute || now < round->activity || now - round->activity >= inactivity)
			udb_native_send_drop(link);
		else
		{
			remaining++;
			link = &round->next;
		}
	}
	if (!remaining)
		return;
	UdbNativeSendScratch *scratch =
		udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return;
	unsigned int rows = 0;
	size_t bytes = 0;
	/* Rotate served entries to the tail. At most the initial inventory is
	 * visited, so a slow/large first peer cannot starve another round. */
	UdbNativeSendRound **tail = &udb_native_send_rounds;
	while (*tail)
		tail = &(*tail)->next;
	while (remaining-- && rows < UDB_NATIVE_SEND_ROWS && UDB_NATIVE_SEND_BYTES - bytes >= UDB_S2S_LINE_MAX)
	{
		UdbNativeSendRound *round = udb_native_send_rounds;
		Client *server = udb_ocl_find_server(round->peer_sid);
		unsigned int row_ceiling = rows + 128;
		size_t byte_ceiling = bytes + 128 * 1024;
		if (row_ceiling > UDB_NATIVE_SEND_ROWS)
			row_ceiling = UDB_NATIVE_SEND_ROWS;
		if (byte_ceiling > UDB_NATIVE_SEND_BYTES)
			byte_ceiling = UDB_NATIVE_SEND_BYTES;
		if (!udb_native_send_step(round, server, scratch, &rows, &bytes, row_ceiling, byte_ceiling))
		{
			if (server && round->active >= 0)
				udb_send_db_to_one(server, ":%s DB %s ERR PUT %d %lu %c", me.id, server->id, UDB_ERR_FATAL,
								   round->round, round->generation->state.blocks[round->active].block);
			if (!round->next)
				tail = &udb_native_send_rounds;
			udb_native_send_drop(&udb_native_send_rounds);
		}
		else if (round->next)
		{
			udb_native_send_rounds = round->next;
			round->next = NULL;
			*tail = round;
			tail = &round->next;
		}
	}
	udb_storage_free(scratch);
}

EVENT(udb_native_sync_tick_event)
{
	udb_native_sync_pump();
}
