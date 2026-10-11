/* Canonical private radix rebuild. Input is replayable, immutable and sorted.
 * Each step appends at most 64 MiB, including directories/catalog COW pages.
 * Large subtrees span multiple private jobs, never a larger output allowance.
 * Engine publication/flush and input generation pins remain separate concerns. */

typedef struct UdbStorageRebuildFrame
{
	UdbStorageLogicalPosition position;
	UdbStorageLogicalSummary left;
	unsigned int phase;
} UdbStorageRebuildFrame;

struct UdbStorageRebuild
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageBlockRoot candidate;
	UdbStorageLogicalPosition origin;
	UdbStorageSource source;
	UdbStorageCatalogIterator *old_nodes;
	UdbStorageRebuildFrame frames[257];
	unsigned char bytes[UDB_STORAGE_RECORD_MAX];
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	size_t previous_length;
	uint64_t next_object;
	UdbStorageLogicalSummary completed;
	unsigned int depth;
	UdbStorageResult failed;
	int cleaned;
	int finished;
	int unwinding;
	int defer_block_hash;
	uint64_t output_allowance;
};

static int udb_storage_position_descends(const UdbStorageLogicalPosition *child,
										 const UdbStorageLogicalPosition *parent)
{
	return child->block == parent->block && child->depth >= parent->depth &&
		   udb_storage_logical_contains(parent, child->prefix, child->partition);
}

static UdbStorageResult udb_storage_rebuild_close(UdbStorageRebuild **pointer)
{
	UdbStorageRebuild *rebuild;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	rebuild = *pointer;
	if (rebuild->old_nodes)
		result = udb_storage_catalog_iterator_close(&rebuild->old_nodes);
	udb_storage_free(rebuild);
	*pointer = NULL;
	return result;
}

static UdbStorageResult udb_storage_rebuild_create(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageBlockRoot *root,
												   const UdbStorageLogicalPosition *position, uint64_t generation,
												   uint64_t watermark, uint64_t next_object,
												   const UdbStorageSource *source, UdbStorageRebuild **out)
{
	UdbStorageRebuild *rebuild;
	unsigned char lower[64] = {0}, upper[64] = {0};
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || file->cache != cache || !udb_storage_block_root_valid(root) ||
		!udb_storage_logical_position_valid(position) || position->block != root->block ||
		generation < root->generation || watermark < root->watermark || !next_object || !source || !source->open ||
		!source->next || !source->close || file->append_limit)
		return UDB_STORAGE_ERROR;
	rebuild = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*rebuild));
	if (!rebuild)
		return UDB_STORAGE_LIMIT;
	rebuild->cache = cache;
	rebuild->file = file;
	rebuild->candidate = *root;
	rebuild->candidate.generation = generation;
	rebuild->candidate.watermark = watermark;
	rebuild->origin = *position;
	rebuild->defer_block_hash = position->depth != 0;
	rebuild->frames[0].position = *position;
	rebuild->source = *source;
	rebuild->next_object = next_object;
	rebuild->output_allowance = UDB_STORAGE_COMPACTION_MAX;
	/* Scan only this base partition, against the immutable old root. */
	lower[0] = upper[0] = position->block;
	lower[1] = upper[1] = 1;
	lower[2] = upper[2] = (unsigned char)(position->partition >> 8);
	lower[3] = upper[3] = (unsigned char)position->partition;
	if (++upper[3] == 0)
		upper[2]++;
	result = udb_storage_catalog_iterator_create(cache, file, &root->catalog, lower, upper, &rebuild->old_nodes);
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_rebuild_close(&rebuild);
		return result;
	}
	*out = rebuild;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_rebuild_remove_one(UdbStorageRebuild *rebuild)
{
	unsigned char key[64], value[128];
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageCatalogRoot changed;
	UdbStorageResult result;
	for (;;)
	{
		result = udb_storage_catalog_iterator_next(rebuild->old_nodes, key, value);
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			result = udb_storage_catalog_iterator_close(&rebuild->old_nodes);
			if (result == UDB_STORAGE_OK)
				rebuild->cleaned = 1;
			return result;
		}
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_node_position_decode(key, &position);
		if (result != UDB_STORAGE_OK)
			return result;
		if (!udb_storage_position_descends(&position, &rebuild->origin))
			continue;
		result = udb_storage_node_decode(&position, value, &node);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_catalog_change(rebuild->cache, rebuild->file, &rebuild->candidate.catalog,
											rebuild->candidate.generation, key, NULL, &changed);
		if (result != UDB_STORAGE_OK)
			return result;
		rebuild->candidate.catalog = changed;
		/* Rebuilt leaves receive fresh run IDs. Old-generation references remain
		 * intact in the pinned input catalog; no physical extent is reclaimed. */
		for (unsigned int i = 0; i < node.run_count; i++)
		{
			udb_storage_run_manifest_key(position.block, UDB_STORAGE_POINTS, node.runs[i], key);
			result = udb_storage_catalog_change(rebuild->cache, rebuild->file, &rebuild->candidate.catalog,
												rebuild->candidate.generation, key, NULL, &changed);
			if (result != UDB_STORAGE_OK)
				return result;
			rebuild->candidate.catalog = changed;
		}
		return UDB_STORAGE_OK;
	}
}

