/* Native single-writer scheduling and immutable generation ownership.
 * Copied jobs group for <=5 ms or >=1 MiB. The durability barrier is the
 * authorization linearization point: cancellation/revision changes before it
 * reject the entire prepared group, never publish its private roots.
 * Main-loop effects/ACK/relay are consumers of completed durable jobs only. */

#define UDB_STORAGE_WRITER_STACK ((size_t)1024 * 1024)
#define UDB_STORAGE_GROUP_BYTES ((size_t)1024 * 1024)
#define UDB_STORAGE_CHECKPOINT_WAL ((uint64_t)64 * 1024 * 1024)

static uint64_t udb_storage_monotonic_ms(void)
{
#ifdef _WIN32
	return GetTickCount64();
#else
	struct timespec time;
	if (clock_gettime(CLOCK_MONOTONIC, &time) || time.tv_sec < 0 || (uint64_t)time.tv_sec > (UINT64_MAX - 999) / 1000)
		return UINT64_MAX;
	return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
#endif
}

static void udb_storage_engine_wake(UdbStorageEngine *engine)
{
#ifdef _WIN32
	WakeConditionVariable(&engine->condition);
#else
	if (pthread_cond_signal(&engine->condition))
		abort();
#endif
}

/* Called with engine->mutex held. Returns OK or ERROR, timeout is OK. */
static UdbStorageResult udb_storage_engine_wait(UdbStorageEngine *engine, uint64_t deadline)
{
#ifdef _WIN32
	DWORD timeout = INFINITE;
	if (deadline)
	{
		uint64_t now = udb_storage_monotonic_ms();
		if (now == UINT64_MAX)
			return UDB_STORAGE_ERROR;
		if (now >= deadline)
			return UDB_STORAGE_OK;
		timeout = deadline - now > MAXDWORD - 1 ? MAXDWORD - 1 : (DWORD)(deadline - now);
	}
	if (!SleepConditionVariableCS(&engine->condition, &engine->mutex.native, timeout) &&
		GetLastError() != ERROR_TIMEOUT)
		return UDB_STORAGE_ERROR;
#else
	int result;
	if (deadline)
	{
		struct timespec time;
		time.tv_sec = (time_t)(deadline / 1000);
		time.tv_nsec = (long)(deadline % 1000) * 1000000;
		result = pthread_cond_timedwait(&engine->condition, &engine->mutex.native, &time);
	}
	else
		result = pthread_cond_wait(&engine->condition, &engine->mutex.native);
	if (result && result != ETIMEDOUT)
		return UDB_STORAGE_ERROR;
#endif
	return UDB_STORAGE_OK;
}

static void udb_storage_engine_generation_drop(UdbStorageEngine *engine, UdbStorageGeneration *generation)
{
	UdbStorageGeneration **link = &engine->generations;
	while (*link && *link != generation)
		link = &(*link)->next;
	if (!*link || !generation->references)
		abort();
	if (--generation->references)
		return;
	*link = generation->next;
	udb_storage_free(generation);
}

static UdbStorageResult udb_storage_engine_pin(UdbStorageEngine *engine, UdbStorageGeneration **generation)
{
	UdbStorageResult result = UDB_STORAGE_ERROR;
	if (!engine || !generation || !engine->mutex.initialized)
		return result;
	*generation = NULL;
	udb_storage_mutex_lock(&engine->mutex);
	if (engine->current && !engine->failed && !engine->stopping)
	{
		if (engine->current->references == SIZE_MAX)
			result = UDB_STORAGE_LIMIT;
		else
		{
			engine->current->references++;
			*generation = engine->current;
			result = UDB_STORAGE_OK;
		}
	}
	udb_storage_mutex_unlock(&engine->mutex);
	return result;
}

