/*
 * UDB 4 native storage: paged fragment directories and bounded visible-row merge.
 * All inputs are immutable, owner-validated and generation-pinned for the entire
 * view lifetime. No function here publishes, commits or reclaims storage.
 */

static int udb_storage_run_valid(UdbStorageCache *cache, const UdbStorageRun *run)
{
	return cache && run && run->file && run->file->opened && run->file->cache == cache &&
		   run->file->directory->budget == cache->budget && udb_storage_block_valid(run->block) &&
		   (run->stream == UDB_STORAGE_POINTS || run->stream == UDB_STORAGE_PREFIXES) &&
		   udb_storage_catalog_root_valid(&run->directory);
}

static void udb_storage_run_key(uint64_t ordinal, unsigned char key[64])
{
	memset(key, 0, 64);
	/* Lexicographic catalog order matches numeric fragment order. */
	for (unsigned int i = 0; i < 8; i++)
		key[i] = (unsigned char)(ordinal >> (56 - 8 * i));
}

static UdbStorageResult udb_storage_run_descriptor(const UdbStorageRun *run, const unsigned char value[128],
												   UdbStorageFragment *fragment)
{
	UdbStorageResult result = udb_storage_fragment_decode(value, UDB_STORAGE_FRAGMENT_DESCRIPTOR_SIZE, fragment);
	if (result != UDB_STORAGE_OK)
		return result;
	for (size_t i = UDB_STORAGE_FRAGMENT_DESCRIPTOR_SIZE; i < 128; i++)
		if (value[i])
			return UDB_STORAGE_CORRUPT;
	if (fragment->block != run->block || fragment->stream != run->stream || fragment->object != run->directory.object ||
		fragment->generation > run->directory.generation)
		return UDB_STORAGE_CORRUPT;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_fragment(UdbStorageCache *cache, const UdbStorageRun *run, uint64_t ordinal,
												 UdbStorageFragment *fragment)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (ordinal >= run->directory.count)
		return UDB_STORAGE_NOT_FOUND;
	udb_storage_run_key(ordinal, key);
	result = udb_storage_catalog_get(cache, run->file, &run->directory, key, value);
	/* An advertised directory cannot silently contain ordinal gaps. */
	if (result == UDB_STORAGE_NOT_FOUND)
		return UDB_STORAGE_CORRUPT;
	return result == UDB_STORAGE_OK ? udb_storage_run_descriptor(run, value, fragment) : result;
}

