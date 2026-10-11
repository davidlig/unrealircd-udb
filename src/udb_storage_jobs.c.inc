/* Production storage job dispatch. Thread-safe pure validators run before
 * journal admission; daemon state/effects/relay are foreground completion work.
 * Wire sequencing and internal physical sequencing are independent counters. */

typedef struct UdbStorageMutationWork
{
	uint64_t job;
	UdbStorageState state;
	UdbStoragePointChange *point;
	UdbStoragePrefixChange *prefix;
	UdbStorageRow row;
	unsigned char bytes[UDB_STORAGE_ROW_MAX];
	unsigned char restore_bytes[UDB_STORAGE_ROW_MAX];
	UdbStorageRow restore;
	int restore_present;
	unsigned int block;
	int normalized;
} UdbStorageMutationWork;

static UdbStorageResult udb_storage_mutation_work_close(UdbStorageEngine *engine)
{
	UdbStorageMutationWork *work = engine->mutation_work;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (!work)
		return result;
	if (work->point)
		result = udb_storage_point_close(&work->point);
	if (result == UDB_STORAGE_OK && work->prefix)
		result = udb_storage_prefix_close(&work->prefix);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_free(work);
	engine->mutation_work = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_mutation_header(const UdbStorageJob *job, const UdbStorageState *base,
													UdbStorageState *candidate, unsigned int *block)
{
	uint64_t sequence, epoch;
	char sid[4];
	int index;
	if (job->length < UDB_STORAGE_MUTATION_HEADER ||
		*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 21) > 1 ||
		!udb_storage_zero(job->payload + 22, 2))
		return UDB_STORAGE_REJECTED;
	memcpy(sid, job->payload, 4);
	if (!udb_storage_sid_valid(sid))
		return UDB_STORAGE_REJECTED;
	epoch = udb_storage_get_u64(job->payload + 4);
	sequence = udb_storage_get_u64(job->payload + 12);
	*candidate = *base;
	if (job->kind == UDB_STORAGE_JOB_BIND)
	{
		if (job->length != UDB_STORAGE_MUTATION_HEADER ||
			*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 20) ||
			*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 21) || sequence)
			return UDB_STORAGE_REJECTED;
		memcpy(candidate->source_sid, sid, 4);
		candidate->source_epoch = epoch;
		candidate->source_sequence = 0;
		return UDB_STORAGE_OK;
	}
	if (memcmp(sid, base->source_sid, 4) || epoch != base->source_epoch || base->source_sequence == UINT64_MAX)
		return UDB_STORAGE_REJECTED;
	if (*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 21))
	{
		if (sequence)
			return UDB_STORAGE_REJECTED;
		sequence = base->source_sequence + 1;
	}
	else if (!sequence || sequence != base->source_sequence + 1)
		return UDB_STORAGE_REJECTED;
	index = udb_storage_stage_block_index(*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 20));
	if (index < 0)
		return UDB_STORAGE_REJECTED;
	if (base->sequence == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	*block = (unsigned int)index;
	candidate->source_sequence = sequence;
	candidate->sequence++;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_mutation_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
													 const UdbStorageState *base, UdbStorageState *candidate)
{
	UdbStorageMutationWork *work = engine->mutation_work;
	UdbStorageResult result;
	unsigned int block = 0;
	UdbStorageState state;
	if (!work)
	{
		result = udb_storage_mutation_header(job, base, &state, &block);
		if (result != UDB_STORAGE_OK)
			return result;
		if (job->kind == UDB_STORAGE_JOB_BIND)
		{
			*candidate = state;
			return UDB_STORAGE_OK;
		}
		if (job->kind == UDB_STORAGE_JOB_DROP)
		{
			if (job->length != UDB_STORAGE_MUTATION_HEADER)
				return UDB_STORAGE_REJECTED;
			memset(state.blocks + block, 0, sizeof(state.blocks[block]));
			state.blocks[block].block = udb_storage_blocks[block];
			state.blocks[block].catalog.object = block + 1;
			state.blocks[block].generation = state.generation;
			state.blocks[block].watermark = state.sequence;
			result = udb_storage_logical_block(udb_storage_blocks[block], udb_storage_journal_empty_partition, NULL,
											   &state.blocks[block].summary);
			if (result == UDB_STORAGE_OK)
				*candidate = state;
			return result;
		}
		if (!engine->validator || !engine->mutation_validator ||
			(job->kind != UDB_STORAGE_JOB_POINT && job->kind != UDB_STORAGE_JOB_PREFIX &&
			 job->kind != UDB_STORAGE_JOB_EXPIRE))
			return UDB_STORAGE_REJECTED;
		work = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*work));
		if (!work)
			return UDB_STORAGE_LIMIT;
		engine->mutation_work = work;
		work->job = job->id;
		work->state = state;
		work->block = block;
		size_t offset = UDB_STORAGE_MUTATION_HEADER + (job->kind == UDB_STORAGE_JOB_EXPIRE ? 8 : 0);
		if (job->length <= offset || job->length - offset > sizeof(work->bytes))
			return UDB_STORAGE_REJECTED;
		memcpy(work->bytes, job->payload + offset, job->length - offset);
		result = udb_storage_row_decode(work->bytes, job->length - offset, &work->row);
		if (result != UDB_STORAGE_OK || work->row.block != udb_storage_blocks[block])
			return UDB_STORAGE_REJECTED;
		if (job->kind == UDB_STORAGE_JOB_EXPIRE)
		{
			uint64_t expected = udb_storage_get_u64(job->payload + UDB_STORAGE_MUTATION_HEADER);
			if (!expected || !*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 21) ||
				work->row.block != 'K' || work->row.depth != 2 || work->row.kind != UDB_STORAGE_VALUE_NONE)
				return UDB_STORAGE_REJECTED;
			UdbStorageRow expiry_key = work->row, stored;
			expiry_key.components[2].data = (const unsigned char *)"expires";
			expiry_key.components[2].length = 7;
			expiry_key.depth = 3;
			/* Compare on the writer's ordered base, not a stale foreground pin.
			 * A stale request consumes neither root nor wire sequence. */
			result = udb_storage_block_lookup(engine->cache, engine->files + block, base->blocks + block, &expiry_key,
											  work->restore_bytes, sizeof(work->restore_bytes), &stored);
			if (result < UDB_STORAGE_NOT_FOUND)
				return result;
			if (result == UDB_STORAGE_NOT_FOUND || stored.kind != UDB_STORAGE_VALUE_NUMBER || stored.number != expected)
			{
				result = udb_storage_mutation_work_close(engine);
				return result == UDB_STORAGE_OK ? UDB_STORAGE_STAGED : result;
			}
		}
		UdbStorageRecord record = {0};
		record.row = work->row;
		record.stamp.sequence = state.sequence;
		record.operation = job->kind == UDB_STORAGE_JOB_POINT ? UDB_STORAGE_PUT : UDB_STORAGE_PREFIX_DELETE;
		/* Public DEL semantics are subtree deletion, never a NONE-value alias. */
		if (job->kind != UDB_STORAGE_JOB_POINT &&
			(record.row.kind != UDB_STORAGE_VALUE_NONE || record.row.number || record.row.value.length))
			return UDB_STORAGE_REJECTED;
		result = engine->validator(&record, engine->validation_context);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_CORRUPT || result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_REJECTED : result;
		UdbStorageRow clear;
		UdbStorageResult normalized = UDB_STORAGE_NOT_FOUND;
		if (job->kind == UDB_STORAGE_JOB_POINT && engine->normalizer)
			normalized = engine->normalizer(&record, &clear, engine->validation_context);
		if (normalized != UDB_STORAGE_OK && normalized != UDB_STORAGE_NOT_FOUND)
			return normalized;
		if (normalized == UDB_STORAGE_OK)
		{
			/* Normalization clears children, preserving any explicit parent. */
			UdbStorageResult found =
				udb_storage_block_lookup(engine->cache, engine->files + block, base->blocks + block, &clear,
										 work->restore_bytes, sizeof(work->restore_bytes), &work->restore);
			if (found < UDB_STORAGE_NOT_FOUND)
				return found;
			work->restore_present = found == UDB_STORAGE_OK;
			if (!udb_storage_key_has_prefix(&record.row, &clear) || clear.kind != UDB_STORAGE_VALUE_NONE ||
				clear.number)
				return UDB_STORAGE_ERROR;
			record.row = clear;
			record.operation = UDB_STORAGE_PREFIX_DELETE;
			result = engine->validator(&record, engine->validation_context);
			if (result != UDB_STORAGE_OK)
				return result;
			result = udb_storage_prefix_create(engine->cache, engine->files + block, base->blocks + block,
											   state.generation, state.next_object, &record, &work->prefix);
			work->normalized = 1;
		}
		else if (job->kind == UDB_STORAGE_JOB_POINT)
			result = udb_storage_point_create(engine->cache, engine->files + block, base->blocks + block,
											  state.generation, state.next_object, &record, &work->point);
		else
			result = udb_storage_prefix_create(engine->cache, engine->files + block, base->blocks + block,
											   state.generation, state.next_object, &record, &work->prefix);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (work->job != job->id)
		return UDB_STORAGE_ERROR;
	if (work->point)
		result = udb_storage_point_step(work->point, work->state.blocks + work->block, &work->state.next_object);
	else
		result = udb_storage_prefix_step(work->prefix, work->state.blocks + work->block, &work->state.next_object);
	if (result == UDB_STORAGE_BUSY)
		return UDB_STORAGE_CONTINUE;
	if (result != UDB_STORAGE_OK)
		return result;
	if (work->normalized == 1 || work->normalized == 3)
	{
		UdbStorageRecord record = {0};
		if (work->state.sequence == UINT64_MAX)
			return UDB_STORAGE_LIMIT;
		if (work->prefix)
			result = udb_storage_prefix_close(&work->prefix);
		if (result == UDB_STORAGE_OK && work->point)
			result = udb_storage_point_close(&work->point);
		if (result != UDB_STORAGE_OK)
			return result;
		int restoring = work->normalized == 1 && work->restore_present;
		record.row = restoring ? work->restore : work->row;
		record.operation = UDB_STORAGE_PUT;
		record.stamp.sequence = ++work->state.sequence;
		result = udb_storage_point_create(engine->cache, engine->files + work->block, work->state.blocks + work->block,
										  work->state.generation, work->state.next_object, &record, &work->point);
		if (result != UDB_STORAGE_OK)
			return result;
		work->normalized = restoring ? 3 : 2;
		return UDB_STORAGE_CONTINUE;
	}
	result =
		engine->mutation_validator(engine, work->state.blocks + work->block, &work->row, engine->validation_context);
	if (result != UDB_STORAGE_OK)
		return result == UDB_STORAGE_CORRUPT || result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_REJECTED : result;
	*candidate = work->state;
	return udb_storage_mutation_work_close(engine);
}

static UdbStorageResult udb_storage_jobs_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
												 const UdbStorageState *base, UdbStorageState *candidate)
{
	if (!engine || !job || !base || !candidate)
		return UDB_STORAGE_ERROR;
	return job->kind >= UDB_STORAGE_STAGE_BEGIN ? udb_storage_stage_jobs_prepare(engine, job, base, candidate)
												: udb_storage_mutation_prepare(engine, job, base, candidate);
}

static UdbStorageResult udb_storage_jobs_finish(UdbStorageEngine *engine, const UdbStorageJob *job)
{
	if (job->kind >= UDB_STORAGE_STAGE_BEGIN)
		return udb_storage_stage_jobs_finish(engine, job);
	UdbStorageMutationWork *work = engine->mutation_work;
	return work && work->job == job->id ? udb_storage_mutation_work_close(engine) : UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_jobs_cleanup(UdbStorageEngine *engine)
{
	UdbStorageResult result = udb_storage_mutation_work_close(engine);
	return result == UDB_STORAGE_OK ? udb_storage_stage_jobs_cleanup(engine) : result;
}