static UdbStorageResult udb_storage_engine_release(UdbStorageEngine *engine, UdbStorageGeneration **pointer)
{
	UdbStorageGeneration *generation;
	if (!engine || !pointer || !*pointer || !engine->mutex.initialized)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&engine->mutex);
	for (generation = engine->generations; generation && generation != *pointer; generation = generation->next)
		;
	if (!generation || (generation == engine->current && generation->references == 1))
	{
		udb_storage_mutex_unlock(&engine->mutex);
		return UDB_STORAGE_ERROR;
	}
	udb_storage_engine_generation_drop(engine, generation);
	*pointer = NULL;
	udb_storage_mutex_unlock(&engine->mutex);
	return UDB_STORAGE_OK;
}

static int udb_storage_engine_job_valid(UdbStorageEngine *engine, const UdbStorageJob *job)
{
	return !job->cancelled && job->module_revision == engine->module_revision &&
		   job->authority_revision == engine->authority_revision && !engine->failed;
}

static void udb_storage_engine_complete(UdbStorageEngine *engine, UdbStorageJob *jobs)
{
	UdbStorageJob *tail = jobs;
	if (!jobs)
		return;
	while (tail->next)
		tail = tail->next;
	if (engine->completed_tail)
		engine->completed_tail->next = jobs;
	else
		engine->completed = jobs;
	engine->completed_tail = tail;
	engine->active = NULL;
}

