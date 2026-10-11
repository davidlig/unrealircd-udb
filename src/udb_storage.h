/*
 * UDB 4 native storage. All persisted integers have explicit little-endian
 * encoding. These types are process-local and must never be written verbatim.
 *
 * Storage callers own serialization and lifetime: initialize the budget before
 * any allocation, release page pins before closing files, and quiesce callers
 * before destroying the cache, directory or budget. Budget, directory and file
 * objects must be zero-initialized before their first initialization/open.
 * Open/close/create/destroy are owner-serialized lifecycle operations.
 */
#ifndef UDB_STORAGE_H
#define UDB_STORAGE_H

#include <errno.h>
#include <limits.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
/* Keep windows.h from pulling winsock.h in before UnrealIRCd's winsock2.h. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sys/types.h>
#include <time.h>
#endif

#define UDB_STORAGE_PAGE_SIZE ((size_t)65536)
#define UDB_STORAGE_PAGE_HEADER ((size_t)96)
#define UDB_STORAGE_PAGE_PAYLOAD (UDB_STORAGE_PAGE_SIZE - UDB_STORAGE_PAGE_HEADER)
#define UDB_STORAGE_FRAGMENT_MAX ((size_t)4 * 1024 * 1024)
#define UDB_STORAGE_LEAF_WEIGHT_MAX ((uint64_t)16 * 1024 * 1024)
#define UDB_STORAGE_BASE_PARTITIONS 4096
#define UDB_STORAGE_CACHE_DEFAULT ((size_t)128 * 1024 * 1024)
#define UDB_STORAGE_AUX_LIMIT ((size_t)64 * 1024 * 1024)
#define UDB_STORAGE_MEMTABLE_LIMIT ((size_t)16 * 1024 * 1024)
#define UDB_STORAGE_QUEUE_LIMIT ((size_t)8 * 1024 * 1024)
#define UDB_STORAGE_MAX_RUNS 8
#define UDB_STORAGE_COMPACTION_MAX ((size_t)64 * 1024 * 1024)
#define UDB_STORAGE_FORMAT 1
#define UDB_STORAGE_COMPONENTS_MAX 8
#define UDB_STORAGE_COMPONENT_MAX 4608
#define UDB_STORAGE_PATH_MAX 8192
#define UDB_STORAGE_VALUE_MAX 4096
#define UDB_STORAGE_ROW_MAX (16 + 4 * UDB_STORAGE_COMPONENTS_MAX + UDB_STORAGE_PATH_MAX + UDB_STORAGE_VALUE_MAX)
#define UDB_STORAGE_KEY_MAX (16 + 4 * UDB_STORAGE_COMPONENTS_MAX + UDB_STORAGE_PATH_MAX)
#define UDB_STORAGE_RECORD_HEADER 32
#define UDB_STORAGE_RECORD_MAX (UDB_STORAGE_ROW_MAX + UDB_STORAGE_RECORD_HEADER)
#define UDB_STORAGE_DATA_ROWS_MAX ((UDB_STORAGE_PAGE_PAYLOAD - 12) / 57)
/* Even maximal separator keys leave room for three index levels in 64 pages. */
#define UDB_STORAGE_FRAGMENT_DATA_PAGES_MAX 53
#define UDB_STORAGE_FRAGMENT_DESCRIPTOR_SIZE 80
#define UDB_STORAGE_IO_PATH_MAX 1024
#define UDB_STORAGE_CACHE_BUCKETS 1024
#define UDB_STORAGE_CATALOG_KEY_SIZE 64
#define UDB_STORAGE_CATALOG_VALUE_SIZE 128
#define UDB_STORAGE_CATALOG_HEIGHT_MAX 8
#define UDB_STORAGE_CATALOG_ROOT_SIZE 128
#define UDB_STORAGE_BLOCK_ROOT_SIZE 384
#define UDB_STORAGE_STATE_SIZE (128 + 6 * UDB_STORAGE_BLOCK_ROOT_SIZE + UDB_STORAGE_CATALOG_ROOT_SIZE)
#define UDB_STORAGE_METADATA_SIZE ((size_t)4 * 1024 * 1024)

typedef enum UdbStorageResult
{
	UDB_STORAGE_ERROR = -1,
	UDB_STORAGE_LIMIT = -2,
	UDB_STORAGE_BUSY = -3,
	UDB_STORAGE_CORRUPT = -4,
	UDB_STORAGE_REJECTED = -5,
	UDB_STORAGE_NOT_FOUND = 0,
	UDB_STORAGE_OK = 1,
	/* Writer-internal results; never returned by typed data queries. */
	UDB_STORAGE_CONTINUE = 2,
	UDB_STORAGE_STAGED = 3
} UdbStorageResult;

typedef enum UdbStorageValueKind
{
	UDB_STORAGE_VALUE_NONE = 0,
	UDB_STORAGE_VALUE_STRING = 1,
	UDB_STORAGE_VALUE_NUMBER = 2
} UdbStorageValueKind;

typedef enum UdbStoragePageKind
{
	UDB_STORAGE_PAGE_DATA = 1,
	UDB_STORAGE_PAGE_INDEX = 2,
	UDB_STORAGE_PAGE_CATALOG = 3,
	UDB_STORAGE_PAGE_METADATA = 4
} UdbStoragePageKind;

typedef struct UdbStorageSpan
{
	const unsigned char *data;
	uint32_t length;
} UdbStorageSpan;

/* Decoded rows borrow their input buffer. No pointer is NUL-terminated. */
typedef struct UdbStorageRow
{
	unsigned char block;
	unsigned char depth;
	UdbStorageValueKind kind;
	UdbStorageSpan components[UDB_STORAGE_COMPONENTS_MAX];
	UdbStorageSpan value;
	uint64_t number;
} UdbStorageRow;

typedef enum UdbStorageOperation
{
	UDB_STORAGE_PUT = 1,
	UDB_STORAGE_DELETE = 2,
	UDB_STORAGE_PREFIX_DELETE = 3
} UdbStorageOperation;

typedef enum UdbStorageStream
{
	UDB_STORAGE_POINTS = 1,
	UDB_STORAGE_PREFIXES = 2
} UdbStorageStream;

typedef struct UdbStorageStamp
{
	uint64_t sequence;
	uint32_t ordinal;
} UdbStorageStamp;

typedef struct UdbStorageRecord
{
	UdbStorageRow row;
	UdbStorageStamp stamp;
	UdbStorageOperation operation;
} UdbStorageRecord;

