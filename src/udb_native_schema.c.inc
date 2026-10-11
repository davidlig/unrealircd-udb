/* Pure native-writer schema admission. Mutable daemon handler lists, config and
 * native match helper static buffers are never read from the writer thread. */

static void udb_native_schema_capture(UdbNativeSchema *schema, UdbStorageBudget *budget)
{
	memset(schema, 0, sizeof(*schema));
	schema->budget = budget;
	schema->spamfilter_utf8 =
		loop.config_status < CONFIG_STATUS_LOAD ? tempiConf.spamfilter_utf8 : iConf.spamfilter_utf8;
	for (unsigned int c = 0; c < 256; c++)
	{
		Cmode *mode = find_channel_mode_handler((char)c);
		if (mode)
		{
			schema->channels[c].known = 1;
			schema->channels[c].member = mode->type == CMODE_MEMBER;
			schema->channels[c].parameters = (unsigned short)mode->paracount;
			schema->channels[c].unset_parameter = mode->unset_with_param != 0;
		}
		schema->snomasks[c] = is_valid_snomask((char)c) != 0;
	}
	for (Umode *mode = usermodes; mode; mode = mode->next)
		schema->users[(unsigned char)mode->letter] = 1;
	schema->users[(unsigned char)'o'] = 0;
}

static int udb_native_modes_valid(const char *value, const UdbNativeSchema *schema, int channel, int snomask)
{
	unsigned int parameters = 0, actual = 0, letters = 0;
	int adding = 1;
	const unsigned char *p = (const unsigned char *)value;
	if (!value || !*value || strlen(value) >= (channel ? 512 : 65))
		return 0;
	while (*p && (!channel || *p != ' '))
	{
		if (*p == '+' || *p == '-')
		{
			adding = *p++ == '+';
			continue;
		}
		if (channel)
		{
			const UdbNativeModeSchema *mode = schema->channels + *p;
			if (*p == 'r' || *p == 'P' || *p == 'O' || !mode->known || mode->member)
				return 0;
			if (adding || mode->unset_parameter)
				parameters += mode->parameters;
		}
		else if (*p < 'A' || (*p > 'Z' && *p < 'a') || *p > 'z' ||
				 !(snomask ? schema->snomasks[*p] : schema->users[*p]))
			return 0;
		letters++;
		p++;
	}
	if (!letters || parameters > MAXMODEPARAMS)
		return 0;
	if (channel)
		while (*p)
		{
			while (*p == ' ')
				p++;
			if (!*p)
				break;
			actual++;
			while (*p && *p != ' ')
				p++;
		}
	return actual == parameters;
}

static int udb_native_targets_valid(const char *value)
{
	static const char order[] = "cpnNPqduatTR";
	int previous = -1;
	if (!value || !*value)
		return 0;
	for (const char *p = value; *p; p++)
	{
		const char *found = strchr(order, *p);
		if (!found || found - order <= previous)
			return 0;
		previous = (int)(found - order);
	}
	return 1;
}

static void *udb_native_pcre_alloc(PCRE2_SIZE size, void *opaque)
{
	return udb_storage_alloc(opaque, UDB_STORAGE_MEMORY_AUX, size);
}

static void udb_native_pcre_free(void *memory, void *opaque)
{
	(void)opaque;
	udb_storage_free(memory);
}

static UdbStorageResult udb_native_pattern_validate(const char *stored, const char *method, UdbNativeSchema *schema)
{
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	pcre2_general_context *general;
	pcre2_compile_context *context;
	pcre2_code *code;
	PCRE2_SIZE offset;
	int error;
	if (!udb_spamfilter_pattern(stored, pattern, sizeof(pattern)))
		return UDB_STORAGE_REJECTED;
	if (!method || !strcmp(method, "simple"))
		return UDB_STORAGE_OK;
	if (strcmp(method, "regex"))
		return UDB_STORAGE_REJECTED;
	general = pcre2_general_context_create(udb_native_pcre_alloc, udb_native_pcre_free, schema->budget);
	if (!general)
		return UDB_STORAGE_LIMIT;
	context = pcre2_compile_context_create(general);
	if (!context)
	{
		pcre2_general_context_free(general);
		return UDB_STORAGE_LIMIT;
	}
	uint32_t options =
		PCRE2_CASELESS | (schema->spamfilter_utf8 ? PCRE2_MATCH_INVALID_UTF : PCRE2_NEVER_UTF | PCRE2_NEVER_UCP);
	code = pcre2_compile((const unsigned char *)pattern, PCRE2_ZERO_TERMINATED, options, &error, &offset, context);
	if (code)
		pcre2_code_free(code);
	pcre2_compile_context_free(context);
	pcre2_general_context_free(general);
	return code ? UDB_STORAGE_OK : error == PCRE2_ERROR_HEAP_FAILED ? UDB_STORAGE_LIMIT : UDB_STORAGE_REJECTED;
}