static void udb_storage_engine_run(UdbStorageEngine *engine)
{
	for (;;)
	{
		UdbStorageJob *jobs, *tail;
		UdbStorageGeneration *generation = NULL;
		UdbStorageState candidate, prepared;
		UdbStorageResult result = UDB_STORAGE_OK;
		uint64_t deadline;
		size_t bytes = 0;
		unsigned int accepted = 0;
		udb_storage_mutex_lock(&engine->mutex);
		while (!engine->pending && !engine->stopping)
		{
			result = udb_storage_engine_wait(engine, 0);
			if (result != UDB_STORAGE_OK)
			{
				engine->failed = 1;
				engine->stopping = 1;
				engine->admission = 0;
				break;
			}
		}
		if (!engine->pending && engine->stopping)
		{
			udb_storage_mutex_unlock(&engine->mutex);
			break;
		}
		deadline = udb_storage_monotonic_ms();
		if (deadline > UINT64_MAX - 5)
		{
			engine->failed = 1;
			engine->stopping = 1;
			engine->admission = 0;
		}
		else
			deadline += 5;
		while (!engine->stopping && engine->pending_bytes < UDB_STORAGE_GROUP_BYTES &&
			   udb_storage_monotonic_ms() < deadline)
		{
			result = udb_storage_engine_wait(engine, deadline);
			if (result != UDB_STORAGE_OK)
			{
				engine->failed = 1;
				engine->stopping = 1;
				engine->admission = 0;
			}
		}
		jobs = tail = engine->pending;
		for (;;)
		{
			bytes += tail->charge;
			if (!tail->next || bytes >= UDB_STORAGE_GROUP_BYTES)
				break;
			tail = tail->next;
		}
		engine->pending = tail->next;
		if (!engine->pending)
			engine->pending_tail = NULL;
		tail->next = NULL;
		engine->pending_bytes -= bytes;
		engine->active = jobs;
		candidate = engine->journal.state;
		if (candidate.generation == UINT64_MAX)
			engine->failed = 1;
		else
			candidate.generation++;
		udb_storage_mutex_unlock(&engine->mutex);
		for (UdbStorageJob *job = jobs; job; job = job->next)
		{
			int valid;
			do
			{
				udb_storage_mutex_lock(&engine->mutex);
				valid = udb_storage_engine_job_valid(engine, job);
				udb_storage_mutex_unlock(&engine->mutex);
				job->result = valid ? engine->prepare(engine, job, &candidate, &prepared) : UDB_STORAGE_BUSY;
				/* CONTINUE is bounded private work, not a durability receipt.
				 * Recheck lifecycle/authority before every further output job. */
			} while (job->result == UDB_STORAGE_CONTINUE);
			/* Release cancelled/failed private controllers before preparing a later
			 * job. Durable success cleanup still waits for the group outcome. */
			if (job->result < UDB_STORAGE_NOT_FOUND && engine->finish && engine->finish(engine, job) != UDB_STORAGE_OK)
				job->result = UDB_STORAGE_ERROR;
			if (job->result == UDB_STORAGE_ERROR || job->result == UDB_STORAGE_CORRUPT)
			{
				udb_storage_mutex_lock(&engine->mutex);
				engine->failed = 1;
				engine->admission = 0;
				udb_storage_mutex_unlock(&engine->mutex);
			}
			if (job->result == UDB_STORAGE_OK)
			{
				if (!udb_storage_state_valid(&prepared) || prepared.generation != candidate.generation ||
					prepared.sequence < candidate.sequence || prepared.next_object < candidate.next_object ||
					memcmp(prepared.store_identity, candidate.store_identity, 16))
				{
					job->result = UDB_STORAGE_CORRUPT;
					udb_storage_mutex_lock(&engine->mutex);
					engine->failed = 1;
					engine->admission = 0;
					udb_storage_mutex_unlock(&engine->mutex);
				}
				else
				{
					/* Effects need this transaction's exact before/after roots, not
					 * the final roots of this or a later flushed group. Admission
					 * reserves both descriptors within the total queue cap. */
					job->before = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*job->before));
					job->after = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*job->after));
					if (!job->before || !job->after)
					{
						udb_storage_free(job->before);
						udb_storage_free(job->after);
						job->before = job->after = NULL;
						job->result = UDB_STORAGE_LIMIT;
						if (engine->finish && engine->finish(engine, job) != UDB_STORAGE_OK)
						{
							job->result = UDB_STORAGE_ERROR;
							udb_storage_mutex_lock(&engine->mutex);
							engine->failed = 1;
							engine->admission = 0;
							udb_storage_mutex_unlock(&engine->mutex);
						}
						continue;
					}
					job->before->state = candidate;
					job->after->state = prepared;
					job->before->references = job->after->references = 1;
					job->committed_storage_sequence = prepared.sequence;
					job->committed_source_epoch = prepared.source_epoch;
					job->committed_source_sequence = prepared.source_sequence;
					memcpy(job->committed_source_sid, prepared.source_sid, 4);
					candidate = prepared;
					accepted++;
				}
			}
		}
		if (accepted)
		{
			generation = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*generation));
			if (!generation)
				result = UDB_STORAGE_LIMIT;
			else
			{
				generation->state = candidate;
				generation->references = 1;
			}
			/* Final barrier. State/event changes taking this mutex afterwards are
			 * ordered after this group, whose commit can no longer be cancelled. */
			udb_storage_mutex_lock(&engine->mutex);
			for (UdbStorageJob *job = jobs; job; job = job->next)
				if (job->result == UDB_STORAGE_OK && !udb_storage_engine_job_valid(engine, job))
					result = UDB_STORAGE_BUSY;
			if (result == UDB_STORAGE_OK)
				for (UdbStorageJob *job = jobs; job; job = job->next)
					if (job->result == UDB_STORAGE_OK)
						job->committing = 1;
			udb_storage_mutex_unlock(&engine->mutex);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_journal_commit(&engine->journal, &candidate);
		}
		udb_storage_mutex_lock(&engine->mutex);
		if (accepted && result == UDB_STORAGE_OK)
		{
			UdbStorageGeneration *old = engine->current;
			/* Receipt pins enter reachability only after the WAL barrier.
			 * Each detached completion owns them until job_free(). */
			for (UdbStorageJob *job = jobs; job; job = job->next)
				if (job->result == UDB_STORAGE_OK)
				{
					job->before->next = engine->generations;
					engine->generations = job->before;
					job->after->next = engine->generations;
					engine->generations = job->after;
				}
			generation->next = engine->generations;
			engine->generations = generation;
			engine->current = generation;
			generation = NULL;
			udb_storage_engine_generation_drop(engine, old);
		}
		if (engine->journal.failed)
		{
			engine->failed = 1;
			engine->admission = 0;
		}
		for (UdbStorageJob *job = jobs; job; job = job->next)
			if (job->result == UDB_STORAGE_OK)
			{
				job->result = result;
				if (result == UDB_STORAGE_OK)
					job->committed_generation = candidate.generation;
				else
				{
					udb_storage_free(job->before);
					udb_storage_free(job->after);
					job->before = job->after = NULL;
				}
			}
		udb_storage_mutex_unlock(&engine->mutex);
		udb_storage_free(generation);
		if (engine->finish)
			for (UdbStorageJob *job = jobs; job; job = job->next)
				if (engine->finish(engine, job) != UDB_STORAGE_OK)
				{
					udb_storage_mutex_lock(&engine->mutex);
					engine->failed = 1;
					engine->admission = 0;
					udb_storage_mutex_unlock(&engine->mutex);
				}
		udb_storage_mutex_lock(&engine->mutex);
		udb_storage_engine_complete(engine, jobs);
		udb_storage_mutex_unlock(&engine->mutex);
		if (!engine->journal.failed && engine->journal.end >= UDB_STORAGE_CHECKPOINT_WAL)
		{
			result = udb_storage_reclaimer_refresh(engine);
			if (result != UDB_STORAGE_OK)
			{
				udb_storage_mutex_lock(&engine->mutex);
				engine->failed = 1;
				engine->admission = 0;
				udb_storage_mutex_unlock(&engine->mutex);
			}
		}
	}
}

