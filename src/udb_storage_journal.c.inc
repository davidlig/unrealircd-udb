/* Global durable root-set journal, two fixed 4 MiB checkpoint slots and recovery.
 * data flush -> WAL flush -> publication; checkpoint flush/readback -> durable
 * WAL floor -> covered retirement. No rename/unlink or old-format parser.
 * A WAL floor prevents silent fallback to an older slot after covered history
 * has been retired. Full-sized malformed WAL pages always fail closed. */

#define UDB_STORAGE_WAL_OBJECT UINT64_C(0x55444257414c3034)
#define UDB_STORAGE_META_OBJECT UINT64_C(0x5544424d45543034)

typedef struct UdbStorageJournalScratch
{
	unsigned char page[65536];
	unsigned char payload[UDB_STORAGE_STATE_SIZE + 64];
	UdbStorageState state[2];
} UdbStorageJournalScratch;

static int udb_storage_journal_valid(const UdbStorageJournal *journal)
{
	if (!journal || !journal->cache || !journal->wal || journal->wal->cache != journal->cache || !journal->wal->opened)
		return 0;
	for (unsigned int i = 0; i < 2; i++)
		if (!journal->metadata[i] || !journal->metadata[i]->opened || journal->metadata[i]->cache != journal->cache)
			return 0;
	for (unsigned int i = 0; i < 6; i++)
		if (!journal->data[i] || !journal->data[i]->opened || journal->data[i]->cache != journal->cache)
			return 0;
	return 1;
}

static UdbStorageResult udb_storage_journal_state_hash(const UdbStorageState *state, UdbStorageJournalScratch *scratch,
													   unsigned char hash[32])
{
	UdbStorageResult result = udb_storage_state_encode(state, scratch->payload);
	if (result != UDB_STORAGE_OK)
		return result;
	return SHA256(scratch->payload, UDB_STORAGE_STATE_SIZE, hash) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
}

