/*
 * UDB 4 native storage: private sorted fragments and paged range/offset indices.
 * Builders never publish, flush a commit, alter a catalog or truncate a file.
 * Their owner must keep the file open and serialize append builders. Orphaned
 * output is reclaimed only by the validated journal/checkpoint layer.
 */

typedef struct UdbStorageIndexEntry
{
	uint64_t offset;
	uint64_t generation;
	size_t key_length;
	unsigned char key[UDB_STORAGE_KEY_MAX];
} UdbStorageIndexEntry;

struct UdbStorageFragmentBuilder
{
	UdbStorageFile *file;
	UdbStorageBudget *budget;
	unsigned char *rows;
	unsigned char *payload;
	unsigned char *page;
	uint32_t *offsets;
	UdbStorageIndexEntry *level[2];
	unsigned char *last_key;
	size_t last_key_length;
	size_t rows_length;
	size_t pending_count;
	uint32_t data_pages;
	uint32_t data_max;
	uint32_t reserved_pages;
	uint32_t total_pages;
	uint64_t start;
	uint64_t generation;
	uint64_t object;
	uint64_t count;
	unsigned char block;
	UdbStorageStream stream;
	int failed;
	int finished;
};

static void udb_storage_fragment_builder_free(UdbStorageFragmentBuilder **pointer)
{
	UdbStorageFragmentBuilder *builder;

	if (!pointer || !*pointer)
		return;
	builder = *pointer;
	udb_storage_free(builder->rows);
	udb_storage_free(builder->payload);
	udb_storage_free(builder->page);
	udb_storage_free(builder->offsets);
	udb_storage_free(builder->level[0]);
	udb_storage_free(builder->level[1]);
	udb_storage_free(builder->last_key);
	udb_storage_free(builder);
	*pointer = NULL;
}

