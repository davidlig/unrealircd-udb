/* Checkpoint-gated reachability and extent reuse. Only metadata is traversed:
 * catalog pages and complete immutable fragment extents are marked. Fixed-size
 * paged bitmaps live in exclusive private files, with one charged chunk per
 * block, never a resident per-record or per-page full-store allocation. */

#define UDB_STORAGE_MAP_HEADER 48
#define UDB_STORAGE_MAP_BYTES (UDB_STORAGE_PAGE_PAYLOAD - UDB_STORAGE_MAP_HEADER)
#define UDB_STORAGE_MAP_BITS ((uint64_t)UDB_STORAGE_MAP_BYTES * 8)

typedef struct UdbStorageFreeMap
{
	UdbStorageFile file;
	UdbStorageDirectory *directory;
	char name[64];
	unsigned char payload[UDB_STORAGE_PAGE_PAYLOAD];
	unsigned char bytes[UDB_STORAGE_PAGE_SIZE];
	unsigned char identity[16];
	uint64_t generation;
	uint64_t pages;
	uint64_t chunk;
	uint64_t cursor[2];
	unsigned int block;
	int loaded;
	int dirty;
	int created;
} UdbStorageFreeMap;

typedef struct UdbStorageReclaimer
{
	UdbStorageEngine *engine;
	UdbStorageFreeMap maps[6];
	unsigned int marking;
} UdbStorageReclaimer;

typedef struct UdbStorageReclaimPin
{
	struct UdbStorageReclaimPin *next;
	UdbStorageGeneration *generation;
} UdbStorageReclaimPin;

typedef UdbStorageResult (*UdbStorageMarkValue)(UdbStorageReclaimer *reclaimer, unsigned int block,
												const unsigned char key[64], const unsigned char value[128],
												void *context);

static UdbStorageResult udb_storage_map_flush(UdbStorageFreeMap *map)
{
	UdbStoragePage page = {0};
	UdbStorageResult result;
	if (!map->loaded || !map->dirty)
		return UDB_STORAGE_OK;
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.generation = map->generation;
	page.object = map->block + 1;
	page.ordinal = map->chunk;
	page.payload.data = map->payload;
	page.payload.length = sizeof(map->payload);
	result = udb_storage_page_encode(&page, map->bytes, sizeof(map->bytes));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(&map->file, map->chunk * UDB_STORAGE_PAGE_SIZE, map->bytes, sizeof(map->bytes));
	if (result == UDB_STORAGE_OK)
		map->dirty = 0;
	return result;
}

