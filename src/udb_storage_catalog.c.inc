/*
 * UDB 4 native storage: bounded, immutable paged range catalog. Keys are opaque
 * canonical 64-byte identifiers; values are opaque 128-byte descriptors. Schema
 * validation belongs to the catalog owner. Updates append a COW path and return
 * a private root; the journal owner must flush, commit and publish that root.
 * Old roots remain readable until the generation-view owner permits reclamation.
 */

#define UDB_STORAGE_CATALOG_HEADER 16
#define UDB_STORAGE_CATALOG_LEAF_ENTRY (UDB_STORAGE_CATALOG_KEY_SIZE + UDB_STORAGE_CATALOG_VALUE_SIZE)
#define UDB_STORAGE_CATALOG_INDEX_ENTRY (UDB_STORAGE_CATALOG_KEY_SIZE + 24)
#define UDB_STORAGE_CATALOG_LEAF_MAX ((UDB_STORAGE_PAGE_PAYLOAD - 16) / UDB_STORAGE_CATALOG_LEAF_ENTRY)
#define UDB_STORAGE_CATALOG_INDEX_MAX ((UDB_STORAGE_PAGE_PAYLOAD - 16) / UDB_STORAGE_CATALOG_INDEX_ENTRY)

static size_t udb_storage_catalog_entry_size(uint32_t height)
{
	return height == 1 ? UDB_STORAGE_CATALOG_LEAF_ENTRY : UDB_STORAGE_CATALOG_INDEX_ENTRY;
}

static int udb_storage_catalog_root_valid(const UdbStorageCatalogRoot *root)
{
	if (!root || !root->object)
		return 0;
	if (!root->count)
	{
		if (root->offset || root->generation || root->height)
			return 0;
		for (size_t i = 0; i < sizeof(root->maximum); i++)
			if (root->maximum[i])
				return 0;
		return 1;
	}
	return root->generation && root->height && root->height <= UDB_STORAGE_CATALOG_HEIGHT_MAX &&
		   root->offset % UDB_STORAGE_PAGE_SIZE == 0 && udb_storage_io_range_valid(root->offset, UDB_STORAGE_PAGE_SIZE);
}