typedef UdbStorageResult (*UdbStorageRecordValidator)(const UdbStorageRecord *record, void *context);

typedef struct UdbStoragePage
{
	UdbStoragePageKind kind;
	uint64_t generation;
	uint64_t object;
	uint64_t ordinal;
	UdbStorageSpan payload;
	uint32_t items;
	uint32_t level;
	uint64_t records;
} UdbStoragePage;

typedef struct UdbStorageFragment
{
	uint64_t start;
	uint64_t length;
	uint64_t root;
	uint64_t generation;
	uint64_t object;
	uint64_t count;
	uint32_t data_pages;
	uint32_t root_level;
	unsigned char block;
	UdbStorageStream stream;
} UdbStorageFragment;

typedef struct UdbStorageFragmentBuilder UdbStorageFragmentBuilder;
typedef struct UdbStorageFragmentIterator UdbStorageFragmentIterator;

/* Radix prefixes use the most significant bits of the canonical key hash. */
typedef struct UdbStorageLogicalPosition
{
	unsigned char block;
	uint16_t partition;
	uint16_t depth;
	unsigned char prefix[32];
} UdbStorageLogicalPosition;

typedef struct UdbStorageLogicalSummary
{
	unsigned char digest[32];
	uint64_t count;
	uint64_t weight;
} UdbStorageLogicalSummary;

typedef struct UdbStorageLeafHasher UdbStorageLeafHasher;

/* Fixed-size catalog records hold logical summaries and physical references.
 * Large lists remain separate paged records, not variable resident values. */
typedef struct UdbStorageCatalogRoot
{
	uint64_t offset;
	uint64_t generation;
	uint64_t object;
	uint64_t count;
	uint32_t height;
	unsigned char maximum[UDB_STORAGE_CATALOG_KEY_SIZE];
} UdbStorageCatalogRoot;

/* Copied immutable block root; the engine owns its generation lifetime. */
typedef struct UdbStorageBlockRoot
{
	UdbStorageCatalogRoot catalog;
	UdbStorageLogicalSummary summary;
	uint64_t generation;
	uint64_t watermark;
	uint64_t prefixes[UDB_STORAGE_MAX_RUNS + 1];
	unsigned int prefix_count;
	unsigned char block;
} UdbStorageBlockRoot;

typedef struct UdbStorageState
{
	UdbStorageBlockRoot blocks[6];
	/* Pending roots in the existing S container; never active block content. */
	UdbStorageCatalogRoot stages;
	uint64_t generation;
	uint64_t sequence;
	uint64_t next_object;
	uint64_t source_epoch;
	uint64_t source_sequence;
	unsigned char store_identity[16];
	char source_sid[4];
} UdbStorageState;

typedef struct UdbStorageStageHead
{
	uint64_t owner;
	uint64_t source_epoch;
	uint64_t source_sequence;
	uint64_t round;
	char source_sid[4];
	unsigned int mask;
} UdbStorageStageHead;

typedef struct UdbStorageCatalogIterator UdbStorageCatalogIterator;

/* All referenced roots/files must stay generation-pinned by the view owner.
 * The small run descriptor is copied; its fragment directory stays on disk. */
typedef struct UdbStorageRun
{
	struct UdbStorageFile *file;
	UdbStorageCatalogRoot directory;
	unsigned char block;
	UdbStorageStream stream;
} UdbStorageRun;

typedef struct UdbStorageRunIterator UdbStorageRunIterator;
typedef struct UdbStorageRunBuilder UdbStorageRunBuilder;
typedef struct UdbStorageView UdbStorageView;
typedef struct UdbStorageBlockIterator UdbStorageBlockIterator;
typedef struct UdbStorageRebuild UdbStorageRebuild;
typedef struct UdbStoragePointChange UdbStoragePointChange;
typedef struct UdbStoragePrefixChange UdbStoragePrefixChange;
/* Replayable sorted visible source. Engine keeps its input generations pinned.
 * open(position) may filter early; the controller independently checks placement.
 * close releases the handle even when reporting an I/O error. */
typedef struct UdbStorageSource
{
	UdbStorageResult (*open)(void *context, const UdbStorageLogicalPosition *position, void **handle);
	UdbStorageResult (*next)(void *handle, UdbStorageRecord *record);
	UdbStorageResult (*close)(void **handle);
	void *context;
} UdbStorageSource;

typedef enum UdbStorageNodeKind
{
	UDB_STORAGE_LEAF = 1,
	UDB_STORAGE_BRANCH = 2
} UdbStorageNodeKind;

typedef struct UdbStorageNode
{
	UdbStorageNodeKind kind;
	UdbStorageLogicalSummary summary;
	unsigned int run_count;
	uint64_t runs[UDB_STORAGE_MAX_RUNS + 1];
} UdbStorageNode;

typedef struct UdbStorageMutex
{
#ifdef _WIN32
	CRITICAL_SECTION native;
#else
	pthread_mutex_t native;
#endif
	int initialized;
} UdbStorageMutex;

typedef enum UdbStorageMemoryClass
{
	UDB_STORAGE_MEMORY_CACHE = 0,
	UDB_STORAGE_MEMORY_AUX = 1
} UdbStorageMemoryClass;

typedef struct UdbStorageCache UdbStorageCache;

typedef struct UdbStorageBudget
{
	UdbStorageMutex mutex;
	size_t limit[2];
	size_t used[2];
	size_t peak[2];
	uint64_t next_file_identity;
	size_t open_files;
	size_t memtable_bytes;
	UdbStorageCache *cache;
	int initialized;
} UdbStorageBudget;

typedef struct UdbStorageDirectory
{
#ifdef _WIN32
	HANDLE handle;
	wchar_t path[UDB_STORAGE_IO_PATH_MAX];
#else
	int handle;
#endif
	UdbStorageBudget *budget;
	unsigned char store_identity[16];
	size_t open_files;
	int opened;
} UdbStorageDirectory;

typedef struct UdbStorageFile
{
#ifdef _WIN32
	HANDLE handle;
#else
	int handle;
#endif
	uint64_t identity;
	/* Nonzero only during an owner-serialized bounded private append job. */
	uint64_t append_limit;
	uint64_t write_remaining;
	uint64_t allocated_bytes;
	void *free_map;
	UdbStorageDirectory *directory;
	UdbStorageCache *cache;
	struct UdbStorageFile *cache_next;
	int opened;
} UdbStorageFile;