#ifdef _WIN32
static DWORD WINAPI udb_storage_engine_thread(LPVOID pointer)
{
	udb_storage_engine_run(pointer);
	return 0;
}
#else
static void *udb_storage_engine_thread(void *pointer)
{
	udb_storage_engine_run(pointer);
	return NULL;
}
#endif

static UdbStorageResult udb_storage_engine_open(UdbStorageEngine *engine, const char *parent, size_t cache_bytes,
												UdbStoragePrepareJob prepare, UdbStorageFinishJob finish,
												UdbStorageEngineCleanup cleanup, UdbStorageRecordValidator validator,
												UdbStorageStageValidator stage_validator,
												UdbStorageMutationValidator mutation_validator,
												UdbStorageMutationNormalizer normalizer,
												UdbStorageBlockValidator block_validator, void *context)
{
	static const char *const names[9] = {"udb_N.db", "udb_C.db",   "udb_I.db",	   "udb_S.db",	  "udb_L.db",
										 "udb_K.db", "udb_wal.db", "udb_meta0.db", "udb_meta1.db"};
	UdbStorageResult result;
	unsigned int existing = 0;
	if (!engine || !prepare || engine->budget.initialized)
		return UDB_STORAGE_ERROR;
	memset(engine, 0, sizeof(*engine));
	engine->validator = validator;
	engine->stage_validator = stage_validator;
	engine->mutation_validator = mutation_validator;
	engine->normalizer = normalizer;
	engine->block_validator = block_validator;
	engine->validation_context = context;
	result = udb_storage_budget_init(&engine->budget, cache_bytes);
	if (result != UDB_STORAGE_OK)
		return result;
	result =
		udb_storage_budget_charge(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*engine) - sizeof(engine->budget));
	if (result != UDB_STORAGE_OK)
		goto fail;
	engine->charged = 1;
	engine->prepare = prepare;
	engine->finish = finish;
	engine->cleanup = cleanup;
	engine->module_revision = engine->authority_revision = engine->next_job = 1;
	if (!udb_storage_mutex_init(&engine->mutex))
	{
		result = UDB_STORAGE_ERROR;
		goto fail;
	}
#ifdef _WIN32
	InitializeConditionVariable(&engine->condition);
