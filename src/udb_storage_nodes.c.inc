/*
 * UDB 4 native storage: typed radix nodes and immutable run manifest records.
 * Namespace 1 stores nodes, 2 point runs and 3 prefix runs. Missing nodes may
 * represent implicit empty subtrees only after complete logical-root validation;
 * missing advertised runs are corruption. Changes return private COW roots.
 */

static void udb_storage_node_key(const UdbStorageLogicalPosition *position, unsigned char key[64])
{
	memset(key, 0, 64);
	key[0] = position->block;
	key[1] = 1;
	key[2] = (unsigned char)(position->partition >> 8);
	key[3] = (unsigned char)position->partition;
	key[4] = (unsigned char)(position->depth >> 8);
	key[5] = (unsigned char)position->depth;
	memcpy(key + 6, position->prefix, 32);
}

static int udb_storage_node_valid(const UdbStorageLogicalPosition *position, const UdbStorageNode *node)
{
	UdbStorageLogicalSummary empty;
	if (!udb_storage_logical_position_valid(position) || !node || !udb_storage_logical_summary_valid(&node->summary) ||
		node->run_count > UDB_STORAGE_MAX_RUNS + 1)
		return 0;
	if (node->kind == UDB_STORAGE_LEAF)
	{
		if (node->summary.weight > UDB_STORAGE_LEAF_WEIGHT_MAX || (node->summary.count && !node->run_count))
			return 0;
		if (!node->summary.count && (udb_storage_logical_empty(position, &empty) != UDB_STORAGE_OK ||
									 memcmp(empty.digest, node->summary.digest, sizeof(empty.digest))))
			return 0;
	}
	else if (node->kind == UDB_STORAGE_BRANCH)
	{
		if (node->run_count || position->depth == 256 || node->summary.weight <= UDB_STORAGE_LEAF_WEIGHT_MAX)
			return 0;
	}
	else
		return 0;
	for (unsigned int i = 0; i < UDB_STORAGE_MAX_RUNS + 1; i++)
	{
		if (i >= node->run_count)
		{
			if (node->runs[i])
				return 0;
			continue;
		}
		if (!node->runs[i])
			return 0;
		for (unsigned int j = 0; j < i; j++)
			if (node->runs[i] == node->runs[j])
				return 0;
	}
	return 1;
}

static UdbStorageResult udb_storage_node_encode(const UdbStorageLogicalPosition *position, const UdbStorageNode *node,
												unsigned char value[128])
{
	if (!value || !udb_storage_node_valid(position, node) || udb_storage_overlaps(value, 128, node, sizeof(*node)))
		return UDB_STORAGE_ERROR;
	memset(value, 0, 128);
	value[0] = (unsigned char)node->kind;
	value[1] = (unsigned char)node->run_count;
	memcpy(value + 8, node->summary.digest, 32);
	udb_storage_put_u64(value + 40, node->summary.count);
	udb_storage_put_u64(value + 48, node->summary.weight);
	for (unsigned int i = 0; i < node->run_count; i++)
		udb_storage_put_u64(value + 56 + 8 * i, node->runs[i]);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_node_decode(const UdbStorageLogicalPosition *position,
												const unsigned char value[128], UdbStorageNode *node)
{
	UdbStorageNode candidate;
	if (!value || !node)
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 2; i < 8; i++)
		if (value[i])
			return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.kind = (UdbStorageNodeKind)value[0];
	candidate.run_count = value[1];
	memcpy(candidate.summary.digest, value + 8, 32);
	candidate.summary.count = udb_storage_get_u64(value + 40);
	candidate.summary.weight = udb_storage_get_u64(value + 48);
	for (unsigned int i = 0; i < UDB_STORAGE_MAX_RUNS + 1; i++)
		candidate.runs[i] = udb_storage_get_u64(value + 56 + 8 * i);
	if (!udb_storage_node_valid(position, &candidate))
		return UDB_STORAGE_CORRUPT;
	*node = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_node_get(UdbStorageCache *cache, UdbStorageFile *file,
											 const UdbStorageCatalogRoot *catalog,
											 const UdbStorageLogicalPosition *position, UdbStorageNode *node)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (!node || !udb_storage_logical_position_valid(position))
		return UDB_STORAGE_ERROR;
	udb_storage_node_key(position, key);
	result = udb_storage_catalog_get(cache, file, catalog, key, value);
	return result == UDB_STORAGE_OK ? udb_storage_node_decode(position, value, node) : result;
}