typedef struct UdbStorageCacheEntry UdbStorageCacheEntry;

struct UdbStorageCacheEntry
{
	UdbStorageCacheEntry *hash_next;
	UdbStorageCacheEntry *older;
	UdbStorageCacheEntry *newer;
	uint64_t file_identity;
	uint64_t offset;
	uint64_t generation;
	unsigned int pins;
	int transient;
	UdbStoragePage page;
	unsigned char bytes[65536];
};

struct UdbStorageCache
{
	UdbStorageMutex mutex;
	UdbStorageBudget *budget;
	UdbStorageCacheEntry **buckets;
	UdbStorageCacheEntry *oldest;
	UdbStorageCacheEntry *newest;
	uint64_t hits;
	uint64_t misses;
	uint64_t evictions;
	size_t entries;
	UdbStorageFile *files;
	/* One validation owner; thread-scoped DATA bypass under cache mutex. */
#ifdef _WIN32
	DWORD validation_thread;
#else
	pthread_t validation_thread;
#endif
	int bypass_admission;
};

/* One owner-serialized journal. Errors with uncertain durability poison it;
 * no rollback/truncation retry is permitted until fresh recovery. */
typedef struct UdbStorageJournal
{
	UdbStorageCache *cache;
	UdbStorageFile *data[6];
	UdbStorageFile *wal;
	UdbStorageFile *metadata[2];
	UdbStorageState state;
	unsigned char last_hash[32];
	uint64_t end;
	unsigned int slot;
	int failed;
} UdbStorageJournal;

typedef struct UdbStorageEngine UdbStorageEngine;
typedef struct UdbStorageRawReader UdbStorageRawReader;
typedef struct UdbStorageRawWriter UdbStorageRawWriter;
typedef struct UdbStorageSort UdbStorageSort;
typedef struct UdbStorageRawRun
{
	uint64_t start;
	uint64_t length;
	uint64_t count;
	uint64_t generation;
	uint64_t object;
	unsigned char block;
} UdbStorageRawRun;
typedef struct UdbStorageJob UdbStorageJob;
typedef struct UdbStorageGeneration UdbStorageGeneration;
typedef UdbStorageResult (*UdbStoragePrepareJob)(UdbStorageEngine *engine, const UdbStorageJob *job,
												 const UdbStorageState *base, UdbStorageState *candidate);
typedef UdbStorageResult (*UdbStorageFinishJob)(UdbStorageEngine *engine, const UdbStorageJob *job);
typedef UdbStorageResult (*UdbStorageEngineCleanup)(UdbStorageEngine *engine);
/* Pure schema/profile validation; no daemon state or Client pointers. */
typedef UdbStorageResult (*UdbStorageStageValidator)(const UdbStorageSource *source, unsigned char block,
													 void *context);
typedef UdbStorageResult (*UdbStorageMutationNormalizer)(const UdbStorageRecord *record, UdbStorageRow *clear_prefix,
														 void *context);
typedef UdbStorageResult (*UdbStorageBlockValidator)(UdbStorageEngine *engine, const UdbStorageBlockRoot *root,
													 void *context);
enum
{
	UDB_STORAGE_JOB_POINT = 1,
	UDB_STORAGE_JOB_PREFIX,
	UDB_STORAGE_JOB_DROP,
	UDB_STORAGE_JOB_BIND,
	UDB_STORAGE_JOB_EXPIRE
};
/* Mutation payload: SID[4], epoch LE64, wire sequence LE64, block, local flag,
 * two zero reserved bytes, then canonical row for POINT/PREFIX.
 * EXPIRE includes expected expiry LE64 before its canonical K owner row. */
#define UDB_STORAGE_MUTATION_HEADER 24
typedef UdbStorageResult (*UdbStorageMutationValidator)(UdbStorageEngine *engine, const UdbStorageBlockRoot *candidate,
														const UdbStorageRow *changed, void *context);
enum
{
	UDB_STORAGE_STAGE_BEGIN = 10,
	UDB_STORAGE_STAGE_PUT,
	UDB_STORAGE_STAGE_END,
	UDB_STORAGE_STAGE_REUSE,
	UDB_STORAGE_STAGE_PROMOTE,
	UDB_STORAGE_STAGE_ABORT
};
/* Process-local copied command, never persisted verbatim. */
typedef struct UdbStorageStageSpec
{
	UdbStorageStageHead head;
	uint64_t count;
	uint64_t rows_limit;
	uint64_t physical_limit;
	unsigned char digest[32];
	unsigned char block;
} UdbStorageStageSpec;

struct UdbStorageGeneration
{
	UdbStorageGeneration *next;
	UdbStorageState state;
	size_t references;
};

/* Immutable input is copied on submit. Only internal scheduling/result fields
 * change. IDs/revisions, not Client pointers, bind lifetime and authorization. */
struct UdbStorageJob
{
	UdbStorageJob *next;
	UdbStorageEngine *engine;
	uint64_t id;
	uint64_t owner;
	uint64_t module_revision;
	uint64_t authority_revision;
	UdbStorageGeneration *before;
	UdbStorageGeneration *after;
	uint64_t committed_generation;
	uint64_t committed_storage_sequence;
	uint64_t committed_source_epoch;
	uint64_t committed_source_sequence;
	char committed_source_sid[4];
	uint32_t kind;
	size_t length;
	size_t charge;
	UdbStorageResult result;
	int cancelled;
	int committing;
	int detached;
	unsigned char payload[1];
};

struct UdbStorageEngine
{
	UdbStorageBudget budget;
	UdbStorageDirectory directory;
	UdbStorageFile files[9];
	UdbStorageCache *cache;
	UdbStorageJournal journal;
	UdbStorageMutex mutex;
#ifdef _WIN32
	CONDITION_VARIABLE condition;
	HANDLE thread;
#else
	pthread_cond_t condition;
	pthread_t thread;
#endif
	UdbStorageGeneration *generations;
	UdbStorageGeneration *current;
	UdbStorageJob *pending;
	UdbStorageJob *pending_tail;
	UdbStorageJob *active;
	UdbStorageJob *completed;
	UdbStorageJob *completed_tail;
	UdbStoragePrepareJob prepare;
	UdbStorageFinishJob finish;
	UdbStorageEngineCleanup cleanup;
	UdbStorageRecordValidator validator;
	void *validation_context;
	UdbStorageStageValidator stage_validator;
	UdbStorageMutationValidator mutation_validator;
	UdbStorageMutationNormalizer normalizer;
	UdbStorageBlockValidator block_validator;
	void *mutation_work;
	void *reclaimer;
	void *work;
	void *stages;
	size_t queue_bytes;
	size_t pending_bytes;
	size_t delivery_bytes;
	uint64_t next_job;
	uint64_t module_revision;
	uint64_t authority_revision;
	int charged;
	int condition_initialized;
	int stack_charged;
	int thread_started;
	int admission;
	int stopping;
	int failed;
};

