/* Private leaf-local delta updates. Existing runs are immutable. At most nine
 * runs participate in a visible merge; compaction preserves winning stamps.
 * Shape changes replay at most two sorted leaf views, never a resident block. */

typedef struct UdbStorageLeafSource
{
	UdbStorageCache *cache;
	UdbStorageRun points[2][UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun prefixes[UDB_STORAGE_MAX_RUNS + 1];
	unsigned int counts[2];
	unsigned int prefix_count;
	uint64_t watermark;
	unsigned char block;
} UdbStorageLeafSource;

typedef struct UdbStorageLeafSourceCursor
{
	UdbStorageLeafSource *source;
	UdbStorageLogicalPosition position;
	UdbStorageView *views[2];
	UdbStorageRecord rows[2];
	int present[2];
	int advance;
} UdbStorageLeafSourceCursor;

struct UdbStoragePointChange
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageBlockRoot root;
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageRecord record;
	unsigned char record_bytes[UDB_STORAGE_RECORD_MAX];
	unsigned char scratch[UDB_STORAGE_ROW_MAX];
	size_t record_length;
	UdbStorageLeafSource source_context;
	UdbStorageSource source;
	UdbStorageRebuild *rebuild;
	uint64_t next_object;
	uint64_t weight;
	unsigned int phase;
	UdbStorageResult failed;
	int finished;
};

static UdbStorageResult udb_storage_leaf_source_close(void **pointer)
{
	UdbStorageLeafSourceCursor *cursor;
	UdbStorageResult result = UDB_STORAGE_OK, closed;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	cursor = *pointer;
	for (unsigned int i = 0; i < 2; i++)
		if (cursor->views[i])
		{
			closed = udb_storage_view_close(cursor->views + i);
			if (closed != UDB_STORAGE_OK)
				result = closed;
		}
	udb_storage_free(cursor);
	*pointer = NULL;
	return result;
}

static UdbStorageResult udb_storage_leaf_source_open(void *opaque, const UdbStorageLogicalPosition *position,
													 void **out)
{
	UdbStorageLeafSource *source = opaque;
	UdbStorageLeafSourceCursor *cursor;
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!source || !udb_storage_logical_position_valid(position) || position->block != source->block)
		return UDB_STORAGE_ERROR;
	cursor = udb_storage_alloc(source->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*cursor));
	if (!cursor)
		return UDB_STORAGE_LIMIT;
	cursor->source = source;
	cursor->position = *position;
	cursor->advance = -1;
	for (unsigned int i = 0; i < 2; i++)
	{
		result = udb_storage_view_create_indexed(source->cache, source->points[i], source->counts[i], source->prefixes,
												 source->prefix_count, NULL, source->watermark, cursor->views + i);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_view_next_record(cursor->views[i], cursor->rows + i);
		if (result < UDB_STORAGE_NOT_FOUND)
			goto fail;
		cursor->present[i] = result == UDB_STORAGE_OK;
	}
	*out = cursor;
	return UDB_STORAGE_OK;
fail:
{
	void *handle = cursor;
	udb_storage_leaf_source_close(&handle);
}
	return result;
}

static UdbStorageResult udb_storage_leaf_source_next(void *opaque, UdbStorageRecord *record)
{
	UdbStorageLeafSourceCursor *cursor = opaque;
	UdbStorageResult result;
	unsigned char hash[32];
	unsigned int partition;
	if (!cursor || !record)
		return UDB_STORAGE_ERROR;
	for (;;)
	{
		if (cursor->advance >= 0)
		{
			unsigned int i = (unsigned int)cursor->advance;
			result = udb_storage_view_next_record(cursor->views[i], cursor->rows + i);
			if (result < UDB_STORAGE_NOT_FOUND)
				return result;
			cursor->present[i] = result == UDB_STORAGE_OK;
			cursor->advance = -1;
		}
		if (!cursor->present[0] && !cursor->present[1])
			return UDB_STORAGE_NOT_FOUND;
		int selected = cursor->present[0] ? 0 : 1;
		if (cursor->present[0] && cursor->present[1])
		{
			int compare = udb_storage_key_compare(&cursor->rows[0].row, &cursor->rows[1].row);
			if (!compare)
				return UDB_STORAGE_CORRUPT;
			selected = compare < 0 ? 0 : 1;
		}
		cursor->advance = selected;
		result = udb_storage_key_hash(&cursor->rows[selected].row, hash, &partition);
		if (result != UDB_STORAGE_OK)
			return result;
		if (!udb_storage_logical_contains(&cursor->position, hash, partition))
			continue;
		*record = cursor->rows[selected];
		return UDB_STORAGE_OK;
	}
}