static int udb_native_span_string(UdbStorageSpan span, char *out, size_t capacity)
{
	if (span.length >= capacity || (span.length && !span.data))
		return 0;
	memcpy(out, span.data, span.length);
	out[span.length] = 0;
	return 1;
}

static int udb_native_descriptor_valid(const UdbKeyDescriptor *descriptor, const UdbStorageRow *row, const char *value,
									   UdbNativeSchema *schema)
{
	if ((descriptor->val_type == UDB_VAL_NUMERIC && row->kind != UDB_STORAGE_VALUE_NUMBER) ||
		(descriptor->val_type == UDB_VAL_STRING && (row->kind != UDB_STORAGE_VALUE_STRING || !*value)) ||
		(descriptor->val_type == UDB_VAL_NONE && row->kind != UDB_STORAGE_VALUE_NONE))
		return 0;
	if (descriptor->validator == udb_options_record_valid)
		return 1;
	if (descriptor->validator == udb_channel_modes_record_valid)
		return udb_native_modes_valid(value, schema, 1, 0);
	if (descriptor->validator == udb_user_modes_record_valid)
		return udb_native_modes_valid(value, schema, 0, 0);
	if (descriptor->validator == udb_snomasks_record_valid)
		return udb_native_modes_valid(value, schema, 0, 1);
	if (descriptor->validator == udb_clone_limit_valid)
		return row->number <= INT_MAX;
	return !descriptor->validator || descriptor->validator(value);
}

