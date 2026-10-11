/* Native external sort for private snapshots/rebuild input. Fixed memtables
 * spill partition/key-sorted streams; eight-way merge passes retain at most
 * eight page cursors. Every step caps total private output at 64 MiB. A fixed
 * 4096-entry partition locator avoids rescanning the full snapshot per leaf. */

#define UDB_STORAGE_SORT_ENTRIES 16384
#define UDB_STORAGE_SORT_DATA ((size_t)832 * 1024)
#define UDB_STORAGE_SORT_FANIN 8

typedef struct UdbStorageSortEntry
{
	uint32_t offset;
	uint32_t length;
	unsigned int partition;
} UdbStorageSortEntry;

typedef struct UdbStorageSortTable
{
	UdbStorageSortEntry entries[UDB_STORAGE_SORT_ENTRIES];
	unsigned char bytes[UDB_STORAGE_SORT_DATA];
	size_t used;
	unsigned int count;
} UdbStorageSortTable;

typedef struct UdbStorageSortPartition
{
	uint64_t offset;
	uint64_t before;
	uint64_t count;
	unsigned int slot;
} UdbStorageSortPartition;

typedef struct UdbStorageSortCursor
{
	UdbStorageRawReader *reader;
	UdbStorageRecord record;
	unsigned int partition;
	int present;
} UdbStorageSortCursor;

struct UdbStorageSort
{
	UdbStorageDirectory *directory;
	UdbStorageCache *cache;
	UdbStorageFile files[2];
	char names[2][64];
	int created[2];
	UdbStorageCatalogRoot runs[2];
	UdbStorageRawRun final;
	UdbStorageSortTable *table;
	size_t table_charge;
	UdbStorageSortCursor cursors[UDB_STORAGE_SORT_FANIN];
	UdbStorageRawWriter *writer;
	UdbStorageRawReader *scanner;
	UdbStorageSortPartition partitions[UDB_STORAGE_BASE_PARTITIONS];
	UdbStorageRecordValidator validator;
	void *context;
	unsigned char bytes[UDB_STORAGE_RECORD_MAX];
	uint64_t rows;
	uint64_t rows_limit;
	uint64_t physical_limit;
	uint64_t input_next;
	uint64_t generation;
	uint64_t sequence;
	uint32_t ordinal_base;
	unsigned int readers;
	unsigned int cursor_count;
	unsigned int input;
	unsigned int output;
	unsigned char block;
	unsigned int phase; /* 0 ingest, 1 sealed, 2 merge, 3 scan, 4 ready */
	int job_full_cap;
	int source_set;
	UdbStorageResult failed;
};

static void udb_storage_sort_table_drop(UdbStorageSort *sort)
{
	if (sort->table)
	{
		udb_storage_free(sort->table);
		sort->table = NULL;
		udb_storage_memtable_uncharge(sort->directory->budget, sort->table_charge);
		sort->table_charge = 0;
	}
}