static UdbStorageResult udb_storage_point_source(UdbStoragePointChange *change, const UdbStorageNode *left,
												 const UdbStorageNode *right)
{
	UdbStorageLeafSource *source = &change->source_context;
	UdbStorageResult result;
	memset(source, 0, sizeof(*source));
	source->cache = change->cache;
	source->watermark = change->root.watermark;
	source->block = change->root.block;
	source->prefix_count = change->root.prefix_count;
	source->counts[0] = left->run_count;
	result = udb_storage_block_points(change->cache, change->file, &change->root, left, source->points[0]);
	if (result == UDB_STORAGE_OK && right)
	{
		source->counts[1] = right->run_count;
		result = udb_storage_block_points(change->cache, change->file, &change->root, right, source->points[1]);
	}
	if (result == UDB_STORAGE_OK)
		result = udb_storage_block_prefixes(change->cache, change->file, &change->root, source->prefixes);
	if (result != UDB_STORAGE_OK)
		return result;
	change->source.context = source;
	change->source.open = udb_storage_leaf_source_open;
	change->source.next = udb_storage_leaf_source_next;
	change->source.close = udb_storage_leaf_source_close;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_point_close(UdbStoragePointChange **pointer)
{
	UdbStoragePointChange *change;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	change = *pointer;
	if (change->rebuild)
		result = udb_storage_rebuild_close(&change->rebuild);
	udb_storage_free(change);
	*pointer = NULL;
	return result;
}

static UdbStorageResult udb_storage_point_create(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageBlockRoot *root, uint64_t generation,
												 uint64_t next_object, const UdbStorageRecord *record,
												 UdbStoragePointChange **out)
{
	UdbStoragePointChange *change;
	UdbStorageRow old;
	UdbStorageResult result;
	size_t length;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || file->cache != cache || file->append_limit || !udb_storage_block_root_valid(root) ||
		!record || record->row.block != root->block ||
		(record->operation != UDB_STORAGE_PUT && record->operation != UDB_STORAGE_DELETE) ||
		record->stamp.sequence <= root->watermark || generation < root->generation || next_object < 8)
		return UDB_STORAGE_ERROR;
	change = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*change));
	if (!change)
		return UDB_STORAGE_LIMIT;
	change->cache = cache;
	change->file = file;
	change->root = *root;
	change->root.generation = generation;
	change->root.watermark = record->stamp.sequence;
	change->next_object = next_object;
	result =
		udb_storage_record_encode(record, change->record_bytes, sizeof(change->record_bytes), &change->record_length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_record_decode(change->record_bytes, change->record_length, &change->record);
	if (result != UDB_STORAGE_OK)
		goto fail;
	result = udb_storage_node_locate(cache, file, &root->catalog, &record->row, &change->position, &change->node);
	if (result == UDB_STORAGE_NOT_FOUND)
	{
		change->node.kind = UDB_STORAGE_LEAF;
		result = udb_storage_logical_empty(&change->position, &change->node.summary);
	}
	if (result != UDB_STORAGE_OK)
		goto fail;
	change->weight = change->node.summary.weight;
	result = udb_storage_block_lookup(cache, file, root, &record->row, change->scratch, sizeof(change->scratch), &old);
	if (result == UDB_STORAGE_OK)
	{
		unsigned char bytes[UDB_STORAGE_ROW_MAX];
		result = udb_storage_row_encode(&old, bytes, sizeof(bytes), &length);
		if (result != UDB_STORAGE_OK)
			goto fail;
		if (length + UINT64_C(128) > change->weight)
		{
			result = UDB_STORAGE_CORRUPT;
			goto fail;
		}
		change->weight -= length + UINT64_C(128);
	}
	else if (result != UDB_STORAGE_NOT_FOUND)
		goto fail;
	if (record->operation == UDB_STORAGE_PUT)
	{
		result = udb_storage_row_encode(&record->row, change->scratch, sizeof(change->scratch), &length);
		if (result != UDB_STORAGE_OK)
			goto fail;
		if (length + UINT64_C(128) > UINT64_MAX - change->weight)
		{
			result = UDB_STORAGE_LIMIT;
			goto fail;
		}
		change->weight += length + UINT64_C(128);
	}
	if (!change->node.run_count && record->operation == UDB_STORAGE_DELETE)
		change->phase = 5;
	*out = change;
	return UDB_STORAGE_OK;
fail:
	udb_storage_point_close(&change);
	return result;
}