static UdbStorageResult udb_storage_run_find(UdbStorageCache *cache, const UdbStorageRun *run, const UdbStorageRow *key,
											 uint64_t *ordinal)
{
	uint64_t lower = 0, upper = run->directory.count;
	while (lower < upper)
	{
		uint64_t middle = lower + (upper - lower) / 2;
		UdbStorageFragment fragment;
		UdbStorageCacheEntry *pin;
		UdbStorageRow maximum;
		UdbStorageResult result = udb_storage_run_fragment(cache, run, middle, &fragment);
		int compare;
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_fragment_maximum(cache, run->file, &fragment, &maximum, &pin);
		if (result != UDB_STORAGE_OK)
			return result;
		compare = udb_storage_key_compare(&maximum, key);
		result = udb_storage_cache_release(cache, pin);
		if (result != UDB_STORAGE_OK)
			return result;
		if (compare < 0)
			lower = middle + 1;
		else
			upper = middle;
	}
	*ordinal = lower;
	return lower == run->directory.count ? UDB_STORAGE_NOT_FOUND : UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_get(UdbStorageCache *cache, const UdbStorageRun *run, const UdbStorageRow *key,
											UdbStorageRecord *record, UdbStorageCacheEntry **pin)
{
	uint64_t ordinal;
	UdbStorageFragment fragment;
	UdbStorageResult result;
	if (!record || !pin)
		return UDB_STORAGE_ERROR;
	*pin = NULL;
	if (!udb_storage_run_valid(cache, run) || !udb_storage_row_valid(key) || key->block != run->block)
		return UDB_STORAGE_ERROR;
	result = udb_storage_run_find(cache, run, key, &ordinal);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_run_fragment(cache, run, ordinal, &fragment);
	if (result != UDB_STORAGE_OK)
		return result;
	return udb_storage_fragment_get(cache, run->file, &fragment, key, record, pin);
}

static UdbStorageResult udb_storage_run_copy_key(const UdbStorageRow *row, unsigned char *out, size_t *length)
{
	UdbStorageRow key = *row;
	key.kind = UDB_STORAGE_VALUE_NONE;
	key.value.data = NULL;
	key.value.length = 0;
	key.number = 0;
	return udb_storage_row_encode(&key, out, UDB_STORAGE_KEY_MAX, length);
}

struct UdbStorageRunBuilder
{
	UdbStorageCache *cache;
	UdbStorageRun run;
	UdbStorageFragmentBuilder *fragment;
	uint64_t generation;
	uint64_t start;
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	size_t previous_length;
	UdbStorageResult failed;
	int finished;
};

static void udb_storage_run_builder_free(UdbStorageRunBuilder **pointer)
{
	UdbStorageRunBuilder *builder;
	if (!pointer || !*pointer)
		return;
	builder = *pointer;
	udb_storage_fragment_builder_free(&builder->fragment);
	udb_storage_free(builder);
	*pointer = NULL;
}

static UdbStorageResult udb_storage_run_builder_create(UdbStorageCache *cache, UdbStorageFile *file,
													   unsigned char block, UdbStorageStream stream,
													   uint64_t generation, uint64_t object, UdbStorageRunBuilder **out)
{
	UdbStorageRunBuilder *builder;
	UdbStorageResult result;
	uint64_t size;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || !file->opened || file->cache != cache || !generation || !object ||
		!udb_storage_block_valid(block) || (stream != UDB_STORAGE_POINTS && stream != UDB_STORAGE_PREFIXES))
		return UDB_STORAGE_ERROR;
	result = udb_storage_file_size(file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	builder = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*builder));
	if (!builder)
		return UDB_STORAGE_LIMIT;
	builder->cache = cache;
	builder->run.file = file;
	builder->run.block = block;
	builder->run.stream = stream;
	builder->run.directory.object = object;
	builder->generation = generation;
	builder->start = size;
	*out = builder;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_builder_open_fragment(UdbStorageRunBuilder *builder)
{
	uint64_t size, reserve;
	uint32_t height = builder->run.directory.height ? builder->run.directory.height : 1;
	UdbStorageResult result = udb_storage_file_size(builder->run.file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size < builder->start || size % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	/* Reserve worst-case fragment plus COW directory path before any output.
	 * Both data/index and catalog bytes count toward the 64 MiB private job cap. */
	reserve = UDB_STORAGE_FRAGMENT_MAX + (uint64_t)(2 * height + 1) * UDB_STORAGE_PAGE_SIZE;
	if (size - builder->start > UDB_STORAGE_COMPACTION_MAX ||
		reserve > UDB_STORAGE_COMPACTION_MAX - (size - builder->start) ||
		!udb_storage_io_range_valid(size, (size_t)reserve))
		return UDB_STORAGE_LIMIT;
	return udb_storage_fragment_builder_create(builder->run.file, builder->generation, builder->run.directory.object,
											   &builder->fragment);
}

static UdbStorageResult udb_storage_run_builder_emit(UdbStorageRunBuilder *builder)
{
	UdbStorageFragment fragment;
	UdbStorageCatalogRoot candidate;
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (!builder->fragment)
		return UDB_STORAGE_OK;
	result = udb_storage_fragment_builder_finish(builder->fragment, &fragment);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_fragment_builder_free(&builder->fragment);
	if (builder->run.directory.count == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	memset(value, 0, sizeof(value));
	result = udb_storage_fragment_encode(&fragment, value, sizeof(value));
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_run_key(builder->run.directory.count, key);
	result = udb_storage_catalog_change(builder->cache, builder->run.file, &builder->run.directory, builder->generation,
										key, value, &candidate);
	if (result != UDB_STORAGE_OK)
		return result;
	builder->run.directory = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_builder_add(UdbStorageRunBuilder *builder, const unsigned char *bytes,
													size_t length)
{
	UdbStorageRecord record;
	UdbStorageRow previous;
	UdbStorageResult result;
	if (!builder || builder->finished)
		return UDB_STORAGE_ERROR;
	if (builder->failed)
		return builder->failed;
	result = udb_storage_record_decode(bytes, length, &record);
	if (result != UDB_STORAGE_OK)
		return builder->failed = result;
	if (record.row.block != builder->run.block || !udb_storage_record_matches_stream(&record, builder->run.stream))
		return builder->failed = UDB_STORAGE_CORRUPT;
	if (builder->previous_length &&
		(udb_storage_row_decode(builder->previous, builder->previous_length, &previous) != UDB_STORAGE_OK ||
		 udb_storage_key_compare(&previous, &record.row) >= 0))
		return builder->failed = UDB_STORAGE_CORRUPT;
	if (!builder->fragment)
	{
		result = udb_storage_run_builder_open_fragment(builder);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_LIMIT ? result : (builder->failed = result);
	}
	result = udb_storage_fragment_builder_add(builder->fragment, bytes, length);
	if (result == UDB_STORAGE_LIMIT)
	{
		result = udb_storage_run_builder_emit(builder);
		if (result != UDB_STORAGE_OK)
			return builder->failed = result;
		result = udb_storage_run_builder_open_fragment(builder);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_LIMIT ? result : (builder->failed = result);
		result = udb_storage_fragment_builder_add(builder->fragment, bytes, length);
	}
	if (result != UDB_STORAGE_OK)
		return builder->failed = result;
	result = udb_storage_run_copy_key(&record.row, builder->previous, &builder->previous_length);
	return result == UDB_STORAGE_OK ? result : (builder->failed = result);
}

static UdbStorageResult udb_storage_run_builder_finish(UdbStorageRunBuilder *builder, UdbStorageRun *run)
{
	UdbStorageResult result;
	if (!builder || !run || builder->finished || udb_storage_overlaps(run, sizeof(*run), builder, sizeof(*builder)))
		return UDB_STORAGE_ERROR;
	if (builder->failed)
		return builder->failed;
	result = udb_storage_run_builder_emit(builder);
	if (result != UDB_STORAGE_OK)
		return builder->failed = result;
	builder->finished = 1;
	*run = builder->run;
	return UDB_STORAGE_OK;
}

typedef struct UdbStorageRunValidation
{
	UdbStorageRun run;
	UdbStorageRecordValidator validator;
	void *context;
	uint64_t watermark;
	uint64_t ordinal;
	size_t previous_length;
	unsigned char previous[UDB_STORAGE_KEY_MAX];
} UdbStorageRunValidation;

static UdbStorageResult udb_storage_run_validate_record(const UdbStorageRecord *record, void *context)
{
	UdbStorageRunValidation *validation = context;
	UdbStorageRow previous;
	UdbStorageResult result;
	if (record->stamp.sequence > validation->watermark)
		return UDB_STORAGE_CORRUPT;
	if (validation->previous_length &&
		(udb_storage_row_decode(validation->previous, validation->previous_length, &previous) != UDB_STORAGE_OK ||
		 udb_storage_key_compare(&previous, &record->row) >= 0))
		return UDB_STORAGE_CORRUPT;
	if (validation->validator)
	{
		result = validation->validator(record, validation->context);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return udb_storage_run_copy_key(&record->row, validation->previous, &validation->previous_length);
}

static UdbStorageResult udb_storage_run_validate_descriptor(const unsigned char *key, const unsigned char *value,
															void *context)
{
	UdbStorageRunValidation *validation = context;
	UdbStorageFragment fragment;
	unsigned char expected[64];
	UdbStorageResult result;
	udb_storage_run_key(validation->ordinal, expected);
	if (memcmp(expected, key, sizeof(expected)))
		return UDB_STORAGE_CORRUPT;
	result = udb_storage_run_descriptor(&validation->run, value, &fragment);
	if (result != UDB_STORAGE_OK)
		return result;
	result =
		udb_storage_fragment_validate(validation->run.file, &fragment, udb_storage_run_validate_record, validation);
	if (result != UDB_STORAGE_OK)
		return result;
	if (validation->ordinal == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	validation->ordinal++;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_validate(UdbStorageFile *file, const UdbStorageRun *run, uint64_t watermark,
												 UdbStorageRecordValidator validator, void *context)
{
	UdbStorageRunValidation *validation;
	UdbStorageResult result;
	if (!file || !file->opened || !udb_storage_run_valid(file->cache, run) || file != run->file)
		return UDB_STORAGE_ERROR;
	validation = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*validation));
	if (!validation)
		return UDB_STORAGE_LIMIT;
	validation->run = *run;
	validation->watermark = watermark;
	validation->validator = validator;
	validation->context = context;
	result = udb_storage_catalog_validate(file, &run->directory, udb_storage_run_validate_descriptor, validation);
	if (result == UDB_STORAGE_OK && validation->ordinal != run->directory.count)
		result = UDB_STORAGE_CORRUPT;
	udb_storage_free(validation);
	return result;
}

struct UdbStorageRunIterator
{
	UdbStorageCache *cache;
	UdbStorageRun run;
	UdbStorageFragmentIterator *fragment_iterator;
	UdbStorageFragment fragment;
	uint64_t ordinal;
	unsigned char prefix[UDB_STORAGE_KEY_MAX];
	size_t prefix_length;
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	size_t previous_length;
	UdbStorageResult failed;
	int finished;
};

static UdbStorageResult udb_storage_run_iterator_close(UdbStorageRunIterator **pointer)
{
	UdbStorageRunIterator *iterator;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	iterator = *pointer;
	if (iterator->fragment_iterator)
	{
		result = udb_storage_fragment_iterator_close(&iterator->fragment_iterator);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	udb_storage_free(iterator);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_iterator_create(UdbStorageCache *cache, const UdbStorageRun *run,
														const UdbStorageRow *prefix, UdbStorageRunIterator **out)
{
	UdbStorageRunIterator *iterator;
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!udb_storage_run_valid(cache, run) ||
		(prefix && (!udb_storage_row_valid(prefix) || prefix->block != run->block)))
		return UDB_STORAGE_ERROR;
	iterator = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*iterator));
	if (!iterator)
		return UDB_STORAGE_LIMIT;
	iterator->cache = cache;
	iterator->run = *run;
	if (prefix)
	{
		result = udb_storage_run_copy_key(prefix, iterator->prefix, &iterator->prefix_length);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_run_find(cache, run, prefix, &iterator->ordinal);
		if (result == UDB_STORAGE_NOT_FOUND)
			iterator->finished = 1;
		else if (result != UDB_STORAGE_OK)
			goto fail;
	}
	if (!run->directory.count)
		iterator->finished = 1;
	*out = iterator;
	return UDB_STORAGE_OK;
fail:
	udb_storage_free(iterator);
	return result;
}

static UdbStorageResult udb_storage_run_iterator_next(UdbStorageRunIterator *iterator, UdbStorageRecord *record)
{
	UdbStorageResult result;
	UdbStorageRow prefix;
	if (!iterator || !record || udb_storage_overlaps(record, sizeof(*record), iterator, sizeof(*iterator)))
		return UDB_STORAGE_ERROR;
	if (iterator->failed)
		return iterator->failed;
	if (iterator->finished)
		return UDB_STORAGE_NOT_FOUND;
	if (iterator->prefix_length &&
		udb_storage_row_decode(iterator->prefix, iterator->prefix_length, &prefix) != UDB_STORAGE_OK)
		return iterator->failed = UDB_STORAGE_CORRUPT;
	while (iterator->ordinal < iterator->run.directory.count)
	{
		UdbStorageRecord candidate;
		if (!iterator->fragment_iterator)
		{
			result = udb_storage_run_fragment(iterator->cache, &iterator->run, iterator->ordinal, &iterator->fragment);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
			result = udb_storage_fragment_iterator_create(iterator->cache, iterator->run.file, &iterator->fragment,
														  iterator->prefix_length ? &prefix : NULL,
														  &iterator->fragment_iterator);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_CORRUPT : result;
		}
		result = udb_storage_fragment_iterator_next(iterator->fragment_iterator, &candidate);
		if (result == UDB_STORAGE_OK)
		{
			UdbStorageRow previous;
			if (iterator->previous_length &&
				(udb_storage_row_decode(iterator->previous, iterator->previous_length, &previous) != UDB_STORAGE_OK ||
				 udb_storage_key_compare(&previous, &candidate.row) >= 0))
				return iterator->failed = UDB_STORAGE_CORRUPT;
			result = udb_storage_run_copy_key(&candidate.row, iterator->previous, &iterator->previous_length);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
			*record = candidate;
			return UDB_STORAGE_OK;
		}
		if (result != UDB_STORAGE_NOT_FOUND)
			return iterator->failed = result;
		result = udb_storage_fragment_iterator_close(&iterator->fragment_iterator);
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
		if (iterator->prefix_length)
		{
			UdbStorageCacheEntry *pin;
			UdbStorageRow maximum;
			int past;
			result =
				udb_storage_fragment_maximum(iterator->cache, iterator->run.file, &iterator->fragment, &maximum, &pin);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
			past = !udb_storage_key_has_prefix(&maximum, &prefix);
			if (udb_storage_key_compare(&maximum, &prefix) < 0)
			{
				udb_storage_cache_release(iterator->cache, pin);
				return iterator->failed = UDB_STORAGE_CORRUPT;
			}
			result = udb_storage_cache_release(iterator->cache, pin);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
			if (past)
			{
				iterator->finished = 1;
				return UDB_STORAGE_NOT_FOUND;
			}
		}
		iterator->ordinal++;
	}
	iterator->finished = 1;
	return UDB_STORAGE_NOT_FOUND;
}

typedef struct UdbStorageViewCursor
{
	UdbStorageRunIterator *iterator;
	UdbStorageRecord record;
	int present;
} UdbStorageViewCursor;

typedef struct UdbStoragePrefixCover
{
	UdbStorageRow key;
	UdbStorageStamp stamp;
	unsigned char bytes[UDB_STORAGE_KEY_MAX];
} UdbStoragePrefixCover;

typedef struct UdbStoragePrefixCursor
{
	UdbStorageViewCursor cursor;
	UdbStoragePrefixCover *covers;
	unsigned int depth;
	UdbStorageStamp seed;
	int has_seed;
} UdbStoragePrefixCursor;

struct UdbStorageView
{
	UdbStorageCache *cache;
	UdbStorageViewCursor points[UDB_STORAGE_MAX_RUNS + 1];
	UdbStoragePrefixCursor prefixes[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun indexed_prefixes[UDB_STORAGE_MAX_RUNS + 1];
	size_t point_count;
	size_t prefix_count;
	uint64_t watermark;
	UdbStorageResult failed;
	unsigned char output[UDB_STORAGE_ROW_MAX];
	int finished;
	int indexed;
};

static UdbStorageResult udb_storage_view_cursor_next(UdbStorageView *view, UdbStorageViewCursor *cursor)
{
	UdbStorageResult result = udb_storage_run_iterator_next(cursor->iterator, &cursor->record);
	if (result == UDB_STORAGE_NOT_FOUND)
	{
		cursor->present = 0;
		return UDB_STORAGE_OK;
	}
	if (result != UDB_STORAGE_OK)
		return result;
	if (cursor->record.stamp.sequence > view->watermark)
		return UDB_STORAGE_CORRUPT;
	cursor->present = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_view_close(UdbStorageView **pointer)
{
	UdbStorageView *view;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	view = *pointer;
	for (size_t i = 0; i < view->point_count; i++)
		if (view->points[i].iterator)
		{
			result = udb_storage_run_iterator_close(&view->points[i].iterator);
			if (result != UDB_STORAGE_OK)
				return result;
		}
	for (size_t i = 0; i < view->prefix_count; i++)
	{
		if (view->prefixes[i].cursor.iterator)
		{
			result = udb_storage_run_iterator_close(&view->prefixes[i].cursor.iterator);
			if (result != UDB_STORAGE_OK)
				return result;
		}
		udb_storage_free(view->prefixes[i].covers);
		view->prefixes[i].covers = NULL;
	}
	udb_storage_free(view);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_view_seed(UdbStorageView *view, const UdbStorageRun *run,
											  const UdbStorageRow *prefix, UdbStoragePrefixCursor *cursor)
{
	UdbStorageRow ancestor = *prefix;
	ancestor.kind = UDB_STORAGE_VALUE_NONE;
	ancestor.value.data = NULL;
	ancestor.value.length = 0;
	ancestor.number = 0;
	for (unsigned int depth = 1; depth < prefix->depth; depth++)
	{
		UdbStorageCacheEntry *pin;
		UdbStorageRecord record;
		UdbStorageResult result;
		ancestor.depth = (unsigned char)depth;
		result = udb_storage_run_get(view->cache, run, &ancestor, &record, &pin);
		if (result == UDB_STORAGE_NOT_FOUND)
			continue;
		if (result != UDB_STORAGE_OK)
			return result;
		if (record.stamp.sequence > view->watermark)
		{
			udb_storage_cache_release(view->cache, pin);
			return UDB_STORAGE_CORRUPT;
		}
		if (!cursor->has_seed || udb_storage_stamp_compare(record.stamp, cursor->seed) > 0)
		{
			cursor->seed = record.stamp;
			cursor->has_seed = 1;
		}
		result = udb_storage_cache_release(view->cache, pin);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_view_create_mode(UdbStorageCache *cache, const UdbStorageRun *points,
													 size_t point_count, const UdbStorageRun *prefixes,
													 size_t prefix_count, const UdbStorageRow *prefix,
													 uint64_t watermark, int indexed, UdbStorageView **out)
{
	UdbStorageView *view;
	UdbStorageResult result;
	unsigned char block = prefix ? prefix->block : 0;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || point_count > UDB_STORAGE_MAX_RUNS + 1 || prefix_count > UDB_STORAGE_MAX_RUNS + 1 ||
		(point_count && !points) || (prefix_count && !prefixes) || (prefix && !udb_storage_row_valid(prefix)))
		return UDB_STORAGE_ERROR;
	for (size_t i = 0; i < point_count + prefix_count; i++)
	{
		const UdbStorageRun *run = i < point_count ? points + i : prefixes + i - point_count;
		if (!udb_storage_run_valid(cache, run) ||
			run->stream != (i < point_count ? UDB_STORAGE_POINTS : UDB_STORAGE_PREFIXES))
			return UDB_STORAGE_ERROR;
		if (!block)
			block = run->block;
		if (run->block != block)
			return UDB_STORAGE_ERROR;
	}
	view = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*view));
	if (!view)
		return UDB_STORAGE_LIMIT;
	view->cache = cache;
	view->watermark = watermark;
	view->point_count = point_count;
	view->prefix_count = prefix_count;
	view->indexed = indexed;
	if (!point_count)
	{
		view->finished = 1;
		*out = view;
		return UDB_STORAGE_OK;
	}
	for (size_t i = 0; i < point_count; i++)
	{
		result = udb_storage_run_iterator_create(cache, points + i, prefix, &view->points[i].iterator);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_view_cursor_next(view, view->points + i);
		if (result != UDB_STORAGE_OK)
			goto fail;
	}
	for (size_t i = 0; i < prefix_count; i++)
	{
		UdbStoragePrefixCursor *cursor = view->prefixes + i;
		if (indexed)
		{
			view->indexed_prefixes[i] = prefixes[i];
			continue;
		}
		cursor->covers = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX,
										   UDB_STORAGE_COMPONENTS_MAX * sizeof(*cursor->covers));
		if (!cursor->covers)
		{
			result = UDB_STORAGE_LIMIT;
			goto fail;
		}
		if (prefix)
		{
			result = udb_storage_view_seed(view, prefixes + i, prefix, cursor);
			if (result != UDB_STORAGE_OK)
				goto fail;
		}
		result = udb_storage_run_iterator_create(cache, prefixes + i, prefix, &cursor->cursor.iterator);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_view_cursor_next(view, &cursor->cursor);
		if (result != UDB_STORAGE_OK)
			goto fail;
	}
	*out = view;
	return UDB_STORAGE_OK;
fail:
	udb_storage_view_close(&view);
	return result;
}

static UdbStorageResult udb_storage_view_create(UdbStorageCache *cache, const UdbStorageRun *points, size_t point_count,
												const UdbStorageRun *prefixes, size_t prefix_count,
												const UdbStorageRow *prefix, uint64_t watermark, UdbStorageView **out)
{
	return udb_storage_view_create_mode(cache, points, point_count, prefixes, prefix_count, prefix, watermark, 0, out);
}

static UdbStorageResult udb_storage_view_create_indexed(UdbStorageCache *cache, const UdbStorageRun *points,
														size_t point_count, const UdbStorageRun *prefixes,
														size_t prefix_count, const UdbStorageRow *prefix,
														uint64_t watermark, UdbStorageView **out)
{
	return udb_storage_view_create_mode(cache, points, point_count, prefixes, prefix_count, prefix, watermark, 1, out);
}

/* Hash leaves must not restart a whole global prefix stream per leaf. */
static UdbStorageResult udb_storage_prefix_cover_get(UdbStorageCache *cache, const UdbStorageRun *run,
													 const UdbStorageRow *key, uint64_t watermark,
													 UdbStorageStamp *maximum, int *present)
{
	UdbStorageRow ancestor = *key;
	ancestor.kind = UDB_STORAGE_VALUE_NONE;
	ancestor.value.data = NULL;
	ancestor.value.length = 0;
	ancestor.number = 0;
	for (unsigned int depth = 1; depth <= key->depth; depth++)
	{
		UdbStorageRecord record;
		UdbStorageCacheEntry *pin;
		UdbStorageResult result;
		ancestor.depth = (unsigned char)depth;
		result = udb_storage_run_get(cache, run, &ancestor, &record, &pin);
		if (result == UDB_STORAGE_NOT_FOUND)
			continue;
		if (result != UDB_STORAGE_OK)
			return result;
		if (record.stamp.sequence > watermark)
		{
			udb_storage_cache_release(cache, pin);
			return UDB_STORAGE_CORRUPT;
		}
		if (!*present || udb_storage_stamp_compare(record.stamp, *maximum) > 0)
		{
			*maximum = record.stamp;
			*present = 1;
		}
		result = udb_storage_cache_release(cache, pin);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_view_cover(UdbStorageView *view, UdbStoragePrefixCursor *cursor,
											   const UdbStorageRow *key, UdbStorageStamp *maximum, int *present)
{
	while (cursor->cursor.present && udb_storage_key_compare(&cursor->cursor.record.row, key) <= 0)
	{
		UdbStoragePrefixCover *cover;
		UdbStorageResult result;
		size_t length;
		while (cursor->depth &&
			   !udb_storage_key_has_prefix(&cursor->cursor.record.row, &cursor->covers[cursor->depth - 1].key))
			cursor->depth--;
		if (cursor->depth == UDB_STORAGE_COMPONENTS_MAX)
			return UDB_STORAGE_CORRUPT;
		cover = cursor->covers + cursor->depth++;
		result = udb_storage_run_copy_key(&cursor->cursor.record.row, cover->bytes, &length);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_row_decode(cover->bytes, length, &cover->key);
		if (result != UDB_STORAGE_OK)
			return result;
		cover->stamp = cursor->cursor.record.stamp;
		result = udb_storage_view_cursor_next(view, &cursor->cursor);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	while (cursor->depth && !udb_storage_key_has_prefix(key, &cursor->covers[cursor->depth - 1].key))
		cursor->depth--;
	if (cursor->has_seed && (!*present || udb_storage_stamp_compare(cursor->seed, *maximum) > 0))
	{
		*maximum = cursor->seed;
		*present = 1;
	}
	for (unsigned int i = 0; i < cursor->depth; i++)
		if (!*present || udb_storage_stamp_compare(cursor->covers[i].stamp, *maximum) > 0)
		{
			*maximum = cursor->covers[i].stamp;
			*present = 1;
		}
	return UDB_STORAGE_OK;
}

static int udb_storage_view_records_equal(const UdbStorageRecord *a, const UdbStorageRecord *b)
{
	if (a->operation != b->operation || a->row.kind != b->row.kind)
		return 0;
	if (a->row.kind == UDB_STORAGE_VALUE_NUMBER)
		return a->row.number == b->row.number;
	if (a->row.kind == UDB_STORAGE_VALUE_STRING)
		return a->row.value.length == b->row.value.length &&
			   !memcmp(a->row.value.data, b->row.value.data, a->row.value.length);
	return 1;
}

static UdbStorageResult udb_storage_view_next_record(UdbStorageView *view, UdbStorageRecord *record)
{
	if (!view || !record || udb_storage_overlaps(record, sizeof(*record), view, sizeof(*view)))
		return UDB_STORAGE_ERROR;
	if (view->failed)
		return view->failed;
	if (view->finished)
		return UDB_STORAGE_NOT_FOUND;
	for (;;)
	{
		UdbStorageRecord *minimum = NULL, *winner;
		UdbStorageStamp deleted;
		UdbStorageStamp winning_stamp;
		int has_delete = 0, visible;
		unsigned int matching[UDB_STORAGE_MAX_RUNS + 1];
		size_t count = 0, length = 0;
		UdbStorageResult result;
		for (size_t i = 0; i < view->point_count; i++)
			if (view->points[i].present &&
				(!minimum || udb_storage_key_compare(&view->points[i].record.row, &minimum->row) < 0))
				minimum = &view->points[i].record;
		if (!minimum)
		{
			view->finished = 1;
			return UDB_STORAGE_NOT_FOUND;
		}
		winner = minimum;
		for (size_t i = 0; i < view->point_count; i++)
		{
			UdbStorageRecord *candidate = &view->points[i].record;
			if (!view->points[i].present || udb_storage_key_compare(&candidate->row, &minimum->row))
				continue;
			for (size_t j = 0; j < count; j++)
			{
				UdbStorageRecord *previous = &view->points[matching[j]].record;
				if (!udb_storage_stamp_compare(previous->stamp, candidate->stamp) &&
					!udb_storage_view_records_equal(previous, candidate))
					return view->failed = UDB_STORAGE_CORRUPT;
			}
			matching[count++] = (unsigned int)i;
			if (udb_storage_stamp_compare(candidate->stamp, winner->stamp) > 0)
				winner = candidate;
		}
		for (size_t i = 0; i < view->prefix_count; i++)
		{
			result = view->indexed
						 ? udb_storage_prefix_cover_get(view->cache, view->indexed_prefixes + i, &minimum->row,
														view->watermark, &deleted, &has_delete)
						 : udb_storage_view_cover(view, view->prefixes + i, &minimum->row, &deleted, &has_delete);
			if (result != UDB_STORAGE_OK)
				return view->failed = result;
		}
		visible = winner->operation == UDB_STORAGE_PUT;
		if (has_delete)
		{
			int compare = udb_storage_stamp_compare(deleted, winner->stamp);
			/* Different operations cannot share an admitted transaction ordinal. */
			if (!compare)
				return view->failed = UDB_STORAGE_CORRUPT;
			if (compare > 0)
				visible = 0;
		}
		if (visible)
		{
			winning_stamp = winner->stamp;
			result = udb_storage_row_encode(&winner->row, view->output, sizeof(view->output), &length);
			if (result != UDB_STORAGE_OK)
				return view->failed = result;
		}
		/* Copy the winner before advancing any cursor and invalidating borrowed spans. */
		for (size_t i = 0; i < count; i++)
		{
			result = udb_storage_view_cursor_next(view, view->points + matching[i]);
			if (result != UDB_STORAGE_OK)
				return view->failed = result;
		}
		if (!visible)
			continue;
		result = udb_storage_row_decode(view->output, length, &record->row);
		if (result == UDB_STORAGE_OK)
		{
			record->stamp = winning_stamp;
			record->operation = UDB_STORAGE_PUT;
		}
		return result == UDB_STORAGE_OK ? result : (view->failed = result);
	}
}

static UdbStorageResult udb_storage_view_next(UdbStorageView *view, UdbStorageRow *row)
{
	UdbStorageRecord record;
	UdbStorageResult result;
	if (!row || (view && udb_storage_overlaps(row, sizeof(*row), view, sizeof(*view))))
		return UDB_STORAGE_ERROR;
	result = udb_storage_view_next_record(view, &record);
	if (result == UDB_STORAGE_OK)
		*row = record.row;
	return result;
}

typedef struct UdbStorageLookup
{
	UdbStorageRecord records[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageCacheEntry *pins[UDB_STORAGE_MAX_RUNS + 1];
	size_t count;
} UdbStorageLookup;

static UdbStorageResult udb_storage_lookup(UdbStorageCache *cache, const UdbStorageRun *points, size_t point_count,
										   const UdbStorageRun *prefixes, size_t prefix_count, const UdbStorageRow *key,
										   uint64_t watermark, unsigned char *bytes, size_t capacity,
										   UdbStorageRow *row)
{
	UdbStorageLookup *lookup;
	UdbStorageResult result;
	UdbStorageRecord *winner = NULL;
	UdbStorageStamp deleted;
	int has_delete = 0;
	size_t length;
	if (!cache || !bytes || !row || !udb_storage_row_valid(key) || point_count > UDB_STORAGE_MAX_RUNS + 1 ||
		prefix_count > UDB_STORAGE_MAX_RUNS + 1 || (point_count && !points) || (prefix_count && !prefixes) ||
		udb_storage_overlaps(bytes, capacity, row, sizeof(*row)))
		return UDB_STORAGE_ERROR;
	for (size_t i = 0; i < point_count + prefix_count; i++)
	{
		const UdbStorageRun *run = i < point_count ? points + i : prefixes + i - point_count;
		if (!udb_storage_run_valid(cache, run) || run->block != key->block ||
			run->stream != (i < point_count ? UDB_STORAGE_POINTS : UDB_STORAGE_PREFIXES))
			return UDB_STORAGE_ERROR;
	}
	lookup = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*lookup));
	if (!lookup)
		return UDB_STORAGE_LIMIT;
	for (size_t i = 0; i < point_count; i++)
	{
		UdbStorageRecord *record = lookup->records + lookup->count;
		result = udb_storage_run_get(cache, points + i, key, record, lookup->pins + lookup->count);
		if (result == UDB_STORAGE_NOT_FOUND)
			continue;
		if (result != UDB_STORAGE_OK)
			goto done;
		lookup->count++;
		if (record->stamp.sequence > watermark)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		for (size_t j = 0; j + 1 < lookup->count; j++)
			if (!udb_storage_stamp_compare(lookup->records[j].stamp, record->stamp) &&
				!udb_storage_view_records_equal(lookup->records + j, record))
			{
				result = UDB_STORAGE_CORRUPT;
				goto done;
			}
		if (!winner || udb_storage_stamp_compare(record->stamp, winner->stamp) > 0)
			winner = record;
	}
	if (!winner)
	{
		result = UDB_STORAGE_NOT_FOUND;
		goto done;
	}
	for (size_t i = 0; i < prefix_count; i++)
	{
		result = udb_storage_prefix_cover_get(cache, prefixes + i, key, watermark, &deleted, &has_delete);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	if (has_delete && !udb_storage_stamp_compare(deleted, winner->stamp))
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	if (winner->operation == UDB_STORAGE_DELETE ||
		(has_delete && udb_storage_stamp_compare(deleted, winner->stamp) > 0))
	{
		result = UDB_STORAGE_NOT_FOUND;
		goto done;
	}
	for (size_t i = 0; i < lookup->count; i++)
		if (udb_storage_overlaps(bytes, capacity, lookup->pins[i], sizeof(*lookup->pins[i])) ||
			udb_storage_overlaps(row, sizeof(*row), lookup->pins[i], sizeof(*lookup->pins[i])))
		{
			result = UDB_STORAGE_ERROR;
			goto done;
		}
	result = udb_storage_row_encode(&winner->row, bytes, capacity, &length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_row_decode(bytes, length, row);
done:
	for (size_t i = 0; i < lookup->count; i++)
	{
		UdbStorageResult released = udb_storage_cache_release(cache, lookup->pins[i]);
		if (released != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND)
			result = released;
	}
	udb_storage_free(lookup);
	return result;
}

static UdbStorageResult udb_storage_exists_prefix(UdbStorageCache *cache, const UdbStorageRun *points,
												  size_t point_count, const UdbStorageRun *prefixes,
												  size_t prefix_count, const UdbStorageRow *prefix, uint64_t watermark)
{
	UdbStorageView *view;
	UdbStorageRow row;
	UdbStorageResult result, closed;
	if (!prefix)
		return UDB_STORAGE_ERROR;
	result = udb_storage_view_create(cache, points, point_count, prefixes, prefix_count, prefix, watermark, &view);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_view_next(view, &row);
	closed = udb_storage_view_close(&view);
	return closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND ? closed : result;
}