static UdbStorageResult udb_native_row_validate(const UdbStorageRecord *record, void *opaque)
{
	UdbNativeSchema *schema = opaque;
	const UdbStorageRow *row = &record->row;
	const UdbBlockSchema *block;
	const UdbKeyDescriptor *descriptor = NULL;
	char parts[3][UDB_COMPONENT_RAW_MAX + 1], value[UDB_RECORD_VALUE_MAX + 1];
	int deleting = record->operation != UDB_STORAGE_PUT;
	size_t wire = 1;
	if (!schema || !schema->budget || !udb_storage_row_valid(row) || row->depth > 3 ||
		(record->operation != UDB_STORAGE_PUT && record->operation != UDB_STORAGE_DELETE &&
		 record->operation != UDB_STORAGE_PREFIX_DELETE))
		return UDB_STORAGE_REJECTED;
	block = udb_get_block_schema(row->block);
	if (!block || row->depth > (unsigned int)block->max_depth ||
		(!deleting && row->depth < (unsigned int)block->min_depth))
		return UDB_STORAGE_REJECTED;
	for (unsigned int i = 0; i < row->depth; i++)
	{
		if (!udb_native_span_string(row->components[i], parts[i], sizeof(parts[i])))
			return UDB_STORAGE_REJECTED;
		wire += 2;
		for (unsigned int j = 0; j < row->components[i].length; j++)
		{
			unsigned char c = row->components[i].data[j];
			wire += c == ':' || c == '%' || c <= 32 || c >= 127 ? 3 : 1;
		}
	}
	value[0] = 0;
	if (row->kind == UDB_STORAGE_VALUE_STRING)
	{
		if (!udb_native_span_string(row->value, value, sizeof(value)))
			return UDB_STORAGE_REJECTED;
		wire += 2 + 4 * (((size_t)row->value.length + 2) / 3);
	}
	else if (row->kind == UDB_STORAGE_VALUE_NUMBER)
	{
		int count = snprintf(value, sizeof(value), "*%" PRIu64, row->number);
		if (count < 0 || count >= (int)sizeof(value))
			return UDB_STORAGE_REJECTED;
		wire += (size_t)count + 1; /* !u on wire; existing pure numeric validators consume *digits. */
	}
	if (row->kind == UDB_STORAGE_VALUE_NONE)
		wire += 2;
	if (wire + UDB_S2S_OVERHEAD_MAX > UDB_S2S_LINE_MAX)
		return UDB_STORAGE_REJECTED;
	if (deleting && (row->kind != UDB_STORAGE_VALUE_NONE || row->number))
		return UDB_STORAGE_REJECTED;
	if (row->block == 'K')
	{
		char type = parts[0][0];
		if (type >= 'a' && type <= 'z')
			type -= 'a' - 'A';
		if (parts[0][1] || !strchr("GZSQF", type))
			return UDB_STORAGE_REJECTED;
		if (row->depth == 1)
			return deleting ? UDB_STORAGE_OK : UDB_STORAGE_REJECTED;
		if ((type == 'G' || type == 'S') && !udb_line_mask_valid(parts[1]))
			return UDB_STORAGE_REJECTED;
		if (type == 'Z' && !udb_zline_mask_valid(parts[1]))
			return UDB_STORAGE_REJECTED;
		if (type == 'Q' && !udb_qline_mask_valid(parts[1]))
			return UDB_STORAGE_REJECTED;
		if (type == 'F')
		{
			UdbStorageResult result = udb_native_pattern_validate(parts[1], NULL, schema);
			if (result != UDB_STORAGE_OK)
				return result;
		}
		if (row->depth == 2)
			return deleting ? UDB_STORAGE_OK : UDB_STORAGE_REJECTED;
		if (!strcmp(parts[2], KKEY_EXPIRES) || (type == 'F' && !strcmp(parts[2], KKEY_BAN_TIME)))
			return deleting ||
						   (row->kind == UDB_STORAGE_VALUE_NUMBER && row->number && row->number <= udb_time_t_max_val())
					   ? UDB_STORAGE_OK
					   : UDB_STORAGE_REJECTED;
		if (!strcmp(parts[2], KKEY_REASON))
			return deleting || (row->kind == UDB_STORAGE_VALUE_STRING && udb_line_reason_valid(value))
					   ? UDB_STORAGE_OK
					   : UDB_STORAGE_REJECTED;
		if (type != 'F')
			return UDB_STORAGE_REJECTED;
		if (!strcmp(parts[2], KKEY_MATCH_TYPE))
			return deleting || (row->kind == UDB_STORAGE_VALUE_STRING && udb_spamfilter_match_type_valid(value))
					   ? UDB_STORAGE_OK
					   : UDB_STORAGE_REJECTED;
		if (!strcmp(parts[2], KKEY_TARGETS))
			return deleting || (row->kind == UDB_STORAGE_VALUE_STRING && udb_native_targets_valid(value))
					   ? UDB_STORAGE_OK
					   : UDB_STORAGE_REJECTED;
		if (!strcmp(parts[2], KKEY_ACTION))
			return deleting || (row->kind == UDB_STORAGE_VALUE_STRING && udb_spamfilter_action_valid(value))
					   ? UDB_STORAGE_OK
					   : UDB_STORAGE_REJECTED;
		return UDB_STORAGE_REJECTED;
	}
	if (block->root_key_validator && !block->root_key_validator(parts[0]))
		return UDB_STORAGE_REJECTED;
	if (row->block != 'S' && row->depth == 1)
		return UDB_STORAGE_OK;
	const char *subkey = parts[row->block == 'S' ? 0 : 1];
	for (size_t i = 0; i < block->subkey_count; i++)
		if (!strcmp(block->subkeys[i].key, subkey))
		{
			descriptor = block->subkeys + i;
			break;
		}
	if (!descriptor)
		return UDB_STORAGE_REJECTED;
	if (row->depth == 3)
	{
		if (!descriptor->allow_children || (descriptor->child_validator && !descriptor->child_validator(parts[2])))
			return UDB_STORAGE_REJECTED;
		return deleting || row->kind != UDB_STORAGE_VALUE_STRING || (*value != 0) ? UDB_STORAGE_OK
																				  : UDB_STORAGE_REJECTED;
	}
	return deleting || udb_native_descriptor_valid(descriptor, row, value, schema) ? UDB_STORAGE_OK
																				   : UDB_STORAGE_REJECTED;
}

typedef struct UdbNativeEntityValidation
{
	UdbStorageRow key;
	unsigned char bytes[UDB_STORAGE_KEY_MAX];
	UdbStorageField fields[6];
	unsigned int seen;
	unsigned int attributes;
	int forbid;
	int present;
} UdbNativeEntityValidation;

