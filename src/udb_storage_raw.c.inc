/* Private external-sort streams: fixed paged records ordered by entity
 * partition, then canonical full key. These are never active block containers
 * and have no compatibility reader. DATA offset tables use the shared codec. */

static int udb_storage_raw_valid(const UdbStorageRawRun *run)
{
	return run && udb_storage_block_valid(run->block) && run->generation && run->object &&
		   !(run->start % UDB_STORAGE_PAGE_SIZE) && !(run->length % UDB_STORAGE_PAGE_SIZE) &&
		   udb_storage_io_range_valid(run->start, (size_t)run->length) &&
		   ((!run->count && !run->length) ||
			(run->count && run->length &&
			 run->count <= run->length / UDB_STORAGE_PAGE_SIZE * UDB_STORAGE_DATA_ROWS_MAX));
}

static UdbStorageResult udb_storage_raw_encode(const UdbStorageRawRun *run, unsigned char bytes[128])
{
	if (!bytes || !udb_storage_raw_valid(run) || udb_storage_overlaps(bytes, 128, run, sizeof(*run)))
		return UDB_STORAGE_ERROR;
	memset(bytes, 0, 128);
	memcpy(bytes, "UDBSORT4", 8);
	bytes[8] = run->block;
	udb_storage_put_u64(bytes + 16, run->generation);
	udb_storage_put_u64(bytes + 24, run->object);
	udb_storage_put_u64(bytes + 32, run->start);
	udb_storage_put_u64(bytes + 40, run->length);
	udb_storage_put_u64(bytes + 48, run->count);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_decode(const unsigned char bytes[128], UdbStorageRawRun *run)
{
	UdbStorageRawRun candidate;
	if (!bytes || !run || udb_storage_overlaps(bytes, 128, run, sizeof(*run)))
		return UDB_STORAGE_ERROR;
	if (memcmp(bytes, "UDBSORT4", 8) || !udb_storage_zero(bytes + 9, 7) || !udb_storage_zero(bytes + 56, 72))
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.block = bytes[8];
	candidate.generation = udb_storage_get_u64(bytes + 16);
	candidate.object = udb_storage_get_u64(bytes + 24);
	candidate.start = udb_storage_get_u64(bytes + 32);
	candidate.length = udb_storage_get_u64(bytes + 40);
	candidate.count = udb_storage_get_u64(bytes + 48);
	if (!udb_storage_raw_valid(&candidate))
		return UDB_STORAGE_CORRUPT;
	*run = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_partition(const UdbStorageRecord *record, unsigned int *partition)
{
	unsigned char hash[32];
	if (record->operation != UDB_STORAGE_PUT)
		return UDB_STORAGE_CORRUPT;
	return udb_storage_key_hash(&record->row, hash, partition);
}

struct UdbStorageRawWriter
{
	UdbStorageFile *file;
	UdbStorageRawRun run;
	unsigned char rows[UDB_STORAGE_PAGE_PAYLOAD];
	unsigned char page[65536];
	unsigned char payload[UDB_STORAGE_PAGE_PAYLOAD];
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	uint32_t offsets[UDB_STORAGE_DATA_ROWS_MAX + 1];
	size_t previous_length;
	size_t used;
	unsigned int partition;
	unsigned int pending;
	UdbStorageResult failed;
	int finished;
};

static void udb_storage_raw_writer_close(UdbStorageRawWriter **pointer)
{
	if (!pointer || !*pointer)
		return;
	udb_storage_free(*pointer);
	*pointer = NULL;
}

static UdbStorageResult udb_storage_raw_writer_create(UdbStorageFile *file, unsigned char block, uint64_t generation,
													  uint64_t object, UdbStorageRawWriter **out)
{
	UdbStorageRawWriter *writer;
	UdbStorageResult result;
	uint64_t size;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!file || !file->opened || !generation || !object || !udb_storage_block_valid(block))
		return UDB_STORAGE_ERROR;
	result = udb_storage_file_size(file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	writer = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*writer));
	if (!writer)
		return UDB_STORAGE_LIMIT;
	writer->file = file;
	writer->run.block = block;
	writer->run.generation = generation;
	writer->run.object = object;
	writer->run.start = size;
	*out = writer;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_writer_flush(UdbStorageRawWriter *writer)
{
	UdbStoragePage page;
	UdbStorageResult result;
	size_t base = 8 + 4 * (writer->pending + 1);
	if (!writer->pending)
		return UDB_STORAGE_OK;
	memset(&page, 0, sizeof(page));
	page.kind = UDB_STORAGE_PAGE_DATA;
	page.generation = writer->run.generation;
	page.object = writer->run.object;
	page.ordinal = (writer->run.start + writer->run.length) / UDB_STORAGE_PAGE_SIZE;
	page.payload.data = writer->payload;
	page.payload.length = (uint32_t)(base + writer->used);
	udb_storage_put_u32(writer->payload, writer->pending);
	udb_storage_put_u32(writer->payload + 4, 0);
	for (unsigned int i = 0; i <= writer->pending; i++)
		udb_storage_put_u32(writer->payload + 8 + 4 * i, (uint32_t)base + writer->offsets[i]);
	memcpy(writer->payload + base, writer->rows, writer->used);
	result = udb_storage_page_encode(&page, writer->page, sizeof(writer->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(writer->file, writer->run.start + writer->run.length, writer->page,
										sizeof(writer->page));
	if (result != UDB_STORAGE_OK)
		return result;
	writer->run.length += UDB_STORAGE_PAGE_SIZE;
	writer->pending = 0;
	writer->used = 0;
	writer->offsets[0] = 0;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_writer_add(UdbStorageRawWriter *writer, const unsigned char *bytes,
												   size_t length)
{
	UdbStorageRecord record;
	UdbStorageRow previous;
	UdbStorageResult result;
	unsigned int partition;
	if (!writer || writer->finished || !bytes || udb_storage_overlaps(bytes, length, writer, sizeof(*writer)))
		return UDB_STORAGE_ERROR;
	if (writer->failed)
		return writer->failed;
	result = udb_storage_record_decode(bytes, length, &record);
	if (result != UDB_STORAGE_OK)
		return writer->failed = result;
	if (record.row.block != writer->run.block)
		return writer->failed = UDB_STORAGE_CORRUPT;
	result = udb_storage_raw_partition(&record, &partition);
	if (result != UDB_STORAGE_OK)
		return writer->failed = result;
	if (writer->previous_length)
	{
		result = udb_storage_row_decode(writer->previous, writer->previous_length, &previous);
		if (result != UDB_STORAGE_OK || partition < writer->partition ||
			(partition == writer->partition && udb_storage_key_compare(&previous, &record.row) >= 0))
			return writer->failed = UDB_STORAGE_CORRUPT;
	}
	if (writer->pending == UDB_STORAGE_DATA_ROWS_MAX ||
		8 + 4 * (writer->pending + 2) + writer->used + length > UDB_STORAGE_PAGE_PAYLOAD)
	{
		result = udb_storage_raw_writer_flush(writer);
		/* LIMIT at a job boundary leaves the incoming record unconsumed. */
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_LIMIT ? result : (writer->failed = result);
	}
	if (writer->run.count == UINT64_MAX || length > sizeof(writer->rows) - writer->used)
		return writer->failed = UDB_STORAGE_LIMIT;
	memcpy(writer->rows + writer->used, bytes, length);
	writer->used += length;
	writer->offsets[++writer->pending] = (uint32_t)writer->used;
	writer->run.count++;
	writer->partition = partition;
	result = udb_storage_run_copy_key(&record.row, writer->previous, &writer->previous_length);
	return result == UDB_STORAGE_OK ? result : (writer->failed = result);
}

static UdbStorageResult udb_storage_raw_writer_finish(UdbStorageRawWriter *writer, UdbStorageRawRun *run)
{
	UdbStorageResult result;
	if (!writer || !run || writer->finished || udb_storage_overlaps(run, sizeof(*run), writer, sizeof(*writer)))
		return UDB_STORAGE_ERROR;
	if (writer->failed)
		return writer->failed;
	result = udb_storage_raw_writer_flush(writer);
	if (result != UDB_STORAGE_OK)
		return result == UDB_STORAGE_LIMIT ? result : (writer->failed = result);
	writer->finished = 1;
	*run = writer->run;
	return UDB_STORAGE_OK;
}

struct UdbStorageRawReader
{
	UdbStorageFile *file;
	UdbStorageRawRun run;
	UdbStoragePage page;
	unsigned char bytes[65536];
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	size_t previous_length;
	uint64_t offset;
	uint64_t count;
	unsigned int slot;
	unsigned int seek_slot;
	unsigned int partition;
	UdbStorageResult failed;
	int loaded;
	int finished;
};

static void udb_storage_raw_reader_close(UdbStorageRawReader **pointer)
{
	if (!pointer || !*pointer)
		return;
	udb_storage_free(*pointer);
	*pointer = NULL;
}

static UdbStorageResult udb_storage_raw_reader_create(UdbStorageFile *file, const UdbStorageRawRun *run,
													  UdbStorageRawReader **out)
{
	UdbStorageRawReader *reader;
	uint64_t size;
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!file || !file->opened || !udb_storage_raw_valid(run))
		return UDB_STORAGE_ERROR;
	result = udb_storage_file_size(file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (run->start > size || run->length > size - run->start)
		return UDB_STORAGE_CORRUPT;
	reader = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*reader));
	if (!reader)
		return UDB_STORAGE_LIMIT;
	reader->file = file;
	reader->run = *run;
	reader->offset = run->start;
	*out = reader;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_reader_next(UdbStorageRawReader *reader, UdbStorageRecord *record)
{
	UdbStorageResult result;
	UdbStorageRow previous;
	uint32_t start, end;
	unsigned int partition;
	if (!reader || !record || udb_storage_overlaps(record, sizeof(*record), reader, sizeof(*reader)))
		return UDB_STORAGE_ERROR;
	if (reader->failed)
		return reader->failed;
	if (reader->finished)
		return UDB_STORAGE_NOT_FOUND;
	if (!reader->loaded || reader->slot == reader->page.items)
	{
		if (reader->offset == reader->run.start + reader->run.length)
		{
			if (reader->count != reader->run.count)
				return reader->failed = UDB_STORAGE_CORRUPT;
			reader->finished = 1;
			return UDB_STORAGE_NOT_FOUND;
		}
		result = udb_storage_file_read(reader->file, reader->offset, reader->bytes, sizeof(reader->bytes));
		if (result == UDB_STORAGE_OK)
			result = udb_storage_page_decode(reader->bytes, sizeof(reader->bytes), &reader->page);
		if (result != UDB_STORAGE_OK)
			return reader->failed = result;
		if (reader->page.kind != UDB_STORAGE_PAGE_DATA || reader->page.level ||
			reader->page.generation != reader->run.generation || reader->page.object != reader->run.object ||
			reader->page.ordinal != reader->offset / UDB_STORAGE_PAGE_SIZE)
			return reader->failed = UDB_STORAGE_CORRUPT;
		reader->offset += UDB_STORAGE_PAGE_SIZE;
		reader->slot = reader->seek_slot;
		reader->seek_slot = 0;
		if (reader->slot >= reader->page.items)
			return reader->failed = UDB_STORAGE_CORRUPT;
		reader->loaded = 1;
	}
	start = udb_storage_get_u32(reader->page.payload.data + 8 + 4 * reader->slot);
	end = udb_storage_get_u32(reader->page.payload.data + 12 + 4 * reader->slot);
	result = udb_storage_record_decode(reader->page.payload.data + start, end - start, record);
	if (result != UDB_STORAGE_OK)
		return reader->failed = result;
	if (record->row.block != reader->run.block || reader->count >= reader->run.count)
		return reader->failed = UDB_STORAGE_CORRUPT;
	result = udb_storage_raw_partition(record, &partition);
	if (result != UDB_STORAGE_OK)
		return reader->failed = result;
	if (reader->previous_length)
	{
		result = udb_storage_row_decode(reader->previous, reader->previous_length, &previous);
		if (result != UDB_STORAGE_OK || partition < reader->partition ||
			(partition == reader->partition && udb_storage_key_compare(&previous, &record->row) >= 0))
			return reader->failed = UDB_STORAGE_CORRUPT;
	}
	result = udb_storage_run_copy_key(&record->row, reader->previous, &reader->previous_length);
	if (result != UDB_STORAGE_OK)
		return reader->failed = result;
	reader->count++;
	reader->slot++;
	reader->partition = partition;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_reader_seek(UdbStorageRawReader *reader, uint64_t offset, unsigned int slot,
													uint64_t count)
{
	if (!reader || reader->loaded || reader->count || reader->failed || reader->finished ||
		offset < reader->run.start || offset >= reader->run.start + reader->run.length ||
		offset % UDB_STORAGE_PAGE_SIZE || slot >= UDB_STORAGE_DATA_ROWS_MAX || count >= reader->run.count)
		return UDB_STORAGE_ERROR;
	reader->offset = offset;
	reader->seek_slot = slot;
	reader->count = count;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_reader_position(UdbStorageRawReader *reader, uint64_t *offset,
														unsigned int *slot, uint64_t *count)
{
	if (!reader || !offset || !slot || !count || !reader->loaded || !reader->slot || !reader->count)
		return UDB_STORAGE_ERROR;
	*offset = reader->offset - UDB_STORAGE_PAGE_SIZE;
	*slot = reader->slot - 1;
	*count = reader->count - 1;
	return UDB_STORAGE_OK;
}