static UdbStorageResult udb_storage_rebuild_read(UdbStorageRebuild *rebuild, void *handle,
												 const UdbStorageLogicalPosition *position, UdbStorageRecord *record,
												 size_t *encoded_length)
{
	UdbStorageResult result;
	UdbStorageRow previous;
	unsigned char hash[32];
	unsigned int partition;
	for (;;)
	{
		result = rebuild->source.next(handle, record);
		if (result != UDB_STORAGE_OK)
			return result;
		if (record->operation != UDB_STORAGE_PUT || record->stamp.sequence > rebuild->candidate.watermark ||
			record->row.block != position->block || !udb_storage_row_valid(&record->row))
			return UDB_STORAGE_CORRUPT;
		if (rebuild->previous_length &&
			(udb_storage_row_decode(rebuild->previous, rebuild->previous_length, &previous) != UDB_STORAGE_OK ||
			 udb_storage_key_compare(&previous, &record->row) >= 0))
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_run_copy_key(&record->row, rebuild->previous, &rebuild->previous_length);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_key_hash(&record->row, hash, &partition);
		if (result != UDB_STORAGE_OK)
			return result;
		if (!udb_storage_logical_contains(position, hash, partition))
			continue;
		return udb_storage_row_encode(&record->row, rebuild->bytes, sizeof(rebuild->bytes), encoded_length);
	}
}

static UdbStorageResult udb_storage_rebuild_probe(UdbStorageRebuild *rebuild, const UdbStorageLogicalPosition *position,
												  int *split)
{
	void *handle = NULL;
	UdbStorageRecord record;
	UdbStorageResult result, closed;
	uint64_t weight = 0;
	size_t length;
	*split = 0;
	rebuild->previous_length = 0;
	result = rebuild->source.open(rebuild->source.context, position, &handle);
	if (result != UDB_STORAGE_OK)
		return result;
	while ((result = udb_storage_rebuild_read(rebuild, handle, position, &record, &length)) == UDB_STORAGE_OK)
	{
		if (length + UINT64_C(128) > UDB_STORAGE_LEAF_WEIGHT_MAX - weight)
		{
			*split = 1;
			break;
		}
		weight += length + UINT64_C(128);
	}
	closed = rebuild->source.close(&handle);
	if (closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND)
		return closed;
	return result == UDB_STORAGE_NOT_FOUND || *split ? UDB_STORAGE_OK : result;
}