static UdbStorageResult udb_storage_node_change(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *catalog, uint64_t generation,
												const UdbStorageLogicalPosition *position, const UdbStorageNode *node,
												UdbStorageCatalogRoot *out)
{
	unsigned char key[64], value[128];
	UdbStorageResult result = udb_storage_node_encode(position, node, value);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_node_key(position, key);
	return udb_storage_catalog_change(cache, file, catalog, generation, key, value, out);
}

static UdbStorageResult udb_storage_node_locate(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *catalog, const UdbStorageRow *key,
												UdbStorageLogicalPosition *position, UdbStorageNode *node)
{
	UdbStorageLogicalPosition current;
	UdbStorageNode candidate;
	UdbStorageResult result;
	unsigned char hash[32];
	unsigned int partition;
	if (!position || !node || udb_storage_overlaps(position, sizeof(*position), node, sizeof(*node)))
		return UDB_STORAGE_ERROR;
	result = udb_storage_key_hash(key, hash, &partition);
	if (result != UDB_STORAGE_OK)
		return result;
	memset(&current, 0, sizeof(current));
	current.block = key->block;
	current.partition = (uint16_t)partition;
	for (;;)
	{
		result = udb_storage_node_get(cache, file, catalog, &current, &candidate);
		if (result != UDB_STORAGE_OK)
		{
			if (result == UDB_STORAGE_NOT_FOUND)
				*position = current;
			return result;
		}
		if (candidate.kind == UDB_STORAGE_LEAF)
		{
			*position = current;
			*node = candidate;
			return UDB_STORAGE_OK;
		}
		result =
			udb_storage_logical_child(&current, (hash[current.depth / 8] >> (7 - current.depth % 8)) & 1, &current);
		if (result != UDB_STORAGE_OK)
			return result;
	}
}

static void udb_storage_run_manifest_key(unsigned char block, UdbStorageStream stream, uint64_t id,
										 unsigned char key[64])
{
	memset(key, 0, 64);
	key[0] = block;
	key[1] = stream == UDB_STORAGE_POINTS ? 2 : 3;
	for (unsigned int i = 0; i < 8; i++)
		key[2 + i] = (unsigned char)(id >> (56 - 8 * i));
}

static UdbStorageResult udb_storage_run_save(UdbStorageCache *cache, const UdbStorageRun *run,
											 const UdbStorageCatalogRoot *catalog, uint64_t generation,
											 UdbStorageCatalogRoot *out)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (!cache || !run || !run->file || !run->file->opened || run->file->cache != cache ||
		!udb_storage_block_valid(run->block) ||
		(run->stream != UDB_STORAGE_POINTS && run->stream != UDB_STORAGE_PREFIXES) ||
		generation < run->directory.generation)
		return UDB_STORAGE_ERROR;
	result = udb_storage_catalog_root_encode(&run->directory, value, sizeof(value));
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_run_manifest_key(run->block, run->stream, run->directory.object, key);
	return udb_storage_catalog_change(cache, run->file, catalog, generation, key, value, out);
}

static UdbStorageResult udb_storage_run_load(UdbStorageCache *cache, UdbStorageFile *file,
											 const UdbStorageCatalogRoot *catalog, unsigned char block,
											 UdbStorageStream stream, uint64_t id, UdbStorageRun *run)
{
	unsigned char key[64], value[128];
	UdbStorageRun candidate;
	UdbStorageResult result;
	if (!run || !id || !udb_storage_block_valid(block) || !udb_storage_catalog_root_valid(catalog) ||
		(stream != UDB_STORAGE_POINTS && stream != UDB_STORAGE_PREFIXES))
		return UDB_STORAGE_ERROR;
	udb_storage_run_manifest_key(block, stream, id, key);
	result = udb_storage_catalog_get(cache, file, catalog, key, value);
	if (result == UDB_STORAGE_NOT_FOUND)
		return UDB_STORAGE_CORRUPT;
	if (result != UDB_STORAGE_OK)
		return result;
	memset(&candidate, 0, sizeof(candidate));
	result = udb_storage_catalog_root_decode(value, sizeof(value), &candidate.directory);
	if (result != UDB_STORAGE_OK)
		return result;
	if (candidate.directory.object != id || candidate.directory.generation > catalog->generation)
		return UDB_STORAGE_CORRUPT;
	candidate.file = file;
	candidate.block = block;
	candidate.stream = stream;
	*run = candidate;
	return UDB_STORAGE_OK;
}