#else
	{
		pthread_condattr_t attributes;
		int error;
		if (pthread_condattr_init(&attributes))
		{
			result = UDB_STORAGE_ERROR;
			goto fail;
		}
		error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
		if (!error)
			error = pthread_cond_init(&engine->condition, &attributes);
		pthread_condattr_destroy(&attributes);
		if (error)
		{
			result = UDB_STORAGE_ERROR;
			goto fail;
		}
	}
#endif
	engine->condition_initialized = 1;
	result = udb_storage_cache_create(&engine->budget, &engine->cache);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_directory_open(&engine->directory, &engine->budget, parent);
	if (result != UDB_STORAGE_OK)
		goto fail;
	for (unsigned int i = 0; i < 9; i++)
	{
		result = udb_storage_file_open(&engine->directory, names[i], 0, engine->files + i);
		if (result == UDB_STORAGE_OK)
			existing++;
		else if (result != UDB_STORAGE_NOT_FOUND)
			goto fail;
	}
	/* A mixed/incomplete provision is corruption, never an empty database. */
	if (existing && existing != 9)
	{
		result = UDB_STORAGE_CORRUPT;
		goto fail;
	}
	if (!existing)
		for (unsigned int i = 0; i < 9; i++)
		{
			result = udb_storage_file_open(&engine->directory, names[i], 1, engine->files + i);
			if (result != UDB_STORAGE_OK)
				goto fail;
		}
	engine->journal.cache = engine->cache;
	for (unsigned int i = 0; i < 6; i++)
		engine->journal.data[i] = engine->files + i;
	engine->journal.wal = engine->files + 6;
	engine->journal.metadata[0] = engine->files + 7;
	engine->journal.metadata[1] = engine->files + 8;
	result = existing ? udb_storage_journal_recover(&engine->journal, validator, context)
					  : udb_storage_journal_initialize(&engine->journal);
	if (result != UDB_STORAGE_OK)
		goto fail;
	memcpy(engine->directory.store_identity, engine->journal.state.store_identity, 16);
	if (engine->block_validator)
		for (unsigned int i = 0; i < 6; i++)
		{
			result = engine->block_validator(engine, engine->journal.state.blocks + i, context);
			if (result != UDB_STORAGE_OK)
				goto fail;
		}
	engine->current = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*engine->current));
	if (!engine->current)
	{
		result = UDB_STORAGE_LIMIT;
		goto fail;
	}
	engine->current->state = engine->journal.state;
	engine->current->references = 1;
	engine->generations = engine->current;
	result = udb_storage_orphans_cleanup(&engine->directory);
	if (result != UDB_STORAGE_OK)
		goto fail;
	result = udb_storage_reclaimer_refresh(engine);
	if (result != UDB_STORAGE_OK)
		goto fail;
	result = udb_storage_budget_charge(&engine->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_WRITER_STACK);
	if (result != UDB_STORAGE_OK)
		goto fail;
	engine->stack_charged = 1;
#ifdef _WIN32
	engine->thread = CreateThread(NULL, UDB_STORAGE_WRITER_STACK, udb_storage_engine_thread, engine,
								  STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
	if (!engine->thread)
	{
		result = UDB_STORAGE_ERROR;
		goto fail;
	}
#else
	{
		pthread_attr_t attributes;
		int error;
		if (pthread_attr_init(&attributes))
		{
			result = UDB_STORAGE_ERROR;
			goto fail;
		}
		error = pthread_attr_setstacksize(&attributes, UDB_STORAGE_WRITER_STACK);
		if (!error)
			error = pthread_create(&engine->thread, &attributes, udb_storage_engine_thread, engine);
		pthread_attr_destroy(&attributes);
		if (error)
		{
			result = UDB_STORAGE_ERROR;
			goto fail;
		}
	}
#endif
	engine->thread_started = 1;
	engine->admission = 1;
	return UDB_STORAGE_OK;
fail:
	engine->failed = 1;
	/* Caller retains this object even on cleanup failure; never free handles
	 * or worker-owned state while an OS operation still owns them. */
	udb_storage_engine_close(engine);
	return result;
}