static UdbStorageResult udb_storage_sort_close(UdbStorageSort **pointer)
{
	UdbStorageSort *sort;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	sort = *pointer;
	if (sort->readers)
		return UDB_STORAGE_BUSY;
	udb_storage_sort_table_drop(sort);
	udb_storage_raw_writer_close(&sort->writer);
	udb_storage_raw_reader_close(&sort->scanner);
	for (unsigned int i = 0; i < UDB_STORAGE_SORT_FANIN; i++)
		udb_storage_raw_reader_close(&sort->cursors[i].reader);
	for (unsigned int i = 0; i < 2; i++)
	{
		if (sort->files[i].opened)
		{
			result = udb_storage_file_close(sort->files + i);
			if (result != UDB_STORAGE_OK)
				return result;
		}
		if (sort->created[i])
		{
			result = udb_storage_file_remove_private(sort->directory, sort->names[i]);
			if (result != UDB_STORAGE_OK)
				return result;
			sort->created[i] = 0;
		}
	}
	udb_storage_free(sort);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_create(UdbStorageDirectory *directory, unsigned char block,
												uint64_t rows_limit, uint64_t physical_limit,
												UdbStorageRecordValidator validator, void *context,
												UdbStorageSort **out)
{
	static const char hex[] = "0123456789abcdef";
	UdbStorageSort *sort;
	UdbStorageResult result;
	unsigned char nonce[16];
	char token[33];
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!directory || !directory->opened || !directory->budget->cache || !udb_storage_block_valid(block) ||
		!rows_limit || physical_limit < UDB_STORAGE_PAGE_SIZE * 8 || physical_limit > INT64_MAX)
		return UDB_STORAGE_ERROR;
	sort = udb_storage_alloc(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*sort));
	if (!sort)
		return UDB_STORAGE_LIMIT;
	sort->directory = directory;
	sort->cache = directory->budget->cache;
	sort->block = block;
	sort->rows_limit = rows_limit;
	sort->physical_limit = physical_limit;
	sort->validator = validator;
	sort->context = context;
	sort->generation = 1;
	sort->table_charge = udb_storage_allocation_charge(sizeof(*sort->table));
	result = udb_storage_memtable_charge(directory->budget, sort->table_charge);
	if (result != UDB_STORAGE_OK)
		goto fail;
	sort->table = udb_storage_alloc(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*sort->table));
	if (!sort->table)
	{
		udb_storage_memtable_uncharge(directory->budget, sort->table_charge);
		sort->table_charge = 0;
		result = UDB_STORAGE_LIMIT;
		goto fail;
	}
	if (RAND_bytes(nonce, sizeof(nonce)) != 1)
	{
		result = UDB_STORAGE_ERROR;
		goto fail;
	}
	for (unsigned int i = 0; i < sizeof(nonce); i++)
	{
		token[2 * i] = hex[nonce[i] >> 4];
		token[2 * i + 1] = hex[nonce[i] & 15];
	}
	token[32] = 0;
	for (unsigned int i = 0; i < 2; i++)
	{
		snprintf(sort->names[i], sizeof(sort->names[i]), "stage_%s_%c_%u.db", token, block, i);
		result = udb_storage_file_open(directory, sort->names[i], 2, sort->files + i);
		if (result != UDB_STORAGE_OK)
			goto fail;
		sort->created[i] = 1;
		result = udb_storage_private_header(sort->files + i, sort->names[i]);
		if (result != UDB_STORAGE_OK)
			goto fail;
		sort->runs[i].object = 1;
	}
	*out = sort;
	return UDB_STORAGE_OK;
fail:
	/* Preserve a failed object only if its native cleanup itself is blocked. */
	if (udb_storage_sort_close(&sort) != UDB_STORAGE_OK)
		*out = sort;
	return result;
}

