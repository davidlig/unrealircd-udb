/* Stable process-local ownership shared by staging and mutations. */
static uint64_t udb_native_owner_sequence;
static uint64_t udb_native_owner_next(void)
{
	return udb_native_owner_sequence == UINT64_MAX ? 0 : ++udb_native_owner_sequence;
}

/* Foreground canonical keys and copied, fixed-width disk profiles. Every call
 * owns its scratch and pin: native commands may synchronously re-enter hooks. */

static int udb_native_hex_digit(unsigned char c)
{
	return c >= '0' && c <= '9'	  ? c - '0'
		   : c >= 'a' && c <= 'f' ? c - 'a' + 10
		   : c >= 'A' && c <= 'F' ? c - 'A' + 10
								  : -1;
}

static UdbStorageResult udb_native_key_parse(const char *path, UdbNativeKey *key)
{
	if (!path || !key || strlen(path) < 4 || strlen(path) + UDB_S2S_OVERHEAD_MAX > UDB_S2S_LINE_MAX || path[1] != ':' ||
		path[2] != ':' || udb_storage_stage_block_index((unsigned char)path[0]) < 0)
		return UDB_STORAGE_REJECTED;
	memset(key, 0, sizeof(*key));
	key->row.block = (unsigned char)path[0];
	const unsigned char *cursor = (const unsigned char *)path + 3;
	while (*cursor)
	{
		if (key->row.depth == UDB_STORAGE_COMPONENTS_MAX)
			return UDB_STORAGE_REJECTED;
		size_t start = key->used;
		while (*cursor && !(cursor[0] == ':' && cursor[1] == ':'))
		{
			unsigned char value = *cursor++;
			if (value == '%')
			{
				if (!cursor[0] || !cursor[1])
					return UDB_STORAGE_REJECTED;
				int high = udb_native_hex_digit(cursor[0]), low = udb_native_hex_digit(cursor[1]);
				if (high < 0 || low < 0)
					return UDB_STORAGE_REJECTED;
				value = (unsigned char)((high << 4) | low);
				if (!value || (value != ':' && value != '%' && value > 32 && value < 127))
					return UDB_STORAGE_REJECTED;
				cursor += 2;
			}
			else if (value == ':' || value <= 32 || value >= 127)
				return UDB_STORAGE_REJECTED;
			if (key->used == sizeof(key->scratch) || key->used - start == UDB_STORAGE_COMPONENT_MAX)
				return UDB_STORAGE_REJECTED;
			key->scratch[key->used++] = value;
		}
		if (key->used == start)
			return UDB_STORAGE_REJECTED;
		key->row.components[key->row.depth].data = key->scratch + start;
		key->row.components[key->row.depth++].length = (uint32_t)(key->used - start);
		if (*cursor)
		{
			cursor += 2;
			if (!*cursor)
				return UDB_STORAGE_REJECTED;
		}
	}
	size_t length;
	UdbStorageResult result = udb_storage_row_encode(&key->row, key->bytes, sizeof(key->bytes), &length);
	return result == UDB_STORAGE_OK ? udb_storage_row_decode(key->bytes, length, &key->row) : result;
}

/* Escape spans directly; storage components are not NUL-terminated strings. */
static UdbStorageResult udb_native_key_path(const UdbStorageRow *row, char *out, size_t capacity)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t used = 0;
	if (!out || capacity < 2 || !udb_storage_row_valid(row))
		return UDB_STORAGE_ERROR;
	out[used++] = (char)row->block;
	for (unsigned int i = 0; i < row->depth; i++)
	{
		if (capacity - used <= 2)
			return UDB_STORAGE_LIMIT;
		out[used++] = ':';
		out[used++] = ':';
		UdbStorageSpan part = row->components[i];
		for (uint32_t j = 0; j < part.length; j++)
		{
			unsigned char c = part.data[j];
			int escape = c == ':' || c == '%' || c <= 32 || c >= 127;
			if (capacity - used <= (escape ? 3 : 1))
				return UDB_STORAGE_LIMIT;
			if (escape)
			{
				out[used++] = '%';
				out[used++] = hex[c >> 4];
				out[used++] = hex[c & 15];
			}
			else
				out[used++] = (char)c;
		}
	}
	out[used] = 0;
	return UDB_STORAGE_OK;
}

/* A profile copies only the named scalar fields. Variable children must use
 * read_iterator. Negative results must never become an absent profile. */
static UdbStorageResult udb_native_profile_load(UdbContext *ctx, unsigned char block, const char *entity,
												const char *const *fields, unsigned int count, UdbStorageProfile **out)
{
	UdbStorageRead *read = NULL;
	UdbStorageRow key;
	UdbStorageResult result, closed;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!ctx || !ctx->storage || !entity || !*entity)
		return UDB_STORAGE_ERROR;
	memset(&key, 0, sizeof(key));
	key.block = block;
	key.depth = 1;
	size_t length = strlen(entity);
	if (length > UDB_STORAGE_COMPONENT_MAX)
		return UDB_STORAGE_REJECTED;
	key.components[0].data = (const unsigned char *)entity;
	key.components[0].length = (uint32_t)length;
	result = udb_storage_read_open(ctx->storage, &read);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_profile_load(read, &key, fields, count, out);
	closed = udb_storage_read_close(&read);
	if (closed != UDB_STORAGE_OK)
	{
		udb_storage_profile_free(out);
		return closed;
	}
	return result;
}

/* One settings scalar or one entity property, within the caller's generation.
 * An explicit NOT_FOUND is the only permitted default/fallback signal. */
static UdbStorageResult udb_native_read_scalar(UdbStorageRead *read, unsigned char block, const char *entity,
											   const char *property, UdbStorageField *field)
{
	UdbStorageRow key = {0};
	if (!property || !*property || strlen(property) > UDB_STORAGE_COMPONENT_MAX ||
		(entity && (!*entity || strlen(entity) > UDB_STORAGE_COMPONENT_MAX)))
		return UDB_STORAGE_REJECTED;
	key.block = block;
	if (entity)
	{
		key.components[0].data = (const unsigned char *)entity;
		key.components[0].length = (uint32_t)strlen(entity);
		key.depth++;
	}
	key.components[key.depth].data = (const unsigned char *)property;
	key.components[key.depth++].length = (uint32_t)strlen(property);
	return udb_storage_read_field(read, &key, field);
}

static int udb_native_link_debug(UdbStorageRead *read, const char *server)
{
	if (!read || !server)
		return 0;
	UdbStorageField *field = udb_storage_alloc(&read->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*field));
	if (!field)
		return 0;
	UdbStorageResult result = udb_native_read_scalar(read, UDB_BLOCK_LINKS, server, LKEY_OPTIONS, field);
	int enabled =
		result == UDB_STORAGE_OK && field->kind == UDB_STORAGE_VALUE_NUMBER && (field->number & UDB_LNKOPT_DEBUG);
	udb_storage_free(field);
	return enabled;
}

static int udb_is_debug_enabled(void)
{
	UdbStorageRead *read = NULL;
	if (!udb_ctx || !udb_ctx->storage || udb_storage_read_open(udb_ctx->storage, &read) != UDB_STORAGE_OK)
		return 0;
	int enabled = udb_native_link_debug(read, me.name);
	if (udb_storage_read_close(&read) != UDB_STORAGE_OK)
		return 0;
	return enabled;
}
