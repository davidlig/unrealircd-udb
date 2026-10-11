/* Foreground typed immutable queries. A read session pins one six-block state,
 * never a tree or the whole block. Scalar profiles contain a fixed field set;
 * variable access lists/mask inventories remain streaming prefix iterators. */

static UdbStorageResult udb_storage_read_open(UdbStorageEngine *engine, UdbStorageRead **out)
{
	UdbStorageRead *read;
	UdbStorageResult result;
	if (!out || !engine)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	read = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*read));
	if (!read)
		return UDB_STORAGE_LIMIT;
	read->engine = engine;
	result = udb_storage_engine_pin(engine, &read->generation);
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_free(read);
		return result;
	}
	*out = read;
	return UDB_STORAGE_OK;
}

/* Ordered durable effects inspect an exact transaction, even if the writer
 * has already published later groups. The job itself owns both disk pins. */
static UdbStorageResult udb_storage_read_job(const UdbStorageJob *job, int after, UdbStorageRead **out)
{
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!job || !job->engine || !job->detached || job->result != UDB_STORAGE_OK || !job->committed_generation)
		return UDB_STORAGE_ERROR;
	UdbStorageGeneration *generation = after ? job->after : job->before;
	if (!generation)
		return UDB_STORAGE_ERROR;
	UdbStorageEngine *engine = job->engine;
	UdbStorageRead *read = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*read));
	if (!read)
		return UDB_STORAGE_LIMIT;
	udb_storage_mutex_lock(&engine->mutex);
	UdbStorageGeneration *found = engine->generations;
	while (found && found != generation)
		found = found->next;
	if (!found || generation->references == SIZE_MAX)
	{
		udb_storage_mutex_unlock(&engine->mutex);
		udb_storage_free(read);
		return UDB_STORAGE_ERROR;
	}
	generation->references++;
	read->engine = engine;
	read->generation = generation;
	udb_storage_mutex_unlock(&engine->mutex);
	*out = read;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_read_close(UdbStorageRead **pointer)
{
	UdbStorageRead *read;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	read = *pointer;
	result = udb_storage_engine_release(read->engine, &read->generation);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_free(read);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_read_root(UdbStorageRead *read, unsigned char block,
											  const UdbStorageBlockRoot **root, UdbStorageFile **file)
{
	int index;
	if (!read || !read->generation || !root || !file)
		return UDB_STORAGE_ERROR;
	if (read->failed)
		return read->failed;
	index = udb_storage_stage_block_index(block);
	if (index < 0)
		return read->failed = UDB_STORAGE_ERROR;
	*root = read->generation->state.blocks + index;
	*file = read->engine->files + index;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_read_field(UdbStorageRead *read, const UdbStorageRow *key, UdbStorageField *field)
{
	const UdbStorageBlockRoot *root = NULL;
	UdbStorageFile *file = NULL;
	UdbStorageResult result;
	UdbStorageRow row;
	unsigned char *bytes;
	if (!read || !field || !udb_storage_row_valid(key))
		return UDB_STORAGE_ERROR;
	memset(field, 0, sizeof(*field));
	result = udb_storage_read_root(read, key->block, &root, &file);
	if (result != UDB_STORAGE_OK)
		return result;
	bytes = udb_storage_alloc(&read->engine->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_ROW_MAX);
	if (!bytes)
		return read->failed = UDB_STORAGE_LIMIT;
	result = udb_storage_block_lookup(read->engine->cache, file, root, key, bytes, UDB_STORAGE_ROW_MAX, &row);
	if (result == UDB_STORAGE_OK)
	{
		field->present = 1;
		field->kind = row.kind;
		field->number = row.number;
		if (row.kind == UDB_STORAGE_VALUE_STRING)
		{
			field->length = row.value.length;
			memcpy(field->string, row.value.data, row.value.length);
			field->string[row.value.length] = 0;
		}
	}
	udb_storage_free(bytes);
	if (result < UDB_STORAGE_NOT_FOUND)
		read->failed = result;
	return result;
}

static UdbStorageResult udb_storage_read_exists(UdbStorageRead *read, const UdbStorageRow *prefix)
{
	const UdbStorageBlockRoot *root = NULL;
	UdbStorageFile *file = NULL;
	UdbStorageResult result;
	if (!read || !udb_storage_row_valid(prefix))
		return UDB_STORAGE_ERROR;
	result = udb_storage_read_root(read, prefix->block, &root, &file);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_block_exists_prefix(read->engine->cache, file, root, prefix);
	if (result < UDB_STORAGE_NOT_FOUND)
		read->failed = result;
	return result;
}

static UdbStorageResult udb_storage_read_iterator(UdbStorageRead *read, unsigned char block,
												  const UdbStorageRow *prefix, UdbStorageBlockIterator **iterator)
{
	const UdbStorageBlockRoot *root = NULL;
	UdbStorageFile *file = NULL;
	UdbStorageResult result;
	if (!read || !iterator)
		return UDB_STORAGE_ERROR;
	*iterator = NULL;
	result = udb_storage_read_root(read, block, &root, &file);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_block_iterator_create(read->engine->cache, file, root, prefix, iterator);
	if (result < UDB_STORAGE_NOT_FOUND)
		read->failed = result;
	return result;
}

static void udb_storage_profile_free(UdbStorageProfile **pointer)
{
	if (!pointer || !*pointer)
		return;
	udb_storage_free(*pointer);
	*pointer = NULL;
}

static UdbStorageResult udb_storage_profile_load(UdbStorageRead *read, const UdbStorageRow *entity,
												 const char *const *names, unsigned int count, UdbStorageProfile **out)
{
	UdbStorageProfile *profile;
	UdbStorageResult result;
	UdbStorageRow key;
	size_t length;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!read || !udb_storage_row_valid(entity) || entity->depth >= UDB_STORAGE_COMPONENTS_MAX || !names || !count ||
		count > UDB_STORAGE_PROFILE_FIELDS)
		return UDB_STORAGE_ERROR;
	result = udb_storage_read_exists(read, entity);
	if (result != UDB_STORAGE_OK)
		return result;
	profile = udb_storage_alloc(&read->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*profile));
	if (!profile)
		return read->failed = UDB_STORAGE_LIMIT;
	result = udb_storage_run_copy_key(entity, profile->components, &length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_row_decode(profile->components, length, &profile->key);
	if (result != UDB_STORAGE_OK)
		goto fail;
	if (profile->key.components[0].length >= sizeof(profile->entity))
	{
		result = UDB_STORAGE_LIMIT;
		goto fail;
	}
	memcpy(profile->entity, profile->key.components[0].data, profile->key.components[0].length);
	profile->entity[profile->key.components[0].length] = 0;
	profile->count = count;
	for (unsigned int i = 0; i < count; i++)
	{
		if (!names[i] || !*names[i])
		{
			result = UDB_STORAGE_ERROR;
			goto fail;
		}
		key = profile->key;
		size_t size = strlen(names[i]);
		if (size > UDB_STORAGE_COMPONENT_MAX)
		{
			result = UDB_STORAGE_LIMIT;
			goto fail;
		}
		key.components[key.depth].data = (const unsigned char *)names[i];
		key.components[key.depth++].length = (uint32_t)size;
		result = udb_storage_read_field(read, &key, profile->fields + i);
		if (result < UDB_STORAGE_NOT_FOUND)
			goto fail;
	}
	*out = profile;
	return UDB_STORAGE_OK;
fail:
	udb_storage_profile_free(&profile);
	if (result < UDB_STORAGE_NOT_FOUND)
		read->failed = result;
	return result;
}

/* A foreground-only empty comparison view for startup effect ownership. It
 * advertises no disk extents, but participates in ordinary pin lifetime. */
static UdbStorageResult udb_storage_read_empty(UdbStorageEngine *engine, UdbStorageRead **out)
{
	if (!out || !engine || !engine->mutex.initialized)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	UdbStorageGeneration *generation = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*generation));
	UdbStorageRead *read = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*read));
	if (!generation || !read)
	{
		udb_storage_free(generation);
		udb_storage_free(read);
		return UDB_STORAGE_LIMIT;
	}
	UdbStorageResult result = UDB_STORAGE_OK;
	for (unsigned int i = 0; i < 6 && result == UDB_STORAGE_OK; i++)
	{
		UdbStorageBlockRoot *root = generation->state.blocks + i;
		root->block = udb_storage_blocks[i];
		root->generation = 1;
		root->catalog.object = i + 1;
		result = udb_storage_logical_block(root->block, udb_storage_journal_empty_partition, NULL, &root->summary);
	}
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_free(generation);
		udb_storage_free(read);
		return result;
	}
	generation->state.generation = 1;
	generation->state.next_object = 8;
	generation->state.stages.object = 7;
	generation->references = 1;
	udb_storage_mutex_lock(&engine->mutex);
	generation->next = engine->generations;
	engine->generations = generation;
	udb_storage_mutex_unlock(&engine->mutex);
	read->engine = engine;
	read->generation = generation;
	*out = read;
	return UDB_STORAGE_OK;
}