static UdbStorageResult udb_storage_map_load(UdbStorageFreeMap *map, uint64_t chunk)
{
	UdbStorageResult result;
	UdbStoragePage page;
	if (map->loaded && map->chunk == chunk)
		return UDB_STORAGE_OK;
	result = udb_storage_map_flush(map);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_file_read(&map->file, chunk * UDB_STORAGE_PAGE_SIZE, map->bytes, sizeof(map->bytes));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_page_decode(map->bytes, sizeof(map->bytes), &page);
	if (result != UDB_STORAGE_OK)
		return result;
	if (page.kind != UDB_STORAGE_PAGE_METADATA || page.generation != map->generation || page.object != map->block + 1 ||
		page.ordinal != chunk || page.payload.length != sizeof(map->payload) ||
		memcmp(page.payload.data, "UDBMAP4\0", 8) || memcmp(page.payload.data + 8, map->identity, 16) ||
		udb_storage_get_u64(page.payload.data + 24) != map->pages || !udb_storage_zero(page.payload.data + 32, 16))
		return UDB_STORAGE_CORRUPT;
	memcpy(map->payload, page.payload.data, sizeof(map->payload));
	map->chunk = chunk;
	map->loaded = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_map_mark(UdbStorageFreeMap *map, uint64_t offset, uint64_t length)
{
	uint64_t first, count;
	UdbStorageResult result;
	if (offset % UDB_STORAGE_PAGE_SIZE || !length || length % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	first = offset / UDB_STORAGE_PAGE_SIZE;
	count = length / UDB_STORAGE_PAGE_SIZE;
	if (first >= map->pages || count > map->pages - first)
		return UDB_STORAGE_CORRUPT;
	while (count--)
	{
		uint64_t chunk = first / UDB_STORAGE_MAP_BITS, bit = first % UDB_STORAGE_MAP_BITS;
		result = udb_storage_map_load(map, chunk);
		if (result != UDB_STORAGE_OK)
			return result;
		map->payload[UDB_STORAGE_MAP_HEADER + bit / 8] |= (unsigned char)(1U << (bit % 8));
		map->dirty = 1;
		first++;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_map_reserve(UdbStorageFreeMap *map, unsigned int pages, uint64_t *offset)
{
	unsigned int role = pages == 1 ? 0 : 1;
	uint64_t first = map->cursor[role], available = 0;
	UdbStorageResult result;
	for (uint64_t i = first; i < map->pages; i++)
	{
		uint64_t bit = i % UDB_STORAGE_MAP_BITS;
		result = udb_storage_map_load(map, i / UDB_STORAGE_MAP_BITS);
		if (result != UDB_STORAGE_OK)
			return result;
		if (map->payload[UDB_STORAGE_MAP_HEADER + bit / 8] & (1U << (bit % 8)))
			available = 0;
		else
			available++;
		if (available == pages)
		{
			first = i + 1 - pages;
			result = udb_storage_map_mark(map, first * UDB_STORAGE_PAGE_SIZE, (uint64_t)pages * UDB_STORAGE_PAGE_SIZE);
			if (result != UDB_STORAGE_OK)
				return result;
			map->cursor[role] = i + 1;
			*offset = first * UDB_STORAGE_PAGE_SIZE;
			return UDB_STORAGE_OK;
		}
	}
	map->cursor[role] = map->pages;
	return UDB_STORAGE_NOT_FOUND;
}

static UdbStorageResult udb_storage_file_reserve_fragment(UdbStorageFile *file, unsigned int *pages, uint64_t *offset)
{
	UdbStorageFreeMap *map;
	UdbStorageResult result;
	uint64_t available = 0, first = 0;
	if (!file || !file->opened || !pages || !offset || *pages != 64)
		return UDB_STORAGE_ERROR;
	map = file->free_map;
	if (map)
	{
		for (uint64_t i = map->cursor[1]; i <= map->pages; i++)
		{
			int used = 1;
			if (i < map->pages)
			{
				result = udb_storage_map_load(map, i / UDB_STORAGE_MAP_BITS);
				if (result != UDB_STORAGE_OK)
					return result;
				uint64_t bit = i % UDB_STORAGE_MAP_BITS;
				used = (map->payload[UDB_STORAGE_MAP_HEADER + bit / 8] & (1U << (bit % 8))) != 0;
			}
			if (!used)
			{
				if (!available)
					first = i;
				available++;
			}
			if ((used || available == 64) && available >= 2)
			{
				uint64_t bytes = available * UDB_STORAGE_PAGE_SIZE;
				if (file->allocated_bytes > UINT64_MAX - bytes)
					return UDB_STORAGE_LIMIT;
				result = udb_storage_map_mark(map, first * UDB_STORAGE_PAGE_SIZE, bytes);
				if (result != UDB_STORAGE_OK)
					return result;
				map->cursor[1] = first + available;
				*pages = (unsigned int)available;
				*offset = first * UDB_STORAGE_PAGE_SIZE;
				file->allocated_bytes += bytes;
				return UDB_STORAGE_OK;
			}
			if (used)
				available = 0;
		}
		map->cursor[1] = map->pages;
	}
	return udb_storage_file_reserve(file, *pages, offset);
}

static UdbStorageResult udb_storage_file_reserve(UdbStorageFile *file, unsigned int pages, uint64_t *offset)
{
	UdbStorageResult result = UDB_STORAGE_NOT_FOUND;
	uint64_t length;
	if (!file || !file->opened || !offset || !pages || pages > UDB_STORAGE_FRAGMENT_MAX / UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_ERROR;
	length = (uint64_t)pages * UDB_STORAGE_PAGE_SIZE;
	if (file->allocated_bytes > UINT64_MAX - length)
		return UDB_STORAGE_LIMIT;
	if (file->free_map)
		result = udb_storage_map_reserve(file->free_map, pages, offset);
	if (result == UDB_STORAGE_NOT_FOUND)
		result = udb_storage_file_size(file, offset);
	if (result != UDB_STORAGE_OK)
		return result;
	if (*offset % UDB_STORAGE_PAGE_SIZE || !udb_storage_io_range_valid(*offset, (size_t)length))
		return UDB_STORAGE_CORRUPT;
	file->allocated_bytes += length;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_mark_catalog(UdbStorageReclaimer *reclaimer, unsigned int block,
												 const UdbStorageCatalogRoot *root, UdbStorageMarkValue value,
												 void *context)
{
	UdbStorageEngine *engine = reclaimer->engine;
	UdbStorageCacheEntry *pin = NULL;
	UdbStorageResult result;
	if (!root->count)
		return UDB_STORAGE_OK;
	result = udb_storage_map_mark(reclaimer->maps + block, root->offset, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_catalog_pin(engine->cache, engine->files + block, root, &pin);
	if (result != UDB_STORAGE_OK)
		return result;
	size_t stride = root->height == 1 ? 192 : 88;
	for (unsigned int i = 0; i < pin->page.items; i++)
	{
		const unsigned char *entry = pin->page.payload.data + 16 + (size_t)i * stride;
		if (root->height > 1)
		{
			UdbStorageCatalogRoot child;
			udb_storage_catalog_child_ref(entry, root, &child);
			result = udb_storage_mark_catalog(reclaimer, block, &child, value, context);
		}
		else if (value)
			result = value(reclaimer, block, entry, entry + 64, context);
		if (result != UDB_STORAGE_OK)
			break;
	}
	UdbStorageResult closed = udb_storage_cache_release(engine->cache, pin);
	return result == UDB_STORAGE_OK ? closed : result;
}

static UdbStorageResult udb_storage_mark_fragment(UdbStorageReclaimer *reclaimer, unsigned int block,
												  const unsigned char key[64], const unsigned char value[128],
												  void *opaque)
{
	UdbStorageRun *run = opaque;
	UdbStorageFragment fragment;
	UdbStorageResult result;
	(void)key;
	result = udb_storage_run_descriptor(run, value, &fragment);
	return result == UDB_STORAGE_OK ? udb_storage_map_mark(reclaimer->maps + block, fragment.start, fragment.length)
									: result;
}

static UdbStorageResult udb_storage_mark_manifest(UdbStorageReclaimer *reclaimer, unsigned int block,
												  const unsigned char key[64], const unsigned char value[128],
												  void *context)
{
	UdbStorageRun run = {0};
	UdbStorageResult result;
	(void)context;
	if (key[1] == 1)
		return UDB_STORAGE_OK;
	if (key[0] != udb_storage_blocks[block] || (key[1] != 2 && key[1] != 3))
		return UDB_STORAGE_CORRUPT;
	run.file = reclaimer->engine->files + block;
	run.block = udb_storage_blocks[block];
	run.stream = key[1] == 2 ? UDB_STORAGE_POINTS : UDB_STORAGE_PREFIXES;
	result = udb_storage_catalog_root_decode(value, 128, &run.directory);
	if (result != UDB_STORAGE_OK)
		return result;
	return udb_storage_mark_catalog(reclaimer, block, &run.directory, udb_storage_mark_fragment, &run);
}

typedef struct UdbStorageMarkStage
{
	unsigned char root[UDB_STORAGE_BLOCK_ROOT_SIZE];
	unsigned int part;
	unsigned int block;
	unsigned char owner[8];
} UdbStorageMarkStage;

static UdbStorageResult udb_storage_mark_stage(UdbStorageReclaimer *reclaimer, unsigned int block,
											   const unsigned char key[64], const unsigned char value[128],
											   void *opaque)
{
	UdbStorageMarkStage *stage = opaque;
	UdbStorageBlockRoot root;
	UdbStorageResult result;
	(void)block;
	if (!key[8])
		return stage->part ? UDB_STORAGE_CORRUPT : UDB_STORAGE_OK;
	if (key[8] > 6 || key[9] != stage->part)
		return UDB_STORAGE_CORRUPT;
	if (!stage->part)
	{
		memcpy(stage->owner, key, 8);
		stage->block = key[8] - 1;
	}
	if (stage->block != key[8] - 1 || memcmp(stage->owner, key, 8))
		return UDB_STORAGE_CORRUPT;
	memcpy(stage->root + 128 * stage->part++, value, 128);
	if (stage->part != 3)
		return UDB_STORAGE_OK;
	stage->part = 0;
	result = udb_storage_block_root_decode(stage->root, &root);
	if (result != UDB_STORAGE_OK)
		return result;
	return udb_storage_mark_catalog(reclaimer, stage->block, &root.catalog, udb_storage_mark_manifest, NULL);
}

static UdbStorageResult udb_storage_reclaimer_close(UdbStorageEngine *engine)
{
	UdbStorageReclaimer *reclaimer = engine->reclaimer;
	UdbStorageResult result;
	if (!reclaimer)
		return UDB_STORAGE_OK;
	for (unsigned int i = 0; i < 6; i++)
		engine->files[i].free_map = NULL;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageFreeMap *map = reclaimer->maps + i;
		if (map->file.opened)
		{
			result = udb_storage_file_close(&map->file);
			if (result != UDB_STORAGE_OK)
				return result;
		}
		if (map->created)
		{
			result = udb_storage_file_remove_private(map->directory, map->name);
			if (result != UDB_STORAGE_OK)
				return result;
			map->created = 0;
		}
	}
	udb_storage_free(reclaimer);
	engine->reclaimer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_reclaimer_refresh(UdbStorageEngine *engine)
{
	UdbStorageReclaimPin *pins = NULL;
	UdbStorageReclaimer *reclaimer;
	UdbStorageResult result;
	unsigned char nonce[16];
	char token[33];
	static const char hex[] = "0123456789abcdef";
	if (engine->work || engine->mutation_work)
		return UDB_STORAGE_BUSY;
	/* The durable floor must advance before an older slot loses its extents. */
	result = udb_storage_journal_checkpoint(&engine->journal);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_reclaimer_close(engine);
	if (result != UDB_STORAGE_OK)
		return result;
	reclaimer = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*reclaimer));
	if (!reclaimer)
		return UDB_STORAGE_LIMIT;
	engine->reclaimer = reclaimer;
	reclaimer->engine = engine;
	if (RAND_bytes(nonce, sizeof(nonce)) != 1)
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 0; i < 16; i++)
	{
		token[i * 2] = hex[nonce[i] >> 4];
		token[i * 2 + 1] = hex[nonce[i] & 15];
	}
	token[32] = 0;
	/* Pin a finite inventory. New readers can only pin the already-inventoried
	 * current root, and this writer publishes nothing until marking finishes. */
	udb_storage_mutex_lock(&engine->mutex);
	for (UdbStorageGeneration *generation = engine->generations; generation; generation = generation->next)
	{
		UdbStorageReclaimPin *pin = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*pin));
		if (!pin || generation->references == SIZE_MAX)
		{
			udb_storage_free(pin);
			result = UDB_STORAGE_LIMIT;
			break;
		}
		generation->references++;
		pin->generation = generation;
		pin->next = pins;
		pins = pin;
	}
	udb_storage_mutex_unlock(&engine->mutex);
	if (result != UDB_STORAGE_OK)
		goto done;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageFreeMap *map = reclaimer->maps + i;
		uint64_t size;
		result = udb_storage_file_size(engine->files + i, &size);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (!size || size % UDB_STORAGE_PAGE_SIZE)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		map->pages = size / UDB_STORAGE_PAGE_SIZE;
		map->block = i;
		map->generation = engine->journal.state.generation;
		map->directory = &engine->directory;
		memcpy(map->identity, engine->journal.state.store_identity, 16);
		snprintf(map->name, sizeof(map->name), "stage_%s_%c_m.db", token, udb_storage_blocks[i]);
		result = udb_storage_file_open(map->directory, map->name, 2, &map->file);
		if (result != UDB_STORAGE_OK)
			goto done;
		map->created = 1;
		uint64_t chunks = (map->pages + UDB_STORAGE_MAP_BITS - 1) / UDB_STORAGE_MAP_BITS;
		for (uint64_t chunk = 0; chunk < chunks; chunk++)
		{
			memset(map->payload, 0, sizeof(map->payload));
			memcpy(map->payload, "UDBMAP4", 7);
			memcpy(map->payload + 8, map->identity, 16);
			udb_storage_put_u64(map->payload + 24, map->pages);
			map->chunk = chunk;
			map->loaded = map->dirty = 1;
			result = udb_storage_map_flush(map);
			if (result != UDB_STORAGE_OK)
				goto done;
		}
		result = udb_storage_map_mark(map, 0, UDB_STORAGE_PAGE_SIZE);
		if (result != UDB_STORAGE_OK)
			goto done;
		map->cursor[0] = map->cursor[1] = 1;
	}
	for (UdbStorageReclaimPin *pin = pins; pin; pin = pin->next)
	{
		const UdbStorageState *state = &pin->generation->state;
		for (unsigned int i = 0; i < 6; i++)
		{
			result = udb_storage_mark_catalog(reclaimer, i, &state->blocks[i].catalog, udb_storage_mark_manifest, NULL);
			if (result != UDB_STORAGE_OK)
				goto done;
		}
		UdbStorageMarkStage stage = {0};
		result = udb_storage_mark_catalog(reclaimer, 3, &state->stages, udb_storage_mark_stage, &stage);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (stage.part)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
	}
	for (unsigned int i = 0; i < 6; i++)
	{
		result = udb_storage_map_flush(reclaimer->maps + i);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_file_flush(&reclaimer->maps[i].file);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	/* Only a complete mark may become the writer's allocator inventory. */
	for (unsigned int i = 0; i < 6; i++)
		engine->files[i].free_map = reclaimer->maps + i;
done:
	while (pins)
	{
		UdbStorageReclaimPin *pin = pins;
		pins = pin->next;
		UdbStorageResult released = udb_storage_engine_release(engine, &pin->generation);
		udb_storage_free(pin);
		if (released != UDB_STORAGE_OK)
			result = released;
	}
	return result;
}