/* Replay ingress reserves both its copied envelope and its eventual writer
 * job. Transferring that credit does not need fresh queue capacity, preventing
 * a full replay buffer from deadlocking its own drain. All reservations share
 * the same queue bound as pending, completed and detached jobs. */
static size_t udb_storage_engine_job_charge(size_t length)
{
	if (length > UDB_STORAGE_QUEUE_LIMIT - sizeof(UdbStorageJob))
		return 0;
	size_t charge = udb_storage_allocation_charge(sizeof(UdbStorageJob) + length);
	size_t receipt = udb_storage_allocation_charge(sizeof(UdbStorageGeneration));
	if (!charge || !receipt || receipt > (SIZE_MAX - charge) / 2)
		return 0;
	charge += 2 * receipt;
	return charge <= UDB_STORAGE_QUEUE_LIMIT ? charge : 0;
}

static UdbStorageResult udb_storage_engine_delivery_reserve(UdbStorageEngine *engine, uint64_t module_revision,
															uint64_t authority_revision, size_t charge)
{
	if (!engine || !engine->mutex.initialized || !charge || charge > UDB_STORAGE_QUEUE_LIMIT)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&engine->mutex);
	UdbStorageResult result = UDB_STORAGE_OK;
	if (!engine->admission || engine->failed || engine->stopping || engine->module_revision != module_revision ||
		engine->authority_revision != authority_revision)
		result = UDB_STORAGE_BUSY;
	else if (engine->queue_bytes > UDB_STORAGE_QUEUE_LIMIT || charge > UDB_STORAGE_QUEUE_LIMIT - engine->queue_bytes)
		result = UDB_STORAGE_LIMIT;
	else
	{
		engine->delivery_bytes += charge;
		engine->queue_bytes += charge;
	}
	udb_storage_mutex_unlock(&engine->mutex);
	return result;
}

static UdbStorageResult udb_storage_engine_delivery_release(UdbStorageEngine *engine, size_t charge)
{
	if (!engine || !engine->mutex.initialized || !charge)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&engine->mutex);
	UdbStorageResult result = UDB_STORAGE_OK;
	if (charge > engine->delivery_bytes || charge > engine->queue_bytes)
		result = UDB_STORAGE_ERROR;
	else
	{
		engine->delivery_bytes -= charge;
		engine->queue_bytes -= charge;
	}
	udb_storage_mutex_unlock(&engine->mutex);
	return result;
}

static UdbStorageResult udb_storage_engine_submit_credit(UdbStorageEngine *engine, uint32_t kind, uint64_t owner,
														 uint64_t module_revision, uint64_t authority_revision,
														 const void *bytes, size_t length, size_t credit, uint64_t *id)
{
	UdbStorageJob *job;
	size_t charge;
	if (!engine || !id || !kind || !owner || (length && !bytes) || !engine->mutex.initialized ||
		length > UDB_STORAGE_QUEUE_LIMIT - sizeof(UdbStorageJob))
		return UDB_STORAGE_ERROR;
	charge = udb_storage_engine_job_charge(length);
	if (!charge || (credit && credit != charge))
		return UDB_STORAGE_LIMIT;
	udb_storage_mutex_lock(&engine->mutex);
	if (!engine->admission || engine->failed || engine->stopping || module_revision != engine->module_revision ||
		authority_revision != engine->authority_revision)
	{
		udb_storage_mutex_unlock(&engine->mutex);
		return UDB_STORAGE_BUSY;
	}
	if (engine->next_job == UINT64_MAX || engine->queue_bytes > UDB_STORAGE_QUEUE_LIMIT ||
		credit > engine->delivery_bytes || charge - credit > UDB_STORAGE_QUEUE_LIMIT - engine->queue_bytes)
	{
		udb_storage_mutex_unlock(&engine->mutex);
		return UDB_STORAGE_LIMIT;
	}
	job = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*job) + length);
	if (!job)
	{
		udb_storage_mutex_unlock(&engine->mutex);
		return UDB_STORAGE_LIMIT;
	}
	job->id = engine->next_job++;
	job->engine = engine;
	job->owner = owner;
	job->kind = kind;
	job->module_revision = module_revision;
	job->authority_revision = authority_revision;
	job->length = length;
	job->charge = charge;
	if (length)
		memcpy(job->payload, bytes, length);
	if (engine->pending_tail)
		engine->pending_tail->next = job;
	else
		engine->pending = job;
	engine->pending_tail = job;
	engine->delivery_bytes -= credit;
	engine->queue_bytes += charge - credit;
	engine->pending_bytes += charge;
	*id = job->id;
	udb_storage_engine_wake(engine);
	udb_storage_mutex_unlock(&engine->mutex);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_engine_submit(UdbStorageEngine *engine, uint32_t kind, uint64_t owner,
												  uint64_t module_revision, uint64_t authority_revision,
												  const void *bytes, size_t length, uint64_t *id)
{
	return udb_storage_engine_submit_credit(engine, kind, owner, module_revision, authority_revision, bytes, length, 0,
											id);
}

