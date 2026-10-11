/*
 * UDB 4 native storage: canonical row and integrity-checked page codecs.
 * Structural validation here does not replace block-schema/S2S validation.
 */

static uint16_t udb_storage_get_u16(const unsigned char *p)
{
	return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t udb_storage_get_u32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t udb_storage_get_u64(const unsigned char *p)
{
	return (uint64_t)udb_storage_get_u32(p) | ((uint64_t)udb_storage_get_u32(p + 4) << 32);
}

static void udb_storage_put_u16(unsigned char *p, uint16_t n)
{
	p[0] = (unsigned char)n;
	p[1] = (unsigned char)(n >> 8);
}

static void udb_storage_put_u32(unsigned char *p, uint32_t n)
{
	for (unsigned int i = 0; i < 4; i++)
		p[i] = (unsigned char)(n >> (i * 8));
}

static void udb_storage_put_u64(unsigned char *p, uint64_t n)
{
	for (unsigned int i = 0; i < 8; i++)
		p[i] = (unsigned char)(n >> (i * 8));
}

static unsigned char udb_storage_fold(unsigned char c)
{
	return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int udb_storage_block_valid(unsigned char block)
{
	return block == 'N' || block == 'C' || block == 'I' || block == 'S' || block == 'L' || block == 'K';
}

static int udb_storage_exact_component(const UdbStorageRow *row, unsigned int component)
{
	return row->block == 'K' && component == 1 && row->depth >= 2 && row->components[0].length == 1 &&
		   row->components[0].data && udb_storage_fold(row->components[0].data[0]) == 'f';
}

static int udb_storage_span_clean(UdbStorageSpan span, int component)
{
	if (span.length && !span.data)
		return 0;
	for (uint32_t i = 0; i < span.length; i++)
	{
		unsigned char c = span.data[i];

		if (!c || c == '\r' || c == '\n' || (component && (c < 32 || c == 127)))
			return 0;
	}
	return 1;
}

static int udb_storage_row_valid(const UdbStorageRow *row)
{
	size_t path_length = 1;

	if (!row || !udb_storage_block_valid(row->block) || !row->depth || row->depth > UDB_STORAGE_COMPONENTS_MAX)
		return 0;
	for (unsigned int i = 0; i < row->depth; i++)
	{
		UdbStorageSpan span = row->components[i];

		if (!span.length || span.length > UDB_STORAGE_COMPONENT_MAX || !udb_storage_span_clean(span, 1))
			return 0;
		path_length += 2 + span.length;
	}
	if (path_length >= UDB_STORAGE_PATH_MAX)
		return 0;
	switch (row->kind)
	{
	case UDB_STORAGE_VALUE_NONE:
	case UDB_STORAGE_VALUE_NUMBER:
		return row->value.length == 0;
	case UDB_STORAGE_VALUE_STRING:
		return row->value.length <= UDB_STORAGE_VALUE_MAX && udb_storage_span_clean(row->value, 0);
	default:
		return 0;
	}
}

/* Alias-safe preflight: output must not overlap any borrowed input span. */
static int udb_storage_overlaps(const void *a, size_t an, const void *b, size_t bn)
{
	uintptr_t ap = (uintptr_t)a, bp = (uintptr_t)b;

	if (!an || !bn)
		return 0;
	if (an > UINTPTR_MAX - ap || bn > UINTPTR_MAX - bp)
		return 1;
	return ap < bp + bn && bp < ap + an;
}

static UdbStorageResult udb_storage_row_encode(const UdbStorageRow *row, unsigned char *out, size_t capacity,
											   size_t *length)
{
	size_t needed = 16;
	size_t position;
	uint32_t value_length;

	if (!out || !length || !udb_storage_row_valid(row))
		return UDB_STORAGE_ERROR;
	if (udb_storage_overlaps(length, sizeof(*length), row, sizeof(*row)) ||
		udb_storage_overlaps(length, sizeof(*length), out, capacity) ||
		udb_storage_overlaps(length, sizeof(*length), row->value.data, row->value.length))
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 0; i < row->depth; i++)
		if (udb_storage_overlaps(length, sizeof(*length), row->components[i].data, row->components[i].length))
			return UDB_STORAGE_ERROR;
	*length = 0;
	value_length = row->kind == UDB_STORAGE_VALUE_NUMBER ? 8 : row->value.length;
	for (unsigned int i = 0; i < row->depth; i++)
		needed += 4 + row->components[i].length;
	needed += value_length;
	if (needed > capacity)
		return UDB_STORAGE_LIMIT;
	if (udb_storage_overlaps(out, needed, row, sizeof(*row)) ||
		udb_storage_overlaps(out, needed, length, sizeof(*length)) ||
		udb_storage_overlaps(out, needed, row->value.data, row->value.length))
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 0; i < row->depth; i++)
		if (udb_storage_overlaps(out, needed, row->components[i].data, row->components[i].length))
			return UDB_STORAGE_ERROR;
	memset(out, 0, 16);
	out[0] = row->block;
	out[1] = row->depth;
	out[2] = (unsigned char)row->kind;
	udb_storage_put_u32(out + 4, (uint32_t)needed);
	udb_storage_put_u32(out + 8, value_length);
	position = 16;
	for (unsigned int i = 0; i < row->depth; i++)
	{
		UdbStorageSpan span = row->components[i];
		int exact = udb_storage_exact_component(row, i);

		udb_storage_put_u32(out + position, span.length);
		position += 4;
		for (uint32_t j = 0; j < span.length; j++)
			out[position++] = exact ? span.data[j] : udb_storage_fold(span.data[j]);
	}
	if (row->kind == UDB_STORAGE_VALUE_NUMBER)
		udb_storage_put_u64(out + position, row->number);
	else if (value_length)
		memcpy(out + position, row->value.data, value_length);
	*length = needed;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_row_decode(const unsigned char *bytes, size_t length, UdbStorageRow *row)
{
	UdbStorageRow candidate;
	size_t position = 16;
	uint32_t value_length;

	if (!bytes || !row || length < 16 || length > UDB_STORAGE_ROW_MAX ||
		udb_storage_overlaps(bytes, length, row, sizeof(*row)))
		return UDB_STORAGE_ERROR;
	if (bytes[3] || udb_storage_get_u32(bytes + 12) || udb_storage_get_u32(bytes + 4) != length)
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.block = bytes[0];
	candidate.depth = bytes[1];
	candidate.kind = (UdbStorageValueKind)bytes[2];
	value_length = udb_storage_get_u32(bytes + 8);
	if (!candidate.depth || candidate.depth > UDB_STORAGE_COMPONENTS_MAX)
		return UDB_STORAGE_CORRUPT;
	for (unsigned int i = 0; i < candidate.depth; i++)
	{
		uint32_t component_length;

		if (length - position < 4)
			return UDB_STORAGE_CORRUPT;
		component_length = udb_storage_get_u32(bytes + position);
		position += 4;
		if (component_length > length - position)
			return UDB_STORAGE_CORRUPT;
		candidate.components[i].data = bytes + position;
		candidate.components[i].length = component_length;
		position += component_length;
	}
	if (value_length != length - position)
		return UDB_STORAGE_CORRUPT;
	if (candidate.kind == UDB_STORAGE_VALUE_NUMBER)
	{
		if (value_length != 8)
			return UDB_STORAGE_CORRUPT;
		candidate.number = udb_storage_get_u64(bytes + position);
	}
	else
	{
		candidate.value.data = bytes + position;
		candidate.value.length = value_length;
	}
	if (!udb_storage_row_valid(&candidate))
		return UDB_STORAGE_CORRUPT;
	for (unsigned int i = 0; i < candidate.depth; i++)
	{
		UdbStorageSpan span = candidate.components[i];

		if (udb_storage_exact_component(&candidate, i))
			continue;
		for (uint32_t j = 0; j < span.length; j++)
			if (span.data[j] != udb_storage_fold(span.data[j]))
				return UDB_STORAGE_CORRUPT;
	}
	*row = candidate;
	return UDB_STORAGE_OK;
}

/* Comparisons accept canonical rows or clean caller-supplied lookup keys. */
static int udb_storage_component_compare(const UdbStorageRow *a, const UdbStorageRow *b, unsigned int i)
{
	uint32_t length =
		a->components[i].length < b->components[i].length ? a->components[i].length : b->components[i].length;
	int a_exact = udb_storage_exact_component(a, i);
	int b_exact = udb_storage_exact_component(b, i);

	for (uint32_t j = 0; j < length; j++)
	{
		unsigned char ac = a->components[i].data[j], bc = b->components[i].data[j];

		if (!a_exact)
			ac = udb_storage_fold(ac);
		if (!b_exact)
			bc = udb_storage_fold(bc);
		if (ac != bc)
			return ac < bc ? -1 : 1;
	}
	if (a->components[i].length == b->components[i].length)
		return 0;
	return a->components[i].length < b->components[i].length ? -1 : 1;
}

static int udb_storage_key_compare(const UdbStorageRow *a, const UdbStorageRow *b)
{
	unsigned int depth = a->depth < b->depth ? a->depth : b->depth;

	if (a->block != b->block)
		return a->block < b->block ? -1 : 1;
	for (unsigned int i = 0; i < depth; i++)
	{
		int result = udb_storage_component_compare(a, b, i);

		if (result)
			return result;
	}
	return a->depth == b->depth ? 0 : a->depth < b->depth ? -1 : 1;
}

static int udb_storage_key_has_prefix(const UdbStorageRow *row, const UdbStorageRow *prefix)
{
	if (!row || !prefix || !udb_storage_row_valid(row) || !udb_storage_row_valid(prefix) ||
		row->block != prefix->block || row->depth < prefix->depth)
		return 0;
	for (unsigned int i = 0; i < prefix->depth; i++)
		if (udb_storage_component_compare(row, prefix, i))
			return 0;
	return 1;
}

/* Finalize into a full EVP buffer; only publish an exact SHA-256 digest. */
static int udb_storage_digest_final(EVP_MD_CTX *context, unsigned char digest[32])
{
	unsigned char candidate[EVP_MAX_MD_SIZE];
	unsigned int length = 0;

	if (!EVP_DigestFinal_ex(context, candidate, &length) || length != 32)
		return 0;
	memcpy(digest, candidate, 32);
	return 1;
}

static int udb_storage_hash_components(EVP_MD_CTX *context, const UdbStorageRow *row, unsigned int depth,
									   const char *domain, size_t domain_length, unsigned char hash[32])
{
	unsigned char size[4];
	unsigned char normalized[256];

	if (!EVP_DigestInit_ex(context, EVP_sha256(), NULL) || !EVP_DigestUpdate(context, domain, domain_length) ||
		!EVP_DigestUpdate(context, &row->block, 1))
		return 0;
	udb_storage_put_u32(size, depth);
	if (!EVP_DigestUpdate(context, size, sizeof(size)))
		return 0;
	for (unsigned int i = 0; i < depth; i++)
	{
		udb_storage_put_u32(size, row->components[i].length);
		if (!EVP_DigestUpdate(context, size, sizeof(size)))
			return 0;
		for (uint32_t j = 0; j < row->components[i].length;)
		{
			uint32_t remaining = row->components[i].length - j;
			size_t chunk = remaining < sizeof(normalized) ? remaining : sizeof(normalized);

			for (size_t k = 0; k < chunk; k++)
			{
				unsigned char c = row->components[i].data[j + k];

				normalized[k] = udb_storage_exact_component(row, i) ? c : udb_storage_fold(c);
			}
			if (!EVP_DigestUpdate(context, normalized, chunk))
				return 0;
			j += (uint32_t)chunk;
		}
	}
	return udb_storage_digest_final(context, hash);
}

static UdbStorageResult udb_storage_key_hash(const UdbStorageRow *row, unsigned char hash[32], unsigned int *partition)
{
	EVP_MD_CTX *context;
	int success;
	unsigned char entity[32];
	unsigned char key[32];
	unsigned int depth;
	static const char entity_domain[] = "UDB4/entity";
	static const char key_domain[] = "UDB4/key";

	if (!hash || !partition || !udb_storage_row_valid(row) || (row->block == 'K' && row->depth < 2))
		return UDB_STORAGE_ERROR;
	depth = row->block == 'K' ? 2 : 1;
	context = EVP_MD_CTX_new();
	if (!context)
		return UDB_STORAGE_ERROR;
	/* Reuse one context for both domains; no per-component allocations. */
	success = udb_storage_hash_components(context, row, depth, entity_domain, sizeof(entity_domain), entity) &&
			  udb_storage_hash_components(context, row, row->depth, key_domain, sizeof(key_domain), key);
	EVP_MD_CTX_free(context);
	if (!success)
		return UDB_STORAGE_ERROR;
	memcpy(hash, key, sizeof(key));
	*partition = ((unsigned int)entity[0] << 4) | ((unsigned int)entity[1] >> 4);
	return UDB_STORAGE_OK;
}

static int udb_storage_page_digest(const unsigned char *bytes, unsigned char digest[32])
{
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	static const char domain[] = "UDB4/page";
	int success;

	if (!context)
		return 0;
	success = EVP_DigestInit_ex(context, EVP_sha256(), NULL) && EVP_DigestUpdate(context, domain, sizeof(domain)) &&
			  EVP_DigestUpdate(context, bytes, 48) &&
			  EVP_DigestUpdate(context, bytes + 80, UDB_STORAGE_PAGE_SIZE - 80) &&
			  udb_storage_digest_final(context, digest);
	EVP_MD_CTX_free(context);
	return success;
}

static UdbStorageResult udb_storage_page_encode(const UdbStoragePage *page, unsigned char *out, size_t capacity)
{
	static const unsigned char magic[8] = {'U', 'D', 'B', 'P', 'G', '4', 0, 0};

	if (!page || !out || !page->generation || page->kind < UDB_STORAGE_PAGE_DATA ||
		page->kind > UDB_STORAGE_PAGE_METADATA || page->payload.length > UDB_STORAGE_PAGE_PAYLOAD ||
		(page->payload.length && !page->payload.data))
		return UDB_STORAGE_ERROR;
	if (capacity < UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_LIMIT;
	if (udb_storage_overlaps(out, UDB_STORAGE_PAGE_SIZE, page, sizeof(*page)) ||
		udb_storage_overlaps(out, UDB_STORAGE_PAGE_SIZE, page->payload.data, page->payload.length))
		return UDB_STORAGE_ERROR;
	memset(out, 0, UDB_STORAGE_PAGE_SIZE);
	memcpy(out, magic, sizeof(magic));
	udb_storage_put_u16(out + 8, UDB_STORAGE_FORMAT);
	udb_storage_put_u16(out + 10, (uint16_t)page->kind);
	udb_storage_put_u32(out + 12, page->payload.length);
	udb_storage_put_u64(out + 16, page->generation);
	udb_storage_put_u64(out + 24, page->object);
	udb_storage_put_u64(out + 32, page->ordinal);
	if (page->payload.length)
		memcpy(out + UDB_STORAGE_PAGE_HEADER, page->payload.data, page->payload.length);
	if (!udb_storage_page_digest(out, out + 48))
		return UDB_STORAGE_ERROR;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_page_read_slots(UdbStoragePage *page)
{
	const unsigned char *bytes = page->payload.data;
	uint32_t previous, next, count, level;
	size_t base;

	if (page->payload.length < 12)
		return UDB_STORAGE_CORRUPT;
	count = udb_storage_get_u32(bytes);
	level = udb_storage_get_u32(bytes + 4);
	if (!count ||
		count >
			(page->kind == UDB_STORAGE_PAGE_DATA ? UDB_STORAGE_DATA_ROWS_MAX : UDB_STORAGE_FRAGMENT_DATA_PAGES_MAX) ||
		(page->kind == UDB_STORAGE_PAGE_DATA ? level != 0 : (!level || level > 3)))
		return UDB_STORAGE_CORRUPT;
	base = 8 + ((size_t)count + 1) * 4;
	if (base >= page->payload.length || udb_storage_get_u32(bytes + 8) != base)
		return UDB_STORAGE_CORRUPT;
	previous = (uint32_t)base;
	for (uint32_t i = 1; i <= count; i++)
	{
		next = udb_storage_get_u32(bytes + 8 + 4 * i);
		if (next <= previous || next > page->payload.length)
			return UDB_STORAGE_CORRUPT;
		previous = next;
	}
	if (previous != page->payload.length)
		return UDB_STORAGE_CORRUPT;
	page->items = count;
	page->level = level;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_page_decode(const unsigned char *bytes, size_t length, UdbStoragePage *page)
{
	static const unsigned char magic[8] = {'U', 'D', 'B', 'P', 'G', '4', 0, 0};
	unsigned char digest[32];
	UdbStoragePage candidate;

	if (!bytes || !page || length != UDB_STORAGE_PAGE_SIZE || udb_storage_overlaps(bytes, length, page, sizeof(*page)))
		return UDB_STORAGE_ERROR;
	if (memcmp(bytes, magic, sizeof(magic)) || udb_storage_get_u16(bytes + 8) != UDB_STORAGE_FORMAT)
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.kind = (UdbStoragePageKind)udb_storage_get_u16(bytes + 10);
	candidate.payload.length = udb_storage_get_u32(bytes + 12);
	candidate.generation = udb_storage_get_u64(bytes + 16);
	candidate.object = udb_storage_get_u64(bytes + 24);
	candidate.ordinal = udb_storage_get_u64(bytes + 32);
	if (!candidate.generation || candidate.kind < UDB_STORAGE_PAGE_DATA || candidate.kind > UDB_STORAGE_PAGE_METADATA ||
		candidate.payload.length > UDB_STORAGE_PAGE_PAYLOAD)
		return UDB_STORAGE_CORRUPT;
	for (size_t i = 40; i < 48; i++)
		if (bytes[i])
			return UDB_STORAGE_CORRUPT;
	for (size_t i = 80; i < UDB_STORAGE_PAGE_HEADER; i++)
		if (bytes[i])
			return UDB_STORAGE_CORRUPT;
	for (size_t i = UDB_STORAGE_PAGE_HEADER + candidate.payload.length; i < length; i++)
		if (bytes[i])
			return UDB_STORAGE_CORRUPT;
	if (!udb_storage_page_digest(bytes, digest))
		return UDB_STORAGE_ERROR;
	if (memcmp(digest, bytes + 48, sizeof(digest)))
		return UDB_STORAGE_CORRUPT;
	candidate.payload.data = bytes + UDB_STORAGE_PAGE_HEADER;
	if ((candidate.kind == UDB_STORAGE_PAGE_DATA || candidate.kind == UDB_STORAGE_PAGE_INDEX) &&
		udb_storage_page_read_slots(&candidate) != UDB_STORAGE_OK)
		return UDB_STORAGE_CORRUPT;
	if (candidate.kind == UDB_STORAGE_PAGE_CATALOG && udb_storage_catalog_page_decode(&candidate) != UDB_STORAGE_OK)
		return UDB_STORAGE_CORRUPT;
	*page = candidate;
	return UDB_STORAGE_OK;
}

static int udb_storage_fragment_valid(const UdbStorageFragment *fragment)
{
	if (!fragment || !udb_storage_block_valid(fragment->block) || !fragment->generation || !fragment->object ||
		(fragment->stream != UDB_STORAGE_POINTS && fragment->stream != UDB_STORAGE_PREFIXES) || !fragment->count ||
		!fragment->data_pages || fragment->data_pages > UDB_STORAGE_FRAGMENT_DATA_PAGES_MAX ||
		fragment->count > (uint64_t)fragment->data_pages * UDB_STORAGE_DATA_ROWS_MAX || !fragment->root_level ||
		fragment->root_level > 3 || fragment->start % UDB_STORAGE_PAGE_SIZE ||
		fragment->length % UDB_STORAGE_PAGE_SIZE || fragment->length > UDB_STORAGE_FRAGMENT_MAX ||
		fragment->length < (uint64_t)(fragment->data_pages + fragment->root_level) * UDB_STORAGE_PAGE_SIZE ||
		fragment->start > INT64_MAX || fragment->length > (uint64_t)INT64_MAX - fragment->start ||
		fragment->root != fragment->start + fragment->length - UDB_STORAGE_PAGE_SIZE)
		return 0;
	return 1;
}

static UdbStorageResult udb_storage_fragment_encode(const UdbStorageFragment *fragment, unsigned char *out,
													size_t capacity)
{
	static const unsigned char magic[8] = {'U', 'D', 'B', 'F', 'R', 'G', '4', 0};

	if (!out || !udb_storage_fragment_valid(fragment) ||
		udb_storage_overlaps(out, UDB_STORAGE_FRAGMENT_DESCRIPTOR_SIZE, fragment, sizeof(*fragment)))
		return UDB_STORAGE_ERROR;
	if (capacity < UDB_STORAGE_FRAGMENT_DESCRIPTOR_SIZE)
		return UDB_STORAGE_LIMIT;
	memset(out, 0, UDB_STORAGE_FRAGMENT_DESCRIPTOR_SIZE);
	memcpy(out, magic, sizeof(magic));
	udb_storage_put_u32(out + 8, UDB_STORAGE_FORMAT);
	udb_storage_put_u32(out + 12, fragment->block);
	udb_storage_put_u64(out + 16, fragment->start);
	udb_storage_put_u64(out + 24, fragment->length);
	udb_storage_put_u64(out + 32, fragment->root);
	udb_storage_put_u64(out + 40, fragment->generation);
	udb_storage_put_u64(out + 48, fragment->object);
	udb_storage_put_u64(out + 56, fragment->count);
	udb_storage_put_u32(out + 64, fragment->data_pages);
	udb_storage_put_u32(out + 68, fragment->root_level);
	out[72] = (unsigned char)fragment->stream;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_decode(const unsigned char *bytes, size_t length,
													UdbStorageFragment *fragment)
{
	static const unsigned char magic[8] = {'U', 'D', 'B', 'F', 'R', 'G', '4', 0};
	UdbStorageFragment candidate;
	uint32_t block;

	if (!bytes || !fragment || length != UDB_STORAGE_FRAGMENT_DESCRIPTOR_SIZE ||
		udb_storage_overlaps(bytes, length, fragment, sizeof(*fragment)))
		return UDB_STORAGE_ERROR;
	if (memcmp(bytes, magic, sizeof(magic)) || udb_storage_get_u32(bytes + 8) != UDB_STORAGE_FORMAT ||
		udb_storage_get_u32(bytes + 73) || bytes[77] || bytes[78] || bytes[79])
		return UDB_STORAGE_CORRUPT;
	block = udb_storage_get_u32(bytes + 12);
	if (block > UCHAR_MAX)
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.block = (unsigned char)block;
	candidate.start = udb_storage_get_u64(bytes + 16);
	candidate.length = udb_storage_get_u64(bytes + 24);
	candidate.root = udb_storage_get_u64(bytes + 32);
	candidate.generation = udb_storage_get_u64(bytes + 40);
	candidate.object = udb_storage_get_u64(bytes + 48);
	candidate.count = udb_storage_get_u64(bytes + 56);
	candidate.data_pages = udb_storage_get_u32(bytes + 64);
	candidate.root_level = udb_storage_get_u32(bytes + 68);
	candidate.stream = (UdbStorageStream)bytes[72];
	if (!udb_storage_fragment_valid(&candidate))
		return UDB_STORAGE_CORRUPT;
	*fragment = candidate;
	return UDB_STORAGE_OK;
}

static int udb_storage_stamp_compare(UdbStorageStamp a, UdbStorageStamp b)
{
	if (a.sequence != b.sequence)
		return a.sequence < b.sequence ? -1 : 1;
	return a.ordinal == b.ordinal ? 0 : a.ordinal < b.ordinal ? -1 : 1;
}

static int udb_storage_record_matches_stream(const UdbStorageRecord *record, UdbStorageStream stream)
{
	return record && (stream == UDB_STORAGE_PREFIXES
						  ? record->operation == UDB_STORAGE_PREFIX_DELETE
						  : stream == UDB_STORAGE_POINTS &&
								(record->operation == UDB_STORAGE_PUT || record->operation == UDB_STORAGE_DELETE));
}

static UdbStorageResult udb_storage_record_encode(const UdbStorageRecord *record, unsigned char *bytes, size_t capacity,
												  size_t *length)
{
	UdbStorageResult result;
	size_t row_length, span = capacity < UDB_STORAGE_RECORD_MAX ? capacity : UDB_STORAGE_RECORD_MAX;
	if (!record || !bytes || !length || !udb_storage_row_valid(&record->row) || record->operation < UDB_STORAGE_PUT ||
		record->operation > UDB_STORAGE_PREFIX_DELETE ||
		(record->operation != UDB_STORAGE_PUT && record->row.kind != UDB_STORAGE_VALUE_NONE) ||
		udb_storage_overlaps(bytes, span, record, sizeof(*record)) ||
		udb_storage_overlaps(length, sizeof(*length), record, sizeof(*record)) ||
		udb_storage_overlaps(length, sizeof(*length), bytes, span))
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 0; i <= record->row.depth; i++)
	{
		UdbStorageSpan input = i == record->row.depth ? record->row.value : record->row.components[i];
		if (udb_storage_overlaps(bytes, span, input.data, input.length) ||
			udb_storage_overlaps(length, sizeof(*length), input.data, input.length))
			return UDB_STORAGE_ERROR;
	}
	*length = 0;
	if (capacity < UDB_STORAGE_RECORD_HEADER)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_row_encode(&record->row, bytes + UDB_STORAGE_RECORD_HEADER,
									capacity - UDB_STORAGE_RECORD_HEADER, &row_length);
	if (result != UDB_STORAGE_OK)
		return result;
	memset(bytes, 0, UDB_STORAGE_RECORD_HEADER);
	memcpy(bytes, "UDR4", 4);
	bytes[4] = (unsigned char)record->operation;
	udb_storage_put_u32(bytes + 8, (uint32_t)(row_length + UDB_STORAGE_RECORD_HEADER));
	udb_storage_put_u32(bytes + 12, record->stamp.ordinal);
	udb_storage_put_u64(bytes + 16, record->stamp.sequence);
	*length = row_length + UDB_STORAGE_RECORD_HEADER;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_record_decode(const unsigned char *bytes, size_t length, UdbStorageRecord *record)
{
	UdbStorageRecord candidate;
	UdbStorageResult result;
	if (!bytes || !record || length < UDB_STORAGE_RECORD_HEADER + 16 || length > UDB_STORAGE_RECORD_MAX ||
		udb_storage_overlaps(bytes, length, record, sizeof(*record)))
		return UDB_STORAGE_ERROR;
	if (memcmp(bytes, "UDR4", 4) || bytes[5] || bytes[6] || bytes[7] || udb_storage_get_u32(bytes + 8) != length ||
		udb_storage_get_u64(bytes + 24))
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.operation = (UdbStorageOperation)bytes[4];
	candidate.stamp.ordinal = udb_storage_get_u32(bytes + 12);
	candidate.stamp.sequence = udb_storage_get_u64(bytes + 16);
	result =
		udb_storage_row_decode(bytes + UDB_STORAGE_RECORD_HEADER, length - UDB_STORAGE_RECORD_HEADER, &candidate.row);
	if (result != UDB_STORAGE_OK)
		return result;
	if (candidate.operation < UDB_STORAGE_PUT || candidate.operation > UDB_STORAGE_PREFIX_DELETE ||
		(candidate.operation != UDB_STORAGE_PUT && candidate.row.kind != UDB_STORAGE_VALUE_NONE))
		return UDB_STORAGE_CORRUPT;
	*record = candidate;
	return UDB_STORAGE_OK;
}