static UdbStorageResult udb_native_entity_finish(UdbNativeEntityValidation *entity, UdbNativeSchema *schema)
{
	if (!entity->present)
		return UDB_STORAGE_OK;
	if (entity->key.block == 'N')
		return entity->forbid && entity->attributes != 1 ? UDB_STORAGE_REJECTED : UDB_STORAGE_OK;
	if (entity->key.block != 'K' || entity->key.components[0].length != 1 ||
		udb_storage_fold(entity->key.components[0].data[0]) != 'f')
		return UDB_STORAGE_OK;
	/* Partial F profiles are inert. A complete profile must compile under the
	 * frozen daemon UTF policy before persistence, ACK or native TKL effects. */
	if ((entity->seen & 15) != 15)
		return UDB_STORAGE_OK;
	char pattern[UDB_COMPONENT_RAW_MAX + 1];
	if (!udb_native_span_string(entity->key.components[1], pattern, sizeof(pattern)))
		return UDB_STORAGE_REJECTED;
	return udb_native_pattern_validate(pattern, entity->fields[1].string, schema);
}

static UdbStorageResult udb_native_entity_add(UdbNativeEntityValidation *entity, const UdbStorageRecord *record,
											  UdbNativeSchema *schema)
{
	static const char *const properties[6] = {KKEY_REASON, KKEY_MATCH_TYPE, KKEY_TARGETS,
											  KKEY_ACTION, KKEY_BAN_TIME,	KKEY_EXPIRES};
	UdbStorageRow key = record->row;
	UdbStorageResult result;
	unsigned int depth = key.block == 'K' ? 2 : 1;
	size_t length;
	key.depth = depth;
	key.kind = UDB_STORAGE_VALUE_NONE;
	key.value.data = NULL;
	key.value.length = 0;
	key.number = 0;
	if (!entity->present || !udb_storage_key_has_prefix(&record->row, &entity->key))
	{
		result = udb_native_entity_finish(entity, schema);
		if (result != UDB_STORAGE_OK)
			return result;
		memset(entity, 0, sizeof(*entity));
		result = udb_storage_row_encode(&key, entity->bytes, sizeof(entity->bytes), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_row_decode(entity->bytes, length, &entity->key);
		if (result != UDB_STORAGE_OK)
			return result;
		entity->present = 1;
	}
	if (record->row.depth == depth)
		return UDB_STORAGE_OK;
	entity->attributes++;
	if (record->row.block == 'N')
	{
		const UdbStorageSpan *name = record->row.components + 1;
		if (name->length == sizeof(NKEY_FORBID) - 1 && !memcmp(name->data, NKEY_FORBID, name->length))
			entity->forbid = 1;
		return UDB_STORAGE_OK;
	}
	if (record->row.block != 'K' || record->row.depth != 3)
		return UDB_STORAGE_REJECTED;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageSpan name = record->row.components[2];
		if (name.length != strlen(properties[i]) || memcmp(name.data, properties[i], name.length))
			continue;
		if (entity->seen & (1U << i))
			return UDB_STORAGE_REJECTED;
		entity->seen |= 1U << i;
		UdbStorageField *field = entity->fields + i;
		field->kind = record->row.kind;
		field->number = record->row.number;
		field->present = 1;
		if (field->kind == UDB_STORAGE_VALUE_STRING)
		{
			field->length = record->row.value.length;
			memcpy(field->string, record->row.value.data, field->length);
			field->string[field->length] = 0;
		}
		return UDB_STORAGE_OK;
	}
	return UDB_STORAGE_REJECTED;
}

static UdbStorageResult udb_native_stage_validate(const UdbStorageSource *source, unsigned char block, void *opaque)
{
	UdbNativeSchema *schema = opaque;
	UdbNativeEntityValidation *entity;
	UdbStorageResult result = UDB_STORAGE_OK, closed;
	UdbStorageRecord record;
	void *handle = NULL;
	if (!schema || !source)
		return UDB_STORAGE_ERROR;
	if (block != 'N' && block != 'K')
		return UDB_STORAGE_OK;
	entity = udb_storage_alloc(schema->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*entity));
	if (!entity)
		return UDB_STORAGE_LIMIT;
	for (unsigned int i = 0; i < UDB_STORAGE_BASE_PARTITIONS; i++)
	{
		UdbStorageLogicalPosition position = {0};
		position.block = block;
		position.partition = (uint16_t)i;
		result = source->open(source->context, &position, &handle);
		if (result != UDB_STORAGE_OK)
			break;
		while ((result = source->next(handle, &record)) == UDB_STORAGE_OK)
		{
			result = udb_native_row_validate(&record, schema);
			if (result == UDB_STORAGE_OK)
				result = udb_native_entity_add(entity, &record, schema);
			if (result != UDB_STORAGE_OK)
				break;
		}
		if (result == UDB_STORAGE_NOT_FOUND)
			result = udb_native_entity_finish(entity, schema);
		closed = source->close(&handle);
		if (result == UDB_STORAGE_OK)
			result = closed;
		if (result != UDB_STORAGE_OK)
			break;
		memset(entity, 0, sizeof(*entity));
	}
	udb_storage_free(entity);
	return result;
}