static UdbStorageResult udb_storage_sort_job_begin(UdbStorageSort *sort, unsigned int target)
{
	uint64_t sizes[2], available;
	UdbStorageResult result;
	for (unsigned int i = 0; i < 2; i++)
	{
		result = udb_storage_file_size(sort->files + i, sizes + i);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (sizes[0] > sort->physical_limit || sizes[1] > sort->physical_limit - sizes[0])
		return UDB_STORAGE_LIMIT;
	available = sort->physical_limit - sizes[0] - sizes[1];
	sort->job_full_cap = available >= UDB_STORAGE_COMPACTION_MAX;
	if (available > UDB_STORAGE_COMPACTION_MAX)
		available = UDB_STORAGE_COMPACTION_MAX;
	if (available < UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(&sort->files[target], sizes[target] + available);
	return UDB_STORAGE_OK;
}

static int udb_storage_sort_compare(UdbStorageSortTable *table, const UdbStorageSortEntry *a,
									const UdbStorageSortEntry *b)
{
	UdbStorageRecord left, right;
	if (a->partition != b->partition)
		return a->partition < b->partition ? -1 : 1;
	/* Entries were validated before admission and their copied bytes are private. */
	if (udb_storage_record_decode(table->bytes + a->offset, a->length, &left) != UDB_STORAGE_OK ||
		udb_storage_record_decode(table->bytes + b->offset, b->length, &right) != UDB_STORAGE_OK)
		abort();
	return udb_storage_key_compare(&left.row, &right.row);
}

static void udb_storage_sort_sift(UdbStorageSortTable *table, unsigned int index, unsigned int count)
{
	while (index < count / 2)
	{
		unsigned int child = 2 * index + 1;
		UdbStorageSortEntry temporary;
		if (child + 1 < count &&
			udb_storage_sort_compare(table, table->entries + child, table->entries + child + 1) < 0)
			child++;
		if (udb_storage_sort_compare(table, table->entries + index, table->entries + child) >= 0)
			break;
		temporary = table->entries[index];
		table->entries[index] = table->entries[child];
		table->entries[child] = temporary;
		index = child;
	}
}

static UdbStorageResult udb_storage_sort_run_get(UdbStorageSort *sort, unsigned int file, uint64_t ordinal,
												 UdbStorageRawRun *run)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (ordinal >= sort->runs[file].count)
		return UDB_STORAGE_NOT_FOUND;
	udb_storage_run_key(ordinal, key);
	result = udb_storage_catalog_get(sort->cache, sort->files + file, sort->runs + file, key, value);
	if (result == UDB_STORAGE_NOT_FOUND)
		return UDB_STORAGE_CORRUPT;
	if (result == UDB_STORAGE_OK)
		result = udb_storage_raw_decode(value, run);
	if (result == UDB_STORAGE_OK &&
		(run->block != sort->block || run->generation != sort->runs[file].generation || run->object != ordinal + 2))
		return UDB_STORAGE_CORRUPT;
	return result;
}

static UdbStorageResult udb_storage_sort_run_save(UdbStorageSort *sort, unsigned int file, const UdbStorageRawRun *run)
{
	unsigned char key[64], value[128];
	UdbStorageCatalogRoot changed;
	UdbStorageResult result = udb_storage_raw_encode(run, value);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_run_key(sort->runs[file].count, key);
	result = udb_storage_catalog_change(sort->cache, sort->files + file, sort->runs + file, run->generation, key, value,
										&changed);
	if (result == UDB_STORAGE_OK)
		sort->runs[file] = changed;
	return result;
}

static UdbStorageResult udb_storage_sort_spill(UdbStorageSort *sort)
{
	UdbStorageSortTable *table = sort->table;
	UdbStorageRawWriter *writer = NULL;
	UdbStorageRawRun run;
	UdbStorageResult result;
	if (!table || !table->count)
		return UDB_STORAGE_OK;
	result = udb_storage_sort_job_begin(sort, 0);
	if (result != UDB_STORAGE_OK)
		return result;
	for (unsigned int i = table->count / 2; i; i--)
		udb_storage_sort_sift(table, i - 1, table->count);
	for (unsigned int i = table->count; i > 1; i--)
	{
		UdbStorageSortEntry temporary = table->entries[0];
		table->entries[0] = table->entries[i - 1];
		table->entries[i - 1] = temporary;
		udb_storage_sort_sift(table, 0, i - 1);
	}
	for (unsigned int i = 1; i < table->count; i++)
		if (!udb_storage_sort_compare(table, table->entries + i - 1, table->entries + i))
		{
			result = UDB_STORAGE_REJECTED;
			goto done;
		}
	result = udb_storage_raw_writer_create(sort->files, sort->block, 1, sort->runs[0].count + 2, &writer);
	if (result != UDB_STORAGE_OK)
		goto done;
	for (unsigned int i = 0; i < table->count; i++)
	{
		UdbStorageSortEntry *entry = table->entries + i;
		result = udb_storage_raw_writer_add(writer, table->bytes + entry->offset, entry->length);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	result = udb_storage_raw_writer_finish(writer, &run);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_sort_run_save(sort, 0, &run);
	if (result == UDB_STORAGE_OK)
	{
		table->count = 0;
		table->used = 0;
	}
done:
	udb_storage_file_job_limit(&sort->files[0], 0);
	udb_storage_raw_writer_close(&writer);
	return result;
}

static UdbStorageResult udb_storage_sort_add(UdbStorageSort *sort, const unsigned char *bytes, size_t length)
{
	UdbStorageRecord record;
	UdbStorageResult result;
	unsigned int partition;
	UdbStorageSortEntry *entry;
	if (!sort || sort->phase || !bytes || !sort->table ||
		udb_storage_overlaps(bytes, length, sort->table, sizeof(*sort->table)))
		return UDB_STORAGE_ERROR;
	if (sort->failed)
		return sort->failed;
	if (sort->rows == sort->rows_limit)
		return sort->failed = UDB_STORAGE_LIMIT;
	result = udb_storage_record_decode(bytes, length, &record);
	if (result != UDB_STORAGE_OK)
		return sort->failed = result;
	if (record.row.block != sort->block || record.operation != UDB_STORAGE_PUT)
		return sort->failed = UDB_STORAGE_CORRUPT;
	if (sort->validator)
	{
		result = sort->validator(&record, sort->context);
		if (result != UDB_STORAGE_OK)
			return sort->failed = result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_CORRUPT : result;
	}
	result = udb_storage_raw_partition(&record, &partition);
	if (result != UDB_STORAGE_OK)
		return sort->failed = result;
	if (sort->table->count == UDB_STORAGE_SORT_ENTRIES || length > sizeof(sort->table->bytes) - sort->table->used)
	{
		result = udb_storage_sort_spill(sort);
		if (result != UDB_STORAGE_OK)
			return sort->failed = result;
	}
	entry = sort->table->entries + sort->table->count++;
	entry->offset = (uint32_t)sort->table->used;
	entry->length = (uint32_t)length;
	entry->partition = partition;
	memcpy(sort->table->bytes + sort->table->used, bytes, length);
	sort->table->used += length;
	sort->rows++;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_seal(UdbStorageSort *sort)
{
	UdbStorageResult result;
	if (!sort || sort->phase)
		return UDB_STORAGE_ERROR;
	if (sort->failed)
		return sort->failed;
	result = udb_storage_sort_spill(sort);
	if (result != UDB_STORAGE_OK)
		return sort->failed = result;
	udb_storage_sort_table_drop(sort);
	sort->phase = 1;
	if (!sort->rows)
	{
		memset(&sort->final, 0, sizeof(sort->final));
		sort->final.block = sort->block;
		sort->final.generation = 1;
		sort->final.object = 2;
		sort->phase = 4;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_cursor_next(UdbStorageSortCursor *cursor)
{
	UdbStorageResult result = udb_storage_raw_reader_next(cursor->reader, &cursor->record);
	if (result == UDB_STORAGE_NOT_FOUND)
	{
		cursor->present = 0;
		return UDB_STORAGE_OK;
	}
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_raw_partition(&cursor->record, &cursor->partition);
	if (result == UDB_STORAGE_OK)
		cursor->present = 1;
	return result;
}

static UdbStorageResult udb_storage_sort_pass_begin(UdbStorageSort *sort)
{
	UdbStorageResult result;
	sort->output = sort->input ^ 1;
	if (sort->generation == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_file_resize(sort->files + sort->output, UDB_STORAGE_PAGE_SIZE);
	if (result != UDB_STORAGE_OK)
		return result;
	memset(sort->runs + sort->output, 0, sizeof(sort->runs[0]));
	sort->runs[sort->output].object = 1;
	sort->generation++;
	sort->input_next = 0;
	sort->phase = 2;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_group_begin(UdbStorageSort *sort)
{
	UdbStorageResult result;
	sort->cursor_count = 0;
	while (sort->input_next < sort->runs[sort->input].count && sort->cursor_count < UDB_STORAGE_SORT_FANIN)
	{
		UdbStorageSortCursor *cursor = sort->cursors + sort->cursor_count;
		UdbStorageRawRun run;
		result = udb_storage_sort_run_get(sort, sort->input, sort->input_next++, &run);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_raw_reader_create(sort->files + sort->input, &run, &cursor->reader);
		if (result != UDB_STORAGE_OK)
			return result;
		sort->cursor_count++;
		result = udb_storage_sort_cursor_next(cursor);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return udb_storage_raw_writer_create(sort->files + sort->output, sort->block, sort->generation,
										 sort->runs[sort->output].count + 2, &sort->writer);
}

static UdbStorageResult udb_storage_sort_scan_begin(UdbStorageSort *sort)
{
	UdbStorageResult result = udb_storage_sort_run_get(sort, sort->input, 0, &sort->final);
	if (result != UDB_STORAGE_OK)
		return result;
	if (sort->final.count != sort->rows)
		return UDB_STORAGE_CORRUPT;
	result = udb_storage_file_resize(sort->files + (sort->input ^ 1), UDB_STORAGE_PAGE_SIZE);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_raw_reader_create(sort->files + sort->input, &sort->final, &sort->scanner);
	if (result == UDB_STORAGE_OK)
		sort->phase = 3;
	return result;
}

static UdbStorageResult udb_storage_sort_scan_step(UdbStorageSort *sort)
{
	UdbStorageRecord record;
	UdbStorageResult result;
	/* Bounded CPU/read work too: at most 65,536 records per scan job. */
	for (unsigned int i = 0; i < 65536; i++)
	{
		unsigned int partition, slot;
		uint64_t offset, before;
		UdbStorageSortPartition *entry;
		result = udb_storage_raw_reader_next(sort->scanner, &record);
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			udb_storage_raw_reader_close(&sort->scanner);
			sort->phase = 4;
			return UDB_STORAGE_OK;
		}
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_raw_partition(&record, &partition);
		if (result != UDB_STORAGE_OK)
			return result;
		entry = sort->partitions + partition;
		if (!entry->count)
		{
			result = udb_storage_raw_reader_position(sort->scanner, &offset, &slot, &before);
			if (result != UDB_STORAGE_OK)
				return result;
			entry->offset = offset;
			entry->slot = slot;
			entry->before = before;
		}
		entry->count++;
	}
	return UDB_STORAGE_BUSY;
}

static UdbStorageResult udb_storage_sort_step(UdbStorageSort *sort)
{
	UdbStorageResult result;
	UdbStorageRawRun run;
	size_t length;
	unsigned int records = 0;
	if (!sort || !sort->phase)
		return UDB_STORAGE_ERROR;
	if (sort->failed)
		return sort->failed;
	if (sort->phase == 4)
		return UDB_STORAGE_OK;
	if (sort->phase == 3)
	{
		result = udb_storage_sort_scan_step(sort);
		if (result < UDB_STORAGE_NOT_FOUND && result != UDB_STORAGE_BUSY)
			sort->failed = result;
		return result;
	}
	if (sort->phase == 1)
	{
		if (sort->runs[sort->input].count == 1)
			return (result = udb_storage_sort_scan_begin(sort)) == UDB_STORAGE_OK ? UDB_STORAGE_BUSY
																				  : (sort->failed = result);
		result = udb_storage_sort_pass_begin(sort);
		if (result != UDB_STORAGE_OK)
			return sort->failed = result;
	}
	result = udb_storage_sort_job_begin(sort, sort->output);
	if (result != UDB_STORAGE_OK)
		return sort->failed = result;
	if (!sort->writer)
	{
		result = udb_storage_sort_group_begin(sort);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	for (;;)
	{
		UdbStorageSortCursor *minimum = NULL;
		for (unsigned int i = 0; i < sort->cursor_count; i++)
		{
			UdbStorageSortCursor *cursor = sort->cursors + i;
			int compare;
			if (!cursor->present)
				continue;
			if (!minimum)
			{
				minimum = cursor;
				continue;
			}
			compare = cursor->partition < minimum->partition ? -1
					  : cursor->partition > minimum->partition
						  ? 1
						  : udb_storage_key_compare(&cursor->record.row, &minimum->record.row);
			if (!compare)
			{
				result = UDB_STORAGE_REJECTED;
				goto done;
			}
			if (compare < 0)
				minimum = cursor;
		}
		if (!minimum)
			break;
		result = udb_storage_record_encode(&minimum->record, sort->bytes, sizeof(sort->bytes), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_raw_writer_add(sort->writer, sort->bytes, length);
		if (result == UDB_STORAGE_LIMIT)
		{
			/* Conservative reserve for the eventual run-directory COW path. */
			uint64_t size;
			UdbStorageResult sized = udb_storage_file_size(sort->files + sort->output, &size);
			if (sized != UDB_STORAGE_OK)
				result = sized;
			else if (size > sort->files[sort->output].append_limit)
				result = UDB_STORAGE_CORRUPT;
			else if (sort->job_full_cap && sort->files[sort->output].append_limit - size < UDB_STORAGE_PAGE_SIZE)
				result = UDB_STORAGE_BUSY;
			goto done;
		}
		if (result != UDB_STORAGE_OK)
			goto done;
		result = udb_storage_sort_cursor_next(minimum);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (++records == 65536)
		{
			result = UDB_STORAGE_BUSY;
			goto done;
		}
	}
	/* Ensure final DATA + directory metadata fit this output job. Yield before
	 * finish, not after consuming a descriptor that cannot yet be catalogued. */
	{
		uint64_t size;
		uint64_t reserve = (uint64_t)(2 * (sort->runs[sort->output].height + 1) + 2) * UDB_STORAGE_PAGE_SIZE;
		result = udb_storage_file_size(sort->files + sort->output, &size);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (reserve > sort->files[sort->output].append_limit - size)
		{
			result = sort->job_full_cap ? UDB_STORAGE_BUSY : UDB_STORAGE_LIMIT;
			goto done;
		}
	}
	result = udb_storage_raw_writer_finish(sort->writer, &run);
	if (result != UDB_STORAGE_OK)
		goto done;
	result = udb_storage_sort_run_save(sort, sort->output, &run);
	if (result != UDB_STORAGE_OK)
		goto done;
	udb_storage_raw_writer_close(&sort->writer);
	for (unsigned int i = 0; i < sort->cursor_count; i++)
		udb_storage_raw_reader_close(&sort->cursors[i].reader);
	sort->cursor_count = 0;
	if (sort->input_next == sort->runs[sort->input].count)
	{
		/* Entire pass has closed its old readers before reusing the private file. */
		sort->input = sort->output;
		sort->phase = 1;
	}
	result = UDB_STORAGE_BUSY;
done:
	udb_storage_file_job_limit(&sort->files[sort->output], 0);
	if (result != UDB_STORAGE_OK && result != UDB_STORAGE_BUSY)
		sort->failed = result;
	return result;
}

typedef struct UdbStorageSortSource
{
	UdbStorageSort *sort;
	UdbStorageRawReader *reader;
	UdbStorageLogicalPosition position;
	uint64_t remaining;
} UdbStorageSortSource;

static UdbStorageResult udb_storage_sort_source_open(void *opaque, const UdbStorageLogicalPosition *position,
													 void **out)
{
	UdbStorageSort *sort = opaque;
	UdbStorageSortSource *source;
	const UdbStorageSortPartition *partition;
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!sort || sort->phase != 4 || sort->failed || !udb_storage_logical_position_valid(position) ||
		position->block != sort->block || sort->readers == UINT_MAX)
		return UDB_STORAGE_ERROR;
	source = udb_storage_alloc(sort->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*source));
	if (!source)
		return UDB_STORAGE_LIMIT;
	source->sort = sort;
	source->position = *position;
	partition = sort->partitions + position->partition;
	source->remaining = partition->count;
	if (source->remaining)
	{
		result = udb_storage_raw_reader_create(sort->files + sort->input, &sort->final, &source->reader);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_raw_reader_seek(source->reader, partition->offset, partition->slot, partition->before);
		if (result != UDB_STORAGE_OK)
		{
			udb_storage_raw_reader_close(&source->reader);
			udb_storage_free(source);
			return result;
		}
	}
	sort->readers++;
	*out = source;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_source_next(void *opaque, UdbStorageRecord *record)
{
	UdbStorageSortSource *source = opaque;
	UdbStorageResult result;
	unsigned char hash[32];
	unsigned int partition;
	if (!source || !record)
		return UDB_STORAGE_ERROR;
	while (source->remaining)
	{
		result = udb_storage_raw_reader_next(source->reader, record);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_CORRUPT : result;
		result = udb_storage_key_hash(&record->row, hash, &partition);
		if (result != UDB_STORAGE_OK)
			return result;
		if (partition != source->position.partition)
			return UDB_STORAGE_CORRUPT;
		source->remaining--;
		if (!udb_storage_logical_contains(&source->position, hash, partition))
			continue;
		if (source->sort->sequence)
		{
			if (record->stamp.sequence || record->stamp.ordinal > UINT32_MAX - source->sort->ordinal_base)
				return UDB_STORAGE_CORRUPT;
			record->stamp.sequence = source->sort->sequence;
			record->stamp.ordinal += source->sort->ordinal_base;
		}
		return UDB_STORAGE_OK;
	}
	return UDB_STORAGE_NOT_FOUND;
}

static UdbStorageResult udb_storage_sort_source_close(void **pointer)
{
	UdbStorageSortSource *source;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	source = *pointer;
	udb_storage_raw_reader_close(&source->reader);
	if (!source->sort->readers)
		abort();
	source->sort->readers--;
	udb_storage_free(source);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_source(UdbStorageSort *sort, uint64_t sequence, uint32_t ordinal_base,
												UdbStorageSource *source)
{
	if (!sort || !source || sort->phase != 4 || sort->failed || sort->readers ||
		(sequence && sort->rows > (uint64_t)UINT32_MAX - ordinal_base + 1) || (!sequence && ordinal_base) ||
		(sort->source_set && (sequence != sort->sequence || ordinal_base != sort->ordinal_base)))
		return UDB_STORAGE_ERROR;
	sort->sequence = sequence;
	sort->ordinal_base = ordinal_base;
	sort->source_set = 1;
	source->open = udb_storage_sort_source_open;
	source->next = udb_storage_sort_source_next;
	source->close = udb_storage_sort_source_close;
	source->context = sort;
	return UDB_STORAGE_OK;
}

static uint64_t udb_storage_sort_rows(const UdbStorageSort *sort)
{
	return sort ? sort->rows : 0;
}
static UdbStorageResult udb_storage_sort_size(UdbStorageSort *sort, uint64_t *bytes)
{
	uint64_t sizes[2];
	UdbStorageResult result;
	if (!sort || !bytes)
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 0; i < 2; i++)
	{
		result = udb_storage_file_size(sort->files + i, sizes + i);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (sizes[0] > UINT64_MAX - sizes[1])
		return UDB_STORAGE_LIMIT;
	*bytes = sizes[0] + sizes[1];
	return UDB_STORAGE_OK;
}
static UdbStorageResult udb_storage_sort_partition_count(UdbStorageSort *sort, unsigned int partition, uint64_t *count)
{
	if (!sort || !count || sort->phase != 4 || sort->failed || partition >= UDB_STORAGE_BASE_PARTITIONS)
		return UDB_STORAGE_ERROR;
	*count = sort->partitions[partition].count;
	return UDB_STORAGE_OK;
}
