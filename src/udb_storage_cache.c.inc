/*
 * UDB 4 native storage: charged allocations and a lazy, bounded LRU page cache.
 * Cache -> budget is the lock order. Budget operations never acquire the cache.
 * Metadata/open/close lifetimes are serialized by the owning engine; page pins
 * may be acquired/released concurrently while their file remains attached.
 */

/* Include allocation headers in the budget and preserve scalar alignment. */
typedef union UdbStorageAllocation
{
	struct
	{
		UdbStorageBudget *budget;
		size_t bytes;
		UdbStorageMemoryClass memory_class;
	} info;
	long double alignment;
	void *pointer_alignment;
	uint64_t integer_alignment;
} UdbStorageAllocation;

static size_t udb_storage_allocation_charge(size_t bytes)
{
	return bytes && bytes <= SIZE_MAX - sizeof(UdbStorageAllocation) ? bytes + sizeof(UdbStorageAllocation) : 0;
}

static UdbStorageResult udb_storage_memtable_charge(UdbStorageBudget *budget, size_t bytes)
{
	UdbStorageResult result = UDB_STORAGE_LIMIT;
	if (!budget || !budget->initialized || !bytes)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&budget->mutex);
	if (bytes <= UDB_STORAGE_MEMTABLE_LIMIT - budget->memtable_bytes)
	{
		budget->memtable_bytes += bytes;
		result = UDB_STORAGE_OK;
	}
	udb_storage_mutex_unlock(&budget->mutex);
	return result;
}

static void udb_storage_memtable_uncharge(UdbStorageBudget *budget, size_t bytes)
{
	udb_storage_mutex_lock(&budget->mutex);
	if (bytes > budget->memtable_bytes)
		abort();
	budget->memtable_bytes -= bytes;
	udb_storage_mutex_unlock(&budget->mutex);
}

static int udb_storage_cache_limit_valid(size_t bytes)
{
	return bytes >= UDB_STORAGE_PAGE_SIZE * 2 && bytes <= SIZE_MAX - UDB_STORAGE_AUX_LIMIT;
}

static UdbStorageResult udb_storage_budget_init(UdbStorageBudget *budget, size_t cache_limit)
{
	if (!budget || !udb_storage_cache_limit_valid(cache_limit))
		return UDB_STORAGE_ERROR;
	if (budget->initialized)
		return UDB_STORAGE_BUSY;
	memset(budget, 0, sizeof(*budget));
	if (!udb_storage_mutex_init(&budget->mutex))
		return UDB_STORAGE_ERROR;
	budget->limit[UDB_STORAGE_MEMORY_CACHE] = cache_limit;
	budget->limit[UDB_STORAGE_MEMORY_AUX] = UDB_STORAGE_AUX_LIMIT;
	budget->used[UDB_STORAGE_MEMORY_AUX] = sizeof(*budget);
	budget->peak[UDB_STORAGE_MEMORY_AUX] = sizeof(*budget);
	budget->next_file_identity = 1;
	budget->initialized = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_budget_destroy(UdbStorageBudget *budget)
{
	if (!budget || !budget->initialized)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&budget->mutex);
	if (budget->used[UDB_STORAGE_MEMORY_CACHE] || budget->used[UDB_STORAGE_MEMORY_AUX] != sizeof(*budget) ||
		budget->open_files || budget->cache || budget->memtable_bytes)
	{
		udb_storage_mutex_unlock(&budget->mutex);
		return UDB_STORAGE_BUSY;
	}
	udb_storage_mutex_unlock(&budget->mutex);
	udb_storage_mutex_destroy(&budget->mutex);
	memset(budget, 0, sizeof(*budget));
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_budget_charge(UdbStorageBudget *budget, UdbStorageMemoryClass memory_class,
												  size_t bytes)
{
	UdbStorageResult result = UDB_STORAGE_LIMIT;

	if (!budget || !budget->initialized || (unsigned int)memory_class > UDB_STORAGE_MEMORY_AUX)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&budget->mutex);
	if (bytes <= budget->limit[memory_class] - budget->used[memory_class])
	{
		budget->used[memory_class] += bytes;
		if (budget->used[memory_class] > budget->peak[memory_class])
			budget->peak[memory_class] = budget->used[memory_class];
		result = UDB_STORAGE_OK;
	}
	udb_storage_mutex_unlock(&budget->mutex);
	return result;
}

static void udb_storage_budget_uncharge(UdbStorageBudget *budget, UdbStorageMemoryClass memory_class, size_t bytes)
{
	udb_storage_mutex_lock(&budget->mutex);
	if (budget->used[memory_class] < bytes)
		abort();
	budget->used[memory_class] -= bytes;
	udb_storage_mutex_unlock(&budget->mutex);
}

