/* New-format value payloads retain one trailing IRC parameter while separating
 * NONE, uint64 zero and strings. No legacy '*' or untagged value decoder. */

static UdbStorageResult udb_native_value_encode(const UdbStorageRow *row, char *out, size_t capacity)
{
	if (!out || !udb_storage_row_valid(row) || capacity < 3)
		return UDB_STORAGE_ERROR;
	out[0] = '!';
	if (row->kind == UDB_STORAGE_VALUE_NONE)
	{
		out[1] = 'n';
		out[2] = 0;
	}
	else if (row->kind == UDB_STORAGE_VALUE_NUMBER)
	{
		int length = snprintf(out, capacity, "!u%" PRIu64, row->number);
		if (length < 0 || (size_t)length >= capacity)
			return UDB_STORAGE_LIMIT;
	}
	else
	{
		size_t length = 4 * (((size_t)row->value.length + 2) / 3);
		if (length + 3 > capacity)
			return UDB_STORAGE_LIMIT;
		out[1] = 's';
		if (row->value.length &&
			EVP_EncodeBlock((unsigned char *)out + 2, row->value.data, (int)row->value.length) != (int)length)
			return UDB_STORAGE_ERROR;
		out[2 + length] = 0;
	}
	return UDB_STORAGE_OK;
}

static int udb_native_base64_digit(unsigned char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	return c == '+' ? 62 : c == '/' ? 63 : -1;
}

static UdbStorageResult udb_native_value_decode(const char *wire, UdbStorageRow *row, unsigned char *value,
												size_t capacity)
{
	if (!wire || !row || wire[0] != '!' || !wire[1])
		return UDB_STORAGE_REJECTED;
	UdbStorageRow candidate = *row;
	candidate.number = 0;
	candidate.value.data = NULL;
	candidate.value.length = 0;
	if (wire[1] == 'n')
	{
		if (wire[2])
			return UDB_STORAGE_REJECTED;
		candidate.kind = UDB_STORAGE_VALUE_NONE;
	}
	else if (wire[1] == 'u')
	{
		uint64_t number;
		if (!wire[2] || (wire[2] == '0' && wire[3]) || !udb_parse_uint64_strict(wire + 2, &number))
			return UDB_STORAGE_REJECTED;
		candidate.kind = UDB_STORAGE_VALUE_NUMBER;
		candidate.number = number;
	}
	else if (wire[1] == 's')
	{
		size_t length = strlen(wire + 2), padding = 0;
		if (length % 4 || length > 4 * ((UDB_STORAGE_VALUE_MAX + 2) / 3) || !value)
			return UDB_STORAGE_REJECTED;
		const unsigned char *input = (const unsigned char *)wire + 2;
		if (length && input[length - 1] == '=')
		{
			padding = 1;
			if (input[length - 2] == '=')
				padding = 2;
		}
		for (size_t i = 0; i < length - padding; i++)
			if (udb_native_base64_digit(input[i]) < 0)
				return UDB_STORAGE_REJECTED;
		if ((padding == 1 && (udb_native_base64_digit(input[length - 2]) & 3)) ||
			(padding == 2 && (udb_native_base64_digit(input[length - 3]) & 15)))
			return UDB_STORAGE_REJECTED;
		size_t decoded = 3 * (length / 4) - padding;
		/* EVP writes all three bytes for the last padded quartet. */
		if (decoded > UDB_STORAGE_VALUE_MAX || 3 * (length / 4) > capacity)
			return UDB_STORAGE_LIMIT;
		if (length && EVP_DecodeBlock(value, input, (int)length) != (int)(decoded + padding))
			return UDB_STORAGE_REJECTED;
		candidate.kind = UDB_STORAGE_VALUE_STRING;
		candidate.value.data = value;
		candidate.value.length = (uint32_t)decoded;
	}
	else
		return UDB_STORAGE_REJECTED;
	if (!udb_storage_row_valid(&candidate))
		return UDB_STORAGE_REJECTED;
	*row = candidate;
	return UDB_STORAGE_OK;
}