static UdbStorageResult udb_storage_fragment_builder_create(UdbStorageFile *file, uint64_t generation, uint64_t object,
															UdbStorageFragmentBuilder **out)
{
	UdbStorageFragmentBuilder *builder;
	UdbStorageResult result;
	uint64_t size;
	unsigned int pages = UDB_STORAGE_FRAGMENT_MAX / UDB_STORAGE_PAGE_SIZE;

	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!file || !file->opened || !generation || !object)
		return UDB_STORAGE_ERROR;
	result = udb_storage_file_reserve_fragment(file, &pages, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	if (!udb_storage_io_range_valid(size, UDB_STORAGE_FRAGMENT_MAX))
		return UDB_STORAGE_LIMIT;
	builder = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*builder));
	if (!builder)
		return UDB_STORAGE_LIMIT;
	builder->file = file;
	builder->budget = file->directory->budget;
	builder->start = size;
	builder->reserved_pages = pages;
	const unsigned int fanout = (UDB_STORAGE_PAGE_PAYLOAD - 12) / (UDB_STORAGE_KEY_MAX + 20);
	for (unsigned int data = 1; data <= UDB_STORAGE_FRAGMENT_DATA_PAGES_MAX; data++)
	{
		unsigned int total = data, nodes = data;
		do
		{
			nodes = (nodes + fanout - 1) / fanout;
			total += nodes;
		} while (nodes > 1);
		if (total > pages)
			break;
		builder->data_max = data;
	}
	if (!builder->data_max)
	{
		udb_storage_fragment_builder_free(&builder);
		return UDB_STORAGE_LIMIT;
	}

	builder->generation = generation;
	builder->object = object;
	builder->rows = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_PAYLOAD);
	builder->payload = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_PAYLOAD);
	builder->page = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	builder->offsets = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX,
										 (UDB_STORAGE_DATA_ROWS_MAX + 1) * sizeof(*builder->offsets));
	for (unsigned int i = 0; i < 2; i++)
		builder->level[i] = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX,
											  UDB_STORAGE_FRAGMENT_DATA_PAGES_MAX * sizeof(*builder->level[i]));
	builder->last_key = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_KEY_MAX);
	if (!builder->rows || !builder->payload || !builder->page || !builder->offsets || !builder->level[0] ||
		!builder->level[1] || !builder->last_key)
	{
		udb_storage_fragment_builder_free(&builder);
		return UDB_STORAGE_LIMIT;
	}
	*out = builder;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_write_page(UdbStorageFragmentBuilder *builder, UdbStoragePageKind kind,
														size_t payload_length, uint64_t *offset)
{
	UdbStoragePage page;
	UdbStorageResult result;
	uint64_t position;

	if (builder->total_pages >= builder->reserved_pages)
		return UDB_STORAGE_LIMIT;
	position = builder->start + (uint64_t)builder->total_pages * UDB_STORAGE_PAGE_SIZE;
	memset(&page, 0, sizeof(page));
	page.kind = kind;
	page.generation = builder->generation;
	page.object = builder->object;
	page.ordinal = position / UDB_STORAGE_PAGE_SIZE;
	page.payload.data = builder->payload;
	page.payload.length = (uint32_t)payload_length;
	result = udb_storage_page_encode(&page, builder->page, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(builder->file, position, builder->page, UDB_STORAGE_PAGE_SIZE);
	if (result != UDB_STORAGE_OK)
	{
		builder->failed = 1;
		return result;
	}
	builder->total_pages++;
	*offset = position;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_flush_data(UdbStorageFragmentBuilder *builder)
{
	UdbStorageResult result;
	UdbStorageIndexEntry *index;
	size_t base, length;
	uint64_t offset;

	if (!builder->pending_count)
		return UDB_STORAGE_OK;
	if (builder->data_pages >= builder->data_max)
		return UDB_STORAGE_LIMIT;
	base = 8 + 4 * (builder->pending_count + 1);
	length = base + builder->rows_length;
	udb_storage_put_u32(builder->payload, (uint32_t)builder->pending_count);
	udb_storage_put_u32(builder->payload + 4, 0);
	for (size_t i = 0; i <= builder->pending_count; i++)
		udb_storage_put_u32(builder->payload + 8 + 4 * i, (uint32_t)base + builder->offsets[i]);
	memcpy(builder->payload + base, builder->rows, builder->rows_length);
	result = udb_storage_fragment_write_page(builder, UDB_STORAGE_PAGE_DATA, length, &offset);
	if (result != UDB_STORAGE_OK)
		return result;
	index = &builder->level[0][builder->data_pages++];
	index->offset = offset;
	index->generation = builder->generation;
	index->key_length = builder->last_key_length;
	memcpy(index->key, builder->last_key, builder->last_key_length);
	builder->pending_count = 0;
	builder->rows_length = 0;
	builder->offsets[0] = 0;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_builder_add(UdbStorageFragmentBuilder *builder, const unsigned char *bytes,
														 size_t length)
{
	UdbStorageRecord record;
	UdbStorageRow row, previous, key;
	UdbStorageResult result;
	size_t needed, key_length;

	if (!builder || builder->failed || builder->finished)
		return UDB_STORAGE_ERROR;
	result = udb_storage_record_decode(bytes, length, &record);
	if (result != UDB_STORAGE_OK)
	{
		builder->failed = 1;
		return result;
	}
	row = record.row;
	if (builder->count)
	{
		if (row.block != builder->block || !udb_storage_record_matches_stream(&record, builder->stream) ||
			udb_storage_row_decode(builder->last_key, builder->last_key_length, &previous) != UDB_STORAGE_OK ||
			udb_storage_key_compare(&previous, &row) >= 0)
		{
			builder->failed = 1;
			return UDB_STORAGE_CORRUPT;
		}
	}
	if (builder->data_pages == builder->data_max)
		return UDB_STORAGE_LIMIT;
	needed = 8 + 4 * (builder->pending_count + 2) + builder->rows_length + length;
	if (needed > UDB_STORAGE_PAGE_PAYLOAD || builder->pending_count == UDB_STORAGE_DATA_ROWS_MAX)
	{
		result = udb_storage_fragment_flush_data(builder);
		if (result != UDB_STORAGE_OK)
			return result;
		if (builder->data_pages == builder->data_max)
			return UDB_STORAGE_LIMIT;
	}
	key = row;
	key.kind = UDB_STORAGE_VALUE_NONE;
	key.value.data = NULL;
	key.value.length = 0;
	key.number = 0;
	/* Encode the last key into separate scratch before touching state. */
	result = udb_storage_row_encode(&key, builder->payload, UDB_STORAGE_PAGE_PAYLOAD, &key_length);
	if (result != UDB_STORAGE_OK)
		return result;
	if (key_length > UDB_STORAGE_KEY_MAX || builder->count == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	memcpy(builder->rows + builder->rows_length, bytes, length);
	builder->rows_length += length;
	builder->offsets[++builder->pending_count] = (uint32_t)builder->rows_length;
	memcpy(builder->last_key, builder->payload, key_length);
	builder->last_key_length = key_length;
	builder->block = row.block;
	builder->stream = record.operation == UDB_STORAGE_PREFIX_DELETE ? UDB_STORAGE_PREFIXES : UDB_STORAGE_POINTS;
	builder->count++;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_build_index(UdbStorageFragmentBuilder *builder,
														 UdbStorageIndexEntry *children, size_t count, uint32_t level,
														 UdbStorageIndexEntry *parents, size_t *parent_count)
{
	size_t begin = 0;

	*parent_count = 0;
	while (begin < count)
	{
		size_t end = begin, entries_length = 0, base, position;
		UdbStorageIndexEntry *parent;
		UdbStorageResult result;
		uint64_t offset;

		while (end < count)
		{
			size_t entry_length = 16 + children[end].key_length;
			size_t needed = 8 + 4 * (end - begin + 2) + entries_length + entry_length;

			if (needed > UDB_STORAGE_PAGE_PAYLOAD)
				break;
			entries_length += entry_length;
			end++;
		}
		if (end == begin)
			return UDB_STORAGE_LIMIT;
		base = 8 + 4 * (end - begin + 1);
		udb_storage_put_u32(builder->payload, (uint32_t)(end - begin));
		udb_storage_put_u32(builder->payload + 4, level);
		position = base;
		for (size_t i = begin; i < end; i++)
		{
			udb_storage_put_u32(builder->payload + 8 + 4 * (i - begin), (uint32_t)position);
			udb_storage_put_u64(builder->payload + position, children[i].offset);
			udb_storage_put_u64(builder->payload + position + 8, children[i].generation);
			memcpy(builder->payload + position + 16, children[i].key, children[i].key_length);
			position += 16 + children[i].key_length;
		}
		udb_storage_put_u32(builder->payload + 8 + 4 * (end - begin), (uint32_t)position);
		result = udb_storage_fragment_write_page(builder, UDB_STORAGE_PAGE_INDEX, position, &offset);
		if (result != UDB_STORAGE_OK)
			return result;
		parent = &parents[(*parent_count)++];
		parent->offset = offset;
		parent->generation = builder->generation;
		parent->key_length = children[end - 1].key_length;
		memcpy(parent->key, children[end - 1].key, parent->key_length);
		begin = end;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_builder_finish(UdbStorageFragmentBuilder *builder,
															UdbStorageFragment *fragment)
{
	UdbStorageResult result;
	size_t count, parent_count = 0;
	unsigned int input = 0;
	uint32_t level = 1;
	UdbStorageFragment candidate;

	if (!builder || !fragment || builder->failed || builder->finished || !builder->count)
		return UDB_STORAGE_ERROR;
	result = udb_storage_fragment_flush_data(builder);
	if (result != UDB_STORAGE_OK)
		return result;
	count = builder->data_pages;
	for (;;)
	{
		if (level > 3)
		{
			builder->failed = 1;
			return UDB_STORAGE_LIMIT;
		}
		result = udb_storage_fragment_build_index(builder, builder->level[input], count, level,
												  builder->level[1 - input], &parent_count);
		if (result != UDB_STORAGE_OK)
		{
			builder->failed = 1;
			return result;
		}
		input = 1 - input;
		if (parent_count == 1)
			break;
		count = parent_count;
		level++;
	}
	memset(&candidate, 0, sizeof(candidate));
	candidate.start = builder->start;
	candidate.length = (uint64_t)builder->total_pages * UDB_STORAGE_PAGE_SIZE;
	candidate.root = builder->level[input][0].offset;
	candidate.generation = builder->generation;
	candidate.object = builder->object;
	candidate.count = builder->count;
	candidate.data_pages = builder->data_pages;
	candidate.root_level = level;
	candidate.block = builder->block;
	candidate.stream = builder->stream;
	if (!udb_storage_fragment_valid(&candidate))
	{
		builder->failed = 1;
		return UDB_STORAGE_ERROR;
	}
	builder->finished = 1;
	*fragment = candidate;
	return UDB_STORAGE_OK;
}

/* The page codec validates complete offset tables on cache admission. */
static UdbStorageResult udb_storage_fragment_slots(const UdbStoragePage *page, uint32_t expected_level, uint32_t *count)
{
	if (!page->items || page->level != expected_level)
		return UDB_STORAGE_CORRUPT;
	*count = page->items;
	return UDB_STORAGE_OK;
}

static void udb_storage_fragment_slot(const UdbStoragePage *page, uint32_t slot, UdbStorageSpan *span)
{
	uint32_t begin = udb_storage_get_u32(page->payload.data + 8 + 4 * slot);
	uint32_t end = udb_storage_get_u32(page->payload.data + 8 + 4 * (slot + 1));
	span->data = page->payload.data + begin;
	span->length = end - begin;
}

static UdbStorageResult udb_storage_fragment_index_key(const UdbStoragePage *page, uint32_t slot,
													   const UdbStorageFragment *fragment, uint64_t page_offset,
													   uint32_t level, UdbStorageRow *key, uint64_t *child)
{
	UdbStorageSpan span;
	UdbStorageResult result;
	uint64_t offset, generation, data_end;

	udb_storage_fragment_slot(page, slot, &span);
	if (span.length < 16)
		return UDB_STORAGE_CORRUPT;
	offset = udb_storage_get_u64(span.data);
	generation = udb_storage_get_u64(span.data + 8);
	data_end = fragment->start + (uint64_t)fragment->data_pages * UDB_STORAGE_PAGE_SIZE;
	if (offset < fragment->start || offset >= page_offset || offset % UDB_STORAGE_PAGE_SIZE ||
		generation != fragment->generation || (level == 1 ? offset >= data_end : offset < data_end))
		return UDB_STORAGE_CORRUPT;
	result = udb_storage_row_decode(span.data + 16, span.length - 16, key);
	if (result != UDB_STORAGE_OK)
		return result;
	if (key->kind != UDB_STORAGE_VALUE_NONE || key->block != fragment->block)
		return UDB_STORAGE_CORRUPT;
	*child = offset;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_seek_data(UdbStorageCache *cache, UdbStorageFile *file,
													   const UdbStorageFragment *fragment, const UdbStorageRow *key,
													   uint64_t *data_offset)
{
	uint64_t offset;
	uint32_t level;
	UdbStorageCacheEntry *entry = NULL;
	UdbStorageResult result;
	UdbStoragePage page;
	uint32_t count, begin, end, slot;

	if (!data_offset)
		return UDB_STORAGE_ERROR;
	*data_offset = 0;
	if (!udb_storage_fragment_valid(fragment) || !udb_storage_row_valid(key) || key->block != fragment->block)
		return UDB_STORAGE_ERROR;
	offset = fragment->root;
	level = fragment->root_level;
	while (level)
	{
		UdbStorageRow separator;
		uint64_t child = 0;

		result = udb_storage_cache_acquire(cache, file, offset, fragment->generation, &entry);
		if (result != UDB_STORAGE_OK)
			return result;
		page = entry->page;
		if (page.kind != UDB_STORAGE_PAGE_INDEX || page.object != fragment->object)
		{
			result = UDB_STORAGE_CORRUPT;
			goto fail;
		}
		result = udb_storage_fragment_slots(&page, level, &count);
		if (result != UDB_STORAGE_OK)
			goto fail;
		begin = 0;
		end = count;
		while (begin < end)
		{
			slot = begin + (end - begin) / 2;
			result = udb_storage_fragment_index_key(&page, slot, fragment, offset, level, &separator, &child);
			if (result != UDB_STORAGE_OK)
				goto fail;
			if (udb_storage_key_compare(&separator, key) < 0)
				begin = slot + 1;
			else
				end = slot;
		}
		if (begin == count)
		{
			result = UDB_STORAGE_NOT_FOUND;
			goto fail;
		}
		result = udb_storage_fragment_index_key(&page, begin, fragment, offset, level, &separator, &child);
		if (result != UDB_STORAGE_OK)
			goto fail;
		offset = child;
		result = udb_storage_cache_release(cache, entry);
		entry = NULL;
		if (result != UDB_STORAGE_OK)
			return result;
		level--;
	}
	*data_offset = offset;
	return UDB_STORAGE_OK;
fail:
	if (entry && udb_storage_cache_release(cache, entry) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_storage_fragment_get(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageFragment *fragment, const UdbStorageRow *key,
												 UdbStorageRecord *record, UdbStorageCacheEntry **pin)
{
	UdbStorageCacheEntry *entry = NULL;
	UdbStoragePage page;
	UdbStorageResult result;
	uint64_t offset;
	uint32_t count, begin, end, slot;

	if (!record || !pin)
		return UDB_STORAGE_ERROR;
	*pin = NULL;
	result = udb_storage_fragment_seek_data(cache, file, fragment, key, &offset);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_cache_acquire(cache, file, offset, fragment->generation, &entry);
	if (result != UDB_STORAGE_OK)
		return result;
	page = entry->page;
	if (page.kind != UDB_STORAGE_PAGE_DATA || page.object != fragment->object)
	{
		result = UDB_STORAGE_CORRUPT;
		goto fail;
	}
	result = udb_storage_fragment_slots(&page, 0, &count);
	if (result != UDB_STORAGE_OK)
		goto fail;
	begin = 0;
	end = count;
	while (begin < end)
	{
		UdbStorageRecord candidate;
		UdbStorageSpan span;

		slot = begin + (end - begin) / 2;
		udb_storage_fragment_slot(&page, slot, &span);
		result = udb_storage_record_decode(span.data, span.length, &candidate);
		if (result != UDB_STORAGE_OK)
			goto fail;
		if (candidate.row.block != fragment->block || !udb_storage_record_matches_stream(&candidate, fragment->stream))
		{
			result = UDB_STORAGE_CORRUPT;
			goto fail;
		}
		if (udb_storage_key_compare(&candidate.row, key) < 0)
			begin = slot + 1;
		else
			end = slot;
	}
	if (begin < count)
	{
		UdbStorageSpan span;
		UdbStorageRecord candidate;

		udb_storage_fragment_slot(&page, begin, &span);
		result = udb_storage_record_decode(span.data, span.length, &candidate);
		if (result != UDB_STORAGE_OK)
			goto fail;
		if (candidate.row.block != fragment->block || !udb_storage_record_matches_stream(&candidate, fragment->stream))
		{
			result = UDB_STORAGE_CORRUPT;
			goto fail;
		}
		if (!udb_storage_key_compare(&candidate.row, key))
		{
			*record = candidate;
			*pin = entry;
			return UDB_STORAGE_OK;
		}
	}
	result = UDB_STORAGE_NOT_FOUND;
fail:
	if (entry && udb_storage_cache_release(cache, entry) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_storage_fragment_maximum(UdbStorageCache *cache, UdbStorageFile *file,
													 const UdbStorageFragment *fragment, UdbStorageRow *key,
													 UdbStorageCacheEntry **pin)
{
	UdbStorageResult result;
	uint64_t child;
	if (!key || !pin || !udb_storage_fragment_valid(fragment))
		return UDB_STORAGE_ERROR;
	*pin = NULL;
	result = udb_storage_cache_acquire(cache, file, fragment->root, fragment->generation, pin);
	if (result != UDB_STORAGE_OK)
		return result;
	if ((*pin)->page.kind != UDB_STORAGE_PAGE_INDEX || (*pin)->page.object != fragment->object ||
		(*pin)->page.level != fragment->root_level || !(*pin)->page.items)
		result = UDB_STORAGE_CORRUPT;
	else
		result = udb_storage_fragment_index_key(&(*pin)->page, (*pin)->page.items - 1, fragment, fragment->root,
												fragment->root_level, key, &child);
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_cache_release(cache, *pin);
		*pin = NULL;
	}
	return result;
}

struct UdbStorageFragmentIterator
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageFragment fragment;
	UdbStorageCacheEntry *entry;
	uint32_t page_number;
	uint32_t slot;
	uint64_t seen;
	size_t prefix_length;
	unsigned char prefix[UDB_STORAGE_KEY_MAX];
	int finished;
	UdbStorageResult failure;
};

static UdbStorageResult udb_storage_fragment_iterator_fail(UdbStorageFragmentIterator *iterator,
														   UdbStorageResult result)
{
	iterator->failure = result;
	return result;
}

static UdbStorageResult udb_storage_fragment_iterator_create(UdbStorageCache *cache, UdbStorageFile *file,
															 const UdbStorageFragment *fragment,
															 const UdbStorageRow *prefix,
															 UdbStorageFragmentIterator **out)
{
	UdbStorageFragmentIterator *iterator;
	UdbStorageResult result;
	uint64_t offset = 0;

	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || !file->opened || file->cache != cache || !udb_storage_fragment_valid(fragment) ||
		(prefix && (!udb_storage_row_valid(prefix) || prefix->block != fragment->block)))
		return UDB_STORAGE_ERROR;
	if (prefix)
	{
		result = udb_storage_fragment_seek_data(cache, file, fragment, prefix, &offset);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	iterator = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*iterator));
	if (!iterator)
		return UDB_STORAGE_LIMIT;
	iterator->cache = cache;
	iterator->file = file;
	iterator->fragment = *fragment;
	if (prefix)
	{
		UdbStorageRow key = *prefix;

		key.kind = UDB_STORAGE_VALUE_NONE;
		key.value.data = NULL;
		key.value.length = 0;
		key.number = 0;
		result = udb_storage_row_encode(&key, iterator->prefix, sizeof(iterator->prefix), &iterator->prefix_length);
		if (result != UDB_STORAGE_OK)
		{
			udb_storage_free(iterator);
			return result;
		}
		iterator->page_number = (uint32_t)((offset - fragment->start) / UDB_STORAGE_PAGE_SIZE);
	}
	*out = iterator;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_iterator_next(UdbStorageFragmentIterator *iterator,
														   UdbStorageRecord *record)
{
	UdbStorageResult result;
	UdbStoragePage page;
	UdbStorageRow prefix;

	if (!iterator || !record)
		return UDB_STORAGE_ERROR;
	if (iterator->failure)
		return iterator->failure;
	if (iterator->finished)
		return UDB_STORAGE_NOT_FOUND;
	if (iterator->prefix_length &&
		udb_storage_row_decode(iterator->prefix, iterator->prefix_length, &prefix) != UDB_STORAGE_OK)
		return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_ERROR);
	for (;;)
	{
		UdbStorageSpan span;
		UdbStorageRecord candidate;

		if (iterator->entry && iterator->slot == iterator->entry->page.items)
		{
			result = udb_storage_cache_release(iterator->cache, iterator->entry);
			if (result != UDB_STORAGE_OK)
				return udb_storage_fragment_iterator_fail(iterator, result);
			iterator->entry = NULL;
			iterator->slot = 0;
			iterator->page_number++;
		}
		if (iterator->page_number == iterator->fragment.data_pages)
		{
			iterator->finished = 1;
			if (!iterator->prefix_length && iterator->seen != iterator->fragment.count)
				return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_CORRUPT);
			return UDB_STORAGE_NOT_FOUND;
		}
		if (!iterator->entry)
		{
			uint64_t offset = iterator->fragment.start + (uint64_t)iterator->page_number * UDB_STORAGE_PAGE_SIZE;

			result = udb_storage_cache_acquire(iterator->cache, iterator->file, offset, iterator->fragment.generation,
											   &iterator->entry);
			if (result != UDB_STORAGE_OK)
				return udb_storage_fragment_iterator_fail(iterator, result);
		}
		page = iterator->entry->page;
		if (page.kind != UDB_STORAGE_PAGE_DATA || page.object != iterator->fragment.object || page.level ||
			iterator->slot >= page.items)
			return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_CORRUPT);
		udb_storage_fragment_slot(&page, iterator->slot++, &span);
		result = udb_storage_record_decode(span.data, span.length, &candidate);
		if (result != UDB_STORAGE_OK)
			return udb_storage_fragment_iterator_fail(iterator, result);
		if (candidate.row.block != iterator->fragment.block ||
			!udb_storage_record_matches_stream(&candidate, iterator->fragment.stream))
			return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_CORRUPT);
		if (iterator->prefix_length && !udb_storage_key_has_prefix(&candidate.row, &prefix))
		{
			if (udb_storage_key_compare(&candidate.row, &prefix) < 0)
				continue;
			result = udb_storage_cache_release(iterator->cache, iterator->entry);
			if (result != UDB_STORAGE_OK)
				return udb_storage_fragment_iterator_fail(iterator, result);
			iterator->entry = NULL;
			iterator->finished = 1;
			return UDB_STORAGE_NOT_FOUND;
		}
		if (!iterator->prefix_length && iterator->seen >= iterator->fragment.count)
			return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_CORRUPT);
		iterator->seen++;
		*record = candidate;
		return UDB_STORAGE_OK;
	}
}

static UdbStorageResult udb_storage_fragment_iterator_close(UdbStorageFragmentIterator **pointer)
{
	UdbStorageResult result;

	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	if ((*pointer)->entry)
	{
		result = udb_storage_cache_release((*pointer)->cache, (*pointer)->entry);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	udb_storage_free(*pointer);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

typedef struct UdbStorageFragmentNode
{
	uint64_t count;
	uint32_t level;
	unsigned int references;
	size_t first_length;
	size_t last_length;
	unsigned char first[UDB_STORAGE_KEY_MAX];
	unsigned char last[UDB_STORAGE_KEY_MAX];
} UdbStorageFragmentNode;

static UdbStorageResult udb_storage_fragment_copy_key(const UdbStorageRow *row, unsigned char *out, size_t *length)
{
	UdbStorageRow key = *row;

	key.kind = UDB_STORAGE_VALUE_NONE;
	key.value.data = NULL;
	key.value.length = 0;
	key.number = 0;
	return udb_storage_row_encode(&key, out, UDB_STORAGE_KEY_MAX, length);
}

/*
 * Startup/candidate validation bypasses cache admission. All scratch is charged,
 * with at most 64 nodes for one fragment, independent of database size. Check
 * reachability exactly once, parent levels, separator fidelity, nonoverlapping
 * child key ranges, all rows and the descriptor count. Callers still validate
 * the owning catalog, logical partition root and journal before publication.
 */
static UdbStorageResult udb_storage_fragment_validate(UdbStorageFile *file, const UdbStorageFragment *fragment,
													  UdbStorageRecordValidator validator, void *context)
{
	UdbStorageFragmentNode *nodes = NULL;
	unsigned char *bytes = NULL;
	unsigned char *previous = NULL;
	size_t previous_length = 0;
	UdbStorageBudget *budget;
	UdbStorageResult result = UDB_STORAGE_ERROR;
	size_t page_count;

	if (!file || !file->opened || !udb_storage_fragment_valid(fragment))
		return UDB_STORAGE_ERROR;
	budget = file->directory->budget;
	page_count = (size_t)(fragment->length / UDB_STORAGE_PAGE_SIZE);
	nodes = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, page_count * sizeof(*nodes));
	bytes = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	previous = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_KEY_MAX);
	if (!nodes || !bytes || !previous)
	{
		result = UDB_STORAGE_LIMIT;
		goto cleanup;
	}
	for (size_t i = 0; i < page_count; i++)
	{
		UdbStoragePage page;
		UdbStorageFragmentNode *node = &nodes[i];
		uint64_t offset = fragment->start + (uint64_t)i * UDB_STORAGE_PAGE_SIZE;
		int data = i < fragment->data_pages;

		result = udb_storage_file_read(file, offset, bytes, UDB_STORAGE_PAGE_SIZE);
		if (result != UDB_STORAGE_OK)
			goto cleanup;
		result = udb_storage_page_decode(bytes, UDB_STORAGE_PAGE_SIZE, &page);
		if (result != UDB_STORAGE_OK)
			goto cleanup;
		if (page.generation != fragment->generation || page.object != fragment->object ||
			page.ordinal != offset / UDB_STORAGE_PAGE_SIZE ||
			page.kind != (data ? UDB_STORAGE_PAGE_DATA : UDB_STORAGE_PAGE_INDEX))
		{
			result = UDB_STORAGE_CORRUPT;
			goto cleanup;
		}
		node->level = page.level;
		for (uint32_t j = 0; j < page.items; j++)
		{
			UdbStorageRow row;

			if (data)
			{
				UdbStorageSpan span;
				UdbStorageRecord record;

				udb_storage_fragment_slot(&page, j, &span);
				result = udb_storage_record_decode(span.data, span.length, &record);
				if (result != UDB_STORAGE_OK)
					goto cleanup;
				row = record.row;
				if (row.block != fragment->block || !udb_storage_record_matches_stream(&record, fragment->stream))
				{
					result = UDB_STORAGE_CORRUPT;
					goto cleanup;
				}
				if (previous_length)
				{
					UdbStorageRow prior;

					result = udb_storage_row_decode(previous, previous_length, &prior);
					if (result != UDB_STORAGE_OK || udb_storage_key_compare(&prior, &row) >= 0)
					{
						result = UDB_STORAGE_CORRUPT;
						goto cleanup;
					}
				}
				if (validator)
				{
					result = validator(&record, context);
					if (result != UDB_STORAGE_OK)
					{
						if (result == UDB_STORAGE_NOT_FOUND)
							result = UDB_STORAGE_CORRUPT;
						goto cleanup;
					}
				}
				if (!j)
				{
					result = udb_storage_fragment_copy_key(&row, node->first, &node->first_length);
					if (result != UDB_STORAGE_OK)
						goto cleanup;
				}
				result = udb_storage_fragment_copy_key(&row, previous, &previous_length);
				if (result != UDB_STORAGE_OK)
					goto cleanup;
				memcpy(node->last, previous, previous_length);
				node->last_length = previous_length;
				node->count++;
			}
			else
			{
				uint64_t child_offset;
				size_t child_number;
				UdbStorageFragmentNode *child;
				UdbStorageRow child_last, child_first;

				result = udb_storage_fragment_index_key(&page, j, fragment, offset, page.level, &row, &child_offset);
				if (result != UDB_STORAGE_OK)
					goto cleanup;
				child_number = (size_t)((child_offset - fragment->start) / UDB_STORAGE_PAGE_SIZE);
				child = &nodes[child_number];
				if (child->level + 1 != node->level || child->references ||
					child->count > fragment->count - node->count ||
					udb_storage_row_decode(child->last, child->last_length, &child_last) != UDB_STORAGE_OK ||
					udb_storage_row_decode(child->first, child->first_length, &child_first) != UDB_STORAGE_OK ||
					udb_storage_key_compare(&child_last, &row))
				{
					result = UDB_STORAGE_CORRUPT;
					goto cleanup;
				}
				if (j)
				{
					UdbStorageRow prior;

					if (udb_storage_row_decode(node->last, node->last_length, &prior) != UDB_STORAGE_OK ||
						udb_storage_key_compare(&prior, &child_first) >= 0)
					{
						result = UDB_STORAGE_CORRUPT;
						goto cleanup;
					}
				}
				else
				{
					memcpy(node->first, child->first, child->first_length);
					node->first_length = child->first_length;
				}
				memcpy(node->last, child->last, child->last_length);
				node->last_length = child->last_length;
				node->count += child->count;
				child->references++;
			}
		}
	}
	for (size_t i = 0; i < page_count; i++)
	{
		if (nodes[i].references != (i + 1 == page_count ? 0u : 1u))
		{
			result = UDB_STORAGE_CORRUPT;
			goto cleanup;
		}
	}
	if (nodes[page_count - 1].count != fragment->count || nodes[page_count - 1].level != fragment->root_level)
	{
		result = UDB_STORAGE_CORRUPT;
		goto cleanup;
	}
	result = UDB_STORAGE_OK;
cleanup:
	udb_storage_free(previous);
	udb_storage_free(bytes);
	udb_storage_free(nodes);
	return result;
}
