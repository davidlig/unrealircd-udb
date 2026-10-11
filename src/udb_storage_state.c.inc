/* Canonical root-set descriptors shared by WAL and pre-existing metadata slots.
 * All six block roots are one state; an individual block file is never authority.
 * Process pointers, cache sizes and runtime authority tokens are not persisted. */

static const unsigned char udb_storage_blocks[6] = {'N', 'C', 'I', 'S', 'L', 'K'};

static int udb_storage_zero(const unsigned char *bytes, size_t length)
{
	for (size_t i = 0; i < length; i++)
		if (bytes[i])
			return 0;
	return 1;
}

static UdbStorageResult udb_storage_block_root_encode(const UdbStorageBlockRoot *root,
													  unsigned char bytes[UDB_STORAGE_BLOCK_ROOT_SIZE])
{
	UdbStorageResult result;
	if (!bytes || !udb_storage_block_root_valid(root) ||
		udb_storage_overlaps(root, sizeof(*root), bytes, UDB_STORAGE_BLOCK_ROOT_SIZE))
		return UDB_STORAGE_ERROR;
	memset(bytes, 0, UDB_STORAGE_BLOCK_ROOT_SIZE);
	memcpy(bytes, "UDBBLK4", 7);
	udb_storage_put_u32(bytes + 8, UDB_STORAGE_FORMAT);
	bytes[12] = root->block;
	bytes[13] = (unsigned char)root->prefix_count;
	udb_storage_put_u64(bytes + 16, root->generation);
	udb_storage_put_u64(bytes + 24, root->watermark);
	result = udb_storage_catalog_root_encode(&root->catalog, bytes + 32, UDB_STORAGE_CATALOG_ROOT_SIZE);
	if (result != UDB_STORAGE_OK)
		return result;
	memcpy(bytes + 160, root->summary.digest, 32);
	udb_storage_put_u64(bytes + 192, root->summary.count);
	udb_storage_put_u64(bytes + 200, root->summary.weight);
	for (unsigned int i = 0; i < root->prefix_count; i++)
		udb_storage_put_u64(bytes + 208 + i * 8, root->prefixes[i]);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_block_root_decode(const unsigned char bytes[UDB_STORAGE_BLOCK_ROOT_SIZE],
													  UdbStorageBlockRoot *root)
{
	UdbStorageBlockRoot candidate;
	UdbStorageResult result;
	if (!bytes || !root || udb_storage_overlaps(bytes, UDB_STORAGE_BLOCK_ROOT_SIZE, root, sizeof(*root)))
		return UDB_STORAGE_ERROR;
	if (memcmp(bytes, "UDBBLK4\0", 8) || udb_storage_get_u32(bytes + 8) != UDB_STORAGE_FORMAT ||
		!udb_storage_zero(bytes + 14, 2) || !udb_storage_zero(bytes + 280, UDB_STORAGE_BLOCK_ROOT_SIZE - 280))
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.block = bytes[12];
	candidate.prefix_count = bytes[13];
	candidate.generation = udb_storage_get_u64(bytes + 16);
	candidate.watermark = udb_storage_get_u64(bytes + 24);
	result = udb_storage_catalog_root_decode(bytes + 32, UDB_STORAGE_CATALOG_ROOT_SIZE, &candidate.catalog);
	if (result != UDB_STORAGE_OK)
		return result;
	memcpy(candidate.summary.digest, bytes + 160, 32);
	candidate.summary.count = udb_storage_get_u64(bytes + 192);
	candidate.summary.weight = udb_storage_get_u64(bytes + 200);
	for (unsigned int i = 0; i < UDB_STORAGE_MAX_RUNS + 1; i++)
		candidate.prefixes[i] = udb_storage_get_u64(bytes + 208 + i * 8);
	if (!udb_storage_block_root_valid(&candidate))
		return UDB_STORAGE_CORRUPT;
	*root = candidate;
	return UDB_STORAGE_OK;
}

static int udb_storage_state_valid(const UdbStorageState *state)
{
	if (!state || !state->generation || state->next_object < 8 || !udb_storage_catalog_root_valid(&state->stages) ||
		state->stages.object != 7 || state->stages.generation > state->generation ||
		udb_storage_zero(state->store_identity, sizeof(state->store_identity)))
		return 0;
	if (state->source_sid[3])
		return 0;
	if (state->source_sid[0])
	{
		/* HEL 4 epochs are exactly 16 lowercase hexadecimal digits. Every
		 * uint64 value, including zero, has an exact canonical wire encoding. */
		if (!udb_storage_sid_valid(state->source_sid))
			return 0;
	}
	else if (state->source_sid[1] || state->source_sid[2] || state->source_epoch || state->source_sequence)
		return 0;
	for (unsigned int i = 0; i < 6; i++)
	{
		const UdbStorageBlockRoot *root = state->blocks + i;
		if (!udb_storage_block_root_valid(root) || root->block != udb_storage_blocks[i] ||
			root->catalog.object != i + 1 || root->generation > state->generation || root->watermark > state->sequence)
			return 0;
	}
	return 1;
}

static int udb_storage_sid_valid(const char sid[4])
{
	if (!sid || sid[3] || sid[0] < '0' || sid[0] > '9')
		return 0;
	for (unsigned int i = 1; i < 3; i++)
		if (!((sid[i] >= '0' && sid[i] <= '9') || (sid[i] >= 'A' && sid[i] <= 'Z')))
			return 0;
	return 1;
}

static UdbStorageResult udb_storage_state_encode(const UdbStorageState *state,
												 unsigned char bytes[UDB_STORAGE_STATE_SIZE])
{
	if (!bytes || !udb_storage_state_valid(state) ||
		udb_storage_overlaps(state, sizeof(*state), bytes, UDB_STORAGE_STATE_SIZE))
		return UDB_STORAGE_ERROR;
	memset(bytes, 0, UDB_STORAGE_STATE_SIZE);
	memcpy(bytes, "UDBSET4", 7);
	udb_storage_put_u32(bytes + 8, UDB_STORAGE_FORMAT);
	udb_storage_put_u32(bytes + 12, 6);
	udb_storage_put_u64(bytes + 16, state->generation);
	udb_storage_put_u64(bytes + 24, state->sequence);
	udb_storage_put_u64(bytes + 32, state->next_object);
	udb_storage_put_u64(bytes + 40, state->source_epoch);
	udb_storage_put_u64(bytes + 48, state->source_sequence);
	memcpy(bytes + 56, state->store_identity, 16);
	memcpy(bytes + 72, state->source_sid, 4);
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageResult result =
			udb_storage_block_root_encode(state->blocks + i, bytes + 128 + i * UDB_STORAGE_BLOCK_ROOT_SIZE);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return udb_storage_catalog_root_encode(&state->stages, bytes + 128 + 6 * UDB_STORAGE_BLOCK_ROOT_SIZE,
										   UDB_STORAGE_CATALOG_ROOT_SIZE);
}

static UdbStorageResult udb_storage_state_decode(const unsigned char bytes[UDB_STORAGE_STATE_SIZE],
												 UdbStorageState *state)
{
	UdbStorageState candidate;
	if (!bytes || !state || udb_storage_overlaps(bytes, UDB_STORAGE_STATE_SIZE, state, sizeof(*state)))
		return UDB_STORAGE_ERROR;
	if (memcmp(bytes, "UDBSET4\0", 8) || udb_storage_get_u32(bytes + 8) != UDB_STORAGE_FORMAT ||
		udb_storage_get_u32(bytes + 12) != 6 || !udb_storage_zero(bytes + 76, 52))
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.generation = udb_storage_get_u64(bytes + 16);
	candidate.sequence = udb_storage_get_u64(bytes + 24);
	candidate.next_object = udb_storage_get_u64(bytes + 32);
	candidate.source_epoch = udb_storage_get_u64(bytes + 40);
	candidate.source_sequence = udb_storage_get_u64(bytes + 48);
	memcpy(candidate.store_identity, bytes + 56, 16);
	memcpy(candidate.source_sid, bytes + 72, 4);
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageResult result =
			udb_storage_block_root_decode(bytes + 128 + i * UDB_STORAGE_BLOCK_ROOT_SIZE, candidate.blocks + i);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	{
		UdbStorageResult result = udb_storage_catalog_root_decode(bytes + 128 + 6 * UDB_STORAGE_BLOCK_ROOT_SIZE,
																  UDB_STORAGE_CATALOG_ROOT_SIZE, &candidate.stages);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (!udb_storage_state_valid(&candidate))
		return UDB_STORAGE_CORRUPT;
	*state = candidate;
	return UDB_STORAGE_OK;
}
