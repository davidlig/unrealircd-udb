/* Complete, owner-exclusive validation of immutable logical block roots.
 * Physical scans and merge pins bypass cache admission. Depth-bounded frames
 * replace recursion and neither catalog nor keys are materialized in memory.
 * A root becomes eligible for lookup only after this operation succeeds. */

typedef struct UdbStorageValidationFrame
{
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageLogicalSummary left;
	unsigned int phase;
} UdbStorageValidationFrame;

typedef struct UdbStorageValidation
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	const UdbStorageBlockRoot *root;
	UdbStorageRecordValidator validator;
	void *context;
	UdbStorageRun prefixes[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun points[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageValidationFrame frames[257];
	unsigned char bytes[UDB_STORAGE_ROW_MAX];
	uint64_t catalog_nodes;
	uint64_t visited_nodes;
} UdbStorageValidation;

static int udb_storage_summary_equal(const UdbStorageLogicalSummary *a, const UdbStorageLogicalSummary *b)
{
	return a->count == b->count && a->weight == b->weight && !memcmp(a->digest, b->digest, 32);
}

static int udb_storage_block_root_valid(const UdbStorageBlockRoot *root)
{
	if (!root || !udb_storage_block_valid(root->block) || !root->generation ||
		!udb_storage_catalog_root_valid(&root->catalog) || root->catalog.generation > root->generation ||
		!udb_storage_logical_summary_valid(&root->summary) || root->prefix_count > UDB_STORAGE_MAX_RUNS + 1)
		return 0;
	for (unsigned int i = 0; i < UDB_STORAGE_MAX_RUNS + 1; i++)
	{
		if (i >= root->prefix_count)
		{
			if (root->prefixes[i])
				return 0;
			continue;
		}
		if (!root->prefixes[i])
			return 0;
		for (unsigned int j = 0; j < i; j++)
			if (root->prefixes[j] == root->prefixes[i])
				return 0;
	}
	return 1;
}

static UdbStorageResult udb_storage_validation_catalog(const unsigned char *key, const unsigned char *value,
													   void *opaque)
{
	UdbStorageValidation *validation = opaque;
	unsigned char canonical[64];
	if (key[0] != validation->root->block)
		return UDB_STORAGE_CORRUPT;
	if (key[1] == 1)
	{
		UdbStorageLogicalPosition position;
		UdbStorageNode node;
		memset(&position, 0, sizeof(position));
		position.block = key[0];
		position.partition = (uint16_t)((key[2] << 8) | key[3]);
		position.depth = (uint16_t)((key[4] << 8) | key[5]);
		memcpy(position.prefix, key + 6, 32);
		if (!udb_storage_logical_position_valid(&position))
			return UDB_STORAGE_CORRUPT;
		udb_storage_node_key(&position, canonical);
		if (memcmp(canonical, key, 64) || udb_storage_node_decode(&position, value, &node) != UDB_STORAGE_OK ||
			validation->catalog_nodes == UINT64_MAX)
			return UDB_STORAGE_CORRUPT;
		validation->catalog_nodes++;
		return UDB_STORAGE_OK;
	}
	if (key[1] == 2 || key[1] == 3)
	{
		UdbStorageRun run;
		uint64_t id = 0;
		UdbStorageResult result;
		memset(&run, 0, sizeof(run));
		for (unsigned int i = 0; i < 8; i++)
			id = (id << 8) | key[2 + i];
		if (!id)
			return UDB_STORAGE_CORRUPT;
		run.block = key[0];
		run.stream = key[1] == 2 ? UDB_STORAGE_POINTS : UDB_STORAGE_PREFIXES;
		run.file = validation->file;
		udb_storage_run_manifest_key(run.block, run.stream, id, canonical);
		if (memcmp(canonical, key, 64))
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_catalog_root_decode(value, 128, &run.directory);
		if (result != UDB_STORAGE_OK)
			return result;
		if (run.directory.object != id || run.directory.generation > validation->root->catalog.generation)
			return UDB_STORAGE_CORRUPT;
		return udb_storage_run_validate(validation->file, &run, validation->root->watermark, validation->validator,
										validation->context);
	}
	return UDB_STORAGE_CORRUPT;
}

static UdbStorageResult udb_storage_validation_placement(const UdbStorageRecord *record, void *opaque)
{
	const UdbStorageLogicalPosition *position = opaque;
	unsigned char hash[32];
	unsigned int partition;
	UdbStorageResult result = udb_storage_key_hash(&record->row, hash, &partition);
	if (result != UDB_STORAGE_OK)
		return result == UDB_STORAGE_ERROR ? UDB_STORAGE_CORRUPT : result;
	return udb_storage_logical_contains(position, hash, partition) ? UDB_STORAGE_OK : UDB_STORAGE_CORRUPT;
}

static UdbStorageResult udb_storage_validation_leaf(UdbStorageValidation *validation,
													const UdbStorageValidationFrame *frame,
													UdbStorageLogicalSummary *summary)
{
	UdbStorageView *view = NULL;
	UdbStorageLeafHasher *hasher = NULL;
	UdbStorageResult result;
	UdbStorageRow row;
	size_t length;
	for (unsigned int i = 0; i < frame->node.run_count; i++)
	{
		result =
			udb_storage_run_load(validation->cache, validation->file, &validation->root->catalog, frame->position.block,
								 UDB_STORAGE_POINTS, frame->node.runs[i], validation->points + i);
		if (result != UDB_STORAGE_OK)
			return result;
		/* Placement applies also to hidden exact deletes and superseded PUTs. */
		result = udb_storage_run_validate(validation->file, validation->points + i, validation->root->watermark,
										  udb_storage_validation_placement, (void *)&frame->position);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	result = udb_storage_leaf_hasher_create(validation->cache->budget, &frame->position, &hasher);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_view_create_indexed(validation->cache, validation->points, frame->node.run_count,
											 validation->prefixes, validation->root->prefix_count, NULL,
											 validation->root->watermark, &view);
	if (result != UDB_STORAGE_OK)
		goto done;
	while ((result = udb_storage_view_next(view, &row)) == UDB_STORAGE_OK)
	{
		result = udb_storage_row_encode(&row, validation->bytes, sizeof(validation->bytes), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_leaf_hasher_add(hasher, validation->bytes, length);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = udb_storage_leaf_hasher_finish(hasher, summary);
done:
	if (view)
	{
		UdbStorageResult closed = udb_storage_view_close(&view);
		if (closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND)
			result = closed;
	}
	udb_storage_leaf_hasher_free(&hasher);
	return result;
}

static UdbStorageResult udb_storage_validation_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
														 void *opaque)
{
	UdbStorageValidation *validation = opaque;
	unsigned int depth = 0;
	UdbStorageLogicalSummary completed;
	UdbStorageResult result;
	UdbStorageValidationFrame *frame = validation->frames;
	memset(frame, 0, sizeof(*frame));
	frame->position.block = validation->root->block;
	frame->position.partition = (uint16_t)partition;
	for (;;)
	{
		frame = validation->frames + depth;
		if (!frame->phase)
		{
			result = udb_storage_node_get(validation->cache, validation->file, &validation->root->catalog,
										  &frame->position, &frame->node);
			if (result == UDB_STORAGE_NOT_FOUND)
				result = udb_storage_logical_empty(&frame->position, &completed);
			else if (result == UDB_STORAGE_OK)
			{
				if (validation->visited_nodes == UINT64_MAX)
					return UDB_STORAGE_CORRUPT;
				validation->visited_nodes++;
				if (frame->node.kind == UDB_STORAGE_BRANCH)
				{
					UdbStorageValidationFrame *child;
					if (depth == 256)
						return UDB_STORAGE_CORRUPT;
					frame->phase = 1;
					child = validation->frames + ++depth;
					memset(child, 0, sizeof(*child));
					result = udb_storage_logical_child(&frame->position, 0, &child->position);
					if (result != UDB_STORAGE_OK)
						return result;
					continue;
				}
				result = udb_storage_validation_leaf(validation, frame, &completed);
				if (result == UDB_STORAGE_OK && !udb_storage_summary_equal(&completed, &frame->node.summary))
					return UDB_STORAGE_CORRUPT;
			}
			if (result != UDB_STORAGE_OK)
				return result;
		}
		else
		{
			result = udb_storage_logical_branch(&frame->position, &frame->left, &completed, &completed);
			if (result != UDB_STORAGE_OK)
				return result == UDB_STORAGE_ERROR ? UDB_STORAGE_CORRUPT : result;
			if (!udb_storage_summary_equal(&completed, &frame->node.summary))
				return UDB_STORAGE_CORRUPT;
		}
		if (!depth)
		{
			*summary = completed;
			return UDB_STORAGE_OK;
		}
		frame = validation->frames + --depth;
		if (frame->phase == 1)
		{
			UdbStorageValidationFrame *child = validation->frames + depth + 1;
			frame->left = completed;
			frame->phase = 2;
			memset(child, 0, sizeof(*child));
			result = udb_storage_logical_child(&frame->position, 1, &child->position);
			if (result != UDB_STORAGE_OK)
				return result;
			depth++;
		}
	}
}

static UdbStorageResult udb_storage_block_validate(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageBlockRoot *root, UdbStorageRecordValidator validator,
												   void *context)
{
	UdbStorageValidation *validation;
	UdbStorageLogicalSummary summary;
	UdbStorageResult result;
	if (!cache || !file || file->cache != cache || !udb_storage_block_root_valid(root))
		return UDB_STORAGE_ERROR;
	/* Caller owns/pins immutable input; other readers may use the cache. */
	validation = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*validation));
	if (!validation)
		return UDB_STORAGE_LIMIT;
	validation->cache = cache;
	validation->file = file;
	validation->root = root;
	validation->validator = validator;
	validation->context = context;
	udb_storage_mutex_lock(&cache->mutex);
	if (cache->bypass_admission)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		udb_storage_free(validation);
		return UDB_STORAGE_BUSY;
	}
#ifdef _WIN32
	cache->validation_thread = GetCurrentThreadId();
#else
	cache->validation_thread = pthread_self();
#endif
	cache->bypass_admission = 1;
	udb_storage_mutex_unlock(&cache->mutex);
	result = udb_storage_catalog_validate(file, &root->catalog, udb_storage_validation_catalog, validation);
	if (result != UDB_STORAGE_OK)
		goto done;
	for (unsigned int i = 0; i < root->prefix_count; i++)
	{
		result = udb_storage_run_load(cache, file, &root->catalog, root->block, UDB_STORAGE_PREFIXES, root->prefixes[i],
									  validation->prefixes + i);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	result = udb_storage_logical_block(root->block, udb_storage_validation_partition, validation, &summary);
	if (result == UDB_STORAGE_OK && (!udb_storage_summary_equal(&summary, &root->summary) ||
									 validation->visited_nodes != validation->catalog_nodes))
		result = UDB_STORAGE_CORRUPT;
done:
	udb_storage_mutex_lock(&cache->mutex);
	cache->bypass_admission = 0;
	udb_storage_mutex_unlock(&cache->mutex);
	udb_storage_free(validation);
	return result;
}

static UdbStorageResult udb_storage_block_object_bound(UdbStorageCache *cache, UdbStorageFile *file,
													   const UdbStorageBlockRoot *root, uint64_t next_object)
{
	UdbStorageCatalogIterator *iterator = NULL;
	UdbStorageResult result, closed;
	unsigned char lower[64] = {0}, upper[64] = {0}, key[64], value[128];
	if (next_object < 8)
		return UDB_STORAGE_CORRUPT;
	lower[0] = upper[0] = root->block;
	lower[1] = 2;
	upper[1] = 4;
	result = udb_storage_catalog_iterator_create(cache, file, &root->catalog, lower, upper, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	while ((result = udb_storage_catalog_iterator_next(iterator, key, value)) == UDB_STORAGE_OK)
	{
		uint64_t id = 0;
		for (unsigned int i = 2; i < 10; i++)
			id = (id << 8) | key[i];
		if (id < 8 || id >= next_object)
		{
			result = UDB_STORAGE_CORRUPT;
			break;
		}
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = UDB_STORAGE_OK;
	closed = udb_storage_catalog_iterator_close(&iterator);
	return result == UDB_STORAGE_OK ? closed : result;
}