static UdbStorageResult udb_native_mutation_validate(UdbStorageEngine *engine, const UdbStorageBlockRoot *root,
													 const UdbStorageRow *changed, void *opaque)
{
	UdbNativeSchema *schema = opaque;
	UdbNativeEntityValidation *entity;
	UdbStorageBlockIterator *iterator = NULL;
	UdbStorageRow prefix;
	UdbStorageRecord record;
	UdbStorageResult result, closed;
	int block;
	if (!schema || !engine || !root || !changed)
		return UDB_STORAGE_ERROR;
	if (changed->block != 'N' && changed->block != 'K')
		return UDB_STORAGE_OK;
	/* K type-only deletion cannot introduce a malformed profile and all its
	 * covered rows have already been materialized through the tombstone. */
	if (changed->block == 'K' && changed->depth == 1)
		return UDB_STORAGE_OK;
	prefix = *changed;
	prefix.depth = changed->block == 'N' ? 1 : 2;
	prefix.kind = UDB_STORAGE_VALUE_NONE;
	prefix.value.data = NULL;
	prefix.value.length = 0;
	prefix.number = 0;
	block = udb_storage_stage_block_index(prefix.block);
	if (block < 0)
		return UDB_STORAGE_ERROR;
	entity = udb_storage_alloc(schema->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*entity));
	if (!entity)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_block_iterator_create(engine->cache, engine->files + block, root, &prefix, &iterator);
	if (result != UDB_STORAGE_OK)
		goto done;
	while ((result = udb_storage_block_iterator_next(iterator, &record)) == UDB_STORAGE_OK)
	{
		result = udb_native_row_validate(&record, schema);
		if (result == UDB_STORAGE_OK)
			result = udb_native_entity_add(entity, &record, schema);
		if (result != UDB_STORAGE_OK)
			break;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = udb_native_entity_finish(entity, schema);
	closed = udb_storage_block_iterator_close(&iterator);
	if (result == UDB_STORAGE_OK)
		result = closed;
done:
	udb_storage_free(entity);
	return result;
}

static UdbStorageResult udb_native_mutation_normalize(const UdbStorageRecord *record, UdbStorageRow *prefix,
													  void *context)
{
	(void)context;
	const UdbStorageRow *row = &record->row;
	if (record->operation != UDB_STORAGE_PUT || row->block != 'N' || row->depth != 2 ||
		row->components[1].length != sizeof(NKEY_FORBID) - 1 ||
		memcmp(row->components[1].data, NKEY_FORBID, sizeof(NKEY_FORBID) - 1))
		return UDB_STORAGE_NOT_FOUND;
	*prefix = *row;
	prefix->depth = 1;
	prefix->kind = UDB_STORAGE_VALUE_NONE;
	prefix->number = 0;
	prefix->value.data = NULL;
	prefix->value.length = 0;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_native_block_profiles_validate(UdbStorageEngine *engine, const UdbStorageBlockRoot *root,
														   void *context)
{
	UdbStorageBlockIterator *iterator = NULL;
	UdbStorageRecord record;
	UdbStorageResult result, closed;
	int block = udb_storage_stage_block_index(root->block);
	if (block < 0)
		return UDB_STORAGE_ERROR;
	if (root->block != 'N' && root->block != 'K')
		return UDB_STORAGE_OK;
	result = udb_storage_block_iterator_create(engine->cache, engine->files + block, root, NULL, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	while ((result = udb_storage_block_iterator_next(iterator, &record)) == UDB_STORAGE_OK)
	{
		unsigned int depth = root->block == 'N' ? 2 : 3;
		const char *name = root->block == 'N' ? NKEY_FORBID : KKEY_MATCH_TYPE;
		if (record.row.depth != depth)
			continue;
		UdbStorageSpan property = record.row.components[depth - 1];
		if (property.length != strlen(name) || memcmp(property.data, name, property.length))
			continue;
		/* Profile scans use a separate iterator and immutable copied prefix,
		 * leaving this whole-block cursor valid until its next advance. */
		result = udb_native_mutation_validate(engine, root, &record.row, context);
		if (result != UDB_STORAGE_OK)
			break;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = UDB_STORAGE_OK;
	closed = udb_storage_block_iterator_close(&iterator);
	return result == UDB_STORAGE_OK ? closed : result;
}
