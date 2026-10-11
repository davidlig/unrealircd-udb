/*
 * UDB 4 native storage: canonical logical hashes, independent of physical pages,
 * insertion order and cache size. A caller supplies the sorted, visible rows of
 * a leaf after applying its tombstones; hashing physical runs is not equivalent.
 * These helpers validate placement/shape. The catalog owner performs splitting,
 * collapse and durable publication and verifies every referenced child summary.
 */

struct UdbStorageLeafHasher
{
	EVP_MD_CTX *context;
	UdbStorageLogicalPosition position;
	UdbStorageLogicalSummary summary;
	unsigned char last_key[UDB_STORAGE_KEY_MAX];
	size_t last_length;
	UdbStorageResult failed;
	int finished;
};

static int udb_storage_logical_position_valid(const UdbStorageLogicalPosition *position)
{
	unsigned int whole, remaining;
	if (!position || !udb_storage_block_valid(position->block) || position->partition >= UDB_STORAGE_BASE_PARTITIONS ||
		position->depth > 256)
		return 0;
	whole = position->depth / 8;
	remaining = position->depth % 8;
	if (remaining && (position->prefix[whole] & (0xffU >> remaining)))
		return 0;
	for (unsigned int i = whole + (remaining != 0); i < 32; i++)
		if (position->prefix[i])
			return 0;
	return 1;
}

static UdbStorageResult udb_storage_logical_child(const UdbStorageLogicalPosition *parent, unsigned int right,
												  UdbStorageLogicalPosition *child)
{
	UdbStorageLogicalPosition candidate;
	if (!child || right > 1 || !udb_storage_logical_position_valid(parent))
		return UDB_STORAGE_ERROR;
	if (parent->depth == 256)
		return UDB_STORAGE_LIMIT;
	candidate = *parent;
	if (right)
		candidate.prefix[parent->depth / 8] |= (unsigned char)(0x80U >> (parent->depth % 8));
	candidate.depth++;
	*child = candidate;
	return UDB_STORAGE_OK;
}

static int udb_storage_logical_hash_begin(EVP_MD_CTX *context, const char *domain, size_t domain_length,
										  const UdbStorageLogicalPosition *position)
{
	unsigned char header[37];
	header[0] = position->block;
	udb_storage_put_u16(header + 1, position->partition);
	udb_storage_put_u16(header + 3, position->depth);
	memcpy(header + 5, position->prefix, 32);
	return EVP_DigestInit_ex(context, EVP_sha256(), NULL) && EVP_DigestUpdate(context, domain, domain_length) &&
		   EVP_DigestUpdate(context, header, sizeof(header));
}

static int udb_storage_logical_hash_finish(EVP_MD_CTX *context, UdbStorageLogicalSummary *summary)
{
	unsigned char totals[16];
	udb_storage_put_u64(totals, summary->count);
	udb_storage_put_u64(totals + 8, summary->weight);
	return EVP_DigestUpdate(context, totals, sizeof(totals)) && udb_storage_digest_final(context, summary->digest);
}

static int udb_storage_logical_summary_valid(const UdbStorageLogicalSummary *summary)
{
	/* The smallest encoded row is 21 bytes, plus its 128-byte logical charge. */
	return summary &&
		   ((!summary->count && !summary->weight) || (summary->count && summary->count <= summary->weight / 149));
}

static int udb_storage_logical_contains(const UdbStorageLogicalPosition *position, const unsigned char hash[32],
										unsigned int partition)
{
	unsigned int whole = position->depth / 8, remaining = position->depth % 8;
	if (partition != position->partition || memcmp(hash, position->prefix, whole))
		return 0;
	return !remaining || (hash[whole] & (0xffU << (8 - remaining))) == position->prefix[whole];
}