static UdbStorageResult udb_storage_point_create(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageBlockRoot *root, uint64_t generation,
												 uint64_t next_object, const UdbStorageRecord *record,
												 UdbStoragePointChange **out);
static UdbStorageResult udb_storage_point_step(UdbStoragePointChange *change, UdbStorageBlockRoot *root,
											   uint64_t *next_object);
static UdbStorageResult udb_storage_point_close(UdbStoragePointChange **change);
static UdbStorageResult udb_storage_block_prefixes(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageBlockRoot *root,
												   UdbStorageRun runs[UDB_STORAGE_MAX_RUNS + 1]);
static UdbStorageResult udb_storage_block_points(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageBlockRoot *root, const UdbStorageNode *node,
												 UdbStorageRun runs[UDB_STORAGE_MAX_RUNS + 1]);
static UdbStorageResult udb_storage_block_partition_iterator(UdbStorageCache *cache, UdbStorageFile *file,
															 const UdbStorageBlockRoot *root, unsigned int partition,
															 UdbStorageBlockIterator **out);
static UdbStorageResult udb_storage_prefix_create(UdbStorageCache *cache, UdbStorageFile *file,
												  const UdbStorageBlockRoot *root, uint64_t generation,
												  uint64_t next_object, const UdbStorageRecord *record,
												  UdbStoragePrefixChange **out);
static UdbStorageResult udb_storage_prefix_step(UdbStoragePrefixChange *change, UdbStorageBlockRoot *root,
												uint64_t *next_object);
static UdbStorageResult udb_storage_prefix_close(UdbStoragePrefixChange **change);
static int udb_storage_stage_block_index(unsigned char block);
static UdbStorageResult udb_storage_journal_empty_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
															void *context);
static UdbStorageResult udb_storage_jobs_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
												 const UdbStorageState *base, UdbStorageState *candidate);
static UdbStorageResult udb_storage_jobs_finish(UdbStorageEngine *engine, const UdbStorageJob *job);
static UdbStorageResult udb_storage_jobs_cleanup(UdbStorageEngine *engine);
static void udb_storage_file_job_limit(UdbStorageFile *file, uint64_t limit);
static UdbStorageResult udb_storage_file_reserve(UdbStorageFile *file, unsigned int pages, uint64_t *offset);
static UdbStorageResult udb_storage_file_reserve_fragment(UdbStorageFile *file, unsigned int *pages, uint64_t *offset);
static UdbStorageResult udb_storage_reclaimer_refresh(UdbStorageEngine *engine);
static UdbStorageResult udb_storage_reclaimer_close(UdbStorageEngine *engine);
static UdbStorageResult udb_storage_catalog_pin(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *root, UdbStorageCacheEntry **pin);
static void udb_storage_catalog_child_ref(const unsigned char *entry, const UdbStorageCatalogRoot *parent,
										  UdbStorageCatalogRoot *child);
static UdbStorageResult udb_storage_run_descriptor(const UdbStorageRun *run, const unsigned char value[128],
												   UdbStorageFragment *fragment);

typedef struct UdbStorageRead
{
	UdbStorageEngine *engine;
	UdbStorageGeneration *generation;
	UdbStorageResult failed;
} UdbStorageRead;

typedef struct UdbStorageField
{
	UdbStorageValueKind kind;
	uint64_t number;
	uint32_t length;
	int present;
	char string[UDB_STORAGE_VALUE_MAX + 1];
} UdbStorageField;

#define UDB_STORAGE_PROFILE_FIELDS 9
typedef struct UdbStorageProfile
{
	UdbStorageRow key;
	unsigned char components[UDB_STORAGE_KEY_MAX];
	char entity[UDB_STORAGE_COMPONENT_MAX + 1];
	UdbStorageField fields[UDB_STORAGE_PROFILE_FIELDS];
	unsigned int count;
} UdbStorageProfile;

static UdbStorageResult udb_storage_read_empty(UdbStorageEngine *engine, UdbStorageRead **out);
static UdbStorageResult udb_storage_read_open(UdbStorageEngine *engine, UdbStorageRead **out);
static UdbStorageResult udb_storage_read_job(const UdbStorageJob *job, int after, UdbStorageRead **out);
static UdbStorageResult udb_storage_read_close(UdbStorageRead **read);
static UdbStorageResult udb_storage_read_field(UdbStorageRead *read, const UdbStorageRow *key, UdbStorageField *field);
static UdbStorageResult udb_storage_read_exists(UdbStorageRead *read, const UdbStorageRow *prefix);
static UdbStorageResult udb_storage_read_iterator(UdbStorageRead *read, unsigned char block,
												  const UdbStorageRow *prefix, UdbStorageBlockIterator **iterator);
static UdbStorageResult udb_storage_profile_load(UdbStorageRead *read, const UdbStorageRow *entity,
												 const char *const *names, unsigned int count,
												 UdbStorageProfile **profile);
static void udb_storage_profile_free(UdbStorageProfile **profile);
static UdbStorageResult udb_storage_block_object_bound(UdbStorageCache *cache, UdbStorageFile *file,
													   const UdbStorageBlockRoot *root, uint64_t next_object);
static uint16_t udb_storage_get_u16(const unsigned char *p);
static int udb_storage_zero(const unsigned char *bytes, size_t length);
static int udb_storage_sid_valid(const char sid[4]);
static const unsigned char udb_storage_blocks[6];
static size_t udb_storage_allocation_charge(size_t bytes);
static UdbStorageResult udb_storage_memtable_charge(UdbStorageBudget *budget, size_t bytes);
static void udb_storage_memtable_uncharge(UdbStorageBudget *budget, size_t bytes);
static UdbStorageResult udb_storage_file_remove_private(UdbStorageDirectory *directory, const char *name);
static UdbStorageResult udb_storage_raw_encode(const UdbStorageRawRun *run, unsigned char bytes[128]);
static UdbStorageResult udb_storage_raw_decode(const unsigned char bytes[128], UdbStorageRawRun *run);
static UdbStorageResult udb_storage_raw_reader_create(UdbStorageFile *file, const UdbStorageRawRun *run,
													  UdbStorageRawReader **out);