static UdbStorageResult udb_storage_point_rebuild_start(UdbStoragePointChange *change, const UdbStorageNode *left,
														const UdbStorageNode *right)
{
	/* No data output occurs while establishing immutable input views. */
	udb_storage_file_job_limit(change->file, 0);
	UdbStorageResult result = udb_storage_point_source(change, left, right);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_rebuild_create(change->cache, change->file, &change->root, &change->position,
											change->root.generation, change->root.watermark, change->next_object,
											&change->source, &change->rebuild);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_rebuild_defer(change->rebuild);
	return result;
}

static UdbStorageResult udb_storage_point_leaf_hash(UdbStoragePointChange *change)
{
	UdbStorageLeafHasher *hasher = NULL;
	UdbStorageRecord record;
	UdbStorageSource source;
	void *handle = NULL;
	UdbStorageResult result, closed;
	size_t length;
	result = udb_storage_point_source(change, &change->node, NULL);
	if (result != UDB_STORAGE_OK)
		return result;
	source = change->source;
	result = source.open(source.context, &change->position, &handle);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_leaf_hasher_create(change->cache->budget, &change->position, &hasher);
	if (result != UDB_STORAGE_OK)
		goto done;
	while ((result = source.next(handle, &record)) == UDB_STORAGE_OK)
	{
		result = udb_storage_row_encode(&record.row, change->scratch, sizeof(change->scratch), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_leaf_hasher_add(hasher, change->scratch, length);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = udb_storage_leaf_hasher_finish(hasher, &change->node.summary);
	if (result == UDB_STORAGE_OK && change->node.summary.weight != change->weight)
		result = UDB_STORAGE_CORRUPT;
done:
	if (handle)
	{
		closed = source.close(&handle);
		if (result == UDB_STORAGE_OK)
			result = closed;
	}
	udb_storage_leaf_hasher_free(&hasher);
	return result;
}

static UdbStorageResult udb_storage_point_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
													void *opaque)
{
	UdbStoragePointChange *change = opaque;
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

static UdbStorageResult udb_storage_point_append(UdbStoragePointChange *change)
{
	UdbStorageRunBuilder *builder = NULL;
	UdbStorageRun run;
	UdbStorageResult result;
	UdbStorageCatalogRoot changed;
	if (change->next_object == UINT64_MAX || change->node.run_count >= UDB_STORAGE_MAX_RUNS + 1)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_run_builder_create(change->cache, change->file, change->root.block, UDB_STORAGE_POINTS,
											change->root.generation, change->next_object++, &builder);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_builder_add(builder, change->record_bytes, change->record_length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_builder_finish(builder, &run);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_save(change->cache, &run, &change->root.catalog, change->root.generation, &changed);
	udb_storage_run_builder_free(&builder);
	if (result != UDB_STORAGE_OK)
		return result;
	change->root.catalog = changed;
	change->node.runs[change->node.run_count++] = run.directory.object;
	if (change->weight > UDB_STORAGE_LEAF_WEIGHT_MAX)
	{
		/* Retain the old valid summary solely to describe this private run set.
		 * It is never published; rebuild supplies the canonical split shape. */
		result = udb_storage_node_change(change->cache, change->file, &change->root.catalog, change->root.generation,
										 &change->position, &change->node, &changed);
		if (result == UDB_STORAGE_OK)
			change->root.catalog = changed;
		change->phase = 3;
		return result;
	}
	result = udb_storage_point_leaf_hash(change);
	if (result != UDB_STORAGE_OK)
		return result;
	if (change->node.summary.count)
		result = udb_storage_node_change(change->cache, change->file, &change->root.catalog, change->root.generation,
										 &change->position, &change->node, &changed);
	else
	{
		unsigned char key[64];
		for (unsigned int i = 0; i < change->node.run_count; i++)
		{
			udb_storage_run_manifest_key(change->root.block, UDB_STORAGE_POINTS, change->node.runs[i], key);
			result = udb_storage_catalog_change(change->cache, change->file, &change->root.catalog,
												change->root.generation, key, NULL, &changed);
			if (result != UDB_STORAGE_OK)
				return result;
			change->root.catalog = changed;
		}
		udb_storage_node_key(&change->position, key);
		result = udb_storage_catalog_change(change->cache, change->file, &change->root.catalog, change->root.generation,
											key, NULL, &changed);
	}
	if (result == UDB_STORAGE_OK)
		change->root.catalog = changed;
	change->phase = 4;
	return result;
}

static UdbStorageResult udb_storage_point_ancestor(UdbStoragePointChange *change)
{
	UdbStorageLogicalPosition parent, children[2];
	UdbStorageNode nodes[2], branch = {0};
	UdbStorageCatalogRoot changed;
	UdbStorageResult result;
	if (!change->position.depth)
	{
		change->phase = 5;
		return UDB_STORAGE_OK;
	}
	parent = change->position;
	parent.depth--;
	parent.prefix[parent.depth / 8] &= (unsigned char)~(0x80U >> (parent.depth % 8));
	for (unsigned int i = 0; i < 2; i++)
	{
		result = udb_storage_logical_child(&parent, i, children + i);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_node_get(change->cache, change->file, &change->root.catalog, children + i, nodes + i);
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			memset(nodes + i, 0, sizeof(nodes[i]));
			nodes[i].kind = UDB_STORAGE_LEAF;
			result = udb_storage_logical_empty(children + i, &nodes[i].summary);
		}
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (nodes[0].summary.weight > UINT64_MAX - nodes[1].summary.weight)
		return UDB_STORAGE_LIMIT;
	change->position = parent;
	if (nodes[0].summary.weight + nodes[1].summary.weight <= UDB_STORAGE_LEAF_WEIGHT_MAX)
	{
		if (nodes[0].kind != UDB_STORAGE_LEAF || nodes[1].kind != UDB_STORAGE_LEAF)
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_point_rebuild_start(change, nodes, nodes + 1);
		change->phase = 3;
		return result;
	}
	branch.kind = UDB_STORAGE_BRANCH;
	result = udb_storage_logical_branch(&parent, &nodes[0].summary, &nodes[1].summary, &branch.summary);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_node_change(change->cache, change->file, &change->root.catalog, change->root.generation,
										 &parent, &branch, &changed);
	if (result == UDB_STORAGE_OK)
		change->root.catalog = changed;
	return result;
}

static UdbStorageResult udb_storage_point_step(UdbStoragePointChange *change, UdbStorageBlockRoot *root,
											   uint64_t *next_object)
{
	UdbStorageResult result;
	uint64_t size;
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
		change->phase = change->node.run_count == UDB_STORAGE_MAX_RUNS + 1 ? 1 : 2;
		if (change->phase == 1)
		{
			result = udb_storage_point_rebuild_start(change, &change->node, NULL);
			if (result != UDB_STORAGE_OK)
				return change->failed = result;
		}
	}
	if (change->phase == 1 || change->phase == 3)
	{
		if (!change->rebuild)
		{
			result = udb_storage_point_rebuild_start(change, &change->node, NULL);
			if (result != UDB_STORAGE_OK)
				return change->failed = result;
		}
		result = udb_storage_rebuild_step(change->rebuild, &change->root, &change->next_object);
		if (result == UDB_STORAGE_BUSY)
			return result;
		if (result != UDB_STORAGE_OK)
			return change->failed = result;
		result = udb_storage_rebuild_close(&change->rebuild);
		if (result != UDB_STORAGE_OK)
			return change->failed = result;
		if (change->phase == 1)
		{
			result = udb_storage_node_get(change->cache, change->file, &change->root.catalog, &change->position,
										  &change->node);
			if (result == UDB_STORAGE_NOT_FOUND)
			{
				memset(&change->node, 0, sizeof(change->node));
				change->node.kind = UDB_STORAGE_LEAF;
				result = udb_storage_logical_empty(&change->position, &change->node.summary);
			}
			if (result != UDB_STORAGE_OK)
				return change->failed = result;
			change->phase = 2;
		}
		else
			change->phase = 4;
		return UDB_STORAGE_BUSY;
	}
	result = udb_storage_file_size(change->file, &size);
	if (result != UDB_STORAGE_OK)
		return change->failed = result;
	if (!udb_storage_io_range_valid(size, UDB_STORAGE_COMPACTION_MAX))
		return change->failed = UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(change->file, size + UDB_STORAGE_COMPACTION_MAX);
	if (change->phase == 2)
		result = udb_storage_point_append(change);
	else if (change->phase == 4)
		result = udb_storage_point_ancestor(change);
	else
	{
		result =
			udb_storage_logical_block(change->root.block, udb_storage_point_partition, change, &change->root.summary);
		if (result == UDB_STORAGE_OK)
			change->finished = 1;
	}
	udb_storage_file_job_limit(change->file, 0);
	if (result != UDB_STORAGE_OK)
		return change->failed = result;
	if (!change->finished)
		return UDB_STORAGE_BUSY;
	*root = change->root;
	*next_object = change->next_object;
	return UDB_STORAGE_OK;
}
