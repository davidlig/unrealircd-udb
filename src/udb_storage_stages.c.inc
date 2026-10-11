/* Durable pending-root catalog in S, distinct from S's logical block catalog.
 * Per-block ACK may follow a pending-root WAL commit, but no imported active
 * block is exposed until all six roots atomically promote. Runtime owner IDs
 * are discarded durably on recovery, never interpreted as live peer authority. */

static void udb_storage_stage_key(uint64_t owner, unsigned int block, unsigned int part, unsigned char key[64])
{
	memset(key, 0, 64);
	for (unsigned int i = 0; i < 8; i++)
		key[i] = (unsigned char)(owner >> (56 - 8 * i));
	key[8] = (unsigned char)block;
	key[9] = (unsigned char)part;
}

static int udb_storage_stage_head_valid(const UdbStorageStageHead *head)
{
	return head && head->owner && head->round && head->mask <= 63 && udb_storage_sid_valid(head->source_sid);
}

static UdbStorageResult udb_storage_stage_head_encode(const UdbStorageStageHead *head, unsigned char value[128])
{
	if (!value || !udb_storage_stage_head_valid(head) || !head->mask ||
		udb_storage_overlaps(value, 128, head, sizeof(*head)))
		return UDB_STORAGE_ERROR;
	memset(value, 0, 128);
	memcpy(value, "UDBSTG4", 7);
	udb_storage_put_u64(value + 8, head->owner);
	memcpy(value + 16, head->source_sid, 4);
	udb_storage_put_u32(value + 20, head->mask);
	udb_storage_put_u64(value + 24, head->source_epoch);
	udb_storage_put_u64(value + 32, head->source_sequence);
	udb_storage_put_u64(value + 40, head->round);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_head_decode(const unsigned char value[128], UdbStorageStageHead *head)
{
	UdbStorageStageHead candidate;
	if (!value || !head)
		return UDB_STORAGE_ERROR;
	if (memcmp(value, "UDBSTG4\0", 8) || !udb_storage_zero(value + 48, 80))
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.owner = udb_storage_get_u64(value + 8);
	memcpy(candidate.source_sid, value + 16, 4);
	candidate.mask = udb_storage_get_u32(value + 20);
	candidate.source_epoch = udb_storage_get_u64(value + 24);
	candidate.source_sequence = udb_storage_get_u64(value + 32);
	candidate.round = udb_storage_get_u64(value + 40);
	if (!udb_storage_stage_head_valid(&candidate) || !candidate.mask)
		return UDB_STORAGE_CORRUPT;
	*head = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_head_get(UdbStorageEngine *engine, const UdbStorageState *state,
												   uint64_t owner, UdbStorageStageHead *head)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	udb_storage_stage_key(owner, 0, 0, key);
	result = udb_storage_catalog_get(engine->cache, engine->files + 3, &state->stages, key, value);
	if (result == UDB_STORAGE_OK)
	{
		result = udb_storage_stage_head_decode(value, head);
		if (result == UDB_STORAGE_OK && head->owner != owner)
			return UDB_STORAGE_CORRUPT;
	}
	return result;
}

static UdbStorageResult udb_storage_stage_root_get(UdbStorageEngine *engine, const UdbStorageState *state,
												   uint64_t owner, unsigned int block, UdbStorageBlockRoot *root)
{
	unsigned char key[64], bytes[UDB_STORAGE_BLOCK_ROOT_SIZE];
	UdbStorageResult result;
	if (block >= 6)
		return UDB_STORAGE_ERROR;
	for (unsigned int part = 0; part < 3; part++)
	{
		udb_storage_stage_key(owner, block + 1, part, key);
		result = udb_storage_catalog_get(engine->cache, engine->files + 3, &state->stages, key, bytes + part * 128);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_CORRUPT : result;
	}
	result = udb_storage_block_root_decode(bytes, root);
	if (result != UDB_STORAGE_OK)
		return result;
	return root->block == udb_storage_blocks[block] && root->catalog.object == block + 1 &&
				   root->generation <= state->generation && root->watermark <= state->sequence
			   ? UDB_STORAGE_OK
			   : UDB_STORAGE_CORRUPT;
}

static UdbStorageResult udb_storage_stage_save(UdbStorageEngine *engine, const UdbStorageState *base,
											   const UdbStorageStageHead *head, const UdbStorageBlockRoot *root,
											   UdbStorageState *candidate)
{
	UdbStorageState state;
	UdbStorageStageHead stored;
	UdbStorageResult result;
	unsigned char key[64], bytes[UDB_STORAGE_BLOCK_ROOT_SIZE], value[128];
	unsigned int block;
	if (!engine || !candidate || !udb_storage_state_valid(base) || !udb_storage_stage_head_valid(head) || head->mask ||
		!udb_storage_block_root_valid(root) || root->generation > base->generation || root->watermark > base->sequence)
		return UDB_STORAGE_ERROR;
	for (block = 0; block < 6 && udb_storage_blocks[block] != root->block; block++)
		;
	if (block == 6 || root->catalog.object != block + 1)
		return UDB_STORAGE_ERROR;
	result = udb_storage_stage_head_get(engine, base, head->owner, &stored);
	if (result == UDB_STORAGE_NOT_FOUND)
		stored = *head;
	else if (result != UDB_STORAGE_OK)
		return result;
	else if (stored.round != head->round || stored.source_epoch != head->source_epoch ||
			 stored.source_sequence != head->source_sequence || memcmp(stored.source_sid, head->source_sid, 4))
		return UDB_STORAGE_REJECTED;
	if (stored.mask & (1U << block))
		return UDB_STORAGE_REJECTED;
	state = *base;
	result = udb_storage_block_root_encode(root, bytes);
	if (result != UDB_STORAGE_OK)
		return result;
	for (unsigned int part = 0; part < 3; part++)
	{
		UdbStorageCatalogRoot changed;
		udb_storage_stage_key(head->owner, block + 1, part, key);
		result = udb_storage_catalog_change(engine->cache, engine->files + 3, &state.stages, state.generation, key,
											bytes + 128 * part, &changed);
		if (result != UDB_STORAGE_OK)
			return result;
		state.stages = changed;
	}
	stored.mask |= 1U << block;
	result = udb_storage_stage_head_encode(&stored, value);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_stage_key(head->owner, 0, 0, key);
	result = udb_storage_catalog_change(engine->cache, engine->files + 3, &state.stages, state.generation, key, value,
										&state.stages);
	if (result == UDB_STORAGE_OK)
		*candidate = state;
	return result;
}

static UdbStorageResult udb_storage_stage_drop(UdbStorageEngine *engine, const UdbStorageState *base, uint64_t owner,
											   UdbStorageState *candidate)
{
	UdbStorageState state;
	UdbStorageStageHead head;
	UdbStorageResult result;
	unsigned char key[64];
	if (!engine || !candidate || !owner || !udb_storage_state_valid(base))
		return UDB_STORAGE_ERROR;
	result = udb_storage_stage_head_get(engine, base, owner, &head);
	if (result != UDB_STORAGE_OK)
		return result;
	state = *base;
	for (unsigned int block = 0; block < 6; block++)
	{
		if (!(head.mask & (1U << block)))
			continue;
		for (unsigned int part = 0; part < 3; part++)
		{
			UdbStorageCatalogRoot changed;
			udb_storage_stage_key(owner, block + 1, part, key);
			result = udb_storage_catalog_change(engine->cache, engine->files + 3, &state.stages, state.generation, key,
												NULL, &changed);
			if (result != UDB_STORAGE_OK)
				return result;
			state.stages = changed;
		}
	}
	udb_storage_stage_key(owner, 0, 0, key);
	result = udb_storage_catalog_change(engine->cache, engine->files + 3, &state.stages, state.generation, key, NULL,
										&state.stages);
	if (result == UDB_STORAGE_OK)
		*candidate = state;
	return result;
}

static UdbStorageResult udb_storage_stage_promote(UdbStorageEngine *engine, const UdbStorageState *base, uint64_t owner,
												  UdbStorageState *candidate)
{
	UdbStorageStageHead head;
	UdbStorageState state;
	UdbStorageResult result;
	if (!engine || !candidate || !udb_storage_state_valid(base))
		return UDB_STORAGE_ERROR;
	result = udb_storage_stage_head_get(engine, base, owner, &head);
	if (result != UDB_STORAGE_OK)
		return result;
	if (head.mask != 63)
		return UDB_STORAGE_REJECTED;
	result = udb_storage_stage_drop(engine, base, owner, &state);
	if (result != UDB_STORAGE_OK)
		return result;
	for (unsigned int block = 0; block < 6; block++)
	{
		result = udb_storage_stage_root_get(engine, base, owner, block, state.blocks + block);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	memcpy(state.source_sid, head.source_sid, 4);
	state.source_epoch = head.source_epoch;
	state.source_sequence = head.source_sequence;
	*candidate = state;
	return UDB_STORAGE_OK;
}

typedef struct UdbStorageStageValidation
{
	UdbStorageJournal *journal;
	UdbStorageRecordValidator validator;
	void *context;
	UdbStorageStageHead head;
	unsigned int mask;
	unsigned int block;
	unsigned int part;
	unsigned char bytes[UDB_STORAGE_BLOCK_ROOT_SIZE];
} UdbStorageStageValidation;

static UdbStorageResult udb_storage_stages_record_validate(const unsigned char *key, const unsigned char *value,
														   void *opaque)
{
	UdbStorageStageValidation *validation = opaque;
	UdbStorageJournal *journal = validation->journal;
	uint64_t owner = 0;
	unsigned int block = key[8], part = key[9];
	unsigned char canonical[64];
	UdbStorageResult result;
	for (unsigned int i = 0; i < 8; i++)
		owner = (owner << 8) | key[i];
	udb_storage_stage_key(owner, block, part, canonical);
	if (!owner || memcmp(canonical, key, 64) || block > 6 || part > 2)
		return UDB_STORAGE_CORRUPT;
	if (!block)
	{
		if (part || validation->part || (validation->head.owner && validation->mask != validation->head.mask))
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_stage_head_decode(value, &validation->head);
		if (result != UDB_STORAGE_OK)
			return result;
		if (validation->head.owner != owner)
			return UDB_STORAGE_CORRUPT;
		validation->mask = 0;
		return UDB_STORAGE_OK;
	}
	if (owner != validation->head.owner || !(validation->head.mask & (1U << (block - 1))) ||
		(validation->mask & (1U << (block - 1))) || part != validation->part || (part && validation->block != block))
		return UDB_STORAGE_CORRUPT;
	validation->block = block;
	memcpy(validation->bytes + 128 * part, value, 128);
	if (++validation->part == 3)
	{
		UdbStorageBlockRoot root;
		result = udb_storage_block_root_decode(validation->bytes, &root);
		if (result != UDB_STORAGE_OK)
			return result;
		if (root.block != udb_storage_blocks[block - 1] || root.catalog.object != block ||
			root.generation > journal->state.generation || root.watermark > journal->state.sequence)
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_block_validate(journal->cache, journal->data[block - 1], &root, validation->validator,
											validation->context);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_block_object_bound(journal->cache, journal->data[block - 1], &root,
													journal->state.next_object);
		if (result != UDB_STORAGE_OK)
			return result;
		validation->mask |= 1U << (block - 1);
		validation->part = 0;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stages_validate(UdbStorageJournal *journal, UdbStorageRecordValidator validator,
													void *context)
{
	UdbStorageStageValidation *validation;
	UdbStorageResult result;
	if (!journal || !udb_storage_state_valid(&journal->state))
		return UDB_STORAGE_ERROR;
	validation = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*validation));
	if (!validation)
		return UDB_STORAGE_LIMIT;
	validation->journal = journal;
	validation->validator = validator;
	validation->context = context;
	result = udb_storage_catalog_validate(journal->data[3], &journal->state.stages, udb_storage_stages_record_validate,
										  validation);
	if (result == UDB_STORAGE_OK && (validation->part || validation->mask != validation->head.mask))
		result = UDB_STORAGE_CORRUPT;
	udb_storage_free(validation);
	return result;
}