static UdbStorageResult udb_storage_raw_reader_next(UdbStorageRawReader *reader, UdbStorageRecord *record);
static void udb_storage_raw_reader_close(UdbStorageRawReader **reader);
static UdbStorageResult udb_storage_raw_reader_seek(UdbStorageRawReader *reader, uint64_t offset, unsigned int slot,
													uint64_t count);
static UdbStorageResult udb_storage_raw_reader_position(UdbStorageRawReader *reader, uint64_t *offset,
														unsigned int *slot, uint64_t *count);
static UdbStorageResult udb_storage_raw_writer_create(UdbStorageFile *file, unsigned char block, uint64_t generation,
													  uint64_t object, UdbStorageRawWriter **out);
static UdbStorageResult udb_storage_raw_writer_add(UdbStorageRawWriter *writer, const unsigned char *bytes,
												   size_t length);
static UdbStorageResult udb_storage_raw_writer_finish(UdbStorageRawWriter *writer, UdbStorageRawRun *run);
static void udb_storage_raw_writer_close(UdbStorageRawWriter **writer);
static UdbStorageResult udb_storage_raw_partition(const UdbStorageRecord *record, unsigned int *partition);
static UdbStorageResult udb_storage_sort_create(UdbStorageDirectory *directory, unsigned char block,
												uint64_t rows_limit, uint64_t physical_limit,
												UdbStorageRecordValidator validator, void *context,
												UdbStorageSort **out);
static UdbStorageResult udb_storage_sort_add(UdbStorageSort *sort, const unsigned char *bytes, size_t length);
static UdbStorageResult udb_storage_sort_seal(UdbStorageSort *sort);
/* Each BUSY step is a distinct <=64 MiB private output job. */
static UdbStorageResult udb_storage_sort_step(UdbStorageSort *sort);
static UdbStorageResult udb_storage_sort_close(UdbStorageSort **sort);
static UdbStorageResult udb_storage_sort_source(UdbStorageSort *sort, uint64_t sequence, uint32_t ordinal_base,
												UdbStorageSource *source);
static UdbStorageResult udb_storage_engine_open(UdbStorageEngine *engine, const char *parent, size_t cache_bytes,
												UdbStoragePrepareJob prepare, UdbStorageFinishJob finish,
												UdbStorageEngineCleanup cleanup, UdbStorageRecordValidator validator,
												UdbStorageStageValidator stage_validator,
												UdbStorageMutationValidator mutation_validator,
												UdbStorageMutationNormalizer normalizer,
												UdbStorageBlockValidator block_validator, void *context);
static UdbStorageResult udb_storage_engine_stop(UdbStorageEngine *engine, int drain);
static UdbStorageResult udb_storage_engine_close(UdbStorageEngine *engine);
static UdbStorageResult udb_storage_engine_pin(UdbStorageEngine *engine, UdbStorageGeneration **generation);
static UdbStorageResult udb_storage_engine_release(UdbStorageEngine *engine, UdbStorageGeneration **generation);
static size_t udb_storage_engine_job_charge(size_t length);
static UdbStorageResult udb_storage_engine_delivery_reserve(UdbStorageEngine *engine, uint64_t module_revision,
															uint64_t authority_revision, size_t charge);
static UdbStorageResult udb_storage_engine_delivery_release(UdbStorageEngine *engine, size_t charge);
static UdbStorageResult udb_storage_engine_submit_credit(UdbStorageEngine *engine, uint32_t kind, uint64_t owner,
														 uint64_t module_revision, uint64_t authority_revision,
														 const void *bytes, size_t length, size_t credit, uint64_t *id);
static UdbStorageResult udb_storage_engine_submit(UdbStorageEngine *engine, uint32_t kind, uint64_t owner,
												  uint64_t module_revision, uint64_t authority_revision,
												  const void *bytes, size_t length, uint64_t *id);
static UdbStorageResult udb_storage_engine_poll(UdbStorageEngine *engine, UdbStorageJob **job);
static UdbStorageResult udb_storage_engine_job_free(UdbStorageEngine *engine, UdbStorageJob **job);
static UdbStorageResult udb_storage_engine_authority(UdbStorageEngine *engine, uint64_t *revision);
static void udb_storage_engine_cancel_owner(UdbStorageEngine *engine, uint64_t owner);
static UdbStorageResult udb_storage_journal_initialize(UdbStorageJournal *journal);
static UdbStorageResult udb_storage_journal_recover(UdbStorageJournal *journal, UdbStorageRecordValidator validator,
													void *context);
static UdbStorageResult udb_storage_journal_commit(UdbStorageJournal *journal, const UdbStorageState *candidate);
static UdbStorageResult udb_storage_journal_checkpoint(UdbStorageJournal *journal);
static UdbStorageResult udb_storage_stages_validate(UdbStorageJournal *journal, UdbStorageRecordValidator validator,
													void *context);
static UdbStorageResult udb_storage_stage_save(UdbStorageEngine *engine, const UdbStorageState *base,
											   const UdbStorageStageHead *head, const UdbStorageBlockRoot *root,
											   UdbStorageState *candidate);
static UdbStorageResult udb_storage_stage_drop(UdbStorageEngine *engine, const UdbStorageState *base, uint64_t owner,
											   UdbStorageState *candidate);
static UdbStorageResult udb_storage_stage_promote(UdbStorageEngine *engine, const UdbStorageState *base, uint64_t owner,
												  UdbStorageState *candidate);
static uint32_t udb_storage_get_u32(const unsigned char *p);
static uint64_t udb_storage_get_u64(const unsigned char *p);
static void udb_storage_put_u16(unsigned char *p, uint16_t n);
static void udb_storage_put_u32(unsigned char *p, uint32_t n);
static void udb_storage_put_u64(unsigned char *p, uint64_t n);
/* Implementation include order is normalized by clang-format. Every helper
 * shared across units needs a declaration, even when its definition comes first
 * in an unformatted checkout. */
static int udb_storage_overlaps(const void *a, size_t an, const void *b, size_t bn);
static int udb_storage_block_valid(unsigned char block);
static int udb_storage_row_valid(const UdbStorageRow *row);
static int udb_storage_io_range_valid(uint64_t offset, size_t length);
static UdbStorageResult udb_storage_row_encode(const UdbStorageRow *row, unsigned char *out, size_t capacity,
											   size_t *length);
