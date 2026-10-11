/* Ordered prefix deletion with bounded private partition rebuilds. A tombstone
 * is retired only after every potentially covered partition has a baseline
 * materialized through its watermark. Old generation pins retain old catalogs.
 * All intermediate roots remain private to the one writer transaction. */

struct UdbStoragePrefixChange
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageBlockRoot root;
	UdbStorageRecord record;
	unsigned char bytes[UDB_STORAGE_RECORD_MAX];
	size_t length;
	UdbStorageSort *sort;
	UdbStorageBlockIterator *iterator;
	UdbStorageRebuild *rebuild;
	UdbStorageSource source;
	uint64_t next_object;
	uint64_t tombstone;
	unsigned int partition;
	unsigned int end;
	unsigned int phase;
	UdbStorageResult failed;
	int finished;
};

static UdbStorageResult udb_storage_prefix_close(UdbStoragePrefixChange **pointer)
{
	UdbStoragePrefixChange *change;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	change = *pointer;
	if (change->rebuild)
	{
		result = udb_storage_rebuild_close(&change->rebuild);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (change->iterator)
	{
		result = udb_storage_block_iterator_close(&change->iterator);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (change->sort)
	{
		result = udb_storage_sort_close(&change->sort);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	udb_storage_free(change);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_prefix_create(UdbStorageCache *cache, UdbStorageFile *file,
												  const UdbStorageBlockRoot *root, uint64_t generation,
												  uint64_t next_object, const UdbStorageRecord *record,
												  UdbStoragePrefixChange **out)
{
	UdbStoragePrefixChange *change;
	UdbStorageResult result;
	unsigned char hash[32];
	unsigned int partition;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || file->cache != cache || file->append_limit || !udb_storage_block_root_valid(root) ||
		!record || record->row.block != root->block || record->operation != UDB_STORAGE_PREFIX_DELETE ||
		record->stamp.sequence <= root->watermark || generation < root->generation || next_object < 8)
		return UDB_STORAGE_ERROR;
	if (root->prefix_count == UDB_STORAGE_MAX_RUNS + 1)
		return UDB_STORAGE_LIMIT;
	change = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*change));
	if (!change)
		return UDB_STORAGE_LIMIT;
	change->cache = cache;
	change->file = file;
	change->root = *root;
	change->root.generation = generation;
	change->root.watermark = record->stamp.sequence;
	change->next_object = next_object;
	result = udb_storage_record_encode(record, change->bytes, sizeof(change->bytes), &change->length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_record_decode(change->bytes, change->length, &change->record);
	if (result != UDB_STORAGE_OK)
		goto fail;
	if (root->block == 'K' && record->row.depth == 1)
		change->end = UDB_STORAGE_BASE_PARTITIONS;
	else
	{
		result = udb_storage_key_hash(&record->row, hash, &partition);
		if (result != UDB_STORAGE_OK)
			goto fail;
		change->partition = partition;
		change->end = partition + 1;
	}
	*out = change;
	return UDB_STORAGE_OK;
fail:
	udb_storage_prefix_close(&change);
	return result;
}

static UdbStorageResult udb_storage_prefix_append(UdbStoragePrefixChange *change)
{
	UdbStorageRunBuilder *builder = NULL;
	UdbStorageRun run;
	UdbStorageCatalogRoot changed;
	UdbStorageResult result;
	uint64_t size;
	if (change->next_object == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_file_size(change->file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (!udb_storage_io_range_valid(size, UDB_STORAGE_COMPACTION_MAX))
		return UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(change->file, size + UDB_STORAGE_COMPACTION_MAX);
	change->tombstone = change->next_object++;
	result = udb_storage_run_builder_create(change->cache, change->file, change->root.block, UDB_STORAGE_PREFIXES,
											change->root.generation, change->tombstone, &builder);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_builder_add(builder, change->bytes, change->length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_builder_finish(builder, &run);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_save(change->cache, &run, &change->root.catalog, change->root.generation, &changed);
	udb_storage_run_builder_free(&builder);
	udb_storage_file_job_limit(change->file, 0);
	if (result != UDB_STORAGE_OK)
		return result;
	change->root.catalog = changed;
	change->root.prefixes[change->root.prefix_count++] = change->tombstone;
	change->phase = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_prefix_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
													 void *opaque)
{
	UdbStoragePrefixChange *change = opaque;
	UdbStorageLogicalPosition position = {0};
	UdbStorageNode node;
	UdbStorageResult result;
	position.block = change->root.block;
	position.partition = (uint16_t)partition;
	result = udb_storage_node_get(change->cache, change->file, &change->root.catalog, &position, &node);
	if (result == UDB_STORAGE_OK)
		*summary = node.summary;
	return result;
}

static UdbStorageResult udb_storage_prefix_step(UdbStoragePrefixChange *change, UdbStorageBlockRoot *root,
												uint64_t *next_object)
{
	UdbStorageResult result;
	if (!change || !root || !next_object || change->file->append_limit)
		return UDB_STORAGE_ERROR;
	if (change->failed)
		return change->failed;
	if (change->finished)
	{
		*root = change->root;
		*next_object = change->next_object;
		return UDB_STORAGE_OK;
	}
	if (!change->phase)
	{
		result = udb_storage_prefix_append(change);
		goto done;
	}
	if (change->partition < change->end)
	{
		UdbStorageLogicalPosition position = {0};
		position.block = change->root.block;
		position.partition = (uint16_t)change->partition;
		if (change->phase == 1)
		{
			UdbStorageNode node;
			result = udb_storage_node_get(change->cache, change->file, &change->root.catalog, &position, &node);
			if (result == UDB_STORAGE_NOT_FOUND)
			{
				change->partition++;
				return UDB_STORAGE_BUSY;
			}
			if (result != UDB_STORAGE_OK)
				goto done;
			result = udb_storage_sort_create(change->file->directory, change->root.block,
											 node.summary.count ? node.summary.count : 1,
											 UINT64_C(16) * 1024 * 1024 * 1024, NULL, NULL, &change->sort);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_block_partition_iterator(change->cache, change->file, &change->root,
															  change->partition, &change->iterator);
			if (result != UDB_STORAGE_OK)
				goto done;
			change->phase = 2;
		}
		if (change->phase == 2)
		{
			UdbStorageRecord record;
			unsigned char bytes[UDB_STORAGE_RECORD_MAX];
			size_t length;
			/* A finite ingest batch bounds both CPU and aggregate spill output
			 * below 64 MiB even for maximal canonical records. */
			for (unsigned int i = 0; i < 1024; i++)
			{
				result = udb_storage_block_iterator_next(change->iterator, &record);
				if (result == UDB_STORAGE_NOT_FOUND)
				{
					result = udb_storage_block_iterator_close(&change->iterator);
					if (result == UDB_STORAGE_OK)
						result = udb_storage_sort_seal(change->sort);
					if (result == UDB_STORAGE_OK)
						change->phase = 3;
					goto done;
				}
				if (result != UDB_STORAGE_OK)
					goto done;
				result = udb_storage_record_encode(&record, bytes, sizeof(bytes), &length);
				if (result == UDB_STORAGE_OK)
					result = udb_storage_sort_add(change->sort, bytes, length);
				if (result != UDB_STORAGE_OK)
					goto done;
			}
			return UDB_STORAGE_BUSY;
		}
		if (change->phase == 3)
		{
			result = udb_storage_sort_step(change->sort);
			if (result == UDB_STORAGE_BUSY)
				return result;
			if (result != UDB_STORAGE_OK)
				goto done;
			result = udb_storage_sort_source(change->sort, 0, 0, &change->source);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_rebuild_create(change->cache, change->file, &change->root, &position,
													change->root.generation, change->root.watermark,
													change->next_object, &change->source, &change->rebuild);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_rebuild_defer(change->rebuild);
			if (result == UDB_STORAGE_OK)
				change->phase = 4;
			goto done;
		}
		result = udb_storage_rebuild_step(change->rebuild, &change->root, &change->next_object);
		if (result == UDB_STORAGE_BUSY)
			return result;
		if (result != UDB_STORAGE_OK)
			goto done;
		result = udb_storage_rebuild_close(&change->rebuild);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_sort_close(&change->sort);
		if (result == UDB_STORAGE_OK)
		{
			change->partition++;
			change->phase = 1;
		}
		goto done;
	}
	{
		unsigned char key[64];
		uint64_t size;
		UdbStorageCatalogRoot changed;
		result = udb_storage_file_size(change->file, &size);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (!udb_storage_io_range_valid(size, UDB_STORAGE_COMPACTION_MAX))
		{
			result = UDB_STORAGE_LIMIT;
			goto done;
		}
		udb_storage_file_job_limit(change->file, size + UDB_STORAGE_COMPACTION_MAX);
		udb_storage_run_manifest_key(change->root.block, UDB_STORAGE_PREFIXES, change->tombstone, key);
		result = udb_storage_catalog_change(change->cache, change->file, &change->root.catalog, change->root.generation,
											key, NULL, &changed);
		udb_storage_file_job_limit(change->file, 0);
		if (result != UDB_STORAGE_OK)
			goto done;
		change->root.catalog = changed;
		if (!change->root.prefix_count || change->root.prefixes[change->root.prefix_count - 1] != change->tombstone)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		change->root.prefixes[--change->root.prefix_count] = 0;
		result =
			udb_storage_logical_block(change->root.block, udb_storage_prefix_partition, change, &change->root.summary);
		if (result == UDB_STORAGE_OK)
			change->finished = 1;
	}
done:
	if (result != UDB_STORAGE_OK)
		return change->failed = result;
	if (!change->finished)
		return UDB_STORAGE_BUSY;
	*root = change->root;
	*next_object = change->next_object;
	return UDB_STORAGE_OK;
}