static void *udb_storage_alloc(UdbStorageBudget *budget, UdbStorageMemoryClass memory_class, size_t bytes)
{
	UdbStorageAllocation *allocation;
	size_t total;

	if (!bytes || bytes > SIZE_MAX - sizeof(*allocation))
		return NULL;
	total = bytes + sizeof(*allocation);
	if (udb_storage_budget_charge(budget, memory_class, total) != UDB_STORAGE_OK)
		return NULL;
	allocation = malloc(total);
	if (!allocation)
	{
		udb_storage_budget_uncharge(budget, memory_class, total);
		return NULL;
	}
	allocation->info.budget = budget;
	allocation->info.bytes = total;
	allocation->info.memory_class = memory_class;
	memset(allocation + 1, 0, bytes);
	return allocation + 1;
}

static void udb_storage_free(void *pointer)
{
	UdbStorageAllocation *allocation;
	UdbStorageBudget *budget;
	UdbStorageMemoryClass memory_class;
	size_t bytes;

	if (!pointer)
		return;
	allocation = (UdbStorageAllocation *)pointer - 1;
	budget = allocation->info.budget;
	memory_class = allocation->info.memory_class;
	bytes = allocation->info.bytes;
	free(allocation);
	/* Release only after free, so another allocator cannot exceed the cap. */
	udb_storage_budget_uncharge(budget, memory_class, bytes);
}

static size_t udb_storage_cache_bucket(uint64_t identity, uint64_t offset, uint64_t generation)
{
	uint64_t hash = identity ^ (offset / UDB_STORAGE_PAGE_SIZE) ^ (generation * UINT64_C(0x9e3779b97f4a7c15));

	hash ^= hash >> 30;
	hash *= UINT64_C(0xbf58476d1ce4e5b9);
	hash ^= hash >> 27;
	hash *= UINT64_C(0x94d049bb133111eb);
	hash ^= hash >> 31;
	return (size_t)hash & (UDB_STORAGE_CACHE_BUCKETS - 1);
}

static void udb_storage_cache_unlink_lru(UdbStorageCache *cache, UdbStorageCacheEntry *entry)
{
	if (entry->older)
		entry->older->newer = entry->newer;
	else
		cache->oldest = entry->newer;
	if (entry->newer)
		entry->newer->older = entry->older;
	else
		cache->newest = entry->older;
	entry->older = NULL;
	entry->newer = NULL;
}

static void udb_storage_cache_make_newest(UdbStorageCache *cache, UdbStorageCacheEntry *entry)
{
	entry->older = cache->newest;
	entry->newer = NULL;
	if (cache->newest)
		cache->newest->newer = entry;
	else
		cache->oldest = entry;
	cache->newest = entry;
}

static void udb_storage_cache_erase(UdbStorageCache *cache, UdbStorageCacheEntry *entry)
{
	size_t bucket = udb_storage_cache_bucket(entry->file_identity, entry->offset, entry->generation);
	UdbStorageCacheEntry **link = &cache->buckets[bucket];

	while (*link && *link != entry)
		link = &(*link)->hash_next;
	if (!*link || entry->pins)
		abort();
	*link = entry->hash_next;
	udb_storage_cache_unlink_lru(cache, entry);
	cache->entries--;
	udb_storage_free(entry);
}

static int udb_storage_cache_evict_one(UdbStorageCache *cache)
{
	for (UdbStorageCacheEntry *entry = cache->oldest; entry; entry = entry->newer)
	{
		if (entry->pins)
			continue;
		udb_storage_cache_erase(cache, entry);
		if (cache->evictions != UINT64_MAX)
			cache->evictions++;
		return 1;
	}
	return 0;
}

