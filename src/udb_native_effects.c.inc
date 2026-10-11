/* Durable publication projection. Reads use the receipt's exact generations,
 * never whichever newer group happens to be current during foreground work. */

static void udb_native_effects_fail(UdbStorageEngine *engine)
{
	/* Durable data cannot be rolled back. Stop admission until reload repairs
	 * effect ownership from the validated roots, rather than claiming READY
	 * from a later no-op transaction that skipped the failed projection. */
	udb_storage_mutex_lock(&engine->mutex);
	engine->failed = 1;
	engine->admission = 0;
	udb_storage_engine_wake(engine);
	udb_storage_mutex_unlock(&engine->mutex);
	udb_ready = 0;
	udb_sync_status = UDB_SYNC_DEGRADED;
}

static UdbStorageResult udb_native_runtime_publication(UdbStorageJob *job)
{
	UdbStorageRead *before = NULL, *after = NULL;
	UdbStorageResult result = udb_storage_read_job(job, 0, &before);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_read_job(job, 1, &after);
	unsigned int changed = 0;
	if (result == UDB_STORAGE_OK)
		for (unsigned int i = 0; i < 6; i++)
			if (memcmp(before->generation->state.blocks[i].summary.digest,
					   after->generation->state.blocks[i].summary.digest, 32))
				changed |= 1U << i;
	UdbStorageRow row = {0};
	char entity[UDB_STORAGE_COMPONENT_MAX + 1];
	int scoped = job->kind == UDB_STORAGE_JOB_POINT || job->kind == UDB_STORAGE_JOB_PREFIX ||
				 job->kind == UDB_STORAGE_JOB_EXPIRE;
	if (result == UDB_STORAGE_OK && scoped)
	{
		size_t offset = UDB_STORAGE_MUTATION_HEADER + (job->kind == UDB_STORAGE_JOB_EXPIRE ? 8 : 0);
		result = job->length > offset ? udb_storage_row_decode(job->payload + offset, job->length - offset, &row)
									  : UDB_STORAGE_ERROR;
		if (result == UDB_STORAGE_OK && !udb_native_span_string(row.components[0], entity, sizeof(entity)))
			result = UDB_STORAGE_ERROR;
	}
	if (result == UDB_STORAGE_OK && (changed & (1U << 3)))
		result = udb_native_settings_apply(udb_ctx, after);
	if (result == UDB_STORAGE_OK && (changed & 1))
	{
		Client *client, *next;
		list_for_each_entry_safe(client, next, &client_list, client_node)
		{
			if (!IsUser(client) || !MyConnect(client) || !client->local || (scoped && strcasecmp(client->name, entity)))
				continue;
			UdbStorageResult applied = udb_nick_reconcile_generations(client, before, after);
			if (applied < UDB_STORAGE_NOT_FOUND)
				result = applied;
		}
	}
	if (result == UDB_STORAGE_OK && (changed & (1U << 1)))
		result = scoped ? udb_channel_reconcile_entity(before, after, entity)
						: udb_channel_reconcile_generations(before, after);
	if (result == UDB_STORAGE_OK && (changed & (1U << 2)))
		result = udb_ip_reconcile_scope(before, after, scoped ? entity : NULL);
	if (result == UDB_STORAGE_OK && (changed & (1U << 5)))
		result =
			scoped ? udb_lines_reconcile_scope(before, after, &row) : udb_lines_reconcile_generations(before, after);
	if (before && udb_storage_read_close(&before) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	if (after && udb_storage_read_close(&after) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	if (result != UDB_STORAGE_OK)
		udb_native_effects_fail(job->engine);
	return result;
}

static UdbStorageResult udb_native_runtime_startup(UdbContext *ctx)
{
	UdbStorageRead *before = NULL, *after = NULL;
	UdbStorageResult result = udb_storage_read_empty(ctx->storage, &before);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_read_open(ctx->storage, &after);
	if (result == UDB_STORAGE_OK)
		result = udb_native_settings_apply(ctx, after);
	if (result == UDB_STORAGE_OK)
	{
		Client *client, *next;
		list_for_each_entry_safe(client, next, &lclient_list, lclient_node)
		{
			if (!MyUser(client))
				continue;
			UdbStorageResult applied = udb_nick_reconcile_generations(client, before, after);
			if (applied < UDB_STORAGE_NOT_FOUND)
				result = applied;
		}
	}
	if (result == UDB_STORAGE_OK)
		result = udb_channel_reconcile_generations(before, after);
	if (result == UDB_STORAGE_OK)
		result = udb_ip_reconcile_generations(before, after);
	if (result == UDB_STORAGE_OK)
		result = udb_lines_reconcile_generations(before, after);
	if (before && udb_storage_read_close(&before) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	if (after && udb_storage_read_close(&after) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_native_receive_publication(UdbStorageJob *job, unsigned long id, Client *peer)
{
	UdbContext *ctx = udb_ctx;
	if (!ctx || ctx->storage != job->engine || !job->after || !job->before || !job->committed_generation)
		return UDB_STORAGE_ERROR;
	const UdbStorageState *state = &job->after->state;
	udb_policy_notify_deferred = 1;
	UdbStorageResult effects = udb_native_runtime_publication(job);
	memcpy(ctx->authority_sid, state->source_sid, 4);
	snprintf(ctx->authority_epoch, sizeof(ctx->authority_epoch), "%016" PRIx64, state->source_epoch);
	ctx->authority_epoch_known = 1;
	ctx->candidate_authority_epoch_known = 0;
	ctx->candidate_authority_sid[0] = 0;
	ctx->candidate_authority_epoch[0] = 0;
	ctx->last_applied_seq = state->source_sequence;
	ctx->current_seq = !strcmp(state->source_sid, me.id) ? state->source_sequence : 0;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbBlock *block = udb_block_by_letter(ctx, (char)state->blocks[i].block);
		if (!block)
		{
			effects = UDB_STORAGE_ERROR;
			continue;
		}
		udb_native_digest_hex(state->blocks[i].summary.digest, block->sha256);
		block->record_count = state->blocks[i].summary.count;
		block->generation = state->generation;
		block->load_state = state->blocks[i].summary.count ? UDB_LOAD_SUCCESS : UDB_LOAD_EMPTY;
		block->modified_at = time(NULL);
	}
	udb_policy_notify_deferred = 0;
	udb_propagator_policy_flush(ctx);
	udb_sync_snomask_filter();
	(void)id;
	(void)peer;
	/* The receiver owns catch-up and READY after the durable six-root commit. */
	return effects;
}
