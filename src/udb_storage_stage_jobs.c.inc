/* Writer-owned snapshot jobs. Inputs are copied canonical bytes and stable IDs.
 * BEGIN/PUT have no durability receipt. END only commits a pending root; the
 * active six-block generation changes exclusively at PROMOTE. Daemon callbacks
 * must check the completion revision before ACK, effects or readiness changes. */

typedef struct UdbStorageStageOwner
{
	struct UdbStorageStageOwner *next;
	UdbStorageStageHead head;
	UdbStorageStageSpec specs[6];
	UdbStorageSort *sorts[6];
	unsigned int admitted;
	unsigned int sealed;
	unsigned int ready;
	int failed;
} UdbStorageStageOwner;

typedef struct UdbStorageStageWork
{
	UdbStorageStageOwner *owner;
	UdbStorageRebuild *rebuild;
	UdbStorageSource source;
	UdbStorageBlockRoot root;
	UdbStorageLogicalSummary partitions[UDB_STORAGE_BASE_PARTITIONS];
	uint64_t job;
	uint64_t sequence;
	uint64_t next_object;
	uint64_t start;
	uint64_t metadata_start;
	unsigned int block;
	unsigned int partition;
	unsigned int phase;
} UdbStorageStageWork;

static int udb_storage_stage_block_index(unsigned char block)
{
	for (unsigned int i = 0; i < 6; i++)
		if (udb_storage_blocks[i] == block)
			return (int)i;
	return -1;
}

static int udb_storage_stage_same_head(const UdbStorageStageHead *a, const UdbStorageStageHead *b)
{
	return a->owner == b->owner && a->round == b->round && a->source_epoch == b->source_epoch &&
		   a->source_sequence == b->source_sequence && !memcmp(a->source_sid, b->source_sid, 4);
}

static UdbStorageStageOwner *udb_storage_stage_owner_find(UdbStorageEngine *engine, uint64_t id)
{
	UdbStorageStageOwner *owner = engine->stages;
	for (; owner && owner->head.owner != id; owner = owner->next)
		;
	return owner;
}