static UdbStorageResult udb_storage_cache_create(UdbStorageBudget *budget, UdbStorageCache **out)
{
	UdbStorageCache *cache;

	if (!budget || !budget->initialized || !out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	/* Creation/destruction is owner-serialized before/after admission. */
	if (budget->cache || budget->open_files)
		return UDB_STORAGE_BUSY;
	cache = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, sizeof(*cache));
	if (!cache)
		return UDB_STORAGE_LIMIT;
	cache->budget = budget;
	if (!udb_storage_mutex_init(&cache->mutex))
	{
		udb_storage_free(cache);
		return UDB_STORAGE_ERROR;
	}
	cache->buckets =
		udb_storage_alloc(budget, UDB_STORAGE_MEMORY_CACHE, UDB_STORAGE_CACHE_BUCKETS * sizeof(*cache->buckets));
	if (!cache->buckets)
	{
		udb_storage_mutex_destroy(&cache->mutex);
		udb_storage_free(cache);
		return UDB_STORAGE_LIMIT;
	}
	budget->cache = cache;
	*out = cache;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_destroy(UdbStorageCache **pointer)
{
	UdbStorageCache *cache;

	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	cache = *pointer;
	udb_storage_mutex_lock(&cache->mutex);
	if (cache->files || cache->budget->open_files)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_BUSY;
	}
	for (UdbStorageCacheEntry *entry = cache->oldest; entry; entry = entry->newer)
	{
		if (entry->pins)
		{
			udb_storage_mutex_unlock(&cache->mutex);
			return UDB_STORAGE_BUSY;
		}
	}
	while (cache->oldest)
		udb_storage_cache_erase(cache, cache->oldest);
	udb_storage_mutex_unlock(&cache->mutex);
	udb_storage_mutex_destroy(&cache->mutex);
	cache->budget->cache = NULL;
	udb_storage_free(cache->buckets);
	udb_storage_free(cache);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_resize(UdbStorageCache *cache, size_t bytes)
{
	size_t used;

	if (!cache || !udb_storage_cache_limit_valid(bytes))
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&cache->mutex);
	for (;;)
	{
		udb_storage_mutex_lock(&cache->budget->mutex);
		used = cache->budget->used[UDB_STORAGE_MEMORY_CACHE];
		if (used <= bytes)
		{
			cache->budget->limit[UDB_STORAGE_MEMORY_CACHE] = bytes;
			udb_storage_mutex_unlock(&cache->budget->mutex);
			break;
		}
		udb_storage_mutex_unlock(&cache->budget->mutex);
		if (!udb_storage_cache_evict_one(cache))
		{
			udb_storage_mutex_unlock(&cache->mutex);
			return UDB_STORAGE_BUSY;
		}
	}
	udb_storage_mutex_unlock(&cache->mutex);
	return UDB_STORAGE_OK;
}

static void udb_storage_cache_attach_file(UdbStorageCache *cache, UdbStorageFile *file)
{
	udb_storage_mutex_lock(&cache->mutex);
	if (file->cache || file->directory->budget != cache->budget)
		abort();
	file->cache = cache;
	file->cache_next = cache->files;
	cache->files = file;
	udb_storage_mutex_unlock(&cache->mutex);
}

static UdbStorageResult udb_storage_cache_acquire(UdbStorageCache *cache, UdbStorageFile *file, uint64_t offset,
												  uint64_t generation, UdbStorageCacheEntry **out)
{
	UdbStorageCacheEntry *entry;
	UdbStoragePage page;
	UdbStorageResult result;
	int bypass;
	size_t bucket, available;
	const size_t charge = sizeof(UdbStorageCacheEntry) + sizeof(UdbStorageAllocation);

	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || !file->opened || file->cache != cache || !generation || offset % UDB_STORAGE_PAGE_SIZE ||
		!udb_storage_io_range_valid(offset, UDB_STORAGE_PAGE_SIZE))
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&cache->mutex);
#ifdef _WIN32
	bypass = cache->bypass_admission && cache->validation_thread == GetCurrentThreadId();
#else
	bypass = cache->bypass_admission && pthread_equal(cache->validation_thread, pthread_self());