static UdbStorageResult udb_storage_rebuild_leaf(UdbStorageRebuild *rebuild, const UdbStorageLogicalPosition *position,
												 UdbStorageLogicalSummary *summary)
{
	void *handle = NULL;
	UdbStorageRunBuilder *builder = NULL;
	UdbStorageLeafHasher *hasher = NULL;
	UdbStorageRecord record;
	UdbStorageRun run;
	UdbStorageNode node;
	UdbStorageCatalogRoot changed;
	UdbStorageResult result, closed;
	size_t length;
	memset(&node, 0, sizeof(node));
	node.kind = UDB_STORAGE_LEAF;
	rebuild->previous_length = 0;
	result = rebuild->source.open(rebuild->source.context, position, &handle);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_leaf_hasher_create(rebuild->cache->budget, position, &hasher);
	if (result != UDB_STORAGE_OK)
		goto done;
	while ((result = udb_storage_rebuild_read(rebuild, handle, position, &record, &length)) == UDB_STORAGE_OK)
	{
		result = udb_storage_leaf_hasher_add(hasher, rebuild->bytes, length);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (!builder)
		{
			if (rebuild->next_object == UINT64_MAX)
			{
				result = UDB_STORAGE_LIMIT;
				goto done;
			}
			result = udb_storage_run_builder_create(rebuild->cache, rebuild->file, position->block, UDB_STORAGE_POINTS,
													rebuild->candidate.generation, rebuild->next_object++, &builder);
			if (result != UDB_STORAGE_OK)
				goto done;
		}
		result = udb_storage_record_encode(&record, rebuild->bytes, sizeof(rebuild->bytes), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_run_builder_add(builder, rebuild->bytes, length);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	if (result != UDB_STORAGE_NOT_FOUND)
		goto done;
	result = udb_storage_leaf_hasher_finish(hasher, &node.summary);
	if (result != UDB_STORAGE_OK)
		goto done;
	if (builder)
	{
		result = udb_storage_run_builder_finish(builder, &run);
		if (result != UDB_STORAGE_OK)
			goto done;
		result = udb_storage_run_save(rebuild->cache, &run, &rebuild->candidate.catalog, rebuild->candidate.generation,
									  &changed);
		if (result != UDB_STORAGE_OK)
			goto done;
		rebuild->candidate.catalog = changed;
		node.run_count = 1;
		node.runs[0] = run.directory.object;
		result = udb_storage_node_change(rebuild->cache, rebuild->file, &rebuild->candidate.catalog,
										 rebuild->candidate.generation, position, &node, &changed);
		if (result != UDB_STORAGE_OK)
			goto done;
		rebuild->candidate.catalog = changed;
	}
	/* Empty leaves stay implicit, with the position-specific canonical digest. */
	*summary = node.summary;
	result = UDB_STORAGE_OK;
done:
	closed = rebuild->source.close(&handle);
	if (closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND)
		result = closed;
	udb_storage_leaf_hasher_free(&hasher);
	udb_storage_run_builder_free(&builder);
	return result;
}

static UdbStorageResult udb_storage_rebuild_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
													  void *opaque)
{
	UdbStorageRebuild *rebuild = opaque;
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageResult result;
	memset(&position, 0, sizeof(position));
	position.block = rebuild->candidate.block;
	position.partition = (uint16_t)partition;
	result = udb_storage_node_get(rebuild->cache, rebuild->file, &rebuild->candidate.catalog, &position, &node);
	if (result == UDB_STORAGE_OK)
		*summary = node.summary;
	return result;
}

static UdbStorageResult udb_storage_rebuild_step(UdbStorageRebuild *rebuild, UdbStorageBlockRoot *out,
												 uint64_t *next_object)
{
	UdbStorageRebuildFrame *frame;
	UdbStorageLogicalSummary completed;
	UdbStorageResult result;
	uint64_t size;
	int split;
	if (!rebuild || !out || !next_object || rebuild->file->append_limit)
		return UDB_STORAGE_ERROR;
	if (rebuild->failed)
		return rebuild->failed;
	if (rebuild->finished)
	{
		*out = rebuild->candidate;
		*next_object = rebuild->next_object;
		return UDB_STORAGE_OK;
	}
	result = udb_storage_file_size(rebuild->file, &size);
	if (result != UDB_STORAGE_OK)
		return rebuild->failed = result;
	if (!udb_storage_io_range_valid(size, rebuild->output_allowance))
		return rebuild->failed = UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(rebuild->file, size + rebuild->output_allowance);
	if (!rebuild->cleaned)
	{
		result = udb_storage_rebuild_remove_one(rebuild);
		goto done;
	}
	frame = rebuild->frames + rebuild->depth;
	if (rebuild->unwinding)
		goto unwind;
	for (;;)
	{
		result = udb_storage_rebuild_probe(rebuild, &frame->position, &split);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (!split)
			break;
		if (frame->position.depth == 256 || rebuild->depth == 256)
		{
			result = UDB_STORAGE_LIMIT;
			goto done;
		}
		frame->phase = 1;
		result = udb_storage_logical_child(&frame->position, 0, &rebuild->frames[rebuild->depth + 1].position);
		if (result != UDB_STORAGE_OK)
			goto done;
		frame = rebuild->frames + ++rebuild->depth;
		frame->phase = 0;
	}
	result = udb_storage_rebuild_leaf(rebuild, &frame->position, &completed);
	if (result != UDB_STORAGE_OK)
		goto done;
	rebuild->completed = completed;
	rebuild->unwinding = 1;
	goto done;
unwind:
	if (rebuild->depth)
	{
		UdbStorageNode node;
		UdbStorageCatalogRoot changed;
		frame = rebuild->frames + --rebuild->depth;
		if (frame->phase == 1)
		{
			frame->left = rebuild->completed;
			frame->phase = 2;
			result = udb_storage_logical_child(&frame->position, 1, &rebuild->frames[rebuild->depth + 1].position);
			if (result != UDB_STORAGE_OK)
				goto done;
			rebuild->frames[++rebuild->depth].phase = 0;
			rebuild->unwinding = 0;
			goto done;
		}
		memset(&node, 0, sizeof(node));
		node.kind = UDB_STORAGE_BRANCH;
		result = udb_storage_logical_branch(&frame->position, &frame->left, &rebuild->completed, &node.summary);
		if (result != UDB_STORAGE_OK)
			goto done;
		result = udb_storage_node_change(rebuild->cache, rebuild->file, &rebuild->candidate.catalog,
										 rebuild->candidate.generation, &frame->position, &node, &changed);
		if (result != UDB_STORAGE_OK)
			goto done;
		rebuild->candidate.catalog = changed;
		rebuild->completed = node.summary;
		goto done;
	}
	result = rebuild->defer_block_hash
				 ? UDB_STORAGE_OK
				 : udb_storage_logical_block(rebuild->candidate.block, udb_storage_rebuild_partition, rebuild,
											 &rebuild->candidate.summary);
	if (result == UDB_STORAGE_OK)
	{
		rebuild->finished = 1;
		*out = rebuild->candidate;
	}
done:
	udb_storage_file_job_limit(rebuild->file, 0);
	*next_object = rebuild->next_object;
	if (result != UDB_STORAGE_OK)
		return rebuild->failed = result;
	return rebuild->finished ? UDB_STORAGE_OK : UDB_STORAGE_BUSY;
}

/* Only a full private block builder may defer the block digest. The resulting
 * root is not admissible for publication until all partition summaries hash. */
static UdbStorageResult udb_storage_rebuild_defer(UdbStorageRebuild *rebuild)
{
	if (!rebuild || rebuild->cleaned || rebuild->finished || rebuild->failed)
		return UDB_STORAGE_ERROR;
	rebuild->defer_block_hash = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_rebuild_summary(UdbStorageRebuild *rebuild, UdbStorageLogicalSummary *summary)
{
	if (!rebuild || !summary || !rebuild->finished || rebuild->failed)
		return UDB_STORAGE_ERROR;
	*summary = rebuild->completed;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_rebuild_allowance(UdbStorageRebuild *rebuild, uint64_t bytes)
{
	if (!rebuild || bytes < UDB_STORAGE_PAGE_SIZE || bytes > UDB_STORAGE_COMPACTION_MAX || rebuild->file->append_limit)
		return UDB_STORAGE_LIMIT;
	rebuild->output_allowance = bytes;
	return UDB_STORAGE_OK;
}