static UdbStorageResult udb_storage_row_decode(const unsigned char *bytes, size_t length, UdbStorageRow *row);
static int udb_storage_stamp_compare(UdbStorageStamp a, UdbStorageStamp b);
static UdbStorageResult udb_storage_record_encode(const UdbStorageRecord *record, unsigned char *bytes, size_t capacity,
												  size_t *length);
static UdbStorageResult udb_storage_record_decode(const unsigned char *bytes, size_t length, UdbStorageRecord *record);
static int udb_storage_record_matches_stream(const UdbStorageRecord *record, UdbStorageStream stream);
static int udb_storage_key_compare(const UdbStorageRow *a, const UdbStorageRow *b);
static int udb_storage_key_has_prefix(const UdbStorageRow *row, const UdbStorageRow *prefix);
static UdbStorageResult udb_storage_key_hash(const UdbStorageRow *row, unsigned char hash[32], unsigned int *partition);
static UdbStorageResult udb_storage_page_encode(const UdbStoragePage *page, unsigned char *out, size_t capacity);
static UdbStorageResult udb_storage_page_decode(const unsigned char *bytes, size_t length, UdbStoragePage *page);
static int udb_storage_fragment_valid(const UdbStorageFragment *fragment);
static UdbStorageResult udb_storage_fragment_encode(const UdbStorageFragment *fragment, unsigned char *out,
													size_t capacity);
static UdbStorageResult udb_storage_fragment_decode(const unsigned char *bytes, size_t length,
													UdbStorageFragment *fragment);
static UdbStorageResult udb_storage_fragment_builder_create(UdbStorageFile *file, uint64_t generation, uint64_t object,
															UdbStorageFragmentBuilder **out);
static UdbStorageResult udb_storage_fragment_builder_add(UdbStorageFragmentBuilder *builder,
														 const unsigned char *record, size_t length);
static UdbStorageResult udb_storage_fragment_builder_finish(UdbStorageFragmentBuilder *builder,
															UdbStorageFragment *fragment);
static void udb_storage_fragment_builder_free(UdbStorageFragmentBuilder **builder);
static UdbStorageResult udb_storage_fragment_get(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageFragment *fragment, const UdbStorageRow *key,
												 UdbStorageRecord *record, UdbStorageCacheEntry **pin);
static UdbStorageResult udb_storage_fragment_maximum(UdbStorageCache *cache, UdbStorageFile *file,
													 const UdbStorageFragment *fragment, UdbStorageRow *key,
													 UdbStorageCacheEntry **pin);
static UdbStorageResult udb_storage_fragment_validate(UdbStorageFile *file, const UdbStorageFragment *fragment,
													  UdbStorageRecordValidator validator, void *context);
static UdbStorageResult udb_storage_fragment_iterator_create(UdbStorageCache *cache, UdbStorageFile *file,
															 const UdbStorageFragment *fragment,
															 const UdbStorageRow *prefix,
															 UdbStorageFragmentIterator **out);
/* Returned row spans stay valid only until next() or close(). */
static UdbStorageResult udb_storage_fragment_iterator_next(UdbStorageFragmentIterator *iterator,
														   UdbStorageRecord *record);
static UdbStorageResult udb_storage_fragment_iterator_close(UdbStorageFragmentIterator **iterator);

static int udb_storage_logical_position_valid(const UdbStorageLogicalPosition *position);
static int udb_storage_logical_summary_valid(const UdbStorageLogicalSummary *summary);
static int udb_storage_logical_contains(const UdbStorageLogicalPosition *position, const unsigned char hash[32],
										unsigned int partition);
static UdbStorageResult udb_storage_logical_child(const UdbStorageLogicalPosition *parent, unsigned int right,
												  UdbStorageLogicalPosition *child);
static UdbStorageResult udb_storage_leaf_hasher_create(UdbStorageBudget *budget,
													   const UdbStorageLogicalPosition *position,
													   UdbStorageLeafHasher **out);
static UdbStorageResult udb_storage_leaf_hasher_add(UdbStorageLeafHasher *hasher, const unsigned char *encoded_row,
													size_t length);
static UdbStorageResult udb_storage_leaf_hasher_finish(UdbStorageLeafHasher *hasher, UdbStorageLogicalSummary *summary);
static void udb_storage_leaf_hasher_free(UdbStorageLeafHasher **hasher);
static UdbStorageResult udb_storage_logical_empty(const UdbStorageLogicalPosition *position,
												  UdbStorageLogicalSummary *summary);
static UdbStorageResult udb_storage_logical_branch(const UdbStorageLogicalPosition *position,
												   const UdbStorageLogicalSummary *left,
												   const UdbStorageLogicalSummary *right,
												   UdbStorageLogicalSummary *summary);
typedef UdbStorageResult (*UdbStoragePartitionReader)(unsigned int partition, UdbStorageLogicalSummary *summary,
													  void *context);
/* NOT_FOUND means an implicit canonical empty base partition, never an I/O error. */
static UdbStorageResult udb_storage_logical_block(unsigned char block, UdbStoragePartitionReader reader, void *context,
												  UdbStorageLogicalSummary *summary);

static UdbStorageResult udb_storage_catalog_page_decode(UdbStoragePage *page);
static int udb_storage_catalog_root_valid(const UdbStorageCatalogRoot *root);
static UdbStorageResult udb_storage_catalog_root_encode(const UdbStorageCatalogRoot *root, unsigned char *bytes,
														size_t capacity);
static UdbStorageResult udb_storage_catalog_root_decode(const unsigned char *bytes, size_t length,
														UdbStorageCatalogRoot *root);
static UdbStorageResult udb_storage_catalog_get(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *root,
												const unsigned char key[UDB_STORAGE_CATALOG_KEY_SIZE],
												unsigned char value[UDB_STORAGE_CATALOG_VALUE_SIZE]);
/* Owner-serialized private append only: no flush, publication or reclamation.
 * NULL value deletes one catalog record; out remains untouched on failure. */
static UdbStorageResult udb_storage_catalog_change(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageCatalogRoot *root, uint64_t generation,
												   const unsigned char key[UDB_STORAGE_CATALOG_KEY_SIZE],
												   const unsigned char value[UDB_STORAGE_CATALOG_VALUE_SIZE],
												   UdbStorageCatalogRoot *out);