static UdbStorageResult udb_storage_leaf_hasher_create(UdbStorageBudget *budget,
													   const UdbStorageLogicalPosition *position,
													   UdbStorageLeafHasher **out)
{
	static const char domain[] = "UDB4/leaf";
	UdbStorageLeafHasher *hasher;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!budget || !budget->initialized || !udb_storage_logical_position_valid(position))
		return UDB_STORAGE_ERROR;
	hasher = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, sizeof(*hasher));
	if (!hasher)
		return UDB_STORAGE_LIMIT;
	hasher->context = EVP_MD_CTX_new();
	hasher->position = *position;
	if (!hasher->context || !udb_storage_logical_hash_begin(hasher->context, domain, sizeof(domain), position))
	{
		EVP_MD_CTX_free(hasher->context);
		udb_storage_free(hasher);
		return UDB_STORAGE_ERROR;
	}
	*out = hasher;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_leaf_hasher_add(UdbStorageLeafHasher *hasher, const unsigned char *encoded_row,
													size_t length)
{
	UdbStorageRow row, previous, key;
	unsigned char hash[32], size[4];
	unsigned int partition;
	UdbStorageResult result;
	if (!hasher || hasher->finished)
		return UDB_STORAGE_ERROR;
	if (hasher->failed)
		return hasher->failed;
	if (!encoded_row || udb_storage_overlaps(encoded_row, length, hasher, sizeof(*hasher)))
		return hasher->failed = UDB_STORAGE_ERROR;
	result = udb_storage_row_decode(encoded_row, length, &row);
	if (result != UDB_STORAGE_OK)
		return hasher->failed = result;
	result = udb_storage_key_hash(&row, hash, &partition);
	if (result != UDB_STORAGE_OK)
		return hasher->failed = result;
	if (row.block != hasher->position.block || !udb_storage_logical_contains(&hasher->position, hash, partition))
		return hasher->failed = UDB_STORAGE_CORRUPT;
	if (hasher->last_length &&
		(udb_storage_row_decode(hasher->last_key, hasher->last_length, &previous) != UDB_STORAGE_OK ||
		 udb_storage_key_compare(&previous, &row) >= 0))
		return hasher->failed = UDB_STORAGE_CORRUPT;
	/* Overflow/collision is never made publishable by ignoring a rejected row. */
	if (length + UINT64_C(128) > UDB_STORAGE_LEAF_WEIGHT_MAX - hasher->summary.weight)
		return hasher->failed = UDB_STORAGE_LIMIT;
	key = row;
	key.kind = UDB_STORAGE_VALUE_NONE;
	key.value.data = NULL;
	key.value.length = 0;
	key.number = 0;
	result = udb_storage_row_encode(&key, hasher->last_key, sizeof(hasher->last_key), &hasher->last_length);
	if (result != UDB_STORAGE_OK)
		return hasher->failed = result;
	udb_storage_put_u32(size, (uint32_t)length);
	if (!EVP_DigestUpdate(hasher->context, size, sizeof(size)) ||
		!EVP_DigestUpdate(hasher->context, encoded_row, length))
		return hasher->failed = UDB_STORAGE_ERROR;
	hasher->summary.count++;
	hasher->summary.weight += length + UINT64_C(128);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_leaf_hasher_finish(UdbStorageLeafHasher *hasher, UdbStorageLogicalSummary *summary)
{
	if (!hasher || !summary || hasher->finished ||
		udb_storage_overlaps(summary, sizeof(*summary), hasher, sizeof(*hasher)))
		return UDB_STORAGE_ERROR;
	if (hasher->failed)
		return hasher->failed;
	if (!udb_storage_logical_hash_finish(hasher->context, &hasher->summary))
		return hasher->failed = UDB_STORAGE_ERROR;
	hasher->finished = 1;
	*summary = hasher->summary;
	return UDB_STORAGE_OK;
}

static void udb_storage_leaf_hasher_free(UdbStorageLeafHasher **hasher)
{
	if (hasher && *hasher)
	{
		EVP_MD_CTX_free((*hasher)->context);
		udb_storage_free(*hasher);
		*hasher = NULL;
	}
}

/* Caller-owned scratch permits block hashing to reuse the empty-leaf context. */
static int udb_storage_logical_empty_digest(EVP_MD_CTX *context, const UdbStorageLogicalPosition *position,
											UdbStorageLogicalSummary *summary)
{
	static const char domain[] = "UDB4/leaf";
	memset(summary, 0, sizeof(*summary));
	return udb_storage_logical_hash_begin(context, domain, sizeof(domain), position) &&
		   udb_storage_logical_hash_finish(context, summary);
}

static UdbStorageResult udb_storage_logical_empty(const UdbStorageLogicalPosition *position,
												  UdbStorageLogicalSummary *summary)
{
	EVP_MD_CTX *context;
	UdbStorageLogicalSummary candidate;
	int success;
	if (!summary || !udb_storage_logical_position_valid(position))
		return UDB_STORAGE_ERROR;
	context = EVP_MD_CTX_new();
	if (!context)
		return UDB_STORAGE_ERROR;
	success = udb_storage_logical_empty_digest(context, position, &candidate);
	EVP_MD_CTX_free(context);
	if (!success)
		return UDB_STORAGE_ERROR;
	*summary = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_logical_branch(const UdbStorageLogicalPosition *position,
												   const UdbStorageLogicalSummary *left,
												   const UdbStorageLogicalSummary *right,
												   UdbStorageLogicalSummary *summary)
{
	static const char domain[] = "UDB4/branch";
	EVP_MD_CTX *context;
	UdbStorageLogicalSummary candidate, empty;
	UdbStorageResult result = UDB_STORAGE_ERROR;
	UdbStorageLogicalPosition child;
	const UdbStorageLogicalSummary *children[2] = {left, right};
	if (!summary || !udb_storage_logical_position_valid(position) || position->depth == 256 ||
		!udb_storage_logical_summary_valid(left) || !udb_storage_logical_summary_valid(right))
		return UDB_STORAGE_ERROR;
	if (left->count > UINT64_MAX - right->count || left->weight > UINT64_MAX - right->weight)
		return UDB_STORAGE_LIMIT;
	candidate.count = left->count + right->count;
	candidate.weight = left->weight + right->weight;
	/* Shape is content-determined: a small subtree must be collapsed to a leaf. */
	if (candidate.weight <= UDB_STORAGE_LEAF_WEIGHT_MAX)
		return UDB_STORAGE_CORRUPT;
	for (unsigned int i = 0; i < 2; i++)
	{
		if (children[i]->count)
			continue;
		if (udb_storage_logical_child(position, i, &child) != UDB_STORAGE_OK ||
			udb_storage_logical_empty(&child, &empty) != UDB_STORAGE_OK)
			return UDB_STORAGE_ERROR;
		if (memcmp(empty.digest, children[i]->digest, sizeof(empty.digest)))
			return UDB_STORAGE_CORRUPT;
	}
	context = EVP_MD_CTX_new();
	if (!context)
		return UDB_STORAGE_ERROR;
	if (!udb_storage_logical_hash_begin(context, domain, sizeof(domain), position))
		goto cleanup;
	for (unsigned int i = 0; i < 2; i++)
	{
		unsigned char totals[16];
		udb_storage_put_u64(totals, children[i]->count);
		udb_storage_put_u64(totals + 8, children[i]->weight);
		if (!EVP_DigestUpdate(context, children[i]->digest, 32) || !EVP_DigestUpdate(context, totals, sizeof(totals)))
			goto cleanup;
	}
	if (!udb_storage_logical_hash_finish(context, &candidate))
		goto cleanup;
	*summary = candidate;
	result = UDB_STORAGE_OK;
cleanup:
	EVP_MD_CTX_free(context);
	return result;
}

static UdbStorageResult udb_storage_logical_block(unsigned char block, UdbStoragePartitionReader reader, void *context,
												  UdbStorageLogicalSummary *summary)
{
	static const char domain[] = "UDB4/block";
	EVP_MD_CTX *hash, *empty_context;
	UdbStorageResult result = UDB_STORAGE_ERROR;
	UdbStorageLogicalSummary candidate, partition;
	UdbStorageLogicalPosition position;
	if (!summary || !reader || !udb_storage_block_valid(block))
		return UDB_STORAGE_ERROR;
	memset(&candidate, 0, sizeof(candidate));
	memset(&position, 0, sizeof(position));
	position.block = block;
	hash = EVP_MD_CTX_new();
	empty_context = EVP_MD_CTX_new();
	if (!hash || !empty_context || !EVP_DigestInit_ex(hash, EVP_sha256(), NULL) ||
		!EVP_DigestUpdate(hash, domain, sizeof(domain)) || !EVP_DigestUpdate(hash, &block, 1))
		goto cleanup;
	for (unsigned int i = 0; i < UDB_STORAGE_BASE_PARTITIONS; i++)
	{
		unsigned char header[18];
		memset(&partition, 0, sizeof(partition));
		position.partition = (uint16_t)i;
		result = reader(i, &partition, context);
		if (result == UDB_STORAGE_NOT_FOUND)
			result = udb_storage_logical_empty_digest(empty_context, &position, &partition) ? UDB_STORAGE_OK
																							: UDB_STORAGE_ERROR;
		if (result != UDB_STORAGE_OK)
			goto cleanup;
		if (!udb_storage_logical_summary_valid(&partition))
		{
			result = UDB_STORAGE_CORRUPT;
			goto cleanup;
		}
		if (!partition.count)
		{
			UdbStorageLogicalSummary empty;
			result =
				udb_storage_logical_empty_digest(empty_context, &position, &empty) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
			if (result != UDB_STORAGE_OK)
				goto cleanup;
			if (memcmp(empty.digest, partition.digest, sizeof(empty.digest)))
			{
				result = UDB_STORAGE_CORRUPT;
				goto cleanup;
			}
		}
		if (candidate.count > UINT64_MAX - partition.count || candidate.weight > UINT64_MAX - partition.weight)
		{
			result = UDB_STORAGE_LIMIT;
			goto cleanup;
		}
		candidate.count += partition.count;
		candidate.weight += partition.weight;
		udb_storage_put_u16(header, (uint16_t)i);
		udb_storage_put_u64(header + 2, partition.count);
		udb_storage_put_u64(header + 10, partition.weight);
		if (!EVP_DigestUpdate(hash, header, sizeof(header)) || !EVP_DigestUpdate(hash, partition.digest, 32))
		{
			result = UDB_STORAGE_ERROR;
			goto cleanup;
		}
	}
	if (!udb_storage_logical_hash_finish(hash, &candidate))
	{
		result = UDB_STORAGE_ERROR;
		goto cleanup;
	}
	*summary = candidate;
	result = UDB_STORAGE_OK;
cleanup:
	EVP_MD_CTX_free(empty_context);
	EVP_MD_CTX_free(hash);
	return result;
}