/* Integrity belongs to the enclosing catalog/metadata/journal frame. */
static UdbStorageResult udb_storage_catalog_root_encode(const UdbStorageCatalogRoot *root, unsigned char *bytes,
														size_t capacity)
{
	static const unsigned char magic[8] = {'U', 'D', 'B', 'C', 'A', 'T', '4', 0};
	if (!bytes || !udb_storage_catalog_root_valid(root) ||
		udb_storage_overlaps(bytes, UDB_STORAGE_CATALOG_ROOT_SIZE, root, sizeof(*root)))
		return UDB_STORAGE_ERROR;
	if (capacity < UDB_STORAGE_CATALOG_ROOT_SIZE)
		return UDB_STORAGE_LIMIT;
	memset(bytes, 0, UDB_STORAGE_CATALOG_ROOT_SIZE);
	memcpy(bytes, magic, sizeof(magic));
	udb_storage_put_u32(bytes + 8, UDB_STORAGE_FORMAT);
	udb_storage_put_u32(bytes + 12, root->height);
	udb_storage_put_u64(bytes + 16, root->offset);
	udb_storage_put_u64(bytes + 24, root->generation);
	udb_storage_put_u64(bytes + 32, root->object);
	udb_storage_put_u64(bytes + 40, root->count);
	memcpy(bytes + 48, root->maximum, sizeof(root->maximum));
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_catalog_root_decode(const unsigned char *bytes, size_t length,
														UdbStorageCatalogRoot *root)
{
	static const unsigned char magic[8] = {'U', 'D', 'B', 'C', 'A', 'T', '4', 0};
	UdbStorageCatalogRoot candidate;
	if (!bytes || !root || length != UDB_STORAGE_CATALOG_ROOT_SIZE ||
		udb_storage_overlaps(bytes, length, root, sizeof(*root)))
		return UDB_STORAGE_ERROR;
	if (memcmp(bytes, magic, sizeof(magic)) || udb_storage_get_u32(bytes + 8) != UDB_STORAGE_FORMAT)
		return UDB_STORAGE_CORRUPT;
	for (size_t i = 112; i < length; i++)
		if (bytes[i])
			return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.height = udb_storage_get_u32(bytes + 12);
	candidate.offset = udb_storage_get_u64(bytes + 16);
	candidate.generation = udb_storage_get_u64(bytes + 24);
	candidate.object = udb_storage_get_u64(bytes + 32);
	candidate.count = udb_storage_get_u64(bytes + 40);
	memcpy(candidate.maximum, bytes + 48, sizeof(candidate.maximum));
	if (!udb_storage_catalog_root_valid(&candidate))
		return UDB_STORAGE_CORRUPT;
	*root = candidate;
	return UDB_STORAGE_OK;
}

/* Validated once at cache admission, not again on every warm lookup. */
static UdbStorageResult udb_storage_catalog_page_decode(UdbStoragePage *page)
{
	const unsigned char *payload = page->payload.data;
	uint32_t slots, level;
	size_t stride, maximum;
	uint64_t records = 0, offset;
	if (page->kind != UDB_STORAGE_PAGE_CATALOG || !page->object || page->payload.length < 16 ||
		page->ordinal > (uint64_t)INT64_MAX / UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	slots = udb_storage_get_u32(payload);
	level = udb_storage_get_u32(payload + 4);
	if (level >= UDB_STORAGE_CATALOG_HEIGHT_MAX)
		return UDB_STORAGE_CORRUPT;
	stride = udb_storage_catalog_entry_size(level + 1);
	maximum = level ? UDB_STORAGE_CATALOG_INDEX_MAX : UDB_STORAGE_CATALOG_LEAF_MAX;
	if (!slots || slots > maximum || page->payload.length != 16 + (size_t)slots * stride)
		return UDB_STORAGE_CORRUPT;
	offset = page->ordinal * UDB_STORAGE_PAGE_SIZE;
	for (uint32_t i = 0; i < slots; i++)
	{
		const unsigned char *entry = payload + 16 + (size_t)i * stride;
		if (i && memcmp(entry - stride, entry, UDB_STORAGE_CATALOG_KEY_SIZE) >= 0)
			return UDB_STORAGE_CORRUPT;
		if (level)
		{
			uint64_t child_offset = udb_storage_get_u64(entry + 64);
			uint64_t generation = udb_storage_get_u64(entry + 72);
			uint64_t count = udb_storage_get_u64(entry + 80);
			if (!generation || generation > page->generation || !count || child_offset == offset ||
				child_offset % UDB_STORAGE_PAGE_SIZE || records > UINT64_MAX - count)
				return UDB_STORAGE_CORRUPT;
			records += count;
		}
		else
			records++;
	}
	if (records != udb_storage_get_u64(payload + 8))
		return UDB_STORAGE_CORRUPT;
	page->items = slots;
	page->level = level;
	page->records = records;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_catalog_matches(const UdbStoragePage *page, const UdbStorageCatalogRoot *root)
{
	size_t stride = udb_storage_catalog_entry_size(root->height);
	if (page->kind != UDB_STORAGE_PAGE_CATALOG || page->generation != root->generation ||
		page->object != root->object || page->ordinal != root->offset / UDB_STORAGE_PAGE_SIZE ||
		page->level + 1 != root->height || page->records != root->count || !page->items ||
		memcmp(page->payload.data + 16 + (size_t)(page->items - 1) * stride, root->maximum, 64))
		return UDB_STORAGE_CORRUPT;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_catalog_pin(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *root, UdbStorageCacheEntry **pin)
{
	UdbStorageResult result = udb_storage_cache_acquire(cache, file, root->offset, root->generation, pin);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_catalog_matches(&(*pin)->page, root);
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_cache_release(cache, *pin);
		*pin = NULL;
	}
	return result;
}

static uint32_t udb_storage_catalog_lower(const unsigned char *entries, uint32_t slots, size_t stride,
										  const unsigned char *key)
{
	uint32_t lower = 0, upper = slots;
	while (lower < upper)
	{
		uint32_t middle = lower + (upper - lower) / 2;
		if (memcmp(entries + (size_t)middle * stride, key, 64) < 0)
			lower = middle + 1;
		else
			upper = middle;
	}
	return lower;
}

static void udb_storage_catalog_child_ref(const unsigned char *entry, const UdbStorageCatalogRoot *parent,
										  UdbStorageCatalogRoot *child)
{
	memset(child, 0, sizeof(*child));
	memcpy(child->maximum, entry, 64);
	child->offset = udb_storage_get_u64(entry + 64);
	child->generation = udb_storage_get_u64(entry + 72);
	child->count = udb_storage_get_u64(entry + 80);
	child->object = parent->object;
	child->height = parent->height - 1;
}

static void udb_storage_catalog_child_encode(unsigned char *entry, const UdbStorageCatalogRoot *child)
{
	memcpy(entry, child->maximum, 64);
	udb_storage_put_u64(entry + 64, child->offset);
	udb_storage_put_u64(entry + 72, child->generation);
	udb_storage_put_u64(entry + 80, child->count);
}

static UdbStorageResult udb_storage_catalog_get(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *root,
												const unsigned char key[UDB_STORAGE_CATALOG_KEY_SIZE],
												unsigned char value[UDB_STORAGE_CATALOG_VALUE_SIZE])
{
	UdbStorageCatalogRoot current;
	if (!cache || !file || !file->opened || !key || !value || !udb_storage_catalog_root_valid(root))
		return UDB_STORAGE_ERROR;
	current = *root;
	if (!current.count)
		return UDB_STORAGE_NOT_FOUND;
	for (;;)
	{
		UdbStorageCacheEntry *pin;
		UdbStorageResult result = udb_storage_catalog_pin(cache, file, &current, &pin);
		const unsigned char *entries;
		uint32_t slot;
		size_t stride = udb_storage_catalog_entry_size(current.height);
		if (result != UDB_STORAGE_OK)
			return result;
		entries = pin->page.payload.data + 16;
		slot = udb_storage_catalog_lower(entries, pin->page.items, stride, key);
		if (slot == pin->page.items || (current.height == 1 && memcmp(entries + (size_t)slot * stride, key, 64)))
		{
			result = UDB_STORAGE_NOT_FOUND;
			udb_storage_cache_release(cache, pin);
			return result;
		}
		if (current.height == 1)
		{
			/* The key is consumed before copying; output must not mutate a pinned page. */
			if (udb_storage_overlaps(value, 128, pin, sizeof(*pin)))
				result = UDB_STORAGE_ERROR;
			else
				memcpy(value, entries + (size_t)slot * stride + 64, 128);
			udb_storage_cache_release(cache, pin);
			return result;
		}
		{
			UdbStorageCatalogRoot child;
			udb_storage_catalog_child_ref(entries + (size_t)slot * stride, &current, &child);
			current = child;
		}
		udb_storage_cache_release(cache, pin);
	}
}

typedef struct UdbStorageCatalogEdit
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	uint64_t generation;
	unsigned char key[64];
	unsigned char value[128];
	int deletion;
} UdbStorageCatalogEdit;

typedef struct UdbStorageCatalogFrame
{
	/* One overflow slot suffices for a split; index slots are smaller than leaves. */
	unsigned char entries[UDB_STORAGE_PAGE_PAYLOAD + UDB_STORAGE_CATALOG_LEAF_ENTRY];
} UdbStorageCatalogFrame;

static UdbStorageResult udb_storage_catalog_append(UdbStorageCatalogEdit *edit, uint64_t object, uint32_t height,
												   const unsigned char *entries, uint32_t slots,
												   UdbStorageCatalogRoot *out)
{
	UdbStorageBudget *budget = edit->file->directory->budget;
	unsigned char *payload = NULL, *bytes = NULL;
	UdbStoragePage page;
	UdbStorageCatalogRoot candidate;
	UdbStorageResult result;
	size_t stride = udb_storage_catalog_entry_size(height), length = 16 + (size_t)slots * stride;
	uint64_t size, count = 0;
	if (!height || height > UDB_STORAGE_CATALOG_HEIGHT_MAX || !slots || length > UDB_STORAGE_PAGE_PAYLOAD)
		return UDB_STORAGE_LIMIT;
	for (uint32_t i = 0; i < slots; i++)
	{
		uint64_t increment = height == 1 ? 1 : udb_storage_get_u64(entries + (size_t)i * stride + 80);
		if (!increment || count > UINT64_MAX - increment)
			return UDB_STORAGE_LIMIT;
		count += increment;
	}
	result = udb_storage_file_reserve(edit->file, 1, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	if (!udb_storage_io_range_valid(size, UDB_STORAGE_PAGE_SIZE))
		return UDB_STORAGE_LIMIT;
	payload = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, length);
	bytes = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	if (!payload || !bytes)
	{
		result = UDB_STORAGE_LIMIT;
		goto done;
	}
	udb_storage_put_u32(payload, slots);
	udb_storage_put_u32(payload + 4, height - 1);
	udb_storage_put_u64(payload + 8, count);
	memcpy(payload + 16, entries, (size_t)slots * stride);
	memset(&page, 0, sizeof(page));
	page.kind = UDB_STORAGE_PAGE_CATALOG;
	page.object = object;
	page.generation = edit->generation;
	page.ordinal = size / UDB_STORAGE_PAGE_SIZE;
	page.payload.data = payload;
	page.payload.length = (uint32_t)length;
	result = udb_storage_catalog_page_decode(&page);
	if (result != UDB_STORAGE_OK)
		goto done;
	result = udb_storage_page_encode(&page, bytes, UDB_STORAGE_PAGE_SIZE);
	if (result != UDB_STORAGE_OK)
		goto done;
	result = udb_storage_file_write(edit->file, size, bytes, UDB_STORAGE_PAGE_SIZE);
	if (result != UDB_STORAGE_OK)
		goto done;
	memset(&candidate, 0, sizeof(candidate));
	candidate.offset = size;
	candidate.generation = edit->generation;
	candidate.object = object;
	candidate.count = count;
	candidate.height = height;
	memcpy(candidate.maximum, entries + (size_t)(slots - 1) * stride, 64);
	*out = candidate;
done:
	udb_storage_free(bytes);
	udb_storage_free(payload);
	return result;
}

/* Replacement references contain zero (removed), one or two (split) nodes. */
static UdbStorageResult udb_storage_catalog_edit_node(UdbStorageCatalogEdit *edit, const UdbStorageCatalogRoot *root,
													  UdbStorageCatalogRoot replacement[2], unsigned int *replacements)
{
	UdbStorageCacheEntry *pin = NULL;
	UdbStorageCatalogFrame *frame = NULL;
	UdbStorageResult result;
	uint32_t slots = 0, slot;
	size_t stride = udb_storage_catalog_entry_size(root->height ? root->height : 1);
	uint32_t height = root->height ? root->height : 1;
	size_t maximum = height == 1 ? UDB_STORAGE_CATALOG_LEAF_MAX : UDB_STORAGE_CATALOG_INDEX_MAX;
	frame = udb_storage_alloc(edit->file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*frame));
	if (!frame)
		return UDB_STORAGE_LIMIT;
	if (root->count)
	{
		result = udb_storage_catalog_pin(edit->cache, edit->file, root, &pin);
		if (result != UDB_STORAGE_OK)
			goto done;
		slots = pin->page.items;
		memcpy(frame->entries, pin->page.payload.data + 16, (size_t)slots * stride);
		result = udb_storage_cache_release(edit->cache, pin);
		pin = NULL;
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	slot = udb_storage_catalog_lower(frame->entries, slots, stride, edit->key);
	if (height == 1)
	{
		int exists = slot < slots && !memcmp(frame->entries + (size_t)slot * stride, edit->key, 64);
		if (edit->deletion)
		{
			if (!exists)
			{
				result = UDB_STORAGE_NOT_FOUND;
				goto done;
			}
			memmove(frame->entries + (size_t)slot * stride, frame->entries + (size_t)(slot + 1) * stride,
					(size_t)(slots - slot - 1) * stride);
			slots--;
		}
		else
		{
			if (!exists)
			{
				memmove(frame->entries + (size_t)(slot + 1) * stride, frame->entries + (size_t)slot * stride,
						(size_t)(slots - slot) * stride);
				slots++;
			}
			memcpy(frame->entries + (size_t)slot * stride, edit->key, 64);
			memcpy(frame->entries + (size_t)slot * stride + 64, edit->value, 128);
		}
	}
	else
	{
		UdbStorageCatalogRoot child, children[2];
		unsigned int child_count;
		if (slot == slots)
		{
			if (edit->deletion)
			{
				result = UDB_STORAGE_NOT_FOUND;
				goto done;
			}
			slot--;
		}
		udb_storage_catalog_child_ref(frame->entries + (size_t)slot * stride, root, &child);
		result = udb_storage_catalog_edit_node(edit, &child, children, &child_count);
		if (result != UDB_STORAGE_OK)
			goto done;
		memmove(frame->entries + (size_t)(slot + child_count) * stride, frame->entries + (size_t)(slot + 1) * stride,
				(size_t)(slots - slot - 1) * stride);
		for (unsigned int i = 0; i < child_count; i++)
			udb_storage_catalog_child_encode(frame->entries + (size_t)(slot + i) * stride, &children[i]);
		slots = slots - 1 + child_count;
	}
	if (!slots)
	{
		*replacements = 0;
		result = UDB_STORAGE_OK;
	}
	else if (slots <= maximum)
	{
		result = udb_storage_catalog_append(edit, root->object, height, frame->entries, slots, replacement);
		if (result == UDB_STORAGE_OK)
			*replacements = 1;
	}
	else
	{
		uint32_t left = slots / 2;
		result = udb_storage_catalog_append(edit, root->object, height, frame->entries, left, replacement);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_catalog_append(edit, root->object, height, frame->entries + (size_t)left * stride,
												slots - left, replacement + 1);
		if (result == UDB_STORAGE_OK)
			*replacements = 2;
	}
done:
	if (pin)
		udb_storage_cache_release(edit->cache, pin);
	udb_storage_free(frame);
	return result;
}

static UdbStorageResult udb_storage_catalog_change(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageCatalogRoot *root, uint64_t generation,
												   const unsigned char key[UDB_STORAGE_CATALOG_KEY_SIZE],
												   const unsigned char value[UDB_STORAGE_CATALOG_VALUE_SIZE],
												   UdbStorageCatalogRoot *out)
{
	UdbStorageCatalogEdit edit;
	UdbStorageCatalogRoot replacements[2], candidate;
	unsigned int count;
	UdbStorageResult result;
	if (!cache || !file || !file->opened || !out || !key || !generation || !udb_storage_catalog_root_valid(root) ||
		generation < root->generation)
		return UDB_STORAGE_ERROR;
	memset(&edit, 0, sizeof(edit));
	edit.cache = cache;
	edit.file = file;
	edit.generation = generation;
	edit.deletion = value == NULL;
	memcpy(edit.key, key, sizeof(edit.key));
	if (value)
		memcpy(edit.value, value, sizeof(edit.value));
	result = udb_storage_catalog_edit_node(&edit, root, replacements, &count);
	if (result != UDB_STORAGE_OK)
		return result;
	if (!count)
	{
		memset(&candidate, 0, sizeof(candidate));
		candidate.object = root->object;
	}
	else if (count == 1)
		candidate = replacements[0];
	else
	{
		unsigned char entries[2 * UDB_STORAGE_CATALOG_INDEX_ENTRY];
		if (replacements[0].height == UDB_STORAGE_CATALOG_HEIGHT_MAX)
			return UDB_STORAGE_LIMIT;
		for (unsigned int i = 0; i < 2; i++)
			udb_storage_catalog_child_encode(entries + i * UDB_STORAGE_CATALOG_INDEX_ENTRY, replacements + i);
		result = udb_storage_catalog_append(&edit, root->object, replacements[0].height + 1, entries, 2, &candidate);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	/* Collapse unary roots, without mutating or unlinking any referenced page. */
	while (candidate.height > 1)
	{
		UdbStorageCacheEntry *pin;
		UdbStorageCatalogRoot child;
		result = udb_storage_catalog_pin(cache, file, &candidate, &pin);
		if (result != UDB_STORAGE_OK)
			return result;
		if (pin->page.items != 1)
		{
			udb_storage_cache_release(cache, pin);
			break;
		}
		udb_storage_catalog_child_ref(pin->page.payload.data + 16, &candidate, &child);
		udb_storage_cache_release(cache, pin);
		candidate = child;
	}
	*out = candidate;
	return UDB_STORAGE_OK;
}

typedef struct UdbStorageCatalogValidation
{
	UdbStorageFile *file;
	UdbStorageCatalogValidator validator;
	void *context;
	unsigned char previous[64];
	uint64_t count;
} UdbStorageCatalogValidation;

static UdbStorageResult udb_storage_catalog_validate_node(UdbStorageCatalogValidation *validation,
														  const UdbStorageCatalogRoot *root)
{
	unsigned char *bytes;
	UdbStoragePage page;
	UdbStorageResult result;
	uint64_t start_count = validation->count;
	size_t stride = udb_storage_catalog_entry_size(root->height);
	bytes = udb_storage_alloc(validation->file->directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	if (!bytes)
		return UDB_STORAGE_LIMIT;
	/* Startup/checkpoint validation bypasses data cache admission. */
	result = udb_storage_file_read(validation->file, root->offset, bytes, UDB_STORAGE_PAGE_SIZE);
	if (result != UDB_STORAGE_OK)
		goto done;
	result = udb_storage_page_decode(bytes, UDB_STORAGE_PAGE_SIZE, &page);
	if (result != UDB_STORAGE_OK)
		goto done;
	result = udb_storage_catalog_matches(&page, root);
	if (result != UDB_STORAGE_OK)
		goto done;
	for (uint32_t i = 0; i < page.items; i++)
	{
		const unsigned char *entry = page.payload.data + 16 + (size_t)i * stride;
		if (root->height > 1)
		{
			UdbStorageCatalogRoot child;
			udb_storage_catalog_child_ref(entry, root, &child);
			result = udb_storage_catalog_validate_node(validation, &child);
		}
		else
		{
			if (validation->count && memcmp(validation->previous, entry, 64) >= 0)
			{
				result = UDB_STORAGE_CORRUPT;
				goto done;
			}
			if (validation->count == UINT64_MAX)
			{
				result = UDB_STORAGE_LIMIT;
				goto done;
			}
			if (validation->validator)
				result = validation->validator(entry, entry + 64, validation->context);
			if (result == UDB_STORAGE_OK)
			{
				memcpy(validation->previous, entry, 64);
				validation->count++;
			}
		}
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	if (validation->count - start_count != root->count)
		result = UDB_STORAGE_CORRUPT;
done:
	udb_storage_free(bytes);
	return result;
}

static UdbStorageResult udb_storage_catalog_validate(UdbStorageFile *file, const UdbStorageCatalogRoot *root,
													 UdbStorageCatalogValidator validator, void *context)
{
	UdbStorageCatalogValidation validation;
	if (!file || !file->opened || !udb_storage_catalog_root_valid(root))
		return UDB_STORAGE_ERROR;
	if (!root->count)
		return UDB_STORAGE_OK;
	memset(&validation, 0, sizeof(validation));
	validation.file = file;
	validation.validator = validator;
	validation.context = context;
	return udb_storage_catalog_validate_node(&validation, root);
}

struct UdbStorageCatalogIterator
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageCatalogRoot root;
	UdbStorageCatalogRoot path[UDB_STORAGE_CATALOG_HEIGHT_MAX];
	uint32_t slots[UDB_STORAGE_CATALOG_HEIGHT_MAX];
	unsigned int depth;
	UdbStorageCacheEntry *leaf;
	uint32_t slot;
	unsigned char upper[64];
	uint64_t seen;
	UdbStorageResult failed;
	int has_upper;
	int full_scan;
	int finished;
};

static UdbStorageResult udb_storage_catalog_iterator_descend(UdbStorageCatalogIterator *iterator,
															 const UdbStorageCatalogRoot *start,
															 const unsigned char *lower)
{
	UdbStorageCatalogRoot current = *start;
	for (;;)
	{
		UdbStorageCacheEntry *pin;
		UdbStorageResult result = udb_storage_catalog_pin(iterator->cache, iterator->file, &current, &pin);
		uint32_t slot;
		size_t stride = udb_storage_catalog_entry_size(current.height);
		if (result != UDB_STORAGE_OK)
			return result;
		slot = lower ? udb_storage_catalog_lower(pin->page.payload.data + 16, pin->page.items, stride, lower) : 0;
		if (slot == pin->page.items)
		{
			udb_storage_cache_release(iterator->cache, pin);
			return UDB_STORAGE_NOT_FOUND;
		}
		if (current.height == 1)
		{
			iterator->leaf = pin;
			iterator->slot = slot;
			return UDB_STORAGE_OK;
		}
		if (iterator->depth >= UDB_STORAGE_CATALOG_HEIGHT_MAX - 1)
		{
			udb_storage_cache_release(iterator->cache, pin);
			return UDB_STORAGE_CORRUPT;
		}
		iterator->path[iterator->depth] = current;
		iterator->slots[iterator->depth++] = slot;
		{
			UdbStorageCatalogRoot child;
			udb_storage_catalog_child_ref(pin->page.payload.data + 16 + (size_t)slot * stride, &current, &child);
			current = child;
		}
		udb_storage_cache_release(iterator->cache, pin);
	}
}

static UdbStorageResult udb_storage_catalog_iterator_close(UdbStorageCatalogIterator **pointer)
{
	UdbStorageCatalogIterator *iterator;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	iterator = *pointer;
	if (iterator->leaf)
	{
		result = udb_storage_cache_release(iterator->cache, iterator->leaf);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	udb_storage_free(iterator);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_catalog_iterator_create(UdbStorageCache *cache, UdbStorageFile *file,
															const UdbStorageCatalogRoot *root,
															const unsigned char *lower, const unsigned char *upper,
															UdbStorageCatalogIterator **out)
{
	UdbStorageCatalogIterator *iterator;
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || !file->opened || !udb_storage_catalog_root_valid(root) ||
		(lower && upper && memcmp(lower, upper, 64) >= 0))
		return UDB_STORAGE_ERROR;
	iterator = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*iterator));
	if (!iterator)
		return UDB_STORAGE_LIMIT;
	iterator->cache = cache;
	iterator->file = file;
	iterator->root = *root;
	iterator->has_upper = upper != NULL;
	iterator->full_scan = !lower && !upper;
	if (upper)
		memcpy(iterator->upper, upper, sizeof(iterator->upper));
	if (!root->count)
		iterator->finished = 1;
	else
	{
		result = udb_storage_catalog_iterator_descend(iterator, root, lower);
		if (result == UDB_STORAGE_NOT_FOUND)
			iterator->finished = 1;
		else if (result != UDB_STORAGE_OK)
		{
			udb_storage_catalog_iterator_close(&iterator);
			return result;
		}
	}
	*out = iterator;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_catalog_iterator_advance(UdbStorageCatalogIterator *iterator)
{
	UdbStorageResult result = udb_storage_cache_release(iterator->cache, iterator->leaf);
	if (result != UDB_STORAGE_OK)
		return result;
	iterator->leaf = NULL;
	while (iterator->depth)
	{
		UdbStorageCacheEntry *pin;
		UdbStorageCatalogRoot *parent = &iterator->path[iterator->depth - 1], child;
		uint32_t slot = ++iterator->slots[iterator->depth - 1];
		result = udb_storage_catalog_pin(iterator->cache, iterator->file, parent, &pin);
		if (result != UDB_STORAGE_OK)
			return result;
		if (slot >= pin->page.items)
		{
			udb_storage_cache_release(iterator->cache, pin);
			iterator->depth--;
			continue;
		}
		udb_storage_catalog_child_ref(pin->page.payload.data + 16 + (size_t)slot * UDB_STORAGE_CATALOG_INDEX_ENTRY,
									  parent, &child);
		udb_storage_cache_release(iterator->cache, pin);
		return udb_storage_catalog_iterator_descend(iterator, &child, NULL);
	}
	if (iterator->full_scan && iterator->seen != iterator->root.count)
		return UDB_STORAGE_CORRUPT;
	iterator->finished = 1;
	return UDB_STORAGE_NOT_FOUND;
}

static UdbStorageResult udb_storage_catalog_iterator_next(UdbStorageCatalogIterator *iterator,
														  unsigned char key[UDB_STORAGE_CATALOG_KEY_SIZE],
														  unsigned char value[UDB_STORAGE_CATALOG_VALUE_SIZE])
{
	const unsigned char *entry;
	UdbStorageResult result;
	if (!iterator || !key || !value || udb_storage_overlaps(key, 64, value, 128) ||
		udb_storage_overlaps(key, 64, iterator, sizeof(*iterator)) ||
		udb_storage_overlaps(value, 128, iterator, sizeof(*iterator)))
		return UDB_STORAGE_ERROR;
	if (iterator->failed)
		return iterator->failed;
	if (iterator->finished)
		return UDB_STORAGE_NOT_FOUND;
	if (!iterator->leaf)
		return iterator->failed = UDB_STORAGE_CORRUPT;
	if (iterator->slot == iterator->leaf->page.items)
	{
		result = udb_storage_catalog_iterator_advance(iterator);
		if (result == UDB_STORAGE_NOT_FOUND)
			return result;
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
	}
	entry = iterator->leaf->page.payload.data + 16 + (size_t)iterator->slot * UDB_STORAGE_CATALOG_LEAF_ENTRY;
	if (iterator->has_upper && memcmp(entry, iterator->upper, 64) >= 0)
	{
		result = udb_storage_cache_release(iterator->cache, iterator->leaf);
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
		iterator->leaf = NULL;
		iterator->finished = 1;
		return UDB_STORAGE_NOT_FOUND;
	}
	if (iterator->seen == iterator->root.count)
		return iterator->failed = UDB_STORAGE_CORRUPT;
	if (udb_storage_overlaps(key, 64, iterator->leaf, sizeof(*iterator->leaf)) ||
		udb_storage_overlaps(value, 128, iterator->leaf, sizeof(*iterator->leaf)))
		return UDB_STORAGE_ERROR;
	memcpy(key, entry, 64);
	memcpy(value, entry + 64, 128);
	iterator->slot++;
	iterator->seen++;
	return UDB_STORAGE_OK;
}