/* Validation callbacks are read-only; they must not mutate referenced storage. */
typedef UdbStorageResult (*UdbStorageCatalogValidator)(const unsigned char *key, const unsigned char *value,
													   void *context);
static UdbStorageResult udb_storage_catalog_validate(UdbStorageFile *file, const UdbStorageCatalogRoot *root,
													 UdbStorageCatalogValidator validator, void *context);
static UdbStorageResult udb_storage_catalog_iterator_create(UdbStorageCache *cache, UdbStorageFile *file,
															const UdbStorageCatalogRoot *root,
															const unsigned char *lower, const unsigned char *upper,
															UdbStorageCatalogIterator **out);
static UdbStorageResult udb_storage_catalog_iterator_next(UdbStorageCatalogIterator *iterator,
														  unsigned char key[UDB_STORAGE_CATALOG_KEY_SIZE],
														  unsigned char value[UDB_STORAGE_CATALOG_VALUE_SIZE]);
static UdbStorageResult udb_storage_catalog_iterator_close(UdbStorageCatalogIterator **iterator);

static UdbStorageResult udb_storage_run_get(UdbStorageCache *cache, const UdbStorageRun *run, const UdbStorageRow *key,
											UdbStorageRecord *record, UdbStorageCacheEntry **pin);
static UdbStorageResult udb_storage_run_builder_create(UdbStorageCache *cache, UdbStorageFile *file,
													   unsigned char block, UdbStorageStream stream,
													   uint64_t generation, uint64_t object,
													   UdbStorageRunBuilder **out);
/* LIMIT leaves the incoming record unconsumed; output remains private. */
static UdbStorageResult udb_storage_run_builder_add(UdbStorageRunBuilder *builder, const unsigned char *record,
													size_t length);
static UdbStorageResult udb_storage_run_builder_finish(UdbStorageRunBuilder *builder, UdbStorageRun *run);
static void udb_storage_run_builder_free(UdbStorageRunBuilder **builder);
static UdbStorageResult udb_storage_run_validate(UdbStorageFile *file, const UdbStorageRun *run, uint64_t watermark,
												 UdbStorageRecordValidator validator, void *context);
static UdbStorageResult udb_storage_run_iterator_create(UdbStorageCache *cache, const UdbStorageRun *run,
														const UdbStorageRow *prefix, UdbStorageRunIterator **out);
static UdbStorageResult udb_storage_run_iterator_next(UdbStorageRunIterator *iterator, UdbStorageRecord *record);
static UdbStorageResult udb_storage_run_iterator_close(UdbStorageRunIterator **iterator);
static UdbStorageResult udb_storage_view_create(UdbStorageCache *cache, const UdbStorageRun *points, size_t point_count,
												const UdbStorageRun *prefixes, size_t prefix_count,
												const UdbStorageRow *prefix, uint64_t watermark, UdbStorageView **out);
/* Visible row spans stay valid only until the next call or view close. */
static UdbStorageResult udb_storage_view_next(UdbStorageView *view, UdbStorageRow *row);
static UdbStorageResult udb_storage_view_next_record(UdbStorageView *view, UdbStorageRecord *record);
static UdbStorageResult udb_storage_view_create_indexed(UdbStorageCache *cache, const UdbStorageRun *points,
														size_t point_count, const UdbStorageRun *prefixes,
														size_t prefix_count, const UdbStorageRow *prefix,
														uint64_t watermark, UdbStorageView **out);
static UdbStorageResult udb_storage_view_close(UdbStorageView **view);
/* Callers own (and budget) the result buffer; decoded spans borrow that buffer. */
static UdbStorageResult udb_storage_lookup(UdbStorageCache *cache, const UdbStorageRun *points, size_t point_count,
										   const UdbStorageRun *prefixes, size_t prefix_count, const UdbStorageRow *key,
										   uint64_t watermark, unsigned char *bytes, size_t capacity,
										   UdbStorageRow *row);
static UdbStorageResult udb_storage_exists_prefix(UdbStorageCache *cache, const UdbStorageRun *points,
												  size_t point_count, const UdbStorageRun *prefixes,
												  size_t prefix_count, const UdbStorageRow *prefix, uint64_t watermark);

static UdbStorageResult udb_storage_node_get(UdbStorageCache *cache, UdbStorageFile *file,
											 const UdbStorageCatalogRoot *catalog,
											 const UdbStorageLogicalPosition *position, UdbStorageNode *node);
static UdbStorageResult udb_storage_node_change(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *catalog, uint64_t generation,
												const UdbStorageLogicalPosition *position, const UdbStorageNode *node,
												UdbStorageCatalogRoot *out);
static UdbStorageResult udb_storage_node_locate(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *catalog, const UdbStorageRow *key,
												UdbStorageLogicalPosition *position, UdbStorageNode *node);
static UdbStorageResult udb_storage_run_save(UdbStorageCache *cache, const UdbStorageRun *run,
											 const UdbStorageCatalogRoot *catalog, uint64_t generation,
											 UdbStorageCatalogRoot *out);
static UdbStorageResult udb_storage_run_load(UdbStorageCache *cache, UdbStorageFile *file,
											 const UdbStorageCatalogRoot *catalog, unsigned char block,
											 UdbStorageStream stream, uint64_t id, UdbStorageRun *run);
static UdbStorageResult udb_storage_block_validate(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageBlockRoot *root, UdbStorageRecordValidator validator,
												   void *context);
static int udb_storage_block_root_valid(const UdbStorageBlockRoot *root);
static int udb_storage_state_valid(const UdbStorageState *state);
static UdbStorageResult udb_storage_block_root_encode(const UdbStorageBlockRoot *root,
													  unsigned char bytes[UDB_STORAGE_BLOCK_ROOT_SIZE]);
static UdbStorageResult udb_storage_block_root_decode(const unsigned char bytes[UDB_STORAGE_BLOCK_ROOT_SIZE],
													  UdbStorageBlockRoot *root);
static UdbStorageResult udb_storage_state_encode(const UdbStorageState *state,
												 unsigned char bytes[UDB_STORAGE_STATE_SIZE]);
static UdbStorageResult udb_storage_state_decode(const unsigned char bytes[UDB_STORAGE_STATE_SIZE],
												 UdbStorageState *state);
static UdbStorageResult udb_storage_rebuild_create(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageBlockRoot *root,
												   const UdbStorageLogicalPosition *position, uint64_t generation,
												   uint64_t watermark, uint64_t next_object,
												   const UdbStorageSource *source, UdbStorageRebuild **out);