static UdbStorageResult udb_storage_metadata_read(UdbStorageJournal *journal, unsigned int slot,
												  UdbStorageJournalScratch *scratch, UdbStorageState *state,
												  unsigned char last_hash[32])
{
	UdbStoragePage page;
	UdbStorageState candidate;
	unsigned char first_hash[32], anchor[32];
	uint64_t size, generation = 0;
	UdbStorageResult result = udb_storage_file_size(journal->metadata[slot], &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size != UDB_STORAGE_METADATA_SIZE)
		return UDB_STORAGE_CORRUPT;
	for (uint64_t ordinal = 0; ordinal < UDB_STORAGE_METADATA_SIZE / UDB_STORAGE_PAGE_SIZE; ordinal++)
	{
		result = udb_storage_file_read(journal->metadata[slot], ordinal * UDB_STORAGE_PAGE_SIZE, scratch->page,
									   sizeof(scratch->page));
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_page_decode(scratch->page, sizeof(scratch->page), &page);
		if (result != UDB_STORAGE_OK)
			return result;
		if (page.kind != UDB_STORAGE_PAGE_METADATA || page.object != UDB_STORAGE_META_OBJECT + slot ||
			page.ordinal != ordinal || (ordinal && page.generation != generation))
			return UDB_STORAGE_CORRUPT;
		if (!ordinal)
		{
			if (page.payload.length != UDB_STORAGE_STATE_SIZE + 32)
				return UDB_STORAGE_CORRUPT;
			result = udb_storage_state_decode(page.payload.data, &candidate);
			if (result != UDB_STORAGE_OK)
				return result;
			generation = page.generation;
			if (candidate.generation != generation)
				return UDB_STORAGE_CORRUPT;
			memcpy(anchor, page.payload.data + UDB_STORAGE_STATE_SIZE, 32);
			memcpy(first_hash, scratch->page + 48, 32);
		}
		else if (ordinal == UDB_STORAGE_METADATA_SIZE / UDB_STORAGE_PAGE_SIZE - 1)
		{
			if (page.payload.length != 40 || memcmp(first_hash, page.payload.data, 32) ||
				udb_storage_get_u64(page.payload.data + 32) != UDB_STORAGE_METADATA_SIZE)
				return UDB_STORAGE_CORRUPT;
		}
		else if (page.payload.length)
			return UDB_STORAGE_CORRUPT;
	}
	*state = candidate;
	memcpy(last_hash, anchor, 32);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_metadata_write(UdbStorageJournal *journal, unsigned int slot,
												   UdbStorageJournalScratch *scratch)
{
	UdbStoragePage page;
	UdbStorageResult result;
	unsigned char first_hash[32], anchor[32], expected[32], observed[32];
	memset(&page, 0, sizeof(page));
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.object = UDB_STORAGE_META_OBJECT + slot;
	page.generation = journal->state.generation;
	page.payload.data = scratch->payload;
	result = udb_storage_state_encode(&journal->state, scratch->payload);
	if (result != UDB_STORAGE_OK)
		return result;
	memcpy(scratch->payload + UDB_STORAGE_STATE_SIZE, journal->last_hash, 32);
	page.payload.length = UDB_STORAGE_STATE_SIZE + 32;
	for (uint64_t ordinal = 0; ordinal < UDB_STORAGE_METADATA_SIZE / UDB_STORAGE_PAGE_SIZE; ordinal++)
	{
		page.ordinal = ordinal;
		if (ordinal == UDB_STORAGE_METADATA_SIZE / UDB_STORAGE_PAGE_SIZE - 1)
		{
			memcpy(scratch->payload, first_hash, 32);
			udb_storage_put_u64(scratch->payload + 32, UDB_STORAGE_METADATA_SIZE);
			page.payload.length = 40;
		}
		else if (ordinal)
			page.payload.length = 0;
		result = udb_storage_page_encode(&page, scratch->page, sizeof(scratch->page));
		if (result != UDB_STORAGE_OK)
			return result;
		if (!ordinal)
			memcpy(first_hash, scratch->page + 48, 32);
		result = udb_storage_file_write(journal->metadata[slot], ordinal * UDB_STORAGE_PAGE_SIZE, scratch->page,
										sizeof(scratch->page));
		if (result != UDB_STORAGE_OK)
			return result;
	}
	result = udb_storage_file_flush(journal->metadata[slot]);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_metadata_read(journal, slot, scratch, scratch->state, anchor);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_journal_state_hash(&journal->state, scratch, expected);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_journal_state_hash(scratch->state, scratch, observed);
	if (result != UDB_STORAGE_OK)
		return result;
	return memcmp(expected, observed, 32) || memcmp(anchor, journal->last_hash, 32) ? UDB_STORAGE_CORRUPT
																					: UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_journal_floor_write(UdbStorageJournal *journal, UdbStorageJournalScratch *scratch)
{
	UdbStoragePage page;
	unsigned char state_hash[32];
	UdbStorageResult result = udb_storage_journal_state_hash(&journal->state, scratch, state_hash);
	if (result != UDB_STORAGE_OK)
		return result;
	memset(scratch->payload, 0, 96);
	memcpy(scratch->payload, "UDBWAL4", 7);
	memcpy(scratch->payload + 8, journal->state.store_identity, 16);
	udb_storage_put_u64(scratch->payload + 24, journal->state.generation);
	memcpy(scratch->payload + 32, state_hash, 32);
	memcpy(scratch->payload + 64, journal->last_hash, 32);
	memset(&page, 0, sizeof(page));
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.object = UDB_STORAGE_WAL_OBJECT;
	page.generation = journal->state.generation;
	page.payload.data = scratch->payload;
	page.payload.length = 96;
	result = udb_storage_page_encode(&page, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(journal->wal, 0, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_flush(journal->wal);
	return result;
}

static UdbStorageResult udb_storage_journal_file_header(UdbStorageJournal *journal, unsigned int index,
														UdbStorageJournalScratch *scratch, int create)
{
	UdbStoragePage page;
	uint64_t size;
	UdbStorageResult result;
	if (create)
	{
		memset(&page, 0, sizeof(page));
		memset(scratch->payload, 0, 32);
		memcpy(scratch->payload, "UDBFILE4", 8);
		memcpy(scratch->payload + 8, journal->state.store_identity, 16);
		scratch->payload[24] = udb_storage_blocks[index];
		page.kind = UDB_STORAGE_PAGE_METADATA;
		page.generation = 1;
		page.object = udb_storage_blocks[index];
		page.payload.data = scratch->payload;
		page.payload.length = 32;
		result = udb_storage_page_encode(&page, scratch->page, sizeof(scratch->page));
		if (result == UDB_STORAGE_OK)
			result = udb_storage_file_write(journal->data[index], 0, scratch->page, sizeof(scratch->page));
		if (result == UDB_STORAGE_OK)
			result = udb_storage_file_flush(journal->data[index]);
		return result;
	}
	result = udb_storage_file_size(journal->data[index], &size);
	if (result != UDB_STORAGE_OK)
		return result;
	/* Private abandoned writes may leave a partial final page; no root may
	 * reference it. Recovery aligns that unreferenced append tail separately. */
	if (size < UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	result = udb_storage_file_read(journal->data[index], 0, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_page_decode(scratch->page, sizeof(scratch->page), &page);
	if (result != UDB_STORAGE_OK)
		return result;
	if (page.kind != UDB_STORAGE_PAGE_METADATA || page.generation != 1 || page.ordinal ||
		page.object != udb_storage_blocks[index] || page.payload.length != 32 ||
		memcmp(page.payload.data, "UDBFILE4", 8) || memcmp(page.payload.data + 8, journal->state.store_identity, 16) ||
		page.payload.data[24] != udb_storage_blocks[index] || !udb_storage_zero(page.payload.data + 25, 7))
		return UDB_STORAGE_CORRUPT;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_journal_empty_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
															void *context)
{
	(void)partition;
	(void)summary;
	(void)context;
	return UDB_STORAGE_NOT_FOUND;
}

static UdbStorageResult udb_storage_journal_initialize(UdbStorageJournal *journal)
{
	UdbStorageJournalScratch *scratch;
	UdbStorageResult result;
	uint64_t size;
	if (!udb_storage_journal_valid(journal) || journal->failed)
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 0; i < 9; i++)
	{
		UdbStorageFile *file = i < 6 ? journal->data[i] : i == 6 ? journal->wal : journal->metadata[i - 7];
		result = udb_storage_file_size(file, &size);
		if (result != UDB_STORAGE_OK)
			return result;
		if (size)
			return UDB_STORAGE_CORRUPT;
	}
	scratch = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_STORAGE_LIMIT;
	memset(&journal->state, 0, sizeof(journal->state));
	memset(journal->last_hash, 0, 32);
	journal->state.generation = 1;
	journal->state.next_object = 8;
	journal->state.stages.object = 7;
	if (RAND_bytes(journal->state.store_identity, sizeof(journal->state.store_identity)) != 1)
	{
		result = UDB_STORAGE_ERROR;
		goto done;
	}
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageBlockRoot *root = journal->state.blocks + i;
		root->block = udb_storage_blocks[i];
		root->generation = 1;
		root->catalog.object = i + 1;
		result = udb_storage_logical_block(root->block, udb_storage_journal_empty_partition, NULL, &root->summary);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_journal_file_header(journal, i, scratch, 1);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	result = udb_storage_metadata_write(journal, 0, scratch);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_metadata_write(journal, 1, scratch);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_journal_floor_write(journal, scratch);
	if (result == UDB_STORAGE_OK)
	{
		journal->slot = 1;
		journal->end = UDB_STORAGE_PAGE_SIZE;
	}
done:
	if (result != UDB_STORAGE_OK)
		journal->failed = 1;
	udb_storage_free(scratch);
	return result;
}

static UdbStorageResult udb_storage_journal_recover(UdbStorageJournal *journal, UdbStorageRecordValidator validator,
													void *context)
{
	UdbStorageJournalScratch *scratch;
	UdbStorageResult result, slots[2];
	UdbStoragePage page;
	unsigned char anchors[2][32], floor_hash[32], floor_anchor[32], state_hash[32], previous[32];
	uint64_t floor, size, checkpoint, previous_generation = 0;
	int checkpoint_seen;
	if (!udb_storage_journal_valid(journal))
		return UDB_STORAGE_ERROR;
	scratch = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_STORAGE_LIMIT;
	for (unsigned int i = 0; i < 2; i++)
	{
		slots[i] = udb_storage_metadata_read(journal, i, scratch, scratch->state + i, anchors[i]);
		if (slots[i] != UDB_STORAGE_OK && slots[i] != UDB_STORAGE_CORRUPT)
		{
			result = slots[i];
			goto done;
		}
	}
	if (slots[0] != UDB_STORAGE_OK && slots[1] != UDB_STORAGE_OK)
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	if (slots[0] == UDB_STORAGE_OK && slots[1] == UDB_STORAGE_OK &&
		scratch->state[0].generation == scratch->state[1].generation)
	{
		unsigned char hashes[2][32];
		result = udb_storage_journal_state_hash(scratch->state, scratch, hashes[0]);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_journal_state_hash(scratch->state + 1, scratch, hashes[1]);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (memcmp(hashes[0], hashes[1], 32) || memcmp(anchors[0], anchors[1], 32))
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
	}
	journal->slot = slots[1] == UDB_STORAGE_OK &&
							(slots[0] != UDB_STORAGE_OK || scratch->state[1].generation >= scratch->state[0].generation)
						? 1
						: 0;
	journal->state = scratch->state[journal->slot];
	memcpy(journal->last_hash, anchors[journal->slot], 32);
	checkpoint = journal->state.generation;
	result = udb_storage_file_size(journal->wal, &size);
	if (result != UDB_STORAGE_OK)
		goto done;
	if (size < UDB_STORAGE_PAGE_SIZE)
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	result = udb_storage_file_read(journal->wal, 0, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_page_decode(scratch->page, sizeof(scratch->page), &page);
	if (result != UDB_STORAGE_OK)
		goto done;
	if (page.kind != UDB_STORAGE_PAGE_METADATA || page.object != UDB_STORAGE_WAL_OBJECT || page.ordinal ||
		page.payload.length != 96 || memcmp(page.payload.data, "UDBWAL4\0", 8) ||
		memcmp(page.payload.data + 8, journal->state.store_identity, 16))
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	floor = udb_storage_get_u64(page.payload.data + 24);
	memcpy(floor_hash, page.payload.data + 32, 32);
	memcpy(floor_anchor, page.payload.data + 64, 32);
	if (!floor || page.generation != floor || checkpoint < floor)
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	checkpoint_seen = checkpoint == floor;
	if (checkpoint_seen)
	{
		result = udb_storage_journal_state_hash(&journal->state, scratch, state_hash);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (memcmp(state_hash, floor_hash, 32) || memcmp(journal->last_hash, floor_anchor, 32))
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
	}
	journal->end = size - size % UDB_STORAGE_PAGE_SIZE;
	for (uint64_t offset = UDB_STORAGE_PAGE_SIZE; offset < journal->end; offset += UDB_STORAGE_PAGE_SIZE)
	{
		UdbStorageState *candidate = scratch->state;
		unsigned char hash[32], predecessor[32];
		result = udb_storage_file_read(journal->wal, offset, scratch->page, sizeof(scratch->page));
		if (result == UDB_STORAGE_OK)
			result = udb_storage_page_decode(scratch->page, sizeof(scratch->page), &page);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (page.kind != UDB_STORAGE_PAGE_METADATA || page.object != UDB_STORAGE_WAL_OBJECT ||
			page.ordinal != offset / UDB_STORAGE_PAGE_SIZE || page.payload.length != UDB_STORAGE_STATE_SIZE + 32)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		memcpy(predecessor, page.payload.data, 32);
		memcpy(hash, scratch->page + 48, 32);
		result = udb_storage_state_decode(page.payload.data + 32, candidate);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (page.generation != candidate->generation ||
			memcmp(candidate->store_identity, journal->state.store_identity, 16) ||
			(previous_generation &&
			 (candidate->generation != previous_generation + 1 || memcmp(predecessor, previous, 32))))
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		previous_generation = candidate->generation;
		memcpy(previous, hash, 32);
		if (candidate->generation == checkpoint)
		{
			if (memcmp(hash, anchors[journal->slot], 32))
			{
				result = UDB_STORAGE_CORRUPT;
				goto done;
			}
			checkpoint_seen = 1;
		}
		if (candidate->generation <= checkpoint)
			continue;
		if (journal->state.generation == UINT64_MAX || candidate->generation != journal->state.generation + 1 ||
			candidate->sequence < journal->state.sequence || candidate->next_object < journal->state.next_object ||
			memcmp(predecessor, journal->last_hash, 32))
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		journal->state = *candidate;
		memcpy(journal->last_hash, hash, 32);
	}
	if (!checkpoint_seen)
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	for (unsigned int i = 0; i < 6; i++)
	{
		result = udb_storage_journal_file_header(journal, i, scratch, 0);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_block_validate(journal->cache, journal->data[i], journal->state.blocks + i, validator,
												context);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_block_object_bound(journal->cache, journal->data[i], journal->state.blocks + i,
													journal->state.next_object);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	result = udb_storage_stages_validate(journal, validator, context);
	if (result != UDB_STORAGE_OK)
		goto done;
	/* Only after every reference/root is validated may incomplete private data
	 * append tails and an incomplete final uncommitted WAL tail be retired. */
	for (unsigned int i = 0; i < 6; i++)
	{
		result = udb_storage_file_size(journal->data[i], &size);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (size % UDB_STORAGE_PAGE_SIZE)
		{
			result = udb_storage_file_resize(journal->data[i], size - size % UDB_STORAGE_PAGE_SIZE);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_file_flush(journal->data[i]);
			if (result != UDB_STORAGE_OK)
				goto done;
		}
	}
	result = udb_storage_file_size(journal->wal, &size);
	if (result == UDB_STORAGE_OK && size != journal->end)
	{
		result = udb_storage_file_resize(journal->wal, journal->end);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_file_flush(journal->wal);
	}
	if (result == UDB_STORAGE_OK && journal->state.stages.count)
	{
		UdbStorageState *discarded = scratch->state;
		if (journal->state.generation == UINT64_MAX)
		{
			result = UDB_STORAGE_LIMIT;
			goto done;
		}
		*discarded = journal->state;
		discarded->generation++;
		memset(&discarded->stages, 0, sizeof(discarded->stages));
		discarded->stages.object = 7;
		/* Every pending reference was validated above. Its runtime owner is
		 * no longer alive, so retire it durably before reusing owner IDs. */
		journal->failed = 0;
		result = udb_storage_journal_commit(journal, discarded);
	}
done:
	journal->failed = result != UDB_STORAGE_OK;
	udb_storage_free(scratch);
	return result;
}

static UdbStorageResult udb_storage_journal_commit(UdbStorageJournal *journal, const UdbStorageState *candidate)
{
	UdbStorageJournalScratch *scratch;
	UdbStoragePage page;
	UdbStorageResult result;
	if (!udb_storage_journal_valid(journal) || journal->failed || !udb_storage_state_valid(candidate) ||
		journal->state.generation == UINT64_MAX || candidate->generation != journal->state.generation + 1 ||
		candidate->sequence < journal->state.sequence || candidate->next_object < journal->state.next_object ||
		memcmp(candidate->store_identity, journal->state.store_identity, 16) ||
		!udb_storage_io_range_valid(journal->end, UDB_STORAGE_PAGE_SIZE))
		return UDB_STORAGE_ERROR;
	scratch = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_STORAGE_LIMIT;
	for (unsigned int i = 0; i < 6; i++)
	{
		/* Flushing unchanged files is safe and prevents a caller's dirty-mask
		 * omission from making a WAL reference durable ahead of its pages. */
		result = udb_storage_file_flush(journal->data[i]);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	memcpy(scratch->payload, journal->last_hash, 32);
	result = udb_storage_state_encode(candidate, scratch->payload + 32);
	if (result != UDB_STORAGE_OK)
		goto done;
	memset(&page, 0, sizeof(page));
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.generation = candidate->generation;
	page.object = UDB_STORAGE_WAL_OBJECT;
	page.ordinal = journal->end / UDB_STORAGE_PAGE_SIZE;
	page.payload.data = scratch->payload;
	page.payload.length = UDB_STORAGE_STATE_SIZE + 32;
	result = udb_storage_page_encode(&page, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(journal->wal, journal->end, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_flush(journal->wal);
	if (result == UDB_STORAGE_OK)
	{
		journal->state = *candidate;
		memcpy(journal->last_hash, scratch->page + 48, 32);
		journal->end += UDB_STORAGE_PAGE_SIZE;
	}
done:
	if (result != UDB_STORAGE_OK)
		journal->failed = 1;
	udb_storage_free(scratch);
	return result;
}

static UdbStorageResult udb_storage_journal_checkpoint(UdbStorageJournal *journal)
{
	UdbStorageJournalScratch *scratch;
	UdbStorageResult result;
	unsigned int inactive;
	if (!udb_storage_journal_valid(journal) || journal->failed || !udb_storage_state_valid(&journal->state))
		return UDB_STORAGE_ERROR;
	scratch = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_STORAGE_LIMIT;
	inactive = journal->slot ^ 1;
	result = udb_storage_metadata_write(journal, inactive, scratch);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_journal_floor_write(journal, scratch);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_resize(journal->wal, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_flush(journal->wal);
	if (result == UDB_STORAGE_OK)
	{
		journal->slot = inactive;
		journal->end = UDB_STORAGE_PAGE_SIZE;
	}
	else
		journal->failed = 1;
	udb_storage_free(scratch);
	return result;
}