static UdbStorageResult udb_storage_engine_poll(UdbStorageEngine *engine, UdbStorageJob **job)
{
	if (!engine || !job || !engine->mutex.initialized)
		return UDB_STORAGE_ERROR;
	*job = NULL;
	udb_storage_mutex_lock(&engine->mutex);
	if (engine->completed)
	{
		*job = engine->completed;
		engine->completed = (*job)->next;
		(*job)->next = NULL;
		(*job)->detached = 1;
		if (!engine->completed)
			engine->completed_tail = NULL;
	}
	udb_storage_mutex_unlock(&engine->mutex);
	return *job ? UDB_STORAGE_OK : UDB_STORAGE_NOT_FOUND;
}

static UdbStorageResult udb_storage_engine_job_free(UdbStorageEngine *engine, UdbStorageJob **pointer)
{
	UdbStorageJob *job;
	size_t charge;
	if (!engine || !pointer || !*pointer || !engine->mutex.initialized)
		return UDB_STORAGE_ERROR;
	job = *pointer;
	if (job->engine != engine || !job->detached || job->next)
		return UDB_STORAGE_ERROR;
	charge = job->charge;
	udb_storage_mutex_lock(&engine->mutex);
	if (!charge || engine->queue_bytes < charge)
	{
		udb_storage_mutex_unlock(&engine->mutex);
		return UDB_STORAGE_ERROR;
	}
	if (job->before)
		udb_storage_engine_generation_drop(engine, job->before);
	if (job->after)
		udb_storage_engine_generation_drop(engine, job->after);
	udb_storage_free(job);
	engine->queue_bytes -= charge;
	*pointer = NULL;
	udb_storage_mutex_unlock(&engine->mutex);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_engine_authority(UdbStorageEngine *engine, uint64_t *revision)
{
	if (!engine || !revision || !engine->mutex.initialized)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&engine->mutex);
	if (engine->authority_revision == UINT64_MAX)
	{
		engine->admission = 0;
		engine->failed = 1;
		udb_storage_mutex_unlock(&engine->mutex);
		return UDB_STORAGE_LIMIT;
	}
	*revision = ++engine->authority_revision;
	udb_storage_engine_wake(engine);
	udb_storage_mutex_unlock(&engine->mutex);
	return UDB_STORAGE_OK;
}

static void udb_storage_engine_cancel_owner(UdbStorageEngine *engine, uint64_t owner)
{
	if (!engine || !owner || !engine->mutex.initialized)
		return;
	udb_storage_mutex_lock(&engine->mutex);
	for (UdbStorageJob *job = engine->pending; job; job = job->next)
		if (job->owner == owner && !job->committing)
			job->cancelled = 1;
	for (UdbStorageJob *job = engine->active; job; job = job->next)
		if (job->owner == owner && !job->committing)
			job->cancelled = 1;
	udb_storage_engine_wake(engine);
	udb_storage_mutex_unlock(&engine->mutex);
}