static UdbStorageResult udb_storage_stage_owner_close(UdbStorageEngine *engine, UdbStorageStageOwner *owner)
{
	UdbStorageStageOwner **link = (UdbStorageStageOwner **)&engine->stages;
	UdbStorageResult result;
	if (!owner)
		return UDB_STORAGE_OK;
	for (unsigned int i = 0; i < 6; i++)
		if (owner->sorts[i])
		{
			result = udb_storage_sort_close(owner->sorts + i);
			if (result != UDB_STORAGE_OK)
				return result;
		}
	while (*link && *link != owner)
		link = &(*link)->next;
	if (!*link)
		return UDB_STORAGE_ERROR;
	*link = owner->next;
	udb_storage_free(owner);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_work_close(UdbStorageEngine *engine)
{
	UdbStorageStageWork *work = engine->work;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (!work)
		return result;
	if (work->rebuild)
		result = udb_storage_rebuild_close(&work->rebuild);
	udb_storage_free(work);
	engine->work = NULL;
	return result;
}

static UdbStorageResult udb_storage_stage_jobs_cleanup(UdbStorageEngine *engine)
{
	UdbStorageResult result = udb_storage_stage_work_close(engine);
	if (result != UDB_STORAGE_OK)
		return result;
	while (engine->stages)
	{
		result = udb_storage_stage_owner_close(engine, engine->stages);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_spec_read(const UdbStorageJob *job, UdbStorageStageSpec *spec)
{
	if (job->length != sizeof(*spec))
		return UDB_STORAGE_REJECTED;
	memcpy(spec, job->payload, sizeof(*spec));
	if (!job->owner || spec->head.owner != job->owner || spec->head.mask || !spec->head.round ||
		!udb_storage_sid_valid(spec->head.source_sid) || udb_storage_stage_block_index(spec->block) < 0 ||
		!spec->rows_limit || spec->rows_limit > (uint64_t)UINT32_MAX + 1 || spec->count > spec->rows_limit ||
		spec->physical_limit < 8 * UDB_STORAGE_PAGE_SIZE || spec->physical_limit > INT64_MAX)
		return UDB_STORAGE_REJECTED;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_owner_admit(UdbStorageEngine *engine, const UdbStorageStageSpec *spec,
													  UdbStorageStageOwner **out)
{
	UdbStorageStageOwner *owner = udb_storage_stage_owner_find(engine, spec->head.owner);
	if (owner && (owner->failed || !udb_storage_stage_same_head(&owner->head, &spec->head)))
		return UDB_STORAGE_REJECTED;
	if (!owner)
	{
		owner = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*owner));
		if (!owner)
			return UDB_STORAGE_LIMIT;
		owner->head = spec->head;
		owner->next = engine->stages;
		engine->stages = owner;
	}
	*out = owner;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_summary_read(unsigned int partition, UdbStorageLogicalSummary *summary,
													   void *opaque)
{
	UdbStorageStageWork *work = opaque;
	if (partition >= UDB_STORAGE_BASE_PARTITIONS)
		return UDB_STORAGE_ERROR;
	*summary = work->partitions[partition];
	return UDB_STORAGE_OK;
}

/* Counts copied sort output and new stable-container bytes, not the size of
 * already-existing active generations. No truncation of active data occurs. */
static UdbStorageResult udb_storage_stage_remaining(UdbStorageEngine *engine, UdbStorageStageWork *work,
													uint64_t *remaining)
{
	uint64_t sorted, data = engine->files[work->block].allocated_bytes;
	uint64_t metadata = work->block == 3 ? work->metadata_start : engine->files[3].allocated_bytes, used;
	UdbStorageResult result = udb_storage_sort_size(work->owner->sorts[work->block], &sorted);
	if (result != UDB_STORAGE_OK)
		return result;
	if (data < work->start || metadata < work->metadata_start)
		return UDB_STORAGE_CORRUPT;
	used = data - work->start;
	if (sorted > UINT64_MAX - used)
		return UDB_STORAGE_LIMIT;
	used += sorted;
	if (metadata - work->metadata_start > UINT64_MAX - used)
		return UDB_STORAGE_LIMIT;
	used += metadata - work->metadata_start;
	if (used > work->owner->specs[work->block].physical_limit)
		return UDB_STORAGE_LIMIT;
	*remaining = work->owner->specs[work->block].physical_limit - used;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_end_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
													  const UdbStorageState *base, UdbStorageState *candidate,
													  UdbStorageStageOwner *owner, unsigned int block)
{
	UdbStorageStageWork *work = engine->work;
	UdbStorageStageSpec *spec = owner->specs + block;
	UdbStorageResult result;
	uint64_t remaining, count;
	if (!work)
	{
		if (owner->sealed & (1U << block) || !owner->sorts[block] ||
			udb_storage_sort_rows(owner->sorts[block]) != spec->count)
			return UDB_STORAGE_REJECTED;
		if (base->sequence == UINT64_MAX)
			return UDB_STORAGE_LIMIT;
		work = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*work));
		if (!work)
			return UDB_STORAGE_LIMIT;
		engine->work = work;
		work->owner = owner;
		work->block = block;
		work->job = job->id;
		work->sequence = base->sequence + 1;
		work->next_object = base->next_object;
		work->start = engine->files[block].allocated_bytes;
		work->metadata_start = engine->files[3].allocated_bytes;
		result = udb_storage_sort_seal(owner->sorts[block]);
		if (result != UDB_STORAGE_OK)
			return result;
		owner->sealed |= 1U << block;
		work->root.block = spec->block;
		work->root.catalog.object = block + 1;
		work->root.generation = base->generation;
		work->root.watermark = work->sequence;
	}
	if (work->job != job->id || work->owner != owner || work->block != block)
		return UDB_STORAGE_ERROR;
	if (!work->phase)
	{
		result = udb_storage_sort_step(owner->sorts[block]);
		if (result == UDB_STORAGE_BUSY)
			return UDB_STORAGE_CONTINUE;
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_sort_source(owner->sorts[block], work->sequence, 0, &work->source);
		if (result != UDB_STORAGE_OK)
			return result;
		/* The schema owner must verify complete N/K entity profiles, not merely
		 * each individual attribute, before any root receives durable admission. */
		result = engine->stage_validator(&work->source, spec->block, engine->validation_context);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_CORRUPT || result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_REJECTED : result;
		work->phase = 1;
	}
	if (work->partition < UDB_STORAGE_BASE_PARTITIONS)
	{
		UdbStorageLogicalPosition position = {0};
		position.block = spec->block;
		position.partition = (uint16_t)work->partition;
		result = udb_storage_sort_partition_count(owner->sorts[block], work->partition, &count);
		if (result != UDB_STORAGE_OK)
			return result;
		if (!count)
		{
			result = udb_storage_logical_empty(&position, work->partitions + work->partition);
			if (result != UDB_STORAGE_OK)
				return result;
			work->partition++;
			return UDB_STORAGE_CONTINUE;
		}
		if (!work->rebuild)
		{
			result = udb_storage_rebuild_create(engine->cache, engine->files + block, &work->root, &position,
												base->generation, work->sequence, work->next_object, &work->source,
												&work->rebuild);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_rebuild_defer(work->rebuild);
			if (result != UDB_STORAGE_OK)
				return result;
		}
		result = udb_storage_stage_remaining(engine, work, &remaining);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_rebuild_allowance(
				work->rebuild, remaining < UDB_STORAGE_COMPACTION_MAX ? remaining : UDB_STORAGE_COMPACTION_MAX);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_rebuild_step(work->rebuild, &work->root, &work->next_object);
		if (result == UDB_STORAGE_BUSY)
			return UDB_STORAGE_CONTINUE;
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_rebuild_summary(work->rebuild, work->partitions + work->partition);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_rebuild_close(&work->rebuild);
		if (result != UDB_STORAGE_OK)
			return result;
		work->partition++;
		return UDB_STORAGE_CONTINUE;
	}
	result = udb_storage_logical_block(spec->block, udb_storage_stage_summary_read, work, &work->root.summary);
	if (result != UDB_STORAGE_OK)
		return result;
	if (work->root.summary.count != spec->count || memcmp(work->root.summary.digest, spec->digest, 32))
		return UDB_STORAGE_REJECTED;
	result = udb_storage_block_validate(engine->cache, engine->files + block, &work->root, engine->validator,
										engine->validation_context);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_stage_remaining(engine, work, &remaining);
	if (result != UDB_STORAGE_OK || remaining < UDB_STORAGE_PAGE_SIZE)
		return result != UDB_STORAGE_OK ? result : UDB_STORAGE_LIMIT;
	uint64_t size;
	result = udb_storage_file_size(engine->files + 3, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (remaining > UDB_STORAGE_COMPACTION_MAX)
		remaining = UDB_STORAGE_COMPACTION_MAX;
	if (!udb_storage_io_range_valid(size, remaining))
		return UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(&engine->files[3], size + remaining);
	UdbStorageState staged = *base;
	staged.sequence = work->sequence;
	staged.next_object = work->next_object;
	result = udb_storage_stage_save(engine, &staged, &owner->head, &work->root, candidate);
	udb_storage_file_job_limit(&engine->files[3], 0);
	if (result != UDB_STORAGE_OK)
		return result;
	owner->ready |= 1U << block;
	/* Later jobs in the same group must not inherit this controller. All root
	 * bytes have been copied into candidate, which remains private until WAL. */
	return udb_storage_stage_work_close(engine);
}

static UdbStorageResult udb_storage_stage_jobs_dispatch(UdbStorageEngine *engine, const UdbStorageJob *job,
														const UdbStorageState *base, UdbStorageState *candidate)
{
	UdbStorageStageOwner *owner;
	UdbStorageStageSpec spec;
	UdbStorageRow row;
	UdbStorageRecord record = {0};
	UdbStorageResult result;
	unsigned char bytes[UDB_STORAGE_RECORD_MAX];
	size_t length;
	int block;
	if (!engine || !job || !base || !candidate || !job->owner)
		return UDB_STORAGE_ERROR;
	owner = udb_storage_stage_owner_find(engine, job->owner);
	if (job->kind == UDB_STORAGE_STAGE_ABORT)
	{
		if (job->length)
			return UDB_STORAGE_REJECTED;
		result = udb_storage_stage_drop(engine, base, job->owner, candidate);
		return result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_STAGED : result;
	}
	if (job->kind == UDB_STORAGE_STAGE_PROMOTE)
	{
		if (job->length || !owner || owner->failed || owner->ready != 63)
			return UDB_STORAGE_REJECTED;
		return udb_storage_stage_promote(engine, base, job->owner, candidate);
	}
	if (job->kind == UDB_STORAGE_STAGE_BEGIN || job->kind == UDB_STORAGE_STAGE_REUSE)
	{
		if (!engine->validator || !engine->stage_validator)
			return UDB_STORAGE_REJECTED;
		result = udb_storage_stage_spec_read(job, &spec);
		if (result != UDB_STORAGE_OK)
			return result;
		block = udb_storage_stage_block_index(spec.block);
		result = udb_storage_stage_owner_admit(engine, &spec, &owner);
		if (result != UDB_STORAGE_OK)
			return result;
		if (owner->admitted & (1U << block))
			return UDB_STORAGE_REJECTED;
		owner->specs[block] = spec;
		owner->admitted |= 1U << block;
		if (job->kind == UDB_STORAGE_STAGE_REUSE)
		{
			if (base->blocks[block].summary.count != spec.count ||
				memcmp(base->blocks[block].summary.digest, spec.digest, 32))
				return UDB_STORAGE_REJECTED;
			result = udb_storage_stage_save(engine, base, &owner->head, base->blocks + block, candidate);
			if (result == UDB_STORAGE_OK)
				owner->ready |= 1U << block;
			return result;
		}
		result = udb_storage_sort_create(&engine->directory, spec.block, spec.rows_limit, spec.physical_limit,
										 engine->validator, engine->validation_context, owner->sorts + block);
		return result == UDB_STORAGE_OK ? UDB_STORAGE_STAGED : result;
	}
	if (!owner || owner->failed)
		return UDB_STORAGE_REJECTED;
	if (job->kind == UDB_STORAGE_STAGE_PUT)
	{
		if (udb_storage_row_decode(job->payload, job->length, &row) != UDB_STORAGE_OK)
			return UDB_STORAGE_REJECTED;
		block = udb_storage_stage_block_index(row.block);
		if (block < 0 || !(owner->admitted & (1U << block)) || owner->sealed & (1U << block) || !owner->sorts[block])
			return UDB_STORAGE_REJECTED;
		uint64_t ordinal = udb_storage_sort_rows(owner->sorts[block]);
		if (ordinal >= owner->specs[block].count || ordinal > UINT32_MAX)
			return UDB_STORAGE_REJECTED;
		record.row = row;
		record.operation = UDB_STORAGE_PUT;
		record.stamp.ordinal = (uint32_t)ordinal;
		result = udb_storage_record_encode(&record, bytes, sizeof(bytes), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_sort_add(owner->sorts[block], bytes, length);
		return result == UDB_STORAGE_OK ? UDB_STORAGE_STAGED : result;
	}
	if (job->kind == UDB_STORAGE_STAGE_END)
	{
		if (job->length != 1 || (block = udb_storage_stage_block_index(job->payload[0])) < 0 ||
			!(owner->admitted & (1U << block)))
			return UDB_STORAGE_REJECTED;
		return udb_storage_stage_end_prepare(engine, job, base, candidate, owner, (unsigned int)block);
	}
	return UDB_STORAGE_REJECTED;
}

static UdbStorageResult udb_storage_stage_jobs_finish(UdbStorageEngine *engine, const UdbStorageJob *job)
{
	UdbStorageStageOwner *owner = udb_storage_stage_owner_find(engine, job->owner);
	UdbStorageResult result;
	if (engine->work && ((UdbStorageStageWork *)engine->work)->job == job->id)
	{
		result = udb_storage_stage_work_close(engine);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (!owner)
		return UDB_STORAGE_OK;
	if ((job->kind == UDB_STORAGE_STAGE_ABORT || job->kind == UDB_STORAGE_STAGE_PROMOTE) &&
		(job->result == UDB_STORAGE_OK || job->result == UDB_STORAGE_STAGED))
		return udb_storage_stage_owner_close(engine, owner);
	if (job->result < UDB_STORAGE_NOT_FOUND)
	{
		owner->failed = 1;
		for (unsigned int i = 0; i < 6; i++)
			if (owner->sorts[i])
			{
				result = udb_storage_sort_close(owner->sorts + i);
				if (result != UDB_STORAGE_OK)
					return result;
			}
	}
	else if (job->kind == UDB_STORAGE_STAGE_END && job->result == UDB_STORAGE_OK)
	{
		int block = udb_storage_stage_block_index(job->payload[0]);
		if (block >= 0 && owner->sorts[block])
			return udb_storage_sort_close(owner->sorts + block);
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_jobs_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
													   const UdbStorageState *base, UdbStorageState *candidate)
{
	UdbStorageResult result = udb_storage_stage_jobs_dispatch(engine, job, base, candidate);
	if (result < UDB_STORAGE_NOT_FOUND && engine && job)
	{
		UdbStorageStageOwner *owner = udb_storage_stage_owner_find(engine, job->owner);
		if (owner)
			owner->failed = 1;
	}
	return result;
}
