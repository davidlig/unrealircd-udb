/* Typed reads over an engine-pinned, fully validated block generation.
 * Iteration order is logical partition/leaf order, then canonical key order
 * within each leaf; it is deliberately not a global key sort. */

struct UdbStorageBlockIterator
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageBlockRoot root;
	UdbStorageCatalogIterator *catalog;
	UdbStorageView *view;
	UdbStorageRun points[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun prefixes[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRow prefix;
	unsigned char bytes[UDB_STORAGE_KEY_MAX];
	UdbStorageResult failed;
	int has_prefix;
	int finished;
};

static UdbStorageResult udb_storage_block_prefixes(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageBlockRoot *root,
												   UdbStorageRun runs[UDB_STORAGE_MAX_RUNS + 1])
{
	for (unsigned int i = 0; i < root->prefix_count; i++)
	{
		UdbStorageResult result = udb_storage_run_load(cache, file, &root->catalog, root->block, UDB_STORAGE_PREFIXES,
													   root->prefixes[i], runs + i);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_block_points(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageBlockRoot *root, const UdbStorageNode *node,
												 UdbStorageRun runs[UDB_STORAGE_MAX_RUNS + 1])
{
	for (unsigned int i = 0; i < node->run_count; i++)
	{
		UdbStorageResult result =
			udb_storage_run_load(cache, file, &root->catalog, root->block, UDB_STORAGE_POINTS, node->runs[i], runs + i);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

typedef struct UdbStorageBlockLookup
{
	UdbStorageRun points[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun prefixes[UDB_STORAGE_MAX_RUNS + 1];
} UdbStorageBlockLookup;

static UdbStorageResult udb_storage_block_lookup(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageBlockRoot *root, const UdbStorageRow *key,
												 unsigned char *bytes, size_t capacity, UdbStorageRow *row)
{
	UdbStorageBlockLookup *lookup;
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageResult result;
	if (!cache || !file || file->cache != cache || !udb_storage_block_root_valid(root) || !udb_storage_row_valid(key) ||
		key->block != root->block || !bytes || !row)
		return UDB_STORAGE_ERROR;
	result = udb_storage_node_locate(cache, file, &root->catalog, key, &position, &node);
	if (result != UDB_STORAGE_OK)
		return result;
	lookup = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*lookup));
	if (!lookup)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_block_points(cache, file, root, &node, lookup->points);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_block_prefixes(cache, file, root, lookup->prefixes);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_lookup(cache, lookup->points, node.run_count, lookup->prefixes, root->prefix_count, key,
									root->watermark, bytes, capacity, row);
	udb_storage_free(lookup);
	return result;
}

static UdbStorageResult udb_storage_node_position_decode(const unsigned char key[64],
														 UdbStorageLogicalPosition *position)
{
	UdbStorageLogicalPosition candidate;
	unsigned char canonical[64];
	memset(&candidate, 0, sizeof(candidate));
	candidate.block = key[0];
	candidate.partition = (uint16_t)((key[2] << 8) | key[3]);
	candidate.depth = (uint16_t)((key[4] << 8) | key[5]);
	memcpy(candidate.prefix, key + 6, 32);
	if (!udb_storage_logical_position_valid(&candidate))
		return UDB_STORAGE_CORRUPT;
	udb_storage_node_key(&candidate, canonical);
	if (memcmp(key, canonical, 64))
		return UDB_STORAGE_CORRUPT;
	*position = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_block_iterator_close(UdbStorageBlockIterator **pointer)
{
	UdbStorageBlockIterator *iterator;
	UdbStorageResult result = UDB_STORAGE_OK, closed;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	iterator = *pointer;
	if (iterator->view)
		result = udb_storage_view_close(&iterator->view);
	if (iterator->catalog)
	{
		closed = udb_storage_catalog_iterator_close(&iterator->catalog);
		if (closed != UDB_STORAGE_OK && result == UDB_STORAGE_OK)
			result = closed;
	}
	udb_storage_free(iterator);
	*pointer = NULL;
	return result;
}

static UdbStorageResult udb_storage_block_iterator_create(UdbStorageCache *cache, UdbStorageFile *file,
														  const UdbStorageBlockRoot *root, const UdbStorageRow *prefix,
														  UdbStorageBlockIterator **out)
{
	UdbStorageBlockIterator *iterator;
	UdbStorageResult result;
	unsigned char lower[64] = {0}, upper[64] = {0}, hash[32];
	unsigned int partition;
	size_t length;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || file->cache != cache || !udb_storage_block_root_valid(root) ||
		(prefix && (!udb_storage_row_valid(prefix) || prefix->block != root->block)))
		return UDB_STORAGE_ERROR;
	iterator = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*iterator));
	if (!iterator)
		return UDB_STORAGE_LIMIT;
	iterator->cache = cache;
	iterator->file = file;
	iterator->root = *root;
	lower[0] = upper[0] = root->block;
	lower[1] = 1;
	upper[1] = 2;
	if (prefix)
	{
		result = udb_storage_run_copy_key(prefix, iterator->bytes, &length);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_row_decode(iterator->bytes, length, &iterator->prefix);
		if (result != UDB_STORAGE_OK)
			goto fail;
		iterator->has_prefix = 1;
		/* K type-only prefixes cover multiple entity partitions. */
		if (prefix->block != 'K' || prefix->depth >= 2)
		{
			result = udb_storage_key_hash(prefix, hash, &partition);
			if (result != UDB_STORAGE_OK)
				goto fail;
			lower[2] = upper[2] = (unsigned char)(partition >> 8);
			lower[3] = upper[3] = (unsigned char)partition;
			upper[1] = 1;
			if (++upper[3] == 0)
				upper[2]++;
		}
	}
	result = udb_storage_block_prefixes(cache, file, root, iterator->prefixes);
	if (result != UDB_STORAGE_OK)
		goto fail;
	result = udb_storage_catalog_iterator_create(cache, file, &root->catalog, lower, upper, &iterator->catalog);
	if (result != UDB_STORAGE_OK)
		goto fail;
	*out = iterator;
	return UDB_STORAGE_OK;
fail:
	udb_storage_block_iterator_close(&iterator);
	return result;
}

static UdbStorageResult udb_storage_block_iterator_next(UdbStorageBlockIterator *iterator, UdbStorageRecord *record)
{
	UdbStorageResult result;
	unsigned char key[64], value[128];
	if (!iterator || !record || udb_storage_overlaps(record, sizeof(*record), iterator, sizeof(*iterator)))
		return UDB_STORAGE_ERROR;
	if (iterator->failed)
		return iterator->failed;
	if (iterator->finished)
		return UDB_STORAGE_NOT_FOUND;
	for (;;)
	{
		UdbStorageLogicalPosition position;
		UdbStorageNode node;
		if (iterator->view)
		{
			result = udb_storage_view_next_record(iterator->view, record);
			if (result == UDB_STORAGE_OK)
				return result;
			if (result != UDB_STORAGE_NOT_FOUND)
				return iterator->failed = result;
			result = udb_storage_view_close(&iterator->view);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
		}
		result = udb_storage_catalog_iterator_next(iterator->catalog, key, value);
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			iterator->finished = 1;
			return result;
		}
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
		result = udb_storage_node_position_decode(key, &position);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_node_decode(&position, value, &node);
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
		if (node.kind != UDB_STORAGE_LEAF || !node.summary.count)
			continue;
		result = udb_storage_block_points(iterator->cache, iterator->file, &iterator->root, &node, iterator->points);
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
		result = udb_storage_view_create_indexed(
			iterator->cache, iterator->points, node.run_count, iterator->prefixes, iterator->root.prefix_count,
			iterator->has_prefix ? &iterator->prefix : NULL, iterator->root.watermark, &iterator->view);
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
	}
}

static UdbStorageResult udb_storage_block_exists_prefix(UdbStorageCache *cache, UdbStorageFile *file,
														const UdbStorageBlockRoot *root, const UdbStorageRow *prefix)
{
	UdbStorageBlockIterator *iterator;
	UdbStorageRecord record;
	UdbStorageResult result, closed;
	if (!prefix)
		return UDB_STORAGE_ERROR;
	result = udb_storage_block_iterator_create(cache, file, root, prefix, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_block_iterator_next(iterator, &record);
	closed = udb_storage_block_iterator_close(&iterator);
	return closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND ? closed : result;
}

static UdbStorageResult udb_storage_block_partition_iterator(UdbStorageCache *cache, UdbStorageFile *file,
															 const UdbStorageBlockRoot *root, unsigned int partition,
															 UdbStorageBlockIterator **out)
{
	UdbStorageResult result;
	unsigned char lower[64] = {0}, upper[64] = {0};
	if (!out || partition >= UDB_STORAGE_BASE_PARTITIONS)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	result = udb_storage_block_iterator_create(cache, file, root, NULL, out);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_catalog_iterator_close(&(*out)->catalog);
	if (result != UDB_STORAGE_OK)
		goto fail;
	lower[0] = upper[0] = root->block;
	lower[1] = upper[1] = 1;
	lower[2] = upper[2] = (unsigned char)(partition >> 8);
	lower[3] = upper[3] = (unsigned char)partition;
	if (++upper[3] == 0)
		upper[2]++;
	result = udb_storage_catalog_iterator_create(cache, file, &root->catalog, lower, upper, &(*out)->catalog);
	if (result == UDB_STORAGE_OK)
		return result;
fail:
	udb_storage_block_iterator_close(out);
	return result;
}