#endif
	bucket = udb_storage_cache_bucket(file->identity, offset, generation);
	for (entry = cache->buckets[bucket]; entry; entry = entry->hash_next)
	{
		if (entry->transient || (bypass && entry->page.kind == UDB_STORAGE_PAGE_DATA))
			continue;
		if (entry->file_identity != file->identity || entry->offset != offset || entry->generation != generation)
			continue;
		if (entry->pins == UINT_MAX)
		{
			udb_storage_mutex_unlock(&cache->mutex);
			return UDB_STORAGE_LIMIT;
		}
		entry->pins++;
		udb_storage_cache_unlink_lru(cache, entry);
		udb_storage_cache_make_newest(cache, entry);
		if (cache->hits != UINT64_MAX)
			cache->hits++;
		*out = entry;
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_OK;
	}
	if (cache->misses != UINT64_MAX)
		cache->misses++;
	while (!bypass)
	{
		udb_storage_mutex_lock(&cache->budget->mutex);
		available = cache->budget->limit[UDB_STORAGE_MEMORY_CACHE] - cache->budget->used[UDB_STORAGE_MEMORY_CACHE];
		udb_storage_mutex_unlock(&cache->budget->mutex);
		if (charge <= available)
			break;
		if (!udb_storage_cache_evict_one(cache))
		{
			udb_storage_mutex_unlock(&cache->mutex);
			return UDB_STORAGE_LIMIT;
		}
	}
	entry =
		udb_storage_alloc(cache->budget, bypass ? UDB_STORAGE_MEMORY_AUX : UDB_STORAGE_MEMORY_CACHE, sizeof(*entry));
	if (!entry)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_ERROR;
	}
	result = udb_storage_file_read(file, offset, entry->bytes, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_page_decode(entry->bytes, UDB_STORAGE_PAGE_SIZE, &page);
	if (result == UDB_STORAGE_OK && (page.generation != generation || page.ordinal != offset / UDB_STORAGE_PAGE_SIZE))
		result = UDB_STORAGE_CORRUPT;
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_free(entry);
		udb_storage_mutex_unlock(&cache->mutex);
		return result;
	}
	entry->file_identity = file->identity;
	entry->offset = offset;
	entry->generation = generation;
	entry->pins = 1;
	entry->transient = bypass && page.kind == UDB_STORAGE_PAGE_DATA;
	if (bypass && !entry->transient)
	{
		/* Retain bounded index/catalog pages, not scanned DATA pages. */
		for (;;)
		{
			udb_storage_mutex_lock(&cache->budget->mutex);
			available = cache->budget->limit[UDB_STORAGE_MEMORY_CACHE] - cache->budget->used[UDB_STORAGE_MEMORY_CACHE];
			udb_storage_mutex_unlock(&cache->budget->mutex);
			if (charge <= available)
				break;
			if (!udb_storage_cache_evict_one(cache))
			{
				entry->transient = 1;
				break;
			}
		}
		if (!entry->transient)
		{
			UdbStorageAllocation *allocation = (UdbStorageAllocation *)entry - 1;
			if (udb_storage_budget_charge(cache->budget, UDB_STORAGE_MEMORY_CACHE, charge) != UDB_STORAGE_OK)
				entry->transient = 1;
			else
			{
				allocation->info.memory_class = UDB_STORAGE_MEMORY_CACHE;
				udb_storage_budget_uncharge(cache->budget, UDB_STORAGE_MEMORY_AUX, charge);
			}
		}
	}
	entry->page = page;
	entry->hash_next = cache->buckets[bucket];
	cache->buckets[bucket] = entry;
	udb_storage_cache_make_newest(cache, entry);
	cache->entries++;
	*out = entry;
	udb_storage_mutex_unlock(&cache->mutex);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_release(UdbStorageCache *cache, UdbStorageCacheEntry *entry)
{
	UdbStorageCacheEntry *member;
	size_t bucket;

	if (!cache || !entry)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&cache->mutex);
	bucket = udb_storage_cache_bucket(entry->file_identity, entry->offset, entry->generation);
	for (member = cache->buckets[bucket]; member && member != entry; member = member->hash_next)
		;
	if (!member || !member->pins)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_ERROR;
	}
	member->pins--;
	if (!member->pins && member->transient)
		udb_storage_cache_erase(cache, member);
	udb_storage_mutex_unlock(&cache->mutex);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_forget_range_locked(UdbStorageCache *cache, uint64_t identity,
															  uint64_t offset, uint64_t length)
{
	uint64_t end;
	UdbStorageCacheEntry *entry, *next;

	if (!identity || length > UINT64_MAX - offset)
		return UDB_STORAGE_ERROR;
	if (!length)
		return UDB_STORAGE_OK;
	end = offset + length;
	for (entry = cache->oldest; entry; entry = entry->newer)
		if (entry->file_identity == identity && entry->offset < end && entry->offset + UDB_STORAGE_PAGE_SIZE > offset &&
			entry->pins)
			return UDB_STORAGE_BUSY;
	for (entry = cache->oldest; entry; entry = next)
	{
		next = entry->newer;
		if (entry->file_identity == identity && entry->offset < end && entry->offset + UDB_STORAGE_PAGE_SIZE > offset)
			udb_storage_cache_erase(cache, entry);
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_forget_range(UdbStorageCache *cache, uint64_t identity, uint64_t offset,
													   uint64_t length)
{
	UdbStorageResult result;

	if (!cache)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&cache->mutex);
	result = udb_storage_cache_forget_range_locked(cache, identity, offset, length);
	udb_storage_mutex_unlock(&cache->mutex);
	return result;
}

static UdbStorageResult udb_storage_cache_forget_file(UdbStorageCache *cache, uint64_t identity)
{
	return udb_storage_cache_forget_range(cache, identity, 0, UINT64_MAX);
}

static UdbStorageResult udb_storage_cache_detach_file(UdbStorageFile *file)
{
	UdbStorageCache *cache = file->cache;
	UdbStorageFile **link;
	UdbStorageResult result;

	if (!cache)
		return UDB_STORAGE_OK;
	udb_storage_mutex_lock(&cache->mutex);
	link = &cache->files;
	while (*link && *link != file)
		link = &(*link)->cache_next;
	if (!*link)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_ERROR;
	}
	result = udb_storage_cache_forget_range_locked(cache, file->identity, 0, UINT64_MAX);
	if (result == UDB_STORAGE_OK)
	{
		*link = file->cache_next;
		file->cache_next = NULL;
		file->cache = NULL;
	}
	udb_storage_mutex_unlock(&cache->mutex);
	return result;
}