/* BUSY means private progress remains. No returned candidate is durable. */
static UdbStorageResult udb_storage_rebuild_step(UdbStorageRebuild *rebuild, UdbStorageBlockRoot *out,
												 uint64_t *next_object);
static UdbStorageResult udb_storage_rebuild_close(UdbStorageRebuild **rebuild);
static UdbStorageResult udb_storage_rebuild_defer(UdbStorageRebuild *rebuild);
static UdbStorageResult udb_storage_rebuild_allowance(UdbStorageRebuild *rebuild, uint64_t bytes);
static uint64_t udb_storage_sort_rows(const UdbStorageSort *sort);
static UdbStorageResult udb_storage_sort_size(UdbStorageSort *sort, uint64_t *bytes);
static UdbStorageResult udb_storage_sort_partition_count(UdbStorageSort *sort, unsigned int partition, uint64_t *count);
static UdbStorageResult udb_storage_stage_jobs_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
													   const UdbStorageState *base, UdbStorageState *candidate);
static UdbStorageResult udb_storage_stage_jobs_finish(UdbStorageEngine *engine, const UdbStorageJob *job);
static UdbStorageResult udb_storage_stage_jobs_cleanup(UdbStorageEngine *engine);

static UdbStorageResult udb_storage_rebuild_summary(UdbStorageRebuild *rebuild, UdbStorageLogicalSummary *summary);

static UdbStorageResult udb_storage_node_position_decode(const unsigned char key[64],
														 UdbStorageLogicalPosition *position);
static void udb_storage_node_key(const UdbStorageLogicalPosition *position, unsigned char key[64]);
static UdbStorageResult udb_storage_node_decode(const UdbStorageLogicalPosition *position,
												const unsigned char value[128], UdbStorageNode *node);
static void udb_storage_run_manifest_key(unsigned char block, UdbStorageStream stream, uint64_t id,
										 unsigned char key[64]);
static void udb_storage_run_key(uint64_t ordinal, unsigned char key[64]);
static UdbStorageResult udb_storage_run_copy_key(const UdbStorageRow *row, unsigned char *out, size_t *length);
static int udb_storage_summary_equal(const UdbStorageLogicalSummary *a, const UdbStorageLogicalSummary *b);
static UdbStorageResult udb_storage_block_lookup(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageBlockRoot *root, const UdbStorageRow *key,
												 unsigned char *bytes, size_t capacity, UdbStorageRow *row);
static UdbStorageResult udb_storage_block_iterator_create(UdbStorageCache *cache, UdbStorageFile *file,
														  const UdbStorageBlockRoot *root, const UdbStorageRow *prefix,
														  UdbStorageBlockIterator **out);
static UdbStorageResult udb_storage_block_iterator_next(UdbStorageBlockIterator *iterator, UdbStorageRecord *record);
static UdbStorageResult udb_storage_block_iterator_close(UdbStorageBlockIterator **iterator);
static UdbStorageResult udb_storage_block_exists_prefix(UdbStorageCache *cache, UdbStorageFile *file,
														const UdbStorageBlockRoot *root, const UdbStorageRow *prefix);

static int udb_storage_mutex_init(UdbStorageMutex *mutex);
static void udb_storage_mutex_lock(UdbStorageMutex *mutex);
static void udb_storage_mutex_unlock(UdbStorageMutex *mutex);
static void udb_storage_mutex_destroy(UdbStorageMutex *mutex);
static UdbStorageResult udb_storage_directory_open(UdbStorageDirectory *directory, UdbStorageBudget *budget,
												   const char *parent);
static UdbStorageResult udb_storage_directory_close(UdbStorageDirectory *directory);
static UdbStorageResult udb_storage_private_header(UdbStorageFile *file, const char *name);
static UdbStorageResult udb_storage_orphans_cleanup(UdbStorageDirectory *directory);
static UdbStorageResult udb_storage_file_open(UdbStorageDirectory *directory, const char *name, int create,
											  UdbStorageFile *file);
static UdbStorageResult udb_storage_file_close(UdbStorageFile *file);
static UdbStorageResult udb_storage_file_read(UdbStorageFile *file, uint64_t offset, void *bytes, size_t length);
static UdbStorageResult udb_storage_file_write(UdbStorageFile *file, uint64_t offset, const void *bytes, size_t length);
static UdbStorageResult udb_storage_file_size(UdbStorageFile *file, uint64_t *size);
static UdbStorageResult udb_storage_file_resize(UdbStorageFile *file, uint64_t size);
static UdbStorageResult udb_storage_file_flush(UdbStorageFile *file);

static UdbStorageResult udb_storage_budget_init(UdbStorageBudget *budget, size_t cache_limit);
static UdbStorageResult udb_storage_budget_destroy(UdbStorageBudget *budget);
static UdbStorageResult udb_storage_budget_charge(UdbStorageBudget *budget, UdbStorageMemoryClass memory_class,
												  size_t bytes);
static void udb_storage_budget_uncharge(UdbStorageBudget *budget, UdbStorageMemoryClass memory_class, size_t bytes);
static void *udb_storage_alloc(UdbStorageBudget *budget, UdbStorageMemoryClass memory_class, size_t bytes);
static void udb_storage_free(void *allocation);
static UdbStorageResult udb_storage_cache_create(UdbStorageBudget *budget, UdbStorageCache **out);
static UdbStorageResult udb_storage_cache_destroy(UdbStorageCache **cache);
static UdbStorageResult udb_storage_cache_resize(UdbStorageCache *cache, size_t bytes);
static UdbStorageResult udb_storage_cache_acquire(UdbStorageCache *cache, UdbStorageFile *file, uint64_t offset,
												  uint64_t generation, UdbStorageCacheEntry **out);
static UdbStorageResult udb_storage_cache_release(UdbStorageCache *cache, UdbStorageCacheEntry *entry);
static UdbStorageResult udb_storage_cache_forget_file(UdbStorageCache *cache, uint64_t identity);
static UdbStorageResult udb_storage_cache_forget_range(UdbStorageCache *cache, uint64_t identity, uint64_t offset,
													   uint64_t length);
static UdbStorageResult udb_storage_cache_detach_file(UdbStorageFile *file);
static void udb_storage_cache_attach_file(UdbStorageCache *cache, UdbStorageFile *file);
static UdbStorageResult udb_storage_cache_forget_range_locked(UdbStorageCache *cache, uint64_t identity,
															  uint64_t offset, uint64_t length);

#endif