static UdbStorageResult udb_storage_engine_stop(UdbStorageEngine *engine, int drain)
{
	if (!engine || !engine->budget.initialized)
		return UDB_STORAGE_ERROR;
	if (!engine->thread_started)
		return UDB_STORAGE_OK;
	udb_storage_mutex_lock(&engine->mutex);
	engine->admission = 0;
	engine->stopping = 1;
	if (!drain)
	{
		for (UdbStorageJob *job = engine->pending; job; job = job->next)
			job->cancelled = 1;
		for (UdbStorageJob *job = engine->active; job; job = job->next)
			if (!job->committing)
				job->cancelled = 1;
	}
	udb_storage_engine_wake(engine);
	udb_storage_mutex_unlock(&engine->mutex);
#ifdef _WIN32
	if (WaitForSingleObject(engine->thread, INFINITE) != WAIT_OBJECT_0 || !CloseHandle(engine->thread))
		return UDB_STORAGE_ERROR;
	engine->thread = NULL;
#else
	if (pthread_join(engine->thread, NULL))
		return UDB_STORAGE_ERROR;
#endif
	engine->thread_started = 0;
	if (engine->stack_charged)
	{
		udb_storage_budget_uncharge(&engine->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_WRITER_STACK);
		engine->stack_charged = 0;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_engine_close(UdbStorageEngine *engine)
{
	UdbStorageResult result;
	UdbStorageJob *job;
	if (!engine || !engine->budget.initialized)
		return UDB_STORAGE_ERROR;
	result = udb_storage_engine_stop(engine, 0);
	if (result != UDB_STORAGE_OK)
		return result;
	if (engine->cleanup)
	{
		result = engine->cleanup(engine);
		if (result != UDB_STORAGE_OK)
			return result;
		engine->cleanup = NULL;
	}
	if (engine->mutex.initialized)
	{
		while (udb_storage_engine_poll(engine, &job) == UDB_STORAGE_OK)
			udb_storage_engine_job_free(engine, &job);
		/* A polled job not yet returned belongs to the caller. */
		if (engine->queue_bytes)
			return UDB_STORAGE_BUSY;
		udb_storage_mutex_lock(&engine->mutex);
		for (UdbStorageGeneration *generation = engine->generations; generation; generation = generation->next)
			if (generation != engine->current || generation->references != 1)
			{
				udb_storage_mutex_unlock(&engine->mutex);
				return UDB_STORAGE_BUSY;
			}
		if (engine->current)
		{
			udb_storage_engine_generation_drop(engine, engine->current);
			engine->current = NULL;
		}
		udb_storage_mutex_unlock(&engine->mutex);
	}
	result = udb_storage_reclaimer_close(engine);
	if (result != UDB_STORAGE_OK)
		return result;
	for (unsigned int i = 0; i < 9; i++)
		if (engine->files[i].opened)
		{
			result = udb_storage_file_close(engine->files + i);
			if (result != UDB_STORAGE_OK)
				return result;
		}
	if (engine->directory.opened)
	{
		result = udb_storage_directory_close(&engine->directory);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (engine->cache)
	{
		result = udb_storage_cache_destroy(&engine->cache);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (engine->condition_initialized)
	{
#ifndef _WIN32
		if (pthread_cond_destroy(&engine->condition))
			return UDB_STORAGE_ERROR;
#endif
		engine->condition_initialized = 0;
	}
	udb_storage_mutex_destroy(&engine->mutex);
	if (engine->stack_charged)
	{
		udb_storage_budget_uncharge(&engine->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_WRITER_STACK);
		engine->stack_charged = 0;
	}
	if (engine->charged)
	{
		udb_storage_budget_uncharge(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*engine) - sizeof(engine->budget));
		engine->charged = 0;
	}
	result = udb_storage_budget_destroy(&engine->budget);
	if (result == UDB_STORAGE_OK)
		memset(engine, 0, sizeof(*engine));
	return result;
}
