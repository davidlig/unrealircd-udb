/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Standalone Bundled Distribution
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

/*** <<<MODULE MANAGER START>>>
module
{
	documentation "https://github.com/davidlig/unrealircd-udb";
	troubleshooting "In case of problems, report issues at https://github.com/davidlig/unrealircd-udb/issues";
	min-unrealircd-version "6.2.*";
	post-install-text {
		"The UDB module is now installed. Next steps:";
		"1. Add the following to your unrealircd.conf:";
		"   loadmodule \"third/udb\";";
		"   udb {";
		"       propagator \"ares-services.yourdomain.net\";";
		"   };";
		"2. Verify configuration with: ./unrealircd configtest";
		"3. Restart UnrealIRCd: ./unrealircd restart";
	}
}
*** <<<MODULE MANAGER END>>>
*/

/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Internal Module Interfaces & Subsystem State
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

#ifndef UDB_INTERNAL_H
#define UDB_INTERNAL_H

/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Public Module Header & Constants
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

#ifndef UDB_H
#define UDB_H

#define UDB_VERSION "4.0.0-rc1"
#define UDB_PROTOCOL_HEL "4"

/* Block Identifiers */
#define UDB_BLOCK_NICKS 'N'
#define UDB_BLOCK_CHANNELS 'C'
#define UDB_BLOCK_IPS 'I'
#define UDB_BLOCK_SETTINGS 'S'
#define UDB_BLOCK_LINKS 'L'
#define UDB_BLOCK_LINES 'K'

#define UDB_NUM_BLOCKS 6

/* Sub-record Keys */

/* Nick sub-records: N::<nick>::<key> <value> */
#define NKEY_ACCESS "access"	 /* IP/CIDR access restriction */
#define NKEY_PASS "pass"		 /* Password hash */
#define NKEY_VHOST "vhost"		 /* Virtual host */
#define NKEY_FORBID "forbid"	 /* Forbidden nick (value = reason) */
#define NKEY_SUSPEND "suspend"	 /* Suspended nick (value = reason) */
#define NKEY_OPER "oper"		 /* Operclass name string (e.g. "locop", "netadmin-with-override") */
#define NKEY_MODES "modes"		 /* Profile user modes (never +o) */
#define NKEY_SNOMASKS "snomasks" /* Desired snomask expression */
#define NKEY_SWHOIS "swhois"	 /* Custom SWHOIS line */

/* Channel sub-records: C::<#chan>::<key> <value> */
#define CKEY_FOUNDER "founder" /* Founder nick */
#define CKEY_MODES "modes"	   /* Locked channel modes */
#define CKEY_TOPIC "topic"	   /* Persistent topic */
#define CKEY_ACCESS "access"   /* Access list (has sub-records per nick) */
#define CKEY_FORBID "forbid"   /* Forbidden channel (value = reason) */
#define CKEY_SUSPEND "suspend" /* Suspended channel (value = reason) */
#define CKEY_OPTIONS "options" /* Channel option flags (*N) */

/* IP sub-records: I::<ip|host>::<key> <value> */
#define IKEY_CLONES "clones"   /* Max clones allowed (*N) */
#define IKEY_NOLINES "nolines" /* Ban exception types (eg. GZQSTmc) */
#define IKEY_HOST "host"	   /* Visible host overlay (never changes realhost) */

/* Settings sub-records: S::<key> <value> */
#define SKEY_CRYPT_KEY "encryption_key" /* Host cloaking key */
#define SKEY_SUFFIX "suffix"			/* Virtual host suffix */
#define SKEY_NICKSERV "nickserv"		/* NickServ bot mask */
#define SKEY_CHANSERV "chanserv"		/* ChanServ bot mask */
#define SKEY_IPSERV "ipserv"			/* IpServ bot mask */
#define SKEY_CLONES "clones"			/* Global max clones (*N) */
#define SKEY_QUIT_CLONES "quit_clones"	/* Quit message for clone limit */
#define SKEY_FLOOD "flood"				/* Password flood limit V:S */
#define SKEY_PROPAGATOR "propagator"	/* Cluster authoritative propagator(s) */

/* Link sub-records: L::<server>::<key> <value> */
#define LKEY_OPTIONS "options" /* Link option flags (*N) */

/* Line sub-records: K::<type>::<pattern>::<key> <value> */
#define KKEY_MATCH_TYPE "match-type" /* Spamfilter match method: regex|simple */
#define KKEY_TARGETS "targets"		 /* Canonical Spamfilter target letters */
#define KKEY_ACTION "action"		 /* Dynamic Spamfilter action */
#define KKEY_BAN_TIME "ban-time"	 /* Duration of sanction emitted on match */
#define KKEY_EXPIRES "expires"		 /* Absolute Unix expiry timestamp */
#define KKEY_REASON "reason"		 /* Ban reason */

/* Spamfilter pattern encoding: K::F::b64:<RFC 4648 base64>::... */
#define UDB_SPAMFILTER_B64_PREFIX "b64:"
#define UDB_SPAMFILTER_PATTERN_MAX 3072
#define UDB_TKL_SET_BY "UDB:managed"

/* Error Codes (for DB ERR protocol messages) */
#define UDB_ERR_NO_BLOCK 1	  /* Block does not exist */
#define UDB_ERR_PARAMS 2	  /* Missing parameters */
#define UDB_ERR_FATAL 3		  /* Fatal / internal error */
#define UDB_ERR_SYNC_ACTIVE 4 /* Sync already in progress */
#define UDB_ERR_NO_SYNC 5	  /* No sync was requested */
#define UDB_ERR_FORBIDDEN 6	  /* Forbidden server */

/* SHA-256 is deliberately handled by UDB, not Auth_Check(). */
#define UDB_AUTHTYPE_SHA256 1001

/* Channel Option Flags (bitmask in C::<#chan>::options *<value>) */
#define UDB_CHOPT_PROTECT_BANS 0x1 /* Only ban author can remove their bans */
#define UDB_CHOPT_LOCK_MODES 0x2   /* Channel modes are locked */
#define UDB_CHOPT_LOCK_TOPIC 0x4   /* Channel topic is locked */
#define UDB_CHOPT_PERSISTENT 0x8   /* Keep the channel alive through native +P */
#define UDB_CHOPT_OPER_ONLY 0x10   /* Restrict channel joins to IRC operators */
#define UDB_CHOPT_SECURE_OPS 0x20  /* Restrict member rank changes */

/* Link Option Flags (bitmask in L::<server>::options *<value>) */
#define UDB_LNKOPT_DEBUG 0x1 /* Debug: receives all UDB mode changes */

#endif /* UDB_H */


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
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h> /* SHA256() one-shot remains supported. */
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

#include "unrealircd.h"
#include <errno.h>
#include <inttypes.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdint.h>

#define UDB_BLOCK_PATH_MAX 1024
#define UDB_RECORD_PATH_MAX 8192
#define UDB_PATH_MAX_COMPONENTS 8
#define UDB_COMPONENT_RAW_MAX 4608
#define UDB_COMPONENT_ENCODED_MAX (3 * UDB_COMPONENT_RAW_MAX)
#define UDB_COMPONENT_MAX UDB_COMPONENT_ENCODED_MAX
#define UDB_RECORD_VALUE_MAX 4096
#define UDB_RECORD_LINE_MAX (UDB_RECORD_PATH_MAX + UDB_RECORD_VALUE_MAX + 32)
#define UDB_S2S_LINE_MAX MAXLINELENGTH
#define UDB_S2S_OVERHEAD_MAX 256
#define UDB_TXID_MAX 31
#define UDB_SYNC_INACTIVITY_TIMEOUT 60
#define UDB_SYNC_ABSOLUTE_TIMEOUT 3600
#define UDB_SYNC_TIMEOUT UDB_SYNC_INACTIVITY_TIMEOUT
#define UDB_RECONCILE_RETRY_MAX 6
#define UDB_RECONCILE_RETRY_BASE 2
#define UDB_DEFAULT_ANTI_ENTROPY_INTERVAL 1800
#define UDB_MIN_ANTI_ENTROPY_INTERVAL 1
#define UDB_MAX_ANTI_ENTROPY_INTERVAL 86400
#define UDB_ANTI_ENTROPY_TIMEOUT 10
#define UDB_ANTI_ENTROPY_MIN_GAP 5
#define UDB_DEFAULT_MAX_STAGED_BYTES ((size_t)16 * 1024 * 1024 * 1024)
#define UDB_MIN_MAX_STAGED_BYTES 1024
#define UDB_MAX_MAX_STAGED_BYTES ((size_t)64 * 1024 * 1024 * 1024)
#define UDB_DEFAULT_MAX_STAGED_RECORDS 10000000
#define UDB_MIN_MAX_STAGED_RECORDS 1
#define UDB_MAX_MAX_STAGED_RECORDS 10000000
#define UDB_HASH_MIN_BUCKETS 2048
#define UDB_HASH_LOAD_DENOMINATOR 4
#define UDB_PASSWORD_FAILURE_SLOTS 256
#define UDB_TKL_MASK_COMPONENT_MAX 127
#define UDB_LINE_EXPIRY_SWEEP_MAX 64
/* Operclass registry (OCL / OCLG) limits */
#define UDB_OCL_MAX_CLASSES 1024
#define UDB_OCL_STAGE_TIMEOUT 30
#define UDB_OCL_DIGEST_HEX_LEN 64
#define UDB_OCL_EPOCH_LEN 16
#define UDB_OCL_MAX_RETIRED_EPOCHS 8
#define UDB_OCL_MAX_PARENT_DEPTH 16
#define UDB_OCL_CANONICAL_MAX (256 * 1024)
#define UDB_OCL_ACL_DEPTH_MAX 64

/* Frozen daemon mode metadata, copied before native writer admission. */
typedef struct UdbNativeModeSchema
{
	unsigned short parameters;
	unsigned char known;
	unsigned char member;
	unsigned char unset_parameter;
} UdbNativeModeSchema;
typedef struct UdbNativeSchema
{
	UdbStorageBudget *budget;
	UdbNativeModeSchema channels[256];
	unsigned char users[256];
	unsigned char snomasks[256];
	int spamfilter_utf8;
} UdbNativeSchema;
static void udb_native_schema_capture(UdbNativeSchema *schema, UdbStorageBudget *budget);
static UdbStorageResult udb_native_row_validate(const UdbStorageRecord *record, void *context);
static UdbStorageResult udb_native_stage_validate(const UdbStorageSource *source, unsigned char block, void *context);
static UdbStorageResult udb_native_mutation_validate(UdbStorageEngine *engine, const UdbStorageBlockRoot *root,
													 const UdbStorageRow *changed, void *context);

static UdbStorageResult udb_native_mutation_normalize(const UdbStorageRecord *record, UdbStorageRow *prefix,
													  void *context);
static UdbStorageResult udb_native_block_profiles_validate(UdbStorageEngine *engine, const UdbStorageBlockRoot *root,
														   void *context);
typedef struct UdbBlock UdbBlock;
typedef struct UdbSyncSession UdbSyncSession;

typedef struct UdbPropagatorSelection
{
	Client *peer;
	char name[HOSTLEN + 1];
	int is_local;
} UdbPropagatorSelection;

typedef enum UdbValueType
{
	UDB_VAL_NONE = 0, /* No value allowed (container node) */
	UDB_VAL_STRING,	  /* Typed non-empty string */
	UDB_VAL_NUMERIC,  /* Typed unsigned numeric scalar */
	UDB_VAL_ANY		  /* Accepts both string and numeric */
} UdbValueType;

typedef int (*UdbValFunc)(const char *value);

typedef struct UdbKeyDescriptor
{
	const char *key;
	UdbValueType val_type;
	UdbValFunc validator;
	int allow_children;
	UdbValFunc child_validator;
} UdbKeyDescriptor;

typedef struct UdbBlockSchema
{
	char letter;
	int min_depth;
	int max_depth;
	UdbValFunc root_key_validator;
	const UdbKeyDescriptor *subkeys;
	size_t subkey_count;
} UdbBlockSchema;

typedef struct UdbBlockDescriptor
{
	char letter;
	const char *name;
	unsigned int mask;
	const UdbBlockSchema *schema;
} UdbBlockDescriptor;

typedef enum UdbBlockLoadState
{
	UDB_LOAD_UNINITIALIZED = 0,
	UDB_LOAD_SUCCESS,
	UDB_LOAD_EMPTY,
	UDB_LOAD_FAILED
} UdbBlockLoadState;

#define UDB_SHA256_HEX_LEN 64
#define UDB_EMPTY_SHA256 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

struct UdbBlock
{
	UdbBlock *next;
	char sha256[UDB_SHA256_HEX_LEN + 1];
	char *filepath;
	unsigned int id;
	uint64_t filesize;
	time_t modified_at;
	Client *syncing_from;
	UdbSyncSession *session;
	Client *pending_from;
	time_t pending_deadline;
	unsigned long pending_round_id;
	uint64_t generation;
	uint64_t record_count;
	char letter;
	unsigned int version;
	UdbBlockLoadState load_state;
};

/* Foreground receive session: copied identifiers only; disk staging is writer-owned. */
struct UdbSyncSession
{
	char peer_sid[4];
	char txid[UDB_TXID_MAX + 1];
	unsigned long round_id;
	uint64_t owner;
	uint64_t end_job;
	uint64_t received_puts;
	uint64_t watermark_seq;
	time_t started_at;
	time_t last_activity;
	time_t deadline;
	time_t absolute_deadline;
};

typedef struct UdbPasswordFailure
{
	char profile[CHANNELLEN + 1];
	char ip[INET6_ADDRSTRLEN];
	unsigned char block_idx;
	unsigned int attempts;
	time_t since;
} UdbPasswordFailure;

typedef struct UdbLineExpiryPending
{
	char type;
	char *pattern;
	time_t expires;
	int sent;
	struct UdbLineExpiryPending *next;
} UdbLineExpiryPending;

/* Operclass registry (OCL): one inventory snapshot per origin server plus an
 * optional staging area for an in-flight BEGIN/ITEM/END transaction. */
typedef struct UdbOclEntry
{
	char name[OPERCLASSLEN + 1];
	char digest[UDB_OCL_DIGEST_HEX_LEN + 1];
} UdbOclEntry;

typedef struct UdbOclInventory
{
	char epoch[UDB_OCL_EPOCH_LEN + 1];
	unsigned long generation;
	unsigned int count;
	char inventory_digest[UDB_OCL_DIGEST_HEX_LEN + 1];
	UdbOclEntry *entries;
} UdbOclInventory;

typedef struct UdbOclOrigin
{
	char sid[IDLEN + 1];
	int member;
	UdbOclInventory *current;
	UdbOclInventory *staging;
	int accepted_valid;
	char accepted_epoch[UDB_OCL_EPOCH_LEN + 1];
	unsigned long accepted_generation;
	unsigned int accepted_count;
	char accepted_digest[UDB_OCL_DIGEST_HEX_LEN + 1];
	char retired_epochs[UDB_OCL_MAX_RETIRED_EPOCHS][UDB_OCL_EPOCH_LEN + 1];
	unsigned int retired_count;
	time_t stage_deadline;
	unsigned int stage_received;
	struct UdbOclOrigin *next;
} UdbOclOrigin;

typedef enum UdbSyncStatus
{
	UDB_SYNC_OK = 0,
	UDB_SYNC_DEGRADED,
	UDB_SYNC_STALE
} UdbSyncStatus;

typedef struct UdbConfig
{
	char *propagator;
	int flood_attempts;
	int flood_period;
	int config_flood_attempts;
	int config_flood_period;
	unsigned int max_staged_records;
	size_t max_staged_bytes;
	size_t cache_size;
	int sync_inactivity_timeout;
	int sync_absolute_timeout;
	int stale_timeout;
	int anti_entropy_interval;
} UdbConfig;

typedef struct UdbContext
{
	UdbStorageEngine *storage;
	UdbNativeSchema native_schema;
	char selected_sid[4];
	char selected_epoch[17];
	int selection_kind;
	size_t native_charge;
	UdbBlock *blocks[256];
	UdbBlock *block_list;
	char *propagator_setting;
	char *quit_clones;
	char *encryption_key;
	char *suffix;
	char *nickserv_mask;
	char *chanserv_mask;
	char *ipserv_mask;
	int block_count;
	uint64_t current_seq;
	uint64_t last_applied_seq;
	char authority_sid[IDLEN + 1];
	char authority_epoch[UDB_OCL_EPOCH_LEN + 1];
	int authority_epoch_known;
	char candidate_authority_sid[IDLEN + 1];
	char candidate_authority_epoch[UDB_OCL_EPOCH_LEN + 1];
	int candidate_authority_epoch_known;
} UdbContext;

/* Caller-owned canonical keys; no record-tree adapters or shared scratch. */
typedef struct UdbNativeKey
{
	UdbStorageRow row;
	unsigned char bytes[UDB_STORAGE_KEY_MAX];
	unsigned char scratch[UDB_STORAGE_PATH_MAX];
	size_t used;
} UdbNativeKey;

static UdbStorageResult udb_native_key_parse(const char *path, UdbNativeKey *key);
static UdbStorageResult udb_native_key_path(const UdbStorageRow *row, char *out, size_t capacity);
static UdbStorageResult udb_native_read_scalar(UdbStorageRead *read, unsigned char block, const char *entity,
											   const char *property, UdbStorageField *field);
static UdbStorageResult udb_native_profile_load(UdbContext *ctx, unsigned char block, const char *entity,
												const char *const *fields, unsigned int count, UdbStorageProfile **out);

static UdbContext *udb_ctx = NULL;
static UdbPasswordFailure udb_password_failures[UDB_PASSWORD_FAILURE_SLOTS];
static UdbSyncStatus udb_sync_status = UDB_SYNC_OK;
static time_t udb_degraded_since = 0;
static time_t udb_propagator_unavailable_since = 0;
static time_t udb_last_successful_sync = 0;
static int udb_ready = 0;
static Client *udb_bootstrap_peer = NULL;

#define UDB_BLOCK_MASK_N (1 << 0)
#define UDB_BLOCK_MASK_C (1 << 1)
#define UDB_BLOCK_MASK_I (1 << 2)
#define UDB_BLOCK_MASK_S (1 << 3)
#define UDB_BLOCK_MASK_L (1 << 4)
#define UDB_BLOCK_MASK_K (1 << 5)
#define UDB_ALL_BLOCKS_MASK                                                                                            \
	(UDB_BLOCK_MASK_N | UDB_BLOCK_MASK_C | UDB_BLOCK_MASK_I | UDB_BLOCK_MASK_S | UDB_BLOCK_MASK_L | UDB_BLOCK_MASK_K)

typedef struct UdbReconcileState
{
	Client *authority_peer;
	unsigned long round_id;
	unsigned long generation;
	int active;
	unsigned int compared_blocks;
	unsigned int divergent_blocks;
	unsigned int completed_blocks;
	unsigned int retry_count;
	time_t next_retry_at;
	time_t started_at;
	time_t last_activity;
	time_t deadline;
	time_t absolute_deadline;
	uint64_t watermark_seq;
	int watermark_known;
	char source_sid[4];
	char source_epoch[UDB_OCL_EPOCH_LEN + 1];
	int source_known;
} UdbReconcileState;

static UdbReconcileState udb_reconcile = {0};

typedef struct UdbAntiEntropyState
{
	unsigned long round_id;
	int pending;
	unsigned int acked_blocks;
	unsigned int divergent_blocks;
	time_t last_check;
	time_t last_success;
	time_t next_check_at;
	time_t deadline;
	unsigned int fail_count;
	uint64_t watermark_seq;
	int watermark_known;
} UdbAntiEntropyState;

static UdbAntiEntropyState udb_anti_entropy = {0};

/* Ordered durable effects defer policy notifications until the complete
 * derived settings set has been installed in foreground state. */
static int udb_policy_notify_deferred = 0;
static int udb_policy_notify_pending = 0;

static int udb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
static int udb_config_run(ConfigFile *cf, ConfigEntry *ce, int type);
static void udb_config_defaults(void);
static int udb_config_rehash(void);
static int udb_config_postconf(void);
static void udb_config_free(UdbContext *ctx);
static char *udb_block_filepath(char letter);
static int udb_module_test(ModuleInfo *modinfo);
static int udb_module_init(ModuleInfo *modinfo);
static int udb_module_load(ModuleInfo *modinfo);
static int udb_module_unload(ModuleInfo *modinfo);
static int udb_engine_init(void);
static void udb_engine_cleanup(UdbContext *ctx);
static void udb_engine_shutdown(void);
static UdbBlock *udb_block_create(UdbContext *ctx, char letter, const char *name);
static UdbBlock *udb_block_by_letter(UdbContext *ctx, char letter);
static int udb_path_encode_component(const char *raw, char *buf, size_t bufsz);
static int udb_path_decode_component(const char *encoded, char *buf, size_t bufsz);
typedef int (*UdbPathComponentFunc)(const char *decoded, unsigned int index, void *data);
static int udb_path_foreach(const char *path, unsigned int max_components, UdbPathComponentFunc fn, void *data);
static int udb_secret_key_is_reserved(const char *key);
static int udb_secret_key_matches(char block_letter, unsigned int depth, const char *key);
static int udb_path_value_is_secret(const char *path);
static int udb_strtoull_strict(const char *s, unsigned long long *out);
static int udb_parse_uint64_strict(const char *s, uint64_t *out);
static int udb_strtoul_strict(const char *s, unsigned long *out);
static int udb_parse_uint_strict(const char *s, unsigned int *out, unsigned int min_val, unsigned int max_val);
static int udb_parse_ulong_strict(const char *s, unsigned long *out, unsigned long min_val, unsigned long max_val);
static int udb_parse_size_strict(const char *s, size_t *out, size_t min_val, size_t max_val);
static unsigned long long udb_time_t_max_val(void);
static int udb_parse_time_t(const char *s, time_t *out);
static int udb_time_add(time_t base, unsigned long duration, time_t *result);
static int udb_timestamp_parse(const char *s, time_t *out);
static int udb_digest_parse(const char *input, char out_hex[UDB_SHA256_HEX_LEN + 1]);
static UdbLineExpiryPending *udb_line_expiry_pending_add(char type, const char *pattern, time_t expires);
static void udb_sync_session_free(UdbBlock *block);
static int udb_block_letter_to_index(char letter);
static const UdbBlockDescriptor *udb_block_descriptor(unsigned int index);
static int udb_block_descriptors_validate(void);

static int udb_sync_to_server(Client *server);
static int udb_native_receive_inf(Client *peer, unsigned long round, char block, const char *digest, uint64_t count,
								  const char *sid, const char *epoch, uint64_t watermark);
static int udb_native_receive_promote(UdbContext *ctx);
static void udb_native_receive_abort(unsigned long round);
static void udb_native_receive_cleanup(void);
static void udb_native_receive_tick_event(void *unused);
static uint64_t udb_native_owner_next(void);
static int udb_native_mutation_bind(Client *peer, unsigned long round, const char *sid, const char *epoch,
									uint64_t watermark);
static int udb_native_mutation_pending(unsigned long round);
static int udb_native_mutation_replay(unsigned long round);
static void udb_native_mutation_abort(unsigned long round);
static void udb_native_mutation_cleanup(void);
static int udb_native_deliveries_present(void);
static void udb_native_mutation_round_begin(Client *peer);
static void udb_native_mutation_complete(UdbStorageJob *job);
static void udb_native_receive_replay_pump(void);
static UdbStorageResult udb_native_runtime_publication(UdbStorageJob *job);
static UdbStorageResult udb_native_runtime_startup(UdbContext *ctx);
static int udb_native_local_bind(UdbContext *ctx);
static UdbStorageResult udb_channel_reconcile_entity(UdbStorageRead *before, UdbStorageRead *after, const char *name);
static UdbStorageResult udb_ip_reconcile_scope(UdbStorageRead *before, UdbStorageRead *after, const char *entity);
static UdbStorageResult udb_lines_reconcile_scope(UdbStorageRead *before, UdbStorageRead *after,
												  const UdbStorageRow *row);
static UdbStorageResult udb_native_receive_publication(UdbStorageJob *job, unsigned long round, Client *peer);
static void udb_native_digest_hex(const unsigned char digest[32], char out[65]);
static int udb_native_sync_offer(Client *server);
static int udb_native_sync_offer_round(Client *server, unsigned long requested, int manifest);
static int udb_native_sync_send(Client *server, char block, unsigned long round);
static int udb_native_sync_ack(Client *server, unsigned long round, char block, const char *txid, const char *digest,
							   uint64_t watermark);
static void udb_native_sync_cleanup(Client *server);
static void udb_native_sync_pump(void);
static void udb_native_sync_tick_event(void *unused);
static int udb_reconcile_accept_watermark(Client *peer, unsigned long round, uint64_t watermark);
static int udb_reconcile_accept_source(Client *peer, unsigned long round, const char *sid, const char *epoch,
									   uint64_t watermark);
static UdbStorageResult udb_native_value_encode(const UdbStorageRow *row, char *out, size_t capacity);
static UdbStorageResult udb_native_value_decode(const char *wire, UdbStorageRow *row, unsigned char *value,
												size_t capacity);

static int udb_has_hello(Client *server);
static int udb_has_staged_sync(Client *server);
static int udb_peer_authorizes_us(Client *server);
static int udb_sync_hello_start(Client *server);
static void udb_sync_hello_ack(Client *server);
static int udb_hello_epoch_matches(Client *server, const char *epoch);
static int udb_hello_peer_advertisement(Client *server, const char *propagator, const char *epoch, int oclg_subscribed);
static const char *udb_sync_hello_propagator(UdbPropagatorSelection *selected);
static void udb_sync_abort(UdbBlock *block, const char *reason);
static unsigned int udb_block_letter_to_mask(char letter);
static int udb_has_active_sessions(UdbContext *ctx);
static void udb_block_clear_pending(UdbBlock *block);
static int udb_has_pending_requests(UdbContext *ctx);
static void udb_reconcile_reset(void);
static void udb_reconcile_begin(Client *authority, unsigned long round_id);
static void udb_reconcile_abort(UdbContext *ctx, const char *reason, int schedule_retry);
static void udb_reconcile_start(Client *authority, unsigned long round_id);
static void udb_reconcile_record_res(Client *peer, unsigned long round_id, char letter);
static void udb_reconcile_record_end(Client *peer, char letter, unsigned long round_id);
static int udb_reconcile_check(UdbContext *ctx);
static int udb_is_authorized_sync_source(UdbContext *ctx, Client *direct_peer);
static int udb_sync_begin(UdbBlock *block, Client *peer, unsigned long round_id, const char *txid, const char *digest,
						  uint64_t watermark_seq);
static int udb_sync_put(UdbBlock *block, Client *peer, unsigned long round_id, const char *txid, const char *path,
						const char *data);
static int udb_sync_end(UdbContext *ctx, UdbBlock *block, Client *peer, unsigned long round_id, const char *txid,
						const char *sha256, uint64_t watermark_seq);
static void udb_sync_ack(Client *peer, const char *block);
static int udb_sync_send_stage(Client *server, UdbBlock *block, unsigned long round_id);
static void udb_sync_server_quit(Client *client);
static void udb_anti_entropy_schedule_next(time_t now);
static void udb_anti_entropy_timer_check(time_t now);
static void udb_anti_entropy_record_ack(Client *peer, unsigned long round_id, char letter, uint64_t remote_count,
										const char *remote_sha, uint64_t watermark_seq);
static void udb_anti_entropy_handle_err(Client *peer, unsigned long round_id, int errcode);
static void udb_anti_entropy_peer_quit(Client *client);
static int udb_send_db_to_one(Client *to, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int udb_is_propagator(UdbContext *ctx, Client *server);
static int udb_server_name_valid(const char *srv);
static int udb_propagator_list_valid(const char *value);
static const char *udb_propagator_policy(UdbContext *ctx);
static int udb_select_propagator(UdbContext *ctx, int require_hello, UdbPropagatorSelection *selected);
static int udb_propagator_policy_present(UdbContext *ctx);
static void udb_propagator_policy_changed(UdbContext *ctx);
static void udb_propagator_policy_flush(UdbContext *ctx);
static void udb_sync_hello_refresh_all(void);
static void udb_propagator_availability_refresh(void);
static void udb_sync_status_refresh(void);
static int udb_hook_readiness_pre_connect(Client *client);
static void udb_query_send_status(Client *client);
static int udb_sendto_confirmed_servers(Client *except, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void udb_protocol_mutation_error(Client *client, const char *subcmd, int error, char letter);
static void udb_mutation_ins(UdbContext *ctx, Client *client, Client *direct_peer, const char *target,
							 const char *epoch, uint64_t seq, const char *path, const char *data, int is_for_me,
							 int is_broadcast);
static void udb_mutation_del(UdbContext *ctx, Client *client, Client *direct_peer, const char *target,
							 const char *epoch, uint64_t seq, const char *path, int is_for_me, int is_broadcast);
static void udb_mutation_drp(UdbContext *ctx, Client *client, Client *direct_peer, const char *target,
							 const char *epoch, uint64_t seq, char letter, int is_for_me, int is_broadcast);
static void udb_mutation_exp(UdbContext *ctx, Client *client, Client *direct_peer, const char *target, const char *path,
							 time_t expected_expires, int is_for_me, int is_broadcast);
static int udb_mutation_expire_local(UdbContext *ctx, const char *path, time_t expected_expires);

typedef enum
{
	UDB_NICK_APPLY_ADOPT,
	UDB_NICK_APPLY_REFRESH,
	UDB_NICK_APPLY_UNSUSPEND
} UdbNickApplyReason;

static void udb_nick_apply(Client *client, UdbStorageProfile *nick_rec, UdbNickApplyReason reason);
static void udb_nick_identity_clear(Client *client);
static void udb_nick_pending_auth_clear(Client *client);
static void udb_nick_preserve_on_rehash(int preserve);
static void udb_nick_shutdown(void);
static UdbStorageResult udb_nick_reconcile_generations(Client *client, UdbStorageRead *before, UdbStorageRead *after);
static UdbStorageResult udb_native_nick_load(const char *nick, UdbStorageProfile **out);
static UdbStorageField *udb_nick_field(UdbStorageProfile *profile, const char *name);
static void udb_nick_reconcile_after_reload(void);
static void udb_nick_revoke_oper(Client *client);
static int udb_oper_owned(Client *client);
static void udb_nick_effects_revoke_oper_vhost(Client *client);
static void udb_nick_effects_apply_oper_vhost_default(Client *client);
static int udb_check_password(const char *pass, UdbStorageProfile *profile_rec, Client *client);
static int udb_nick_access_allowed(Client *client, UdbStorageProfile *nick_rec);
static void udb_nick_set_vhost(Client *client, const char *vhost);
static void udb_nick_remove_vhost(Client *client);
static int udb_nick_grant_oper(Client *client, UdbStorageProfile *nick_rec, UdbStorageField *oper_rec);
static int udb_channels_load(ModuleInfo *modinfo);
static UdbStorageResult udb_ip_reconcile_generations(UdbStorageRead *before, UdbStorageRead *after);
static void udb_ip_refresh_derived_hosts(void);
static void udb_ips_shutdown(void);
static UdbStorageResult udb_native_settings_apply(UdbContext *ctx, UdbStorageRead *read);
static int udb_native_link_debug(UdbStorageRead *read, const char *server);
static UdbStorageResult udb_lines_reconcile_generations(UdbStorageRead *before, UdbStorageRead *after);
static int udb_spamfilter_pattern(const char *stored, char *pattern, size_t patternsz);
static const char *udb_get_bot_nick(const char *service_key, int force_default);
static const char *udb_get_bot_mask(const char *service_key, int force_default);
static Client *udb_service_source(const char *service_key);
static void udb_send_service_notice(Client *target, const char *service_key, FORMAT_STRING(const char *pattern), ...)
	__attribute__((format(printf, 3, 4)));
static int udb_ip_reapply_vhost(Client *client);
static int udb_ip_safe_host(Client *client, const char *host);
/* Runtime dispatcher; concrete per-block effects stay in their own modules. */
static void udb_send_to_debugs(Client *source, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Operclass registry (OCL) and global operclass view (OCLG) */
static Client *udb_ocl_find_server(const char *sid);
static void udb_ocl_shutdown(void);
static int udb_ocl_local_rebuild(void);
static void udb_ocl_maybe_replay_to_peer(Client *server);
static void udb_ocl_peer_instance_changed(Client *server);
static const char *udb_ocl_epoch_value(void);
static void udb_ocl_membership_changed(void);
static void udb_ocl_origin_quit(Client *client);
static void udb_ocl_stage_timeout_check(time_t now);
static void udb_ocl_handle(Client *client, Client *direct_peer, int parc, const char *parv[], int is_broadcast);
static void udb_oclg_handle(Client *client, int is_for_me);
static int udb_ocl_registry_complete(void);
static int udb_oclg_push(Client *to);

static int udb_protocol_init(ModuleInfo *modinfo);
int udb_nicks_init(ModuleInfo *modinfo);
int udb_nicks_load(ModuleInfo *modinfo);
static UdbStorageResult udb_channel_reconcile_generations(UdbStorageRead *before, UdbStorageRead *after);
static void udb_channels_shutdown(void);
static void udb_channels_init(ModuleInfo *modinfo);
static int udb_ips_init(ModuleInfo *modinfo);
static void udb_lines_init(ModuleInfo *modinfo);
static void udb_lines_shutdown(void);
static void udb_lines_expiry_sweep(time_t now);
static void udb_lines_expiry_peer_disconnected(Client *client);
static void udb_query_init(ModuleInfo *modinfo);
static void udb_sync_snomask_filter(void);

static int udb_is_debug_enabled(void);

#define udb_log(level, event_id, client, msg, ...)                                                                     \
	unreal_log(level, "udb", event_id, client, "[UDB] " msg, ##__VA_ARGS__)
#define udb_strdup(dest, src) safe_strdup(dest, src)

#endif /* UDB_INTERNAL_H */


ModuleHeader MOD_HEADER = {"third/udb", UDB_VERSION,
						   "UDB 4 - Unreal Database System (nick/channel/IP registration & sync)",
						   "David Abuín Fontán ('davidlig')", "unrealircd-6"};

/* Implementation units share one translation unit. Shared declarations in
 * udb_internal.h allow static helpers to cross subsystem boundaries. */

/* Exclusive native binary persistence and bounded immutable queries. */
/* Inlined: udb_storage_cache.c.inc */
/*
 * UDB 4 native storage: charged allocations and a lazy, bounded LRU page cache.
 * Cache -> budget is the lock order. Budget operations never acquire the cache.
 * Metadata/open/close lifetimes are serialized by the owning engine; page pins
 * may be acquired/released concurrently while their file remains attached.
 */

/* Include allocation headers in the budget and preserve scalar alignment. */
typedef union UdbStorageAllocation
{
	struct
	{
		UdbStorageBudget *budget;
		size_t bytes;
		UdbStorageMemoryClass memory_class;
	} info;
	long double alignment;
	void *pointer_alignment;
	uint64_t integer_alignment;
} UdbStorageAllocation;

static size_t udb_storage_allocation_charge(size_t bytes)
{
	return bytes && bytes <= SIZE_MAX - sizeof(UdbStorageAllocation) ? bytes + sizeof(UdbStorageAllocation) : 0;
}

static UdbStorageResult udb_storage_memtable_charge(UdbStorageBudget *budget, size_t bytes)
{
	UdbStorageResult result = UDB_STORAGE_LIMIT;
	if (!budget || !budget->initialized || !bytes)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&budget->mutex);
	if (bytes <= UDB_STORAGE_MEMTABLE_LIMIT - budget->memtable_bytes)
	{
		budget->memtable_bytes += bytes;
		result = UDB_STORAGE_OK;
	}
	udb_storage_mutex_unlock(&budget->mutex);
	return result;
}

static void udb_storage_memtable_uncharge(UdbStorageBudget *budget, size_t bytes)
{
	udb_storage_mutex_lock(&budget->mutex);
	if (bytes > budget->memtable_bytes)
		abort();
	budget->memtable_bytes -= bytes;
	udb_storage_mutex_unlock(&budget->mutex);
}

static int udb_storage_cache_limit_valid(size_t bytes)
{
	return bytes >= UDB_STORAGE_PAGE_SIZE * 2 && bytes <= SIZE_MAX - UDB_STORAGE_AUX_LIMIT;
}

static UdbStorageResult udb_storage_budget_init(UdbStorageBudget *budget, size_t cache_limit)
{
	if (!budget || !udb_storage_cache_limit_valid(cache_limit))
		return UDB_STORAGE_ERROR;
	if (budget->initialized)
		return UDB_STORAGE_BUSY;
	memset(budget, 0, sizeof(*budget));
	if (!udb_storage_mutex_init(&budget->mutex))
		return UDB_STORAGE_ERROR;
	budget->limit[UDB_STORAGE_MEMORY_CACHE] = cache_limit;
	budget->limit[UDB_STORAGE_MEMORY_AUX] = UDB_STORAGE_AUX_LIMIT;
	budget->used[UDB_STORAGE_MEMORY_AUX] = sizeof(*budget);
	budget->peak[UDB_STORAGE_MEMORY_AUX] = sizeof(*budget);
	budget->next_file_identity = 1;
	budget->initialized = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_budget_destroy(UdbStorageBudget *budget)
{
	if (!budget || !budget->initialized)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&budget->mutex);
	if (budget->used[UDB_STORAGE_MEMORY_CACHE] || budget->used[UDB_STORAGE_MEMORY_AUX] != sizeof(*budget) ||
		budget->open_files || budget->cache || budget->memtable_bytes)
	{
		udb_storage_mutex_unlock(&budget->mutex);
		return UDB_STORAGE_BUSY;
	}
	udb_storage_mutex_unlock(&budget->mutex);
	udb_storage_mutex_destroy(&budget->mutex);
	memset(budget, 0, sizeof(*budget));
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_budget_charge(UdbStorageBudget *budget, UdbStorageMemoryClass memory_class,
												  size_t bytes)
{
	UdbStorageResult result = UDB_STORAGE_LIMIT;

	if (!budget || !budget->initialized || (unsigned int)memory_class > UDB_STORAGE_MEMORY_AUX)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&budget->mutex);
	if (bytes <= budget->limit[memory_class] - budget->used[memory_class])
	{
		budget->used[memory_class] += bytes;
		if (budget->used[memory_class] > budget->peak[memory_class])
			budget->peak[memory_class] = budget->used[memory_class];
		result = UDB_STORAGE_OK;
	}
	udb_storage_mutex_unlock(&budget->mutex);
	return result;
}

static void udb_storage_budget_uncharge(UdbStorageBudget *budget, UdbStorageMemoryClass memory_class, size_t bytes)
{
	udb_storage_mutex_lock(&budget->mutex);
	if (budget->used[memory_class] < bytes)
		abort();
	budget->used[memory_class] -= bytes;
	udb_storage_mutex_unlock(&budget->mutex);
}

static void *udb_storage_alloc(UdbStorageBudget *budget, UdbStorageMemoryClass memory_class, size_t bytes)
{
	UdbStorageAllocation *allocation;
	size_t total;

	if (!bytes || bytes > SIZE_MAX - sizeof(*allocation))
		return NULL;
	total = bytes + sizeof(*allocation);
	if (udb_storage_budget_charge(budget, memory_class, total) != UDB_STORAGE_OK)
		return NULL;
	allocation = malloc(total);
	if (!allocation)
	{
		udb_storage_budget_uncharge(budget, memory_class, total);
		return NULL;
	}
	allocation->info.budget = budget;
	allocation->info.bytes = total;
	allocation->info.memory_class = memory_class;
	memset(allocation + 1, 0, bytes);
	return allocation + 1;
}

static void udb_storage_free(void *pointer)
{
	UdbStorageAllocation *allocation;
	UdbStorageBudget *budget;
	UdbStorageMemoryClass memory_class;
	size_t bytes;

	if (!pointer)
		return;
	allocation = (UdbStorageAllocation *)pointer - 1;
	budget = allocation->info.budget;
	memory_class = allocation->info.memory_class;
	bytes = allocation->info.bytes;
	free(allocation);
	/* Release only after free, so another allocator cannot exceed the cap. */
	udb_storage_budget_uncharge(budget, memory_class, bytes);
}

static size_t udb_storage_cache_bucket(uint64_t identity, uint64_t offset, uint64_t generation)
{
	uint64_t hash = identity ^ (offset / UDB_STORAGE_PAGE_SIZE) ^ (generation * UINT64_C(0x9e3779b97f4a7c15));

	hash ^= hash >> 30;
	hash *= UINT64_C(0xbf58476d1ce4e5b9);
	hash ^= hash >> 27;
	hash *= UINT64_C(0x94d049bb133111eb);
	hash ^= hash >> 31;
	return (size_t)hash & (UDB_STORAGE_CACHE_BUCKETS - 1);
}

static void udb_storage_cache_unlink_lru(UdbStorageCache *cache, UdbStorageCacheEntry *entry)
{
	if (entry->older)
		entry->older->newer = entry->newer;
	else
		cache->oldest = entry->newer;
	if (entry->newer)
		entry->newer->older = entry->older;
	else
		cache->newest = entry->older;
	entry->older = NULL;
	entry->newer = NULL;
}

static void udb_storage_cache_make_newest(UdbStorageCache *cache, UdbStorageCacheEntry *entry)
{
	entry->older = cache->newest;
	entry->newer = NULL;
	if (cache->newest)
		cache->newest->newer = entry;
	else
		cache->oldest = entry;
	cache->newest = entry;
}

static void udb_storage_cache_erase(UdbStorageCache *cache, UdbStorageCacheEntry *entry)
{
	size_t bucket = udb_storage_cache_bucket(entry->file_identity, entry->offset, entry->generation);
	UdbStorageCacheEntry **link = &cache->buckets[bucket];

	while (*link && *link != entry)
		link = &(*link)->hash_next;
	if (!*link || entry->pins)
		abort();
	*link = entry->hash_next;
	udb_storage_cache_unlink_lru(cache, entry);
	cache->entries--;
	udb_storage_free(entry);
}

static int udb_storage_cache_evict_one(UdbStorageCache *cache)
{
	for (UdbStorageCacheEntry *entry = cache->oldest; entry; entry = entry->newer)
	{
		if (entry->pins)
			continue;
		udb_storage_cache_erase(cache, entry);
		if (cache->evictions != UINT64_MAX)
			cache->evictions++;
		return 1;
	}
	return 0;
}

static UdbStorageResult udb_storage_cache_create(UdbStorageBudget *budget, UdbStorageCache **out)
{
	UdbStorageCache *cache;

	if (!budget || !budget->initialized || !out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	/* Creation/destruction is owner-serialized before/after admission. */
	if (budget->cache || budget->open_files)
		return UDB_STORAGE_BUSY;
	cache = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, sizeof(*cache));
	if (!cache)
		return UDB_STORAGE_LIMIT;
	cache->budget = budget;
	if (!udb_storage_mutex_init(&cache->mutex))
	{
		udb_storage_free(cache);
		return UDB_STORAGE_ERROR;
	}
	cache->buckets =
		udb_storage_alloc(budget, UDB_STORAGE_MEMORY_CACHE, UDB_STORAGE_CACHE_BUCKETS * sizeof(*cache->buckets));
	if (!cache->buckets)
	{
		udb_storage_mutex_destroy(&cache->mutex);
		udb_storage_free(cache);
		return UDB_STORAGE_LIMIT;
	}
	budget->cache = cache;
	*out = cache;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_destroy(UdbStorageCache **pointer)
{
	UdbStorageCache *cache;

	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	cache = *pointer;
	udb_storage_mutex_lock(&cache->mutex);
	if (cache->files || cache->budget->open_files)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_BUSY;
	}
	for (UdbStorageCacheEntry *entry = cache->oldest; entry; entry = entry->newer)
	{
		if (entry->pins)
		{
			udb_storage_mutex_unlock(&cache->mutex);
			return UDB_STORAGE_BUSY;
		}
	}
	while (cache->oldest)
		udb_storage_cache_erase(cache, cache->oldest);
	udb_storage_mutex_unlock(&cache->mutex);
	udb_storage_mutex_destroy(&cache->mutex);
	cache->budget->cache = NULL;
	udb_storage_free(cache->buckets);
	udb_storage_free(cache);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_resize(UdbStorageCache *cache, size_t bytes)
{
	size_t used;

	if (!cache || !udb_storage_cache_limit_valid(bytes))
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&cache->mutex);
	for (;;)
	{
		udb_storage_mutex_lock(&cache->budget->mutex);
		used = cache->budget->used[UDB_STORAGE_MEMORY_CACHE];
		if (used <= bytes)
		{
			cache->budget->limit[UDB_STORAGE_MEMORY_CACHE] = bytes;
			udb_storage_mutex_unlock(&cache->budget->mutex);
			break;
		}
		udb_storage_mutex_unlock(&cache->budget->mutex);
		if (!udb_storage_cache_evict_one(cache))
		{
			udb_storage_mutex_unlock(&cache->mutex);
			return UDB_STORAGE_BUSY;
		}
	}
	udb_storage_mutex_unlock(&cache->mutex);
	return UDB_STORAGE_OK;
}

static void udb_storage_cache_attach_file(UdbStorageCache *cache, UdbStorageFile *file)
{
	udb_storage_mutex_lock(&cache->mutex);
	if (file->cache || file->directory->budget != cache->budget)
		abort();
	file->cache = cache;
	file->cache_next = cache->files;
	cache->files = file;
	udb_storage_mutex_unlock(&cache->mutex);
}

static UdbStorageResult udb_storage_cache_acquire(UdbStorageCache *cache, UdbStorageFile *file, uint64_t offset,
												  uint64_t generation, UdbStorageCacheEntry **out)
{
	UdbStorageCacheEntry *entry;
	UdbStoragePage page;
	UdbStorageResult result;
	int bypass;
	size_t bucket, available;
	const size_t charge = sizeof(UdbStorageCacheEntry) + sizeof(UdbStorageAllocation);

	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || !file->opened || file->cache != cache || !generation || offset % UDB_STORAGE_PAGE_SIZE ||
		!udb_storage_io_range_valid(offset, UDB_STORAGE_PAGE_SIZE))
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&cache->mutex);
#ifdef _WIN32
	bypass = cache->bypass_admission && cache->validation_thread == GetCurrentThreadId();
#else
	bypass = cache->bypass_admission && pthread_equal(cache->validation_thread, pthread_self());
#endif
	bucket = udb_storage_cache_bucket(file->identity, offset, generation);
	for (entry = cache->buckets[bucket]; entry; entry = entry->hash_next)
	{
		if (entry->transient || (bypass && entry->page.kind == UDB_STORAGE_PAGE_DATA))
			continue;
		if (entry->file_identity != file->identity || entry->offset != offset || entry->generation != generation)
			continue;
		if (entry->pins == UINT_MAX)
		{
			udb_storage_mutex_unlock(&cache->mutex);
			return UDB_STORAGE_LIMIT;
		}
		entry->pins++;
		udb_storage_cache_unlink_lru(cache, entry);
		udb_storage_cache_make_newest(cache, entry);
		if (cache->hits != UINT64_MAX)
			cache->hits++;
		*out = entry;
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_OK;
	}
	if (cache->misses != UINT64_MAX)
		cache->misses++;
	while (!bypass)
	{
		udb_storage_mutex_lock(&cache->budget->mutex);
		available = cache->budget->limit[UDB_STORAGE_MEMORY_CACHE] - cache->budget->used[UDB_STORAGE_MEMORY_CACHE];
		udb_storage_mutex_unlock(&cache->budget->mutex);
		if (charge <= available)
			break;
		if (!udb_storage_cache_evict_one(cache))
		{
			udb_storage_mutex_unlock(&cache->mutex);
			return UDB_STORAGE_LIMIT;
		}
	}
	entry =
		udb_storage_alloc(cache->budget, bypass ? UDB_STORAGE_MEMORY_AUX : UDB_STORAGE_MEMORY_CACHE, sizeof(*entry));
	if (!entry)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_ERROR;
	}
	result = udb_storage_file_read(file, offset, entry->bytes, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_page_decode(entry->bytes, UDB_STORAGE_PAGE_SIZE, &page);
	if (result == UDB_STORAGE_OK && (page.generation != generation || page.ordinal != offset / UDB_STORAGE_PAGE_SIZE))
		result = UDB_STORAGE_CORRUPT;
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_free(entry);
		udb_storage_mutex_unlock(&cache->mutex);
		return result;
	}
	entry->file_identity = file->identity;
	entry->offset = offset;
	entry->generation = generation;
	entry->pins = 1;
	entry->transient = bypass && page.kind == UDB_STORAGE_PAGE_DATA;
	if (bypass && !entry->transient)
	{
		/* Retain bounded index/catalog pages, not scanned DATA pages. */
		for (;;)
		{
			udb_storage_mutex_lock(&cache->budget->mutex);
			available = cache->budget->limit[UDB_STORAGE_MEMORY_CACHE] - cache->budget->used[UDB_STORAGE_MEMORY_CACHE];
			udb_storage_mutex_unlock(&cache->budget->mutex);
			if (charge <= available)
				break;
			if (!udb_storage_cache_evict_one(cache))
			{
				entry->transient = 1;
				break;
			}
		}
		if (!entry->transient)
		{
			UdbStorageAllocation *allocation = (UdbStorageAllocation *)entry - 1;
			if (udb_storage_budget_charge(cache->budget, UDB_STORAGE_MEMORY_CACHE, charge) != UDB_STORAGE_OK)
				entry->transient = 1;
			else
			{
				allocation->info.memory_class = UDB_STORAGE_MEMORY_CACHE;
				udb_storage_budget_uncharge(cache->budget, UDB_STORAGE_MEMORY_AUX, charge);
			}
		}
	}
	entry->page = page;
	entry->hash_next = cache->buckets[bucket];
	cache->buckets[bucket] = entry;
	udb_storage_cache_make_newest(cache, entry);
	cache->entries++;
	*out = entry;
	udb_storage_mutex_unlock(&cache->mutex);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_release(UdbStorageCache *cache, UdbStorageCacheEntry *entry)
{
	UdbStorageCacheEntry *member;
	size_t bucket;

	if (!cache || !entry)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&cache->mutex);
	bucket = udb_storage_cache_bucket(entry->file_identity, entry->offset, entry->generation);
	for (member = cache->buckets[bucket]; member && member != entry; member = member->hash_next)
		;
	if (!member || !member->pins)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_ERROR;
	}
	member->pins--;
	if (!member->pins && member->transient)
		udb_storage_cache_erase(cache, member);
	udb_storage_mutex_unlock(&cache->mutex);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_forget_range_locked(UdbStorageCache *cache, uint64_t identity,
															  uint64_t offset, uint64_t length)
{
	uint64_t end;
	UdbStorageCacheEntry *entry, *next;

	if (!identity || length > UINT64_MAX - offset)
		return UDB_STORAGE_ERROR;
	if (!length)
		return UDB_STORAGE_OK;
	end = offset + length;
	for (entry = cache->oldest; entry; entry = entry->newer)
		if (entry->file_identity == identity && entry->offset < end && entry->offset + UDB_STORAGE_PAGE_SIZE > offset &&
			entry->pins)
			return UDB_STORAGE_BUSY;
	for (entry = cache->oldest; entry; entry = next)
	{
		next = entry->newer;
		if (entry->file_identity == identity && entry->offset < end && entry->offset + UDB_STORAGE_PAGE_SIZE > offset)
			udb_storage_cache_erase(cache, entry);
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_cache_forget_range(UdbStorageCache *cache, uint64_t identity, uint64_t offset,
													   uint64_t length)
{
	UdbStorageResult result;

	if (!cache)
		return UDB_STORAGE_ERROR;
	udb_storage_mutex_lock(&cache->mutex);
	result = udb_storage_cache_forget_range_locked(cache, identity, offset, length);
	udb_storage_mutex_unlock(&cache->mutex);
	return result;
}

static UdbStorageResult udb_storage_cache_forget_file(UdbStorageCache *cache, uint64_t identity)
{
	return udb_storage_cache_forget_range(cache, identity, 0, UINT64_MAX);
}

static UdbStorageResult udb_storage_cache_detach_file(UdbStorageFile *file)
{
	UdbStorageCache *cache = file->cache;
	UdbStorageFile **link;
	UdbStorageResult result;

	if (!cache)
		return UDB_STORAGE_OK;
	udb_storage_mutex_lock(&cache->mutex);
	link = &cache->files;
	while (*link && *link != file)
		link = &(*link)->cache_next;
	if (!*link)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		return UDB_STORAGE_ERROR;
	}
	result = udb_storage_cache_forget_range_locked(cache, file->identity, 0, UINT64_MAX);
	if (result == UDB_STORAGE_OK)
	{
		*link = file->cache_next;
		file->cache_next = NULL;
		file->cache = NULL;
	}
	udb_storage_mutex_unlock(&cache->mutex);
	return result;
}

/* End of udb_storage_cache.c.inc */
/* Inlined: udb_storage_catalog.c.inc */
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

/* End of udb_storage_catalog.c.inc */
/* Inlined: udb_storage_codec.c.inc */
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

/* End of udb_storage_codec.c.inc */
/* Inlined: udb_storage_engine.c.inc */
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

/* End of udb_storage_engine.c.inc */
/* Inlined: udb_storage_fragments.c.inc */
/*
 * UDB 4 native storage: private sorted fragments and paged range/offset indices.
 * Builders never publish, flush a commit, alter a catalog or truncate a file.
 * Their owner must keep the file open and serialize append builders. Orphaned
 * output is reclaimed only by the validated journal/checkpoint layer.
 */

typedef struct UdbStorageIndexEntry
{
	uint64_t offset;
	uint64_t generation;
	size_t key_length;
	unsigned char key[UDB_STORAGE_KEY_MAX];
} UdbStorageIndexEntry;

struct UdbStorageFragmentBuilder
{
	UdbStorageFile *file;
	UdbStorageBudget *budget;
	unsigned char *rows;
	unsigned char *payload;
	unsigned char *page;
	uint32_t *offsets;
	UdbStorageIndexEntry *level[2];
	unsigned char *last_key;
	size_t last_key_length;
	size_t rows_length;
	size_t pending_count;
	uint32_t data_pages;
	uint32_t data_max;
	uint32_t reserved_pages;
	uint32_t total_pages;
	uint64_t start;
	uint64_t generation;
	uint64_t object;
	uint64_t count;
	unsigned char block;
	UdbStorageStream stream;
	int failed;
	int finished;
};

static void udb_storage_fragment_builder_free(UdbStorageFragmentBuilder **pointer)
{
	UdbStorageFragmentBuilder *builder;

	if (!pointer || !*pointer)
		return;
	builder = *pointer;
	udb_storage_free(builder->rows);
	udb_storage_free(builder->payload);
	udb_storage_free(builder->page);
	udb_storage_free(builder->offsets);
	udb_storage_free(builder->level[0]);
	udb_storage_free(builder->level[1]);
	udb_storage_free(builder->last_key);
	udb_storage_free(builder);
	*pointer = NULL;
}

static UdbStorageResult udb_storage_fragment_builder_create(UdbStorageFile *file, uint64_t generation, uint64_t object,
															UdbStorageFragmentBuilder **out)
{
	UdbStorageFragmentBuilder *builder;
	UdbStorageResult result;
	uint64_t size;
	unsigned int pages = UDB_STORAGE_FRAGMENT_MAX / UDB_STORAGE_PAGE_SIZE;

	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!file || !file->opened || !generation || !object)
		return UDB_STORAGE_ERROR;
	result = udb_storage_file_reserve_fragment(file, &pages, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	if (!udb_storage_io_range_valid(size, UDB_STORAGE_FRAGMENT_MAX))
		return UDB_STORAGE_LIMIT;
	builder = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*builder));
	if (!builder)
		return UDB_STORAGE_LIMIT;
	builder->file = file;
	builder->budget = file->directory->budget;
	builder->start = size;
	builder->reserved_pages = pages;
	const unsigned int fanout = (UDB_STORAGE_PAGE_PAYLOAD - 12) / (UDB_STORAGE_KEY_MAX + 20);
	for (unsigned int data = 1; data <= UDB_STORAGE_FRAGMENT_DATA_PAGES_MAX; data++)
	{
		unsigned int total = data, nodes = data;
		do
		{
			nodes = (nodes + fanout - 1) / fanout;
			total += nodes;
		} while (nodes > 1);
		if (total > pages)
			break;
		builder->data_max = data;
	}
	if (!builder->data_max)
	{
		udb_storage_fragment_builder_free(&builder);
		return UDB_STORAGE_LIMIT;
	}

	builder->generation = generation;
	builder->object = object;
	builder->rows = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_PAYLOAD);
	builder->payload = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_PAYLOAD);
	builder->page = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	builder->offsets = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX,
										 (UDB_STORAGE_DATA_ROWS_MAX + 1) * sizeof(*builder->offsets));
	for (unsigned int i = 0; i < 2; i++)
		builder->level[i] = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX,
											  UDB_STORAGE_FRAGMENT_DATA_PAGES_MAX * sizeof(*builder->level[i]));
	builder->last_key = udb_storage_alloc(builder->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_KEY_MAX);
	if (!builder->rows || !builder->payload || !builder->page || !builder->offsets || !builder->level[0] ||
		!builder->level[1] || !builder->last_key)
	{
		udb_storage_fragment_builder_free(&builder);
		return UDB_STORAGE_LIMIT;
	}
	*out = builder;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_write_page(UdbStorageFragmentBuilder *builder, UdbStoragePageKind kind,
														size_t payload_length, uint64_t *offset)
{
	UdbStoragePage page;
	UdbStorageResult result;
	uint64_t position;

	if (builder->total_pages >= builder->reserved_pages)
		return UDB_STORAGE_LIMIT;
	position = builder->start + (uint64_t)builder->total_pages * UDB_STORAGE_PAGE_SIZE;
	memset(&page, 0, sizeof(page));
	page.kind = kind;
	page.generation = builder->generation;
	page.object = builder->object;
	page.ordinal = position / UDB_STORAGE_PAGE_SIZE;
	page.payload.data = builder->payload;
	page.payload.length = (uint32_t)payload_length;
	result = udb_storage_page_encode(&page, builder->page, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(builder->file, position, builder->page, UDB_STORAGE_PAGE_SIZE);
	if (result != UDB_STORAGE_OK)
	{
		builder->failed = 1;
		return result;
	}
	builder->total_pages++;
	*offset = position;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_flush_data(UdbStorageFragmentBuilder *builder)
{
	UdbStorageResult result;
	UdbStorageIndexEntry *index;
	size_t base, length;
	uint64_t offset;

	if (!builder->pending_count)
		return UDB_STORAGE_OK;
	if (builder->data_pages >= builder->data_max)
		return UDB_STORAGE_LIMIT;
	base = 8 + 4 * (builder->pending_count + 1);
	length = base + builder->rows_length;
	udb_storage_put_u32(builder->payload, (uint32_t)builder->pending_count);
	udb_storage_put_u32(builder->payload + 4, 0);
	for (size_t i = 0; i <= builder->pending_count; i++)
		udb_storage_put_u32(builder->payload + 8 + 4 * i, (uint32_t)base + builder->offsets[i]);
	memcpy(builder->payload + base, builder->rows, builder->rows_length);
	result = udb_storage_fragment_write_page(builder, UDB_STORAGE_PAGE_DATA, length, &offset);
	if (result != UDB_STORAGE_OK)
		return result;
	index = &builder->level[0][builder->data_pages++];
	index->offset = offset;
	index->generation = builder->generation;
	index->key_length = builder->last_key_length;
	memcpy(index->key, builder->last_key, builder->last_key_length);
	builder->pending_count = 0;
	builder->rows_length = 0;
	builder->offsets[0] = 0;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_builder_add(UdbStorageFragmentBuilder *builder, const unsigned char *bytes,
														 size_t length)
{
	UdbStorageRecord record;
	UdbStorageRow row, previous, key;
	UdbStorageResult result;
	size_t needed, key_length;

	if (!builder || builder->failed || builder->finished)
		return UDB_STORAGE_ERROR;
	result = udb_storage_record_decode(bytes, length, &record);
	if (result != UDB_STORAGE_OK)
	{
		builder->failed = 1;
		return result;
	}
	row = record.row;
	if (builder->count)
	{
		if (row.block != builder->block || !udb_storage_record_matches_stream(&record, builder->stream) ||
			udb_storage_row_decode(builder->last_key, builder->last_key_length, &previous) != UDB_STORAGE_OK ||
			udb_storage_key_compare(&previous, &row) >= 0)
		{
			builder->failed = 1;
			return UDB_STORAGE_CORRUPT;
		}
	}
	if (builder->data_pages == builder->data_max)
		return UDB_STORAGE_LIMIT;
	needed = 8 + 4 * (builder->pending_count + 2) + builder->rows_length + length;
	if (needed > UDB_STORAGE_PAGE_PAYLOAD || builder->pending_count == UDB_STORAGE_DATA_ROWS_MAX)
	{
		result = udb_storage_fragment_flush_data(builder);
		if (result != UDB_STORAGE_OK)
			return result;
		if (builder->data_pages == builder->data_max)
			return UDB_STORAGE_LIMIT;
	}
	key = row;
	key.kind = UDB_STORAGE_VALUE_NONE;
	key.value.data = NULL;
	key.value.length = 0;
	key.number = 0;
	/* Encode the last key into separate scratch before touching state. */
	result = udb_storage_row_encode(&key, builder->payload, UDB_STORAGE_PAGE_PAYLOAD, &key_length);
	if (result != UDB_STORAGE_OK)
		return result;
	if (key_length > UDB_STORAGE_KEY_MAX || builder->count == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	memcpy(builder->rows + builder->rows_length, bytes, length);
	builder->rows_length += length;
	builder->offsets[++builder->pending_count] = (uint32_t)builder->rows_length;
	memcpy(builder->last_key, builder->payload, key_length);
	builder->last_key_length = key_length;
	builder->block = row.block;
	builder->stream = record.operation == UDB_STORAGE_PREFIX_DELETE ? UDB_STORAGE_PREFIXES : UDB_STORAGE_POINTS;
	builder->count++;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_build_index(UdbStorageFragmentBuilder *builder,
														 UdbStorageIndexEntry *children, size_t count, uint32_t level,
														 UdbStorageIndexEntry *parents, size_t *parent_count)
{
	size_t begin = 0;

	*parent_count = 0;
	while (begin < count)
	{
		size_t end = begin, entries_length = 0, base, position;
		UdbStorageIndexEntry *parent;
		UdbStorageResult result;
		uint64_t offset;

		while (end < count)
		{
			size_t entry_length = 16 + children[end].key_length;
			size_t needed = 8 + 4 * (end - begin + 2) + entries_length + entry_length;

			if (needed > UDB_STORAGE_PAGE_PAYLOAD)
				break;
			entries_length += entry_length;
			end++;
		}
		if (end == begin)
			return UDB_STORAGE_LIMIT;
		base = 8 + 4 * (end - begin + 1);
		udb_storage_put_u32(builder->payload, (uint32_t)(end - begin));
		udb_storage_put_u32(builder->payload + 4, level);
		position = base;
		for (size_t i = begin; i < end; i++)
		{
			udb_storage_put_u32(builder->payload + 8 + 4 * (i - begin), (uint32_t)position);
			udb_storage_put_u64(builder->payload + position, children[i].offset);
			udb_storage_put_u64(builder->payload + position + 8, children[i].generation);
			memcpy(builder->payload + position + 16, children[i].key, children[i].key_length);
			position += 16 + children[i].key_length;
		}
		udb_storage_put_u32(builder->payload + 8 + 4 * (end - begin), (uint32_t)position);
		result = udb_storage_fragment_write_page(builder, UDB_STORAGE_PAGE_INDEX, position, &offset);
		if (result != UDB_STORAGE_OK)
			return result;
		parent = &parents[(*parent_count)++];
		parent->offset = offset;
		parent->generation = builder->generation;
		parent->key_length = children[end - 1].key_length;
		memcpy(parent->key, children[end - 1].key, parent->key_length);
		begin = end;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_builder_finish(UdbStorageFragmentBuilder *builder,
															UdbStorageFragment *fragment)
{
	UdbStorageResult result;
	size_t count, parent_count = 0;
	unsigned int input = 0;
	uint32_t level = 1;
	UdbStorageFragment candidate;

	if (!builder || !fragment || builder->failed || builder->finished || !builder->count)
		return UDB_STORAGE_ERROR;
	result = udb_storage_fragment_flush_data(builder);
	if (result != UDB_STORAGE_OK)
		return result;
	count = builder->data_pages;
	for (;;)
	{
		if (level > 3)
		{
			builder->failed = 1;
			return UDB_STORAGE_LIMIT;
		}
		result = udb_storage_fragment_build_index(builder, builder->level[input], count, level,
												  builder->level[1 - input], &parent_count);
		if (result != UDB_STORAGE_OK)
		{
			builder->failed = 1;
			return result;
		}
		input = 1 - input;
		if (parent_count == 1)
			break;
		count = parent_count;
		level++;
	}
	memset(&candidate, 0, sizeof(candidate));
	candidate.start = builder->start;
	candidate.length = (uint64_t)builder->total_pages * UDB_STORAGE_PAGE_SIZE;
	candidate.root = builder->level[input][0].offset;
	candidate.generation = builder->generation;
	candidate.object = builder->object;
	candidate.count = builder->count;
	candidate.data_pages = builder->data_pages;
	candidate.root_level = level;
	candidate.block = builder->block;
	candidate.stream = builder->stream;
	if (!udb_storage_fragment_valid(&candidate))
	{
		builder->failed = 1;
		return UDB_STORAGE_ERROR;
	}
	builder->finished = 1;
	*fragment = candidate;
	return UDB_STORAGE_OK;
}

/* The page codec validates complete offset tables on cache admission. */
static UdbStorageResult udb_storage_fragment_slots(const UdbStoragePage *page, uint32_t expected_level, uint32_t *count)
{
	if (!page->items || page->level != expected_level)
		return UDB_STORAGE_CORRUPT;
	*count = page->items;
	return UDB_STORAGE_OK;
}

static void udb_storage_fragment_slot(const UdbStoragePage *page, uint32_t slot, UdbStorageSpan *span)
{
	uint32_t begin = udb_storage_get_u32(page->payload.data + 8 + 4 * slot);
	uint32_t end = udb_storage_get_u32(page->payload.data + 8 + 4 * (slot + 1));
	span->data = page->payload.data + begin;
	span->length = end - begin;
}

static UdbStorageResult udb_storage_fragment_index_key(const UdbStoragePage *page, uint32_t slot,
													   const UdbStorageFragment *fragment, uint64_t page_offset,
													   uint32_t level, UdbStorageRow *key, uint64_t *child)
{
	UdbStorageSpan span;
	UdbStorageResult result;
	uint64_t offset, generation, data_end;

	udb_storage_fragment_slot(page, slot, &span);
	if (span.length < 16)
		return UDB_STORAGE_CORRUPT;
	offset = udb_storage_get_u64(span.data);
	generation = udb_storage_get_u64(span.data + 8);
	data_end = fragment->start + (uint64_t)fragment->data_pages * UDB_STORAGE_PAGE_SIZE;
	if (offset < fragment->start || offset >= page_offset || offset % UDB_STORAGE_PAGE_SIZE ||
		generation != fragment->generation || (level == 1 ? offset >= data_end : offset < data_end))
		return UDB_STORAGE_CORRUPT;
	result = udb_storage_row_decode(span.data + 16, span.length - 16, key);
	if (result != UDB_STORAGE_OK)
		return result;
	if (key->kind != UDB_STORAGE_VALUE_NONE || key->block != fragment->block)
		return UDB_STORAGE_CORRUPT;
	*child = offset;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_seek_data(UdbStorageCache *cache, UdbStorageFile *file,
													   const UdbStorageFragment *fragment, const UdbStorageRow *key,
													   uint64_t *data_offset)
{
	uint64_t offset;
	uint32_t level;
	UdbStorageCacheEntry *entry = NULL;
	UdbStorageResult result;
	UdbStoragePage page;
	uint32_t count, begin, end, slot;

	if (!data_offset)
		return UDB_STORAGE_ERROR;
	*data_offset = 0;
	if (!udb_storage_fragment_valid(fragment) || !udb_storage_row_valid(key) || key->block != fragment->block)
		return UDB_STORAGE_ERROR;
	offset = fragment->root;
	level = fragment->root_level;
	while (level)
	{
		UdbStorageRow separator;
		uint64_t child = 0;

		result = udb_storage_cache_acquire(cache, file, offset, fragment->generation, &entry);
		if (result != UDB_STORAGE_OK)
			return result;
		page = entry->page;
		if (page.kind != UDB_STORAGE_PAGE_INDEX || page.object != fragment->object)
		{
			result = UDB_STORAGE_CORRUPT;
			goto fail;
		}
		result = udb_storage_fragment_slots(&page, level, &count);
		if (result != UDB_STORAGE_OK)
			goto fail;
		begin = 0;
		end = count;
		while (begin < end)
		{
			slot = begin + (end - begin) / 2;
			result = udb_storage_fragment_index_key(&page, slot, fragment, offset, level, &separator, &child);
			if (result != UDB_STORAGE_OK)
				goto fail;
			if (udb_storage_key_compare(&separator, key) < 0)
				begin = slot + 1;
			else
				end = slot;
		}
		if (begin == count)
		{
			result = UDB_STORAGE_NOT_FOUND;
			goto fail;
		}
		result = udb_storage_fragment_index_key(&page, begin, fragment, offset, level, &separator, &child);
		if (result != UDB_STORAGE_OK)
			goto fail;
		offset = child;
		result = udb_storage_cache_release(cache, entry);
		entry = NULL;
		if (result != UDB_STORAGE_OK)
			return result;
		level--;
	}
	*data_offset = offset;
	return UDB_STORAGE_OK;
fail:
	if (entry && udb_storage_cache_release(cache, entry) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_storage_fragment_get(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageFragment *fragment, const UdbStorageRow *key,
												 UdbStorageRecord *record, UdbStorageCacheEntry **pin)
{
	UdbStorageCacheEntry *entry = NULL;
	UdbStoragePage page;
	UdbStorageResult result;
	uint64_t offset;
	uint32_t count, begin, end, slot;

	if (!record || !pin)
		return UDB_STORAGE_ERROR;
	*pin = NULL;
	result = udb_storage_fragment_seek_data(cache, file, fragment, key, &offset);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_cache_acquire(cache, file, offset, fragment->generation, &entry);
	if (result != UDB_STORAGE_OK)
		return result;
	page = entry->page;
	if (page.kind != UDB_STORAGE_PAGE_DATA || page.object != fragment->object)
	{
		result = UDB_STORAGE_CORRUPT;
		goto fail;
	}
	result = udb_storage_fragment_slots(&page, 0, &count);
	if (result != UDB_STORAGE_OK)
		goto fail;
	begin = 0;
	end = count;
	while (begin < end)
	{
		UdbStorageRecord candidate;
		UdbStorageSpan span;

		slot = begin + (end - begin) / 2;
		udb_storage_fragment_slot(&page, slot, &span);
		result = udb_storage_record_decode(span.data, span.length, &candidate);
		if (result != UDB_STORAGE_OK)
			goto fail;
		if (candidate.row.block != fragment->block || !udb_storage_record_matches_stream(&candidate, fragment->stream))
		{
			result = UDB_STORAGE_CORRUPT;
			goto fail;
		}
		if (udb_storage_key_compare(&candidate.row, key) < 0)
			begin = slot + 1;
		else
			end = slot;
	}
	if (begin < count)
	{
		UdbStorageSpan span;
		UdbStorageRecord candidate;

		udb_storage_fragment_slot(&page, begin, &span);
		result = udb_storage_record_decode(span.data, span.length, &candidate);
		if (result != UDB_STORAGE_OK)
			goto fail;
		if (candidate.row.block != fragment->block || !udb_storage_record_matches_stream(&candidate, fragment->stream))
		{
			result = UDB_STORAGE_CORRUPT;
			goto fail;
		}
		if (!udb_storage_key_compare(&candidate.row, key))
		{
			*record = candidate;
			*pin = entry;
			return UDB_STORAGE_OK;
		}
	}
	result = UDB_STORAGE_NOT_FOUND;
fail:
	if (entry && udb_storage_cache_release(cache, entry) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_storage_fragment_maximum(UdbStorageCache *cache, UdbStorageFile *file,
													 const UdbStorageFragment *fragment, UdbStorageRow *key,
													 UdbStorageCacheEntry **pin)
{
	UdbStorageResult result;
	uint64_t child;
	if (!key || !pin || !udb_storage_fragment_valid(fragment))
		return UDB_STORAGE_ERROR;
	*pin = NULL;
	result = udb_storage_cache_acquire(cache, file, fragment->root, fragment->generation, pin);
	if (result != UDB_STORAGE_OK)
		return result;
	if ((*pin)->page.kind != UDB_STORAGE_PAGE_INDEX || (*pin)->page.object != fragment->object ||
		(*pin)->page.level != fragment->root_level || !(*pin)->page.items)
		result = UDB_STORAGE_CORRUPT;
	else
		result = udb_storage_fragment_index_key(&(*pin)->page, (*pin)->page.items - 1, fragment, fragment->root,
												fragment->root_level, key, &child);
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_cache_release(cache, *pin);
		*pin = NULL;
	}
	return result;
}

struct UdbStorageFragmentIterator
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageFragment fragment;
	UdbStorageCacheEntry *entry;
	uint32_t page_number;
	uint32_t slot;
	uint64_t seen;
	size_t prefix_length;
	unsigned char prefix[UDB_STORAGE_KEY_MAX];
	int finished;
	UdbStorageResult failure;
};

static UdbStorageResult udb_storage_fragment_iterator_fail(UdbStorageFragmentIterator *iterator,
														   UdbStorageResult result)
{
	iterator->failure = result;
	return result;
}

static UdbStorageResult udb_storage_fragment_iterator_create(UdbStorageCache *cache, UdbStorageFile *file,
															 const UdbStorageFragment *fragment,
															 const UdbStorageRow *prefix,
															 UdbStorageFragmentIterator **out)
{
	UdbStorageFragmentIterator *iterator;
	UdbStorageResult result;
	uint64_t offset = 0;

	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || !file->opened || file->cache != cache || !udb_storage_fragment_valid(fragment) ||
		(prefix && (!udb_storage_row_valid(prefix) || prefix->block != fragment->block)))
		return UDB_STORAGE_ERROR;
	if (prefix)
	{
		result = udb_storage_fragment_seek_data(cache, file, fragment, prefix, &offset);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	iterator = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*iterator));
	if (!iterator)
		return UDB_STORAGE_LIMIT;
	iterator->cache = cache;
	iterator->file = file;
	iterator->fragment = *fragment;
	if (prefix)
	{
		UdbStorageRow key = *prefix;

		key.kind = UDB_STORAGE_VALUE_NONE;
		key.value.data = NULL;
		key.value.length = 0;
		key.number = 0;
		result = udb_storage_row_encode(&key, iterator->prefix, sizeof(iterator->prefix), &iterator->prefix_length);
		if (result != UDB_STORAGE_OK)
		{
			udb_storage_free(iterator);
			return result;
		}
		iterator->page_number = (uint32_t)((offset - fragment->start) / UDB_STORAGE_PAGE_SIZE);
	}
	*out = iterator;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_fragment_iterator_next(UdbStorageFragmentIterator *iterator,
														   UdbStorageRecord *record)
{
	UdbStorageResult result;
	UdbStoragePage page;
	UdbStorageRow prefix;

	if (!iterator || !record)
		return UDB_STORAGE_ERROR;
	if (iterator->failure)
		return iterator->failure;
	if (iterator->finished)
		return UDB_STORAGE_NOT_FOUND;
	if (iterator->prefix_length &&
		udb_storage_row_decode(iterator->prefix, iterator->prefix_length, &prefix) != UDB_STORAGE_OK)
		return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_ERROR);
	for (;;)
	{
		UdbStorageSpan span;
		UdbStorageRecord candidate;

		if (iterator->entry && iterator->slot == iterator->entry->page.items)
		{
			result = udb_storage_cache_release(iterator->cache, iterator->entry);
			if (result != UDB_STORAGE_OK)
				return udb_storage_fragment_iterator_fail(iterator, result);
			iterator->entry = NULL;
			iterator->slot = 0;
			iterator->page_number++;
		}
		if (iterator->page_number == iterator->fragment.data_pages)
		{
			iterator->finished = 1;
			if (!iterator->prefix_length && iterator->seen != iterator->fragment.count)
				return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_CORRUPT);
			return UDB_STORAGE_NOT_FOUND;
		}
		if (!iterator->entry)
		{
			uint64_t offset = iterator->fragment.start + (uint64_t)iterator->page_number * UDB_STORAGE_PAGE_SIZE;

			result = udb_storage_cache_acquire(iterator->cache, iterator->file, offset, iterator->fragment.generation,
											   &iterator->entry);
			if (result != UDB_STORAGE_OK)
				return udb_storage_fragment_iterator_fail(iterator, result);
		}
		page = iterator->entry->page;
		if (page.kind != UDB_STORAGE_PAGE_DATA || page.object != iterator->fragment.object || page.level ||
			iterator->slot >= page.items)
			return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_CORRUPT);
		udb_storage_fragment_slot(&page, iterator->slot++, &span);
		result = udb_storage_record_decode(span.data, span.length, &candidate);
		if (result != UDB_STORAGE_OK)
			return udb_storage_fragment_iterator_fail(iterator, result);
		if (candidate.row.block != iterator->fragment.block ||
			!udb_storage_record_matches_stream(&candidate, iterator->fragment.stream))
			return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_CORRUPT);
		if (iterator->prefix_length && !udb_storage_key_has_prefix(&candidate.row, &prefix))
		{
			if (udb_storage_key_compare(&candidate.row, &prefix) < 0)
				continue;
			result = udb_storage_cache_release(iterator->cache, iterator->entry);
			if (result != UDB_STORAGE_OK)
				return udb_storage_fragment_iterator_fail(iterator, result);
			iterator->entry = NULL;
			iterator->finished = 1;
			return UDB_STORAGE_NOT_FOUND;
		}
		if (!iterator->prefix_length && iterator->seen >= iterator->fragment.count)
			return udb_storage_fragment_iterator_fail(iterator, UDB_STORAGE_CORRUPT);
		iterator->seen++;
		*record = candidate;
		return UDB_STORAGE_OK;
	}
}

static UdbStorageResult udb_storage_fragment_iterator_close(UdbStorageFragmentIterator **pointer)
{
	UdbStorageResult result;

	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	if ((*pointer)->entry)
	{
		result = udb_storage_cache_release((*pointer)->cache, (*pointer)->entry);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	udb_storage_free(*pointer);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

typedef struct UdbStorageFragmentNode
{
	uint64_t count;
	uint32_t level;
	unsigned int references;
	size_t first_length;
	size_t last_length;
	unsigned char first[UDB_STORAGE_KEY_MAX];
	unsigned char last[UDB_STORAGE_KEY_MAX];
} UdbStorageFragmentNode;

static UdbStorageResult udb_storage_fragment_copy_key(const UdbStorageRow *row, unsigned char *out, size_t *length)
{
	UdbStorageRow key = *row;

	key.kind = UDB_STORAGE_VALUE_NONE;
	key.value.data = NULL;
	key.value.length = 0;
	key.number = 0;
	return udb_storage_row_encode(&key, out, UDB_STORAGE_KEY_MAX, length);
}

/*
 * Startup/candidate validation bypasses cache admission. All scratch is charged,
 * with at most 64 nodes for one fragment, independent of database size. Check
 * reachability exactly once, parent levels, separator fidelity, nonoverlapping
 * child key ranges, all rows and the descriptor count. Callers still validate
 * the owning catalog, logical partition root and journal before publication.
 */
static UdbStorageResult udb_storage_fragment_validate(UdbStorageFile *file, const UdbStorageFragment *fragment,
													  UdbStorageRecordValidator validator, void *context)
{
	UdbStorageFragmentNode *nodes = NULL;
	unsigned char *bytes = NULL;
	unsigned char *previous = NULL;
	size_t previous_length = 0;
	UdbStorageBudget *budget;
	UdbStorageResult result = UDB_STORAGE_ERROR;
	size_t page_count;

	if (!file || !file->opened || !udb_storage_fragment_valid(fragment))
		return UDB_STORAGE_ERROR;
	budget = file->directory->budget;
	page_count = (size_t)(fragment->length / UDB_STORAGE_PAGE_SIZE);
	nodes = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, page_count * sizeof(*nodes));
	bytes = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	previous = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_KEY_MAX);
	if (!nodes || !bytes || !previous)
	{
		result = UDB_STORAGE_LIMIT;
		goto cleanup;
	}
	for (size_t i = 0; i < page_count; i++)
	{
		UdbStoragePage page;
		UdbStorageFragmentNode *node = &nodes[i];
		uint64_t offset = fragment->start + (uint64_t)i * UDB_STORAGE_PAGE_SIZE;
		int data = i < fragment->data_pages;

		result = udb_storage_file_read(file, offset, bytes, UDB_STORAGE_PAGE_SIZE);
		if (result != UDB_STORAGE_OK)
			goto cleanup;
		result = udb_storage_page_decode(bytes, UDB_STORAGE_PAGE_SIZE, &page);
		if (result != UDB_STORAGE_OK)
			goto cleanup;
		if (page.generation != fragment->generation || page.object != fragment->object ||
			page.ordinal != offset / UDB_STORAGE_PAGE_SIZE ||
			page.kind != (data ? UDB_STORAGE_PAGE_DATA : UDB_STORAGE_PAGE_INDEX))
		{
			result = UDB_STORAGE_CORRUPT;
			goto cleanup;
		}
		node->level = page.level;
		for (uint32_t j = 0; j < page.items; j++)
		{
			UdbStorageRow row;

			if (data)
			{
				UdbStorageSpan span;
				UdbStorageRecord record;

				udb_storage_fragment_slot(&page, j, &span);
				result = udb_storage_record_decode(span.data, span.length, &record);
				if (result != UDB_STORAGE_OK)
					goto cleanup;
				row = record.row;
				if (row.block != fragment->block || !udb_storage_record_matches_stream(&record, fragment->stream))
				{
					result = UDB_STORAGE_CORRUPT;
					goto cleanup;
				}
				if (previous_length)
				{
					UdbStorageRow prior;

					result = udb_storage_row_decode(previous, previous_length, &prior);
					if (result != UDB_STORAGE_OK || udb_storage_key_compare(&prior, &row) >= 0)
					{
						result = UDB_STORAGE_CORRUPT;
						goto cleanup;
					}
				}
				if (validator)
				{
					result = validator(&record, context);
					if (result != UDB_STORAGE_OK)
					{
						if (result == UDB_STORAGE_NOT_FOUND)
							result = UDB_STORAGE_CORRUPT;
						goto cleanup;
					}
				}
				if (!j)
				{
					result = udb_storage_fragment_copy_key(&row, node->first, &node->first_length);
					if (result != UDB_STORAGE_OK)
						goto cleanup;
				}
				result = udb_storage_fragment_copy_key(&row, previous, &previous_length);
				if (result != UDB_STORAGE_OK)
					goto cleanup;
				memcpy(node->last, previous, previous_length);
				node->last_length = previous_length;
				node->count++;
			}
			else
			{
				uint64_t child_offset;
				size_t child_number;
				UdbStorageFragmentNode *child;
				UdbStorageRow child_last, child_first;

				result = udb_storage_fragment_index_key(&page, j, fragment, offset, page.level, &row, &child_offset);
				if (result != UDB_STORAGE_OK)
					goto cleanup;
				child_number = (size_t)((child_offset - fragment->start) / UDB_STORAGE_PAGE_SIZE);
				child = &nodes[child_number];
				if (child->level + 1 != node->level || child->references ||
					child->count > fragment->count - node->count ||
					udb_storage_row_decode(child->last, child->last_length, &child_last) != UDB_STORAGE_OK ||
					udb_storage_row_decode(child->first, child->first_length, &child_first) != UDB_STORAGE_OK ||
					udb_storage_key_compare(&child_last, &row))
				{
					result = UDB_STORAGE_CORRUPT;
					goto cleanup;
				}
				if (j)
				{
					UdbStorageRow prior;

					if (udb_storage_row_decode(node->last, node->last_length, &prior) != UDB_STORAGE_OK ||
						udb_storage_key_compare(&prior, &child_first) >= 0)
					{
						result = UDB_STORAGE_CORRUPT;
						goto cleanup;
					}
				}
				else
				{
					memcpy(node->first, child->first, child->first_length);
					node->first_length = child->first_length;
				}
				memcpy(node->last, child->last, child->last_length);
				node->last_length = child->last_length;
				node->count += child->count;
				child->references++;
			}
		}
	}
	for (size_t i = 0; i < page_count; i++)
	{
		if (nodes[i].references != (i + 1 == page_count ? 0u : 1u))
		{
			result = UDB_STORAGE_CORRUPT;
			goto cleanup;
		}
	}
	if (nodes[page_count - 1].count != fragment->count || nodes[page_count - 1].level != fragment->root_level)
	{
		result = UDB_STORAGE_CORRUPT;
		goto cleanup;
	}
	result = UDB_STORAGE_OK;
cleanup:
	udb_storage_free(previous);
	udb_storage_free(bytes);
	udb_storage_free(nodes);
	return result;
}

/* End of udb_storage_fragments.c.inc */
/* Inlined: udb_storage_hash.c.inc */
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

/* End of udb_storage_hash.c.inc */
/* Inlined: udb_storage_io.c.inc */
/*
 * UDB 4 native storage: exclusive, non-destructive 64-bit file operations.
 * File creation is provisioning, not a transaction commit. The engine must
 * validate a complete manifest and its durable journal before publication.
 */
#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
typedef char UdbStorageRequires64BitOffsets[(sizeof(off_t) >= 8) ? 1 : -1];
#endif

static int udb_storage_mutex_init(UdbStorageMutex *mutex)
{
	if (!mutex)
		return 0;
	memset(mutex, 0, sizeof(*mutex));
#ifdef _WIN32
	if (!InitializeCriticalSectionEx(&mutex->native, 0, 0))
		return 0;
#else
	if (pthread_mutex_init(&mutex->native, NULL))
		return 0;
#endif
	mutex->initialized = 1;
	return 1;
}

static void udb_storage_mutex_lock(UdbStorageMutex *mutex)
{
#ifdef _WIN32
	EnterCriticalSection(&mutex->native);
#else
	if (pthread_mutex_lock(&mutex->native))
		abort();
#endif
}

static void udb_storage_mutex_unlock(UdbStorageMutex *mutex)
{
#ifdef _WIN32
	LeaveCriticalSection(&mutex->native);
#else
	if (pthread_mutex_unlock(&mutex->native))
		abort();
#endif
}

static void udb_storage_mutex_destroy(UdbStorageMutex *mutex)
{
	if (!mutex || !mutex->initialized)
		return;
#ifdef _WIN32
	DeleteCriticalSection(&mutex->native);
#else
	if (pthread_mutex_destroy(&mutex->native))
		abort();
#endif
	mutex->initialized = 0;
}

static int udb_storage_file_name_valid(const char *name)
{
	size_t length;

	if (!name || (strncmp(name, "udb_", 4) && strncmp(name, "stage_", 6)))
		return 0;
	length = strlen(name);
	if (length < 5 || length > 63 || strstr(name, "..") || name[length - 1] == '.')
		return 0;
	for (size_t i = 0; i < length; i++)
	{
		unsigned char c = (unsigned char)name[i];

		if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-' &&
			c != '.')
			return 0;
	}
	return 1;
}

static int udb_storage_io_range_valid(uint64_t offset, size_t length)
{
	return offset <= INT64_MAX && (uint64_t)length <= (uint64_t)INT64_MAX - offset;
}

#ifdef _WIN32
/* Stack-bounded owner-only security descriptor; Windows copies it on create. */
typedef struct UdbStorageWinSecurity
{
	union
	{
		TOKEN_USER align;
		unsigned char bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
	} user;
	union
	{
		ACL align;
		unsigned char bytes[256];
	} acl;
	SECURITY_DESCRIPTOR descriptor;
	SECURITY_ATTRIBUTES attributes;
} UdbStorageWinSecurity;

static int udb_storage_win_security_init(UdbStorageWinSecurity *security)
{
	HANDLE token;
	DWORD length = 0;
	DWORD acl_length;
	PSID sid;

	memset(security, 0, sizeof(*security));
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return 0;
	if (!GetTokenInformation(token, TokenUser, security->user.bytes, sizeof(security->user.bytes), &length))
	{
		CloseHandle(token);
		return 0;
	}
	if (!CloseHandle(token))
		return 0;
	sid = security->user.align.User.Sid;
	if (!IsValidSid(sid))
		return 0;
	acl_length = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(sid);
	if (acl_length > sizeof(security->acl.bytes) ||
		!InitializeAcl((PACL)security->acl.bytes, acl_length, ACL_REVISION) ||
		!AddAccessAllowedAce((PACL)security->acl.bytes, ACL_REVISION, FILE_ALL_ACCESS, sid) ||
		!InitializeSecurityDescriptor(&security->descriptor, SECURITY_DESCRIPTOR_REVISION) ||
		!SetSecurityDescriptorOwner(&security->descriptor, sid, FALSE) ||
		!SetSecurityDescriptorDacl(&security->descriptor, TRUE, (PACL)security->acl.bytes, FALSE) ||
		!SetSecurityDescriptorControl(&security->descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
		return 0;
	security->attributes.nLength = sizeof(security->attributes);
	security->attributes.lpSecurityDescriptor = &security->descriptor;
	security->attributes.bInheritHandle = FALSE;
	return 1;
}

static int udb_storage_win_owner_only(HANDLE handle, UdbStorageBudget *budget, UdbStorageWinSecurity *security)
{
	DWORD length = 0;
	PSECURITY_DESCRIPTOR descriptor = NULL;
	PSID owner = NULL;
	PACL acl = NULL;
	BOOL defaulted, present;
	SECURITY_DESCRIPTOR_CONTROL control;
	DWORD revision;
	PACCESS_ALLOWED_ACE ace;
	int valid = 0;

	GetKernelObjectSecurity(handle, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, 0, &length);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !length || length > 65536)
		return 0;
	descriptor = udb_storage_alloc(budget, UDB_STORAGE_MEMORY_AUX, length);
	if (!descriptor)
		return 0;
	if (!GetKernelObjectSecurity(handle, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, descriptor, length,
								 &length) ||
		!IsValidSecurityDescriptor(descriptor) || !GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) ||
		!owner || !IsValidSid(owner) || !EqualSid(owner, security->user.align.User.Sid) ||
		!GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) || !present || !acl || !IsValidAcl(acl) ||
		acl->AceCount != 1 || !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
		!(control & SE_DACL_PROTECTED) || !GetAce(acl, 0, (void **)&ace) ||
		ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags || ace->Mask != FILE_ALL_ACCESS ||
		ace->Header.AceSize < offsetof(ACCESS_ALLOWED_ACE, SidStart) + 8 ||
		((SID *)&ace->SidStart)->SubAuthorityCount > SID_MAX_SUB_AUTHORITIES ||
		GetSidLengthRequired(((SID *)&ace->SidStart)->SubAuthorityCount) >
			ace->Header.AceSize - offsetof(ACCESS_ALLOWED_ACE, SidStart) ||
		!IsValidSid((PSID)&ace->SidStart) ||
		GetLengthSid((PSID)&ace->SidStart) > ace->Header.AceSize - offsetof(ACCESS_ALLOWED_ACE, SidStart) ||
		!EqualSid((PSID)&ace->SidStart, security->user.align.User.Sid))
		goto cleanup;
	valid = 1;
cleanup:
	udb_storage_free(descriptor);
	return valid;
}

static int udb_storage_win_local_ntfs(const wchar_t *path)
{
	wchar_t volume[UDB_STORAGE_IO_PATH_MAX], filesystem[32];
	DWORD flags;

	if (!GetVolumePathNameW(path, volume, UDB_STORAGE_IO_PATH_MAX) || GetDriveTypeW(volume) != DRIVE_FIXED)
		return 0;
	if (!GetVolumeInformationW(volume, NULL, 0, NULL, NULL, &flags, filesystem,
							   (DWORD)(sizeof(filesystem) / sizeof(filesystem[0]))))
		return 0;
	return !_wcsicmp(filesystem, L"NTFS");
}
#else
static int udb_storage_posix_directory_valid(int fd)
{
	struct stat st;

	return !fstat(fd, &st) && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && (st.st_mode & 0777) == 0700;
}

static int udb_storage_posix_file_valid(int fd)
{
	struct stat st;

	return !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() && (st.st_mode & 0777) == 0600 &&
		   st.st_nlink == 1;
}

static int udb_storage_posix_flush(int fd)
{
	int result;

	do
		result = fsync(fd);
	while (result < 0 && errno == EINTR);
	return result == 0;
}
#endif

static UdbStorageResult udb_storage_directory_open(UdbStorageDirectory *directory, UdbStorageBudget *budget,
												   const char *parent)
{
	int created = 0;

	if (!directory || !budget || !budget->initialized || !parent || !*parent ||
		strlen(parent) > UDB_STORAGE_IO_PATH_MAX - 6)
		return UDB_STORAGE_ERROR;
	if (directory->opened)
		return UDB_STORAGE_BUSY;
	if (udb_storage_budget_charge(budget, UDB_STORAGE_MEMORY_AUX, sizeof(*directory)) != UDB_STORAGE_OK)
		return UDB_STORAGE_LIMIT;
	memset(directory, 0, sizeof(*directory));
#ifdef _WIN32
	{
		UdbStorageWinSecurity security;
		BY_HANDLE_FILE_INFORMATION information;
		int length =
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, parent, -1, directory->path, UDB_STORAGE_IO_PATH_MAX);

		directory->handle = INVALID_HANDLE_VALUE;
		if (!length || !udb_storage_win_security_init(&security))
			goto fail;
		length--;
		while (length && (directory->path[length - 1] == L'\\' || directory->path[length - 1] == L'/'))
			length--;
		memcpy(directory->path + length, L"\\udb", 5 * sizeof(wchar_t));
		if (!udb_storage_win_local_ntfs(directory->path))
			goto fail;
		if (CreateDirectoryW(directory->path, &security.attributes))
			created = 1;
		else if (GetLastError() != ERROR_ALREADY_EXISTS)
			goto fail;
		directory->handle = CreateFileW(directory->path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL,
										FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
										FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (directory->handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(directory->handle, &information) ||
			!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
			(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
			!udb_storage_win_owner_only(directory->handle, budget, &security))
			goto fail;
		/* NTFS provisioning durability belongs to the journal/manifest barrier,
		 * not a fictitious POSIX directory-fsync emulation. */
		(void)created;
	}
#else
	{
		int parent_fd = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

		directory->handle = -1;
		if (parent_fd < 0)
			goto fail;
		if (!mkdirat(parent_fd, "udb", 0700))
			created = 1;
		else if (errno != EEXIST)
		{
			close(parent_fd);
			goto fail;
		}
		directory->handle = openat(parent_fd, "udb", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (directory->handle < 0 || !udb_storage_posix_directory_valid(directory->handle) ||
			(created && !udb_storage_posix_flush(parent_fd)))
		{
			close(parent_fd);
			goto fail;
		}
		if (close(parent_fd))
			goto fail;
	}
#endif
	directory->budget = budget;
	directory->opened = 1;
	return UDB_STORAGE_OK;
fail:
#ifdef _WIN32
	if (directory->handle != INVALID_HANDLE_VALUE)
		CloseHandle(directory->handle);
#else
	if (directory->handle >= 0)
		close(directory->handle);
#endif
	udb_storage_budget_uncharge(budget, UDB_STORAGE_MEMORY_AUX, sizeof(*directory));
	memset(directory, 0, sizeof(*directory));
	return UDB_STORAGE_ERROR;
}

static UdbStorageResult udb_storage_directory_close(UdbStorageDirectory *directory)
{
	UdbStorageResult result;

	if (!directory || !directory->opened)
		return UDB_STORAGE_ERROR;
	if (directory->open_files)
		return UDB_STORAGE_BUSY;
#ifdef _WIN32
	result = CloseHandle(directory->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
	if (result != UDB_STORAGE_OK)
		return result;
#else
	result = close(directory->handle) == 0 ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#endif
	udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*directory));
	memset(directory, 0, sizeof(*directory));
	return result;
}

static UdbStorageResult udb_storage_file_open(UdbStorageDirectory *directory, const char *name, int create,
											  UdbStorageFile *file)
{
	UdbStorageResult result = UDB_STORAGE_ERROR;
	int created = 0;

	if (!directory || !directory->opened || !file || !udb_storage_file_name_valid(name) ||
		directory->open_files == SIZE_MAX || create < 0 || create > 3 || (create == 3 && strncmp(name, "stage_", 6)))
		return UDB_STORAGE_ERROR;
	if (file->opened)
		return UDB_STORAGE_BUSY;
	if (udb_storage_budget_charge(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*file)) != UDB_STORAGE_OK)
		return UDB_STORAGE_LIMIT;
	memset(file, 0, sizeof(*file));
	udb_storage_mutex_lock(&directory->budget->mutex);
	if (!directory->budget->next_file_identity || directory->budget->open_files == SIZE_MAX)
	{
		udb_storage_mutex_unlock(&directory->budget->mutex);
		udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*file));
		return UDB_STORAGE_LIMIT;
	}
	file->identity = directory->budget->next_file_identity++;
	udb_storage_mutex_unlock(&directory->budget->mutex);
#ifdef _WIN32
	{
		UdbStorageWinSecurity security;
		BY_HANDLE_FILE_INFORMATION information;
		wchar_t path[UDB_STORAGE_IO_PATH_MAX];
		size_t length = wcslen(directory->path), name_length = strlen(name);

		file->handle = INVALID_HANDLE_VALUE;
		if (length + name_length + 2 > UDB_STORAGE_IO_PATH_MAX || !udb_storage_win_security_init(&security))
			goto fail;
		memcpy(path, directory->path, length * sizeof(wchar_t));
		path[length++] = L'\\';
		for (size_t i = 0; i <= name_length; i++)
			path[length + i] = (wchar_t)(unsigned char)name[i];
		if (create == 1 || create == 2)
		{
			file->handle =
				CreateFileW(path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, &security.attributes, CREATE_NEW,
							FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_OVERLAPPED, NULL);
			if (file->handle != INVALID_HANDLE_VALUE)
				created = 1;
			else if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
				goto win_error;
		}
		if (!created && create == 2)
		{
			result = UDB_STORAGE_BUSY;
			goto fail;
		}
		if (!created)
			file->handle =
				CreateFileW(path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL | (create == 3 ? DELETE : 0), 0, NULL,
							OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_OVERLAPPED, NULL);
		if (file->handle == INVALID_HANDLE_VALUE)
			goto win_error;
		if (GetFileType(file->handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(file->handle, &information) ||
			(information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
			information.nNumberOfLinks != 1 ||
			!udb_storage_win_owner_only(file->handle, directory->budget, &security) ||
			(created && !FlushFileBuffers(file->handle)))
			goto fail;
		goto win_done;
	win_error:
		if (GetLastError() == ERROR_SHARING_VIOLATION || GetLastError() == ERROR_LOCK_VIOLATION)
			result = UDB_STORAGE_BUSY;
		else if (GetLastError() == ERROR_FILE_NOT_FOUND)
			result = UDB_STORAGE_NOT_FOUND;
		goto fail;
	win_done:;
	}
#else
	file->handle = -1;
	if (create == 1 || create == 2)
	{
		file->handle = openat(directory->handle, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (file->handle >= 0)
			created = 1;
		else if (errno != EEXIST)
			goto posix_error;
	}
	if (!created && create == 2)
	{
		result = UDB_STORAGE_BUSY;
		goto fail;
	}
	if (!created)
		file->handle = openat(directory->handle, name, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
	if (file->handle < 0)
		goto posix_error;
	if (!udb_storage_posix_file_valid(file->handle))
		goto fail;
	if (flock(file->handle, LOCK_EX | LOCK_NB))
	{
		result = errno == EWOULDBLOCK || errno == EAGAIN ? UDB_STORAGE_BUSY : UDB_STORAGE_ERROR;
		goto fail;
	}
	if (created && (!udb_storage_posix_flush(file->handle) || !udb_storage_posix_flush(directory->handle)))
		goto fail;
	goto posix_done;
posix_error:
	if (errno == ENOENT)
		result = UDB_STORAGE_NOT_FOUND;
	goto fail;
posix_done:;
#endif
	file->directory = directory;
	file->opened = 1;
	directory->open_files++;
	udb_storage_mutex_lock(&directory->budget->mutex);
	directory->budget->open_files++;
	udb_storage_mutex_unlock(&directory->budget->mutex);
	if (directory->budget->cache)
		udb_storage_cache_attach_file(directory->budget->cache, file);
	return UDB_STORAGE_OK;
fail:
#ifdef _WIN32
	if (file->handle != INVALID_HANDLE_VALUE)
		CloseHandle(file->handle);
#else
	if (file->handle >= 0)
		close(file->handle);
#endif
	udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*file));
	memset(file, 0, sizeof(*file));
	return result;
}

static UdbStorageResult udb_storage_file_close(UdbStorageFile *file)
{
	UdbStorageResult result;

	if (!file || !file->opened)
		return UDB_STORAGE_ERROR;
	result = udb_storage_cache_detach_file(file);
	if (result != UDB_STORAGE_OK)
		return result;
#ifdef _WIN32
	result = CloseHandle(file->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
	if (result != UDB_STORAGE_OK)
		return result;
#else
	/* On Linux close(EINTR) has already released the descriptor; never retry. */
	result = close(file->handle) == 0 ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#endif
	file->directory->open_files--;
	udb_storage_mutex_lock(&file->directory->budget->mutex);
	file->directory->budget->open_files--;
	udb_storage_mutex_unlock(&file->directory->budget->mutex);
	udb_storage_budget_uncharge(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*file));
	memset(file, 0, sizeof(*file));
	return result;
}

/* Only caller-owned, quiesced private staging files may be removed. Stable
 * containers/WAL/metadata never use unlink/rename as a commit mechanism. */
static UdbStorageResult udb_storage_file_remove_private(UdbStorageDirectory *directory, const char *name)
{
	if (!directory || !directory->opened || !udb_storage_file_name_valid(name) || strncmp(name, "stage_", 6))
		return UDB_STORAGE_ERROR;
#ifdef _WIN32
	{
		wchar_t path[UDB_STORAGE_IO_PATH_MAX];
		size_t length = wcslen(directory->path), name_length = strlen(name);
		if (length + name_length + 2 > UDB_STORAGE_IO_PATH_MAX)
			return UDB_STORAGE_LIMIT;
		memcpy(path, directory->path, length * sizeof(*path));
		path[length++] = L'\\';
		for (size_t i = 0; i <= name_length; i++)
			path[length + i] = (wchar_t)(unsigned char)name[i];
		if (!DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND)
			return UDB_STORAGE_ERROR;
		return UDB_STORAGE_OK;
	}
#else
	if (unlinkat(directory->handle, name, 0) && errno != ENOENT)
		return UDB_STORAGE_ERROR;
	return udb_storage_posix_flush(directory->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#endif
}

#ifdef _WIN32
static UdbStorageResult udb_storage_win_transfer(UdbStorageFile *file, uint64_t offset, void *bytes, size_t length,
												 int write)
{
	unsigned char *position = bytes;
	OVERLAPPED operation;
	HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
	UdbStorageResult result = UDB_STORAGE_ERROR;

	if (!event)
		return result;
	while (length)
	{
		DWORD chunk = length > 1024 * 1024 ? 1024 * 1024 : (DWORD)length;
		DWORD transferred = 0;
		BOOL success;

		memset(&operation, 0, sizeof(operation));
		operation.Offset = (DWORD)offset;
		operation.OffsetHigh = (DWORD)(offset >> 32);
		operation.hEvent = event;
		if (!ResetEvent(event))
			goto cleanup;
		success = write ? WriteFile(file->handle, position, chunk, NULL, &operation)
						: ReadFile(file->handle, position, chunk, NULL, &operation);
		if (!success && GetLastError() != ERROR_IO_PENDING)
			goto cleanup;
		if (!GetOverlappedResult(file->handle, &operation, &transferred, TRUE))
		{
			/* Cancellation does not itself complete an operation. Do not let
			 * a pending request retain this stack OVERLAPPED or caller buffer. */
			CancelIoEx(file->handle, &operation);
			GetOverlappedResult(file->handle, &operation, &transferred, TRUE);
			goto cleanup;
		}
		if (!transferred || transferred > chunk)
			goto cleanup;
		position += transferred;
		offset += transferred;
		length -= transferred;
	}
	result = UDB_STORAGE_OK;
cleanup:
	if (!CloseHandle(event))
		result = UDB_STORAGE_ERROR;
	return result;
}
#endif

static UdbStorageResult udb_storage_file_read(UdbStorageFile *file, uint64_t offset, void *bytes, size_t length)
{
	if (!file || !file->opened || (length && !bytes) || !udb_storage_io_range_valid(offset, length))
		return UDB_STORAGE_ERROR;
	if (!length)
		return UDB_STORAGE_OK;
#ifdef _WIN32
	return udb_storage_win_transfer(file, offset, bytes, length, 0);
#else
	{
		unsigned char *position = bytes;

		while (length)
		{
			size_t chunk = length > 1024 * 1024 ? 1024 * 1024 : length;
			ssize_t transferred = pread(file->handle, position, chunk, (off_t)offset);

			if (transferred < 0 && errno == EINTR)
				continue;
			if (transferred <= 0 || (size_t)transferred > chunk)
				return UDB_STORAGE_ERROR;
			position += transferred;
			offset += (uint64_t)transferred;
			length -= (size_t)transferred;
		}
		return UDB_STORAGE_OK;
	}
#endif
}

static UdbStorageResult udb_storage_file_write_native(UdbStorageFile *file, uint64_t offset, const void *bytes,
													  size_t length)
{
#ifdef _WIN32
	return udb_storage_win_transfer(file, offset, (void *)bytes, length, 1);
#else
	{
		const unsigned char *position = bytes;

		while (length)
		{
			size_t chunk = length > 1024 * 1024 ? 1024 * 1024 : length;
			ssize_t transferred = pwrite(file->handle, position, chunk, (off_t)offset);

			if (transferred < 0 && errno == EINTR)
				continue;
			if (transferred <= 0 || (size_t)transferred > chunk)
				return UDB_STORAGE_ERROR;
			position += transferred;
			offset += (uint64_t)transferred;
			length -= (size_t)transferred;
		}
		return UDB_STORAGE_OK;
	}
#endif
}

static UdbStorageResult udb_storage_file_write(UdbStorageFile *file, uint64_t offset, const void *bytes, size_t length)
{
	UdbStorageResult result;

	if (!file || !file->opened || (length && !bytes) || !udb_storage_io_range_valid(offset, length))
		return UDB_STORAGE_ERROR;
	if (file->append_limit && (offset > file->append_limit || length > file->append_limit - offset))
		return UDB_STORAGE_LIMIT;
	if (file->append_limit)
	{
		if (length > file->write_remaining)
			return UDB_STORAGE_LIMIT;
		file->write_remaining -= length;
	}
	if (!length)
		return UDB_STORAGE_OK;
	if (!file->cache)
		return udb_storage_file_write_native(file, offset, bytes, length);
	/* Cache -> budget is the only lock order. Hold the cache through the write
	 * so a concurrent acquire cannot re-admit stale bytes after invalidation. */
	udb_storage_mutex_lock(&file->cache->mutex);
	result = udb_storage_cache_forget_range_locked(file->cache, file->identity, offset, length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write_native(file, offset, bytes, length);
	udb_storage_mutex_unlock(&file->cache->mutex);
	return result;
}

static UdbStorageResult udb_storage_file_size(UdbStorageFile *file, uint64_t *size)
{
	if (!file || !file->opened || !size)
		return UDB_STORAGE_ERROR;
#ifdef _WIN32
	{
		LARGE_INTEGER value;

		if (!GetFileSizeEx(file->handle, &value) || value.QuadPart < 0)
			return UDB_STORAGE_ERROR;
		*size = (uint64_t)value.QuadPart;
	}
#else
	{
		struct stat st;

		if (fstat(file->handle, &st) || st.st_size < 0)
			return UDB_STORAGE_ERROR;
		*size = (uint64_t)st.st_size;
	}
#endif
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_file_resize_native(UdbStorageFile *file, uint64_t size)
{
#ifdef _WIN32
	{
		LARGE_INTEGER position;

		position.QuadPart = (LONGLONG)size;
		if (!SetFilePointerEx(file->handle, position, NULL, FILE_BEGIN) || !SetEndOfFile(file->handle))
			return UDB_STORAGE_ERROR;
	}
#else
	{
		int error;

		do
			error = ftruncate(file->handle, (off_t)size);
		while (error < 0 && errno == EINTR);
		if (error)
			return UDB_STORAGE_ERROR;
	}
#endif
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_file_resize(UdbStorageFile *file, uint64_t size)
{
	UdbStorageResult result;
	uint64_t previous;

	if (!file || !file->opened || size > INT64_MAX)
		return UDB_STORAGE_ERROR;
	if (file->append_limit && size > file->append_limit)
		return UDB_STORAGE_LIMIT;
	if (file->cache)
		udb_storage_mutex_lock(&file->cache->mutex);
	result = udb_storage_file_size(file, &previous);
	if (result == UDB_STORAGE_OK && file->cache && size < previous)
		result = udb_storage_cache_forget_range_locked(file->cache, file->identity, size, UINT64_MAX - size);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_resize_native(file, size);
	if (file->cache)
		udb_storage_mutex_unlock(&file->cache->mutex);
	return result;
}

static UdbStorageResult udb_storage_file_flush(UdbStorageFile *file)
{
	if (!file || !file->opened)
		return UDB_STORAGE_ERROR;
#ifdef _WIN32
	return FlushFileBuffers(file->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#else
	return udb_storage_posix_flush(file->handle) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
#endif
}

static void udb_storage_file_job_limit(UdbStorageFile *file, uint64_t limit)
{
	uint64_t size = 0;
	file->append_limit = limit;
	file->write_remaining = 0;
	if (limit && udb_storage_file_size(file, &size) == UDB_STORAGE_OK && size <= limit)
		file->write_remaining = limit - size;
}

/* End of udb_storage_io.c.inc */
/* Inlined: udb_storage_jobs.c.inc */
/* Production storage job dispatch. Thread-safe pure validators run before
 * journal admission; daemon state/effects/relay are foreground completion work.
 * Wire sequencing and internal physical sequencing are independent counters. */

typedef struct UdbStorageMutationWork
{
	uint64_t job;
	UdbStorageState state;
	UdbStoragePointChange *point;
	UdbStoragePrefixChange *prefix;
	UdbStorageRow row;
	unsigned char bytes[UDB_STORAGE_ROW_MAX];
	unsigned char restore_bytes[UDB_STORAGE_ROW_MAX];
	UdbStorageRow restore;
	int restore_present;
	unsigned int block;
	int normalized;
} UdbStorageMutationWork;

static UdbStorageResult udb_storage_mutation_work_close(UdbStorageEngine *engine)
{
	UdbStorageMutationWork *work = engine->mutation_work;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (!work)
		return result;
	if (work->point)
		result = udb_storage_point_close(&work->point);
	if (result == UDB_STORAGE_OK && work->prefix)
		result = udb_storage_prefix_close(&work->prefix);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_free(work);
	engine->mutation_work = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_mutation_header(const UdbStorageJob *job, const UdbStorageState *base,
													UdbStorageState *candidate, unsigned int *block)
{
	uint64_t sequence, epoch;
	char sid[4];
	int index;
	if (job->length < UDB_STORAGE_MUTATION_HEADER ||
		*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 21) > 1 ||
		!udb_storage_zero(job->payload + 22, 2))
		return UDB_STORAGE_REJECTED;
	memcpy(sid, job->payload, 4);
	if (!udb_storage_sid_valid(sid))
		return UDB_STORAGE_REJECTED;
	epoch = udb_storage_get_u64(job->payload + 4);
	sequence = udb_storage_get_u64(job->payload + 12);
	*candidate = *base;
	if (job->kind == UDB_STORAGE_JOB_BIND)
	{
		if (job->length != UDB_STORAGE_MUTATION_HEADER ||
			*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 20) ||
			*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 21) || sequence)
			return UDB_STORAGE_REJECTED;
		memcpy(candidate->source_sid, sid, 4);
		candidate->source_epoch = epoch;
		candidate->source_sequence = 0;
		return UDB_STORAGE_OK;
	}
	if (memcmp(sid, base->source_sid, 4) || epoch != base->source_epoch || base->source_sequence == UINT64_MAX)
		return UDB_STORAGE_REJECTED;
	if (*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 21))
	{
		if (sequence)
			return UDB_STORAGE_REJECTED;
		sequence = base->source_sequence + 1;
	}
	else if (!sequence || sequence != base->source_sequence + 1)
		return UDB_STORAGE_REJECTED;
	index = udb_storage_stage_block_index(*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 20));
	if (index < 0)
		return UDB_STORAGE_REJECTED;
	if (base->sequence == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	*block = (unsigned int)index;
	candidate->source_sequence = sequence;
	candidate->sequence++;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_mutation_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
													 const UdbStorageState *base, UdbStorageState *candidate)
{
	UdbStorageMutationWork *work = engine->mutation_work;
	UdbStorageResult result;
	unsigned int block = 0;
	UdbStorageState state;
	if (!work)
	{
		result = udb_storage_mutation_header(job, base, &state, &block);
		if (result != UDB_STORAGE_OK)
			return result;
		if (job->kind == UDB_STORAGE_JOB_BIND)
		{
			*candidate = state;
			return UDB_STORAGE_OK;
		}
		if (job->kind == UDB_STORAGE_JOB_DROP)
		{
			if (job->length != UDB_STORAGE_MUTATION_HEADER)
				return UDB_STORAGE_REJECTED;
			memset(state.blocks + block, 0, sizeof(state.blocks[block]));
			state.blocks[block].block = udb_storage_blocks[block];
			state.blocks[block].catalog.object = block + 1;
			state.blocks[block].generation = state.generation;
			state.blocks[block].watermark = state.sequence;
			result = udb_storage_logical_block(udb_storage_blocks[block], udb_storage_journal_empty_partition, NULL,
											   &state.blocks[block].summary);
			if (result == UDB_STORAGE_OK)
				*candidate = state;
			return result;
		}
		if (!engine->validator || !engine->mutation_validator ||
			(job->kind != UDB_STORAGE_JOB_POINT && job->kind != UDB_STORAGE_JOB_PREFIX &&
			 job->kind != UDB_STORAGE_JOB_EXPIRE))
			return UDB_STORAGE_REJECTED;
		work = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*work));
		if (!work)
			return UDB_STORAGE_LIMIT;
		engine->mutation_work = work;
		work->job = job->id;
		work->state = state;
		work->block = block;
		size_t offset = UDB_STORAGE_MUTATION_HEADER + (job->kind == UDB_STORAGE_JOB_EXPIRE ? 8 : 0);
		if (job->length <= offset || job->length - offset > sizeof(work->bytes))
			return UDB_STORAGE_REJECTED;
		memcpy(work->bytes, job->payload + offset, job->length - offset);
		result = udb_storage_row_decode(work->bytes, job->length - offset, &work->row);
		if (result != UDB_STORAGE_OK || work->row.block != udb_storage_blocks[block])
			return UDB_STORAGE_REJECTED;
		if (job->kind == UDB_STORAGE_JOB_EXPIRE)
		{
			uint64_t expected = udb_storage_get_u64(job->payload + UDB_STORAGE_MUTATION_HEADER);
			if (!expected || !*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 21) ||
				work->row.block != 'K' || work->row.depth != 2 || work->row.kind != UDB_STORAGE_VALUE_NONE)
				return UDB_STORAGE_REJECTED;
			UdbStorageRow expiry_key = work->row, stored;
			expiry_key.components[2].data = (const unsigned char *)"expires";
			expiry_key.components[2].length = 7;
			expiry_key.depth = 3;
			/* Compare on the writer's ordered base, not a stale foreground pin.
			 * A stale request consumes neither root nor wire sequence. */
			result = udb_storage_block_lookup(engine->cache, engine->files + block, base->blocks + block, &expiry_key,
											  work->restore_bytes, sizeof(work->restore_bytes), &stored);
			if (result < UDB_STORAGE_NOT_FOUND)
				return result;
			if (result == UDB_STORAGE_NOT_FOUND || stored.kind != UDB_STORAGE_VALUE_NUMBER || stored.number != expected)
			{
				result = udb_storage_mutation_work_close(engine);
				return result == UDB_STORAGE_OK ? UDB_STORAGE_STAGED : result;
			}
		}
		UdbStorageRecord record = {0};
		record.row = work->row;
		record.stamp.sequence = state.sequence;
		record.operation = job->kind == UDB_STORAGE_JOB_POINT ? UDB_STORAGE_PUT : UDB_STORAGE_PREFIX_DELETE;
		/* Public DEL semantics are subtree deletion, never a NONE-value alias. */
		if (job->kind != UDB_STORAGE_JOB_POINT &&
			(record.row.kind != UDB_STORAGE_VALUE_NONE || record.row.number || record.row.value.length))
			return UDB_STORAGE_REJECTED;
		result = engine->validator(&record, engine->validation_context);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_CORRUPT || result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_REJECTED : result;
		UdbStorageRow clear;
		UdbStorageResult normalized = UDB_STORAGE_NOT_FOUND;
		if (job->kind == UDB_STORAGE_JOB_POINT && engine->normalizer)
			normalized = engine->normalizer(&record, &clear, engine->validation_context);
		if (normalized != UDB_STORAGE_OK && normalized != UDB_STORAGE_NOT_FOUND)
			return normalized;
		if (normalized == UDB_STORAGE_OK)
		{
			/* Normalization clears children, preserving any explicit parent. */
			UdbStorageResult found =
				udb_storage_block_lookup(engine->cache, engine->files + block, base->blocks + block, &clear,
										 work->restore_bytes, sizeof(work->restore_bytes), &work->restore);
			if (found < UDB_STORAGE_NOT_FOUND)
				return found;
			work->restore_present = found == UDB_STORAGE_OK;
			if (!udb_storage_key_has_prefix(&record.row, &clear) || clear.kind != UDB_STORAGE_VALUE_NONE ||
				clear.number)
				return UDB_STORAGE_ERROR;
			record.row = clear;
			record.operation = UDB_STORAGE_PREFIX_DELETE;
			result = engine->validator(&record, engine->validation_context);
			if (result != UDB_STORAGE_OK)
				return result;
			result = udb_storage_prefix_create(engine->cache, engine->files + block, base->blocks + block,
											   state.generation, state.next_object, &record, &work->prefix);
			work->normalized = 1;
		}
		else if (job->kind == UDB_STORAGE_JOB_POINT)
			result = udb_storage_point_create(engine->cache, engine->files + block, base->blocks + block,
											  state.generation, state.next_object, &record, &work->point);
		else
			result = udb_storage_prefix_create(engine->cache, engine->files + block, base->blocks + block,
											   state.generation, state.next_object, &record, &work->prefix);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (work->job != job->id)
		return UDB_STORAGE_ERROR;
	if (work->point)
		result = udb_storage_point_step(work->point, work->state.blocks + work->block, &work->state.next_object);
	else
		result = udb_storage_prefix_step(work->prefix, work->state.blocks + work->block, &work->state.next_object);
	if (result == UDB_STORAGE_BUSY)
		return UDB_STORAGE_CONTINUE;
	if (result != UDB_STORAGE_OK)
		return result;
	if (work->normalized == 1 || work->normalized == 3)
	{
		UdbStorageRecord record = {0};
		if (work->state.sequence == UINT64_MAX)
			return UDB_STORAGE_LIMIT;
		if (work->prefix)
			result = udb_storage_prefix_close(&work->prefix);
		if (result == UDB_STORAGE_OK && work->point)
			result = udb_storage_point_close(&work->point);
		if (result != UDB_STORAGE_OK)
			return result;
		int restoring = work->normalized == 1 && work->restore_present;
		record.row = restoring ? work->restore : work->row;
		record.operation = UDB_STORAGE_PUT;
		record.stamp.sequence = ++work->state.sequence;
		result = udb_storage_point_create(engine->cache, engine->files + work->block, work->state.blocks + work->block,
										  work->state.generation, work->state.next_object, &record, &work->point);
		if (result != UDB_STORAGE_OK)
			return result;
		work->normalized = restoring ? 3 : 2;
		return UDB_STORAGE_CONTINUE;
	}
	result =
		engine->mutation_validator(engine, work->state.blocks + work->block, &work->row, engine->validation_context);
	if (result != UDB_STORAGE_OK)
		return result == UDB_STORAGE_CORRUPT || result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_REJECTED : result;
	*candidate = work->state;
	return udb_storage_mutation_work_close(engine);
}

static UdbStorageResult udb_storage_jobs_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
												 const UdbStorageState *base, UdbStorageState *candidate)
{
	if (!engine || !job || !base || !candidate)
		return UDB_STORAGE_ERROR;
	return job->kind >= UDB_STORAGE_STAGE_BEGIN ? udb_storage_stage_jobs_prepare(engine, job, base, candidate)
												: udb_storage_mutation_prepare(engine, job, base, candidate);
}

static UdbStorageResult udb_storage_jobs_finish(UdbStorageEngine *engine, const UdbStorageJob *job)
{
	if (job->kind >= UDB_STORAGE_STAGE_BEGIN)
		return udb_storage_stage_jobs_finish(engine, job);
	UdbStorageMutationWork *work = engine->mutation_work;
	return work && work->job == job->id ? udb_storage_mutation_work_close(engine) : UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_jobs_cleanup(UdbStorageEngine *engine)
{
	UdbStorageResult result = udb_storage_mutation_work_close(engine);
	return result == UDB_STORAGE_OK ? udb_storage_stage_jobs_cleanup(engine) : result;
}

/* End of udb_storage_jobs.c.inc */
/* Inlined: udb_storage_journal.c.inc */
/* Global durable root-set journal, two fixed 4 MiB checkpoint slots and recovery.
 * data flush -> WAL flush -> publication; checkpoint flush/readback -> durable
 * WAL floor -> covered retirement. No rename/unlink or old-format parser.
 * A WAL floor prevents silent fallback to an older slot after covered history
 * has been retired. Full-sized malformed WAL pages always fail closed. */

#define UDB_STORAGE_WAL_OBJECT UINT64_C(0x55444257414c3034)
#define UDB_STORAGE_META_OBJECT UINT64_C(0x5544424d45543034)

typedef struct UdbStorageJournalScratch
{
	unsigned char page[65536];
	unsigned char payload[UDB_STORAGE_STATE_SIZE + 64];
	UdbStorageState state[2];
} UdbStorageJournalScratch;

static int udb_storage_journal_valid(const UdbStorageJournal *journal)
{
	if (!journal || !journal->cache || !journal->wal || journal->wal->cache != journal->cache || !journal->wal->opened)
		return 0;
	for (unsigned int i = 0; i < 2; i++)
		if (!journal->metadata[i] || !journal->metadata[i]->opened || journal->metadata[i]->cache != journal->cache)
			return 0;
	for (unsigned int i = 0; i < 6; i++)
		if (!journal->data[i] || !journal->data[i]->opened || journal->data[i]->cache != journal->cache)
			return 0;
	return 1;
}

static UdbStorageResult udb_storage_journal_state_hash(const UdbStorageState *state, UdbStorageJournalScratch *scratch,
													   unsigned char hash[32])
{
	UdbStorageResult result = udb_storage_state_encode(state, scratch->payload);
	if (result != UDB_STORAGE_OK)
		return result;
	return SHA256(scratch->payload, UDB_STORAGE_STATE_SIZE, hash) ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
}

static UdbStorageResult udb_storage_metadata_read(UdbStorageJournal *journal, unsigned int slot,
												  UdbStorageJournalScratch *scratch, UdbStorageState *state,
												  unsigned char last_hash[32])
{
	UdbStoragePage page;
	UdbStorageState candidate;
	unsigned char first_hash[32], anchor[32];
	uint64_t size, generation = 0;
	UdbStorageResult result = udb_storage_file_size(journal->metadata[slot], &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size != UDB_STORAGE_METADATA_SIZE)
		return UDB_STORAGE_CORRUPT;
	for (uint64_t ordinal = 0; ordinal < UDB_STORAGE_METADATA_SIZE / UDB_STORAGE_PAGE_SIZE; ordinal++)
	{
		result = udb_storage_file_read(journal->metadata[slot], ordinal * UDB_STORAGE_PAGE_SIZE, scratch->page,
									   sizeof(scratch->page));
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_page_decode(scratch->page, sizeof(scratch->page), &page);
		if (result != UDB_STORAGE_OK)
			return result;
		if (page.kind != UDB_STORAGE_PAGE_METADATA || page.object != UDB_STORAGE_META_OBJECT + slot ||
			page.ordinal != ordinal || (ordinal && page.generation != generation))
			return UDB_STORAGE_CORRUPT;
		if (!ordinal)
		{
			if (page.payload.length != UDB_STORAGE_STATE_SIZE + 32)
				return UDB_STORAGE_CORRUPT;
			result = udb_storage_state_decode(page.payload.data, &candidate);
			if (result != UDB_STORAGE_OK)
				return result;
			generation = page.generation;
			if (candidate.generation != generation)
				return UDB_STORAGE_CORRUPT;
			memcpy(anchor, page.payload.data + UDB_STORAGE_STATE_SIZE, 32);
			memcpy(first_hash, scratch->page + 48, 32);
		}
		else if (ordinal == UDB_STORAGE_METADATA_SIZE / UDB_STORAGE_PAGE_SIZE - 1)
		{
			if (page.payload.length != 40 || memcmp(first_hash, page.payload.data, 32) ||
				udb_storage_get_u64(page.payload.data + 32) != UDB_STORAGE_METADATA_SIZE)
				return UDB_STORAGE_CORRUPT;
		}
		else if (page.payload.length)
			return UDB_STORAGE_CORRUPT;
	}
	*state = candidate;
	memcpy(last_hash, anchor, 32);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_metadata_write(UdbStorageJournal *journal, unsigned int slot,
												   UdbStorageJournalScratch *scratch)
{
	UdbStoragePage page;
	UdbStorageResult result;
	unsigned char first_hash[32], anchor[32], expected[32], observed[32];
	memset(&page, 0, sizeof(page));
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.object = UDB_STORAGE_META_OBJECT + slot;
	page.generation = journal->state.generation;
	page.payload.data = scratch->payload;
	result = udb_storage_state_encode(&journal->state, scratch->payload);
	if (result != UDB_STORAGE_OK)
		return result;
	memcpy(scratch->payload + UDB_STORAGE_STATE_SIZE, journal->last_hash, 32);
	page.payload.length = UDB_STORAGE_STATE_SIZE + 32;
	for (uint64_t ordinal = 0; ordinal < UDB_STORAGE_METADATA_SIZE / UDB_STORAGE_PAGE_SIZE; ordinal++)
	{
		page.ordinal = ordinal;
		if (ordinal == UDB_STORAGE_METADATA_SIZE / UDB_STORAGE_PAGE_SIZE - 1)
		{
			memcpy(scratch->payload, first_hash, 32);
			udb_storage_put_u64(scratch->payload + 32, UDB_STORAGE_METADATA_SIZE);
			page.payload.length = 40;
		}
		else if (ordinal)
			page.payload.length = 0;
		result = udb_storage_page_encode(&page, scratch->page, sizeof(scratch->page));
		if (result != UDB_STORAGE_OK)
			return result;
		if (!ordinal)
			memcpy(first_hash, scratch->page + 48, 32);
		result = udb_storage_file_write(journal->metadata[slot], ordinal * UDB_STORAGE_PAGE_SIZE, scratch->page,
										sizeof(scratch->page));
		if (result != UDB_STORAGE_OK)
			return result;
	}
	result = udb_storage_file_flush(journal->metadata[slot]);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_metadata_read(journal, slot, scratch, scratch->state, anchor);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_journal_state_hash(&journal->state, scratch, expected);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_journal_state_hash(scratch->state, scratch, observed);
	if (result != UDB_STORAGE_OK)
		return result;
	return memcmp(expected, observed, 32) || memcmp(anchor, journal->last_hash, 32) ? UDB_STORAGE_CORRUPT
																					: UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_journal_floor_write(UdbStorageJournal *journal, UdbStorageJournalScratch *scratch)
{
	UdbStoragePage page;
	unsigned char state_hash[32];
	UdbStorageResult result = udb_storage_journal_state_hash(&journal->state, scratch, state_hash);
	if (result != UDB_STORAGE_OK)
		return result;
	memset(scratch->payload, 0, 96);
	memcpy(scratch->payload, "UDBWAL4", 7);
	memcpy(scratch->payload + 8, journal->state.store_identity, 16);
	udb_storage_put_u64(scratch->payload + 24, journal->state.generation);
	memcpy(scratch->payload + 32, state_hash, 32);
	memcpy(scratch->payload + 64, journal->last_hash, 32);
	memset(&page, 0, sizeof(page));
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.object = UDB_STORAGE_WAL_OBJECT;
	page.generation = journal->state.generation;
	page.payload.data = scratch->payload;
	page.payload.length = 96;
	result = udb_storage_page_encode(&page, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(journal->wal, 0, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_flush(journal->wal);
	return result;
}

static UdbStorageResult udb_storage_journal_file_header(UdbStorageJournal *journal, unsigned int index,
														UdbStorageJournalScratch *scratch, int create)
{
	UdbStoragePage page;
	uint64_t size;
	UdbStorageResult result;
	if (create)
	{
		memset(&page, 0, sizeof(page));
		memset(scratch->payload, 0, 32);
		memcpy(scratch->payload, "UDBFILE4", 8);
		memcpy(scratch->payload + 8, journal->state.store_identity, 16);
		scratch->payload[24] = udb_storage_blocks[index];
		page.kind = UDB_STORAGE_PAGE_METADATA;
		page.generation = 1;
		page.object = udb_storage_blocks[index];
		page.payload.data = scratch->payload;
		page.payload.length = 32;
		result = udb_storage_page_encode(&page, scratch->page, sizeof(scratch->page));
		if (result == UDB_STORAGE_OK)
			result = udb_storage_file_write(journal->data[index], 0, scratch->page, sizeof(scratch->page));
		if (result == UDB_STORAGE_OK)
			result = udb_storage_file_flush(journal->data[index]);
		return result;
	}
	result = udb_storage_file_size(journal->data[index], &size);
	if (result != UDB_STORAGE_OK)
		return result;
	/* Private abandoned writes may leave a partial final page; no root may
	 * reference it. Recovery aligns that unreferenced append tail separately. */
	if (size < UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	result = udb_storage_file_read(journal->data[index], 0, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_page_decode(scratch->page, sizeof(scratch->page), &page);
	if (result != UDB_STORAGE_OK)
		return result;
	if (page.kind != UDB_STORAGE_PAGE_METADATA || page.generation != 1 || page.ordinal ||
		page.object != udb_storage_blocks[index] || page.payload.length != 32 ||
		memcmp(page.payload.data, "UDBFILE4", 8) || memcmp(page.payload.data + 8, journal->state.store_identity, 16) ||
		page.payload.data[24] != udb_storage_blocks[index] || !udb_storage_zero(page.payload.data + 25, 7))
		return UDB_STORAGE_CORRUPT;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_journal_empty_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
															void *context)
{
	(void)partition;
	(void)summary;
	(void)context;
	return UDB_STORAGE_NOT_FOUND;
}

static UdbStorageResult udb_storage_journal_initialize(UdbStorageJournal *journal)
{
	UdbStorageJournalScratch *scratch;
	UdbStorageResult result;
	uint64_t size;
	if (!udb_storage_journal_valid(journal) || journal->failed)
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 0; i < 9; i++)
	{
		UdbStorageFile *file = i < 6 ? journal->data[i] : i == 6 ? journal->wal : journal->metadata[i - 7];
		result = udb_storage_file_size(file, &size);
		if (result != UDB_STORAGE_OK)
			return result;
		if (size)
			return UDB_STORAGE_CORRUPT;
	}
	scratch = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_STORAGE_LIMIT;
	memset(&journal->state, 0, sizeof(journal->state));
	memset(journal->last_hash, 0, 32);
	journal->state.generation = 1;
	journal->state.next_object = 8;
	journal->state.stages.object = 7;
	if (RAND_bytes(journal->state.store_identity, sizeof(journal->state.store_identity)) != 1)
	{
		result = UDB_STORAGE_ERROR;
		goto done;
	}
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageBlockRoot *root = journal->state.blocks + i;
		root->block = udb_storage_blocks[i];
		root->generation = 1;
		root->catalog.object = i + 1;
		result = udb_storage_logical_block(root->block, udb_storage_journal_empty_partition, NULL, &root->summary);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_journal_file_header(journal, i, scratch, 1);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	result = udb_storage_metadata_write(journal, 0, scratch);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_metadata_write(journal, 1, scratch);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_journal_floor_write(journal, scratch);
	if (result == UDB_STORAGE_OK)
	{
		journal->slot = 1;
		journal->end = UDB_STORAGE_PAGE_SIZE;
	}
done:
	if (result != UDB_STORAGE_OK)
		journal->failed = 1;
	udb_storage_free(scratch);
	return result;
}

static UdbStorageResult udb_storage_journal_recover(UdbStorageJournal *journal, UdbStorageRecordValidator validator,
													void *context)
{
	UdbStorageJournalScratch *scratch;
	UdbStorageResult result, slots[2];
	UdbStoragePage page;
	unsigned char anchors[2][32], floor_hash[32], floor_anchor[32], state_hash[32], previous[32];
	uint64_t floor, size, checkpoint, previous_generation = 0;
	int checkpoint_seen;
	if (!udb_storage_journal_valid(journal))
		return UDB_STORAGE_ERROR;
	scratch = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_STORAGE_LIMIT;
	for (unsigned int i = 0; i < 2; i++)
	{
		slots[i] = udb_storage_metadata_read(journal, i, scratch, scratch->state + i, anchors[i]);
		if (slots[i] != UDB_STORAGE_OK && slots[i] != UDB_STORAGE_CORRUPT)
		{
			result = slots[i];
			goto done;
		}
	}
	if (slots[0] != UDB_STORAGE_OK && slots[1] != UDB_STORAGE_OK)
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	if (slots[0] == UDB_STORAGE_OK && slots[1] == UDB_STORAGE_OK &&
		scratch->state[0].generation == scratch->state[1].generation)
	{
		unsigned char hashes[2][32];
		result = udb_storage_journal_state_hash(scratch->state, scratch, hashes[0]);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_journal_state_hash(scratch->state + 1, scratch, hashes[1]);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (memcmp(hashes[0], hashes[1], 32) || memcmp(anchors[0], anchors[1], 32))
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
	}
	journal->slot = slots[1] == UDB_STORAGE_OK &&
							(slots[0] != UDB_STORAGE_OK || scratch->state[1].generation >= scratch->state[0].generation)
						? 1
						: 0;
	journal->state = scratch->state[journal->slot];
	memcpy(journal->last_hash, anchors[journal->slot], 32);
	checkpoint = journal->state.generation;
	result = udb_storage_file_size(journal->wal, &size);
	if (result != UDB_STORAGE_OK)
		goto done;
	if (size < UDB_STORAGE_PAGE_SIZE)
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	result = udb_storage_file_read(journal->wal, 0, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_page_decode(scratch->page, sizeof(scratch->page), &page);
	if (result != UDB_STORAGE_OK)
		goto done;
	if (page.kind != UDB_STORAGE_PAGE_METADATA || page.object != UDB_STORAGE_WAL_OBJECT || page.ordinal ||
		page.payload.length != 96 || memcmp(page.payload.data, "UDBWAL4\0", 8) ||
		memcmp(page.payload.data + 8, journal->state.store_identity, 16))
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	floor = udb_storage_get_u64(page.payload.data + 24);
	memcpy(floor_hash, page.payload.data + 32, 32);
	memcpy(floor_anchor, page.payload.data + 64, 32);
	if (!floor || page.generation != floor || checkpoint < floor)
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	checkpoint_seen = checkpoint == floor;
	if (checkpoint_seen)
	{
		result = udb_storage_journal_state_hash(&journal->state, scratch, state_hash);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (memcmp(state_hash, floor_hash, 32) || memcmp(journal->last_hash, floor_anchor, 32))
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
	}
	journal->end = size - size % UDB_STORAGE_PAGE_SIZE;
	for (uint64_t offset = UDB_STORAGE_PAGE_SIZE; offset < journal->end; offset += UDB_STORAGE_PAGE_SIZE)
	{
		UdbStorageState *candidate = scratch->state;
		unsigned char hash[32], predecessor[32];
		result = udb_storage_file_read(journal->wal, offset, scratch->page, sizeof(scratch->page));
		if (result == UDB_STORAGE_OK)
			result = udb_storage_page_decode(scratch->page, sizeof(scratch->page), &page);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (page.kind != UDB_STORAGE_PAGE_METADATA || page.object != UDB_STORAGE_WAL_OBJECT ||
			page.ordinal != offset / UDB_STORAGE_PAGE_SIZE || page.payload.length != UDB_STORAGE_STATE_SIZE + 32)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		memcpy(predecessor, page.payload.data, 32);
		memcpy(hash, scratch->page + 48, 32);
		result = udb_storage_state_decode(page.payload.data + 32, candidate);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (page.generation != candidate->generation ||
			memcmp(candidate->store_identity, journal->state.store_identity, 16) ||
			(previous_generation &&
			 (candidate->generation != previous_generation + 1 || memcmp(predecessor, previous, 32))))
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		previous_generation = candidate->generation;
		memcpy(previous, hash, 32);
		if (candidate->generation == checkpoint)
		{
			if (memcmp(hash, anchors[journal->slot], 32))
			{
				result = UDB_STORAGE_CORRUPT;
				goto done;
			}
			checkpoint_seen = 1;
		}
		if (candidate->generation <= checkpoint)
			continue;
		if (journal->state.generation == UINT64_MAX || candidate->generation != journal->state.generation + 1 ||
			candidate->sequence < journal->state.sequence || candidate->next_object < journal->state.next_object ||
			memcmp(predecessor, journal->last_hash, 32))
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		journal->state = *candidate;
		memcpy(journal->last_hash, hash, 32);
	}
	if (!checkpoint_seen)
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	for (unsigned int i = 0; i < 6; i++)
	{
		result = udb_storage_journal_file_header(journal, i, scratch, 0);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_block_validate(journal->cache, journal->data[i], journal->state.blocks + i, validator,
												context);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_block_object_bound(journal->cache, journal->data[i], journal->state.blocks + i,
													journal->state.next_object);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	result = udb_storage_stages_validate(journal, validator, context);
	if (result != UDB_STORAGE_OK)
		goto done;
	/* Only after every reference/root is validated may incomplete private data
	 * append tails and an incomplete final uncommitted WAL tail be retired. */
	for (unsigned int i = 0; i < 6; i++)
	{
		result = udb_storage_file_size(journal->data[i], &size);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (size % UDB_STORAGE_PAGE_SIZE)
		{
			result = udb_storage_file_resize(journal->data[i], size - size % UDB_STORAGE_PAGE_SIZE);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_file_flush(journal->data[i]);
			if (result != UDB_STORAGE_OK)
				goto done;
		}
	}
	result = udb_storage_file_size(journal->wal, &size);
	if (result == UDB_STORAGE_OK && size != journal->end)
	{
		result = udb_storage_file_resize(journal->wal, journal->end);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_file_flush(journal->wal);
	}
	if (result == UDB_STORAGE_OK && journal->state.stages.count)
	{
		UdbStorageState *discarded = scratch->state;
		if (journal->state.generation == UINT64_MAX)
		{
			result = UDB_STORAGE_LIMIT;
			goto done;
		}
		*discarded = journal->state;
		discarded->generation++;
		memset(&discarded->stages, 0, sizeof(discarded->stages));
		discarded->stages.object = 7;
		/* Every pending reference was validated above. Its runtime owner is
		 * no longer alive, so retire it durably before reusing owner IDs. */
		journal->failed = 0;
		result = udb_storage_journal_commit(journal, discarded);
	}
done:
	journal->failed = result != UDB_STORAGE_OK;
	udb_storage_free(scratch);
	return result;
}

static UdbStorageResult udb_storage_journal_commit(UdbStorageJournal *journal, const UdbStorageState *candidate)
{
	UdbStorageJournalScratch *scratch;
	UdbStoragePage page;
	UdbStorageResult result;
	if (!udb_storage_journal_valid(journal) || journal->failed || !udb_storage_state_valid(candidate) ||
		journal->state.generation == UINT64_MAX || candidate->generation != journal->state.generation + 1 ||
		candidate->sequence < journal->state.sequence || candidate->next_object < journal->state.next_object ||
		memcmp(candidate->store_identity, journal->state.store_identity, 16) ||
		!udb_storage_io_range_valid(journal->end, UDB_STORAGE_PAGE_SIZE))
		return UDB_STORAGE_ERROR;
	scratch = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_STORAGE_LIMIT;
	for (unsigned int i = 0; i < 6; i++)
	{
		/* Flushing unchanged files is safe and prevents a caller's dirty-mask
		 * omission from making a WAL reference durable ahead of its pages. */
		result = udb_storage_file_flush(journal->data[i]);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	memcpy(scratch->payload, journal->last_hash, 32);
	result = udb_storage_state_encode(candidate, scratch->payload + 32);
	if (result != UDB_STORAGE_OK)
		goto done;
	memset(&page, 0, sizeof(page));
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.generation = candidate->generation;
	page.object = UDB_STORAGE_WAL_OBJECT;
	page.ordinal = journal->end / UDB_STORAGE_PAGE_SIZE;
	page.payload.data = scratch->payload;
	page.payload.length = UDB_STORAGE_STATE_SIZE + 32;
	result = udb_storage_page_encode(&page, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(journal->wal, journal->end, scratch->page, sizeof(scratch->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_flush(journal->wal);
	if (result == UDB_STORAGE_OK)
	{
		journal->state = *candidate;
		memcpy(journal->last_hash, scratch->page + 48, 32);
		journal->end += UDB_STORAGE_PAGE_SIZE;
	}
done:
	if (result != UDB_STORAGE_OK)
		journal->failed = 1;
	udb_storage_free(scratch);
	return result;
}

static UdbStorageResult udb_storage_journal_checkpoint(UdbStorageJournal *journal)
{
	UdbStorageJournalScratch *scratch;
	UdbStorageResult result;
	unsigned int inactive;
	if (!udb_storage_journal_valid(journal) || journal->failed || !udb_storage_state_valid(&journal->state))
		return UDB_STORAGE_ERROR;
	scratch = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_STORAGE_LIMIT;
	inactive = journal->slot ^ 1;
	result = udb_storage_metadata_write(journal, inactive, scratch);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_journal_floor_write(journal, scratch);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_resize(journal->wal, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_flush(journal->wal);
	if (result == UDB_STORAGE_OK)
	{
		journal->slot = inactive;
		journal->end = UDB_STORAGE_PAGE_SIZE;
	}
	else
		journal->failed = 1;
	udb_storage_free(scratch);
	return result;
}

/* End of udb_storage_journal.c.inc */
/* Inlined: udb_storage_nodes.c.inc */
/*
 * UDB 4 native storage: typed radix nodes and immutable run manifest records.
 * Namespace 1 stores nodes, 2 point runs and 3 prefix runs. Missing nodes may
 * represent implicit empty subtrees only after complete logical-root validation;
 * missing advertised runs are corruption. Changes return private COW roots.
 */

static void udb_storage_node_key(const UdbStorageLogicalPosition *position, unsigned char key[64])
{
	memset(key, 0, 64);
	key[0] = position->block;
	key[1] = 1;
	key[2] = (unsigned char)(position->partition >> 8);
	key[3] = (unsigned char)position->partition;
	key[4] = (unsigned char)(position->depth >> 8);
	key[5] = (unsigned char)position->depth;
	memcpy(key + 6, position->prefix, 32);
}

static int udb_storage_node_valid(const UdbStorageLogicalPosition *position, const UdbStorageNode *node)
{
	UdbStorageLogicalSummary empty;
	if (!udb_storage_logical_position_valid(position) || !node || !udb_storage_logical_summary_valid(&node->summary) ||
		node->run_count > UDB_STORAGE_MAX_RUNS + 1)
		return 0;
	if (node->kind == UDB_STORAGE_LEAF)
	{
		if (node->summary.weight > UDB_STORAGE_LEAF_WEIGHT_MAX || (node->summary.count && !node->run_count))
			return 0;
		if (!node->summary.count && (udb_storage_logical_empty(position, &empty) != UDB_STORAGE_OK ||
									 memcmp(empty.digest, node->summary.digest, sizeof(empty.digest))))
			return 0;
	}
	else if (node->kind == UDB_STORAGE_BRANCH)
	{
		if (node->run_count || position->depth == 256 || node->summary.weight <= UDB_STORAGE_LEAF_WEIGHT_MAX)
			return 0;
	}
	else
		return 0;
	for (unsigned int i = 0; i < UDB_STORAGE_MAX_RUNS + 1; i++)
	{
		if (i >= node->run_count)
		{
			if (node->runs[i])
				return 0;
			continue;
		}
		if (!node->runs[i])
			return 0;
		for (unsigned int j = 0; j < i; j++)
			if (node->runs[i] == node->runs[j])
				return 0;
	}
	return 1;
}

static UdbStorageResult udb_storage_node_encode(const UdbStorageLogicalPosition *position, const UdbStorageNode *node,
												unsigned char value[128])
{
	if (!value || !udb_storage_node_valid(position, node) || udb_storage_overlaps(value, 128, node, sizeof(*node)))
		return UDB_STORAGE_ERROR;
	memset(value, 0, 128);
	value[0] = (unsigned char)node->kind;
	value[1] = (unsigned char)node->run_count;
	memcpy(value + 8, node->summary.digest, 32);
	udb_storage_put_u64(value + 40, node->summary.count);
	udb_storage_put_u64(value + 48, node->summary.weight);
	for (unsigned int i = 0; i < node->run_count; i++)
		udb_storage_put_u64(value + 56 + 8 * i, node->runs[i]);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_node_decode(const UdbStorageLogicalPosition *position,
												const unsigned char value[128], UdbStorageNode *node)
{
	UdbStorageNode candidate;
	if (!value || !node)
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 2; i < 8; i++)
		if (value[i])
			return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.kind = (UdbStorageNodeKind)value[0];
	candidate.run_count = value[1];
	memcpy(candidate.summary.digest, value + 8, 32);
	candidate.summary.count = udb_storage_get_u64(value + 40);
	candidate.summary.weight = udb_storage_get_u64(value + 48);
	for (unsigned int i = 0; i < UDB_STORAGE_MAX_RUNS + 1; i++)
		candidate.runs[i] = udb_storage_get_u64(value + 56 + 8 * i);
	if (!udb_storage_node_valid(position, &candidate))
		return UDB_STORAGE_CORRUPT;
	*node = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_node_get(UdbStorageCache *cache, UdbStorageFile *file,
											 const UdbStorageCatalogRoot *catalog,
											 const UdbStorageLogicalPosition *position, UdbStorageNode *node)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (!node || !udb_storage_logical_position_valid(position))
		return UDB_STORAGE_ERROR;
	udb_storage_node_key(position, key);
	result = udb_storage_catalog_get(cache, file, catalog, key, value);
	return result == UDB_STORAGE_OK ? udb_storage_node_decode(position, value, node) : result;
}

static UdbStorageResult udb_storage_node_change(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *catalog, uint64_t generation,
												const UdbStorageLogicalPosition *position, const UdbStorageNode *node,
												UdbStorageCatalogRoot *out)
{
	unsigned char key[64], value[128];
	UdbStorageResult result = udb_storage_node_encode(position, node, value);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_node_key(position, key);
	return udb_storage_catalog_change(cache, file, catalog, generation, key, value, out);
}

static UdbStorageResult udb_storage_node_locate(UdbStorageCache *cache, UdbStorageFile *file,
												const UdbStorageCatalogRoot *catalog, const UdbStorageRow *key,
												UdbStorageLogicalPosition *position, UdbStorageNode *node)
{
	UdbStorageLogicalPosition current;
	UdbStorageNode candidate;
	UdbStorageResult result;
	unsigned char hash[32];
	unsigned int partition;
	if (!position || !node || udb_storage_overlaps(position, sizeof(*position), node, sizeof(*node)))
		return UDB_STORAGE_ERROR;
	result = udb_storage_key_hash(key, hash, &partition);
	if (result != UDB_STORAGE_OK)
		return result;
	memset(&current, 0, sizeof(current));
	current.block = key->block;
	current.partition = (uint16_t)partition;
	for (;;)
	{
		result = udb_storage_node_get(cache, file, catalog, &current, &candidate);
		if (result != UDB_STORAGE_OK)
		{
			if (result == UDB_STORAGE_NOT_FOUND)
				*position = current;
			return result;
		}
		if (candidate.kind == UDB_STORAGE_LEAF)
		{
			*position = current;
			*node = candidate;
			return UDB_STORAGE_OK;
		}
		result =
			udb_storage_logical_child(&current, (hash[current.depth / 8] >> (7 - current.depth % 8)) & 1, &current);
		if (result != UDB_STORAGE_OK)
			return result;
	}
}

static void udb_storage_run_manifest_key(unsigned char block, UdbStorageStream stream, uint64_t id,
										 unsigned char key[64])
{
	memset(key, 0, 64);
	key[0] = block;
	key[1] = stream == UDB_STORAGE_POINTS ? 2 : 3;
	for (unsigned int i = 0; i < 8; i++)
		key[2 + i] = (unsigned char)(id >> (56 - 8 * i));
}

static UdbStorageResult udb_storage_run_save(UdbStorageCache *cache, const UdbStorageRun *run,
											 const UdbStorageCatalogRoot *catalog, uint64_t generation,
											 UdbStorageCatalogRoot *out)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (!cache || !run || !run->file || !run->file->opened || run->file->cache != cache ||
		!udb_storage_block_valid(run->block) ||
		(run->stream != UDB_STORAGE_POINTS && run->stream != UDB_STORAGE_PREFIXES) ||
		generation < run->directory.generation)
		return UDB_STORAGE_ERROR;
	result = udb_storage_catalog_root_encode(&run->directory, value, sizeof(value));
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_run_manifest_key(run->block, run->stream, run->directory.object, key);
	return udb_storage_catalog_change(cache, run->file, catalog, generation, key, value, out);
}

static UdbStorageResult udb_storage_run_load(UdbStorageCache *cache, UdbStorageFile *file,
											 const UdbStorageCatalogRoot *catalog, unsigned char block,
											 UdbStorageStream stream, uint64_t id, UdbStorageRun *run)
{
	unsigned char key[64], value[128];
	UdbStorageRun candidate;
	UdbStorageResult result;
	if (!run || !id || !udb_storage_block_valid(block) || !udb_storage_catalog_root_valid(catalog) ||
		(stream != UDB_STORAGE_POINTS && stream != UDB_STORAGE_PREFIXES))
		return UDB_STORAGE_ERROR;
	udb_storage_run_manifest_key(block, stream, id, key);
	result = udb_storage_catalog_get(cache, file, catalog, key, value);
	if (result == UDB_STORAGE_NOT_FOUND)
		return UDB_STORAGE_CORRUPT;
	if (result != UDB_STORAGE_OK)
		return result;
	memset(&candidate, 0, sizeof(candidate));
	result = udb_storage_catalog_root_decode(value, sizeof(value), &candidate.directory);
	if (result != UDB_STORAGE_OK)
		return result;
	if (candidate.directory.object != id || candidate.directory.generation > catalog->generation)
		return UDB_STORAGE_CORRUPT;
	candidate.file = file;
	candidate.block = block;
	candidate.stream = stream;
	*run = candidate;
	return UDB_STORAGE_OK;
}

/* End of udb_storage_nodes.c.inc */
/* Inlined: udb_storage_orphans.c.inc */
/* Startup-only private cleanup, after full stable recovery and before writer
 * admission. A filename alone never authorizes deletion of an unknown file. */

typedef struct UdbStoragePrivateName
{
	unsigned char nonce[16];
	unsigned char block;
	char role;
} UdbStoragePrivateName;

static int udb_storage_private_name(const char *name, UdbStoragePrivateName *decoded)
{
	if (!name || strlen(name) != 45 || memcmp(name, "stage_", 6) || name[38] != '_' || name[40] != '_' ||
		strcmp(name + 42, ".db") || !udb_storage_block_valid((unsigned char)name[39]) || !strchr("01m", name[41]))
		return 0;
	memset(decoded, 0, sizeof(*decoded));
	decoded->block = (unsigned char)name[39];
	decoded->role = name[41];
	for (unsigned int i = 0; i < 16; i++)
	{
		unsigned char a = (unsigned char)name[6 + 2 * i], b = (unsigned char)name[7 + 2 * i];
		int high = a >= '0' && a <= '9' ? a - '0' : a >= 'a' && a <= 'f' ? a - 'a' + 10 : -1;
		int low = b >= '0' && b <= '9' ? b - '0' : b >= 'a' && b <= 'f' ? b - 'a' + 10 : -1;
		if (high < 0 || low < 0)
			return 0;
		decoded->nonce[i] = (unsigned char)((high << 4) | low);
	}
	return 1;
}

static UdbStorageResult udb_storage_private_header(UdbStorageFile *file, const char *name)
{
	UdbStoragePrivateName decoded;
	if (!file || !file->opened || !udb_storage_private_name(name, &decoded) || decoded.role == 'm' ||
		udb_storage_zero(file->directory->store_identity, 16))
		return UDB_STORAGE_ERROR;
	unsigned char *bytes = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	if (!bytes)
		return UDB_STORAGE_LIMIT;
	unsigned char payload[48] = {0};
	memcpy(payload, "UDBTMP4", 7);
	memcpy(payload + 8, file->directory->store_identity, 16);
	memcpy(payload + 24, decoded.nonce, 16);
	payload[40] = decoded.block;
	payload[41] = (unsigned char)decoded.role;
	UdbStoragePage page = {0};
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.generation = page.object = 1;
	page.payload.data = payload;
	page.payload.length = sizeof(payload);
	UdbStorageResult result = udb_storage_page_encode(&page, bytes, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(file, 0, bytes, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_flush(file);
	udb_storage_free(bytes);
	return result;
}

static UdbStorageResult udb_storage_orphan_remove(UdbStorageDirectory *directory, const char *name,
												  unsigned char *bytes)
{
	UdbStoragePrivateName decoded;
	if (!udb_storage_private_name(name, &decoded))
		return UDB_STORAGE_OK;
	UdbStorageFile file = {0};
	/* Mode 3 opens an existing private file, with native DELETE authority on
	 * Windows. It never provisions/truncates a file, and retains exclusive access. */
	UdbStorageResult result = udb_storage_file_open(directory, name, 3, &file);
	if (result != UDB_STORAGE_OK)
		return UDB_STORAGE_OK; /* Foreign, unsafe, locked or vanished: preserve. */
	UdbStoragePage page;
	int owned = udb_storage_file_read(&file, 0, bytes, UDB_STORAGE_PAGE_SIZE) == UDB_STORAGE_OK &&
				udb_storage_page_decode(bytes, UDB_STORAGE_PAGE_SIZE, &page) == UDB_STORAGE_OK &&
				page.kind == UDB_STORAGE_PAGE_METADATA && !page.ordinal && page.payload.length >= 48 &&
				!memcmp(page.payload.data + 8, directory->store_identity, 16);
	if (owned && decoded.role == 'm')
		owned = !memcmp(page.payload.data, "UDBMAP4\0", 8) &&
				page.object == (uint64_t)udb_storage_stage_block_index(decoded.block) + 1;
	else if (owned)
		owned = page.generation == 1 && page.object == 1 && page.payload.length == 48 &&
				!memcmp(page.payload.data, "UDBTMP4\0", 8) && !memcmp(page.payload.data + 24, decoded.nonce, 16) &&
				page.payload.data[40] == decoded.block && page.payload.data[41] == (unsigned char)decoded.role &&
				udb_storage_zero(page.payload.data + 42, 6);
	result = UDB_STORAGE_OK;
	if (owned)
	{
#ifdef _WIN32
		FILE_DISPOSITION_INFO disposition;
		disposition.DeleteFile = TRUE;
		if (!SetFileInformationByHandle(file.handle, FileDispositionInfo, &disposition, sizeof(disposition)))
			result = UDB_STORAGE_ERROR;
#else
		struct stat held, named;
		if (fstat(file.handle, &held) || fstatat(directory->handle, name, &named, AT_SYMLINK_NOFOLLOW) ||
			held.st_dev != named.st_dev || held.st_ino != named.st_ino || !S_ISREG(named.st_mode) ||
			named.st_nlink != 1)
			result = UDB_STORAGE_ERROR;
		else if (unlinkat(directory->handle, name, 0) || !udb_storage_posix_flush(directory->handle))
			result = UDB_STORAGE_ERROR;
#endif
	}
	if (udb_storage_file_close(&file) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_storage_orphans_cleanup(UdbStorageDirectory *directory)
{
	if (!directory || !directory->opened || udb_storage_zero(directory->store_identity, 16))
		return UDB_STORAGE_ERROR;
	/* Reserve a bounded directory-enumerator scratch allowance in addition to
	 * our explicit page buffer; no filename inventory is retained. */
	if (udb_storage_budget_charge(directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE) != UDB_STORAGE_OK)
		return UDB_STORAGE_LIMIT;
	unsigned char *bytes = udb_storage_alloc(directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	if (!bytes)
	{
		udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
		return UDB_STORAGE_LIMIT;
	}
	UdbStorageResult result = UDB_STORAGE_OK;
#ifdef _WIN32
	wchar_t pattern[UDB_STORAGE_IO_PATH_MAX];
	size_t length = wcslen(directory->path);
	if (length + 4 > UDB_STORAGE_IO_PATH_MAX)
		result = UDB_STORAGE_LIMIT;
	else
	{
		memcpy(pattern, directory->path, length * sizeof(wchar_t));
		memcpy(pattern + length, L"\\*", 3 * sizeof(wchar_t));
		WIN32_FIND_DATAW entry;
		HANDLE search = FindFirstFileW(pattern, &entry);
		if (search == INVALID_HANDLE_VALUE)
			result = GetLastError() == ERROR_FILE_NOT_FOUND ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
		else
		{
			do
			{
				char name[46];
				size_t i = 0;
				while (i < 45 && entry.cFileName[i] && entry.cFileName[i] < 128)
				{
					name[i] = (char)entry.cFileName[i];
					i++;
				}
				if (entry.cFileName[i])
					continue;
				name[i] = 0;
				result = udb_storage_orphan_remove(directory, name, bytes);
			} while (result == UDB_STORAGE_OK && FindNextFileW(search, &entry));
			if (result == UDB_STORAGE_OK && GetLastError() != ERROR_NO_MORE_FILES)
				result = UDB_STORAGE_ERROR;
			if (!FindClose(search))
				result = UDB_STORAGE_ERROR;
		}
	}
#else
	int fd = openat(directory->handle, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	DIR *search = fd >= 0 ? fdopendir(fd) : NULL;
	if (!search)
	{
		if (fd >= 0)
			close(fd);
		result = UDB_STORAGE_ERROR;
	}
	else
	{
		for (;;)
		{
			errno = 0;
			struct dirent *entry = readdir(search);
			if (!entry)
			{
				if (errno)
					result = UDB_STORAGE_ERROR;
				break;
			}
			result = udb_storage_orphan_remove(directory, entry->d_name, bytes);
			if (result != UDB_STORAGE_OK)
				break;
		}
		if (closedir(search))
			result = UDB_STORAGE_ERROR;
	}
#endif
	udb_storage_free(bytes);
	udb_storage_budget_uncharge(directory->budget, UDB_STORAGE_MEMORY_AUX, UDB_STORAGE_PAGE_SIZE);
	return result;
}

/* End of udb_storage_orphans.c.inc */
/* Inlined: udb_storage_point.c.inc */
/* Private leaf-local delta updates. Existing runs are immutable. At most nine
 * runs participate in a visible merge; compaction preserves winning stamps.
 * Shape changes replay at most two sorted leaf views, never a resident block. */

typedef struct UdbStorageLeafSource
{
	UdbStorageCache *cache;
	UdbStorageRun points[2][UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun prefixes[UDB_STORAGE_MAX_RUNS + 1];
	unsigned int counts[2];
	unsigned int prefix_count;
	uint64_t watermark;
	unsigned char block;
} UdbStorageLeafSource;

typedef struct UdbStorageLeafSourceCursor
{
	UdbStorageLeafSource *source;
	UdbStorageLogicalPosition position;
	UdbStorageView *views[2];
	UdbStorageRecord rows[2];
	int present[2];
	int advance;
} UdbStorageLeafSourceCursor;

struct UdbStoragePointChange
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageBlockRoot root;
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageRecord record;
	unsigned char record_bytes[UDB_STORAGE_RECORD_MAX];
	unsigned char scratch[UDB_STORAGE_ROW_MAX];
	size_t record_length;
	UdbStorageLeafSource source_context;
	UdbStorageSource source;
	UdbStorageRebuild *rebuild;
	uint64_t next_object;
	uint64_t weight;
	unsigned int phase;
	UdbStorageResult failed;
	int finished;
};

static UdbStorageResult udb_storage_leaf_source_close(void **pointer)
{
	UdbStorageLeafSourceCursor *cursor;
	UdbStorageResult result = UDB_STORAGE_OK, closed;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	cursor = *pointer;
	for (unsigned int i = 0; i < 2; i++)
		if (cursor->views[i])
		{
			closed = udb_storage_view_close(cursor->views + i);
			if (closed != UDB_STORAGE_OK)
				result = closed;
		}
	udb_storage_free(cursor);
	*pointer = NULL;
	return result;
}

static UdbStorageResult udb_storage_leaf_source_open(void *opaque, const UdbStorageLogicalPosition *position,
													 void **out)
{
	UdbStorageLeafSource *source = opaque;
	UdbStorageLeafSourceCursor *cursor;
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!source || !udb_storage_logical_position_valid(position) || position->block != source->block)
		return UDB_STORAGE_ERROR;
	cursor = udb_storage_alloc(source->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*cursor));
	if (!cursor)
		return UDB_STORAGE_LIMIT;
	cursor->source = source;
	cursor->position = *position;
	cursor->advance = -1;
	for (unsigned int i = 0; i < 2; i++)
	{
		result = udb_storage_view_create_indexed(source->cache, source->points[i], source->counts[i], source->prefixes,
												 source->prefix_count, NULL, source->watermark, cursor->views + i);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_view_next_record(cursor->views[i], cursor->rows + i);
		if (result < UDB_STORAGE_NOT_FOUND)
			goto fail;
		cursor->present[i] = result == UDB_STORAGE_OK;
	}
	*out = cursor;
	return UDB_STORAGE_OK;
fail:
{
	void *handle = cursor;
	udb_storage_leaf_source_close(&handle);
}
	return result;
}

static UdbStorageResult udb_storage_leaf_source_next(void *opaque, UdbStorageRecord *record)
{
	UdbStorageLeafSourceCursor *cursor = opaque;
	UdbStorageResult result;
	unsigned char hash[32];
	unsigned int partition;
	if (!cursor || !record)
		return UDB_STORAGE_ERROR;
	for (;;)
	{
		if (cursor->advance >= 0)
		{
			unsigned int i = (unsigned int)cursor->advance;
			result = udb_storage_view_next_record(cursor->views[i], cursor->rows + i);
			if (result < UDB_STORAGE_NOT_FOUND)
				return result;
			cursor->present[i] = result == UDB_STORAGE_OK;
			cursor->advance = -1;
		}
		if (!cursor->present[0] && !cursor->present[1])
			return UDB_STORAGE_NOT_FOUND;
		int selected = cursor->present[0] ? 0 : 1;
		if (cursor->present[0] && cursor->present[1])
		{
			int compare = udb_storage_key_compare(&cursor->rows[0].row, &cursor->rows[1].row);
			if (!compare)
				return UDB_STORAGE_CORRUPT;
			selected = compare < 0 ? 0 : 1;
		}
		cursor->advance = selected;
		result = udb_storage_key_hash(&cursor->rows[selected].row, hash, &partition);
		if (result != UDB_STORAGE_OK)
			return result;
		if (!udb_storage_logical_contains(&cursor->position, hash, partition))
			continue;
		*record = cursor->rows[selected];
		return UDB_STORAGE_OK;
	}
}

static UdbStorageResult udb_storage_point_source(UdbStoragePointChange *change, const UdbStorageNode *left,
												 const UdbStorageNode *right)
{
	UdbStorageLeafSource *source = &change->source_context;
	UdbStorageResult result;
	memset(source, 0, sizeof(*source));
	source->cache = change->cache;
	source->watermark = change->root.watermark;
	source->block = change->root.block;
	source->prefix_count = change->root.prefix_count;
	source->counts[0] = left->run_count;
	result = udb_storage_block_points(change->cache, change->file, &change->root, left, source->points[0]);
	if (result == UDB_STORAGE_OK && right)
	{
		source->counts[1] = right->run_count;
		result = udb_storage_block_points(change->cache, change->file, &change->root, right, source->points[1]);
	}
	if (result == UDB_STORAGE_OK)
		result = udb_storage_block_prefixes(change->cache, change->file, &change->root, source->prefixes);
	if (result != UDB_STORAGE_OK)
		return result;
	change->source.context = source;
	change->source.open = udb_storage_leaf_source_open;
	change->source.next = udb_storage_leaf_source_next;
	change->source.close = udb_storage_leaf_source_close;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_point_close(UdbStoragePointChange **pointer)
{
	UdbStoragePointChange *change;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	change = *pointer;
	if (change->rebuild)
		result = udb_storage_rebuild_close(&change->rebuild);
	udb_storage_free(change);
	*pointer = NULL;
	return result;
}

static UdbStorageResult udb_storage_point_create(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageBlockRoot *root, uint64_t generation,
												 uint64_t next_object, const UdbStorageRecord *record,
												 UdbStoragePointChange **out)
{
	UdbStoragePointChange *change;
	UdbStorageRow old;
	UdbStorageResult result;
	size_t length;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || file->cache != cache || file->append_limit || !udb_storage_block_root_valid(root) ||
		!record || record->row.block != root->block ||
		(record->operation != UDB_STORAGE_PUT && record->operation != UDB_STORAGE_DELETE) ||
		record->stamp.sequence <= root->watermark || generation < root->generation || next_object < 8)
		return UDB_STORAGE_ERROR;
	change = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*change));
	if (!change)
		return UDB_STORAGE_LIMIT;
	change->cache = cache;
	change->file = file;
	change->root = *root;
	change->root.generation = generation;
	change->root.watermark = record->stamp.sequence;
	change->next_object = next_object;
	result =
		udb_storage_record_encode(record, change->record_bytes, sizeof(change->record_bytes), &change->record_length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_record_decode(change->record_bytes, change->record_length, &change->record);
	if (result != UDB_STORAGE_OK)
		goto fail;
	result = udb_storage_node_locate(cache, file, &root->catalog, &record->row, &change->position, &change->node);
	if (result == UDB_STORAGE_NOT_FOUND)
	{
		change->node.kind = UDB_STORAGE_LEAF;
		result = udb_storage_logical_empty(&change->position, &change->node.summary);
	}
	if (result != UDB_STORAGE_OK)
		goto fail;
	change->weight = change->node.summary.weight;
	result = udb_storage_block_lookup(cache, file, root, &record->row, change->scratch, sizeof(change->scratch), &old);
	if (result == UDB_STORAGE_OK)
	{
		unsigned char bytes[UDB_STORAGE_ROW_MAX];
		result = udb_storage_row_encode(&old, bytes, sizeof(bytes), &length);
		if (result != UDB_STORAGE_OK)
			goto fail;
		if (length + UINT64_C(128) > change->weight)
		{
			result = UDB_STORAGE_CORRUPT;
			goto fail;
		}
		change->weight -= length + UINT64_C(128);
	}
	else if (result != UDB_STORAGE_NOT_FOUND)
		goto fail;
	if (record->operation == UDB_STORAGE_PUT)
	{
		result = udb_storage_row_encode(&record->row, change->scratch, sizeof(change->scratch), &length);
		if (result != UDB_STORAGE_OK)
			goto fail;
		if (length + UINT64_C(128) > UINT64_MAX - change->weight)
		{
			result = UDB_STORAGE_LIMIT;
			goto fail;
		}
		change->weight += length + UINT64_C(128);
	}
	if (!change->node.run_count && record->operation == UDB_STORAGE_DELETE)
		change->phase = 5;
	*out = change;
	return UDB_STORAGE_OK;
fail:
	udb_storage_point_close(&change);
	return result;
}

static UdbStorageResult udb_storage_point_rebuild_start(UdbStoragePointChange *change, const UdbStorageNode *left,
														const UdbStorageNode *right)
{
	/* No data output occurs while establishing immutable input views. */
	udb_storage_file_job_limit(change->file, 0);
	UdbStorageResult result = udb_storage_point_source(change, left, right);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_rebuild_create(change->cache, change->file, &change->root, &change->position,
											change->root.generation, change->root.watermark, change->next_object,
											&change->source, &change->rebuild);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_rebuild_defer(change->rebuild);
	return result;
}

static UdbStorageResult udb_storage_point_leaf_hash(UdbStoragePointChange *change)
{
	UdbStorageLeafHasher *hasher = NULL;
	UdbStorageRecord record;
	UdbStorageSource source;
	void *handle = NULL;
	UdbStorageResult result, closed;
	size_t length;
	result = udb_storage_point_source(change, &change->node, NULL);
	if (result != UDB_STORAGE_OK)
		return result;
	source = change->source;
	result = source.open(source.context, &change->position, &handle);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_leaf_hasher_create(change->cache->budget, &change->position, &hasher);
	if (result != UDB_STORAGE_OK)
		goto done;
	while ((result = source.next(handle, &record)) == UDB_STORAGE_OK)
	{
		result = udb_storage_row_encode(&record.row, change->scratch, sizeof(change->scratch), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_leaf_hasher_add(hasher, change->scratch, length);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = udb_storage_leaf_hasher_finish(hasher, &change->node.summary);
	if (result == UDB_STORAGE_OK && change->node.summary.weight != change->weight)
		result = UDB_STORAGE_CORRUPT;
done:
	if (handle)
	{
		closed = source.close(&handle);
		if (result == UDB_STORAGE_OK)
			result = closed;
	}
	udb_storage_leaf_hasher_free(&hasher);
	return result;
}

static UdbStorageResult udb_storage_point_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
													void *opaque)
{
	UdbStoragePointChange *change = opaque;
	UdbStorageLogicalPosition position = {0};
	UdbStorageNode node;
	UdbStorageResult result;
	position.block = change->root.block;
	position.partition = (uint16_t)partition;
	result = udb_storage_node_get(change->cache, change->file, &change->root.catalog, &position, &node);
	if (result == UDB_STORAGE_OK)
		*summary = node.summary;
	return result;
}

static UdbStorageResult udb_storage_point_append(UdbStoragePointChange *change)
{
	UdbStorageRunBuilder *builder = NULL;
	UdbStorageRun run;
	UdbStorageResult result;
	UdbStorageCatalogRoot changed;
	if (change->next_object == UINT64_MAX || change->node.run_count >= UDB_STORAGE_MAX_RUNS + 1)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_run_builder_create(change->cache, change->file, change->root.block, UDB_STORAGE_POINTS,
											change->root.generation, change->next_object++, &builder);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_builder_add(builder, change->record_bytes, change->record_length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_builder_finish(builder, &run);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_save(change->cache, &run, &change->root.catalog, change->root.generation, &changed);
	udb_storage_run_builder_free(&builder);
	if (result != UDB_STORAGE_OK)
		return result;
	change->root.catalog = changed;
	change->node.runs[change->node.run_count++] = run.directory.object;
	if (change->weight > UDB_STORAGE_LEAF_WEIGHT_MAX)
	{
		/* Retain the old valid summary solely to describe this private run set.
		 * It is never published; rebuild supplies the canonical split shape. */
		result = udb_storage_node_change(change->cache, change->file, &change->root.catalog, change->root.generation,
										 &change->position, &change->node, &changed);
		if (result == UDB_STORAGE_OK)
			change->root.catalog = changed;
		change->phase = 3;
		return result;
	}
	result = udb_storage_point_leaf_hash(change);
	if (result != UDB_STORAGE_OK)
		return result;
	if (change->node.summary.count)
		result = udb_storage_node_change(change->cache, change->file, &change->root.catalog, change->root.generation,
										 &change->position, &change->node, &changed);
	else
	{
		unsigned char key[64];
		for (unsigned int i = 0; i < change->node.run_count; i++)
		{
			udb_storage_run_manifest_key(change->root.block, UDB_STORAGE_POINTS, change->node.runs[i], key);
			result = udb_storage_catalog_change(change->cache, change->file, &change->root.catalog,
												change->root.generation, key, NULL, &changed);
			if (result != UDB_STORAGE_OK)
				return result;
			change->root.catalog = changed;
		}
		udb_storage_node_key(&change->position, key);
		result = udb_storage_catalog_change(change->cache, change->file, &change->root.catalog, change->root.generation,
											key, NULL, &changed);
	}
	if (result == UDB_STORAGE_OK)
		change->root.catalog = changed;
	change->phase = 4;
	return result;
}

static UdbStorageResult udb_storage_point_ancestor(UdbStoragePointChange *change)
{
	UdbStorageLogicalPosition parent, children[2];
	UdbStorageNode nodes[2], branch = {0};
	UdbStorageCatalogRoot changed;
	UdbStorageResult result;
	if (!change->position.depth)
	{
		change->phase = 5;
		return UDB_STORAGE_OK;
	}
	parent = change->position;
	parent.depth--;
	parent.prefix[parent.depth / 8] &= (unsigned char)~(0x80U >> (parent.depth % 8));
	for (unsigned int i = 0; i < 2; i++)
	{
		result = udb_storage_logical_child(&parent, i, children + i);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_node_get(change->cache, change->file, &change->root.catalog, children + i, nodes + i);
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			memset(nodes + i, 0, sizeof(nodes[i]));
			nodes[i].kind = UDB_STORAGE_LEAF;
			result = udb_storage_logical_empty(children + i, &nodes[i].summary);
		}
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (nodes[0].summary.weight > UINT64_MAX - nodes[1].summary.weight)
		return UDB_STORAGE_LIMIT;
	change->position = parent;
	if (nodes[0].summary.weight + nodes[1].summary.weight <= UDB_STORAGE_LEAF_WEIGHT_MAX)
	{
		if (nodes[0].kind != UDB_STORAGE_LEAF || nodes[1].kind != UDB_STORAGE_LEAF)
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_point_rebuild_start(change, nodes, nodes + 1);
		change->phase = 3;
		return result;
	}
	branch.kind = UDB_STORAGE_BRANCH;
	result = udb_storage_logical_branch(&parent, &nodes[0].summary, &nodes[1].summary, &branch.summary);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_node_change(change->cache, change->file, &change->root.catalog, change->root.generation,
										 &parent, &branch, &changed);
	if (result == UDB_STORAGE_OK)
		change->root.catalog = changed;
	return result;
}

static UdbStorageResult udb_storage_point_step(UdbStoragePointChange *change, UdbStorageBlockRoot *root,
											   uint64_t *next_object)
{
	UdbStorageResult result;
	uint64_t size;
	if (!change || !root || !next_object || change->file->append_limit)
		return UDB_STORAGE_ERROR;
	if (change->failed)
		return change->failed;
	if (change->finished)
	{
		*root = change->root;
		*next_object = change->next_object;
		return UDB_STORAGE_OK;
	}
	if (!change->phase)
	{
		change->phase = change->node.run_count == UDB_STORAGE_MAX_RUNS + 1 ? 1 : 2;
		if (change->phase == 1)
		{
			result = udb_storage_point_rebuild_start(change, &change->node, NULL);
			if (result != UDB_STORAGE_OK)
				return change->failed = result;
		}
	}
	if (change->phase == 1 || change->phase == 3)
	{
		if (!change->rebuild)
		{
			result = udb_storage_point_rebuild_start(change, &change->node, NULL);
			if (result != UDB_STORAGE_OK)
				return change->failed = result;
		}
		result = udb_storage_rebuild_step(change->rebuild, &change->root, &change->next_object);
		if (result == UDB_STORAGE_BUSY)
			return result;
		if (result != UDB_STORAGE_OK)
			return change->failed = result;
		result = udb_storage_rebuild_close(&change->rebuild);
		if (result != UDB_STORAGE_OK)
			return change->failed = result;
		if (change->phase == 1)
		{
			result = udb_storage_node_get(change->cache, change->file, &change->root.catalog, &change->position,
										  &change->node);
			if (result == UDB_STORAGE_NOT_FOUND)
			{
				memset(&change->node, 0, sizeof(change->node));
				change->node.kind = UDB_STORAGE_LEAF;
				result = udb_storage_logical_empty(&change->position, &change->node.summary);
			}
			if (result != UDB_STORAGE_OK)
				return change->failed = result;
			change->phase = 2;
		}
		else
			change->phase = 4;
		return UDB_STORAGE_BUSY;
	}
	result = udb_storage_file_size(change->file, &size);
	if (result != UDB_STORAGE_OK)
		return change->failed = result;
	if (!udb_storage_io_range_valid(size, UDB_STORAGE_COMPACTION_MAX))
		return change->failed = UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(change->file, size + UDB_STORAGE_COMPACTION_MAX);
	if (change->phase == 2)
		result = udb_storage_point_append(change);
	else if (change->phase == 4)
		result = udb_storage_point_ancestor(change);
	else
	{
		result =
			udb_storage_logical_block(change->root.block, udb_storage_point_partition, change, &change->root.summary);
		if (result == UDB_STORAGE_OK)
			change->finished = 1;
	}
	udb_storage_file_job_limit(change->file, 0);
	if (result != UDB_STORAGE_OK)
		return change->failed = result;
	if (!change->finished)
		return UDB_STORAGE_BUSY;
	*root = change->root;
	*next_object = change->next_object;
	return UDB_STORAGE_OK;
}

/* End of udb_storage_point.c.inc */
/* Inlined: udb_storage_prefix.c.inc */
/* Ordered prefix deletion with bounded private partition rebuilds. A tombstone
 * is retired only after every potentially covered partition has a baseline
 * materialized through its watermark. Old generation pins retain old catalogs.
 * All intermediate roots remain private to the one writer transaction. */

struct UdbStoragePrefixChange
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageBlockRoot root;
	UdbStorageRecord record;
	unsigned char bytes[UDB_STORAGE_RECORD_MAX];
	size_t length;
	UdbStorageSort *sort;
	UdbStorageBlockIterator *iterator;
	UdbStorageRebuild *rebuild;
	UdbStorageSource source;
	uint64_t next_object;
	uint64_t tombstone;
	unsigned int partition;
	unsigned int end;
	unsigned int phase;
	UdbStorageResult failed;
	int finished;
};

static UdbStorageResult udb_storage_prefix_close(UdbStoragePrefixChange **pointer)
{
	UdbStoragePrefixChange *change;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	change = *pointer;
	if (change->rebuild)
	{
		result = udb_storage_rebuild_close(&change->rebuild);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (change->iterator)
	{
		result = udb_storage_block_iterator_close(&change->iterator);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (change->sort)
	{
		result = udb_storage_sort_close(&change->sort);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	udb_storage_free(change);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_prefix_create(UdbStorageCache *cache, UdbStorageFile *file,
												  const UdbStorageBlockRoot *root, uint64_t generation,
												  uint64_t next_object, const UdbStorageRecord *record,
												  UdbStoragePrefixChange **out)
{
	UdbStoragePrefixChange *change;
	UdbStorageResult result;
	unsigned char hash[32];
	unsigned int partition;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || file->cache != cache || file->append_limit || !udb_storage_block_root_valid(root) ||
		!record || record->row.block != root->block || record->operation != UDB_STORAGE_PREFIX_DELETE ||
		record->stamp.sequence <= root->watermark || generation < root->generation || next_object < 8)
		return UDB_STORAGE_ERROR;
	if (root->prefix_count == UDB_STORAGE_MAX_RUNS + 1)
		return UDB_STORAGE_LIMIT;
	change = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*change));
	if (!change)
		return UDB_STORAGE_LIMIT;
	change->cache = cache;
	change->file = file;
	change->root = *root;
	change->root.generation = generation;
	change->root.watermark = record->stamp.sequence;
	change->next_object = next_object;
	result = udb_storage_record_encode(record, change->bytes, sizeof(change->bytes), &change->length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_record_decode(change->bytes, change->length, &change->record);
	if (result != UDB_STORAGE_OK)
		goto fail;
	if (root->block == 'K' && record->row.depth == 1)
		change->end = UDB_STORAGE_BASE_PARTITIONS;
	else
	{
		result = udb_storage_key_hash(&record->row, hash, &partition);
		if (result != UDB_STORAGE_OK)
			goto fail;
		change->partition = partition;
		change->end = partition + 1;
	}
	*out = change;
	return UDB_STORAGE_OK;
fail:
	udb_storage_prefix_close(&change);
	return result;
}

static UdbStorageResult udb_storage_prefix_append(UdbStoragePrefixChange *change)
{
	UdbStorageRunBuilder *builder = NULL;
	UdbStorageRun run;
	UdbStorageCatalogRoot changed;
	UdbStorageResult result;
	uint64_t size;
	if (change->next_object == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_file_size(change->file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (!udb_storage_io_range_valid(size, UDB_STORAGE_COMPACTION_MAX))
		return UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(change->file, size + UDB_STORAGE_COMPACTION_MAX);
	change->tombstone = change->next_object++;
	result = udb_storage_run_builder_create(change->cache, change->file, change->root.block, UDB_STORAGE_PREFIXES,
											change->root.generation, change->tombstone, &builder);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_builder_add(builder, change->bytes, change->length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_builder_finish(builder, &run);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_run_save(change->cache, &run, &change->root.catalog, change->root.generation, &changed);
	udb_storage_run_builder_free(&builder);
	udb_storage_file_job_limit(change->file, 0);
	if (result != UDB_STORAGE_OK)
		return result;
	change->root.catalog = changed;
	change->root.prefixes[change->root.prefix_count++] = change->tombstone;
	change->phase = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_prefix_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
													 void *opaque)
{
	UdbStoragePrefixChange *change = opaque;
	UdbStorageLogicalPosition position = {0};
	UdbStorageNode node;
	UdbStorageResult result;
	position.block = change->root.block;
	position.partition = (uint16_t)partition;
	result = udb_storage_node_get(change->cache, change->file, &change->root.catalog, &position, &node);
	if (result == UDB_STORAGE_OK)
		*summary = node.summary;
	return result;
}

static UdbStorageResult udb_storage_prefix_step(UdbStoragePrefixChange *change, UdbStorageBlockRoot *root,
												uint64_t *next_object)
{
	UdbStorageResult result;
	if (!change || !root || !next_object || change->file->append_limit)
		return UDB_STORAGE_ERROR;
	if (change->failed)
		return change->failed;
	if (change->finished)
	{
		*root = change->root;
		*next_object = change->next_object;
		return UDB_STORAGE_OK;
	}
	if (!change->phase)
	{
		result = udb_storage_prefix_append(change);
		goto done;
	}
	if (change->partition < change->end)
	{
		UdbStorageLogicalPosition position = {0};
		position.block = change->root.block;
		position.partition = (uint16_t)change->partition;
		if (change->phase == 1)
		{
			UdbStorageNode node;
			result = udb_storage_node_get(change->cache, change->file, &change->root.catalog, &position, &node);
			if (result == UDB_STORAGE_NOT_FOUND)
			{
				change->partition++;
				return UDB_STORAGE_BUSY;
			}
			if (result != UDB_STORAGE_OK)
				goto done;
			result = udb_storage_sort_create(change->file->directory, change->root.block,
											 node.summary.count ? node.summary.count : 1,
											 UINT64_C(16) * 1024 * 1024 * 1024, NULL, NULL, &change->sort);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_block_partition_iterator(change->cache, change->file, &change->root,
															  change->partition, &change->iterator);
			if (result != UDB_STORAGE_OK)
				goto done;
			change->phase = 2;
		}
		if (change->phase == 2)
		{
			UdbStorageRecord record;
			unsigned char bytes[UDB_STORAGE_RECORD_MAX];
			size_t length;
			/* A finite ingest batch bounds both CPU and aggregate spill output
			 * below 64 MiB even for maximal canonical records. */
			for (unsigned int i = 0; i < 1024; i++)
			{
				result = udb_storage_block_iterator_next(change->iterator, &record);
				if (result == UDB_STORAGE_NOT_FOUND)
				{
					result = udb_storage_block_iterator_close(&change->iterator);
					if (result == UDB_STORAGE_OK)
						result = udb_storage_sort_seal(change->sort);
					if (result == UDB_STORAGE_OK)
						change->phase = 3;
					goto done;
				}
				if (result != UDB_STORAGE_OK)
					goto done;
				result = udb_storage_record_encode(&record, bytes, sizeof(bytes), &length);
				if (result == UDB_STORAGE_OK)
					result = udb_storage_sort_add(change->sort, bytes, length);
				if (result != UDB_STORAGE_OK)
					goto done;
			}
			return UDB_STORAGE_BUSY;
		}
		if (change->phase == 3)
		{
			result = udb_storage_sort_step(change->sort);
			if (result == UDB_STORAGE_BUSY)
				return result;
			if (result != UDB_STORAGE_OK)
				goto done;
			result = udb_storage_sort_source(change->sort, 0, 0, &change->source);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_rebuild_create(change->cache, change->file, &change->root, &position,
													change->root.generation, change->root.watermark,
													change->next_object, &change->source, &change->rebuild);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_rebuild_defer(change->rebuild);
			if (result == UDB_STORAGE_OK)
				change->phase = 4;
			goto done;
		}
		result = udb_storage_rebuild_step(change->rebuild, &change->root, &change->next_object);
		if (result == UDB_STORAGE_BUSY)
			return result;
		if (result != UDB_STORAGE_OK)
			goto done;
		result = udb_storage_rebuild_close(&change->rebuild);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_sort_close(&change->sort);
		if (result == UDB_STORAGE_OK)
		{
			change->partition++;
			change->phase = 1;
		}
		goto done;
	}
	{
		unsigned char key[64];
		uint64_t size;
		UdbStorageCatalogRoot changed;
		result = udb_storage_file_size(change->file, &size);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (!udb_storage_io_range_valid(size, UDB_STORAGE_COMPACTION_MAX))
		{
			result = UDB_STORAGE_LIMIT;
			goto done;
		}
		udb_storage_file_job_limit(change->file, size + UDB_STORAGE_COMPACTION_MAX);
		udb_storage_run_manifest_key(change->root.block, UDB_STORAGE_PREFIXES, change->tombstone, key);
		result = udb_storage_catalog_change(change->cache, change->file, &change->root.catalog, change->root.generation,
											key, NULL, &changed);
		udb_storage_file_job_limit(change->file, 0);
		if (result != UDB_STORAGE_OK)
			goto done;
		change->root.catalog = changed;
		if (!change->root.prefix_count || change->root.prefixes[change->root.prefix_count - 1] != change->tombstone)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		change->root.prefixes[--change->root.prefix_count] = 0;
		result =
			udb_storage_logical_block(change->root.block, udb_storage_prefix_partition, change, &change->root.summary);
		if (result == UDB_STORAGE_OK)
			change->finished = 1;
	}
done:
	if (result != UDB_STORAGE_OK)
		return change->failed = result;
	if (!change->finished)
		return UDB_STORAGE_BUSY;
	*root = change->root;
	*next_object = change->next_object;
	return UDB_STORAGE_OK;
}

/* End of udb_storage_prefix.c.inc */
/* Inlined: udb_storage_queries.c.inc */
/* Typed reads over an engine-pinned, fully validated block generation.
 * Iteration order is logical partition/leaf order, then canonical key order
 * within each leaf; it is deliberately not a global key sort. */

struct UdbStorageBlockIterator
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageBlockRoot root;
	UdbStorageCatalogIterator *catalog;
	UdbStorageView *view;
	UdbStorageRun points[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun prefixes[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRow prefix;
	unsigned char bytes[UDB_STORAGE_KEY_MAX];
	UdbStorageResult failed;
	int has_prefix;
	int finished;
};

static UdbStorageResult udb_storage_block_prefixes(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageBlockRoot *root,
												   UdbStorageRun runs[UDB_STORAGE_MAX_RUNS + 1])
{
	for (unsigned int i = 0; i < root->prefix_count; i++)
	{
		UdbStorageResult result = udb_storage_run_load(cache, file, &root->catalog, root->block, UDB_STORAGE_PREFIXES,
													   root->prefixes[i], runs + i);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_block_points(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageBlockRoot *root, const UdbStorageNode *node,
												 UdbStorageRun runs[UDB_STORAGE_MAX_RUNS + 1])
{
	for (unsigned int i = 0; i < node->run_count; i++)
	{
		UdbStorageResult result =
			udb_storage_run_load(cache, file, &root->catalog, root->block, UDB_STORAGE_POINTS, node->runs[i], runs + i);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

typedef struct UdbStorageBlockLookup
{
	UdbStorageRun points[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun prefixes[UDB_STORAGE_MAX_RUNS + 1];
} UdbStorageBlockLookup;

static UdbStorageResult udb_storage_block_lookup(UdbStorageCache *cache, UdbStorageFile *file,
												 const UdbStorageBlockRoot *root, const UdbStorageRow *key,
												 unsigned char *bytes, size_t capacity, UdbStorageRow *row)
{
	UdbStorageBlockLookup *lookup;
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageResult result;
	if (!cache || !file || file->cache != cache || !udb_storage_block_root_valid(root) || !udb_storage_row_valid(key) ||
		key->block != root->block || !bytes || !row)
		return UDB_STORAGE_ERROR;
	result = udb_storage_node_locate(cache, file, &root->catalog, key, &position, &node);
	if (result != UDB_STORAGE_OK)
		return result;
	lookup = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*lookup));
	if (!lookup)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_block_points(cache, file, root, &node, lookup->points);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_block_prefixes(cache, file, root, lookup->prefixes);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_lookup(cache, lookup->points, node.run_count, lookup->prefixes, root->prefix_count, key,
									root->watermark, bytes, capacity, row);
	udb_storage_free(lookup);
	return result;
}

static UdbStorageResult udb_storage_node_position_decode(const unsigned char key[64],
														 UdbStorageLogicalPosition *position)
{
	UdbStorageLogicalPosition candidate;
	unsigned char canonical[64];
	memset(&candidate, 0, sizeof(candidate));
	candidate.block = key[0];
	candidate.partition = (uint16_t)((key[2] << 8) | key[3]);
	candidate.depth = (uint16_t)((key[4] << 8) | key[5]);
	memcpy(candidate.prefix, key + 6, 32);
	if (!udb_storage_logical_position_valid(&candidate))
		return UDB_STORAGE_CORRUPT;
	udb_storage_node_key(&candidate, canonical);
	if (memcmp(key, canonical, 64))
		return UDB_STORAGE_CORRUPT;
	*position = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_block_iterator_close(UdbStorageBlockIterator **pointer)
{
	UdbStorageBlockIterator *iterator;
	UdbStorageResult result = UDB_STORAGE_OK, closed;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	iterator = *pointer;
	if (iterator->view)
		result = udb_storage_view_close(&iterator->view);
	if (iterator->catalog)
	{
		closed = udb_storage_catalog_iterator_close(&iterator->catalog);
		if (closed != UDB_STORAGE_OK && result == UDB_STORAGE_OK)
			result = closed;
	}
	udb_storage_free(iterator);
	*pointer = NULL;
	return result;
}

static UdbStorageResult udb_storage_block_iterator_create(UdbStorageCache *cache, UdbStorageFile *file,
														  const UdbStorageBlockRoot *root, const UdbStorageRow *prefix,
														  UdbStorageBlockIterator **out)
{
	UdbStorageBlockIterator *iterator;
	UdbStorageResult result;
	unsigned char lower[64] = {0}, upper[64] = {0}, hash[32];
	unsigned int partition;
	size_t length;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || file->cache != cache || !udb_storage_block_root_valid(root) ||
		(prefix && (!udb_storage_row_valid(prefix) || prefix->block != root->block)))
		return UDB_STORAGE_ERROR;
	iterator = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*iterator));
	if (!iterator)
		return UDB_STORAGE_LIMIT;
	iterator->cache = cache;
	iterator->file = file;
	iterator->root = *root;
	lower[0] = upper[0] = root->block;
	lower[1] = 1;
	upper[1] = 2;
	if (prefix)
	{
		result = udb_storage_run_copy_key(prefix, iterator->bytes, &length);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_row_decode(iterator->bytes, length, &iterator->prefix);
		if (result != UDB_STORAGE_OK)
			goto fail;
		iterator->has_prefix = 1;
		/* K type-only prefixes cover multiple entity partitions. */
		if (prefix->block != 'K' || prefix->depth >= 2)
		{
			result = udb_storage_key_hash(prefix, hash, &partition);
			if (result != UDB_STORAGE_OK)
				goto fail;
			lower[2] = upper[2] = (unsigned char)(partition >> 8);
			lower[3] = upper[3] = (unsigned char)partition;
			upper[1] = 1;
			if (++upper[3] == 0)
				upper[2]++;
		}
	}
	result = udb_storage_block_prefixes(cache, file, root, iterator->prefixes);
	if (result != UDB_STORAGE_OK)
		goto fail;
	result = udb_storage_catalog_iterator_create(cache, file, &root->catalog, lower, upper, &iterator->catalog);
	if (result != UDB_STORAGE_OK)
		goto fail;
	*out = iterator;
	return UDB_STORAGE_OK;
fail:
	udb_storage_block_iterator_close(&iterator);
	return result;
}

static UdbStorageResult udb_storage_block_iterator_next(UdbStorageBlockIterator *iterator, UdbStorageRecord *record)
{
	UdbStorageResult result;
	unsigned char key[64], value[128];
	if (!iterator || !record || udb_storage_overlaps(record, sizeof(*record), iterator, sizeof(*iterator)))
		return UDB_STORAGE_ERROR;
	if (iterator->failed)
		return iterator->failed;
	if (iterator->finished)
		return UDB_STORAGE_NOT_FOUND;
	for (;;)
	{
		UdbStorageLogicalPosition position;
		UdbStorageNode node;
		if (iterator->view)
		{
			result = udb_storage_view_next_record(iterator->view, record);
			if (result == UDB_STORAGE_OK)
				return result;
			if (result != UDB_STORAGE_NOT_FOUND)
				return iterator->failed = result;
			result = udb_storage_view_close(&iterator->view);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
		}
		result = udb_storage_catalog_iterator_next(iterator->catalog, key, value);
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			iterator->finished = 1;
			return result;
		}
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
		result = udb_storage_node_position_decode(key, &position);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_node_decode(&position, value, &node);
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
		if (node.kind != UDB_STORAGE_LEAF || !node.summary.count)
			continue;
		result = udb_storage_block_points(iterator->cache, iterator->file, &iterator->root, &node, iterator->points);
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
		result = udb_storage_view_create_indexed(
			iterator->cache, iterator->points, node.run_count, iterator->prefixes, iterator->root.prefix_count,
			iterator->has_prefix ? &iterator->prefix : NULL, iterator->root.watermark, &iterator->view);
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
	}
}

static UdbStorageResult udb_storage_block_exists_prefix(UdbStorageCache *cache, UdbStorageFile *file,
														const UdbStorageBlockRoot *root, const UdbStorageRow *prefix)
{
	UdbStorageBlockIterator *iterator;
	UdbStorageRecord record;
	UdbStorageResult result, closed;
	if (!prefix)
		return UDB_STORAGE_ERROR;
	result = udb_storage_block_iterator_create(cache, file, root, prefix, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_block_iterator_next(iterator, &record);
	closed = udb_storage_block_iterator_close(&iterator);
	return closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND ? closed : result;
}

static UdbStorageResult udb_storage_block_partition_iterator(UdbStorageCache *cache, UdbStorageFile *file,
															 const UdbStorageBlockRoot *root, unsigned int partition,
															 UdbStorageBlockIterator **out)
{
	UdbStorageResult result;
	unsigned char lower[64] = {0}, upper[64] = {0};
	if (!out || partition >= UDB_STORAGE_BASE_PARTITIONS)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	result = udb_storage_block_iterator_create(cache, file, root, NULL, out);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_catalog_iterator_close(&(*out)->catalog);
	if (result != UDB_STORAGE_OK)
		goto fail;
	lower[0] = upper[0] = root->block;
	lower[1] = upper[1] = 1;
	lower[2] = upper[2] = (unsigned char)(partition >> 8);
	lower[3] = upper[3] = (unsigned char)partition;
	if (++upper[3] == 0)
		upper[2]++;
	result = udb_storage_catalog_iterator_create(cache, file, &root->catalog, lower, upper, &(*out)->catalog);
	if (result == UDB_STORAGE_OK)
		return result;
fail:
	udb_storage_block_iterator_close(out);
	return result;
}

/* End of udb_storage_queries.c.inc */
/* Inlined: udb_storage_raw.c.inc */
/* Private external-sort streams: fixed paged records ordered by entity
 * partition, then canonical full key. These are never active block containers
 * and have no compatibility reader. DATA offset tables use the shared codec. */

static int udb_storage_raw_valid(const UdbStorageRawRun *run)
{
	return run && udb_storage_block_valid(run->block) && run->generation && run->object &&
		   !(run->start % UDB_STORAGE_PAGE_SIZE) && !(run->length % UDB_STORAGE_PAGE_SIZE) &&
		   udb_storage_io_range_valid(run->start, (size_t)run->length) &&
		   ((!run->count && !run->length) ||
			(run->count && run->length &&
			 run->count <= run->length / UDB_STORAGE_PAGE_SIZE * UDB_STORAGE_DATA_ROWS_MAX));
}

static UdbStorageResult udb_storage_raw_encode(const UdbStorageRawRun *run, unsigned char bytes[128])
{
	if (!bytes || !udb_storage_raw_valid(run) || udb_storage_overlaps(bytes, 128, run, sizeof(*run)))
		return UDB_STORAGE_ERROR;
	memset(bytes, 0, 128);
	memcpy(bytes, "UDBSORT4", 8);
	bytes[8] = run->block;
	udb_storage_put_u64(bytes + 16, run->generation);
	udb_storage_put_u64(bytes + 24, run->object);
	udb_storage_put_u64(bytes + 32, run->start);
	udb_storage_put_u64(bytes + 40, run->length);
	udb_storage_put_u64(bytes + 48, run->count);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_decode(const unsigned char bytes[128], UdbStorageRawRun *run)
{
	UdbStorageRawRun candidate;
	if (!bytes || !run || udb_storage_overlaps(bytes, 128, run, sizeof(*run)))
		return UDB_STORAGE_ERROR;
	if (memcmp(bytes, "UDBSORT4", 8) || !udb_storage_zero(bytes + 9, 7) || !udb_storage_zero(bytes + 56, 72))
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.block = bytes[8];
	candidate.generation = udb_storage_get_u64(bytes + 16);
	candidate.object = udb_storage_get_u64(bytes + 24);
	candidate.start = udb_storage_get_u64(bytes + 32);
	candidate.length = udb_storage_get_u64(bytes + 40);
	candidate.count = udb_storage_get_u64(bytes + 48);
	if (!udb_storage_raw_valid(&candidate))
		return UDB_STORAGE_CORRUPT;
	*run = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_partition(const UdbStorageRecord *record, unsigned int *partition)
{
	unsigned char hash[32];
	if (record->operation != UDB_STORAGE_PUT)
		return UDB_STORAGE_CORRUPT;
	return udb_storage_key_hash(&record->row, hash, partition);
}

struct UdbStorageRawWriter
{
	UdbStorageFile *file;
	UdbStorageRawRun run;
	unsigned char rows[UDB_STORAGE_PAGE_PAYLOAD];
	unsigned char page[65536];
	unsigned char payload[UDB_STORAGE_PAGE_PAYLOAD];
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	uint32_t offsets[UDB_STORAGE_DATA_ROWS_MAX + 1];
	size_t previous_length;
	size_t used;
	unsigned int partition;
	unsigned int pending;
	UdbStorageResult failed;
	int finished;
};

static void udb_storage_raw_writer_close(UdbStorageRawWriter **pointer)
{
	if (!pointer || !*pointer)
		return;
	udb_storage_free(*pointer);
	*pointer = NULL;
}

static UdbStorageResult udb_storage_raw_writer_create(UdbStorageFile *file, unsigned char block, uint64_t generation,
													  uint64_t object, UdbStorageRawWriter **out)
{
	UdbStorageRawWriter *writer;
	UdbStorageResult result;
	uint64_t size;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!file || !file->opened || !generation || !object || !udb_storage_block_valid(block))
		return UDB_STORAGE_ERROR;
	result = udb_storage_file_size(file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	writer = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*writer));
	if (!writer)
		return UDB_STORAGE_LIMIT;
	writer->file = file;
	writer->run.block = block;
	writer->run.generation = generation;
	writer->run.object = object;
	writer->run.start = size;
	*out = writer;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_writer_flush(UdbStorageRawWriter *writer)
{
	UdbStoragePage page;
	UdbStorageResult result;
	size_t base = 8 + 4 * (writer->pending + 1);
	if (!writer->pending)
		return UDB_STORAGE_OK;
	memset(&page, 0, sizeof(page));
	page.kind = UDB_STORAGE_PAGE_DATA;
	page.generation = writer->run.generation;
	page.object = writer->run.object;
	page.ordinal = (writer->run.start + writer->run.length) / UDB_STORAGE_PAGE_SIZE;
	page.payload.data = writer->payload;
	page.payload.length = (uint32_t)(base + writer->used);
	udb_storage_put_u32(writer->payload, writer->pending);
	udb_storage_put_u32(writer->payload + 4, 0);
	for (unsigned int i = 0; i <= writer->pending; i++)
		udb_storage_put_u32(writer->payload + 8 + 4 * i, (uint32_t)base + writer->offsets[i]);
	memcpy(writer->payload + base, writer->rows, writer->used);
	result = udb_storage_page_encode(&page, writer->page, sizeof(writer->page));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(writer->file, writer->run.start + writer->run.length, writer->page,
										sizeof(writer->page));
	if (result != UDB_STORAGE_OK)
		return result;
	writer->run.length += UDB_STORAGE_PAGE_SIZE;
	writer->pending = 0;
	writer->used = 0;
	writer->offsets[0] = 0;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_writer_add(UdbStorageRawWriter *writer, const unsigned char *bytes,
												   size_t length)
{
	UdbStorageRecord record;
	UdbStorageRow previous;
	UdbStorageResult result;
	unsigned int partition;
	if (!writer || writer->finished || !bytes || udb_storage_overlaps(bytes, length, writer, sizeof(*writer)))
		return UDB_STORAGE_ERROR;
	if (writer->failed)
		return writer->failed;
	result = udb_storage_record_decode(bytes, length, &record);
	if (result != UDB_STORAGE_OK)
		return writer->failed = result;
	if (record.row.block != writer->run.block)
		return writer->failed = UDB_STORAGE_CORRUPT;
	result = udb_storage_raw_partition(&record, &partition);
	if (result != UDB_STORAGE_OK)
		return writer->failed = result;
	if (writer->previous_length)
	{
		result = udb_storage_row_decode(writer->previous, writer->previous_length, &previous);
		if (result != UDB_STORAGE_OK || partition < writer->partition ||
			(partition == writer->partition && udb_storage_key_compare(&previous, &record.row) >= 0))
			return writer->failed = UDB_STORAGE_CORRUPT;
	}
	if (writer->pending == UDB_STORAGE_DATA_ROWS_MAX ||
		8 + 4 * (writer->pending + 2) + writer->used + length > UDB_STORAGE_PAGE_PAYLOAD)
	{
		result = udb_storage_raw_writer_flush(writer);
		/* LIMIT at a job boundary leaves the incoming record unconsumed. */
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_LIMIT ? result : (writer->failed = result);
	}
	if (writer->run.count == UINT64_MAX || length > sizeof(writer->rows) - writer->used)
		return writer->failed = UDB_STORAGE_LIMIT;
	memcpy(writer->rows + writer->used, bytes, length);
	writer->used += length;
	writer->offsets[++writer->pending] = (uint32_t)writer->used;
	writer->run.count++;
	writer->partition = partition;
	result = udb_storage_run_copy_key(&record.row, writer->previous, &writer->previous_length);
	return result == UDB_STORAGE_OK ? result : (writer->failed = result);
}

static UdbStorageResult udb_storage_raw_writer_finish(UdbStorageRawWriter *writer, UdbStorageRawRun *run)
{
	UdbStorageResult result;
	if (!writer || !run || writer->finished || udb_storage_overlaps(run, sizeof(*run), writer, sizeof(*writer)))
		return UDB_STORAGE_ERROR;
	if (writer->failed)
		return writer->failed;
	result = udb_storage_raw_writer_flush(writer);
	if (result != UDB_STORAGE_OK)
		return result == UDB_STORAGE_LIMIT ? result : (writer->failed = result);
	writer->finished = 1;
	*run = writer->run;
	return UDB_STORAGE_OK;
}

struct UdbStorageRawReader
{
	UdbStorageFile *file;
	UdbStorageRawRun run;
	UdbStoragePage page;
	unsigned char bytes[65536];
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	size_t previous_length;
	uint64_t offset;
	uint64_t count;
	unsigned int slot;
	unsigned int seek_slot;
	unsigned int partition;
	UdbStorageResult failed;
	int loaded;
	int finished;
};

static void udb_storage_raw_reader_close(UdbStorageRawReader **pointer)
{
	if (!pointer || !*pointer)
		return;
	udb_storage_free(*pointer);
	*pointer = NULL;
}

static UdbStorageResult udb_storage_raw_reader_create(UdbStorageFile *file, const UdbStorageRawRun *run,
													  UdbStorageRawReader **out)
{
	UdbStorageRawReader *reader;
	uint64_t size;
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!file || !file->opened || !udb_storage_raw_valid(run))
		return UDB_STORAGE_ERROR;
	result = udb_storage_file_size(file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (run->start > size || run->length > size - run->start)
		return UDB_STORAGE_CORRUPT;
	reader = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*reader));
	if (!reader)
		return UDB_STORAGE_LIMIT;
	reader->file = file;
	reader->run = *run;
	reader->offset = run->start;
	*out = reader;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_reader_next(UdbStorageRawReader *reader, UdbStorageRecord *record)
{
	UdbStorageResult result;
	UdbStorageRow previous;
	uint32_t start, end;
	unsigned int partition;
	if (!reader || !record || udb_storage_overlaps(record, sizeof(*record), reader, sizeof(*reader)))
		return UDB_STORAGE_ERROR;
	if (reader->failed)
		return reader->failed;
	if (reader->finished)
		return UDB_STORAGE_NOT_FOUND;
	if (!reader->loaded || reader->slot == reader->page.items)
	{
		if (reader->offset == reader->run.start + reader->run.length)
		{
			if (reader->count != reader->run.count)
				return reader->failed = UDB_STORAGE_CORRUPT;
			reader->finished = 1;
			return UDB_STORAGE_NOT_FOUND;
		}
		result = udb_storage_file_read(reader->file, reader->offset, reader->bytes, sizeof(reader->bytes));
		if (result == UDB_STORAGE_OK)
			result = udb_storage_page_decode(reader->bytes, sizeof(reader->bytes), &reader->page);
		if (result != UDB_STORAGE_OK)
			return reader->failed = result;
		if (reader->page.kind != UDB_STORAGE_PAGE_DATA || reader->page.level ||
			reader->page.generation != reader->run.generation || reader->page.object != reader->run.object ||
			reader->page.ordinal != reader->offset / UDB_STORAGE_PAGE_SIZE)
			return reader->failed = UDB_STORAGE_CORRUPT;
		reader->offset += UDB_STORAGE_PAGE_SIZE;
		reader->slot = reader->seek_slot;
		reader->seek_slot = 0;
		if (reader->slot >= reader->page.items)
			return reader->failed = UDB_STORAGE_CORRUPT;
		reader->loaded = 1;
	}
	start = udb_storage_get_u32(reader->page.payload.data + 8 + 4 * reader->slot);
	end = udb_storage_get_u32(reader->page.payload.data + 12 + 4 * reader->slot);
	result = udb_storage_record_decode(reader->page.payload.data + start, end - start, record);
	if (result != UDB_STORAGE_OK)
		return reader->failed = result;
	if (record->row.block != reader->run.block || reader->count >= reader->run.count)
		return reader->failed = UDB_STORAGE_CORRUPT;
	result = udb_storage_raw_partition(record, &partition);
	if (result != UDB_STORAGE_OK)
		return reader->failed = result;
	if (reader->previous_length)
	{
		result = udb_storage_row_decode(reader->previous, reader->previous_length, &previous);
		if (result != UDB_STORAGE_OK || partition < reader->partition ||
			(partition == reader->partition && udb_storage_key_compare(&previous, &record->row) >= 0))
			return reader->failed = UDB_STORAGE_CORRUPT;
	}
	result = udb_storage_run_copy_key(&record->row, reader->previous, &reader->previous_length);
	if (result != UDB_STORAGE_OK)
		return reader->failed = result;
	reader->count++;
	reader->slot++;
	reader->partition = partition;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_reader_seek(UdbStorageRawReader *reader, uint64_t offset, unsigned int slot,
													uint64_t count)
{
	if (!reader || reader->loaded || reader->count || reader->failed || reader->finished ||
		offset < reader->run.start || offset >= reader->run.start + reader->run.length ||
		offset % UDB_STORAGE_PAGE_SIZE || slot >= UDB_STORAGE_DATA_ROWS_MAX || count >= reader->run.count)
		return UDB_STORAGE_ERROR;
	reader->offset = offset;
	reader->seek_slot = slot;
	reader->count = count;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_raw_reader_position(UdbStorageRawReader *reader, uint64_t *offset,
														unsigned int *slot, uint64_t *count)
{
	if (!reader || !offset || !slot || !count || !reader->loaded || !reader->slot || !reader->count)
		return UDB_STORAGE_ERROR;
	*offset = reader->offset - UDB_STORAGE_PAGE_SIZE;
	*slot = reader->slot - 1;
	*count = reader->count - 1;
	return UDB_STORAGE_OK;
}

/* End of udb_storage_raw.c.inc */
/* Inlined: udb_storage_read.c.inc */
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

/* End of udb_storage_read.c.inc */
/* Inlined: udb_storage_rebuild.c.inc */
/* Canonical private radix rebuild. Input is replayable, immutable and sorted.
 * Each step appends at most 64 MiB, including directories/catalog COW pages.
 * Large subtrees span multiple private jobs, never a larger output allowance.
 * Engine publication/flush and input generation pins remain separate concerns. */

typedef struct UdbStorageRebuildFrame
{
	UdbStorageLogicalPosition position;
	UdbStorageLogicalSummary left;
	unsigned int phase;
} UdbStorageRebuildFrame;

struct UdbStorageRebuild
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	UdbStorageBlockRoot candidate;
	UdbStorageLogicalPosition origin;
	UdbStorageSource source;
	UdbStorageCatalogIterator *old_nodes;
	UdbStorageRebuildFrame frames[257];
	unsigned char bytes[UDB_STORAGE_RECORD_MAX];
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	size_t previous_length;
	uint64_t next_object;
	UdbStorageLogicalSummary completed;
	unsigned int depth;
	UdbStorageResult failed;
	int cleaned;
	int finished;
	int unwinding;
	int defer_block_hash;
	uint64_t output_allowance;
};

static int udb_storage_position_descends(const UdbStorageLogicalPosition *child,
										 const UdbStorageLogicalPosition *parent)
{
	return child->block == parent->block && child->depth >= parent->depth &&
		   udb_storage_logical_contains(parent, child->prefix, child->partition);
}

static UdbStorageResult udb_storage_rebuild_close(UdbStorageRebuild **pointer)
{
	UdbStorageRebuild *rebuild;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	rebuild = *pointer;
	if (rebuild->old_nodes)
		result = udb_storage_catalog_iterator_close(&rebuild->old_nodes);
	udb_storage_free(rebuild);
	*pointer = NULL;
	return result;
}

static UdbStorageResult udb_storage_rebuild_create(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageBlockRoot *root,
												   const UdbStorageLogicalPosition *position, uint64_t generation,
												   uint64_t watermark, uint64_t next_object,
												   const UdbStorageSource *source, UdbStorageRebuild **out)
{
	UdbStorageRebuild *rebuild;
	unsigned char lower[64] = {0}, upper[64] = {0};
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || file->cache != cache || !udb_storage_block_root_valid(root) ||
		!udb_storage_logical_position_valid(position) || position->block != root->block ||
		generation < root->generation || watermark < root->watermark || !next_object || !source || !source->open ||
		!source->next || !source->close || file->append_limit)
		return UDB_STORAGE_ERROR;
	rebuild = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*rebuild));
	if (!rebuild)
		return UDB_STORAGE_LIMIT;
	rebuild->cache = cache;
	rebuild->file = file;
	rebuild->candidate = *root;
	rebuild->candidate.generation = generation;
	rebuild->candidate.watermark = watermark;
	rebuild->origin = *position;
	rebuild->defer_block_hash = position->depth != 0;
	rebuild->frames[0].position = *position;
	rebuild->source = *source;
	rebuild->next_object = next_object;
	rebuild->output_allowance = UDB_STORAGE_COMPACTION_MAX;
	/* Scan only this base partition, against the immutable old root. */
	lower[0] = upper[0] = position->block;
	lower[1] = upper[1] = 1;
	lower[2] = upper[2] = (unsigned char)(position->partition >> 8);
	lower[3] = upper[3] = (unsigned char)position->partition;
	if (++upper[3] == 0)
		upper[2]++;
	result = udb_storage_catalog_iterator_create(cache, file, &root->catalog, lower, upper, &rebuild->old_nodes);
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_rebuild_close(&rebuild);
		return result;
	}
	*out = rebuild;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_rebuild_remove_one(UdbStorageRebuild *rebuild)
{
	unsigned char key[64], value[128];
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageCatalogRoot changed;
	UdbStorageResult result;
	for (;;)
	{
		result = udb_storage_catalog_iterator_next(rebuild->old_nodes, key, value);
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			result = udb_storage_catalog_iterator_close(&rebuild->old_nodes);
			if (result == UDB_STORAGE_OK)
				rebuild->cleaned = 1;
			return result;
		}
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_node_position_decode(key, &position);
		if (result != UDB_STORAGE_OK)
			return result;
		if (!udb_storage_position_descends(&position, &rebuild->origin))
			continue;
		result = udb_storage_node_decode(&position, value, &node);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_catalog_change(rebuild->cache, rebuild->file, &rebuild->candidate.catalog,
											rebuild->candidate.generation, key, NULL, &changed);
		if (result != UDB_STORAGE_OK)
			return result;
		rebuild->candidate.catalog = changed;
		/* Rebuilt leaves receive fresh run IDs. Old-generation references remain
		 * intact in the pinned input catalog; no physical extent is reclaimed. */
		for (unsigned int i = 0; i < node.run_count; i++)
		{
			udb_storage_run_manifest_key(position.block, UDB_STORAGE_POINTS, node.runs[i], key);
			result = udb_storage_catalog_change(rebuild->cache, rebuild->file, &rebuild->candidate.catalog,
												rebuild->candidate.generation, key, NULL, &changed);
			if (result != UDB_STORAGE_OK)
				return result;
			rebuild->candidate.catalog = changed;
		}
		return UDB_STORAGE_OK;
	}
}

static UdbStorageResult udb_storage_rebuild_read(UdbStorageRebuild *rebuild, void *handle,
												 const UdbStorageLogicalPosition *position, UdbStorageRecord *record,
												 size_t *encoded_length)
{
	UdbStorageResult result;
	UdbStorageRow previous;
	unsigned char hash[32];
	unsigned int partition;
	for (;;)
	{
		result = rebuild->source.next(handle, record);
		if (result != UDB_STORAGE_OK)
			return result;
		if (record->operation != UDB_STORAGE_PUT || record->stamp.sequence > rebuild->candidate.watermark ||
			record->row.block != position->block || !udb_storage_row_valid(&record->row))
			return UDB_STORAGE_CORRUPT;
		if (rebuild->previous_length &&
			(udb_storage_row_decode(rebuild->previous, rebuild->previous_length, &previous) != UDB_STORAGE_OK ||
			 udb_storage_key_compare(&previous, &record->row) >= 0))
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_run_copy_key(&record->row, rebuild->previous, &rebuild->previous_length);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_key_hash(&record->row, hash, &partition);
		if (result != UDB_STORAGE_OK)
			return result;
		if (!udb_storage_logical_contains(position, hash, partition))
			continue;
		return udb_storage_row_encode(&record->row, rebuild->bytes, sizeof(rebuild->bytes), encoded_length);
	}
}

static UdbStorageResult udb_storage_rebuild_probe(UdbStorageRebuild *rebuild, const UdbStorageLogicalPosition *position,
												  int *split)
{
	void *handle = NULL;
	UdbStorageRecord record;
	UdbStorageResult result, closed;
	uint64_t weight = 0;
	size_t length;
	*split = 0;
	rebuild->previous_length = 0;
	result = rebuild->source.open(rebuild->source.context, position, &handle);
	if (result != UDB_STORAGE_OK)
		return result;
	while ((result = udb_storage_rebuild_read(rebuild, handle, position, &record, &length)) == UDB_STORAGE_OK)
	{
		if (length + UINT64_C(128) > UDB_STORAGE_LEAF_WEIGHT_MAX - weight)
		{
			*split = 1;
			break;
		}
		weight += length + UINT64_C(128);
	}
	closed = rebuild->source.close(&handle);
	if (closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND)
		return closed;
	return result == UDB_STORAGE_NOT_FOUND || *split ? UDB_STORAGE_OK : result;
}

static UdbStorageResult udb_storage_rebuild_leaf(UdbStorageRebuild *rebuild, const UdbStorageLogicalPosition *position,
												 UdbStorageLogicalSummary *summary)
{
	void *handle = NULL;
	UdbStorageRunBuilder *builder = NULL;
	UdbStorageLeafHasher *hasher = NULL;
	UdbStorageRecord record;
	UdbStorageRun run;
	UdbStorageNode node;
	UdbStorageCatalogRoot changed;
	UdbStorageResult result, closed;
	size_t length;
	memset(&node, 0, sizeof(node));
	node.kind = UDB_STORAGE_LEAF;
	rebuild->previous_length = 0;
	result = rebuild->source.open(rebuild->source.context, position, &handle);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_leaf_hasher_create(rebuild->cache->budget, position, &hasher);
	if (result != UDB_STORAGE_OK)
		goto done;
	while ((result = udb_storage_rebuild_read(rebuild, handle, position, &record, &length)) == UDB_STORAGE_OK)
	{
		result = udb_storage_leaf_hasher_add(hasher, rebuild->bytes, length);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (!builder)
		{
			if (rebuild->next_object == UINT64_MAX)
			{
				result = UDB_STORAGE_LIMIT;
				goto done;
			}
			result = udb_storage_run_builder_create(rebuild->cache, rebuild->file, position->block, UDB_STORAGE_POINTS,
													rebuild->candidate.generation, rebuild->next_object++, &builder);
			if (result != UDB_STORAGE_OK)
				goto done;
		}
		result = udb_storage_record_encode(&record, rebuild->bytes, sizeof(rebuild->bytes), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_run_builder_add(builder, rebuild->bytes, length);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	if (result != UDB_STORAGE_NOT_FOUND)
		goto done;
	result = udb_storage_leaf_hasher_finish(hasher, &node.summary);
	if (result != UDB_STORAGE_OK)
		goto done;
	if (builder)
	{
		result = udb_storage_run_builder_finish(builder, &run);
		if (result != UDB_STORAGE_OK)
			goto done;
		result = udb_storage_run_save(rebuild->cache, &run, &rebuild->candidate.catalog, rebuild->candidate.generation,
									  &changed);
		if (result != UDB_STORAGE_OK)
			goto done;
		rebuild->candidate.catalog = changed;
		node.run_count = 1;
		node.runs[0] = run.directory.object;
		result = udb_storage_node_change(rebuild->cache, rebuild->file, &rebuild->candidate.catalog,
										 rebuild->candidate.generation, position, &node, &changed);
		if (result != UDB_STORAGE_OK)
			goto done;
		rebuild->candidate.catalog = changed;
	}
	/* Empty leaves stay implicit, with the position-specific canonical digest. */
	*summary = node.summary;
	result = UDB_STORAGE_OK;
done:
	closed = rebuild->source.close(&handle);
	if (closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND)
		result = closed;
	udb_storage_leaf_hasher_free(&hasher);
	udb_storage_run_builder_free(&builder);
	return result;
}

static UdbStorageResult udb_storage_rebuild_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
													  void *opaque)
{
	UdbStorageRebuild *rebuild = opaque;
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageResult result;
	memset(&position, 0, sizeof(position));
	position.block = rebuild->candidate.block;
	position.partition = (uint16_t)partition;
	result = udb_storage_node_get(rebuild->cache, rebuild->file, &rebuild->candidate.catalog, &position, &node);
	if (result == UDB_STORAGE_OK)
		*summary = node.summary;
	return result;
}

static UdbStorageResult udb_storage_rebuild_step(UdbStorageRebuild *rebuild, UdbStorageBlockRoot *out,
												 uint64_t *next_object)
{
	UdbStorageRebuildFrame *frame;
	UdbStorageLogicalSummary completed;
	UdbStorageResult result;
	uint64_t size;
	int split;
	if (!rebuild || !out || !next_object || rebuild->file->append_limit)
		return UDB_STORAGE_ERROR;
	if (rebuild->failed)
		return rebuild->failed;
	if (rebuild->finished)
	{
		*out = rebuild->candidate;
		*next_object = rebuild->next_object;
		return UDB_STORAGE_OK;
	}
	result = udb_storage_file_size(rebuild->file, &size);
	if (result != UDB_STORAGE_OK)
		return rebuild->failed = result;
	if (!udb_storage_io_range_valid(size, rebuild->output_allowance))
		return rebuild->failed = UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(rebuild->file, size + rebuild->output_allowance);
	if (!rebuild->cleaned)
	{
		result = udb_storage_rebuild_remove_one(rebuild);
		goto done;
	}
	frame = rebuild->frames + rebuild->depth;
	if (rebuild->unwinding)
		goto unwind;
	for (;;)
	{
		result = udb_storage_rebuild_probe(rebuild, &frame->position, &split);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (!split)
			break;
		if (frame->position.depth == 256 || rebuild->depth == 256)
		{
			result = UDB_STORAGE_LIMIT;
			goto done;
		}
		frame->phase = 1;
		result = udb_storage_logical_child(&frame->position, 0, &rebuild->frames[rebuild->depth + 1].position);
		if (result != UDB_STORAGE_OK)
			goto done;
		frame = rebuild->frames + ++rebuild->depth;
		frame->phase = 0;
	}
	result = udb_storage_rebuild_leaf(rebuild, &frame->position, &completed);
	if (result != UDB_STORAGE_OK)
		goto done;
	rebuild->completed = completed;
	rebuild->unwinding = 1;
	goto done;
unwind:
	if (rebuild->depth)
	{
		UdbStorageNode node;
		UdbStorageCatalogRoot changed;
		frame = rebuild->frames + --rebuild->depth;
		if (frame->phase == 1)
		{
			frame->left = rebuild->completed;
			frame->phase = 2;
			result = udb_storage_logical_child(&frame->position, 1, &rebuild->frames[rebuild->depth + 1].position);
			if (result != UDB_STORAGE_OK)
				goto done;
			rebuild->frames[++rebuild->depth].phase = 0;
			rebuild->unwinding = 0;
			goto done;
		}
		memset(&node, 0, sizeof(node));
		node.kind = UDB_STORAGE_BRANCH;
		result = udb_storage_logical_branch(&frame->position, &frame->left, &rebuild->completed, &node.summary);
		if (result != UDB_STORAGE_OK)
			goto done;
		result = udb_storage_node_change(rebuild->cache, rebuild->file, &rebuild->candidate.catalog,
										 rebuild->candidate.generation, &frame->position, &node, &changed);
		if (result != UDB_STORAGE_OK)
			goto done;
		rebuild->candidate.catalog = changed;
		rebuild->completed = node.summary;
		goto done;
	}
	result = rebuild->defer_block_hash
				 ? UDB_STORAGE_OK
				 : udb_storage_logical_block(rebuild->candidate.block, udb_storage_rebuild_partition, rebuild,
											 &rebuild->candidate.summary);
	if (result == UDB_STORAGE_OK)
	{
		rebuild->finished = 1;
		*out = rebuild->candidate;
	}
done:
	udb_storage_file_job_limit(rebuild->file, 0);
	*next_object = rebuild->next_object;
	if (result != UDB_STORAGE_OK)
		return rebuild->failed = result;
	return rebuild->finished ? UDB_STORAGE_OK : UDB_STORAGE_BUSY;
}

/* Only a full private block builder may defer the block digest. The resulting
 * root is not admissible for publication until all partition summaries hash. */
static UdbStorageResult udb_storage_rebuild_defer(UdbStorageRebuild *rebuild)
{
	if (!rebuild || rebuild->cleaned || rebuild->finished || rebuild->failed)
		return UDB_STORAGE_ERROR;
	rebuild->defer_block_hash = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_rebuild_summary(UdbStorageRebuild *rebuild, UdbStorageLogicalSummary *summary)
{
	if (!rebuild || !summary || !rebuild->finished || rebuild->failed)
		return UDB_STORAGE_ERROR;
	*summary = rebuild->completed;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_rebuild_allowance(UdbStorageRebuild *rebuild, uint64_t bytes)
{
	if (!rebuild || bytes < UDB_STORAGE_PAGE_SIZE || bytes > UDB_STORAGE_COMPACTION_MAX || rebuild->file->append_limit)
		return UDB_STORAGE_LIMIT;
	rebuild->output_allowance = bytes;
	return UDB_STORAGE_OK;
}

/* End of udb_storage_rebuild.c.inc */
/* Inlined: udb_storage_reclaim.c.inc */
/* Checkpoint-gated reachability and extent reuse. Only metadata is traversed:
 * catalog pages and complete immutable fragment extents are marked. Fixed-size
 * paged bitmaps live in exclusive private files, with one charged chunk per
 * block, never a resident per-record or per-page full-store allocation. */

#define UDB_STORAGE_MAP_HEADER 48
#define UDB_STORAGE_MAP_BYTES (UDB_STORAGE_PAGE_PAYLOAD - UDB_STORAGE_MAP_HEADER)
#define UDB_STORAGE_MAP_BITS ((uint64_t)UDB_STORAGE_MAP_BYTES * 8)

typedef struct UdbStorageFreeMap
{
	UdbStorageFile file;
	UdbStorageDirectory *directory;
	char name[64];
	unsigned char payload[UDB_STORAGE_PAGE_PAYLOAD];
	unsigned char bytes[UDB_STORAGE_PAGE_SIZE];
	unsigned char identity[16];
	uint64_t generation;
	uint64_t pages;
	uint64_t chunk;
	uint64_t cursor[2];
	unsigned int block;
	int loaded;
	int dirty;
	int created;
} UdbStorageFreeMap;

typedef struct UdbStorageReclaimer
{
	UdbStorageEngine *engine;
	UdbStorageFreeMap maps[6];
	unsigned int marking;
} UdbStorageReclaimer;

typedef struct UdbStorageReclaimPin
{
	struct UdbStorageReclaimPin *next;
	UdbStorageGeneration *generation;
} UdbStorageReclaimPin;

typedef UdbStorageResult (*UdbStorageMarkValue)(UdbStorageReclaimer *reclaimer, unsigned int block,
												const unsigned char key[64], const unsigned char value[128],
												void *context);

static UdbStorageResult udb_storage_map_flush(UdbStorageFreeMap *map)
{
	UdbStoragePage page = {0};
	UdbStorageResult result;
	if (!map->loaded || !map->dirty)
		return UDB_STORAGE_OK;
	page.kind = UDB_STORAGE_PAGE_METADATA;
	page.generation = map->generation;
	page.object = map->block + 1;
	page.ordinal = map->chunk;
	page.payload.data = map->payload;
	page.payload.length = sizeof(map->payload);
	result = udb_storage_page_encode(&page, map->bytes, sizeof(map->bytes));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_file_write(&map->file, map->chunk * UDB_STORAGE_PAGE_SIZE, map->bytes, sizeof(map->bytes));
	if (result == UDB_STORAGE_OK)
		map->dirty = 0;
	return result;
}

static UdbStorageResult udb_storage_map_load(UdbStorageFreeMap *map, uint64_t chunk)
{
	UdbStorageResult result;
	UdbStoragePage page;
	if (map->loaded && map->chunk == chunk)
		return UDB_STORAGE_OK;
	result = udb_storage_map_flush(map);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_file_read(&map->file, chunk * UDB_STORAGE_PAGE_SIZE, map->bytes, sizeof(map->bytes));
	if (result == UDB_STORAGE_OK)
		result = udb_storage_page_decode(map->bytes, sizeof(map->bytes), &page);
	if (result != UDB_STORAGE_OK)
		return result;
	if (page.kind != UDB_STORAGE_PAGE_METADATA || page.generation != map->generation || page.object != map->block + 1 ||
		page.ordinal != chunk || page.payload.length != sizeof(map->payload) ||
		memcmp(page.payload.data, "UDBMAP4\0", 8) || memcmp(page.payload.data + 8, map->identity, 16) ||
		udb_storage_get_u64(page.payload.data + 24) != map->pages || !udb_storage_zero(page.payload.data + 32, 16))
		return UDB_STORAGE_CORRUPT;
	memcpy(map->payload, page.payload.data, sizeof(map->payload));
	map->chunk = chunk;
	map->loaded = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_map_mark(UdbStorageFreeMap *map, uint64_t offset, uint64_t length)
{
	uint64_t first, count;
	UdbStorageResult result;
	if (offset % UDB_STORAGE_PAGE_SIZE || !length || length % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	first = offset / UDB_STORAGE_PAGE_SIZE;
	count = length / UDB_STORAGE_PAGE_SIZE;
	if (first >= map->pages || count > map->pages - first)
		return UDB_STORAGE_CORRUPT;
	while (count--)
	{
		uint64_t chunk = first / UDB_STORAGE_MAP_BITS, bit = first % UDB_STORAGE_MAP_BITS;
		result = udb_storage_map_load(map, chunk);
		if (result != UDB_STORAGE_OK)
			return result;
		map->payload[UDB_STORAGE_MAP_HEADER + bit / 8] |= (unsigned char)(1U << (bit % 8));
		map->dirty = 1;
		first++;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_map_reserve(UdbStorageFreeMap *map, unsigned int pages, uint64_t *offset)
{
	unsigned int role = pages == 1 ? 0 : 1;
	uint64_t first = map->cursor[role], available = 0;
	UdbStorageResult result;
	for (uint64_t i = first; i < map->pages; i++)
	{
		uint64_t bit = i % UDB_STORAGE_MAP_BITS;
		result = udb_storage_map_load(map, i / UDB_STORAGE_MAP_BITS);
		if (result != UDB_STORAGE_OK)
			return result;
		if (map->payload[UDB_STORAGE_MAP_HEADER + bit / 8] & (1U << (bit % 8)))
			available = 0;
		else
			available++;
		if (available == pages)
		{
			first = i + 1 - pages;
			result = udb_storage_map_mark(map, first * UDB_STORAGE_PAGE_SIZE, (uint64_t)pages * UDB_STORAGE_PAGE_SIZE);
			if (result != UDB_STORAGE_OK)
				return result;
			map->cursor[role] = i + 1;
			*offset = first * UDB_STORAGE_PAGE_SIZE;
			return UDB_STORAGE_OK;
		}
	}
	map->cursor[role] = map->pages;
	return UDB_STORAGE_NOT_FOUND;
}

static UdbStorageResult udb_storage_file_reserve_fragment(UdbStorageFile *file, unsigned int *pages, uint64_t *offset)
{
	UdbStorageFreeMap *map;
	UdbStorageResult result;
	uint64_t available = 0, first = 0;
	if (!file || !file->opened || !pages || !offset || *pages != 64)
		return UDB_STORAGE_ERROR;
	map = file->free_map;
	if (map)
	{
		for (uint64_t i = map->cursor[1]; i <= map->pages; i++)
		{
			int used = 1;
			if (i < map->pages)
			{
				result = udb_storage_map_load(map, i / UDB_STORAGE_MAP_BITS);
				if (result != UDB_STORAGE_OK)
					return result;
				uint64_t bit = i % UDB_STORAGE_MAP_BITS;
				used = (map->payload[UDB_STORAGE_MAP_HEADER + bit / 8] & (1U << (bit % 8))) != 0;
			}
			if (!used)
			{
				if (!available)
					first = i;
				available++;
			}
			if ((used || available == 64) && available >= 2)
			{
				uint64_t bytes = available * UDB_STORAGE_PAGE_SIZE;
				if (file->allocated_bytes > UINT64_MAX - bytes)
					return UDB_STORAGE_LIMIT;
				result = udb_storage_map_mark(map, first * UDB_STORAGE_PAGE_SIZE, bytes);
				if (result != UDB_STORAGE_OK)
					return result;
				map->cursor[1] = first + available;
				*pages = (unsigned int)available;
				*offset = first * UDB_STORAGE_PAGE_SIZE;
				file->allocated_bytes += bytes;
				return UDB_STORAGE_OK;
			}
			if (used)
				available = 0;
		}
		map->cursor[1] = map->pages;
	}
	return udb_storage_file_reserve(file, *pages, offset);
}

static UdbStorageResult udb_storage_file_reserve(UdbStorageFile *file, unsigned int pages, uint64_t *offset)
{
	UdbStorageResult result = UDB_STORAGE_NOT_FOUND;
	uint64_t length;
	if (!file || !file->opened || !offset || !pages || pages > UDB_STORAGE_FRAGMENT_MAX / UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_ERROR;
	length = (uint64_t)pages * UDB_STORAGE_PAGE_SIZE;
	if (file->allocated_bytes > UINT64_MAX - length)
		return UDB_STORAGE_LIMIT;
	if (file->free_map)
		result = udb_storage_map_reserve(file->free_map, pages, offset);
	if (result == UDB_STORAGE_NOT_FOUND)
		result = udb_storage_file_size(file, offset);
	if (result != UDB_STORAGE_OK)
		return result;
	if (*offset % UDB_STORAGE_PAGE_SIZE || !udb_storage_io_range_valid(*offset, (size_t)length))
		return UDB_STORAGE_CORRUPT;
	file->allocated_bytes += length;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_mark_catalog(UdbStorageReclaimer *reclaimer, unsigned int block,
												 const UdbStorageCatalogRoot *root, UdbStorageMarkValue value,
												 void *context)
{
	UdbStorageEngine *engine = reclaimer->engine;
	UdbStorageCacheEntry *pin = NULL;
	UdbStorageResult result;
	if (!root->count)
		return UDB_STORAGE_OK;
	result = udb_storage_map_mark(reclaimer->maps + block, root->offset, UDB_STORAGE_PAGE_SIZE);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_catalog_pin(engine->cache, engine->files + block, root, &pin);
	if (result != UDB_STORAGE_OK)
		return result;
	size_t stride = root->height == 1 ? 192 : 88;
	for (unsigned int i = 0; i < pin->page.items; i++)
	{
		const unsigned char *entry = pin->page.payload.data + 16 + (size_t)i * stride;
		if (root->height > 1)
		{
			UdbStorageCatalogRoot child;
			udb_storage_catalog_child_ref(entry, root, &child);
			result = udb_storage_mark_catalog(reclaimer, block, &child, value, context);
		}
		else if (value)
			result = value(reclaimer, block, entry, entry + 64, context);
		if (result != UDB_STORAGE_OK)
			break;
	}
	UdbStorageResult closed = udb_storage_cache_release(engine->cache, pin);
	return result == UDB_STORAGE_OK ? closed : result;
}

static UdbStorageResult udb_storage_mark_fragment(UdbStorageReclaimer *reclaimer, unsigned int block,
												  const unsigned char key[64], const unsigned char value[128],
												  void *opaque)
{
	UdbStorageRun *run = opaque;
	UdbStorageFragment fragment;
	UdbStorageResult result;
	(void)key;
	result = udb_storage_run_descriptor(run, value, &fragment);
	return result == UDB_STORAGE_OK ? udb_storage_map_mark(reclaimer->maps + block, fragment.start, fragment.length)
									: result;
}

static UdbStorageResult udb_storage_mark_manifest(UdbStorageReclaimer *reclaimer, unsigned int block,
												  const unsigned char key[64], const unsigned char value[128],
												  void *context)
{
	UdbStorageRun run = {0};
	UdbStorageResult result;
	(void)context;
	if (key[1] == 1)
		return UDB_STORAGE_OK;
	if (key[0] != udb_storage_blocks[block] || (key[1] != 2 && key[1] != 3))
		return UDB_STORAGE_CORRUPT;
	run.file = reclaimer->engine->files + block;
	run.block = udb_storage_blocks[block];
	run.stream = key[1] == 2 ? UDB_STORAGE_POINTS : UDB_STORAGE_PREFIXES;
	result = udb_storage_catalog_root_decode(value, 128, &run.directory);
	if (result != UDB_STORAGE_OK)
		return result;
	return udb_storage_mark_catalog(reclaimer, block, &run.directory, udb_storage_mark_fragment, &run);
}

typedef struct UdbStorageMarkStage
{
	unsigned char root[UDB_STORAGE_BLOCK_ROOT_SIZE];
	unsigned int part;
	unsigned int block;
	unsigned char owner[8];
} UdbStorageMarkStage;

static UdbStorageResult udb_storage_mark_stage(UdbStorageReclaimer *reclaimer, unsigned int block,
											   const unsigned char key[64], const unsigned char value[128],
											   void *opaque)
{
	UdbStorageMarkStage *stage = opaque;
	UdbStorageBlockRoot root;
	UdbStorageResult result;
	(void)block;
	if (!key[8])
		return stage->part ? UDB_STORAGE_CORRUPT : UDB_STORAGE_OK;
	if (key[8] > 6 || key[9] != stage->part)
		return UDB_STORAGE_CORRUPT;
	if (!stage->part)
	{
		memcpy(stage->owner, key, 8);
		stage->block = key[8] - 1;
	}
	if (stage->block != key[8] - 1 || memcmp(stage->owner, key, 8))
		return UDB_STORAGE_CORRUPT;
	memcpy(stage->root + 128 * stage->part++, value, 128);
	if (stage->part != 3)
		return UDB_STORAGE_OK;
	stage->part = 0;
	result = udb_storage_block_root_decode(stage->root, &root);
	if (result != UDB_STORAGE_OK)
		return result;
	return udb_storage_mark_catalog(reclaimer, stage->block, &root.catalog, udb_storage_mark_manifest, NULL);
}

static UdbStorageResult udb_storage_reclaimer_close(UdbStorageEngine *engine)
{
	UdbStorageReclaimer *reclaimer = engine->reclaimer;
	UdbStorageResult result;
	if (!reclaimer)
		return UDB_STORAGE_OK;
	for (unsigned int i = 0; i < 6; i++)
		engine->files[i].free_map = NULL;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageFreeMap *map = reclaimer->maps + i;
		if (map->file.opened)
		{
			result = udb_storage_file_close(&map->file);
			if (result != UDB_STORAGE_OK)
				return result;
		}
		if (map->created)
		{
			result = udb_storage_file_remove_private(map->directory, map->name);
			if (result != UDB_STORAGE_OK)
				return result;
			map->created = 0;
		}
	}
	udb_storage_free(reclaimer);
	engine->reclaimer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_reclaimer_refresh(UdbStorageEngine *engine)
{
	UdbStorageReclaimPin *pins = NULL;
	UdbStorageReclaimer *reclaimer;
	UdbStorageResult result;
	unsigned char nonce[16];
	char token[33];
	static const char hex[] = "0123456789abcdef";
	if (engine->work || engine->mutation_work)
		return UDB_STORAGE_BUSY;
	/* The durable floor must advance before an older slot loses its extents. */
	result = udb_storage_journal_checkpoint(&engine->journal);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_reclaimer_close(engine);
	if (result != UDB_STORAGE_OK)
		return result;
	reclaimer = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*reclaimer));
	if (!reclaimer)
		return UDB_STORAGE_LIMIT;
	engine->reclaimer = reclaimer;
	reclaimer->engine = engine;
	if (RAND_bytes(nonce, sizeof(nonce)) != 1)
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 0; i < 16; i++)
	{
		token[i * 2] = hex[nonce[i] >> 4];
		token[i * 2 + 1] = hex[nonce[i] & 15];
	}
	token[32] = 0;
	/* Pin a finite inventory. New readers can only pin the already-inventoried
	 * current root, and this writer publishes nothing until marking finishes. */
	udb_storage_mutex_lock(&engine->mutex);
	for (UdbStorageGeneration *generation = engine->generations; generation; generation = generation->next)
	{
		UdbStorageReclaimPin *pin = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*pin));
		if (!pin || generation->references == SIZE_MAX)
		{
			udb_storage_free(pin);
			result = UDB_STORAGE_LIMIT;
			break;
		}
		generation->references++;
		pin->generation = generation;
		pin->next = pins;
		pins = pin;
	}
	udb_storage_mutex_unlock(&engine->mutex);
	if (result != UDB_STORAGE_OK)
		goto done;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageFreeMap *map = reclaimer->maps + i;
		uint64_t size;
		result = udb_storage_file_size(engine->files + i, &size);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (!size || size % UDB_STORAGE_PAGE_SIZE)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		map->pages = size / UDB_STORAGE_PAGE_SIZE;
		map->block = i;
		map->generation = engine->journal.state.generation;
		map->directory = &engine->directory;
		memcpy(map->identity, engine->journal.state.store_identity, 16);
		snprintf(map->name, sizeof(map->name), "stage_%s_%c_m.db", token, udb_storage_blocks[i]);
		result = udb_storage_file_open(map->directory, map->name, 2, &map->file);
		if (result != UDB_STORAGE_OK)
			goto done;
		map->created = 1;
		uint64_t chunks = (map->pages + UDB_STORAGE_MAP_BITS - 1) / UDB_STORAGE_MAP_BITS;
		for (uint64_t chunk = 0; chunk < chunks; chunk++)
		{
			memset(map->payload, 0, sizeof(map->payload));
			memcpy(map->payload, "UDBMAP4", 7);
			memcpy(map->payload + 8, map->identity, 16);
			udb_storage_put_u64(map->payload + 24, map->pages);
			map->chunk = chunk;
			map->loaded = map->dirty = 1;
			result = udb_storage_map_flush(map);
			if (result != UDB_STORAGE_OK)
				goto done;
		}
		result = udb_storage_map_mark(map, 0, UDB_STORAGE_PAGE_SIZE);
		if (result != UDB_STORAGE_OK)
			goto done;
		map->cursor[0] = map->cursor[1] = 1;
	}
	for (UdbStorageReclaimPin *pin = pins; pin; pin = pin->next)
	{
		const UdbStorageState *state = &pin->generation->state;
		for (unsigned int i = 0; i < 6; i++)
		{
			result = udb_storage_mark_catalog(reclaimer, i, &state->blocks[i].catalog, udb_storage_mark_manifest, NULL);
			if (result != UDB_STORAGE_OK)
				goto done;
		}
		UdbStorageMarkStage stage = {0};
		result = udb_storage_mark_catalog(reclaimer, 3, &state->stages, udb_storage_mark_stage, &stage);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (stage.part)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
	}
	for (unsigned int i = 0; i < 6; i++)
	{
		result = udb_storage_map_flush(reclaimer->maps + i);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_file_flush(&reclaimer->maps[i].file);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	/* Only a complete mark may become the writer's allocator inventory. */
	for (unsigned int i = 0; i < 6; i++)
		engine->files[i].free_map = reclaimer->maps + i;
done:
	while (pins)
	{
		UdbStorageReclaimPin *pin = pins;
		pins = pin->next;
		UdbStorageResult released = udb_storage_engine_release(engine, &pin->generation);
		udb_storage_free(pin);
		if (released != UDB_STORAGE_OK)
			result = released;
	}
	return result;
}

/* End of udb_storage_reclaim.c.inc */
/* Inlined: udb_storage_runs.c.inc */
/*
 * UDB 4 native storage: paged fragment directories and bounded visible-row merge.
 * All inputs are immutable, owner-validated and generation-pinned for the entire
 * view lifetime. No function here publishes, commits or reclaims storage.
 */

static int udb_storage_run_valid(UdbStorageCache *cache, const UdbStorageRun *run)
{
	return cache && run && run->file && run->file->opened && run->file->cache == cache &&
		   run->file->directory->budget == cache->budget && udb_storage_block_valid(run->block) &&
		   (run->stream == UDB_STORAGE_POINTS || run->stream == UDB_STORAGE_PREFIXES) &&
		   udb_storage_catalog_root_valid(&run->directory);
}

static void udb_storage_run_key(uint64_t ordinal, unsigned char key[64])
{
	memset(key, 0, 64);
	/* Lexicographic catalog order matches numeric fragment order. */
	for (unsigned int i = 0; i < 8; i++)
		key[i] = (unsigned char)(ordinal >> (56 - 8 * i));
}

static UdbStorageResult udb_storage_run_descriptor(const UdbStorageRun *run, const unsigned char value[128],
												   UdbStorageFragment *fragment)
{
	UdbStorageResult result = udb_storage_fragment_decode(value, UDB_STORAGE_FRAGMENT_DESCRIPTOR_SIZE, fragment);
	if (result != UDB_STORAGE_OK)
		return result;
	for (size_t i = UDB_STORAGE_FRAGMENT_DESCRIPTOR_SIZE; i < 128; i++)
		if (value[i])
			return UDB_STORAGE_CORRUPT;
	if (fragment->block != run->block || fragment->stream != run->stream || fragment->object != run->directory.object ||
		fragment->generation > run->directory.generation)
		return UDB_STORAGE_CORRUPT;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_fragment(UdbStorageCache *cache, const UdbStorageRun *run, uint64_t ordinal,
												 UdbStorageFragment *fragment)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (ordinal >= run->directory.count)
		return UDB_STORAGE_NOT_FOUND;
	udb_storage_run_key(ordinal, key);
	result = udb_storage_catalog_get(cache, run->file, &run->directory, key, value);
	/* An advertised directory cannot silently contain ordinal gaps. */
	if (result == UDB_STORAGE_NOT_FOUND)
		return UDB_STORAGE_CORRUPT;
	return result == UDB_STORAGE_OK ? udb_storage_run_descriptor(run, value, fragment) : result;
}

static UdbStorageResult udb_storage_run_find(UdbStorageCache *cache, const UdbStorageRun *run, const UdbStorageRow *key,
											 uint64_t *ordinal)
{
	uint64_t lower = 0, upper = run->directory.count;
	while (lower < upper)
	{
		uint64_t middle = lower + (upper - lower) / 2;
		UdbStorageFragment fragment;
		UdbStorageCacheEntry *pin;
		UdbStorageRow maximum;
		UdbStorageResult result = udb_storage_run_fragment(cache, run, middle, &fragment);
		int compare;
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_fragment_maximum(cache, run->file, &fragment, &maximum, &pin);
		if (result != UDB_STORAGE_OK)
			return result;
		compare = udb_storage_key_compare(&maximum, key);
		result = udb_storage_cache_release(cache, pin);
		if (result != UDB_STORAGE_OK)
			return result;
		if (compare < 0)
			lower = middle + 1;
		else
			upper = middle;
	}
	*ordinal = lower;
	return lower == run->directory.count ? UDB_STORAGE_NOT_FOUND : UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_get(UdbStorageCache *cache, const UdbStorageRun *run, const UdbStorageRow *key,
											UdbStorageRecord *record, UdbStorageCacheEntry **pin)
{
	uint64_t ordinal;
	UdbStorageFragment fragment;
	UdbStorageResult result;
	if (!record || !pin)
		return UDB_STORAGE_ERROR;
	*pin = NULL;
	if (!udb_storage_run_valid(cache, run) || !udb_storage_row_valid(key) || key->block != run->block)
		return UDB_STORAGE_ERROR;
	result = udb_storage_run_find(cache, run, key, &ordinal);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_run_fragment(cache, run, ordinal, &fragment);
	if (result != UDB_STORAGE_OK)
		return result;
	return udb_storage_fragment_get(cache, run->file, &fragment, key, record, pin);
}

static UdbStorageResult udb_storage_run_copy_key(const UdbStorageRow *row, unsigned char *out, size_t *length)
{
	UdbStorageRow key = *row;
	key.kind = UDB_STORAGE_VALUE_NONE;
	key.value.data = NULL;
	key.value.length = 0;
	key.number = 0;
	return udb_storage_row_encode(&key, out, UDB_STORAGE_KEY_MAX, length);
}

struct UdbStorageRunBuilder
{
	UdbStorageCache *cache;
	UdbStorageRun run;
	UdbStorageFragmentBuilder *fragment;
	uint64_t generation;
	uint64_t start;
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	size_t previous_length;
	UdbStorageResult failed;
	int finished;
};

static void udb_storage_run_builder_free(UdbStorageRunBuilder **pointer)
{
	UdbStorageRunBuilder *builder;
	if (!pointer || !*pointer)
		return;
	builder = *pointer;
	udb_storage_fragment_builder_free(&builder->fragment);
	udb_storage_free(builder);
	*pointer = NULL;
}

static UdbStorageResult udb_storage_run_builder_create(UdbStorageCache *cache, UdbStorageFile *file,
													   unsigned char block, UdbStorageStream stream,
													   uint64_t generation, uint64_t object, UdbStorageRunBuilder **out)
{
	UdbStorageRunBuilder *builder;
	UdbStorageResult result;
	uint64_t size;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || !file || !file->opened || file->cache != cache || !generation || !object ||
		!udb_storage_block_valid(block) || (stream != UDB_STORAGE_POINTS && stream != UDB_STORAGE_PREFIXES))
		return UDB_STORAGE_ERROR;
	result = udb_storage_file_size(file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	builder = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*builder));
	if (!builder)
		return UDB_STORAGE_LIMIT;
	builder->cache = cache;
	builder->run.file = file;
	builder->run.block = block;
	builder->run.stream = stream;
	builder->run.directory.object = object;
	builder->generation = generation;
	builder->start = size;
	*out = builder;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_builder_open_fragment(UdbStorageRunBuilder *builder)
{
	uint64_t size, reserve;
	uint32_t height = builder->run.directory.height ? builder->run.directory.height : 1;
	UdbStorageResult result = udb_storage_file_size(builder->run.file, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (size < builder->start || size % UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_CORRUPT;
	/* Reserve worst-case fragment plus COW directory path before any output.
	 * Both data/index and catalog bytes count toward the 64 MiB private job cap. */
	reserve = UDB_STORAGE_FRAGMENT_MAX + (uint64_t)(2 * height + 1) * UDB_STORAGE_PAGE_SIZE;
	if (size - builder->start > UDB_STORAGE_COMPACTION_MAX ||
		reserve > UDB_STORAGE_COMPACTION_MAX - (size - builder->start) ||
		!udb_storage_io_range_valid(size, (size_t)reserve))
		return UDB_STORAGE_LIMIT;
	return udb_storage_fragment_builder_create(builder->run.file, builder->generation, builder->run.directory.object,
											   &builder->fragment);
}

static UdbStorageResult udb_storage_run_builder_emit(UdbStorageRunBuilder *builder)
{
	UdbStorageFragment fragment;
	UdbStorageCatalogRoot candidate;
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (!builder->fragment)
		return UDB_STORAGE_OK;
	result = udb_storage_fragment_builder_finish(builder->fragment, &fragment);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_fragment_builder_free(&builder->fragment);
	if (builder->run.directory.count == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	memset(value, 0, sizeof(value));
	result = udb_storage_fragment_encode(&fragment, value, sizeof(value));
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_run_key(builder->run.directory.count, key);
	result = udb_storage_catalog_change(builder->cache, builder->run.file, &builder->run.directory, builder->generation,
										key, value, &candidate);
	if (result != UDB_STORAGE_OK)
		return result;
	builder->run.directory = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_builder_add(UdbStorageRunBuilder *builder, const unsigned char *bytes,
													size_t length)
{
	UdbStorageRecord record;
	UdbStorageRow previous;
	UdbStorageResult result;
	if (!builder || builder->finished)
		return UDB_STORAGE_ERROR;
	if (builder->failed)
		return builder->failed;
	result = udb_storage_record_decode(bytes, length, &record);
	if (result != UDB_STORAGE_OK)
		return builder->failed = result;
	if (record.row.block != builder->run.block || !udb_storage_record_matches_stream(&record, builder->run.stream))
		return builder->failed = UDB_STORAGE_CORRUPT;
	if (builder->previous_length &&
		(udb_storage_row_decode(builder->previous, builder->previous_length, &previous) != UDB_STORAGE_OK ||
		 udb_storage_key_compare(&previous, &record.row) >= 0))
		return builder->failed = UDB_STORAGE_CORRUPT;
	if (!builder->fragment)
	{
		result = udb_storage_run_builder_open_fragment(builder);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_LIMIT ? result : (builder->failed = result);
	}
	result = udb_storage_fragment_builder_add(builder->fragment, bytes, length);
	if (result == UDB_STORAGE_LIMIT)
	{
		result = udb_storage_run_builder_emit(builder);
		if (result != UDB_STORAGE_OK)
			return builder->failed = result;
		result = udb_storage_run_builder_open_fragment(builder);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_LIMIT ? result : (builder->failed = result);
		result = udb_storage_fragment_builder_add(builder->fragment, bytes, length);
	}
	if (result != UDB_STORAGE_OK)
		return builder->failed = result;
	result = udb_storage_run_copy_key(&record.row, builder->previous, &builder->previous_length);
	return result == UDB_STORAGE_OK ? result : (builder->failed = result);
}

static UdbStorageResult udb_storage_run_builder_finish(UdbStorageRunBuilder *builder, UdbStorageRun *run)
{
	UdbStorageResult result;
	if (!builder || !run || builder->finished || udb_storage_overlaps(run, sizeof(*run), builder, sizeof(*builder)))
		return UDB_STORAGE_ERROR;
	if (builder->failed)
		return builder->failed;
	result = udb_storage_run_builder_emit(builder);
	if (result != UDB_STORAGE_OK)
		return builder->failed = result;
	builder->finished = 1;
	*run = builder->run;
	return UDB_STORAGE_OK;
}

typedef struct UdbStorageRunValidation
{
	UdbStorageRun run;
	UdbStorageRecordValidator validator;
	void *context;
	uint64_t watermark;
	uint64_t ordinal;
	size_t previous_length;
	unsigned char previous[UDB_STORAGE_KEY_MAX];
} UdbStorageRunValidation;

static UdbStorageResult udb_storage_run_validate_record(const UdbStorageRecord *record, void *context)
{
	UdbStorageRunValidation *validation = context;
	UdbStorageRow previous;
	UdbStorageResult result;
	if (record->stamp.sequence > validation->watermark)
		return UDB_STORAGE_CORRUPT;
	if (validation->previous_length &&
		(udb_storage_row_decode(validation->previous, validation->previous_length, &previous) != UDB_STORAGE_OK ||
		 udb_storage_key_compare(&previous, &record->row) >= 0))
		return UDB_STORAGE_CORRUPT;
	if (validation->validator)
	{
		result = validation->validator(record, validation->context);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return udb_storage_run_copy_key(&record->row, validation->previous, &validation->previous_length);
}

static UdbStorageResult udb_storage_run_validate_descriptor(const unsigned char *key, const unsigned char *value,
															void *context)
{
	UdbStorageRunValidation *validation = context;
	UdbStorageFragment fragment;
	unsigned char expected[64];
	UdbStorageResult result;
	udb_storage_run_key(validation->ordinal, expected);
	if (memcmp(expected, key, sizeof(expected)))
		return UDB_STORAGE_CORRUPT;
	result = udb_storage_run_descriptor(&validation->run, value, &fragment);
	if (result != UDB_STORAGE_OK)
		return result;
	result =
		udb_storage_fragment_validate(validation->run.file, &fragment, udb_storage_run_validate_record, validation);
	if (result != UDB_STORAGE_OK)
		return result;
	if (validation->ordinal == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	validation->ordinal++;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_validate(UdbStorageFile *file, const UdbStorageRun *run, uint64_t watermark,
												 UdbStorageRecordValidator validator, void *context)
{
	UdbStorageRunValidation *validation;
	UdbStorageResult result;
	if (!file || !file->opened || !udb_storage_run_valid(file->cache, run) || file != run->file)
		return UDB_STORAGE_ERROR;
	validation = udb_storage_alloc(file->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*validation));
	if (!validation)
		return UDB_STORAGE_LIMIT;
	validation->run = *run;
	validation->watermark = watermark;
	validation->validator = validator;
	validation->context = context;
	result = udb_storage_catalog_validate(file, &run->directory, udb_storage_run_validate_descriptor, validation);
	if (result == UDB_STORAGE_OK && validation->ordinal != run->directory.count)
		result = UDB_STORAGE_CORRUPT;
	udb_storage_free(validation);
	return result;
}

struct UdbStorageRunIterator
{
	UdbStorageCache *cache;
	UdbStorageRun run;
	UdbStorageFragmentIterator *fragment_iterator;
	UdbStorageFragment fragment;
	uint64_t ordinal;
	unsigned char prefix[UDB_STORAGE_KEY_MAX];
	size_t prefix_length;
	unsigned char previous[UDB_STORAGE_KEY_MAX];
	size_t previous_length;
	UdbStorageResult failed;
	int finished;
};

static UdbStorageResult udb_storage_run_iterator_close(UdbStorageRunIterator **pointer)
{
	UdbStorageRunIterator *iterator;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	iterator = *pointer;
	if (iterator->fragment_iterator)
	{
		result = udb_storage_fragment_iterator_close(&iterator->fragment_iterator);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	udb_storage_free(iterator);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_run_iterator_create(UdbStorageCache *cache, const UdbStorageRun *run,
														const UdbStorageRow *prefix, UdbStorageRunIterator **out)
{
	UdbStorageRunIterator *iterator;
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!udb_storage_run_valid(cache, run) ||
		(prefix && (!udb_storage_row_valid(prefix) || prefix->block != run->block)))
		return UDB_STORAGE_ERROR;
	iterator = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*iterator));
	if (!iterator)
		return UDB_STORAGE_LIMIT;
	iterator->cache = cache;
	iterator->run = *run;
	if (prefix)
	{
		result = udb_storage_run_copy_key(prefix, iterator->prefix, &iterator->prefix_length);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_run_find(cache, run, prefix, &iterator->ordinal);
		if (result == UDB_STORAGE_NOT_FOUND)
			iterator->finished = 1;
		else if (result != UDB_STORAGE_OK)
			goto fail;
	}
	if (!run->directory.count)
		iterator->finished = 1;
	*out = iterator;
	return UDB_STORAGE_OK;
fail:
	udb_storage_free(iterator);
	return result;
}

static UdbStorageResult udb_storage_run_iterator_next(UdbStorageRunIterator *iterator, UdbStorageRecord *record)
{
	UdbStorageResult result;
	UdbStorageRow prefix;
	if (!iterator || !record || udb_storage_overlaps(record, sizeof(*record), iterator, sizeof(*iterator)))
		return UDB_STORAGE_ERROR;
	if (iterator->failed)
		return iterator->failed;
	if (iterator->finished)
		return UDB_STORAGE_NOT_FOUND;
	if (iterator->prefix_length &&
		udb_storage_row_decode(iterator->prefix, iterator->prefix_length, &prefix) != UDB_STORAGE_OK)
		return iterator->failed = UDB_STORAGE_CORRUPT;
	while (iterator->ordinal < iterator->run.directory.count)
	{
		UdbStorageRecord candidate;
		if (!iterator->fragment_iterator)
		{
			result = udb_storage_run_fragment(iterator->cache, &iterator->run, iterator->ordinal, &iterator->fragment);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
			result = udb_storage_fragment_iterator_create(iterator->cache, iterator->run.file, &iterator->fragment,
														  iterator->prefix_length ? &prefix : NULL,
														  &iterator->fragment_iterator);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_CORRUPT : result;
		}
		result = udb_storage_fragment_iterator_next(iterator->fragment_iterator, &candidate);
		if (result == UDB_STORAGE_OK)
		{
			UdbStorageRow previous;
			if (iterator->previous_length &&
				(udb_storage_row_decode(iterator->previous, iterator->previous_length, &previous) != UDB_STORAGE_OK ||
				 udb_storage_key_compare(&previous, &candidate.row) >= 0))
				return iterator->failed = UDB_STORAGE_CORRUPT;
			result = udb_storage_run_copy_key(&candidate.row, iterator->previous, &iterator->previous_length);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
			*record = candidate;
			return UDB_STORAGE_OK;
		}
		if (result != UDB_STORAGE_NOT_FOUND)
			return iterator->failed = result;
		result = udb_storage_fragment_iterator_close(&iterator->fragment_iterator);
		if (result != UDB_STORAGE_OK)
			return iterator->failed = result;
		if (iterator->prefix_length)
		{
			UdbStorageCacheEntry *pin;
			UdbStorageRow maximum;
			int past;
			result =
				udb_storage_fragment_maximum(iterator->cache, iterator->run.file, &iterator->fragment, &maximum, &pin);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
			past = !udb_storage_key_has_prefix(&maximum, &prefix);
			if (udb_storage_key_compare(&maximum, &prefix) < 0)
			{
				udb_storage_cache_release(iterator->cache, pin);
				return iterator->failed = UDB_STORAGE_CORRUPT;
			}
			result = udb_storage_cache_release(iterator->cache, pin);
			if (result != UDB_STORAGE_OK)
				return iterator->failed = result;
			if (past)
			{
				iterator->finished = 1;
				return UDB_STORAGE_NOT_FOUND;
			}
		}
		iterator->ordinal++;
	}
	iterator->finished = 1;
	return UDB_STORAGE_NOT_FOUND;
}

typedef struct UdbStorageViewCursor
{
	UdbStorageRunIterator *iterator;
	UdbStorageRecord record;
	int present;
} UdbStorageViewCursor;

typedef struct UdbStoragePrefixCover
{
	UdbStorageRow key;
	UdbStorageStamp stamp;
	unsigned char bytes[UDB_STORAGE_KEY_MAX];
} UdbStoragePrefixCover;

typedef struct UdbStoragePrefixCursor
{
	UdbStorageViewCursor cursor;
	UdbStoragePrefixCover *covers;
	unsigned int depth;
	UdbStorageStamp seed;
	int has_seed;
} UdbStoragePrefixCursor;

struct UdbStorageView
{
	UdbStorageCache *cache;
	UdbStorageViewCursor points[UDB_STORAGE_MAX_RUNS + 1];
	UdbStoragePrefixCursor prefixes[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun indexed_prefixes[UDB_STORAGE_MAX_RUNS + 1];
	size_t point_count;
	size_t prefix_count;
	uint64_t watermark;
	UdbStorageResult failed;
	unsigned char output[UDB_STORAGE_ROW_MAX];
	int finished;
	int indexed;
};

static UdbStorageResult udb_storage_view_cursor_next(UdbStorageView *view, UdbStorageViewCursor *cursor)
{
	UdbStorageResult result = udb_storage_run_iterator_next(cursor->iterator, &cursor->record);
	if (result == UDB_STORAGE_NOT_FOUND)
	{
		cursor->present = 0;
		return UDB_STORAGE_OK;
	}
	if (result != UDB_STORAGE_OK)
		return result;
	if (cursor->record.stamp.sequence > view->watermark)
		return UDB_STORAGE_CORRUPT;
	cursor->present = 1;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_view_close(UdbStorageView **pointer)
{
	UdbStorageView *view;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	view = *pointer;
	for (size_t i = 0; i < view->point_count; i++)
		if (view->points[i].iterator)
		{
			result = udb_storage_run_iterator_close(&view->points[i].iterator);
			if (result != UDB_STORAGE_OK)
				return result;
		}
	for (size_t i = 0; i < view->prefix_count; i++)
	{
		if (view->prefixes[i].cursor.iterator)
		{
			result = udb_storage_run_iterator_close(&view->prefixes[i].cursor.iterator);
			if (result != UDB_STORAGE_OK)
				return result;
		}
		udb_storage_free(view->prefixes[i].covers);
		view->prefixes[i].covers = NULL;
	}
	udb_storage_free(view);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_view_seed(UdbStorageView *view, const UdbStorageRun *run,
											  const UdbStorageRow *prefix, UdbStoragePrefixCursor *cursor)
{
	UdbStorageRow ancestor = *prefix;
	ancestor.kind = UDB_STORAGE_VALUE_NONE;
	ancestor.value.data = NULL;
	ancestor.value.length = 0;
	ancestor.number = 0;
	for (unsigned int depth = 1; depth < prefix->depth; depth++)
	{
		UdbStorageCacheEntry *pin;
		UdbStorageRecord record;
		UdbStorageResult result;
		ancestor.depth = (unsigned char)depth;
		result = udb_storage_run_get(view->cache, run, &ancestor, &record, &pin);
		if (result == UDB_STORAGE_NOT_FOUND)
			continue;
		if (result != UDB_STORAGE_OK)
			return result;
		if (record.stamp.sequence > view->watermark)
		{
			udb_storage_cache_release(view->cache, pin);
			return UDB_STORAGE_CORRUPT;
		}
		if (!cursor->has_seed || udb_storage_stamp_compare(record.stamp, cursor->seed) > 0)
		{
			cursor->seed = record.stamp;
			cursor->has_seed = 1;
		}
		result = udb_storage_cache_release(view->cache, pin);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_view_create_mode(UdbStorageCache *cache, const UdbStorageRun *points,
													 size_t point_count, const UdbStorageRun *prefixes,
													 size_t prefix_count, const UdbStorageRow *prefix,
													 uint64_t watermark, int indexed, UdbStorageView **out)
{
	UdbStorageView *view;
	UdbStorageResult result;
	unsigned char block = prefix ? prefix->block : 0;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!cache || point_count > UDB_STORAGE_MAX_RUNS + 1 || prefix_count > UDB_STORAGE_MAX_RUNS + 1 ||
		(point_count && !points) || (prefix_count && !prefixes) || (prefix && !udb_storage_row_valid(prefix)))
		return UDB_STORAGE_ERROR;
	for (size_t i = 0; i < point_count + prefix_count; i++)
	{
		const UdbStorageRun *run = i < point_count ? points + i : prefixes + i - point_count;
		if (!udb_storage_run_valid(cache, run) ||
			run->stream != (i < point_count ? UDB_STORAGE_POINTS : UDB_STORAGE_PREFIXES))
			return UDB_STORAGE_ERROR;
		if (!block)
			block = run->block;
		if (run->block != block)
			return UDB_STORAGE_ERROR;
	}
	view = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*view));
	if (!view)
		return UDB_STORAGE_LIMIT;
	view->cache = cache;
	view->watermark = watermark;
	view->point_count = point_count;
	view->prefix_count = prefix_count;
	view->indexed = indexed;
	if (!point_count)
	{
		view->finished = 1;
		*out = view;
		return UDB_STORAGE_OK;
	}
	for (size_t i = 0; i < point_count; i++)
	{
		result = udb_storage_run_iterator_create(cache, points + i, prefix, &view->points[i].iterator);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_view_cursor_next(view, view->points + i);
		if (result != UDB_STORAGE_OK)
			goto fail;
	}
	for (size_t i = 0; i < prefix_count; i++)
	{
		UdbStoragePrefixCursor *cursor = view->prefixes + i;
		if (indexed)
		{
			view->indexed_prefixes[i] = prefixes[i];
			continue;
		}
		cursor->covers = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX,
										   UDB_STORAGE_COMPONENTS_MAX * sizeof(*cursor->covers));
		if (!cursor->covers)
		{
			result = UDB_STORAGE_LIMIT;
			goto fail;
		}
		if (prefix)
		{
			result = udb_storage_view_seed(view, prefixes + i, prefix, cursor);
			if (result != UDB_STORAGE_OK)
				goto fail;
		}
		result = udb_storage_run_iterator_create(cache, prefixes + i, prefix, &cursor->cursor.iterator);
		if (result != UDB_STORAGE_OK)
			goto fail;
		result = udb_storage_view_cursor_next(view, &cursor->cursor);
		if (result != UDB_STORAGE_OK)
			goto fail;
	}
	*out = view;
	return UDB_STORAGE_OK;
fail:
	udb_storage_view_close(&view);
	return result;
}

static UdbStorageResult udb_storage_view_create(UdbStorageCache *cache, const UdbStorageRun *points, size_t point_count,
												const UdbStorageRun *prefixes, size_t prefix_count,
												const UdbStorageRow *prefix, uint64_t watermark, UdbStorageView **out)
{
	return udb_storage_view_create_mode(cache, points, point_count, prefixes, prefix_count, prefix, watermark, 0, out);
}

static UdbStorageResult udb_storage_view_create_indexed(UdbStorageCache *cache, const UdbStorageRun *points,
														size_t point_count, const UdbStorageRun *prefixes,
														size_t prefix_count, const UdbStorageRow *prefix,
														uint64_t watermark, UdbStorageView **out)
{
	return udb_storage_view_create_mode(cache, points, point_count, prefixes, prefix_count, prefix, watermark, 1, out);
}

/* Hash leaves must not restart a whole global prefix stream per leaf. */
static UdbStorageResult udb_storage_prefix_cover_get(UdbStorageCache *cache, const UdbStorageRun *run,
													 const UdbStorageRow *key, uint64_t watermark,
													 UdbStorageStamp *maximum, int *present)
{
	UdbStorageRow ancestor = *key;
	ancestor.kind = UDB_STORAGE_VALUE_NONE;
	ancestor.value.data = NULL;
	ancestor.value.length = 0;
	ancestor.number = 0;
	for (unsigned int depth = 1; depth <= key->depth; depth++)
	{
		UdbStorageRecord record;
		UdbStorageCacheEntry *pin;
		UdbStorageResult result;
		ancestor.depth = (unsigned char)depth;
		result = udb_storage_run_get(cache, run, &ancestor, &record, &pin);
		if (result == UDB_STORAGE_NOT_FOUND)
			continue;
		if (result != UDB_STORAGE_OK)
			return result;
		if (record.stamp.sequence > watermark)
		{
			udb_storage_cache_release(cache, pin);
			return UDB_STORAGE_CORRUPT;
		}
		if (!*present || udb_storage_stamp_compare(record.stamp, *maximum) > 0)
		{
			*maximum = record.stamp;
			*present = 1;
		}
		result = udb_storage_cache_release(cache, pin);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_view_cover(UdbStorageView *view, UdbStoragePrefixCursor *cursor,
											   const UdbStorageRow *key, UdbStorageStamp *maximum, int *present)
{
	while (cursor->cursor.present && udb_storage_key_compare(&cursor->cursor.record.row, key) <= 0)
	{
		UdbStoragePrefixCover *cover;
		UdbStorageResult result;
		size_t length;
		while (cursor->depth &&
			   !udb_storage_key_has_prefix(&cursor->cursor.record.row, &cursor->covers[cursor->depth - 1].key))
			cursor->depth--;
		if (cursor->depth == UDB_STORAGE_COMPONENTS_MAX)
			return UDB_STORAGE_CORRUPT;
		cover = cursor->covers + cursor->depth++;
		result = udb_storage_run_copy_key(&cursor->cursor.record.row, cover->bytes, &length);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_row_decode(cover->bytes, length, &cover->key);
		if (result != UDB_STORAGE_OK)
			return result;
		cover->stamp = cursor->cursor.record.stamp;
		result = udb_storage_view_cursor_next(view, &cursor->cursor);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	while (cursor->depth && !udb_storage_key_has_prefix(key, &cursor->covers[cursor->depth - 1].key))
		cursor->depth--;
	if (cursor->has_seed && (!*present || udb_storage_stamp_compare(cursor->seed, *maximum) > 0))
	{
		*maximum = cursor->seed;
		*present = 1;
	}
	for (unsigned int i = 0; i < cursor->depth; i++)
		if (!*present || udb_storage_stamp_compare(cursor->covers[i].stamp, *maximum) > 0)
		{
			*maximum = cursor->covers[i].stamp;
			*present = 1;
		}
	return UDB_STORAGE_OK;
}

static int udb_storage_view_records_equal(const UdbStorageRecord *a, const UdbStorageRecord *b)
{
	if (a->operation != b->operation || a->row.kind != b->row.kind)
		return 0;
	if (a->row.kind == UDB_STORAGE_VALUE_NUMBER)
		return a->row.number == b->row.number;
	if (a->row.kind == UDB_STORAGE_VALUE_STRING)
		return a->row.value.length == b->row.value.length &&
			   !memcmp(a->row.value.data, b->row.value.data, a->row.value.length);
	return 1;
}

static UdbStorageResult udb_storage_view_next_record(UdbStorageView *view, UdbStorageRecord *record)
{
	if (!view || !record || udb_storage_overlaps(record, sizeof(*record), view, sizeof(*view)))
		return UDB_STORAGE_ERROR;
	if (view->failed)
		return view->failed;
	if (view->finished)
		return UDB_STORAGE_NOT_FOUND;
	for (;;)
	{
		UdbStorageRecord *minimum = NULL, *winner;
		UdbStorageStamp deleted;
		UdbStorageStamp winning_stamp;
		int has_delete = 0, visible;
		unsigned int matching[UDB_STORAGE_MAX_RUNS + 1];
		size_t count = 0, length = 0;
		UdbStorageResult result;
		for (size_t i = 0; i < view->point_count; i++)
			if (view->points[i].present &&
				(!minimum || udb_storage_key_compare(&view->points[i].record.row, &minimum->row) < 0))
				minimum = &view->points[i].record;
		if (!minimum)
		{
			view->finished = 1;
			return UDB_STORAGE_NOT_FOUND;
		}
		winner = minimum;
		for (size_t i = 0; i < view->point_count; i++)
		{
			UdbStorageRecord *candidate = &view->points[i].record;
			if (!view->points[i].present || udb_storage_key_compare(&candidate->row, &minimum->row))
				continue;
			for (size_t j = 0; j < count; j++)
			{
				UdbStorageRecord *previous = &view->points[matching[j]].record;
				if (!udb_storage_stamp_compare(previous->stamp, candidate->stamp) &&
					!udb_storage_view_records_equal(previous, candidate))
					return view->failed = UDB_STORAGE_CORRUPT;
			}
			matching[count++] = (unsigned int)i;
			if (udb_storage_stamp_compare(candidate->stamp, winner->stamp) > 0)
				winner = candidate;
		}
		for (size_t i = 0; i < view->prefix_count; i++)
		{
			result = view->indexed
						 ? udb_storage_prefix_cover_get(view->cache, view->indexed_prefixes + i, &minimum->row,
														view->watermark, &deleted, &has_delete)
						 : udb_storage_view_cover(view, view->prefixes + i, &minimum->row, &deleted, &has_delete);
			if (result != UDB_STORAGE_OK)
				return view->failed = result;
		}
		visible = winner->operation == UDB_STORAGE_PUT;
		if (has_delete)
		{
			int compare = udb_storage_stamp_compare(deleted, winner->stamp);
			/* Different operations cannot share an admitted transaction ordinal. */
			if (!compare)
				return view->failed = UDB_STORAGE_CORRUPT;
			if (compare > 0)
				visible = 0;
		}
		if (visible)
		{
			winning_stamp = winner->stamp;
			result = udb_storage_row_encode(&winner->row, view->output, sizeof(view->output), &length);
			if (result != UDB_STORAGE_OK)
				return view->failed = result;
		}
		/* Copy the winner before advancing any cursor and invalidating borrowed spans. */
		for (size_t i = 0; i < count; i++)
		{
			result = udb_storage_view_cursor_next(view, view->points + matching[i]);
			if (result != UDB_STORAGE_OK)
				return view->failed = result;
		}
		if (!visible)
			continue;
		result = udb_storage_row_decode(view->output, length, &record->row);
		if (result == UDB_STORAGE_OK)
		{
			record->stamp = winning_stamp;
			record->operation = UDB_STORAGE_PUT;
		}
		return result == UDB_STORAGE_OK ? result : (view->failed = result);
	}
}

static UdbStorageResult udb_storage_view_next(UdbStorageView *view, UdbStorageRow *row)
{
	UdbStorageRecord record;
	UdbStorageResult result;
	if (!row || (view && udb_storage_overlaps(row, sizeof(*row), view, sizeof(*view))))
		return UDB_STORAGE_ERROR;
	result = udb_storage_view_next_record(view, &record);
	if (result == UDB_STORAGE_OK)
		*row = record.row;
	return result;
}

typedef struct UdbStorageLookup
{
	UdbStorageRecord records[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageCacheEntry *pins[UDB_STORAGE_MAX_RUNS + 1];
	size_t count;
} UdbStorageLookup;

static UdbStorageResult udb_storage_lookup(UdbStorageCache *cache, const UdbStorageRun *points, size_t point_count,
										   const UdbStorageRun *prefixes, size_t prefix_count, const UdbStorageRow *key,
										   uint64_t watermark, unsigned char *bytes, size_t capacity,
										   UdbStorageRow *row)
{
	UdbStorageLookup *lookup;
	UdbStorageResult result;
	UdbStorageRecord *winner = NULL;
	UdbStorageStamp deleted;
	int has_delete = 0;
	size_t length;
	if (!cache || !bytes || !row || !udb_storage_row_valid(key) || point_count > UDB_STORAGE_MAX_RUNS + 1 ||
		prefix_count > UDB_STORAGE_MAX_RUNS + 1 || (point_count && !points) || (prefix_count && !prefixes) ||
		udb_storage_overlaps(bytes, capacity, row, sizeof(*row)))
		return UDB_STORAGE_ERROR;
	for (size_t i = 0; i < point_count + prefix_count; i++)
	{
		const UdbStorageRun *run = i < point_count ? points + i : prefixes + i - point_count;
		if (!udb_storage_run_valid(cache, run) || run->block != key->block ||
			run->stream != (i < point_count ? UDB_STORAGE_POINTS : UDB_STORAGE_PREFIXES))
			return UDB_STORAGE_ERROR;
	}
	lookup = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*lookup));
	if (!lookup)
		return UDB_STORAGE_LIMIT;
	for (size_t i = 0; i < point_count; i++)
	{
		UdbStorageRecord *record = lookup->records + lookup->count;
		result = udb_storage_run_get(cache, points + i, key, record, lookup->pins + lookup->count);
		if (result == UDB_STORAGE_NOT_FOUND)
			continue;
		if (result != UDB_STORAGE_OK)
			goto done;
		lookup->count++;
		if (record->stamp.sequence > watermark)
		{
			result = UDB_STORAGE_CORRUPT;
			goto done;
		}
		for (size_t j = 0; j + 1 < lookup->count; j++)
			if (!udb_storage_stamp_compare(lookup->records[j].stamp, record->stamp) &&
				!udb_storage_view_records_equal(lookup->records + j, record))
			{
				result = UDB_STORAGE_CORRUPT;
				goto done;
			}
		if (!winner || udb_storage_stamp_compare(record->stamp, winner->stamp) > 0)
			winner = record;
	}
	if (!winner)
	{
		result = UDB_STORAGE_NOT_FOUND;
		goto done;
	}
	for (size_t i = 0; i < prefix_count; i++)
	{
		result = udb_storage_prefix_cover_get(cache, prefixes + i, key, watermark, &deleted, &has_delete);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	if (has_delete && !udb_storage_stamp_compare(deleted, winner->stamp))
	{
		result = UDB_STORAGE_CORRUPT;
		goto done;
	}
	if (winner->operation == UDB_STORAGE_DELETE ||
		(has_delete && udb_storage_stamp_compare(deleted, winner->stamp) > 0))
	{
		result = UDB_STORAGE_NOT_FOUND;
		goto done;
	}
	for (size_t i = 0; i < lookup->count; i++)
		if (udb_storage_overlaps(bytes, capacity, lookup->pins[i], sizeof(*lookup->pins[i])) ||
			udb_storage_overlaps(row, sizeof(*row), lookup->pins[i], sizeof(*lookup->pins[i])))
		{
			result = UDB_STORAGE_ERROR;
			goto done;
		}
	result = udb_storage_row_encode(&winner->row, bytes, capacity, &length);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_row_decode(bytes, length, row);
done:
	for (size_t i = 0; i < lookup->count; i++)
	{
		UdbStorageResult released = udb_storage_cache_release(cache, lookup->pins[i]);
		if (released != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND)
			result = released;
	}
	udb_storage_free(lookup);
	return result;
}

static UdbStorageResult udb_storage_exists_prefix(UdbStorageCache *cache, const UdbStorageRun *points,
												  size_t point_count, const UdbStorageRun *prefixes,
												  size_t prefix_count, const UdbStorageRow *prefix, uint64_t watermark)
{
	UdbStorageView *view;
	UdbStorageRow row;
	UdbStorageResult result, closed;
	if (!prefix)
		return UDB_STORAGE_ERROR;
	result = udb_storage_view_create(cache, points, point_count, prefixes, prefix_count, prefix, watermark, &view);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_view_next(view, &row);
	closed = udb_storage_view_close(&view);
	return closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND ? closed : result;
}

/* End of udb_storage_runs.c.inc */
/* Inlined: udb_storage_sort.c.inc */
/* Native external sort for private snapshots/rebuild input. Fixed memtables
 * spill partition/key-sorted streams; eight-way merge passes retain at most
 * eight page cursors. Every step caps total private output at 64 MiB. A fixed
 * 4096-entry partition locator avoids rescanning the full snapshot per leaf. */

#define UDB_STORAGE_SORT_ENTRIES 16384
#define UDB_STORAGE_SORT_DATA ((size_t)832 * 1024)
#define UDB_STORAGE_SORT_FANIN 8

typedef struct UdbStorageSortEntry
{
	uint32_t offset;
	uint32_t length;
	unsigned int partition;
} UdbStorageSortEntry;

typedef struct UdbStorageSortTable
{
	UdbStorageSortEntry entries[UDB_STORAGE_SORT_ENTRIES];
	unsigned char bytes[UDB_STORAGE_SORT_DATA];
	size_t used;
	unsigned int count;
} UdbStorageSortTable;

typedef struct UdbStorageSortPartition
{
	uint64_t offset;
	uint64_t before;
	uint64_t count;
	unsigned int slot;
} UdbStorageSortPartition;

typedef struct UdbStorageSortCursor
{
	UdbStorageRawReader *reader;
	UdbStorageRecord record;
	unsigned int partition;
	int present;
} UdbStorageSortCursor;

struct UdbStorageSort
{
	UdbStorageDirectory *directory;
	UdbStorageCache *cache;
	UdbStorageFile files[2];
	char names[2][64];
	int created[2];
	UdbStorageCatalogRoot runs[2];
	UdbStorageRawRun final;
	UdbStorageSortTable *table;
	size_t table_charge;
	UdbStorageSortCursor cursors[UDB_STORAGE_SORT_FANIN];
	UdbStorageRawWriter *writer;
	UdbStorageRawReader *scanner;
	UdbStorageSortPartition partitions[UDB_STORAGE_BASE_PARTITIONS];
	UdbStorageRecordValidator validator;
	void *context;
	unsigned char bytes[UDB_STORAGE_RECORD_MAX];
	uint64_t rows;
	uint64_t rows_limit;
	uint64_t physical_limit;
	uint64_t input_next;
	uint64_t generation;
	uint64_t sequence;
	uint32_t ordinal_base;
	unsigned int readers;
	unsigned int cursor_count;
	unsigned int input;
	unsigned int output;
	unsigned char block;
	unsigned int phase; /* 0 ingest, 1 sealed, 2 merge, 3 scan, 4 ready */
	int job_full_cap;
	int source_set;
	UdbStorageResult failed;
};

static void udb_storage_sort_table_drop(UdbStorageSort *sort)
{
	if (sort->table)
	{
		udb_storage_free(sort->table);
		sort->table = NULL;
		udb_storage_memtable_uncharge(sort->directory->budget, sort->table_charge);
		sort->table_charge = 0;
	}
}

static UdbStorageResult udb_storage_sort_close(UdbStorageSort **pointer)
{
	UdbStorageSort *sort;
	UdbStorageResult result;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	sort = *pointer;
	if (sort->readers)
		return UDB_STORAGE_BUSY;
	udb_storage_sort_table_drop(sort);
	udb_storage_raw_writer_close(&sort->writer);
	udb_storage_raw_reader_close(&sort->scanner);
	for (unsigned int i = 0; i < UDB_STORAGE_SORT_FANIN; i++)
		udb_storage_raw_reader_close(&sort->cursors[i].reader);
	for (unsigned int i = 0; i < 2; i++)
	{
		if (sort->files[i].opened)
		{
			result = udb_storage_file_close(sort->files + i);
			if (result != UDB_STORAGE_OK)
				return result;
		}
		if (sort->created[i])
		{
			result = udb_storage_file_remove_private(sort->directory, sort->names[i]);
			if (result != UDB_STORAGE_OK)
				return result;
			sort->created[i] = 0;
		}
	}
	udb_storage_free(sort);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_create(UdbStorageDirectory *directory, unsigned char block,
												uint64_t rows_limit, uint64_t physical_limit,
												UdbStorageRecordValidator validator, void *context,
												UdbStorageSort **out)
{
	static const char hex[] = "0123456789abcdef";
	UdbStorageSort *sort;
	UdbStorageResult result;
	unsigned char nonce[16];
	char token[33];
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!directory || !directory->opened || !directory->budget->cache || !udb_storage_block_valid(block) ||
		!rows_limit || physical_limit < UDB_STORAGE_PAGE_SIZE * 8 || physical_limit > INT64_MAX)
		return UDB_STORAGE_ERROR;
	sort = udb_storage_alloc(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*sort));
	if (!sort)
		return UDB_STORAGE_LIMIT;
	sort->directory = directory;
	sort->cache = directory->budget->cache;
	sort->block = block;
	sort->rows_limit = rows_limit;
	sort->physical_limit = physical_limit;
	sort->validator = validator;
	sort->context = context;
	sort->generation = 1;
	sort->table_charge = udb_storage_allocation_charge(sizeof(*sort->table));
	result = udb_storage_memtable_charge(directory->budget, sort->table_charge);
	if (result != UDB_STORAGE_OK)
		goto fail;
	sort->table = udb_storage_alloc(directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*sort->table));
	if (!sort->table)
	{
		udb_storage_memtable_uncharge(directory->budget, sort->table_charge);
		sort->table_charge = 0;
		result = UDB_STORAGE_LIMIT;
		goto fail;
	}
	if (RAND_bytes(nonce, sizeof(nonce)) != 1)
	{
		result = UDB_STORAGE_ERROR;
		goto fail;
	}
	for (unsigned int i = 0; i < sizeof(nonce); i++)
	{
		token[2 * i] = hex[nonce[i] >> 4];
		token[2 * i + 1] = hex[nonce[i] & 15];
	}
	token[32] = 0;
	for (unsigned int i = 0; i < 2; i++)
	{
		snprintf(sort->names[i], sizeof(sort->names[i]), "stage_%s_%c_%u.db", token, block, i);
		result = udb_storage_file_open(directory, sort->names[i], 2, sort->files + i);
		if (result != UDB_STORAGE_OK)
			goto fail;
		sort->created[i] = 1;
		result = udb_storage_private_header(sort->files + i, sort->names[i]);
		if (result != UDB_STORAGE_OK)
			goto fail;
		sort->runs[i].object = 1;
	}
	*out = sort;
	return UDB_STORAGE_OK;
fail:
	/* Preserve a failed object only if its native cleanup itself is blocked. */
	if (udb_storage_sort_close(&sort) != UDB_STORAGE_OK)
		*out = sort;
	return result;
}

static UdbStorageResult udb_storage_sort_job_begin(UdbStorageSort *sort, unsigned int target)
{
	uint64_t sizes[2], available;
	UdbStorageResult result;
	for (unsigned int i = 0; i < 2; i++)
	{
		result = udb_storage_file_size(sort->files + i, sizes + i);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (sizes[0] > sort->physical_limit || sizes[1] > sort->physical_limit - sizes[0])
		return UDB_STORAGE_LIMIT;
	available = sort->physical_limit - sizes[0] - sizes[1];
	sort->job_full_cap = available >= UDB_STORAGE_COMPACTION_MAX;
	if (available > UDB_STORAGE_COMPACTION_MAX)
		available = UDB_STORAGE_COMPACTION_MAX;
	if (available < UDB_STORAGE_PAGE_SIZE)
		return UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(&sort->files[target], sizes[target] + available);
	return UDB_STORAGE_OK;
}

static int udb_storage_sort_compare(UdbStorageSortTable *table, const UdbStorageSortEntry *a,
									const UdbStorageSortEntry *b)
{
	UdbStorageRecord left, right;
	if (a->partition != b->partition)
		return a->partition < b->partition ? -1 : 1;
	/* Entries were validated before admission and their copied bytes are private. */
	if (udb_storage_record_decode(table->bytes + a->offset, a->length, &left) != UDB_STORAGE_OK ||
		udb_storage_record_decode(table->bytes + b->offset, b->length, &right) != UDB_STORAGE_OK)
		abort();
	return udb_storage_key_compare(&left.row, &right.row);
}

static void udb_storage_sort_sift(UdbStorageSortTable *table, unsigned int index, unsigned int count)
{
	while (index < count / 2)
	{
		unsigned int child = 2 * index + 1;
		UdbStorageSortEntry temporary;
		if (child + 1 < count &&
			udb_storage_sort_compare(table, table->entries + child, table->entries + child + 1) < 0)
			child++;
		if (udb_storage_sort_compare(table, table->entries + index, table->entries + child) >= 0)
			break;
		temporary = table->entries[index];
		table->entries[index] = table->entries[child];
		table->entries[child] = temporary;
		index = child;
	}
}

static UdbStorageResult udb_storage_sort_run_get(UdbStorageSort *sort, unsigned int file, uint64_t ordinal,
												 UdbStorageRawRun *run)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	if (ordinal >= sort->runs[file].count)
		return UDB_STORAGE_NOT_FOUND;
	udb_storage_run_key(ordinal, key);
	result = udb_storage_catalog_get(sort->cache, sort->files + file, sort->runs + file, key, value);
	if (result == UDB_STORAGE_NOT_FOUND)
		return UDB_STORAGE_CORRUPT;
	if (result == UDB_STORAGE_OK)
		result = udb_storage_raw_decode(value, run);
	if (result == UDB_STORAGE_OK &&
		(run->block != sort->block || run->generation != sort->runs[file].generation || run->object != ordinal + 2))
		return UDB_STORAGE_CORRUPT;
	return result;
}

static UdbStorageResult udb_storage_sort_run_save(UdbStorageSort *sort, unsigned int file, const UdbStorageRawRun *run)
{
	unsigned char key[64], value[128];
	UdbStorageCatalogRoot changed;
	UdbStorageResult result = udb_storage_raw_encode(run, value);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_run_key(sort->runs[file].count, key);
	result = udb_storage_catalog_change(sort->cache, sort->files + file, sort->runs + file, run->generation, key, value,
										&changed);
	if (result == UDB_STORAGE_OK)
		sort->runs[file] = changed;
	return result;
}

static UdbStorageResult udb_storage_sort_spill(UdbStorageSort *sort)
{
	UdbStorageSortTable *table = sort->table;
	UdbStorageRawWriter *writer = NULL;
	UdbStorageRawRun run;
	UdbStorageResult result;
	if (!table || !table->count)
		return UDB_STORAGE_OK;
	result = udb_storage_sort_job_begin(sort, 0);
	if (result != UDB_STORAGE_OK)
		return result;
	for (unsigned int i = table->count / 2; i; i--)
		udb_storage_sort_sift(table, i - 1, table->count);
	for (unsigned int i = table->count; i > 1; i--)
	{
		UdbStorageSortEntry temporary = table->entries[0];
		table->entries[0] = table->entries[i - 1];
		table->entries[i - 1] = temporary;
		udb_storage_sort_sift(table, 0, i - 1);
	}
	for (unsigned int i = 1; i < table->count; i++)
		if (!udb_storage_sort_compare(table, table->entries + i - 1, table->entries + i))
		{
			result = UDB_STORAGE_REJECTED;
			goto done;
		}
	result = udb_storage_raw_writer_create(sort->files, sort->block, 1, sort->runs[0].count + 2, &writer);
	if (result != UDB_STORAGE_OK)
		goto done;
	for (unsigned int i = 0; i < table->count; i++)
	{
		UdbStorageSortEntry *entry = table->entries + i;
		result = udb_storage_raw_writer_add(writer, table->bytes + entry->offset, entry->length);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	result = udb_storage_raw_writer_finish(writer, &run);
	if (result == UDB_STORAGE_OK)
		result = udb_storage_sort_run_save(sort, 0, &run);
	if (result == UDB_STORAGE_OK)
	{
		table->count = 0;
		table->used = 0;
	}
done:
	udb_storage_file_job_limit(&sort->files[0], 0);
	udb_storage_raw_writer_close(&writer);
	return result;
}

static UdbStorageResult udb_storage_sort_add(UdbStorageSort *sort, const unsigned char *bytes, size_t length)
{
	UdbStorageRecord record;
	UdbStorageResult result;
	unsigned int partition;
	UdbStorageSortEntry *entry;
	if (!sort || sort->phase || !bytes || !sort->table ||
		udb_storage_overlaps(bytes, length, sort->table, sizeof(*sort->table)))
		return UDB_STORAGE_ERROR;
	if (sort->failed)
		return sort->failed;
	if (sort->rows == sort->rows_limit)
		return sort->failed = UDB_STORAGE_LIMIT;
	result = udb_storage_record_decode(bytes, length, &record);
	if (result != UDB_STORAGE_OK)
		return sort->failed = result;
	if (record.row.block != sort->block || record.operation != UDB_STORAGE_PUT)
		return sort->failed = UDB_STORAGE_CORRUPT;
	if (sort->validator)
	{
		result = sort->validator(&record, sort->context);
		if (result != UDB_STORAGE_OK)
			return sort->failed = result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_CORRUPT : result;
	}
	result = udb_storage_raw_partition(&record, &partition);
	if (result != UDB_STORAGE_OK)
		return sort->failed = result;
	if (sort->table->count == UDB_STORAGE_SORT_ENTRIES || length > sizeof(sort->table->bytes) - sort->table->used)
	{
		result = udb_storage_sort_spill(sort);
		if (result != UDB_STORAGE_OK)
			return sort->failed = result;
	}
	entry = sort->table->entries + sort->table->count++;
	entry->offset = (uint32_t)sort->table->used;
	entry->length = (uint32_t)length;
	entry->partition = partition;
	memcpy(sort->table->bytes + sort->table->used, bytes, length);
	sort->table->used += length;
	sort->rows++;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_seal(UdbStorageSort *sort)
{
	UdbStorageResult result;
	if (!sort || sort->phase)
		return UDB_STORAGE_ERROR;
	if (sort->failed)
		return sort->failed;
	result = udb_storage_sort_spill(sort);
	if (result != UDB_STORAGE_OK)
		return sort->failed = result;
	udb_storage_sort_table_drop(sort);
	sort->phase = 1;
	if (!sort->rows)
	{
		memset(&sort->final, 0, sizeof(sort->final));
		sort->final.block = sort->block;
		sort->final.generation = 1;
		sort->final.object = 2;
		sort->phase = 4;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_cursor_next(UdbStorageSortCursor *cursor)
{
	UdbStorageResult result = udb_storage_raw_reader_next(cursor->reader, &cursor->record);
	if (result == UDB_STORAGE_NOT_FOUND)
	{
		cursor->present = 0;
		return UDB_STORAGE_OK;
	}
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_raw_partition(&cursor->record, &cursor->partition);
	if (result == UDB_STORAGE_OK)
		cursor->present = 1;
	return result;
}

static UdbStorageResult udb_storage_sort_pass_begin(UdbStorageSort *sort)
{
	UdbStorageResult result;
	sort->output = sort->input ^ 1;
	if (sort->generation == UINT64_MAX)
		return UDB_STORAGE_LIMIT;
	result = udb_storage_file_resize(sort->files + sort->output, UDB_STORAGE_PAGE_SIZE);
	if (result != UDB_STORAGE_OK)
		return result;
	memset(sort->runs + sort->output, 0, sizeof(sort->runs[0]));
	sort->runs[sort->output].object = 1;
	sort->generation++;
	sort->input_next = 0;
	sort->phase = 2;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_group_begin(UdbStorageSort *sort)
{
	UdbStorageResult result;
	sort->cursor_count = 0;
	while (sort->input_next < sort->runs[sort->input].count && sort->cursor_count < UDB_STORAGE_SORT_FANIN)
	{
		UdbStorageSortCursor *cursor = sort->cursors + sort->cursor_count;
		UdbStorageRawRun run;
		result = udb_storage_sort_run_get(sort, sort->input, sort->input_next++, &run);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_raw_reader_create(sort->files + sort->input, &run, &cursor->reader);
		if (result != UDB_STORAGE_OK)
			return result;
		sort->cursor_count++;
		result = udb_storage_sort_cursor_next(cursor);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return udb_storage_raw_writer_create(sort->files + sort->output, sort->block, sort->generation,
										 sort->runs[sort->output].count + 2, &sort->writer);
}

static UdbStorageResult udb_storage_sort_scan_begin(UdbStorageSort *sort)
{
	UdbStorageResult result = udb_storage_sort_run_get(sort, sort->input, 0, &sort->final);
	if (result != UDB_STORAGE_OK)
		return result;
	if (sort->final.count != sort->rows)
		return UDB_STORAGE_CORRUPT;
	result = udb_storage_file_resize(sort->files + (sort->input ^ 1), UDB_STORAGE_PAGE_SIZE);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_raw_reader_create(sort->files + sort->input, &sort->final, &sort->scanner);
	if (result == UDB_STORAGE_OK)
		sort->phase = 3;
	return result;
}

static UdbStorageResult udb_storage_sort_scan_step(UdbStorageSort *sort)
{
	UdbStorageRecord record;
	UdbStorageResult result;
	/* Bounded CPU/read work too: at most 65,536 records per scan job. */
	for (unsigned int i = 0; i < 65536; i++)
	{
		unsigned int partition, slot;
		uint64_t offset, before;
		UdbStorageSortPartition *entry;
		result = udb_storage_raw_reader_next(sort->scanner, &record);
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			udb_storage_raw_reader_close(&sort->scanner);
			sort->phase = 4;
			return UDB_STORAGE_OK;
		}
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_raw_partition(&record, &partition);
		if (result != UDB_STORAGE_OK)
			return result;
		entry = sort->partitions + partition;
		if (!entry->count)
		{
			result = udb_storage_raw_reader_position(sort->scanner, &offset, &slot, &before);
			if (result != UDB_STORAGE_OK)
				return result;
			entry->offset = offset;
			entry->slot = slot;
			entry->before = before;
		}
		entry->count++;
	}
	return UDB_STORAGE_BUSY;
}

static UdbStorageResult udb_storage_sort_step(UdbStorageSort *sort)
{
	UdbStorageResult result;
	UdbStorageRawRun run;
	size_t length;
	unsigned int records = 0;
	if (!sort || !sort->phase)
		return UDB_STORAGE_ERROR;
	if (sort->failed)
		return sort->failed;
	if (sort->phase == 4)
		return UDB_STORAGE_OK;
	if (sort->phase == 3)
	{
		result = udb_storage_sort_scan_step(sort);
		if (result < UDB_STORAGE_NOT_FOUND && result != UDB_STORAGE_BUSY)
			sort->failed = result;
		return result;
	}
	if (sort->phase == 1)
	{
		if (sort->runs[sort->input].count == 1)
			return (result = udb_storage_sort_scan_begin(sort)) == UDB_STORAGE_OK ? UDB_STORAGE_BUSY
																				  : (sort->failed = result);
		result = udb_storage_sort_pass_begin(sort);
		if (result != UDB_STORAGE_OK)
			return sort->failed = result;
	}
	result = udb_storage_sort_job_begin(sort, sort->output);
	if (result != UDB_STORAGE_OK)
		return sort->failed = result;
	if (!sort->writer)
	{
		result = udb_storage_sort_group_begin(sort);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	for (;;)
	{
		UdbStorageSortCursor *minimum = NULL;
		for (unsigned int i = 0; i < sort->cursor_count; i++)
		{
			UdbStorageSortCursor *cursor = sort->cursors + i;
			int compare;
			if (!cursor->present)
				continue;
			if (!minimum)
			{
				minimum = cursor;
				continue;
			}
			compare = cursor->partition < minimum->partition ? -1
					  : cursor->partition > minimum->partition
						  ? 1
						  : udb_storage_key_compare(&cursor->record.row, &minimum->record.row);
			if (!compare)
			{
				result = UDB_STORAGE_REJECTED;
				goto done;
			}
			if (compare < 0)
				minimum = cursor;
		}
		if (!minimum)
			break;
		result = udb_storage_record_encode(&minimum->record, sort->bytes, sizeof(sort->bytes), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_raw_writer_add(sort->writer, sort->bytes, length);
		if (result == UDB_STORAGE_LIMIT)
		{
			/* Conservative reserve for the eventual run-directory COW path. */
			uint64_t size;
			UdbStorageResult sized = udb_storage_file_size(sort->files + sort->output, &size);
			if (sized != UDB_STORAGE_OK)
				result = sized;
			else if (size > sort->files[sort->output].append_limit)
				result = UDB_STORAGE_CORRUPT;
			else if (sort->job_full_cap && sort->files[sort->output].append_limit - size < UDB_STORAGE_PAGE_SIZE)
				result = UDB_STORAGE_BUSY;
			goto done;
		}
		if (result != UDB_STORAGE_OK)
			goto done;
		result = udb_storage_sort_cursor_next(minimum);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (++records == 65536)
		{
			result = UDB_STORAGE_BUSY;
			goto done;
		}
	}
	/* Ensure final DATA + directory metadata fit this output job. Yield before
	 * finish, not after consuming a descriptor that cannot yet be catalogued. */
	{
		uint64_t size;
		uint64_t reserve = (uint64_t)(2 * (sort->runs[sort->output].height + 1) + 2) * UDB_STORAGE_PAGE_SIZE;
		result = udb_storage_file_size(sort->files + sort->output, &size);
		if (result != UDB_STORAGE_OK)
			goto done;
		if (reserve > sort->files[sort->output].append_limit - size)
		{
			result = sort->job_full_cap ? UDB_STORAGE_BUSY : UDB_STORAGE_LIMIT;
			goto done;
		}
	}
	result = udb_storage_raw_writer_finish(sort->writer, &run);
	if (result != UDB_STORAGE_OK)
		goto done;
	result = udb_storage_sort_run_save(sort, sort->output, &run);
	if (result != UDB_STORAGE_OK)
		goto done;
	udb_storage_raw_writer_close(&sort->writer);
	for (unsigned int i = 0; i < sort->cursor_count; i++)
		udb_storage_raw_reader_close(&sort->cursors[i].reader);
	sort->cursor_count = 0;
	if (sort->input_next == sort->runs[sort->input].count)
	{
		/* Entire pass has closed its old readers before reusing the private file. */
		sort->input = sort->output;
		sort->phase = 1;
	}
	result = UDB_STORAGE_BUSY;
done:
	udb_storage_file_job_limit(&sort->files[sort->output], 0);
	if (result != UDB_STORAGE_OK && result != UDB_STORAGE_BUSY)
		sort->failed = result;
	return result;
}

typedef struct UdbStorageSortSource
{
	UdbStorageSort *sort;
	UdbStorageRawReader *reader;
	UdbStorageLogicalPosition position;
	uint64_t remaining;
} UdbStorageSortSource;

static UdbStorageResult udb_storage_sort_source_open(void *opaque, const UdbStorageLogicalPosition *position,
													 void **out)
{
	UdbStorageSort *sort = opaque;
	UdbStorageSortSource *source;
	const UdbStorageSortPartition *partition;
	UdbStorageResult result;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!sort || sort->phase != 4 || sort->failed || !udb_storage_logical_position_valid(position) ||
		position->block != sort->block || sort->readers == UINT_MAX)
		return UDB_STORAGE_ERROR;
	source = udb_storage_alloc(sort->directory->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*source));
	if (!source)
		return UDB_STORAGE_LIMIT;
	source->sort = sort;
	source->position = *position;
	partition = sort->partitions + position->partition;
	source->remaining = partition->count;
	if (source->remaining)
	{
		result = udb_storage_raw_reader_create(sort->files + sort->input, &sort->final, &source->reader);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_raw_reader_seek(source->reader, partition->offset, partition->slot, partition->before);
		if (result != UDB_STORAGE_OK)
		{
			udb_storage_raw_reader_close(&source->reader);
			udb_storage_free(source);
			return result;
		}
	}
	sort->readers++;
	*out = source;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_source_next(void *opaque, UdbStorageRecord *record)
{
	UdbStorageSortSource *source = opaque;
	UdbStorageResult result;
	unsigned char hash[32];
	unsigned int partition;
	if (!source || !record)
		return UDB_STORAGE_ERROR;
	while (source->remaining)
	{
		result = udb_storage_raw_reader_next(source->reader, record);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_CORRUPT : result;
		result = udb_storage_key_hash(&record->row, hash, &partition);
		if (result != UDB_STORAGE_OK)
			return result;
		if (partition != source->position.partition)
			return UDB_STORAGE_CORRUPT;
		source->remaining--;
		if (!udb_storage_logical_contains(&source->position, hash, partition))
			continue;
		if (source->sort->sequence)
		{
			if (record->stamp.sequence || record->stamp.ordinal > UINT32_MAX - source->sort->ordinal_base)
				return UDB_STORAGE_CORRUPT;
			record->stamp.sequence = source->sort->sequence;
			record->stamp.ordinal += source->sort->ordinal_base;
		}
		return UDB_STORAGE_OK;
	}
	return UDB_STORAGE_NOT_FOUND;
}

static UdbStorageResult udb_storage_sort_source_close(void **pointer)
{
	UdbStorageSortSource *source;
	if (!pointer || !*pointer)
		return UDB_STORAGE_ERROR;
	source = *pointer;
	udb_storage_raw_reader_close(&source->reader);
	if (!source->sort->readers)
		abort();
	source->sort->readers--;
	udb_storage_free(source);
	*pointer = NULL;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_sort_source(UdbStorageSort *sort, uint64_t sequence, uint32_t ordinal_base,
												UdbStorageSource *source)
{
	if (!sort || !source || sort->phase != 4 || sort->failed || sort->readers ||
		(sequence && sort->rows > (uint64_t)UINT32_MAX - ordinal_base + 1) || (!sequence && ordinal_base) ||
		(sort->source_set && (sequence != sort->sequence || ordinal_base != sort->ordinal_base)))
		return UDB_STORAGE_ERROR;
	sort->sequence = sequence;
	sort->ordinal_base = ordinal_base;
	sort->source_set = 1;
	source->open = udb_storage_sort_source_open;
	source->next = udb_storage_sort_source_next;
	source->close = udb_storage_sort_source_close;
	source->context = sort;
	return UDB_STORAGE_OK;
}

static uint64_t udb_storage_sort_rows(const UdbStorageSort *sort)
{
	return sort ? sort->rows : 0;
}
static UdbStorageResult udb_storage_sort_size(UdbStorageSort *sort, uint64_t *bytes)
{
	uint64_t sizes[2];
	UdbStorageResult result;
	if (!sort || !bytes)
		return UDB_STORAGE_ERROR;
	for (unsigned int i = 0; i < 2; i++)
	{
		result = udb_storage_file_size(sort->files + i, sizes + i);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (sizes[0] > UINT64_MAX - sizes[1])
		return UDB_STORAGE_LIMIT;
	*bytes = sizes[0] + sizes[1];
	return UDB_STORAGE_OK;
}
static UdbStorageResult udb_storage_sort_partition_count(UdbStorageSort *sort, unsigned int partition, uint64_t *count)
{
	if (!sort || !count || sort->phase != 4 || sort->failed || partition >= UDB_STORAGE_BASE_PARTITIONS)
		return UDB_STORAGE_ERROR;
	*count = sort->partitions[partition].count;
	return UDB_STORAGE_OK;
}

/* End of udb_storage_sort.c.inc */
/* Inlined: udb_storage_stage_jobs.c.inc */
/* Writer-owned snapshot jobs. Inputs are copied canonical bytes and stable IDs.
 * BEGIN/PUT have no durability receipt. END only commits a pending root; the
 * active six-block generation changes exclusively at PROMOTE. Daemon callbacks
 * must check the completion revision before ACK, effects or readiness changes. */

typedef struct UdbStorageStageOwner
{
	struct UdbStorageStageOwner *next;
	UdbStorageStageHead head;
	UdbStorageStageSpec specs[6];
	UdbStorageSort *sorts[6];
	unsigned int admitted;
	unsigned int sealed;
	unsigned int ready;
	int failed;
} UdbStorageStageOwner;

typedef struct UdbStorageStageWork
{
	UdbStorageStageOwner *owner;
	UdbStorageRebuild *rebuild;
	UdbStorageSource source;
	UdbStorageBlockRoot root;
	UdbStorageLogicalSummary partitions[UDB_STORAGE_BASE_PARTITIONS];
	uint64_t job;
	uint64_t sequence;
	uint64_t next_object;
	uint64_t start;
	uint64_t metadata_start;
	unsigned int block;
	unsigned int partition;
	unsigned int phase;
} UdbStorageStageWork;

static int udb_storage_stage_block_index(unsigned char block)
{
	for (unsigned int i = 0; i < 6; i++)
		if (udb_storage_blocks[i] == block)
			return (int)i;
	return -1;
}

static int udb_storage_stage_same_head(const UdbStorageStageHead *a, const UdbStorageStageHead *b)
{
	return a->owner == b->owner && a->round == b->round && a->source_epoch == b->source_epoch &&
		   a->source_sequence == b->source_sequence && !memcmp(a->source_sid, b->source_sid, 4);
}

static UdbStorageStageOwner *udb_storage_stage_owner_find(UdbStorageEngine *engine, uint64_t id)
{
	UdbStorageStageOwner *owner = engine->stages;
	for (; owner && owner->head.owner != id; owner = owner->next)
		;
	return owner;
}

static UdbStorageResult udb_storage_stage_owner_close(UdbStorageEngine *engine, UdbStorageStageOwner *owner)
{
	UdbStorageStageOwner **link = (UdbStorageStageOwner **)&engine->stages;
	UdbStorageResult result;
	if (!owner)
		return UDB_STORAGE_OK;
	for (unsigned int i = 0; i < 6; i++)
		if (owner->sorts[i])
		{
			result = udb_storage_sort_close(owner->sorts + i);
			if (result != UDB_STORAGE_OK)
				return result;
		}
	while (*link && *link != owner)
		link = &(*link)->next;
	if (!*link)
		return UDB_STORAGE_ERROR;
	*link = owner->next;
	udb_storage_free(owner);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_work_close(UdbStorageEngine *engine)
{
	UdbStorageStageWork *work = engine->work;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (!work)
		return result;
	if (work->rebuild)
		result = udb_storage_rebuild_close(&work->rebuild);
	udb_storage_free(work);
	engine->work = NULL;
	return result;
}

static UdbStorageResult udb_storage_stage_jobs_cleanup(UdbStorageEngine *engine)
{
	UdbStorageResult result = udb_storage_stage_work_close(engine);
	if (result != UDB_STORAGE_OK)
		return result;
	while (engine->stages)
	{
		result = udb_storage_stage_owner_close(engine, engine->stages);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_spec_read(const UdbStorageJob *job, UdbStorageStageSpec *spec)
{
	if (job->length != sizeof(*spec))
		return UDB_STORAGE_REJECTED;
	memcpy(spec, job->payload, sizeof(*spec));
	if (!job->owner || spec->head.owner != job->owner || spec->head.mask || !spec->head.round ||
		!udb_storage_sid_valid(spec->head.source_sid) || udb_storage_stage_block_index(spec->block) < 0 ||
		!spec->rows_limit || spec->rows_limit > (uint64_t)UINT32_MAX + 1 || spec->count > spec->rows_limit ||
		spec->physical_limit < 8 * UDB_STORAGE_PAGE_SIZE || spec->physical_limit > INT64_MAX)
		return UDB_STORAGE_REJECTED;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_owner_admit(UdbStorageEngine *engine, const UdbStorageStageSpec *spec,
													  UdbStorageStageOwner **out)
{
	UdbStorageStageOwner *owner = udb_storage_stage_owner_find(engine, spec->head.owner);
	if (owner && (owner->failed || !udb_storage_stage_same_head(&owner->head, &spec->head)))
		return UDB_STORAGE_REJECTED;
	if (!owner)
	{
		owner = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*owner));
		if (!owner)
			return UDB_STORAGE_LIMIT;
		owner->head = spec->head;
		owner->next = engine->stages;
		engine->stages = owner;
	}
	*out = owner;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_summary_read(unsigned int partition, UdbStorageLogicalSummary *summary,
													   void *opaque)
{
	UdbStorageStageWork *work = opaque;
	if (partition >= UDB_STORAGE_BASE_PARTITIONS)
		return UDB_STORAGE_ERROR;
	*summary = work->partitions[partition];
	return UDB_STORAGE_OK;
}

/* Counts copied sort output and new stable-container bytes, not the size of
 * already-existing active generations. No truncation of active data occurs. */
static UdbStorageResult udb_storage_stage_remaining(UdbStorageEngine *engine, UdbStorageStageWork *work,
													uint64_t *remaining)
{
	uint64_t sorted, data = engine->files[work->block].allocated_bytes;
	uint64_t metadata = work->block == 3 ? work->metadata_start : engine->files[3].allocated_bytes, used;
	UdbStorageResult result = udb_storage_sort_size(work->owner->sorts[work->block], &sorted);
	if (result != UDB_STORAGE_OK)
		return result;
	if (data < work->start || metadata < work->metadata_start)
		return UDB_STORAGE_CORRUPT;
	used = data - work->start;
	if (sorted > UINT64_MAX - used)
		return UDB_STORAGE_LIMIT;
	used += sorted;
	if (metadata - work->metadata_start > UINT64_MAX - used)
		return UDB_STORAGE_LIMIT;
	used += metadata - work->metadata_start;
	if (used > work->owner->specs[work->block].physical_limit)
		return UDB_STORAGE_LIMIT;
	*remaining = work->owner->specs[work->block].physical_limit - used;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_end_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
													  const UdbStorageState *base, UdbStorageState *candidate,
													  UdbStorageStageOwner *owner, unsigned int block)
{
	UdbStorageStageWork *work = engine->work;
	UdbStorageStageSpec *spec = owner->specs + block;
	UdbStorageResult result;
	uint64_t remaining, count;
	if (!work)
	{
		if (owner->sealed & (1U << block) || !owner->sorts[block] ||
			udb_storage_sort_rows(owner->sorts[block]) != spec->count)
			return UDB_STORAGE_REJECTED;
		if (base->sequence == UINT64_MAX)
			return UDB_STORAGE_LIMIT;
		work = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*work));
		if (!work)
			return UDB_STORAGE_LIMIT;
		engine->work = work;
		work->owner = owner;
		work->block = block;
		work->job = job->id;
		work->sequence = base->sequence + 1;
		work->next_object = base->next_object;
		work->start = engine->files[block].allocated_bytes;
		work->metadata_start = engine->files[3].allocated_bytes;
		result = udb_storage_sort_seal(owner->sorts[block]);
		if (result != UDB_STORAGE_OK)
			return result;
		owner->sealed |= 1U << block;
		work->root.block = spec->block;
		work->root.catalog.object = block + 1;
		work->root.generation = base->generation;
		work->root.watermark = work->sequence;
	}
	if (work->job != job->id || work->owner != owner || work->block != block)
		return UDB_STORAGE_ERROR;
	if (!work->phase)
	{
		result = udb_storage_sort_step(owner->sorts[block]);
		if (result == UDB_STORAGE_BUSY)
			return UDB_STORAGE_CONTINUE;
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_sort_source(owner->sorts[block], work->sequence, 0, &work->source);
		if (result != UDB_STORAGE_OK)
			return result;
		/* The schema owner must verify complete N/K entity profiles, not merely
		 * each individual attribute, before any root receives durable admission. */
		result = engine->stage_validator(&work->source, spec->block, engine->validation_context);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_CORRUPT || result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_REJECTED : result;
		work->phase = 1;
	}
	if (work->partition < UDB_STORAGE_BASE_PARTITIONS)
	{
		UdbStorageLogicalPosition position = {0};
		position.block = spec->block;
		position.partition = (uint16_t)work->partition;
		result = udb_storage_sort_partition_count(owner->sorts[block], work->partition, &count);
		if (result != UDB_STORAGE_OK)
			return result;
		if (!count)
		{
			result = udb_storage_logical_empty(&position, work->partitions + work->partition);
			if (result != UDB_STORAGE_OK)
				return result;
			work->partition++;
			return UDB_STORAGE_CONTINUE;
		}
		if (!work->rebuild)
		{
			result = udb_storage_rebuild_create(engine->cache, engine->files + block, &work->root, &position,
												base->generation, work->sequence, work->next_object, &work->source,
												&work->rebuild);
			if (result == UDB_STORAGE_OK)
				result = udb_storage_rebuild_defer(work->rebuild);
			if (result != UDB_STORAGE_OK)
				return result;
		}
		result = udb_storage_stage_remaining(engine, work, &remaining);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_rebuild_allowance(
				work->rebuild, remaining < UDB_STORAGE_COMPACTION_MAX ? remaining : UDB_STORAGE_COMPACTION_MAX);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_rebuild_step(work->rebuild, &work->root, &work->next_object);
		if (result == UDB_STORAGE_BUSY)
			return UDB_STORAGE_CONTINUE;
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_rebuild_summary(work->rebuild, work->partitions + work->partition);
		if (result != UDB_STORAGE_OK)
			return result;
		result = udb_storage_rebuild_close(&work->rebuild);
		if (result != UDB_STORAGE_OK)
			return result;
		work->partition++;
		return UDB_STORAGE_CONTINUE;
	}
	result = udb_storage_logical_block(spec->block, udb_storage_stage_summary_read, work, &work->root.summary);
	if (result != UDB_STORAGE_OK)
		return result;
	if (work->root.summary.count != spec->count || memcmp(work->root.summary.digest, spec->digest, 32))
		return UDB_STORAGE_REJECTED;
	result = udb_storage_block_validate(engine->cache, engine->files + block, &work->root, engine->validator,
										engine->validation_context);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_stage_remaining(engine, work, &remaining);
	if (result != UDB_STORAGE_OK || remaining < UDB_STORAGE_PAGE_SIZE)
		return result != UDB_STORAGE_OK ? result : UDB_STORAGE_LIMIT;
	uint64_t size;
	result = udb_storage_file_size(engine->files + 3, &size);
	if (result != UDB_STORAGE_OK)
		return result;
	if (remaining > UDB_STORAGE_COMPACTION_MAX)
		remaining = UDB_STORAGE_COMPACTION_MAX;
	if (!udb_storage_io_range_valid(size, remaining))
		return UDB_STORAGE_LIMIT;
	udb_storage_file_job_limit(&engine->files[3], size + remaining);
	UdbStorageState staged = *base;
	staged.sequence = work->sequence;
	staged.next_object = work->next_object;
	result = udb_storage_stage_save(engine, &staged, &owner->head, &work->root, candidate);
	udb_storage_file_job_limit(&engine->files[3], 0);
	if (result != UDB_STORAGE_OK)
		return result;
	owner->ready |= 1U << block;
	/* Later jobs in the same group must not inherit this controller. All root
	 * bytes have been copied into candidate, which remains private until WAL. */
	return udb_storage_stage_work_close(engine);
}

static UdbStorageResult udb_storage_stage_jobs_dispatch(UdbStorageEngine *engine, const UdbStorageJob *job,
														const UdbStorageState *base, UdbStorageState *candidate)
{
	UdbStorageStageOwner *owner;
	UdbStorageStageSpec spec;
	UdbStorageRow row;
	UdbStorageRecord record = {0};
	UdbStorageResult result;
	unsigned char bytes[UDB_STORAGE_RECORD_MAX];
	size_t length;
	int block;
	if (!engine || !job || !base || !candidate || !job->owner)
		return UDB_STORAGE_ERROR;
	owner = udb_storage_stage_owner_find(engine, job->owner);
	if (job->kind == UDB_STORAGE_STAGE_ABORT)
	{
		if (job->length)
			return UDB_STORAGE_REJECTED;
		result = udb_storage_stage_drop(engine, base, job->owner, candidate);
		return result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_STAGED : result;
	}
	if (job->kind == UDB_STORAGE_STAGE_PROMOTE)
	{
		if (job->length || !owner || owner->failed || owner->ready != 63)
			return UDB_STORAGE_REJECTED;
		return udb_storage_stage_promote(engine, base, job->owner, candidate);
	}
	if (job->kind == UDB_STORAGE_STAGE_BEGIN || job->kind == UDB_STORAGE_STAGE_REUSE)
	{
		if (!engine->validator || !engine->stage_validator)
			return UDB_STORAGE_REJECTED;
		result = udb_storage_stage_spec_read(job, &spec);
		if (result != UDB_STORAGE_OK)
			return result;
		block = udb_storage_stage_block_index(spec.block);
		result = udb_storage_stage_owner_admit(engine, &spec, &owner);
		if (result != UDB_STORAGE_OK)
			return result;
		if (owner->admitted & (1U << block))
			return UDB_STORAGE_REJECTED;
		owner->specs[block] = spec;
		owner->admitted |= 1U << block;
		if (job->kind == UDB_STORAGE_STAGE_REUSE)
		{
			if (base->blocks[block].summary.count != spec.count ||
				memcmp(base->blocks[block].summary.digest, spec.digest, 32))
				return UDB_STORAGE_REJECTED;
			result = udb_storage_stage_save(engine, base, &owner->head, base->blocks + block, candidate);
			if (result == UDB_STORAGE_OK)
				owner->ready |= 1U << block;
			return result;
		}
		result = udb_storage_sort_create(&engine->directory, spec.block, spec.rows_limit, spec.physical_limit,
										 engine->validator, engine->validation_context, owner->sorts + block);
		return result == UDB_STORAGE_OK ? UDB_STORAGE_STAGED : result;
	}
	if (!owner || owner->failed)
		return UDB_STORAGE_REJECTED;
	if (job->kind == UDB_STORAGE_STAGE_PUT)
	{
		if (udb_storage_row_decode(job->payload, job->length, &row) != UDB_STORAGE_OK)
			return UDB_STORAGE_REJECTED;
		block = udb_storage_stage_block_index(row.block);
		if (block < 0 || !(owner->admitted & (1U << block)) || owner->sealed & (1U << block) || !owner->sorts[block])
			return UDB_STORAGE_REJECTED;
		uint64_t ordinal = udb_storage_sort_rows(owner->sorts[block]);
		if (ordinal >= owner->specs[block].count || ordinal > UINT32_MAX)
			return UDB_STORAGE_REJECTED;
		record.row = row;
		record.operation = UDB_STORAGE_PUT;
		record.stamp.ordinal = (uint32_t)ordinal;
		result = udb_storage_record_encode(&record, bytes, sizeof(bytes), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_sort_add(owner->sorts[block], bytes, length);
		return result == UDB_STORAGE_OK ? UDB_STORAGE_STAGED : result;
	}
	if (job->kind == UDB_STORAGE_STAGE_END)
	{
		if (job->length != 1 || (block = udb_storage_stage_block_index(job->payload[0])) < 0 ||
			!(owner->admitted & (1U << block)))
			return UDB_STORAGE_REJECTED;
		return udb_storage_stage_end_prepare(engine, job, base, candidate, owner, (unsigned int)block);
	}
	return UDB_STORAGE_REJECTED;
}

static UdbStorageResult udb_storage_stage_jobs_finish(UdbStorageEngine *engine, const UdbStorageJob *job)
{
	UdbStorageStageOwner *owner = udb_storage_stage_owner_find(engine, job->owner);
	UdbStorageResult result;
	if (engine->work && ((UdbStorageStageWork *)engine->work)->job == job->id)
	{
		result = udb_storage_stage_work_close(engine);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	if (!owner)
		return UDB_STORAGE_OK;
	if ((job->kind == UDB_STORAGE_STAGE_ABORT || job->kind == UDB_STORAGE_STAGE_PROMOTE) &&
		(job->result == UDB_STORAGE_OK || job->result == UDB_STORAGE_STAGED))
		return udb_storage_stage_owner_close(engine, owner);
	if (job->result < UDB_STORAGE_NOT_FOUND)
	{
		owner->failed = 1;
		for (unsigned int i = 0; i < 6; i++)
			if (owner->sorts[i])
			{
				result = udb_storage_sort_close(owner->sorts + i);
				if (result != UDB_STORAGE_OK)
					return result;
			}
	}
	else if (job->kind == UDB_STORAGE_STAGE_END && job->result == UDB_STORAGE_OK)
	{
		int block = udb_storage_stage_block_index(job->payload[0]);
		if (block >= 0 && owner->sorts[block])
			return udb_storage_sort_close(owner->sorts + block);
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_jobs_prepare(UdbStorageEngine *engine, const UdbStorageJob *job,
													   const UdbStorageState *base, UdbStorageState *candidate)
{
	UdbStorageResult result = udb_storage_stage_jobs_dispatch(engine, job, base, candidate);
	if (result < UDB_STORAGE_NOT_FOUND && engine && job)
	{
		UdbStorageStageOwner *owner = udb_storage_stage_owner_find(engine, job->owner);
		if (owner)
			owner->failed = 1;
	}
	return result;
}

/* End of udb_storage_stage_jobs.c.inc */
/* Inlined: udb_storage_stages.c.inc */
/* Durable pending-root catalog in S, distinct from S's logical block catalog.
 * Per-block ACK may follow a pending-root WAL commit, but no imported active
 * block is exposed until all six roots atomically promote. Runtime owner IDs
 * are discarded durably on recovery, never interpreted as live peer authority. */

static void udb_storage_stage_key(uint64_t owner, unsigned int block, unsigned int part, unsigned char key[64])
{
	memset(key, 0, 64);
	for (unsigned int i = 0; i < 8; i++)
		key[i] = (unsigned char)(owner >> (56 - 8 * i));
	key[8] = (unsigned char)block;
	key[9] = (unsigned char)part;
}

static int udb_storage_stage_head_valid(const UdbStorageStageHead *head)
{
	return head && head->owner && head->round && head->mask <= 63 && udb_storage_sid_valid(head->source_sid);
}

static UdbStorageResult udb_storage_stage_head_encode(const UdbStorageStageHead *head, unsigned char value[128])
{
	if (!value || !udb_storage_stage_head_valid(head) || !head->mask ||
		udb_storage_overlaps(value, 128, head, sizeof(*head)))
		return UDB_STORAGE_ERROR;
	memset(value, 0, 128);
	memcpy(value, "UDBSTG4", 7);
	udb_storage_put_u64(value + 8, head->owner);
	memcpy(value + 16, head->source_sid, 4);
	udb_storage_put_u32(value + 20, head->mask);
	udb_storage_put_u64(value + 24, head->source_epoch);
	udb_storage_put_u64(value + 32, head->source_sequence);
	udb_storage_put_u64(value + 40, head->round);
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_head_decode(const unsigned char value[128], UdbStorageStageHead *head)
{
	UdbStorageStageHead candidate;
	if (!value || !head)
		return UDB_STORAGE_ERROR;
	if (memcmp(value, "UDBSTG4\0", 8) || !udb_storage_zero(value + 48, 80))
		return UDB_STORAGE_CORRUPT;
	memset(&candidate, 0, sizeof(candidate));
	candidate.owner = udb_storage_get_u64(value + 8);
	memcpy(candidate.source_sid, value + 16, 4);
	candidate.mask = udb_storage_get_u32(value + 20);
	candidate.source_epoch = udb_storage_get_u64(value + 24);
	candidate.source_sequence = udb_storage_get_u64(value + 32);
	candidate.round = udb_storage_get_u64(value + 40);
	if (!udb_storage_stage_head_valid(&candidate) || !candidate.mask)
		return UDB_STORAGE_CORRUPT;
	*head = candidate;
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stage_head_get(UdbStorageEngine *engine, const UdbStorageState *state,
												   uint64_t owner, UdbStorageStageHead *head)
{
	unsigned char key[64], value[128];
	UdbStorageResult result;
	udb_storage_stage_key(owner, 0, 0, key);
	result = udb_storage_catalog_get(engine->cache, engine->files + 3, &state->stages, key, value);
	if (result == UDB_STORAGE_OK)
	{
		result = udb_storage_stage_head_decode(value, head);
		if (result == UDB_STORAGE_OK && head->owner != owner)
			return UDB_STORAGE_CORRUPT;
	}
	return result;
}

static UdbStorageResult udb_storage_stage_root_get(UdbStorageEngine *engine, const UdbStorageState *state,
												   uint64_t owner, unsigned int block, UdbStorageBlockRoot *root)
{
	unsigned char key[64], bytes[UDB_STORAGE_BLOCK_ROOT_SIZE];
	UdbStorageResult result;
	if (block >= 6)
		return UDB_STORAGE_ERROR;
	for (unsigned int part = 0; part < 3; part++)
	{
		udb_storage_stage_key(owner, block + 1, part, key);
		result = udb_storage_catalog_get(engine->cache, engine->files + 3, &state->stages, key, bytes + part * 128);
		if (result != UDB_STORAGE_OK)
			return result == UDB_STORAGE_NOT_FOUND ? UDB_STORAGE_CORRUPT : result;
	}
	result = udb_storage_block_root_decode(bytes, root);
	if (result != UDB_STORAGE_OK)
		return result;
	return root->block == udb_storage_blocks[block] && root->catalog.object == block + 1 &&
				   root->generation <= state->generation && root->watermark <= state->sequence
			   ? UDB_STORAGE_OK
			   : UDB_STORAGE_CORRUPT;
}

static UdbStorageResult udb_storage_stage_save(UdbStorageEngine *engine, const UdbStorageState *base,
											   const UdbStorageStageHead *head, const UdbStorageBlockRoot *root,
											   UdbStorageState *candidate)
{
	UdbStorageState state;
	UdbStorageStageHead stored;
	UdbStorageResult result;
	unsigned char key[64], bytes[UDB_STORAGE_BLOCK_ROOT_SIZE], value[128];
	unsigned int block;
	if (!engine || !candidate || !udb_storage_state_valid(base) || !udb_storage_stage_head_valid(head) || head->mask ||
		!udb_storage_block_root_valid(root) || root->generation > base->generation || root->watermark > base->sequence)
		return UDB_STORAGE_ERROR;
	for (block = 0; block < 6 && udb_storage_blocks[block] != root->block; block++)
		;
	if (block == 6 || root->catalog.object != block + 1)
		return UDB_STORAGE_ERROR;
	result = udb_storage_stage_head_get(engine, base, head->owner, &stored);
	if (result == UDB_STORAGE_NOT_FOUND)
		stored = *head;
	else if (result != UDB_STORAGE_OK)
		return result;
	else if (stored.round != head->round || stored.source_epoch != head->source_epoch ||
			 stored.source_sequence != head->source_sequence || memcmp(stored.source_sid, head->source_sid, 4))
		return UDB_STORAGE_REJECTED;
	if (stored.mask & (1U << block))
		return UDB_STORAGE_REJECTED;
	state = *base;
	result = udb_storage_block_root_encode(root, bytes);
	if (result != UDB_STORAGE_OK)
		return result;
	for (unsigned int part = 0; part < 3; part++)
	{
		UdbStorageCatalogRoot changed;
		udb_storage_stage_key(head->owner, block + 1, part, key);
		result = udb_storage_catalog_change(engine->cache, engine->files + 3, &state.stages, state.generation, key,
											bytes + 128 * part, &changed);
		if (result != UDB_STORAGE_OK)
			return result;
		state.stages = changed;
	}
	stored.mask |= 1U << block;
	result = udb_storage_stage_head_encode(&stored, value);
	if (result != UDB_STORAGE_OK)
		return result;
	udb_storage_stage_key(head->owner, 0, 0, key);
	result = udb_storage_catalog_change(engine->cache, engine->files + 3, &state.stages, state.generation, key, value,
										&state.stages);
	if (result == UDB_STORAGE_OK)
		*candidate = state;
	return result;
}

static UdbStorageResult udb_storage_stage_drop(UdbStorageEngine *engine, const UdbStorageState *base, uint64_t owner,
											   UdbStorageState *candidate)
{
	UdbStorageState state;
	UdbStorageStageHead head;
	UdbStorageResult result;
	unsigned char key[64];
	if (!engine || !candidate || !owner || !udb_storage_state_valid(base))
		return UDB_STORAGE_ERROR;
	result = udb_storage_stage_head_get(engine, base, owner, &head);
	if (result != UDB_STORAGE_OK)
		return result;
	state = *base;
	for (unsigned int block = 0; block < 6; block++)
	{
		if (!(head.mask & (1U << block)))
			continue;
		for (unsigned int part = 0; part < 3; part++)
		{
			UdbStorageCatalogRoot changed;
			udb_storage_stage_key(owner, block + 1, part, key);
			result = udb_storage_catalog_change(engine->cache, engine->files + 3, &state.stages, state.generation, key,
												NULL, &changed);
			if (result != UDB_STORAGE_OK)
				return result;
			state.stages = changed;
		}
	}
	udb_storage_stage_key(owner, 0, 0, key);
	result = udb_storage_catalog_change(engine->cache, engine->files + 3, &state.stages, state.generation, key, NULL,
										&state.stages);
	if (result == UDB_STORAGE_OK)
		*candidate = state;
	return result;
}

static UdbStorageResult udb_storage_stage_promote(UdbStorageEngine *engine, const UdbStorageState *base, uint64_t owner,
												  UdbStorageState *candidate)
{
	UdbStorageStageHead head;
	UdbStorageState state;
	UdbStorageResult result;
	if (!engine || !candidate || !udb_storage_state_valid(base))
		return UDB_STORAGE_ERROR;
	result = udb_storage_stage_head_get(engine, base, owner, &head);
	if (result != UDB_STORAGE_OK)
		return result;
	if (head.mask != 63)
		return UDB_STORAGE_REJECTED;
	result = udb_storage_stage_drop(engine, base, owner, &state);
	if (result != UDB_STORAGE_OK)
		return result;
	for (unsigned int block = 0; block < 6; block++)
	{
		result = udb_storage_stage_root_get(engine, base, owner, block, state.blocks + block);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	memcpy(state.source_sid, head.source_sid, 4);
	state.source_epoch = head.source_epoch;
	state.source_sequence = head.source_sequence;
	*candidate = state;
	return UDB_STORAGE_OK;
}

typedef struct UdbStorageStageValidation
{
	UdbStorageJournal *journal;
	UdbStorageRecordValidator validator;
	void *context;
	UdbStorageStageHead head;
	unsigned int mask;
	unsigned int block;
	unsigned int part;
	unsigned char bytes[UDB_STORAGE_BLOCK_ROOT_SIZE];
} UdbStorageStageValidation;

static UdbStorageResult udb_storage_stages_record_validate(const unsigned char *key, const unsigned char *value,
														   void *opaque)
{
	UdbStorageStageValidation *validation = opaque;
	UdbStorageJournal *journal = validation->journal;
	uint64_t owner = 0;
	unsigned int block = key[8], part = key[9];
	unsigned char canonical[64];
	UdbStorageResult result;
	for (unsigned int i = 0; i < 8; i++)
		owner = (owner << 8) | key[i];
	udb_storage_stage_key(owner, block, part, canonical);
	if (!owner || memcmp(canonical, key, 64) || block > 6 || part > 2)
		return UDB_STORAGE_CORRUPT;
	if (!block)
	{
		if (part || validation->part || (validation->head.owner && validation->mask != validation->head.mask))
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_stage_head_decode(value, &validation->head);
		if (result != UDB_STORAGE_OK)
			return result;
		if (validation->head.owner != owner)
			return UDB_STORAGE_CORRUPT;
		validation->mask = 0;
		return UDB_STORAGE_OK;
	}
	if (owner != validation->head.owner || !(validation->head.mask & (1U << (block - 1))) ||
		(validation->mask & (1U << (block - 1))) || part != validation->part || (part && validation->block != block))
		return UDB_STORAGE_CORRUPT;
	validation->block = block;
	memcpy(validation->bytes + 128 * part, value, 128);
	if (++validation->part == 3)
	{
		UdbStorageBlockRoot root;
		result = udb_storage_block_root_decode(validation->bytes, &root);
		if (result != UDB_STORAGE_OK)
			return result;
		if (root.block != udb_storage_blocks[block - 1] || root.catalog.object != block ||
			root.generation > journal->state.generation || root.watermark > journal->state.sequence)
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_block_validate(journal->cache, journal->data[block - 1], &root, validation->validator,
											validation->context);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_block_object_bound(journal->cache, journal->data[block - 1], &root,
													journal->state.next_object);
		if (result != UDB_STORAGE_OK)
			return result;
		validation->mask |= 1U << (block - 1);
		validation->part = 0;
	}
	return UDB_STORAGE_OK;
}

static UdbStorageResult udb_storage_stages_validate(UdbStorageJournal *journal, UdbStorageRecordValidator validator,
													void *context)
{
	UdbStorageStageValidation *validation;
	UdbStorageResult result;
	if (!journal || !udb_storage_state_valid(&journal->state))
		return UDB_STORAGE_ERROR;
	validation = udb_storage_alloc(journal->cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*validation));
	if (!validation)
		return UDB_STORAGE_LIMIT;
	validation->journal = journal;
	validation->validator = validator;
	validation->context = context;
	result = udb_storage_catalog_validate(journal->data[3], &journal->state.stages, udb_storage_stages_record_validate,
										  validation);
	if (result == UDB_STORAGE_OK && (validation->part || validation->mask != validation->head.mask))
		result = UDB_STORAGE_CORRUPT;
	udb_storage_free(validation);
	return result;
}

/* End of udb_storage_stages.c.inc */
/* Inlined: udb_storage_state.c.inc */
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

/* End of udb_storage_state.c.inc */
/* Inlined: udb_storage_validate.c.inc */
/* Complete, owner-exclusive validation of immutable logical block roots.
 * Physical scans and merge pins bypass cache admission. Depth-bounded frames
 * replace recursion and neither catalog nor keys are materialized in memory.
 * A root becomes eligible for lookup only after this operation succeeds. */

typedef struct UdbStorageValidationFrame
{
	UdbStorageLogicalPosition position;
	UdbStorageNode node;
	UdbStorageLogicalSummary left;
	unsigned int phase;
} UdbStorageValidationFrame;

typedef struct UdbStorageValidation
{
	UdbStorageCache *cache;
	UdbStorageFile *file;
	const UdbStorageBlockRoot *root;
	UdbStorageRecordValidator validator;
	void *context;
	UdbStorageRun prefixes[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageRun points[UDB_STORAGE_MAX_RUNS + 1];
	UdbStorageValidationFrame frames[257];
	unsigned char bytes[UDB_STORAGE_ROW_MAX];
	uint64_t catalog_nodes;
	uint64_t visited_nodes;
} UdbStorageValidation;

static int udb_storage_summary_equal(const UdbStorageLogicalSummary *a, const UdbStorageLogicalSummary *b)
{
	return a->count == b->count && a->weight == b->weight && !memcmp(a->digest, b->digest, 32);
}

static int udb_storage_block_root_valid(const UdbStorageBlockRoot *root)
{
	if (!root || !udb_storage_block_valid(root->block) || !root->generation ||
		!udb_storage_catalog_root_valid(&root->catalog) || root->catalog.generation > root->generation ||
		!udb_storage_logical_summary_valid(&root->summary) || root->prefix_count > UDB_STORAGE_MAX_RUNS + 1)
		return 0;
	for (unsigned int i = 0; i < UDB_STORAGE_MAX_RUNS + 1; i++)
	{
		if (i >= root->prefix_count)
		{
			if (root->prefixes[i])
				return 0;
			continue;
		}
		if (!root->prefixes[i])
			return 0;
		for (unsigned int j = 0; j < i; j++)
			if (root->prefixes[j] == root->prefixes[i])
				return 0;
	}
	return 1;
}

static UdbStorageResult udb_storage_validation_catalog(const unsigned char *key, const unsigned char *value,
													   void *opaque)
{
	UdbStorageValidation *validation = opaque;
	unsigned char canonical[64];
	if (key[0] != validation->root->block)
		return UDB_STORAGE_CORRUPT;
	if (key[1] == 1)
	{
		UdbStorageLogicalPosition position;
		UdbStorageNode node;
		memset(&position, 0, sizeof(position));
		position.block = key[0];
		position.partition = (uint16_t)((key[2] << 8) | key[3]);
		position.depth = (uint16_t)((key[4] << 8) | key[5]);
		memcpy(position.prefix, key + 6, 32);
		if (!udb_storage_logical_position_valid(&position))
			return UDB_STORAGE_CORRUPT;
		udb_storage_node_key(&position, canonical);
		if (memcmp(canonical, key, 64) || udb_storage_node_decode(&position, value, &node) != UDB_STORAGE_OK ||
			validation->catalog_nodes == UINT64_MAX)
			return UDB_STORAGE_CORRUPT;
		validation->catalog_nodes++;
		return UDB_STORAGE_OK;
	}
	if (key[1] == 2 || key[1] == 3)
	{
		UdbStorageRun run;
		uint64_t id = 0;
		UdbStorageResult result;
		memset(&run, 0, sizeof(run));
		for (unsigned int i = 0; i < 8; i++)
			id = (id << 8) | key[2 + i];
		if (!id)
			return UDB_STORAGE_CORRUPT;
		run.block = key[0];
		run.stream = key[1] == 2 ? UDB_STORAGE_POINTS : UDB_STORAGE_PREFIXES;
		run.file = validation->file;
		udb_storage_run_manifest_key(run.block, run.stream, id, canonical);
		if (memcmp(canonical, key, 64))
			return UDB_STORAGE_CORRUPT;
		result = udb_storage_catalog_root_decode(value, 128, &run.directory);
		if (result != UDB_STORAGE_OK)
			return result;
		if (run.directory.object != id || run.directory.generation > validation->root->catalog.generation)
			return UDB_STORAGE_CORRUPT;
		return udb_storage_run_validate(validation->file, &run, validation->root->watermark, validation->validator,
										validation->context);
	}
	return UDB_STORAGE_CORRUPT;
}

static UdbStorageResult udb_storage_validation_placement(const UdbStorageRecord *record, void *opaque)
{
	const UdbStorageLogicalPosition *position = opaque;
	unsigned char hash[32];
	unsigned int partition;
	UdbStorageResult result = udb_storage_key_hash(&record->row, hash, &partition);
	if (result != UDB_STORAGE_OK)
		return result == UDB_STORAGE_ERROR ? UDB_STORAGE_CORRUPT : result;
	return udb_storage_logical_contains(position, hash, partition) ? UDB_STORAGE_OK : UDB_STORAGE_CORRUPT;
}

static UdbStorageResult udb_storage_validation_leaf(UdbStorageValidation *validation,
													const UdbStorageValidationFrame *frame,
													UdbStorageLogicalSummary *summary)
{
	UdbStorageView *view = NULL;
	UdbStorageLeafHasher *hasher = NULL;
	UdbStorageResult result;
	UdbStorageRow row;
	size_t length;
	for (unsigned int i = 0; i < frame->node.run_count; i++)
	{
		result =
			udb_storage_run_load(validation->cache, validation->file, &validation->root->catalog, frame->position.block,
								 UDB_STORAGE_POINTS, frame->node.runs[i], validation->points + i);
		if (result != UDB_STORAGE_OK)
			return result;
		/* Placement applies also to hidden exact deletes and superseded PUTs. */
		result = udb_storage_run_validate(validation->file, validation->points + i, validation->root->watermark,
										  udb_storage_validation_placement, (void *)&frame->position);
		if (result != UDB_STORAGE_OK)
			return result;
	}
	result = udb_storage_leaf_hasher_create(validation->cache->budget, &frame->position, &hasher);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_view_create_indexed(validation->cache, validation->points, frame->node.run_count,
											 validation->prefixes, validation->root->prefix_count, NULL,
											 validation->root->watermark, &view);
	if (result != UDB_STORAGE_OK)
		goto done;
	while ((result = udb_storage_view_next(view, &row)) == UDB_STORAGE_OK)
	{
		result = udb_storage_row_encode(&row, validation->bytes, sizeof(validation->bytes), &length);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_leaf_hasher_add(hasher, validation->bytes, length);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = udb_storage_leaf_hasher_finish(hasher, summary);
done:
	if (view)
	{
		UdbStorageResult closed = udb_storage_view_close(&view);
		if (closed != UDB_STORAGE_OK && result >= UDB_STORAGE_NOT_FOUND)
			result = closed;
	}
	udb_storage_leaf_hasher_free(&hasher);
	return result;
}

static UdbStorageResult udb_storage_validation_partition(unsigned int partition, UdbStorageLogicalSummary *summary,
														 void *opaque)
{
	UdbStorageValidation *validation = opaque;
	unsigned int depth = 0;
	UdbStorageLogicalSummary completed;
	UdbStorageResult result;
	UdbStorageValidationFrame *frame = validation->frames;
	memset(frame, 0, sizeof(*frame));
	frame->position.block = validation->root->block;
	frame->position.partition = (uint16_t)partition;
	for (;;)
	{
		frame = validation->frames + depth;
		if (!frame->phase)
		{
			result = udb_storage_node_get(validation->cache, validation->file, &validation->root->catalog,
										  &frame->position, &frame->node);
			if (result == UDB_STORAGE_NOT_FOUND)
				result = udb_storage_logical_empty(&frame->position, &completed);
			else if (result == UDB_STORAGE_OK)
			{
				if (validation->visited_nodes == UINT64_MAX)
					return UDB_STORAGE_CORRUPT;
				validation->visited_nodes++;
				if (frame->node.kind == UDB_STORAGE_BRANCH)
				{
					UdbStorageValidationFrame *child;
					if (depth == 256)
						return UDB_STORAGE_CORRUPT;
					frame->phase = 1;
					child = validation->frames + ++depth;
					memset(child, 0, sizeof(*child));
					result = udb_storage_logical_child(&frame->position, 0, &child->position);
					if (result != UDB_STORAGE_OK)
						return result;
					continue;
				}
				result = udb_storage_validation_leaf(validation, frame, &completed);
				if (result == UDB_STORAGE_OK && !udb_storage_summary_equal(&completed, &frame->node.summary))
					return UDB_STORAGE_CORRUPT;
			}
			if (result != UDB_STORAGE_OK)
				return result;
		}
		else
		{
			result = udb_storage_logical_branch(&frame->position, &frame->left, &completed, &completed);
			if (result != UDB_STORAGE_OK)
				return result == UDB_STORAGE_ERROR ? UDB_STORAGE_CORRUPT : result;
			if (!udb_storage_summary_equal(&completed, &frame->node.summary))
				return UDB_STORAGE_CORRUPT;
		}
		if (!depth)
		{
			*summary = completed;
			return UDB_STORAGE_OK;
		}
		frame = validation->frames + --depth;
		if (frame->phase == 1)
		{
			UdbStorageValidationFrame *child = validation->frames + depth + 1;
			frame->left = completed;
			frame->phase = 2;
			memset(child, 0, sizeof(*child));
			result = udb_storage_logical_child(&frame->position, 1, &child->position);
			if (result != UDB_STORAGE_OK)
				return result;
			depth++;
		}
	}
}

static UdbStorageResult udb_storage_block_validate(UdbStorageCache *cache, UdbStorageFile *file,
												   const UdbStorageBlockRoot *root, UdbStorageRecordValidator validator,
												   void *context)
{
	UdbStorageValidation *validation;
	UdbStorageLogicalSummary summary;
	UdbStorageResult result;
	if (!cache || !file || file->cache != cache || !udb_storage_block_root_valid(root))
		return UDB_STORAGE_ERROR;
	/* Caller owns/pins immutable input; other readers may use the cache. */
	validation = udb_storage_alloc(cache->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*validation));
	if (!validation)
		return UDB_STORAGE_LIMIT;
	validation->cache = cache;
	validation->file = file;
	validation->root = root;
	validation->validator = validator;
	validation->context = context;
	udb_storage_mutex_lock(&cache->mutex);
	if (cache->bypass_admission)
	{
		udb_storage_mutex_unlock(&cache->mutex);
		udb_storage_free(validation);
		return UDB_STORAGE_BUSY;
	}
#ifdef _WIN32
	cache->validation_thread = GetCurrentThreadId();
#else
	cache->validation_thread = pthread_self();
#endif
	cache->bypass_admission = 1;
	udb_storage_mutex_unlock(&cache->mutex);
	result = udb_storage_catalog_validate(file, &root->catalog, udb_storage_validation_catalog, validation);
	if (result != UDB_STORAGE_OK)
		goto done;
	for (unsigned int i = 0; i < root->prefix_count; i++)
	{
		result = udb_storage_run_load(cache, file, &root->catalog, root->block, UDB_STORAGE_PREFIXES, root->prefixes[i],
									  validation->prefixes + i);
		if (result != UDB_STORAGE_OK)
			goto done;
	}
	result = udb_storage_logical_block(root->block, udb_storage_validation_partition, validation, &summary);
	if (result == UDB_STORAGE_OK && (!udb_storage_summary_equal(&summary, &root->summary) ||
									 validation->visited_nodes != validation->catalog_nodes))
		result = UDB_STORAGE_CORRUPT;
done:
	udb_storage_mutex_lock(&cache->mutex);
	cache->bypass_admission = 0;
	udb_storage_mutex_unlock(&cache->mutex);
	udb_storage_free(validation);
	return result;
}

static UdbStorageResult udb_storage_block_object_bound(UdbStorageCache *cache, UdbStorageFile *file,
													   const UdbStorageBlockRoot *root, uint64_t next_object)
{
	UdbStorageCatalogIterator *iterator = NULL;
	UdbStorageResult result, closed;
	unsigned char lower[64] = {0}, upper[64] = {0}, key[64], value[128];
	if (next_object < 8)
		return UDB_STORAGE_CORRUPT;
	lower[0] = upper[0] = root->block;
	lower[1] = 2;
	upper[1] = 4;
	result = udb_storage_catalog_iterator_create(cache, file, &root->catalog, lower, upper, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	while ((result = udb_storage_catalog_iterator_next(iterator, key, value)) == UDB_STORAGE_OK)
	{
		uint64_t id = 0;
		for (unsigned int i = 2; i < 10; i++)
			id = (id << 8) | key[i];
		if (id < 8 || id >= next_object)
		{
			result = UDB_STORAGE_CORRUPT;
			break;
		}
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = UDB_STORAGE_OK;
	closed = udb_storage_catalog_iterator_close(&iterator);
	return result == UDB_STORAGE_OK ? closed : result;
}

/* End of udb_storage_validate.c.inc */

/* Canonical wire path encoding and secret tuple classification */
/* Inlined: udb_store.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Canonical Wire Paths and Secret Classification
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

/* Canonical path components */
static int udb_path_encode_component(const char *raw, char *buf, size_t bufsz)
{
	static const char hex[] = "0123456789ABCDEF";
	const unsigned char *p;
	size_t out_len = 0;

	if (!raw || !buf || bufsz == 0)
		return 0;

	for (p = (const unsigned char *)raw; *p; p++)
	{
		if (*p == ':' || *p == '%' || *p <= 32 || *p >= 127)
		{
			if (out_len + 3 >= bufsz)
				return 0;
			buf[out_len++] = '%';
			buf[out_len++] = hex[(*p >> 4) & 0x0F];
			buf[out_len++] = hex[*p & 0x0F];
		}
		else
		{
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = (char)*p;
		}
	}
	buf[out_len] = '\0';
	return 1;
}

static int udb_path_decode_component(const char *encoded, char *buf, size_t bufsz)
{
	const char *p;
	size_t out_len = 0;

	if (!encoded || !buf || bufsz == 0)
		return 0;

	for (p = encoded; *p; p++)
	{
		if (*p == '%')
		{
			int high, low;
			if (!p[1] || !p[2])
				return 0;
			if (p[1] >= '0' && p[1] <= '9')
				high = p[1] - '0';
			else if (p[1] >= 'a' && p[1] <= 'f')
				high = p[1] - 'a' + 10;
			else if (p[1] >= 'A' && p[1] <= 'F')
				high = p[1] - 'A' + 10;
			else
				return 0;

			if (p[2] >= '0' && p[2] <= '9')
				low = p[2] - '0';
			else if (p[2] >= 'a' && p[2] <= 'f')
				low = p[2] - 'a' + 10;
			else if (p[2] >= 'A' && p[2] <= 'F')
				low = p[2] - 'A' + 10;
			else
				return 0;

			unsigned char val = (unsigned char)((high << 4) | low);
			if (val == 0) /* Embedded null byte is rejected */
				return 0;
			/* Canonicality check: reject non-canonical percent-encoding when
			 * udb_path_encode_component() would have emitted the byte literally */
			if (val != ':' && val != '%' && val > 32 && val < 127)
				return 0;
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = (char)val;
			p += 2;
		}
		else
		{
			/* A raw colon could be mistaken for :: path syntax.  The encoder
			 * always writes it as %3A, so reject the non-canonical spelling. */
			if (*p == ':')
				return 0;
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = *p;
		}
	}
	buf[out_len] = '\0';
	return 1;
}

static int udb_path_foreach(const char *path, unsigned int max_components, UdbPathComponentFunc fn, void *data)
{
	char encoded[UDB_COMPONENT_ENCODED_MAX + 1];
	char decoded[UDB_COMPONENT_RAW_MAX + 1];
	const char *component;
	const char *separator;
	unsigned int index = 0;

	if (!path || !*path)
		return 0;
	component = path;
	for (;;)
	{
		size_t length;

		separator = strstr(component, "::");
		if (separator == component || (separator && !separator[2]))
			return 0;
		length = separator ? (size_t)(separator - component) : strlen(component);
		if (length > UDB_COMPONENT_ENCODED_MAX)
			return 0;
		memcpy(encoded, component, length);
		encoded[length] = '\0';
		if (!udb_path_decode_component(encoded, decoded, sizeof(decoded)))
			return 0;
		if (index >= max_components || !fn(decoded, index, data))
			return 0;
		index++;
		if (!separator)
			break;
		component = separator + 2;
	}
	return (int)index;
}

static int udb_secret_key_is_reserved(const char *key)
{
	return key && (!strcasecmp(key, NKEY_PASS) || !strcasecmp(key, SKEY_CRYPT_KEY) || !strcasecmp(key, CKEY_MODES));
}

static int udb_secret_key_matches(char block_letter, unsigned int depth, const char *key)
{
	if (!key)
		return 0;
	if (!strcasecmp(key, NKEY_PASS))
		return block_letter == UDB_BLOCK_NICKS && depth == 2;
	if (!strcasecmp(key, SKEY_CRYPT_KEY))
		return block_letter == UDB_BLOCK_SETTINGS && depth == 1;
	if (!strcasecmp(key, CKEY_MODES))
		return block_letter == UDB_BLOCK_CHANNELS && depth == 2;
	return 0;
}

typedef struct UdbSecretPathState
{
	char last[UDB_COMPONENT_RAW_MAX + 1];
	unsigned int depth;
} UdbSecretPathState;

static int udb_path_secret_component(const char *decoded, unsigned int index, void *data)
{
	UdbSecretPathState *state = data;

	strlcpy(state->last, decoded, sizeof(state->last));
	state->depth = index + 1;
	return 1;
}

static int udb_path_value_is_secret(const char *path)
{
	UdbSecretPathState state;

	if (!path || path[0] == '\0' || path[1] != ':' || path[2] != ':')
		return 0;
	memset(&state, 0, sizeof(state));
	if (!udb_path_foreach(path + 3, UDB_PATH_MAX_COMPONENTS, udb_path_secret_component, &state))
		return 0;
	return udb_secret_key_matches(path[0], state.depth, state.last);
}

/* End of udb_store.c.inc */

/* Configuration: daemon block parsing and UDB settings state */
/* Inlined: udb_config.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Configuration Parsing, Block S Settings & Link Options
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static UdbConfig *udb_cfg = NULL;
static int udb_config_seen_propagator = 0;
static int udb_config_seen_cache;
static int udb_config_seen_flood;

static int udb_flood_valid(const char *value, int *attempts, int *period);

typedef enum UdbConfigKind
{
	UDB_CFG_SERVER_NAME,
	UDB_CFG_FLOOD,
	UDB_CFG_UINT,
	UDB_CFG_INT,
	UDB_CFG_SIZE
} UdbConfigKind;

typedef struct UdbConfigDirective
{
	const char *name;
	UdbConfigKind kind;
	const char *unit;
	size_t offset;
	unsigned int min;
	unsigned int max;
	size_t size_min;
	size_t size_max;
	size_t fallback;
} UdbConfigDirective;

static const UdbConfigDirective udb_config_directives[] = {
	{"propagator", UDB_CFG_SERVER_NAME, "", 0, 0, 0, 0, 0, 0},
	{"password-flood", UDB_CFG_FLOOD, "", 0, 0, 0, 0, 0, 0},
	{"cache-size", UDB_CFG_SIZE, "bytes", offsetof(UdbConfig, cache_size), 0, 0, (size_t)16 * 1024 * 1024, SIZE_MAX / 2,
	 (size_t)128 * 1024 * 1024},
	{"max-staged-records", UDB_CFG_UINT, "records", offsetof(UdbConfig, max_staged_records), UDB_MIN_MAX_STAGED_RECORDS,
	 UDB_MAX_MAX_STAGED_RECORDS, 0, 0, UDB_DEFAULT_MAX_STAGED_RECORDS},
	{"max-staged-bytes", UDB_CFG_SIZE, "bytes", offsetof(UdbConfig, max_staged_bytes), 0, 0, UDB_MIN_MAX_STAGED_BYTES,
	 (size_t)UDB_MAX_MAX_STAGED_BYTES, UDB_DEFAULT_MAX_STAGED_BYTES},
	{"sync-inactivity-timeout", UDB_CFG_INT, "seconds", offsetof(UdbConfig, sync_inactivity_timeout), 1, 86400, 0, 0,
	 UDB_SYNC_INACTIVITY_TIMEOUT},
	{"sync-absolute-timeout", UDB_CFG_INT, "seconds", offsetof(UdbConfig, sync_absolute_timeout), 1, 86400, 0, 0,
	 UDB_SYNC_ABSOLUTE_TIMEOUT},
	{"stale-timeout", UDB_CFG_INT, "seconds", offsetof(UdbConfig, stale_timeout), 1, 604800, 0, 0, 300},
	{"anti-entropy-interval", UDB_CFG_INT, "seconds", offsetof(UdbConfig, anti_entropy_interval),
	 UDB_MIN_ANTI_ENTROPY_INTERVAL, UDB_MAX_ANTI_ENTROPY_INTERVAL, 0, 0, UDB_DEFAULT_ANTI_ENTROPY_INTERVAL},
};

static const UdbConfigDirective *udb_config_directive_find(const char *name)
{
	for (size_t i = 0; i < sizeof(udb_config_directives) / sizeof(udb_config_directives[0]); i++)
		if (!strcmp(udb_config_directives[i].name, name))
			return &udb_config_directives[i];
	return NULL;
}

static int udb_config_apply_entry(const UdbConfigDirective *directive, ConfigEntry *cep, int apply)
{
	switch (directive->kind)
	{
	case UDB_CFG_SERVER_NAME:
		if (!cep->value || !udb_server_name_valid(cep->value))
			return 0;
		if (apply)
		{
			udb_config_seen_propagator = 1;
			safe_strdup(udb_cfg->propagator, cep->value);
		}
		return 1;
	case UDB_CFG_FLOOD:
		if (!cep->value || !udb_flood_valid(cep->value, apply ? &udb_cfg->flood_attempts : NULL,
											apply ? &udb_cfg->flood_period : NULL))
			return 0;
		return 1;
	case UDB_CFG_UINT:
	{
		unsigned int value;

		if (!cep->value || !udb_parse_uint_strict(cep->value, &value, directive->min, directive->max))
			return 0;
		if (apply)
			*(unsigned int *)((char *)udb_cfg + directive->offset) = value;
		return 1;
	}
	case UDB_CFG_INT:
	{
		unsigned int value;

		if (!cep->value || !udb_parse_uint_strict(cep->value, &value, directive->min, directive->max))
			return 0;
		if (apply)
			*(int *)((char *)udb_cfg + directive->offset) = (int)value;
		return 1;
	}
	case UDB_CFG_SIZE:
	{
		size_t value;

		if (!cep->value || !udb_parse_size_strict(cep->value, &value, directive->size_min, directive->size_max))
			return 0;
		if (apply)
			*(size_t *)((char *)udb_cfg + directive->offset) = value;
		return 1;
	}
	}
	return 0;
}

static void udb_config_error_entry(const UdbConfigDirective *directive, ConfigEntry *cep)
{
	switch (directive->kind)
	{
	case UDB_CFG_SERVER_NAME:
		config_error("%s:%i: udb::%s requires a valid server name", cep->file->filename, cep->line_number,
					 directive->name);
		break;
	case UDB_CFG_FLOOD:
		config_error("%s:%i: udb::%s requires format attempts:seconds with positive integers (e.g. 5:30)",
					 cep->file->filename, cep->line_number, directive->name);
		break;
	case UDB_CFG_UINT:
	case UDB_CFG_INT:
		if (directive->fallback)
			config_error("%s:%i: udb::%s must be between %u and %u %s (default %u)", cep->file->filename,
						 cep->line_number, directive->name, directive->min, directive->max, directive->unit,
						 (unsigned int)directive->fallback);
		else
			config_error("%s:%i: udb::%s must be between %u and %u %s", cep->file->filename, cep->line_number,
						 directive->name, directive->min, directive->max, directive->unit);
		break;
	case UDB_CFG_SIZE:
		config_error("%s:%i: udb::%s must be between %llu and %llu %s", cep->file->filename, cep->line_number,
					 directive->name, (unsigned long long)directive->size_min, (unsigned long long)directive->size_max,
					 directive->unit);
		break;
	}
}

static void udb_config_defaults(void)
{
	if (!udb_cfg)
		return;
	for (size_t i = 0; i < sizeof(udb_config_directives) / sizeof(udb_config_directives[0]); i++)
	{
		const UdbConfigDirective *directive = &udb_config_directives[i];

		if (!directive->fallback || directive->kind == UDB_CFG_SERVER_NAME || directive->kind == UDB_CFG_FLOOD)
			continue;
		if (directive->kind == UDB_CFG_UINT)
		{
			unsigned int *slot = (unsigned int *)((char *)udb_cfg + directive->offset);
			if (*slot == 0)
				*slot = directive->fallback;
		}
		else if (directive->kind == UDB_CFG_INT)
		{
			int *slot = (int *)((char *)udb_cfg + directive->offset);
			if (*slot == 0)
				*slot = (int)directive->fallback;
		}
		else if (directive->kind == UDB_CFG_SIZE)
		{
			size_t *slot = (size_t *)((char *)udb_cfg + directive->offset);
			if (*slot == 0)
				*slot = (size_t)directive->fallback;
		}
	}
	if (udb_cfg->flood_attempts == 0)
		udb_cfg->flood_attempts = 5;
	if (udb_cfg->flood_period == 0)
		udb_cfg->flood_period = 60;
	if (udb_config_seen_flood || !udb_cfg->config_flood_attempts || !udb_cfg->config_flood_period)
	{
		udb_cfg->config_flood_attempts = udb_cfg->flood_attempts;
		udb_cfg->config_flood_period = udb_cfg->flood_period;
	}
}

/* UnrealIRCd config-hook contract: 0 leaves unrelated blocks to other modules;
 * 1 accepts udb, and -1 rejects it with the validation error count in errs. */
static int udb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	ConfigEntry *cep;

	if (type != CONFIG_MAIN)
		return 0;
	if (!ce || !ce->name || strcmp(ce->name, "udb"))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		const UdbConfigDirective *directive = udb_config_directive_find(cep->name);

		if (!directive)
		{
			config_error("%s:%i: unknown directive udb::%s", cep->file->filename, cep->line_number, cep->name);
			errors++;
			continue;
		}
		if (!udb_config_apply_entry(directive, cep, 0))
		{
			udb_config_error_entry(directive, cep);
			errors++;
		}
	}

	*errs = errors;
	return errors ? -1 : 1;
}

/* CONFIGRUN only visits directives that still exist. Track the new generation
 * without mutating the active value until a successful postconf, so a failed
 * rehash cannot erase the last known-good propagator override. */
static int udb_config_rehash(void)
{
	udb_config_seen_propagator = 0;
	udb_config_seen_cache = 0;
	udb_config_seen_flood = 0;
	return 0;
}

static int udb_config_postconf(void)
{
	if (udb_cfg && !udb_config_seen_cache)
		udb_cfg->cache_size = UDB_STORAGE_CACHE_DEFAULT;
	if (udb_cfg && !udb_config_seen_flood)
	{
		udb_cfg->config_flood_attempts = 5;
		udb_cfg->config_flood_period = 60;
	}
	if (udb_cfg && !udb_config_seen_propagator)
		safe_free(udb_cfg->propagator);
	if (udb_ctx && udb_ctx->storage)
	{
		UdbStorageEngine *engine = udb_ctx->storage;
		UdbStorageResult resized = udb_storage_cache_resize(engine->cache, udb_cfg->cache_size);
		if (resized != UDB_STORAGE_OK)
			udb_log(ULOG_WARNING, "UDB_CACHE_RESIZE_DEFERRED", NULL,
					"Cannot apply local cache-size while pages are pinned; retaining the current cache budget until a "
					"later rehash");
		UdbStorageRead *read = NULL;
		UdbStorageResult result = udb_storage_read_open(engine, &read);
		udb_policy_notify_deferred = 1;
		if (result == UDB_STORAGE_OK)
			result = udb_native_settings_apply(udb_ctx, read);
		if (read && udb_storage_read_close(&read) != UDB_STORAGE_OK)
			result = UDB_STORAGE_ERROR;
		udb_policy_notify_deferred = 0;
		udb_propagator_policy_changed(udb_ctx);
		if (result != UDB_STORAGE_OK)
		{
			udb_ready = 0;
			udb_sync_status = UDB_SYNC_DEGRADED;
		}
	}
	/* HOOKTYPE_REHASH_COMPLETE: the new configuration is active, so rebuild
	 * the local operclass inventory and publish any changes. */
	udb_ocl_local_rebuild();
	return 0;
}

static int udb_config_run(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if (type != CONFIG_MAIN)
		return 0;
	if (!ce || !ce->name || strcmp(ce->name, "udb"))
		return 0;

	if (!udb_cfg)
		udb_cfg = safe_alloc(sizeof(UdbConfig));

	for (cep = ce->items; cep; cep = cep->next)
	{
		const UdbConfigDirective *directive = udb_config_directive_find(cep->name);

		if (directive)
		{
			if (!strcmp(directive->name, "cache-size"))
				udb_config_seen_cache = 1;
			if (!strcmp(directive->name, "password-flood"))
				udb_config_seen_flood = 1;
			(void)udb_config_apply_entry(directive, cep, 1);
		}
	}

	/* Persistent files always live in UnrealIRCd PERMDATADIR. */
	udb_config_defaults();

	return 1;
}

static void udb_config_free(UdbContext *ctx)
{
	if (ctx)
	{
		udb_storage_free(ctx->quit_clones);
		ctx->quit_clones = NULL;
		udb_storage_free(ctx->encryption_key);
		ctx->encryption_key = NULL;
		udb_storage_free(ctx->suffix);
		ctx->suffix = NULL;
		udb_storage_free(ctx->nickserv_mask);
		ctx->nickserv_mask = NULL;
		udb_storage_free(ctx->chanserv_mask);
		ctx->chanserv_mask = NULL;
		udb_storage_free(ctx->ipserv_mask);
		ctx->ipserv_mask = NULL;
		udb_storage_free(ctx->propagator_setting);
		ctx->propagator_setting = NULL;
	}
	if (udb_cfg)
	{
		safe_free(udb_cfg->propagator);
		safe_free(udb_cfg);
		udb_cfg = NULL;
	}
}

static int udb_setting_string_valid(const char *value)
{
	return value && *value && strlen(value) <= UDB_RECORD_VALUE_MAX && !strpbrk(value, "\r\n");
}

static int udb_suffix_valid(const char *value)
{
	const unsigned char *p;
	const char *label;

	/* The derived label is 32 hexadecimal characters. */
	if (!value || !*value || strlen(value) > HOSTLEN - 32 || value[0] != '.')
		return 0;
	label = value + 1;
	p = (const unsigned char *)label;
	for (; *p; p++)
	{
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' ||
			  *p == '-'))
			return 0;
		if (*p == '.')
		{
			if (p == (const unsigned char *)label || p[-1] == '-')
				return 0;
			label = (const char *)p + 1;
		}
		else if (*p == '-' && p == (const unsigned char *)label)
		{
			return 0;
		}
	}
	return value[1] && *label && value[strlen(value) - 1] != '-' && value[strlen(value) - 1] != '.';
}

static int udb_encryption_key_valid(const char *value)
{
	size_t i;

	/* A fixed-size hexadecimal key keeps the setting portable and unambiguous. */
	if (!value || strlen(value) != 64)
		return 0;
	for (i = 0; value[i]; i++)
		if (!isxdigit((unsigned char)value[i]))
			return 0;
	return 1;
}

static int udb_service_mask_valid(const char *value)
{
	const char *bang;
	const char *at;

	if (!udb_setting_string_valid(value) || strpbrk(value, " \t"))
		return 0;
	bang = strchr(value, '!');
	at = bang ? strchr(bang + 1, '@') : NULL;
	return bang && bang != value && at && at != bang + 1 && at[1];
}

static int udb_flood_valid(const char *value, int *attempts, int *period)
{
	const char *colon;
	unsigned int parsed_attempts, parsed_period;

	if (!value || !*value)
		return 0;
	colon = strchr(value, ':');
	if (!colon || colon == value || !colon[1])
		return 0;
	char attempts_buf[32];
	size_t attempts_len = colon - value;
	if (attempts_len >= sizeof(attempts_buf))
		return 0;
	memcpy(attempts_buf, value, attempts_len);
	attempts_buf[attempts_len] = '\0';
	if (!udb_parse_uint_strict(attempts_buf, &parsed_attempts, 1, INT_MAX) ||
		!udb_parse_uint_strict(colon + 1, &parsed_period, 1, INT_MAX))
		return 0;
	if (attempts)
		*attempts = (int)parsed_attempts;
	if (period)
		*period = (int)parsed_period;
	return 1;
}

static void udb_settings_restore_flood(void)
{
	if (!udb_cfg)
		return;
	udb_cfg->flood_attempts = udb_cfg->config_flood_attempts;
	udb_cfg->flood_period = udb_cfg->config_flood_period;
}

typedef struct UdbSettingOps
{
	const char *key;
	UdbValFunc validator;
	size_t value_offset;
	unsigned int flags;
} UdbSettingOps;

#define UDB_SETTING_NUMERIC_ONLY 0x1
#define UDB_SETTING_FLOOD 0x2
#define UDB_SETTING_REFRESH_HOSTS 0x4
#define UDB_SETTING_POLICY_CHANGED 0x8

static const UdbSettingOps udb_setting_ops[] = {
	{SKEY_QUIT_CLONES, udb_setting_string_valid, offsetof(UdbContext, quit_clones), 0},
	{SKEY_CLONES, NULL, 0, UDB_SETTING_NUMERIC_ONLY},
	{SKEY_FLOOD, NULL, 0, UDB_SETTING_FLOOD},
	{SKEY_SUFFIX, udb_suffix_valid, offsetof(UdbContext, suffix), UDB_SETTING_REFRESH_HOSTS},
	{SKEY_CRYPT_KEY, udb_encryption_key_valid, offsetof(UdbContext, encryption_key), UDB_SETTING_REFRESH_HOSTS},
	{SKEY_NICKSERV, udb_service_mask_valid, offsetof(UdbContext, nickserv_mask), 0},
	{SKEY_CHANSERV, udb_service_mask_valid, offsetof(UdbContext, chanserv_mask), 0},
	{SKEY_IPSERV, udb_service_mask_valid, offsetof(UdbContext, ipserv_mask), 0},
	{SKEY_PROPAGATOR, udb_propagator_list_valid, offsetof(UdbContext, propagator_setting), UDB_SETTING_POLICY_CHANGED},
};

static const UdbSettingOps *udb_setting_ops_find(const char *key)
{
	for (size_t i = 0; i < sizeof(udb_setting_ops) / sizeof(udb_setting_ops[0]); i++)
		if (!strcmp(udb_setting_ops[i].key, key))
			return &udb_setting_ops[i];
	return NULL;
}

static char **udb_setting_slot(UdbContext *ctx, const UdbSettingOps *ops)
{
	return (char **)((char *)ctx + ops->value_offset);
}

/* Prepare every derived setting before replacing any foreground slot. A
 * multi-setting import never exposes a transient encryption-key/suffix pair. */
static UdbStorageResult udb_native_settings_apply(UdbContext *ctx, UdbStorageRead *read)
{
	char *values[sizeof(udb_setting_ops) / sizeof(udb_setting_ops[0])] = {0};
	UdbStorageField *field;
	UdbStorageResult result = UDB_STORAGE_OK;
	int attempts = udb_cfg ? udb_cfg->config_flood_attempts : 5;
	int period = udb_cfg ? udb_cfg->config_flood_period : 60;
	int refresh = 0, policy = 0;
	if (!ctx || !ctx->storage || !read)
		return UDB_STORAGE_ERROR;
	field = udb_storage_alloc(&ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*field));
	if (!field)
		return UDB_STORAGE_LIMIT;
	for (size_t i = 0; i < sizeof(udb_setting_ops) / sizeof(udb_setting_ops[0]); i++)
	{
		const UdbSettingOps *ops = udb_setting_ops + i;
		result = udb_native_read_scalar(read, UDB_BLOCK_SETTINGS, NULL, ops->key, field);
		if (result < UDB_STORAGE_NOT_FOUND)
			goto finish;
		if (ops->flags & UDB_SETTING_NUMERIC_ONLY)
		{
			if (result == UDB_STORAGE_OK && (field->kind != UDB_STORAGE_VALUE_NUMBER || !field->number))
			{
				result = UDB_STORAGE_CORRUPT;
				goto finish;
			}
			continue;
		}
		if (ops->flags & UDB_SETTING_FLOOD)
		{
			if (result == UDB_STORAGE_OK &&
				(field->kind != UDB_STORAGE_VALUE_STRING || !udb_flood_valid(field->string, &attempts, &period)))
			{
				result = UDB_STORAGE_CORRUPT;
				goto finish;
			}
			continue;
		}
		if (result == UDB_STORAGE_OK)
		{
			if (field->kind != UDB_STORAGE_VALUE_STRING || (ops->validator && !ops->validator(field->string)))
			{
				result = UDB_STORAGE_CORRUPT;
				goto finish;
			}
			values[i] = udb_storage_alloc(&ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, (size_t)field->length + 1);
			if (!values[i])
			{
				result = UDB_STORAGE_LIMIT;
				goto finish;
			}
			memcpy(values[i], field->string, (size_t)field->length + 1);
		}
		const char *old = *udb_setting_slot(ctx, ops);
		int changed = (old != NULL) != (values[i] != NULL) || (old && values[i] && strcmp(old, values[i]));
		if (changed && (ops->flags & UDB_SETTING_REFRESH_HOSTS))
			refresh = 1;
		if (changed && (ops->flags & UDB_SETTING_POLICY_CHANGED))
			policy = 1;
	}
	for (size_t i = 0; i < sizeof(udb_setting_ops) / sizeof(udb_setting_ops[0]); i++)
	{
		const UdbSettingOps *ops = udb_setting_ops + i;
		if (ops->flags & (UDB_SETTING_NUMERIC_ONLY | UDB_SETTING_FLOOD))
			continue;
		char **slot = udb_setting_slot(ctx, ops);
		udb_storage_free(*slot);
		*slot = values[i];
		values[i] = NULL;
	}
	if (udb_cfg)
	{
		udb_cfg->flood_attempts = attempts;
		udb_cfg->flood_period = period;
	}
	if (refresh)
		udb_ip_refresh_derived_hosts();
	if (policy)
		udb_propagator_policy_changed(ctx);
	result = UDB_STORAGE_OK;
finish:
	for (size_t i = 0; i < sizeof(udb_setting_ops) / sizeof(udb_setting_ops[0]); i++)
		udb_storage_free(values[i]);
	udb_storage_free(field);
	return result;
}

/* End of udb_config.c.inc */

/* Pure business schema, bounded parsing, and foreground session lifetime */
/* Inlined: udb_core.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Database Core Engine & Record Manipulation
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int udb_strtoull_strict(const char *s, unsigned long long *out)
{
	char *end;
	unsigned long long val;

	if (!s || !*s)
		return 0;
	for (const char *p = s; *p; p++)
	{
		if (!isdigit((unsigned char)*p))
			return 0;
	}
	errno = 0;
	val = strtoull(s, &end, 10);
	if (errno == ERANGE || *end != '\0')
		return 0;
	if (out)
		*out = val;
	return 1;
}

static int udb_strtoul_strict(const char *s, unsigned long *out)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val) || val > ULONG_MAX)
		return 0;
	if (out)
		*out = (unsigned long)val;
	return 1;
}

static int udb_parse_uint64_strict(const char *s, uint64_t *out)
{
	unsigned long long val;

	if (!udb_strtoull_strict(s, &val) || val > UINT64_MAX)
		return 0;
	if (out)
		*out = (uint64_t)val;
	return 1;
}

static int udb_parse_uint_strict(const char *s, unsigned int *out, unsigned int min_val, unsigned int max_val)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val) || val < min_val || val > max_val || val > UINT_MAX)
		return 0;
	if (out)
		*out = (unsigned int)val;
	return 1;
}

static int udb_parse_ulong_strict(const char *s, unsigned long *out, unsigned long min_val, unsigned long max_val)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val) || val < min_val || val > max_val || val > ULONG_MAX)
		return 0;
	if (out)
		*out = (unsigned long)val;
	return 1;
}

static int udb_parse_size_strict(const char *s, size_t *out, size_t min_val, size_t max_val)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val) || val < min_val || val > max_val || val > SIZE_MAX)
		return 0;
	if (out)
		*out = (size_t)val;
	return 1;
}

static unsigned long long udb_time_t_max_val(void)
{
	if (((time_t)-1) < 0)
	{
		if (sizeof(time_t) <= 4)
			return (unsigned long long)0x7FFFFFFFUL;
		else
			return (unsigned long long)0x7FFFFFFFFFFFFFFFLL;
	}
	else
	{
		if (sizeof(time_t) <= 4)
			return (unsigned long long)0xFFFFFFFFUL;
		else
			return (unsigned long long)0xFFFFFFFFFFFFFFFFULL;
	}
}

static int udb_parse_time_t(const char *s, time_t *out)
{
	unsigned long long val;
	if (!udb_strtoull_strict(s, &val))
		return 0;
	if (val > udb_time_t_max_val())
		return 0;
	if (out)
		*out = (time_t)val;
	return 1;
}

static int udb_time_add(time_t base, unsigned long duration, time_t *result)
{
	if (base < 0)
		return 0;
	unsigned long long max_t = udb_time_t_max_val();
	unsigned long long base_ull = (unsigned long long)base;
	unsigned long long dur_ull = (unsigned long long)duration;

	if (base_ull > max_t)
		return 0;
	if (dur_ull > max_t - base_ull)
		return 0;

	unsigned long long sum = base_ull + dur_ull;
	if (sum > max_t)
		return 0;
	if (result)
		*result = (time_t)sum;
	return 1;
}

static int udb_timestamp_parse(const char *s, time_t *out)
{
	return udb_parse_time_t(s, out);
}

static int udb_digest_parse(const char *input, char out_hex[UDB_SHA256_HEX_LEN + 1])
{
	if (!input || strlen(input) != UDB_SHA256_HEX_LEN)
		return 0;
	for (int i = 0; i < UDB_SHA256_HEX_LEN; i++)
	{
		unsigned char c = (unsigned char)input[i];
		if (!isdigit(c) && (c < 'a' || c > 'f'))
			return 0;
		if (out_hex)
			out_hex[i] = (char)c;
	}
	if (out_hex)
		out_hex[UDB_SHA256_HEX_LEN] = '\0';
	return 1;
}

static int udb_numeric_record_valid(const char *value)
{
	unsigned long val;

	if (!value || value[0] != '*' || value[1] == '\0')
		return 0;
	return udb_strtoul_strict(value + 1, &val);
}

static int udb_clone_limit_valid(const char *value)
{
	unsigned long val;

	return udb_numeric_record_valid(value) && udb_strtoul_strict(value + 1, &val) && val <= INT_MAX;
}

static int udb_k_expires_valid(const char *value)
{
	time_t expires;

	/* Zero is deliberately not a second spelling of permanent. */
	return udb_numeric_record_valid(value) && udb_parse_time_t(value + 1, &expires) && expires > 0;
}

static int udb_zline_mask_valid(const char *mask)
{
	char address[INET6_ADDRSTRLEN], canonical[INET6_ADDRSTRLEN];
	const char *slash;
	unsigned int prefix;
	int family;
	size_t length;
	struct in_addr v4, v4_network;
	struct in6_addr v6, v6_network;

	if (!mask || !*mask || strchr(mask, '@'))
		return 0;
	slash = strchr(mask, '/');
	length = slash ? (size_t)(slash - mask) : strlen(mask);
	if (!length || length >= sizeof(address) || (slash && strchr(slash + 1, '/')))
		return 0;
	memcpy(address, mask, length);
	address[length] = '\0';
	if (inet_pton(AF_INET, address, &v4) == 1)
	{
		family = AF_INET;
		if (!inet_ntop(family, &v4, canonical, sizeof(canonical)) || strcmp(address, canonical))
			return 0;
	}
	else if (inet_pton(AF_INET6, address, &v6) == 1)
	{
		family = AF_INET6;
		if (!inet_ntop(family, &v6, canonical, sizeof(canonical)) || strcmp(address, canonical))
			return 0;
	}
	else
		return 0;
	if (!slash)
		return 1;
	if (!udb_parse_uint_strict(slash + 1, &prefix, 0, family == AF_INET ? 32 : 128))
		return 0;
	/* CIDR identities are network identities: host bits would be an alias. */
	if (family == AF_INET)
	{
		uint32_t raw = ntohl(v4.s_addr);
		uint32_t network = prefix ? raw & (0xffffffffU << (32 - prefix)) : 0;
		v4_network.s_addr = htonl(network);
		return !memcmp(&v4, &v4_network, sizeof(v4));
	}
	memcpy(&v6_network, &v6, sizeof(v6));
	for (unsigned int i = prefix; i < 128; i++)
		if (v6_network.s6_addr[i / 8] & (0x80U >> (i % 8)))
			return 0;
	return 1;
}

static int udb_spamfilter_match_type_valid(const char *value)
{
	return value && (!strcmp(value, "regex") || !strcmp(value, "simple"));
}

static int udb_spamfilter_match_type_value(const char *value)
{
	return value && !strcmp(value, "simple") ? MATCH_SIMPLE : MATCH_PCRE_REGEX;
}

static int udb_spamfilter_pattern_valid(const char *stored, const char *match_type)
{
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	Match *match;

	if (!udb_spamfilter_pattern(stored, pattern, sizeof(pattern)))
		return 0;
	if (!match_type)
		return 1; /* A partial F profile is stored but never materialized. */
	match = unreal_create_match(udb_spamfilter_match_type_value(match_type), pattern, NULL);
	if (!match)
		return 0;
	unreal_delete_match(match);
	return 1;
}

static int udb_spamfilter_targets_valid(const char *value)
{
	static const char canonical[] = "cpnNPqduatTR";
	const char *p;
	unsigned short targets;

	if (!value || !*value || strlen(value) >= sizeof(canonical))
		return 0;
	for (p = value; *p; p++)
	{
		const char *pos = strchr(canonical, *p);
		if (!pos || (p > value && p[-1] >= *p))
			return 0;
	}
	targets = (unsigned short)spamfilter_gettargets(value, NULL);
	return targets && !strcmp(value, spamfilter_target_inttostring(targets));
}

static int udb_k_ban_time_valid(const char *value)
{
	time_t duration;
	return udb_numeric_record_valid(value) && udb_parse_time_t(value + 1, &duration) && duration > 0;
}

static int udb_line_reason_valid(const char *value)
{
	const unsigned char *p;
	if (!value || !*value || value[0] == '*')
		return 0;
	for (p = (const unsigned char *)value; *p; p++)
		if (*p == '\r' || *p == '\n')
			return 0;
	return 1;
}

static int udb_line_mask_valid(const char *mask)
{
	const char *at;
	size_t userlen, hostlen;

	if (!mask || !*mask || !(at = strchr(mask, '@')) || strchr(at + 1, '@'))
		return 0;
	userlen = (size_t)(at - mask);
	hostlen = strlen(at + 1);
	return userlen > 0 && userlen <= UDB_TKL_MASK_COMPONENT_MAX && hostlen > 0 && hostlen <= UDB_TKL_MASK_COMPONENT_MAX;
}

static int udb_qline_mask_valid(const char *mask)
{
	const unsigned char *p;
	size_t len;

	if (!mask || !(len = strlen(mask)) || len > NICKLEN)
		return 0;
	for (p = (const unsigned char *)mask; *p; p++)
		if (iscntrl(*p) || *p == ':' || *p == '@')
			return 0;
	return 1;
}

static int udb_options_record_valid(const char *value)
{
	return udb_numeric_record_valid(value);
}

static int udb_channel_modes_record_valid(const char *value)
{
	char modebuf[512];
	char *modes;
	char *p, *param;
	int expected_params = 0;
	int actual_params = 0;
	int mode_letters = 0;
	int what = MODE_ADD;
	const char *c;

	if (!value || !*value || strlen(value) >= sizeof(modebuf))
		return 0;

	strlcpy(modebuf, value, sizeof(modebuf));
	modes = strtoken(&p, modebuf, " ");
	if (!modes || !*modes)
		return 0;

	for (c = modes; *c; c++)
	{
		Cmode *cm;

		if (*c == '+')
		{
			what = MODE_ADD;
			continue;
		}
		if (*c == '-')
		{
			what = MODE_DEL;
			continue;
		}

		/* +r is UDB-owned registration state, +P and +O are expressed by
		 * C::options bits, and member ranks never live here. */
		if (*c == 'r' || *c == 'P' || *c == 'O')
			return 0;

		cm = find_channel_mode_handler(*c);
		if (!cm)
			return 0;

		if (cm->type == CMODE_MEMBER)
			return 0;

		mode_letters++;
		if (what == MODE_ADD && cm->paracount > 0)
		{
			expected_params++;
		}
		else if (what == MODE_DEL && cm->paracount > 0 && cm->unset_with_param)
		{
			expected_params++;
		}
	}

	if (mode_letters == 0)
		return 0;

	while ((param = strtoken(&p, NULL, " ")))
	{
		if (!*param)
			return 0;
		actual_params++;
	}

	return expected_params <= MAXMODEPARAMS && expected_params == actual_params;
}

static int udb_user_mode_letter_valid(char c)
{
	Umode *um;
	if (c == 'o') /* +o is strictly forbidden in N::modes */
		return 0;
	if (!isalpha((unsigned char)c))
		return 0;
	if (!usermodes)
		return 1;
	for (um = usermodes; um; um = um->next)
	{
		if (um->letter == c)
			return 1;
	}
	return 0;
}

static int udb_user_modes_record_valid(const char *value)
{
	const char *c;
	int letters = 0;

	if (!value || !*value || strlen(value) > 64)
		return 0;

	for (c = value; *c; c++)
	{
		if (*c == '+' || *c == '-')
			continue;
		if (!udb_user_mode_letter_valid(*c))
			return 0;
		letters++;
	}

	return letters > 0;
}

static int udb_snomasks_record_valid(const char *value)
{
	const char *c;
	int letters = 0;

	if (!value || !*value || strlen(value) > 64)
		return 0;

	for (c = value; *c; c++)
	{
		if (*c == '+' || *c == '-')
			continue;
		if (!isalpha((unsigned char)*c) || !is_valid_snomask(*c))
			return 0;
		letters++;
	}

	return letters > 0;
}

static int udb_oper_record_valid(const char *value)
{
	const char *c;
	if (!value || !*value || strlen(value) > 64)
		return 0;
	for (c = value; *c; c++)
	{
		if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-')
			return 0;
	}
	return 1;
}

static int udb_password_hash_valid(const char *value)
{
	size_t i;
	if (!value || !*value)
		return 0;
	if (!strncmp(value, "argon2id:$argon2id$", 19))
		return 1;
	if (!strncmp(value, "crypt:", 6))
		return value[6] != '\0';
	if (strncmp(value, "sha256:", 7) || strlen(value + 7) != 64)
		return 0;
	for (i = 7; value[i]; i++)
	{
		if (!isxdigit((unsigned char)value[i]))
			return 0;
	}
	return 1;
}

static int udb_vhost_valid(const char *value)
{
	return value && *value && strlen(value) <= HOSTLEN && !strpbrk(value, " \t\r\n");
}

static int udb_nolines_record_valid(const char *value)
{
	const char *c;
	if (!value || !*value || strlen(value) > 16)
		return 0;
	for (c = value; *c; c++)
	{
		if (!strchr("GZQSTmc", *c))
			return 0;
	}
	return 1;
}

static int udb_non_empty_string_valid(const char *value)
{
	return value && *value && strlen(value) <= UDB_RECORD_VALUE_MAX && !strpbrk(value, "\r\n");
}

static int udb_nick_name_valid(const char *name)
{
	const char *c;
	if (!name || !*name || strlen(name) > NICKLEN)
		return 0;
	if (isdigit((unsigned char)name[0]) || name[0] == '-')
		return 0;
	for (c = name; *c; c++)
	{
		if (!isalnum((unsigned char)*c) && !strchr("[]\\`_^{|}-", *c))
			return 0;
	}
	return 1;
}

static int udb_channel_name_valid(const char *name)
{
	const char *c;
	if (!name || strlen(name) < 2 || strlen(name) > CHANNELLEN)
		return 0;
	if (name[0] != '#' && name[0] != '&')
		return 0;
	for (c = name; *c; c++)
	{
		if (*c <= ' ' || *c == ',' || *c == ':' || *c == 7)
			return 0;
	}
	return 1;
}

static int udb_ip_host_mask_valid(const char *mask)
{
	const char *c;
	if (!mask || !*mask || strlen(mask) > HOSTLEN)
		return 0;
	for (c = mask; *c; c++)
	{
		if (*c <= ' ' || (unsigned char)*c > 126)
			return 0;
	}
	return 1;
}

static int udb_server_name_valid(const char *srv)
{
	return srv && *srv && strlen(srv) <= HOSTLEN && !strpbrk(srv, " \t\r\n") && valid_server_name(srv);
}

/* Iterate a comma-separated propagator list without copying or truncating it.
 * Spaces are accepted only around commas; tabs and line breaks are rejected. */
static int udb_propagator_list_next(const char **cursor, char name[HOSTLEN + 1])
{
	const char *start, *end, *p;
	size_t len;

	if (!cursor || !(p = *cursor) || !*p)
		return 0;
	while (*p == ' ')
		p++;
	if (!*p || *p == ',')
		return -1;
	start = p;
	while (*p && *p != ',')
		p++;
	end = p;
	while (end > start && end[-1] == ' ')
		end--;
	len = (size_t)(end - start);
	if (!len || len > HOSTLEN)
		return -1;
	memcpy(name, start, len);
	name[len] = '\0';
	if (!udb_server_name_valid(name))
		return -1;
	if (*p == ',')
	{
		p++;
		if (!*p)
			return -1;
	}
	*cursor = p;
	return 1;
}

static int udb_propagator_list_valid(const char *value)
{
	const char *cursor = value;
	char name[HOSTLEN + 1];
	int result, count = 0;

	if (!value || !*value || strlen(value) > UDB_RECORD_VALUE_MAX || strpbrk(value, "\r\n\t"))
		return 0;
	while ((result = udb_propagator_list_next(&cursor, name)) > 0)
		count++;
	return result == 0 && count > 0;
}

static int udb_flood_setting_valid(const char *value)
{
	int attempts = 0, period = 0;
	return udb_flood_valid(value, &attempts, &period);
}

static int udb_tkl_type_valid(const char *type)
{
	return type && strlen(type) == 1 && strchr("GZSQF", type[0]);
}

static int udb_spamfilter_action_valid(const char *value)
{
	BanActionValue action = value && *value ? banact_stringtoval(value) : 0;
	return action && !banact_config_only(action);
}

/* Admission, staged snapshots, disk load and runtime apply all use this
 * candidate-level check.  Partial profiles are safe and inert; a complete
 * profile must be materializable before it can replace an active one. */
static const UdbKeyDescriptor udb_schema_n_subkeys[] = {
	{NKEY_ACCESS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_PASS, UDB_VAL_STRING, udb_password_hash_valid, 0, NULL},
	{NKEY_VHOST, UDB_VAL_STRING, udb_vhost_valid, 0, NULL},
	{NKEY_FORBID, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_SUSPEND, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_OPER, UDB_VAL_STRING, udb_oper_record_valid, 0, NULL},
	{NKEY_MODES, UDB_VAL_STRING, udb_user_modes_record_valid, 0, NULL},
	{NKEY_SNOMASKS, UDB_VAL_STRING, udb_snomasks_record_valid, 0, NULL},
	{NKEY_SWHOIS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_c_subkeys[] = {
	{CKEY_FOUNDER, UDB_VAL_STRING, udb_nick_name_valid, 0, NULL},
	{CKEY_MODES, UDB_VAL_STRING, udb_channel_modes_record_valid, 0, NULL},
	{CKEY_TOPIC, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_ACCESS, UDB_VAL_ANY, NULL, 1, udb_nick_name_valid},
	{CKEY_FORBID, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_SUSPEND, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_OPTIONS, UDB_VAL_NUMERIC, udb_options_record_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_i_subkeys[] = {
	{IKEY_CLONES, UDB_VAL_NUMERIC, udb_clone_limit_valid, 0, NULL},
	{IKEY_NOLINES, UDB_VAL_STRING, udb_nolines_record_valid, 0, NULL},
	{IKEY_HOST, UDB_VAL_STRING, udb_vhost_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_s_subkeys[] = {
	{SKEY_CLONES, UDB_VAL_NUMERIC, udb_clone_limit_valid, 0, NULL},
	{SKEY_QUIT_CLONES, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{SKEY_FLOOD, UDB_VAL_STRING, udb_flood_setting_valid, 0, NULL},
	{SKEY_CRYPT_KEY, UDB_VAL_STRING, udb_encryption_key_valid, 0, NULL},
	{SKEY_SUFFIX, UDB_VAL_STRING, udb_suffix_valid, 0, NULL},
	{SKEY_NICKSERV, UDB_VAL_STRING, udb_service_mask_valid, 0, NULL},
	{SKEY_CHANSERV, UDB_VAL_STRING, udb_service_mask_valid, 0, NULL},
	{SKEY_IPSERV, UDB_VAL_STRING, udb_service_mask_valid, 0, NULL},
	{SKEY_PROPAGATOR, UDB_VAL_STRING, udb_propagator_list_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_l_subkeys[] = {
	{LKEY_OPTIONS, UDB_VAL_NUMERIC, udb_options_record_valid, 0, NULL}};

static const UdbBlockSchema udb_block_schemas[] = {{UDB_BLOCK_NICKS, 1, 2, udb_nick_name_valid, udb_schema_n_subkeys,
													sizeof(udb_schema_n_subkeys) / sizeof(udb_schema_n_subkeys[0])},
												   {UDB_BLOCK_CHANNELS, 1, 3, udb_channel_name_valid,
													udb_schema_c_subkeys,
													sizeof(udb_schema_c_subkeys) / sizeof(udb_schema_c_subkeys[0])},
												   {UDB_BLOCK_IPS, 1, 2, udb_ip_host_mask_valid, udb_schema_i_subkeys,
													sizeof(udb_schema_i_subkeys) / sizeof(udb_schema_i_subkeys[0])},
												   {UDB_BLOCK_SETTINGS, 1, 1, NULL, udb_schema_s_subkeys,
													sizeof(udb_schema_s_subkeys) / sizeof(udb_schema_s_subkeys[0])},
												   {UDB_BLOCK_LINKS, 1, 2, udb_server_name_valid, udb_schema_l_subkeys,
													sizeof(udb_schema_l_subkeys) / sizeof(udb_schema_l_subkeys[0])},
												   {UDB_BLOCK_LINES, 2, 3, udb_tkl_type_valid, NULL, 0}};

static const UdbBlockDescriptor udb_block_descriptors[] = {
	{UDB_BLOCK_NICKS, "Nicks", UDB_BLOCK_MASK_N, &udb_block_schemas[0]},
	{UDB_BLOCK_CHANNELS, "Channels", UDB_BLOCK_MASK_C, &udb_block_schemas[1]},
	{UDB_BLOCK_IPS, "IPs", UDB_BLOCK_MASK_I, &udb_block_schemas[2]},
	{UDB_BLOCK_SETTINGS, "Settings", UDB_BLOCK_MASK_S, &udb_block_schemas[3]},
	{UDB_BLOCK_LINKS, "Links", UDB_BLOCK_MASK_L, &udb_block_schemas[4]},
	{UDB_BLOCK_LINES, "Lines", UDB_BLOCK_MASK_K, &udb_block_schemas[5]},
};

_Static_assert(sizeof(udb_block_descriptors) / sizeof(udb_block_descriptors[0]) == UDB_NUM_BLOCKS,
			   "block descriptor table must cover every UDB block");

static const UdbBlockDescriptor *udb_block_descriptor(unsigned int index)
{
	return index < UDB_NUM_BLOCKS ? &udb_block_descriptors[index] : NULL;
}

static int udb_block_descriptors_validate(void)
{
	for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		const UdbBlockDescriptor *descriptor = &udb_block_descriptors[i];

		if (!descriptor->name || !descriptor->schema || descriptor->schema->letter != descriptor->letter ||
			descriptor->mask != (1u << i))
			return 0;
	}
	return 1;
}

static int udb_block_letter_to_index(char letter)
{
	for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
		if (udb_block_descriptors[i].letter == letter)
			return (int)i;
	return -1;
}

static unsigned int udb_block_letter_to_mask(char letter)
{
	int index = udb_block_letter_to_index(letter);

	return index < 0 ? 0 : udb_block_descriptors[index].mask;
}

static const UdbBlockSchema *udb_get_block_schema(char letter)
{
	int index = udb_block_letter_to_index(letter);

	return index < 0 ? NULL : udb_block_descriptors[index].schema;
}

static void udb_sync_session_free(UdbBlock *block)
{
	if (!block || !block->session)
		return;
	udb_storage_free(block->session);
	block->session = NULL;
	block->syncing_from = NULL;
}

static const char *udb_get_bot_nick(const char *service_key, int force_default)
{
	static char buf[64];
	const char *mask = udb_get_bot_mask(service_key, force_default);
	char *p;

	strlcpy(buf, mask, sizeof(buf));
	p = strchr(buf, '!');
	if (p)
		*p = '\0';
	return buf;
}

static const char *udb_get_bot_mask(const char *service_key, int force_default)
{
	if (!force_default && udb_ctx)
	{
		if (!strcasecmp(service_key, SKEY_NICKSERV) && udb_ctx->nickserv_mask)
			return udb_ctx->nickserv_mask;
		if (!strcasecmp(service_key, SKEY_CHANSERV) && udb_ctx->chanserv_mask)
			return udb_ctx->chanserv_mask;
		if (!strcasecmp(service_key, SKEY_IPSERV) && udb_ctx->ipserv_mask)
			return udb_ctx->ipserv_mask;
	}
	if (!strcasecmp(service_key, SKEY_NICKSERV))
		return "NickServ!*@*";
	if (!strcasecmp(service_key, SKEY_CHANSERV))
		return "ChanServ!*@*";
	if (!strcasecmp(service_key, SKEY_IPSERV))
		return "IpServ!*@*";
	return "UDB!*@*";
}

static void udb_send_to_debugs(Client *source, const char *fmt, ...)
{
	char buf[1024];
	va_list va;

	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);

	UdbStorageRead *read = NULL;
	if (udb_ctx && udb_ctx->storage && udb_storage_read_open(udb_ctx->storage, &read) == UDB_STORAGE_OK)
	{
		Client *client;
		list_for_each_entry(client, &server_list, special_node) if (IsServer(client) && client != source &&
																	udb_native_link_debug(read, client->name))
			sendto_one(client, NULL, ":%s NOTICE %s :[UDB Debug] %s", me.id, client->id, buf);
		udb_storage_read_close(&read);
	}

	if (udb_is_debug_enabled())
	{
		udb_log(ULOG_INFO, "UDB_DEBUG", source, "$msg", log_data_string("msg", buf));
	}
	else
	{
		unreal_log(ULOG_DEBUG, "udb", "UDB_DEBUG", source, "[UDB Debug] $msg", log_data_string("msg", buf));
	}
}

/* End of udb_core.c.inc */
/* Inlined: udb_native_read.c.inc */
/* Stable process-local ownership shared by staging and mutations. */
static uint64_t udb_native_owner_sequence;
static uint64_t udb_native_owner_next(void)
{
	return udb_native_owner_sequence == UINT64_MAX ? 0 : ++udb_native_owner_sequence;
}

/* Foreground canonical keys and copied, fixed-width disk profiles. Every call
 * owns its scratch and pin: native commands may synchronously re-enter hooks. */

static int udb_native_hex_digit(unsigned char c)
{
	return c >= '0' && c <= '9'	  ? c - '0'
		   : c >= 'a' && c <= 'f' ? c - 'a' + 10
		   : c >= 'A' && c <= 'F' ? c - 'A' + 10
								  : -1;
}

static UdbStorageResult udb_native_key_parse(const char *path, UdbNativeKey *key)
{
	if (!path || !key || strlen(path) < 4 || strlen(path) + UDB_S2S_OVERHEAD_MAX > UDB_S2S_LINE_MAX || path[1] != ':' ||
		path[2] != ':' || udb_storage_stage_block_index((unsigned char)path[0]) < 0)
		return UDB_STORAGE_REJECTED;
	memset(key, 0, sizeof(*key));
	key->row.block = (unsigned char)path[0];
	const unsigned char *cursor = (const unsigned char *)path + 3;
	while (*cursor)
	{
		if (key->row.depth == UDB_STORAGE_COMPONENTS_MAX)
			return UDB_STORAGE_REJECTED;
		size_t start = key->used;
		while (*cursor && !(cursor[0] == ':' && cursor[1] == ':'))
		{
			unsigned char value = *cursor++;
			if (value == '%')
			{
				if (!cursor[0] || !cursor[1])
					return UDB_STORAGE_REJECTED;
				int high = udb_native_hex_digit(cursor[0]), low = udb_native_hex_digit(cursor[1]);
				if (high < 0 || low < 0)
					return UDB_STORAGE_REJECTED;
				value = (unsigned char)((high << 4) | low);
				if (!value || (value != ':' && value != '%' && value > 32 && value < 127))
					return UDB_STORAGE_REJECTED;
				cursor += 2;
			}
			else if (value == ':' || value <= 32 || value >= 127)
				return UDB_STORAGE_REJECTED;
			if (key->used == sizeof(key->scratch) || key->used - start == UDB_STORAGE_COMPONENT_MAX)
				return UDB_STORAGE_REJECTED;
			key->scratch[key->used++] = value;
		}
		if (key->used == start)
			return UDB_STORAGE_REJECTED;
		key->row.components[key->row.depth].data = key->scratch + start;
		key->row.components[key->row.depth++].length = (uint32_t)(key->used - start);
		if (*cursor)
		{
			cursor += 2;
			if (!*cursor)
				return UDB_STORAGE_REJECTED;
		}
	}
	size_t length;
	UdbStorageResult result = udb_storage_row_encode(&key->row, key->bytes, sizeof(key->bytes), &length);
	return result == UDB_STORAGE_OK ? udb_storage_row_decode(key->bytes, length, &key->row) : result;
}

/* Escape spans directly; storage components are not NUL-terminated strings. */
static UdbStorageResult udb_native_key_path(const UdbStorageRow *row, char *out, size_t capacity)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t used = 0;
	if (!out || capacity < 2 || !udb_storage_row_valid(row))
		return UDB_STORAGE_ERROR;
	out[used++] = (char)row->block;
	for (unsigned int i = 0; i < row->depth; i++)
	{
		if (capacity - used <= 2)
			return UDB_STORAGE_LIMIT;
		out[used++] = ':';
		out[used++] = ':';
		UdbStorageSpan part = row->components[i];
		for (uint32_t j = 0; j < part.length; j++)
		{
			unsigned char c = part.data[j];
			int escape = c == ':' || c == '%' || c <= 32 || c >= 127;
			if (capacity - used <= (escape ? 3 : 1))
				return UDB_STORAGE_LIMIT;
			if (escape)
			{
				out[used++] = '%';
				out[used++] = hex[c >> 4];
				out[used++] = hex[c & 15];
			}
			else
				out[used++] = (char)c;
		}
	}
	out[used] = 0;
	return UDB_STORAGE_OK;
}

/* A profile copies only the named scalar fields. Variable children must use
 * read_iterator. Negative results must never become an absent profile. */
static UdbStorageResult udb_native_profile_load(UdbContext *ctx, unsigned char block, const char *entity,
												const char *const *fields, unsigned int count, UdbStorageProfile **out)
{
	UdbStorageRead *read = NULL;
	UdbStorageRow key;
	UdbStorageResult result, closed;
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!ctx || !ctx->storage || !entity || !*entity)
		return UDB_STORAGE_ERROR;
	memset(&key, 0, sizeof(key));
	key.block = block;
	key.depth = 1;
	size_t length = strlen(entity);
	if (length > UDB_STORAGE_COMPONENT_MAX)
		return UDB_STORAGE_REJECTED;
	key.components[0].data = (const unsigned char *)entity;
	key.components[0].length = (uint32_t)length;
	result = udb_storage_read_open(ctx->storage, &read);
	if (result != UDB_STORAGE_OK)
		return result;
	result = udb_storage_profile_load(read, &key, fields, count, out);
	closed = udb_storage_read_close(&read);
	if (closed != UDB_STORAGE_OK)
	{
		udb_storage_profile_free(out);
		return closed;
	}
	return result;
}

/* One settings scalar or one entity property, within the caller's generation.
 * An explicit NOT_FOUND is the only permitted default/fallback signal. */
static UdbStorageResult udb_native_read_scalar(UdbStorageRead *read, unsigned char block, const char *entity,
											   const char *property, UdbStorageField *field)
{
	UdbStorageRow key = {0};
	if (!property || !*property || strlen(property) > UDB_STORAGE_COMPONENT_MAX ||
		(entity && (!*entity || strlen(entity) > UDB_STORAGE_COMPONENT_MAX)))
		return UDB_STORAGE_REJECTED;
	key.block = block;
	if (entity)
	{
		key.components[0].data = (const unsigned char *)entity;
		key.components[0].length = (uint32_t)strlen(entity);
		key.depth++;
	}
	key.components[key.depth].data = (const unsigned char *)property;
	key.components[key.depth++].length = (uint32_t)strlen(property);
	return udb_storage_read_field(read, &key, field);
}

static int udb_native_link_debug(UdbStorageRead *read, const char *server)
{
	if (!read || !server)
		return 0;
	UdbStorageField *field = udb_storage_alloc(&read->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*field));
	if (!field)
		return 0;
	UdbStorageResult result = udb_native_read_scalar(read, UDB_BLOCK_LINKS, server, LKEY_OPTIONS, field);
	int enabled =
		result == UDB_STORAGE_OK && field->kind == UDB_STORAGE_VALUE_NUMBER && (field->number & UDB_LNKOPT_DEBUG);
	udb_storage_free(field);
	return enabled;
}

static int udb_is_debug_enabled(void)
{
	UdbStorageRead *read = NULL;
	if (!udb_ctx || !udb_ctx->storage || udb_storage_read_open(udb_ctx->storage, &read) != UDB_STORAGE_OK)
		return 0;
	int enabled = udb_native_link_debug(read, me.name);
	if (udb_storage_read_close(&read) != UDB_STORAGE_OK)
		return 0;
	return enabled;
}

/* End of udb_native_read.c.inc */
/* Inlined: udb_native_schema.c.inc */
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

/* End of udb_native_schema.c.inc */

/* Dynamic connected service clients and service-originated notices */
/* Inlined: udb_services.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Service-Client Resolution (NickServ, ChanServ, IpServ) & Notices
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

/* Resolve a unique live ULine user for this operation; missing or ambiguous
 * matches fall back to &me. The returned Client is borrowed, never cached. */
static Client *udb_service_source(const char *service_key)
{
	const char *mask = udb_get_bot_mask(service_key, 0);
	Client *source = NULL;
	Client *client;

	if (!mask || !match_user)
	{
		udb_log(ULOG_WARNING, "UDB_SERVICE_SOURCE_FALLBACK", NULL,
				"No user-mask matcher is available for $service; using the local server source",
				log_data_string("service", service_key));
		return &me;
	}

	list_for_each_entry(client, &client_list, client_node)
	{
		if (!IsUser(client) || IsDead(client) || !client->user || !IsULine(client) || !client->name[0] ||
			!client->user->username[0] || !client->user->realhost[0])
			continue;
		if (!match_user(mask, client, MATCH_CHECK_ALL))
			continue;
		if (source)
		{
			udb_log(ULOG_WARNING, "UDB_SERVICE_SOURCE_FALLBACK", NULL,
					"Multiple connected ULine users match the $service mask; using the local server source",
					log_data_string("service", service_key), log_data_string("mask", mask));
			return &me;
		}
		source = client;
	}

	if (!source)
	{
		udb_log(ULOG_WARNING, "UDB_SERVICE_SOURCE_FALLBACK", NULL,
				"No connected ULine user matches the $service mask; using the local server source",
				log_data_string("service", service_key), log_data_string("mask", mask));
		return &me;
	}

	return source;
}

static void udb_send_service_notice(Client *target, const char *service_key, FORMAT_STRING(const char *pattern), ...)
{
	Client *source;
	char message[1024];
	char *name;
	va_list vl;

	if (!target || !target->name[0])
		return;
	source = udb_service_source(service_key);
	name = *target->name ? target->name : "*";
	va_start(vl, pattern);
	ircvsnprintf(message, sizeof(message), pattern, vl);
	va_end(vl);
	if (source != &me)
	{
		sendto_prefix_one(target, source, NULL, ":%s NOTICE %s :%s", source->name, name, message);
	}
	else
	{
		const char *bot_nick = udb_get_bot_nick(service_key, 0);
		sendto_one(target, NULL, ":%s NOTICE %s :%s", bot_nick ? bot_nick : me.name, name, message);
	}
}

/* End of udb_services.c.inc */

/* Staged synchronization sessions: HEL capability and transfer state */
/* Inlined: udb_sync.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Staged Synchronization Sessions & Snapshot Transfers
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static unsigned long udb_sync_txid = 0;

typedef struct UdbHelloPeer UdbHelloPeer;

struct UdbHelloPeer
{
	Client *peer;
	time_t deadline;
	int state;
	int authorizes_us;
	int auth_received;
	int local_selection_sent;
	int reconciliation_offered;
	int ocl_capable;	 /* Peer advertised the mandatory OCL token. */
	int oclg_subscribed; /* Peer requested the OCLG consumer view. */
	int ocl_replayed;	 /* Inventories already replayed to this peer. */
	int oclg_replayed;	 /* OCLG snapshot already sent for this peer instance. */
	int remote_epoch_known;
	char remote_epoch[UDB_OCL_EPOCH_LEN + 1];
	UdbHelloPeer *next;
};

#define UDB_HEL_WAITING 1
#define UDB_HEL_CONFIRMED 2
#define UDB_HEL_UNSUPPORTED 3

static UdbHelloPeer *udb_hello_peers = NULL;
static UdbHelloPeer *udb_hello_peer(Client *server, int create);
static int udb_has_active_sessions(UdbContext *ctx);
static int udb_has_pending_requests(UdbContext *ctx);

static void udb_hello_cleanup_all(void)
{
	while (udb_hello_peers)
	{
		UdbHelloPeer *next = udb_hello_peers->next;
		safe_free(udb_hello_peers);
		udb_hello_peers = next;
	}
}

static void udb_maybe_start_reconciliation(Client *server, int force_offer)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);
	UdbPropagatorSelection selected;

	if (!peer || !udb_has_hello(server) || !IsServer(server) || !MyConnect(server))
		return;
	/* A peer authorizing us identifies this side as its upstream; it does not
	 * authorize a reverse pull.  Offer our committed snapshot only while
	 * READY, never while another authority/round is active, and never to our
	 * own selected upstream: that would reverse the authority direction and
	 * was the source of bilateral RES churn. */
	if (!peer->authorizes_us || !udb_ready)
		return;
	if (udb_select_propagator(udb_ctx, 1, &selected) && !selected.is_local && selected.peer == server)
		return;
	if (udb_reconcile.active || udb_has_active_sessions(udb_ctx) || udb_has_pending_requests(udb_ctx))
		return;
	if (force_offer || !peer->reconciliation_offered)
	{
		if (udb_sync_to_server(server))
			peer->reconciliation_offered = 1;
	}
}

static void udb_offer_reconciliation_to_downstreams(void)
{
	Client *server;

	if (!udb_ready)
		return;
	list_for_each_entry(server, &server_list, special_node)
	{
		if (IsServer(server) && MyConnect(server) && udb_has_hello(server) && udb_peer_authorizes_us(server))
			udb_maybe_start_reconciliation(server, 1);
	}
}

static UdbHelloPeer *udb_hello_peer(Client *server, int create)
{
	UdbHelloPeer *peer;

	for (peer = udb_hello_peers; peer; peer = peer->next)
		if (peer->peer == server)
			return peer;
	if (!create)
		return NULL;
	peer = safe_alloc(sizeof(*peer));
	peer->peer = server;
	peer->next = udb_hello_peers;
	udb_hello_peers = peer;
	return peer;
}

static int udb_has_hello(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	return server && IsServer(server) && MyConnect(server) && peer && peer->state == UDB_HEL_CONFIRMED;
}

static int udb_has_staged_sync(Client *server)
{
	return udb_has_hello(server);
}

static int udb_peer_authorizes_us(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	return udb_has_hello(server) && peer->authorizes_us;
}

static int udb_hello_epoch_valid(const char *epoch)
{
	int i;

	if (!epoch || strlen(epoch) != UDB_OCL_EPOCH_LEN)
		return 0;
	for (i = 0; i < UDB_OCL_EPOCH_LEN; i++)
	{
		char c = epoch[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return 0;
	}
	return 1;
}

static int udb_hello_epoch_matches(Client *server, const char *epoch)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	return peer && peer->remote_epoch_known && epoch && !strcmp(peer->remote_epoch, epoch);
}

static int udb_hello_parse_propagator(const char *propagator, int *authorizes_us)
{
	if (!propagator || !*propagator || !authorizes_us)
		return 0;
	if (!strcmp(propagator, "?"))
		*authorizes_us = 1;
	else if (!strcmp(propagator, "-"))
		*authorizes_us = 0;
	else if (udb_server_name_valid(propagator))
		*authorizes_us = !strcasecmp(propagator, me.name);
	else
		return 0;
	return 1;
}

static const char *udb_sync_hello_propagator(UdbPropagatorSelection *selected)
{
	if (selected && udb_select_propagator(udb_ctx, 1, selected))
		return selected->name;
	if (!udb_propagator_policy_present(udb_ctx))
		return udb_ready ? me.name : "?";
	return "-";
}

static int udb_hello_peer_advertisement(Client *server, const char *propagator, const char *epoch, int oclg_subscribed)
{
	UdbHelloPeer *peer;
	int authorizes_us;
	int epoch_changed;
	int subscription_changed;

	if (!server || !udb_hello_epoch_valid(epoch) || !udb_hello_parse_propagator(propagator, &authorizes_us))
		return 0;
	peer = udb_hello_peer(server, 1);
	epoch_changed = peer->remote_epoch_known && strcmp(peer->remote_epoch, epoch);
	subscription_changed = peer->oclg_subscribed != oclg_subscribed;
	if (epoch_changed)
	{
		/* The SERVER connection survives a module reload, but all data and
		 * replay latches belonging to the old remote instance do not. */
		peer->reconciliation_offered = 0;
		peer->ocl_replayed = 0;
		peer->oclg_replayed = 0;
		/* Recompute for every other subscriber now, then replay to this peer
		 * after its HEL exchange has been acknowledged. */
		peer->oclg_subscribed = 0;
		udb_ocl_peer_instance_changed(server);
		if (udb_ctx && udb_is_propagator(udb_ctx, server))
		{
			/* A neighbor instance change invalidates in-flight ownership, not the
			 * persisted root SID/epoch or its durable sequence. */
			if (udb_ctx->storage)
			{
				uint64_t revision;
				udb_storage_engine_authority(udb_ctx->storage, &revision);
			}
			if (udb_reconcile.active && udb_reconcile.authority_peer == server)
				udb_reconcile_abort(udb_ctx, "authority peer instance changed", 1);
			udb_sync_status = UDB_SYNC_DEGRADED;
			if (!udb_degraded_since)
				udb_degraded_since = time(NULL);
		}
		udb_log(ULOG_INFO, "UDB_HEL_INSTANCE_CHANGED", server, "Direct peer announced a new UDB instance epoch $epoch",
				log_data_string("epoch", epoch));
	}
	else if (subscription_changed)
		peer->oclg_replayed = 0;
	strlcpy(peer->remote_epoch, epoch, sizeof(peer->remote_epoch));
	peer->remote_epoch_known = 1;
	peer->authorizes_us = authorizes_us;
	peer->auth_received = 1;
	peer->ocl_capable = 1;
	peer->oclg_subscribed = oclg_subscribed;
	return 1;
}

static int udb_sync_hello_start(Client *server)
{
	UdbHelloPeer *peer;
	UdbPropagatorSelection selected;
	const char *propagator;
	int ok;

	if (!server || !IsServer(server) || !MyConnect(server))
		return 0;
	peer = udb_hello_peer(server, 1);
	if (peer->state == UDB_HEL_WAITING || peer->state == UDB_HEL_CONFIRMED)
		return 0;
	peer->state = UDB_HEL_WAITING;
	peer->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	/* No policy: a fresh node is seeking a bootstrap source, while a READY
	 * node is its own standalone authority. */
	propagator = udb_sync_hello_propagator(&selected);
	ok = udb_send_db_to_one(server, ":%s DB %s HEL " UDB_PROTOCOL_HEL " %s %s OCL", me.id, server->id, propagator,
							udb_ocl_epoch_value());
	if (!ok)
	{
		peer->state = 0;
		peer->deadline = 0;
		return 0;
	}
	peer->local_selection_sent = 1;
	return 1;
}

static void udb_sync_hello_ack(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	if (!peer)
		return;

	if (peer->state == UDB_HEL_CONFIRMED)
	{
		udb_maybe_start_reconciliation(server, 0);
		udb_ocl_maybe_replay_to_peer(server);
		return;
	}

	if (peer->state == UDB_HEL_WAITING)
	{
		peer->state = UDB_HEL_CONFIRMED;
		peer->deadline = 0;
		udb_log(ULOG_INFO, "UDB_HEL_CONFIRMED", server,
				"UDB HEL " UDB_PROTOCOL_HEL " capability confirmed for directly linked server");
		udb_propagator_policy_changed(udb_ctx);
		udb_maybe_start_reconciliation(server, 0);
		udb_ocl_maybe_replay_to_peer(server);
	}
}

static int udb_has_active_sessions(UdbContext *ctx)
{
	UdbBlock *block;
	if (!ctx)
		return 0;
	for (block = ctx->block_list; block; block = block->next)
	{
		if (block->session)
			return 1;
	}
	return 0;
}

static int udb_has_pending_requests(UdbContext *ctx)
{
	UdbBlock *block;
	if (!ctx)
		return 0;
	for (block = ctx->block_list; block; block = block->next)
	{
		if (block->pending_from)
			return 1;
	}
	return 0;
}

static void udb_reconcile_reset(void)
{
	udb_reconcile.authority_peer = NULL;
	udb_reconcile.active = 0;
	udb_reconcile.compared_blocks = 0;
	udb_reconcile.divergent_blocks = 0;
	udb_reconcile.completed_blocks = 0;
	udb_reconcile.generation = 0;
	udb_reconcile.started_at = 0;
	udb_reconcile.last_activity = 0;
	udb_reconcile.deadline = 0;
	udb_reconcile.absolute_deadline = 0;
	udb_reconcile.watermark_seq = 0;
	udb_reconcile.watermark_known = 0;
	udb_reconcile.source_known = 0;
	udb_reconcile.source_sid[0] = 0;
	udb_reconcile.source_epoch[0] = 0;
}

static void udb_reconcile_abort(UdbContext *ctx, const char *reason, int schedule_retry)
{
	UdbBlock *block;
	udb_native_receive_abort(udb_reconcile.round_id);
	udb_log(ULOG_WARNING, "UDB_RECONCILE_ABORT", udb_reconcile.authority_peer,
			"Reconciliation round $round aborted: $reason", log_data_integer("round", (int)udb_reconcile.round_id),
			log_data_string("reason", reason ? reason : "unspecified"));

	if (ctx)
	{
		for (block = ctx->block_list; block; block = block->next)
		{
			udb_block_clear_pending(block);
			if (block->session)
				udb_sync_abort(block, reason ? reason : "reconciliation aborted");
		}
	}
	if (schedule_retry && udb_reconcile.retry_count < UDB_RECONCILE_RETRY_MAX)
	{
		udb_reconcile.retry_count++;
		unsigned int shift = udb_reconcile.retry_count > 4 ? 4 : udb_reconcile.retry_count;
		udb_reconcile.next_retry_at = time(NULL) + (UDB_RECONCILE_RETRY_BASE << shift);
	}
	else
	{
		udb_reconcile.retry_count = 0;
		udb_reconcile.next_retry_at = 0;
	}
	udb_reconcile_reset();
}

/* INF, not the direct peer HEL, binds the root mutation lineage. Every block
 * in a round must agree before it can enter a private durable stage. */
static int udb_reconcile_accept_source(Client *peer, unsigned long round, const char *sid, const char *epoch,
									   uint64_t watermark)
{
	if (!udb_reconcile.active || udb_reconcile.authority_peer != peer || udb_reconcile.round_id != round ||
		!udb_storage_sid_valid(sid) || !udb_hello_epoch_valid(epoch))
		return 0;
	if (udb_reconcile.source_known &&
		(strcmp(udb_reconcile.source_sid, sid) || strcmp(udb_reconcile.source_epoch, epoch)))
	{
		udb_reconcile_abort(udb_ctx, "inconsistent reconciliation source lineage", 1);
		return 0;
	}
	if (!udb_reconcile_accept_watermark(peer, round, watermark))
		return 0;
	memcpy(udb_reconcile.source_sid, sid, 4);
	memcpy(udb_reconcile.source_epoch, epoch, UDB_OCL_EPOCH_LEN + 1);
	udb_reconcile.source_known = 1;
	return 1;
}

static int udb_reconcile_accept_watermark(Client *peer, unsigned long round_id, uint64_t watermark_seq)
{
	if (!udb_reconcile.active || udb_reconcile.authority_peer != peer || udb_reconcile.round_id != round_id)
		return 0;
	if (!udb_reconcile.watermark_known)
	{
		udb_reconcile.watermark_seq = watermark_seq;
		udb_reconcile.watermark_known = 1;
		return 1;
	}
	if (udb_reconcile.watermark_seq == watermark_seq)
		return 1;
	udb_reconcile_abort(udb_ctx, "inconsistent reconciliation watermark", 1);
	return 0;
}

/* One recovery boundary for every operation whose persistence outcome is not
 * trustworthy.  It intentionally reuses authority selection and retry bounds;
 * it does not alter HEL negotiation or hop-by-hop routing. */
static void udb_sync_round_failure(UdbBlock *block, Client *peer, unsigned long round_id, const char *reason)
{
	if (block && peer && udb_reconcile.active && udb_reconcile.authority_peer == peer &&
		udb_reconcile.round_id == round_id)
		udb_reconcile_abort(udb_ctx, reason, 1);
}

static void udb_reconcile_begin(Client *authority, unsigned long round_id)
{
	unsigned int retry_count = udb_reconcile.retry_count;
	udb_native_mutation_round_begin(authority);
	udb_ready = 0;
	udb_reconcile.authority_peer = authority;
	udb_reconcile.round_id = round_id;
	udb_reconcile.generation = round_id ? round_id : 1;
	udb_reconcile.active = 1;
	udb_reconcile.compared_blocks = 0;
	udb_reconcile.divergent_blocks = 0;
	udb_reconcile.completed_blocks = 0;
	udb_reconcile.retry_count = retry_count;
	udb_reconcile.next_retry_at = 0;
	udb_reconcile.started_at = time(NULL);
	udb_reconcile.last_activity = udb_reconcile.started_at;
	udb_reconcile.deadline =
		udb_reconcile.started_at +
		((udb_cfg && udb_cfg->sync_inactivity_timeout > 0) ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_TIMEOUT);
	/* The absolute deadline is never refreshed by traffic: a broken authority
	 * drip-feeding valid frames cannot keep a round alive indefinitely. */
	udb_reconcile.absolute_deadline =
		udb_reconcile.started_at +
		((udb_cfg && udb_cfg->sync_absolute_timeout > 0) ? udb_cfg->sync_absolute_timeout : UDB_SYNC_ABSOLUTE_TIMEOUT);
	udb_reconcile.watermark_seq = 0;
	udb_reconcile.watermark_known = 0;
	udb_reconcile.source_known = 0;
	udb_reconcile.source_sid[0] = 0;
	udb_reconcile.source_epoch[0] = 0;
	udb_log(ULOG_INFO, "UDB_RECONCILE_ROUND_START", authority,
			"Started reconciliation round $round with authority peer $peer",
			log_data_integer("round", (int)udb_reconcile.round_id), log_data_client("peer", authority));
}

static void udb_reconcile_start(Client *authority, unsigned long round_id)
{
	if (udb_reconcile.authority_peer != authority || !udb_reconcile.active || udb_reconcile.round_id != round_id)
	{
		if (udb_reconcile.active)
			udb_reconcile_abort(udb_ctx, "new reconciliation round", 0);
		udb_reconcile_begin(authority, round_id);
	}
}

/* Latch READY health to DEGRADED the moment divergence with the authority is
 * confirmed. Idempotent: only the OK -> DEGRADED transition is acted upon, and
 * only udb_reconcile_check() may ever perform the reverse transition. */
static void udb_sync_mark_degraded(Client *peer, const char *reason)
{
	if (!udb_ready || udb_sync_status != UDB_SYNC_OK)
		return;
	udb_sync_status = UDB_SYNC_DEGRADED;
	udb_log(ULOG_WARNING, "UDB_SYNC_DIVERGED", peer,
			"Database divergence confirmed ($reason); recovery required before health returns to OK",
			log_data_string("reason", reason ? reason : "unknown"));
}

static void udb_reconcile_record_res(Client *peer, unsigned long round_id, char letter)
{
	unsigned int mask = udb_block_letter_to_mask(letter);
	if (!mask)
		return;

	if (!udb_reconcile.active || udb_reconcile.authority_peer != peer || udb_reconcile.round_id != round_id)
		return;
	udb_reconcile.divergent_blocks |= mask;
	udb_reconcile.completed_blocks &= ~mask;
	udb_sync_mark_degraded(peer, "block recovery required");
	udb_reconcile.last_activity = time(NULL);
	udb_reconcile.deadline =
		udb_reconcile.last_activity +
		((udb_cfg && udb_cfg->sync_inactivity_timeout > 0) ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_TIMEOUT);
}

static void udb_reconcile_record_end(Client *peer, char letter, unsigned long round_id)
{
	unsigned int mask = udb_block_letter_to_mask(letter);
	if (!mask)
		return;

	if (!udb_reconcile.active || udb_reconcile.authority_peer != peer || udb_reconcile.round_id != round_id)
	{
		udb_log(ULOG_WARNING, "UDB_RECONCILE_STALE_END", peer,
				"Ignored staged END for block $block with round $session_round (current round is $current_round)",
				log_data_string("block", (char[]){letter, '\0'}), log_data_integer("session_round", (int)round_id),
				log_data_integer("current_round", (int)udb_reconcile.round_id));
		return;
	}

	udb_reconcile.compared_blocks |= mask;
	udb_reconcile.completed_blocks |= mask;
	udb_reconcile.divergent_blocks &= ~mask;
	udb_reconcile.last_activity = time(NULL);
	udb_reconcile.deadline =
		udb_reconcile.last_activity +
		((udb_cfg && udb_cfg->sync_inactivity_timeout > 0) ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_TIMEOUT);
}

static int udb_reconcile_check(UdbContext *ctx)
{
	return udb_native_receive_promote(ctx);
}

static int udb_is_authorized_sync_source(UdbContext *ctx, Client *direct_peer)
{
	if (!direct_peer || !udb_has_hello(direct_peer))
		return 0;

	if (udb_propagator_policy_present(ctx))
	{
		return udb_is_propagator(ctx, direct_peer);
	}
	else
	{
		/* Without a policy there is no remote authority once READY: this node
		 * is a standalone authority and accepts no imports.  Before READY only
		 * the exclusive bootstrap owner may feed us. */
		if (!udb_ready)
		{
			if (!udb_bootstrap_peer)
				udb_bootstrap_peer = direct_peer;

			return (udb_bootstrap_peer == direct_peer);
		}
		return 0;
	}
}

/* Observability-only tracker: how long no eligible propagator has been
 * usable. It never touches udb_ready, udb_sync_status or client admission;
 * the bootstrap ladder uses udb_degraded_since instead. */
static void udb_propagator_availability_refresh(void)
{
	UdbContext *ctx = udb_ctx;
	UdbPropagatorSelection selected;

	if (!udb_propagator_policy_present(ctx) || udb_select_propagator(ctx, 1, &selected))
	{
		udb_propagator_unavailable_since = 0;
		return;
	}
	if (udb_propagator_unavailable_since == 0)
		udb_propagator_unavailable_since = time(NULL);
}

static void udb_sync_status_refresh(void)
{
	UdbContext *ctx = udb_ctx;
	time_t now = time(NULL);
	int timeout = (udb_cfg && udb_cfg->stale_timeout > 0) ? udb_cfg->stale_timeout : 300;

	if (udb_ready)
	{
		udb_degraded_since = 0;
		/* READY health is latched by reconciliation (only udb_reconcile_check()
		 * may return DEGRADED to OK); STALE is invalid while READY. */
		if (udb_sync_status == UDB_SYNC_STALE)
			udb_sync_status = UDB_SYNC_OK;
		return;
	}

	/* Bootstrap-pending nodes age toward STALE regardless of propagator
	 * availability: the clock measures time without a READY database. */
	if (udb_degraded_since == 0)
		udb_degraded_since = now;

	time_t elapsed = (now >= udb_degraded_since) ? (now - udb_degraded_since) : 0;

	if (elapsed >= timeout)
	{
		if (udb_sync_status != UDB_SYNC_STALE)
		{
			udb_log(ULOG_ERROR, "UDB_SYNC_STALE", NULL,
					"UDB bootstrap is stale ($seconds seconds without a READY database); new local clients remain "
					"denied until bootstrap completes",
					log_data_integer("seconds", (int)elapsed));
			udb_sync_status = UDB_SYNC_STALE;
		}
	}
	else if (udb_sync_status != UDB_SYNC_DEGRADED && udb_sync_status != UDB_SYNC_STALE)
	{
		const char *policy = udb_propagator_policy(ctx);
		udb_log(ULOG_WARNING, "UDB_SYNC_DEGRADED", NULL,
				"UDB bootstrap pending; no READY database yet. Policy: $policy",
				log_data_string("policy", policy ? policy : "none"));
		udb_sync_status = UDB_SYNC_DEGRADED;
	}
}

static int udb_hook_readiness_pre_connect(Client *client)
{
	if (!client || !MyConnect(client) || IsServer(client))
		return HOOK_CONTINUE;

	if (!udb_ready)
	{
		udb_log(ULOG_WARNING, "UDB_NOT_READY_DENY_CLIENT", client,
				"Rejecting new local client connection: UDB initialization pending");
		exit_client(client, NULL,
					"UDB synchronization unavailable; this server is temporarily not accepting new connections");
		return HOOK_DENY;
	}
	return HOOK_CONTINUE;
}

static void udb_sync_hello_refresh_all(void)
{
	Client *server;
	UdbPropagatorSelection selected;
	const char *propagator;

	propagator = udb_sync_hello_propagator(&selected);
	list_for_each_entry(server, &server_list, special_node)
	{
		if (IsServer(server) && MyConnect(server) && udb_has_hello(server))
		{
			UdbHelloPeer *hello = udb_hello_peer(server, 0);
			if (udb_send_db_to_one(server, ":%s DB %s HEL " UDB_PROTOCOL_HEL " %s %s OCL", me.id, server->id,
								   propagator, udb_ocl_epoch_value()) &&
				hello)
				hello->local_selection_sent = 1;
		}
	}
}

static void udb_sync_abort(UdbBlock *block, const char *reason)
{
	if (!block)
		return;
	if (block->session)
	{
		udb_log(ULOG_WARNING, "UDB_SYNC_ABORT", udb_ocl_find_server(block->session->peer_sid),
				"Aborted staged sync of block $block: $reason", log_data_string("block", (char[]){block->letter, '\0'}),
				log_data_string("reason", reason));
		udb_sync_session_free(block);
	}
	block->syncing_from = NULL;
	if (block->pending_from)
	{
		block->pending_from = NULL;
		block->pending_deadline = 0;
		block->pending_round_id = 0;
	}
}

static void udb_propagator_policy_changed(UdbContext *ctx)
{
	UdbBlock *block;
	UdbPropagatorSelection selected;
	Client *peer = NULL;

	if (!ctx)
		return;
	if (udb_policy_notify_deferred)
	{
		/* A block commit is mid-flight; replay once the new tree is installed. */
		udb_policy_notify_pending = 1;
		return;
	}
	int policy = udb_propagator_policy_present(ctx);
	int available = policy ? udb_select_propagator(ctx, 1, &selected) : 0;
	if (policy && available && !selected.is_local)
		peer = selected.peer;
	int kind = !policy || (available && selected.is_local) ? 1 : peer ? 2 : 3;
	char sid[4] = {0}, epoch[17] = {0};
	if (kind == 1)
		memcpy(sid, me.id, 4);
	else if (peer)
	{
		UdbHelloPeer *hello = udb_hello_peer(peer, 0);
		memcpy(sid, peer->id, 4);
		if (hello && hello->remote_epoch_known)
			memcpy(epoch, hello->remote_epoch, 17);
	}
	int selection_changed =
		ctx->selection_kind != kind || memcmp(ctx->selected_sid, sid, 4) || memcmp(ctx->selected_epoch, epoch, 17);
	if (ctx->storage && selection_changed)
	{
		uint64_t revision;
		udb_storage_engine_authority(ctx->storage, &revision);
		udb_native_mutation_abort(0);
		ctx->selection_kind = kind;
		memcpy(ctx->selected_sid, sid, 4);
		memcpy(ctx->selected_epoch, epoch, 17);
		if (kind != 1)
		{
			udb_ready = 0;
			udb_sync_status = UDB_SYNC_DEGRADED;
		}
		if (udb_reconcile.active)
			udb_reconcile_abort(ctx, "selected authority or instance changed", 1);
	}

	/* Without a policy the exclusive bootstrap owner stays valid until it
	 * disconnects or READY completes; a later HEL from another peer must not
	 * steal the ownership. */
	if (udb_propagator_policy_present(ctx) && udb_bootstrap_peer && udb_bootstrap_peer != peer)
		udb_bootstrap_peer = NULL;
	if (udb_reconcile.authority_peer && policy && udb_reconcile.authority_peer != peer)
	{
		if (ctx->storage)
		{
			uint64_t revision;
			udb_storage_engine_authority(ctx->storage, &revision);
		}
		udb_reconcile_abort(ctx, "propagator policy changed", 1);
	}

	for (block = ctx->block_list; block; block = block->next)
	{
		/* Replacing block S removes the old tree's effects while the S session
		 * still owns its replacement tree. Do not abort that session from its
		 * own policy callback; the new policy is applied after commit. */
		if (block->session && block->letter != UDB_BLOCK_SETTINGS &&
			(!peer || strcmp(block->session->peer_sid, peer->id)))
			udb_sync_abort(block, "propagator policy changed");
		else if (!block->session && block->syncing_from && block->syncing_from != peer)
			block->syncing_from = NULL;

		if (block->pending_from && block->pending_from != peer)
		{
			block->pending_from = NULL;
			block->pending_deadline = 0;
			block->pending_round_id = 0;
		}
	}
	if (kind == 1 && ctx->storage && !udb_reconcile.active &&
		(!ctx->authority_epoch_known || strcmp(ctx->authority_sid, me.id)))
	{
		udb_ready = 0;
		udb_native_local_bind(ctx);
	}
	/* Confirming an unrelated downstream is not an upstream retry. Refreshing
	 * unchanged selections provokes fresh inventories and withdraws READY for
	 * no authority change; explicit retry paths still refresh HEL themselves.
	 * Without a policy the advertised bootstrap role also depends on READY. */
	if (selection_changed || !policy)
		udb_sync_hello_refresh_all();
	udb_propagator_availability_refresh();
	udb_sync_status_refresh();

	if (peer && udb_has_hello(peer) && (!udb_ready || udb_sync_status != UDB_SYNC_OK))
		udb_maybe_start_reconciliation(peer, 0);
}

static void udb_propagator_policy_flush(UdbContext *ctx)
{
	if (!udb_policy_notify_pending || udb_policy_notify_deferred)
		return;
	udb_policy_notify_pending = 0;
	udb_propagator_policy_changed(ctx);
}

static void udb_sync_ack(Client *peer, const char *block)
{
	udb_log(ULOG_INFO, "UDB_SYNC_ACK", peer, "Staged sync acknowledged for block $block",
			log_data_string("block", block));
}

static int udb_sync_send_stage(Client *server, UdbBlock *block, unsigned long round_id)
{
	return block && udb_native_sync_send(server, block->letter, round_id);
}

static time_t udb_anti_entropy_calc_interval(void)
{
	int base = (udb_cfg && udb_cfg->anti_entropy_interval > 0) ? udb_cfg->anti_entropy_interval
															   : UDB_DEFAULT_ANTI_ENTROPY_INTERVAL;
	int jitter_range = base / 10;
	if (jitter_range > 0)
	{
		int delta = (rand() % (2 * jitter_range + 1)) - jitter_range;
		int res = base + delta;
		return res > 0 ? (time_t)res : 1;
	}
	return base > 0 ? (time_t)base : 1;
}

static void udb_anti_entropy_schedule_next(time_t now)
{
	udb_anti_entropy.next_check_at = now + udb_anti_entropy_calc_interval();
}

static void udb_anti_entropy_trigger(time_t now, Client *authority_peer)
{
	static unsigned long ae_round_sequence = 0;
	unsigned long round_id = (unsigned long)now + ++ae_round_sequence;
	if (!round_id)
		round_id = ++ae_round_sequence;

	udb_anti_entropy.round_id = round_id;
	udb_anti_entropy.pending = 1;
	udb_anti_entropy.acked_blocks = 0;
	udb_anti_entropy.divergent_blocks = 0;
	udb_anti_entropy.watermark_seq = 0;
	udb_anti_entropy.watermark_known = 0;
	udb_anti_entropy.last_check = now;
	udb_anti_entropy.deadline = now + UDB_ANTI_ENTROPY_TIMEOUT;

	udb_send_db_to_one(authority_peer, ":%s DB %s MANIFEST REQ %lu", me.id, authority_peer->id, round_id);
}

static void udb_anti_entropy_timer_check(time_t now)
{
	if (!udb_ready || udb_sync_status != UDB_SYNC_OK)
		return;

	UdbPropagatorSelection selected;
	if (!udb_select_propagator(udb_ctx, 1, &selected) || selected.is_local || !selected.peer ||
		!udb_has_hello(selected.peer))
		return;

	if (udb_anti_entropy.pending)
	{
		if (now >= udb_anti_entropy.deadline)
		{
			udb_anti_entropy.pending = 0;
			udb_anti_entropy.fail_count++;
			unsigned int shift = udb_anti_entropy.fail_count > 4 ? 4 : udb_anti_entropy.fail_count;
			time_t delay = (time_t)(UDB_ANTI_ENTROPY_MIN_GAP << shift);
			udb_anti_entropy.next_check_at = now + delay;
		}
		return;
	}

	if (udb_reconcile.active || udb_has_active_sessions(udb_ctx) || udb_has_pending_requests(udb_ctx))
		return;

	if (udb_anti_entropy.next_check_at == 0)
	{
		udb_anti_entropy_schedule_next(now);
		return;
	}

	int base_interval = (udb_cfg && udb_cfg->anti_entropy_interval > 0) ? udb_cfg->anti_entropy_interval
																		: UDB_DEFAULT_ANTI_ENTROPY_INTERVAL;
	time_t min_gap = (base_interval < UDB_ANTI_ENTROPY_MIN_GAP) ? (time_t)base_interval : UDB_ANTI_ENTROPY_MIN_GAP;
	if (udb_anti_entropy.last_check && (now - udb_anti_entropy.last_check < min_gap))
		return;

	if (now >= udb_anti_entropy.next_check_at)
	{
		udb_anti_entropy_trigger(now, selected.peer);
	}
}

/* MANIFEST ACK remains request correlation/diagnostics only. Extended INF
 * from the same pinned generation owns lineage and native stage admission. */
static void udb_anti_entropy_record_ack(Client *peer, unsigned long id, char block, uint64_t count, const char *digest,
										uint64_t watermark)
{
	unsigned int mask = udb_block_letter_to_mask(block);
	if (!udb_anti_entropy.pending || udb_anti_entropy.round_id != id || !mask ||
		!udb_is_authorized_sync_source(udb_ctx, peer) || (udb_anti_entropy.acked_blocks & mask))
		return;
	if (udb_anti_entropy.watermark_known && udb_anti_entropy.watermark_seq != watermark)
	{
		udb_anti_entropy_handle_err(peer, id, UDB_ERR_PARAMS);
		udb_reconcile_abort(udb_ctx, "inconsistent anti-entropy watermark", 1);
		return;
	}
	udb_anti_entropy.watermark_seq = watermark;
	udb_anti_entropy.watermark_known = 1;
	udb_anti_entropy.acked_blocks |= mask;
	(void)count;
	(void)digest;
	if (udb_anti_entropy.acked_blocks == UDB_ALL_BLOCKS_MASK)
	{
		udb_anti_entropy.pending = 0;
		udb_anti_entropy_schedule_next(time(NULL));
	}
}

static void udb_anti_entropy_handle_err(Client *peer, unsigned long round_id, int errcode)
{
	if (udb_anti_entropy.pending && udb_anti_entropy.round_id == round_id)
	{
		time_t now = time(NULL);
		udb_anti_entropy.pending = 0;
		udb_anti_entropy.fail_count++;
		unsigned int shift = udb_anti_entropy.fail_count > 4 ? 4 : udb_anti_entropy.fail_count;
		time_t delay = (time_t)(UDB_ANTI_ENTROPY_MIN_GAP << shift);
		udb_anti_entropy.next_check_at = now + delay;
		(void)peer;
		(void)errcode;
	}
}

static void udb_anti_entropy_peer_quit(Client *client)
{
	(void)client;
	if (udb_anti_entropy.pending)
	{
		udb_anti_entropy.pending = 0;
		udb_anti_entropy.next_check_at = time(NULL) + UDB_ANTI_ENTROPY_MIN_GAP;
	}
}

EVENT(udb_sync_timeout_event)
{
	UdbBlock *block;
	UdbHelloPeer *peer, *next;
	time_t now = time(NULL);
	const char *abort_reason = NULL;

	for (block = udb_ctx ? udb_ctx->block_list : NULL; block; block = block->next)
	{
		if (block->session)
		{
			if (block->session->deadline <= now)
				abort_reason = "snapshot inactivity timeout";
			else if (block->session->absolute_deadline <= now)
				abort_reason = "snapshot absolute timeout";
		}
		if (block->pending_from && block->pending_deadline <= now)
		{
			udb_log(ULOG_WARNING, "UDB_PENDING_RES_TIMEOUT", block->pending_from,
					"Pending RES request for block $block timed out waiting for BEGIN",
					log_data_string("block", (char[]){block->letter, '\0'}));
			abort_reason = "RES timeout";
		}
	}
	if (udb_reconcile.active)
	{
		if (udb_reconcile.deadline <= now)
			abort_reason = "reconciliation inventory timeout";
		else if (udb_reconcile.absolute_deadline <= now)
			abort_reason = "reconciliation absolute timeout";
	}
	if (abort_reason)
		udb_reconcile_abort(udb_ctx, abort_reason, 1);
	udb_ocl_stage_timeout_check(now);
	udb_lines_expiry_sweep(now);
	for (peer = udb_hello_peers; peer; peer = next)
	{
		next = peer->next;
		if (peer->state == UDB_HEL_WAITING && peer->deadline <= now)
		{
			Client *c = peer->peer;
			peer->state = UDB_HEL_UNSUPPORTED;
			udb_log(ULOG_ERROR, "UDB_HEL_TIMEOUT", c,
					"No UDB HEL " UDB_PROTOCOL_HEL " acknowledgement from directly linked server; link aborted");
			if (c)
			{
				exit_client_fmt(c, NULL, "Link aborted: server does not support UDB protocol (HEL timeout)");
			}
		}
	}
	if (!udb_reconcile.active && udb_reconcile.next_retry_at && udb_reconcile.next_retry_at <= now)
	{
		UdbPropagatorSelection selected;
		udb_reconcile.next_retry_at = 0;
		if (udb_select_propagator(udb_ctx, 1, &selected) && !selected.is_local && selected.peer)
		{
			udb_sync_hello_refresh_all();
			udb_maybe_start_reconciliation(selected.peer, 1);
			/* Keep retrying if the HEL refresh or resulting inventory is lost. Any
			 * accepted INF starts the round and clears this deadline. */
			if (!udb_reconcile.active && udb_reconcile.retry_count < UDB_RECONCILE_RETRY_MAX)
			{
				udb_reconcile.retry_count++;
				unsigned int shift = udb_reconcile.retry_count > 4 ? 4 : udb_reconcile.retry_count;
				udb_reconcile.next_retry_at = now + (UDB_RECONCILE_RETRY_BASE << shift);
			}
		}
	}
	udb_propagator_availability_refresh();
	udb_sync_status_refresh();
	udb_anti_entropy_timer_check(now);
}

static void udb_sync_server_quit(Client *client)
{
	if (udb_ctx && udb_ctx->storage && client &&
		(udb_is_propagator(udb_ctx, client) || udb_reconcile.authority_peer == client))
	{
		uint64_t revision;
		udb_storage_engine_authority(udb_ctx->storage, &revision);
	}
	udb_lines_expiry_peer_disconnected(client);
	udb_anti_entropy_peer_quit(client);
	UdbBlock *block;
	UdbHelloPeer **peer;

	if (!udb_ctx || !client)
		return;
	for (peer = &udb_hello_peers; *peer; peer = &(*peer)->next)
		if ((*peer)->peer == client)
		{
			UdbHelloPeer *old = *peer;
			*peer = old->next;
			safe_free(old);
			break;
		}
	for (block = udb_ctx->block_list; block; block = block->next)
	{
		if (block->session && !strcmp(block->session->peer_sid, client->id))
			udb_sync_abort(block, "peer quit");
		else if (block->syncing_from == client)
			block->syncing_from = NULL;

		if (block->pending_from == client)
		{
			block->pending_from = NULL;
			block->pending_deadline = 0;
			block->pending_round_id = 0;
		}
	}

	if (udb_bootstrap_peer == client)
		udb_bootstrap_peer = NULL;
	if (udb_reconcile.authority_peer == client)
	{
		udb_log(ULOG_INFO, "UDB_AUTHORITY_FAILOVER", client,
				"Authority peer $client disconnected; resetting reconciliation round",
				log_data_client("client", client));
		udb_reconcile_abort(udb_ctx, "authority disconnected", 1);
	}
	udb_ocl_origin_quit(client);

	udb_propagator_policy_changed(udb_ctx);
}

/* End of udb_sync.c.inc */

/* Pinned native snapshot senders and typed wire values. */
/* Inlined: udb_native_effects.c.inc */
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

/* End of udb_native_effects.c.inc */
/* Inlined: udb_native_receive.c.inc */
/* Foreground receive ownership is independent of writer-owned disk stages.
 * Network frames enqueue copied canonical jobs; only durable completions may
 * acknowledge END or publish a complete six-block import. */

typedef struct UdbNativeReceiveRound
{
	struct UdbNativeReceiveRound *next;
	UdbStorageEngine *engine;
	UdbStorageStageSpec specs[6];
	uint64_t owner;
	uint64_t module_revision;
	uint64_t authority_revision;
	uint64_t promote_job;
	uint64_t abort_job;
	char peer_sid[4];
	char peer_epoch[17];
	unsigned long round;
	unsigned int inventory;
	unsigned int durable;
	int abandoned;
	int promoted;
} UdbNativeReceiveRound;

typedef struct UdbNativeReceiveScratch
{
	UdbNativeKey key;
	unsigned char value[UDB_STORAGE_VALUE_MAX + 3];
	unsigned char bytes[UDB_STORAGE_ROW_MAX];
} UdbNativeReceiveScratch;

static UdbNativeReceiveRound *udb_native_receive_rounds;

static uint64_t udb_native_epoch_number(const char epoch[17])
{
	uint64_t value = 0;
	for (unsigned int i = 0; i < 16; i++)
		value = (value << 4) | (uint64_t)(epoch[i] <= '9' ? epoch[i] - '0' : epoch[i] - 'a' + 10);
	return value;
}

static UdbNativeReceiveRound *udb_native_receive_owner(uint64_t owner)
{
	for (UdbNativeReceiveRound *round = udb_native_receive_rounds; round; round = round->next)
		if (round->owner == owner)
			return round;
	return NULL;
}

static int udb_native_receive_peer(UdbNativeReceiveRound *round, Client *peer)
{
	if (!round)
		return 0;
	udb_storage_mutex_lock(&round->engine->mutex);
	int revision_valid = round->module_revision == round->engine->module_revision &&
						 round->authority_revision == round->engine->authority_revision && !round->engine->failed &&
						 !round->engine->stopping;
	udb_storage_mutex_unlock(&round->engine->mutex);
	if (!revision_valid)
		return 0;
	UdbHelloPeer *hello = peer ? udb_hello_peer(peer, 0) : NULL;
	return round && !round->abandoned && peer && !IsDead(peer) && !IsDeadSocket(peer) && MyConnect(peer) &&
		   !strcmp(peer->id, round->peer_sid) && hello && hello->remote_epoch_known &&
		   !strcmp(hello->remote_epoch, round->peer_epoch) && udb_has_staged_sync(peer) &&
		   udb_is_authorized_sync_source(udb_ctx, peer) && udb_reconcile.active &&
		   udb_reconcile.authority_peer == peer && udb_reconcile.round_id == round->round;
}

static UdbNativeReceiveRound *udb_native_receive_find(Client *peer, unsigned long id)
{
	for (UdbNativeReceiveRound *round = udb_native_receive_rounds; round; round = round->next)
		if (round->round == id && udb_native_receive_peer(round, peer))
			return round;
	return NULL;
}

static void udb_native_receive_drop(UdbNativeReceiveRound *round)
{
	UdbNativeReceiveRound **link = &udb_native_receive_rounds;
	while (*link && *link != round)
		link = &(*link)->next;
	if (!*link)
		abort();
	*link = round->next;
	udb_storage_free(round);
}

static UdbStorageResult udb_native_receive_submit(UdbNativeReceiveRound *round, uint32_t kind, const void *bytes,
												  size_t length, uint64_t *id)
{
	return udb_storage_engine_submit(round->engine, kind, round->owner, round->module_revision,
									 round->authority_revision, bytes, length, id);
}

/* An abort may encounter a full copied queue. Retain its small charged owner
 * and retry admission once per event; never leak a durable pending-root owner. */
static void udb_native_receive_abort(unsigned long id)
{
	udb_native_mutation_abort(id);
	for (UdbNativeReceiveRound *round = udb_native_receive_rounds; round; round = round->next)
		if (!round->abandoned && (!id || round->round == id))
		{
			round->abandoned = 1;
			udb_storage_engine_cancel_owner(round->engine, round->owner);
		}
}

static void udb_native_receive_cleanup(void)
{
	while (udb_native_receive_rounds)
	{
		udb_storage_engine_cancel_owner(udb_native_receive_rounds->engine, udb_native_receive_rounds->owner);
		udb_native_receive_drop(udb_native_receive_rounds);
	}
}

/* Strict INF admission captures origin before any block comparison. Duplicate
 * inventory cannot replace a spec whose BEGIN/PUT jobs are already queued. */
static int udb_native_receive_inf(Client *peer, unsigned long id, char block, const char *digest, uint64_t count,
								  const char *sid, const char *epoch, uint64_t watermark)
{
	int index = udb_storage_stage_block_index((unsigned char)block);
	UdbStorageGeneration *pin = NULL;
	UdbNativeReceiveRound *round;
	char normalized[65];
	if (!udb_ctx || !udb_ctx->storage || index < 0 || !id || !peer || !udb_has_staged_sync(peer) ||
		!udb_is_authorized_sync_source(udb_ctx, peer) || !sid || strlen(sid) != 3 || !udb_storage_sid_valid(sid) ||
		!udb_hello_epoch_valid(epoch) || !udb_digest_parse(digest, normalized))
		return UDB_ERR_PARAMS;
	if (udb_reconcile.active && udb_reconcile.authority_peer == peer && id < udb_reconcile.round_id)
		return UDB_ERR_NO_SYNC;
	udb_reconcile_start(peer, id);
	if (!udb_reconcile_accept_source(peer, id, sid, epoch, watermark) ||
		!udb_native_mutation_bind(peer, id, sid, epoch, watermark))
		return UDB_ERR_PARAMS;
	round = udb_native_receive_find(peer, id);
	if (!round)
	{
		unsigned int owners = 0;
		for (UdbNativeReceiveRound *entry = udb_native_receive_rounds; entry; entry = entry->next)
			owners++;
		UdbHelloPeer *hello = udb_hello_peer(peer, 0);
		if (owners >= 8 || udb_native_owner_sequence == UINT64_MAX || !hello || !hello->remote_epoch_known)
			return UDB_ERR_FATAL;
		round = udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*round));
		if (!round)
			return UDB_ERR_FATAL;
		round->engine = udb_ctx->storage;
		round->owner = udb_native_owner_next();
		round->round = id;
		memcpy(round->peer_sid, peer->id, 4);
		memcpy(round->peer_epoch, hello->remote_epoch, 17);
		udb_storage_mutex_lock(&round->engine->mutex);
		round->module_revision = round->engine->module_revision;
		round->authority_revision = round->engine->authority_revision;
		udb_storage_mutex_unlock(&round->engine->mutex);
		round->next = udb_native_receive_rounds;
		udb_native_receive_rounds = round;
	}
	unsigned int mask = 1U << index;
	if (round->inventory & mask)
		return UDB_ERR_PARAMS;
	UdbStorageStageSpec *spec = round->specs + index;
	spec->head.owner = round->owner;
	spec->head.round = id;
	spec->head.source_sequence = watermark;
	spec->head.source_epoch = udb_native_epoch_number(epoch);
	memcpy(spec->head.source_sid, sid, 4);
	spec->block = (unsigned char)block;
	spec->count = count;
	spec->rows_limit = udb_cfg ? udb_cfg->max_staged_records : UDB_DEFAULT_MAX_STAGED_RECORDS;
	spec->physical_limit = udb_cfg ? udb_cfg->max_staged_bytes : UDB_DEFAULT_MAX_STAGED_BYTES;
	if (count > spec->rows_limit)
		return UDB_ERR_PARAMS;
	for (unsigned int i = 0; i < 32; i++)
	{
		int hi = normalized[2 * i] <= '9' ? normalized[2 * i] - '0' : normalized[2 * i] - 'a' + 10;
		int lo = normalized[2 * i + 1] <= '9' ? normalized[2 * i + 1] - '0' : normalized[2 * i + 1] - 'a' + 10;
		spec->digest[i] = (unsigned char)((hi << 4) | lo);
	}
	if (udb_storage_engine_pin(round->engine, &pin) != UDB_STORAGE_OK)
		return UDB_ERR_FATAL;
	int equal = pin->state.blocks[index].summary.count == count &&
				!memcmp(pin->state.blocks[index].summary.digest, spec->digest, 32);
	if (udb_storage_engine_release(round->engine, &pin) != UDB_STORAGE_OK)
		return UDB_ERR_FATAL;
	round->inventory |= mask;
	udb_reconcile.compared_blocks |= mask;
	udb_reconcile.last_activity = time(NULL);
	udb_reconcile.deadline =
		udb_reconcile.last_activity + (udb_cfg ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT);
	if (equal)
	{
		uint64_t job;
		if (udb_native_receive_submit(round, UDB_STORAGE_STAGE_REUSE, spec, sizeof(*spec), &job) != UDB_STORAGE_OK)
			return UDB_ERR_FATAL;
	}
	else
	{
		UdbBlock *descriptor = udb_block_by_letter(udb_ctx, block);
		if (!descriptor || !udb_send_db_to_one(peer, ":%s DB %s RES %lu %c", me.id, peer->id, id, block))
			return UDB_ERR_FATAL;
		udb_reconcile_record_res(peer, id, block);
		descriptor->pending_from = peer;
		descriptor->pending_round_id = id;
		descriptor->pending_deadline = udb_reconcile.deadline;
	}
	return 0;
}

static int udb_sync_begin(UdbBlock *block, Client *peer, unsigned long id, const char *txid, const char *digest,
						  uint64_t watermark)
{
	UdbNativeReceiveRound *round = udb_native_receive_find(peer, id);
	int index = block ? udb_storage_stage_block_index((unsigned char)block->letter) : -1;
	char expected[65], normalized[65];
	if (!round || index < 0 || !txid || !*txid || strlen(txid) > UDB_TXID_MAX || block->session ||
		block->pending_from != peer || block->pending_round_id != id || !(round->inventory & (1U << index)))
		return UDB_ERR_NO_SYNC;
	for (const unsigned char *p = (const unsigned char *)txid; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' ||
			  *p == '_'))
			return UDB_ERR_PARAMS;
	udb_native_digest_hex(round->specs[index].digest, expected);
	if (watermark != round->specs[index].head.source_sequence || !udb_digest_parse(digest, normalized) ||
		strcmp(normalized, expected))
		return UDB_ERR_PARAMS;
	UdbSyncSession *session = udb_storage_alloc(&round->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*session));
	if (!session)
		return UDB_ERR_FATAL;
	uint64_t job;
	if (udb_native_receive_submit(round, UDB_STORAGE_STAGE_BEGIN, round->specs + index, sizeof(round->specs[index]),
								  &job) != UDB_STORAGE_OK)
	{
		udb_storage_free(session);
		return UDB_ERR_FATAL;
	}
	session->owner = round->owner;
	memcpy(session->peer_sid, round->peer_sid, 4);
	session->round_id = id;
	memcpy(session->txid, txid, strlen(txid) + 1);
	session->started_at = session->last_activity = time(NULL);
	session->deadline =
		session->started_at + (udb_cfg ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT);
	session->absolute_deadline =
		session->started_at + (udb_cfg ? udb_cfg->sync_absolute_timeout : UDB_SYNC_ABSOLUTE_TIMEOUT);
	session->watermark_seq = watermark;
	block->session = session;
	block->syncing_from = peer;
	udb_block_clear_pending(block);
	return 0;
}

static int udb_native_receive_session(UdbBlock *block, Client *peer, unsigned long id, const char *txid,
									  UdbNativeReceiveRound **out)
{
	UdbSyncSession *session = block ? block->session : NULL;
	*out = udb_native_receive_find(peer, id);
	time_t now = time(NULL);
	return *out && session && session->owner == (*out)->owner && !session->end_job && session->round_id == id && txid &&
		   !strcmp(session->txid, txid) && !strcmp(session->peer_sid, peer->id) && now < session->deadline &&
		   now < session->absolute_deadline;
}

static int udb_sync_put(UdbBlock *block, Client *peer, unsigned long id, const char *txid, const char *path,
						const char *value)
{
	UdbNativeReceiveRound *round;
	if (!udb_native_receive_session(block, peer, id, txid, &round))
		return UDB_ERR_NO_SYNC;
	int index = udb_storage_stage_block_index((unsigned char)block->letter);
	if (block->session->received_puts >= round->specs[index].count)
		return UDB_ERR_PARAMS;
	UdbNativeReceiveScratch *scratch =
		udb_storage_alloc(&round->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return UDB_ERR_FATAL;
	UdbStorageResult result = udb_native_key_parse(path, &scratch->key);
	if (result == UDB_STORAGE_OK && scratch->key.row.block != (unsigned char)block->letter)
		result = UDB_STORAGE_REJECTED;
	if (result == UDB_STORAGE_OK)
		result = udb_native_value_decode(value, &scratch->key.row, scratch->value, sizeof(scratch->value));
	size_t length = 0;
	if (result == UDB_STORAGE_OK)
		result = udb_storage_row_encode(&scratch->key.row, scratch->bytes, sizeof(scratch->bytes), &length);
	uint64_t job;
	if (result == UDB_STORAGE_OK)
		result = udb_native_receive_submit(round, UDB_STORAGE_STAGE_PUT, scratch->bytes, length, &job);
	udb_storage_free(scratch);
	if (result != UDB_STORAGE_OK)
		return result == UDB_STORAGE_REJECTED ? UDB_ERR_PARAMS : UDB_ERR_FATAL;
	block->session->received_puts++;
	block->session->last_activity = time(NULL);
	block->session->deadline =
		block->session->last_activity + (udb_cfg ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT);
	return 0;
}

static int udb_sync_end(UdbContext *ctx, UdbBlock *block, Client *peer, unsigned long id, const char *txid,
						const char *digest, uint64_t watermark)
{
	UdbNativeReceiveRound *round;
	char expected[65], normalized[65];
	if (!ctx || !udb_native_receive_session(block, peer, id, txid, &round))
		return UDB_ERR_NO_SYNC;
	int index = udb_storage_stage_block_index((unsigned char)block->letter);
	udb_native_digest_hex(round->specs[index].digest, expected);
	if (!udb_digest_parse(digest, normalized) || strcmp(expected, normalized) ||
		watermark != round->specs[index].head.source_sequence ||
		block->session->received_puts != round->specs[index].count)
		return UDB_ERR_PARAMS;
	unsigned char letter = (unsigned char)block->letter;
	if (udb_native_receive_submit(round, UDB_STORAGE_STAGE_END, &letter, 1, &block->session->end_job) != UDB_STORAGE_OK)
		return UDB_ERR_FATAL;
	return 0;
}

static int udb_native_receive_promote(UdbContext *ctx)
{
	UdbNativeReceiveRound *round =
		ctx ? udb_native_receive_find(udb_reconcile.authority_peer, udb_reconcile.round_id) : NULL;
	if (!round || round->inventory != 63 || round->durable != 63 || round->promote_job || !udb_reconcile.source_known ||
		!udb_reconcile.watermark_known || udb_has_active_sessions(ctx) || udb_has_pending_requests(ctx))
		return 0;
	if (udb_native_receive_submit(round, UDB_STORAGE_STAGE_PROMOTE, NULL, 0, &round->promote_job) != UDB_STORAGE_OK)
		udb_reconcile_abort(ctx, "native promotion admission failed", 1);
	return 0; /* READY belongs to the durable completion, never to admission. */
}

static void udb_native_receive_complete(UdbStorageJob *job)
{
	UdbNativeReceiveRound *round = udb_native_receive_owner(job->owner);
	if (!round || round->engine != job->engine)
		return;
	Client *peer = udb_ocl_find_server(round->peer_sid);
	if (job->kind == UDB_STORAGE_STAGE_ABORT)
	{
		if (round->abandoned && round->abort_job == job->id)
		{
			if (job->result == UDB_STORAGE_OK || job->result == UDB_STORAGE_STAGED)
				udb_native_receive_drop(round);
			else
				round->abort_job = 0;
		}
		return;
	}
	/* Cancellation after the writer's final barrier cannot undo durability.
	 * Apply this receipt in order even if its network round has since vanished;
	 * only a still-authorized matching round may restore readiness. */
	if (job->kind == UDB_STORAGE_STAGE_PROMOTE && job->result == UDB_STORAGE_OK && job->committed_generation &&
		job->after && round->promote_job == job->id)
	{
		int current = udb_native_receive_peer(round, peer);
		UdbStorageResult effects = udb_native_receive_publication(job, round->round, current ? peer : NULL);
		if (effects != UDB_STORAGE_OK || !current)
		{
			udb_ready = 0;
			udb_sync_status = UDB_SYNC_DEGRADED;
			if (udb_reconcile.active && udb_reconcile.round_id == round->round)
				udb_reconcile_abort(udb_ctx, "native promotion effects or authority unavailable", 1);
			else
				round->abandoned = 1;
		}
		else
		{
			round->promoted = 1;
			udb_native_receive_replay_pump();
		}
		return;
	}
	if (round->abandoned || !udb_native_receive_peer(round, peer))
		return;
	if (job->result != UDB_STORAGE_OK && job->result != UDB_STORAGE_STAGED)
	{
		udb_log(ULOG_WARNING, "UDB_NATIVE_STAGE_FAILED", peer,
				"Native stage job $kind failed with storage result $result", log_data_integer("kind", job->kind),
				log_data_integer("result", job->result));
		udb_reconcile_abort(udb_ctx, "native stage preparation or persistence failed", 1);
		return;
	}
	if (job->kind == UDB_STORAGE_STAGE_BEGIN || job->kind == UDB_STORAGE_STAGE_PUT)
		return;
	if (job->result != UDB_STORAGE_OK || !job->committed_generation || !job->after)
	{
		udb_reconcile_abort(udb_ctx, "missing native durable stage receipt", 1);
		return;
	}
	if (job->kind == UDB_STORAGE_STAGE_END || job->kind == UDB_STORAGE_STAGE_REUSE)
	{
		unsigned char block;
		if (job->kind == UDB_STORAGE_STAGE_END)
			block = job->payload[0];
		else
		{
			UdbStorageStageSpec spec;
			memcpy(&spec, job->payload, sizeof(spec));
			block = spec.block;
		}
		int index = udb_storage_stage_block_index(block);
		if (index < 0 || (round->durable & (1U << index)))
		{
			udb_reconcile_abort(udb_ctx, "duplicate native stage completion", 1);
			return;
		}
		if (job->kind == UDB_STORAGE_STAGE_END)
		{
			UdbBlock *descriptor = udb_block_by_letter(udb_ctx, (char)block);
			UdbSyncSession *session = descriptor ? descriptor->session : NULL;
			char digest[65];
			udb_native_digest_hex(round->specs[index].digest, digest);
			if (!session || session->owner != round->owner || session->end_job != job->id ||
				!udb_send_db_to_one(peer, ":%s DB %s ACK %lu %c %s %s %" PRIu64, me.id, peer->id, round->round, block,
									session->txid, digest, session->watermark_seq))
			{
				udb_reconcile_abort(udb_ctx, "native END acknowledgement failed", 1);
				return;
			}
			udb_sync_session_free(descriptor);
		}
		round->durable |= 1U << index;
		udb_reconcile_record_end(peer, (char)block, round->round);
		udb_reconcile_check(udb_ctx);
	}
}

/* READY is delayed until every accepted post-watermark mutation has its own
 * durable receipt and ordered effects. New traffic joins this bounded tail. */
static void udb_native_receive_replay_pump(void)
{
	if (!udb_reconcile.active)
		return;
	UdbNativeReceiveRound *round = udb_native_receive_find(udb_reconcile.authority_peer, udb_reconcile.round_id);
	if (!round || !round->promoted)
		return;
	if (!udb_native_mutation_replay(round->round))
	{
		udb_reconcile_abort(udb_ctx, "native replay admission failed", 1);
		return;
	}
	if (udb_native_mutation_pending(round->round))
		return;
	unsigned long id = round->round;
	udb_reconcile_reset();
	udb_reconcile.retry_count = 0;
	udb_reconcile.next_retry_at = 0;
	udb_native_receive_drop(round);
	UdbPropagatorSelection selected;
	int local =
		!udb_propagator_policy_present(udb_ctx) || (udb_select_propagator(udb_ctx, 1, &selected) && selected.is_local);
	if (local && strcmp(udb_ctx->authority_sid, me.id))
	{
		/* Bootstrap does not make a relay's root epoch this node's own epoch.
		 * Becoming standalone is a separate durable origin bind. */
		udb_ready = 0;
		udb_native_local_bind(udb_ctx);
		return;
	}
	(void)id;
	udb_ready = 1;
	udb_last_successful_sync = time(NULL);
	udb_sync_status = UDB_SYNC_OK;
	udb_degraded_since = 0;
	udb_bootstrap_peer = NULL;
	udb_offer_reconciliation_to_downstreams();
}

EVENT(udb_native_receive_tick_event)
{
	if (!udb_ctx || !udb_ctx->storage)
		return;
	UdbStorageEngine *engine = udb_ctx->storage;
	/* Completion work is bounded even when a large PUT burst has accumulated. */
	for (unsigned int i = 0; i < 1024; i++)
	{
		UdbStorageJob *job = NULL;
		if (udb_storage_engine_poll(engine, &job) != UDB_STORAGE_OK)
			break;
		if (job->kind >= UDB_STORAGE_STAGE_BEGIN)
			udb_native_receive_complete(job);
		else
			udb_native_mutation_complete(job);
		if (udb_storage_engine_job_free(engine, &job) != UDB_STORAGE_OK)
			abort();
	}
	udb_storage_mutex_lock(&engine->mutex);
	int failed = engine->failed || engine->stopping || !engine->admission;
	udb_storage_mutex_unlock(&engine->mutex);
	if (failed)
	{
		udb_ready = 0;
		udb_sync_status = UDB_SYNC_DEGRADED;
		udb_native_mutation_abort(0);
		if (udb_reconcile.active)
			udb_reconcile_abort(udb_ctx, "native writer unavailable", 1);
	}
	else if (udb_ctx->selection_kind == 1 && !udb_reconcile.active && !udb_ready && !udb_native_deliveries_present())
		udb_native_local_bind(udb_ctx);
	udb_native_receive_replay_pump();
	for (UdbNativeReceiveRound *round = udb_native_receive_rounds, *next; round; round = next)
	{
		next = round->next;
		if (!round->abandoned || round->abort_job)
			continue;
		udb_storage_mutex_lock(&engine->mutex);
		round->module_revision = engine->module_revision;
		round->authority_revision = engine->authority_revision;
		int unavailable = engine->failed || engine->stopping || !engine->admission;
		udb_storage_mutex_unlock(&engine->mutex);
		if (unavailable)
			udb_native_receive_drop(round); /* recovery validates and discards pending roots */
		else
			udb_native_receive_submit(round, UDB_STORAGE_STAGE_ABORT, NULL, 0, &round->abort_job);
	}
}

/* End of udb_native_receive.c.inc */
/* Inlined: udb_native_sync.c.inc */
/* Foreground snapshot senders pin disk generations, not resident record lists.
 * Each peer/round is bound to its negotiated instance and expires finitely. */

#define UDB_NATIVE_SEND_ROWS 1024
#define UDB_NATIVE_SEND_BYTES ((size_t)1024 * 1024)

typedef struct UdbNativeSendRound
{
	struct UdbNativeSendRound *next;
	UdbStorageEngine *engine;
	UdbStorageGeneration *generation;
	UdbStorageBlockIterator *iterator;
	char peer_sid[4];
	char peer_epoch[UDB_OCL_EPOCH_LEN + 1];
	char txid[UDB_NUM_BLOCKS][UDB_TXID_MAX + 1];
	unsigned long round;
	uint64_t started;
	uint64_t activity;
	uint64_t emitted;
	unsigned int requested;
	unsigned int sent;
	unsigned int acknowledged;
	int active;
} UdbNativeSendRound;

typedef struct UdbNativeSendScratch
{
	char path[UDB_STORAGE_PATH_MAX * 3 + 1];
	char value[4 * ((UDB_STORAGE_VALUE_MAX + 2) / 3) + 3];
} UdbNativeSendScratch;

static UdbNativeSendRound *udb_native_send_rounds;
static unsigned long udb_native_send_sequence;

static void udb_native_digest_hex(const unsigned char digest[32], char out[65])
{
	static const char hex[] = "0123456789abcdef";
	for (unsigned int i = 0; i < 32; i++)
	{
		out[2 * i] = hex[digest[i] >> 4];
		out[2 * i + 1] = hex[digest[i] & 15];
	}
	out[64] = 0;
}

static void udb_native_send_drop(UdbNativeSendRound **link)
{
	UdbNativeSendRound *round = *link;
	if (round->iterator && udb_storage_block_iterator_close(&round->iterator) != UDB_STORAGE_OK)
		abort();
	if (udb_storage_engine_release(round->engine, &round->generation) != UDB_STORAGE_OK)
		abort();
	*link = round->next;
	udb_storage_free(round);
}

static int udb_native_send_peer(UdbNativeSendRound *round, Client *server)
{
	UdbHelloPeer *peer = server ? udb_hello_peer(server, 0) : NULL;
	return server && !IsDead(server) && !IsDeadSocket(server) && MyConnect(server) && udb_ready &&
		   udb_peer_authorizes_us(server) && !strcmp(round->peer_sid, server->id) && peer && peer->remote_epoch_known &&
		   !strcmp(round->peer_epoch, peer->remote_epoch);
}

static UdbNativeSendRound *udb_native_send_find(Client *server, unsigned long round)
{
	for (UdbNativeSendRound *entry = udb_native_send_rounds; entry; entry = entry->next)
		if (entry->round == round && udb_native_send_peer(entry, server))
			return entry;
	return NULL;
}

static void udb_native_sync_cleanup(Client *server)
{
	UdbNativeSendRound **link = &udb_native_send_rounds;
	while (*link)
		if (!server || !strcmp((*link)->peer_sid, server->id))
			udb_native_send_drop(link);
		else
			link = &(*link)->next;
}

static int udb_native_sync_offer_round(Client *server, unsigned long requested, int manifest)
{
	UdbNativeSendRound *round;
	UdbStorageGeneration *generation = NULL;
	UdbHelloPeer *peer = server ? udb_hello_peer(server, 0) : NULL;
	if (!udb_ctx || !udb_ctx->storage || !udb_ready || !udb_peer_authorizes_us(server) || !peer ||
		!peer->remote_epoch_known || !udb_storage_sid_valid(server->id) ||
		(!requested && udb_native_send_sequence == ULONG_MAX))
		return 0;
	if (udb_storage_engine_pin(udb_ctx->storage, &generation) != UDB_STORAGE_OK)
		return 0;
	if (!udb_storage_sid_valid(generation->state.source_sid))
	{
		udb_storage_engine_release(udb_ctx->storage, &generation);
		return 0;
	}
	round = udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*round));
	if (!round)
	{
		udb_storage_engine_release(udb_ctx->storage, &generation);
		return 0;
	}
	round->engine = udb_ctx->storage;
	round->generation = generation;
	if (!udb_native_send_sequence)
	{
		time_t seed = time(NULL);
		if (seed > 0 && (uint64_t)seed < ULONG_MAX)
			udb_native_send_sequence = (unsigned long)seed;
	}
	round->round = requested ? requested : ++udb_native_send_sequence;
	round->started = round->activity = udb_storage_monotonic_ms();
	if (round->started == UINT64_MAX)
	{
		udb_storage_engine_release(round->engine, &round->generation);
		udb_storage_free(round);
		return 0;
	}
	round->active = -1;
	memcpy(round->peer_sid, server->id, 4);
	memcpy(round->peer_epoch, peer->remote_epoch, sizeof(round->peer_epoch));
	udb_native_sync_cleanup(server);
	round->next = udb_native_send_rounds;
	udb_native_send_rounds = round;
	char epoch[17];
	snprintf(epoch, sizeof(epoch), "%016" PRIx64, generation->state.source_epoch);
	for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		const UdbStorageBlockRoot *root = generation->state.blocks + i;
		char digest[65];
		udb_native_digest_hex(root->summary.digest, digest);
		if (manifest && !udb_send_db_to_one(server, ":%s DB %s MANIFEST ACK %lu %c %" PRIu64 " %s %" PRIu64, me.id,
											server->id, round->round, root->block, root->summary.count, digest,
											generation->state.source_sequence))
		{
			udb_native_sync_cleanup(server);
			return 0;
		}
		if (!udb_send_db_to_one(server, ":%s DB %s INF %lu %c %s %" PRIu64 " 0 %" PRIu64 " %s %s", me.id, server->id,
								round->round, root->block, digest, root->summary.count,
								generation->state.source_sequence, generation->state.source_sid, epoch))
		{
			udb_native_sync_cleanup(server);
			return 0;
		}
	}
	return 1;
}

static int udb_native_sync_offer(Client *server)
{
	return udb_native_sync_offer_round(server, 0, 0);
}

static int udb_native_sync_send(Client *server, char block, unsigned long round_id)
{
	UdbNativeSendRound *round = udb_native_send_find(server, round_id);
	int index = udb_storage_stage_block_index((unsigned char)block);
	if (!round || index < 0 || (round->requested & (1U << index)))
		return 0;
	round->requested |= 1U << index;
	round->activity = udb_storage_monotonic_ms();
	return 1;
}

static int udb_native_sync_ack(Client *server, unsigned long round_id, char block, const char *txid, const char *digest,
							   uint64_t watermark)
{
	UdbNativeSendRound *round = udb_native_send_find(server, round_id);
	int index = udb_storage_stage_block_index((unsigned char)block);
	char expected[65];
	if (!round || index < 0 || !txid || !digest || !(round->sent & (1U << index)) ||
		(round->acknowledged & (1U << index)) || strcmp(round->txid[index], txid) ||
		watermark != round->generation->state.source_sequence)
		return 0;
	udb_native_digest_hex(round->generation->state.blocks[index].summary.digest, expected);
	if (strcmp(expected, digest))
		return 0;
	round->acknowledged |= 1U << index;
	round->activity = udb_storage_monotonic_ms();
	return 1;
}

/* A bounded turn never materializes a block or fills an unbounded SendQ. The
 * iterator owns the last returned row until the next call; no row copy list. */
static int udb_native_send_step(UdbNativeSendRound *round, Client *server, UdbNativeSendScratch *scratch,
								unsigned int *rows, size_t *bytes, unsigned int row_ceiling, size_t byte_ceiling)
{
	UdbStorageResult result;
	UdbStorageRecord record;
	if (round->active < 0)
	{
		/* END only queues the receiver's durable rebuild. Keep its single
		 * writer free of the next block until the validated ACK grants credit. */
		if (round->sent & ~round->acknowledged)
			return 1;
		for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
			if ((round->requested & ~round->sent) & (1U << i))
			{
				round->active = (int)i;
				break;
			}
		if (round->active < 0)
			return 1;
		int index = round->active;
		const UdbStorageBlockRoot *root = round->generation->state.blocks + index;
		if (udb_storage_block_iterator_create(round->engine->cache, round->engine->files + index, root, NULL,
											  &round->iterator) != UDB_STORAGE_OK)
			return 0;
		char digest[65];
		udb_native_digest_hex(root->summary.digest, digest);
		snprintf(round->txid[index], sizeof(round->txid[index]), "%lx-%c", round->round, root->block);
		if (!udb_send_db_to_one(server, ":%s DB %s BEGIN %lu %c %s %s %" PRIu64, me.id, server->id, round->round,
								root->block, round->txid[index], digest, round->generation->state.source_sequence))
			return 0;
		round->emitted = 0;
	}
	while (*rows < row_ceiling && *bytes <= byte_ceiling && byte_ceiling - *bytes >= UDB_S2S_LINE_MAX)
	{
		if (!udb_native_send_peer(round, server))
			return 0;
		size_t queued = DBufLength(&server->local->sendQ);
		size_t maximum = get_sendq(server);
		/* Reserve one maximum-sized row before advancing the iterator. */
		if (queued >= maximum || maximum - queued < UDB_S2S_LINE_MAX)
			return 1;
		result = udb_storage_block_iterator_next(round->iterator, &record);
		const UdbStorageBlockRoot *root = round->generation->state.blocks + round->active;
		if (result == UDB_STORAGE_NOT_FOUND)
		{
			char digest[65];
			udb_native_digest_hex(root->summary.digest, digest);
			if (round->emitted != root->summary.count ||
				udb_storage_block_iterator_close(&round->iterator) != UDB_STORAGE_OK ||
				!udb_send_db_to_one(server, ":%s DB %s END %lu %c %s %s %" PRIu64, me.id, server->id, round->round,
									root->block, round->txid[round->active], digest,
									round->generation->state.source_sequence))
				return 0;
			round->sent |= 1U << round->active;
			round->active = -1;
			round->activity = udb_storage_monotonic_ms();
			return 1;
		}
		if (result != UDB_STORAGE_OK ||
			udb_native_key_path(&record.row, scratch->path, sizeof(scratch->path)) != UDB_STORAGE_OK ||
			udb_native_value_encode(&record.row, scratch->value, sizeof(scratch->value)) != UDB_STORAGE_OK)
			return 0;
		size_t size = strlen(scratch->path) + strlen(scratch->value) + UDB_S2S_OVERHEAD_MAX;
		if (size > UDB_S2S_LINE_MAX || round->emitted == UINT64_MAX ||
			!udb_send_db_to_one(server, ":%s DB %s PUT %lu %c %s %s :%s", me.id, server->id, round->round, root->block,
								round->txid[round->active], scratch->path, scratch->value))
			return 0;
		round->emitted++;
		(*rows)++;
		*bytes += size;
		round->activity = udb_storage_monotonic_ms();
	}
	return 1;
}

static void udb_native_sync_pump(void)
{
	if (!udb_ctx || !udb_ctx->storage || !udb_native_send_rounds)
		return;
	uint64_t now = udb_storage_monotonic_ms();
	uint64_t inactivity = (uint64_t)(udb_cfg ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT) * 1000;
	uint64_t absolute = (uint64_t)(udb_cfg ? udb_cfg->sync_absolute_timeout : UDB_SYNC_ABSOLUTE_TIMEOUT) * 1000;
	UdbNativeSendRound **link = &udb_native_send_rounds;
	size_t remaining = 0;
	/* Expiry needs no allocation; memory pressure must not retain stale pins. */
	while (*link)
	{
		UdbNativeSendRound *round = *link;
		Client *server = udb_ocl_find_server(round->peer_sid);
		if (!udb_native_send_peer(round, server) || now == UINT64_MAX || now < round->started ||
			now - round->started >= absolute || now < round->activity || now - round->activity >= inactivity)
			udb_native_send_drop(link);
		else
		{
			remaining++;
			link = &round->next;
		}
	}
	if (!remaining)
		return;
	UdbNativeSendScratch *scratch =
		udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return;
	unsigned int rows = 0;
	size_t bytes = 0;
	/* Rotate served entries to the tail. At most the initial inventory is
	 * visited, so a slow/large first peer cannot starve another round. */
	UdbNativeSendRound **tail = &udb_native_send_rounds;
	while (*tail)
		tail = &(*tail)->next;
	while (remaining-- && rows < UDB_NATIVE_SEND_ROWS && UDB_NATIVE_SEND_BYTES - bytes >= UDB_S2S_LINE_MAX)
	{
		UdbNativeSendRound *round = udb_native_send_rounds;
		Client *server = udb_ocl_find_server(round->peer_sid);
		unsigned int row_ceiling = rows + 128;
		size_t byte_ceiling = bytes + 128 * 1024;
		if (row_ceiling > UDB_NATIVE_SEND_ROWS)
			row_ceiling = UDB_NATIVE_SEND_ROWS;
		if (byte_ceiling > UDB_NATIVE_SEND_BYTES)
			byte_ceiling = UDB_NATIVE_SEND_BYTES;
		if (!udb_native_send_step(round, server, scratch, &rows, &bytes, row_ceiling, byte_ceiling))
		{
			if (server && round->active >= 0)
				udb_send_db_to_one(server, ":%s DB %s ERR PUT %d %lu %c", me.id, server->id, UDB_ERR_FATAL,
								   round->round, round->generation->state.blocks[round->active].block);
			if (!round->next)
				tail = &udb_native_send_rounds;
			udb_native_send_drop(&udb_native_send_rounds);
		}
		else if (round->next)
		{
			udb_native_send_rounds = round->next;
			round->next = NULL;
			*tail = round;
			tail = &round->next;
		}
	}
	udb_storage_free(scratch);
}

EVENT(udb_native_sync_tick_event)
{
	udb_native_sync_pump();
}

/* End of udb_native_sync.c.inc */
/* Inlined: udb_native_wire.c.inc */
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

/* End of udb_native_wire.c.inc */

/* Operclass registry: local inventory, OCL propagation and OCLG view */
/* Inlined: udb_operclasses.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Operclass Inventory (OCL) & Global Operclass View (OCLG)
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

/* OCL is the distributed source of truth about the operclasses each
 * participant IRCd currently has loaded.  OCLG is a derived projection (the
 * intersection of every participant inventory with matching effective
 * digests) published to explicit consumer subscribers such as Services.
 * Neither is ever persisted to the udb_*.db blocks.
 */

/* UnrealIRCd internals compatibility layer
 *
 * conf.c owns the active operclass list and does not export it through h.h.
 * Every direct reference to these internals is confined to this section so an
 * upstream layout change has a single place to adapt. */
extern MODVAR ConfigItem_operclass *conf_operclass;

static ConfigItem_operclass *udb_compat_operclass_head(void)
{
	return conf_operclass;
}

static ConfigItem_operclass *udb_compat_operclass_next(const ConfigItem_operclass *oc)
{
	return oc ? oc->next : NULL;
}

static OperClass *udb_compat_operclass_struct(const ConfigItem_operclass *oc)
{
	return oc ? oc->classStruct : NULL;
}

static const char *udb_compat_operclass_name(const ConfigItem_operclass *oc)
{
	OperClass *contract = udb_compat_operclass_struct(oc);

	return contract && contract->name && *contract->name ? contract->name : NULL;
}

static const char *udb_compat_operclass_parent_name(const ConfigItem_operclass *oc)
{
	OperClass *contract = udb_compat_operclass_struct(oc);

	return contract && contract->ISA && *contract->ISA ? contract->ISA : NULL;
}

static UdbOclOrigin *udb_ocl_origins = NULL;
static UdbOclInventory *udb_ocl_local = NULL;
static char udb_ocl_epoch[UDB_OCL_EPOCH_LEN + 1] = "";
static unsigned long udb_ocl_local_generation = 0;
static UdbOclInventory *udb_oclg_view = NULL;
static int udb_oclg_ready = 0;
static unsigned long udb_oclg_generation = 0;

static void udb_ocl_ensure_epoch(void);

static const char *udb_ocl_epoch_value(void)
{
	udb_ocl_ensure_epoch();
	return udb_ocl_epoch;
}
static void udb_oclg_recompute(void);
static void udb_oclg_push_subscribers(void);
static void udb_ocl_stage_discard(UdbOclOrigin *origin);

static void udb_ocl_free_inventory(UdbOclInventory *inv)
{
	if (!inv)
		return;
	safe_free(inv->entries);
	safe_free(inv);
}

static UdbOclOrigin *udb_ocl_find_origin(const char *sid)
{
	UdbOclOrigin *origin;

	for (origin = udb_ocl_origins; origin; origin = origin->next)
		if (!strcmp(origin->sid, sid))
			return origin;
	return NULL;
}

static UdbOclOrigin *udb_ocl_get_origin(const char *sid)
{
	UdbOclOrigin *origin = udb_ocl_find_origin(sid);

	if (origin)
		return origin;
	origin = safe_alloc(sizeof(*origin));
	strlcpy(origin->sid, sid, sizeof(origin->sid));
	origin->next = udb_ocl_origins;
	udb_ocl_origins = origin;
	return origin;
}

static int udb_ocl_member_exists(const char *sid)
{
	UdbOclOrigin *origin = udb_ocl_find_origin(sid);

	return origin && origin->member;
}

static int udb_ocl_member_add(const char *sid)
{
	UdbOclOrigin *origin;

	if (!sid || !*sid || !strcmp(sid, me.id))
		return 0;
	origin = udb_ocl_get_origin(sid);
	if (origin->member)
		return 0;
	origin->member = 1;
	return 1;
}

static int udb_ocl_member_remove(const char *sid)
{
	UdbOclOrigin *origin = udb_ocl_find_origin(sid);

	if (!origin || !origin->member)
		return 0;
	origin->member = 0;
	return 1;
}

static void udb_ocl_purge_nonmembers(void)
{
	UdbOclOrigin **link, *old;

	for (link = &udb_ocl_origins; *link;)
	{
		if ((*link)->member)
		{
			link = &(*link)->next;
			continue;
		}
		old = *link;
		*link = old->next;
		udb_ocl_free_inventory(old->current);
		udb_ocl_free_inventory(old->staging);
		safe_free(old);
	}
}

/* Once an epoch of an origin has been replaced or superseded, every later
 * frame from that epoch is stale for this receiver. */
static int udb_ocl_epoch_retired(UdbOclOrigin *origin, const char *epoch)
{
	unsigned int i;

	for (i = 0; i < origin->retired_count && i < UDB_OCL_MAX_RETIRED_EPOCHS; i++)
		if (!strcmp(origin->retired_epochs[i], epoch))
			return 1;
	return 0;
}

static void udb_ocl_retire_epoch(UdbOclOrigin *origin, const char *epoch)
{
	unsigned int i;

	if (!epoch || !*epoch || udb_ocl_epoch_retired(origin, epoch))
		return;
	if (origin->retired_count < UDB_OCL_MAX_RETIRED_EPOCHS)
	{
		strlcpy(origin->retired_epochs[origin->retired_count++], epoch, UDB_OCL_EPOCH_LEN + 1);
		return;
	}
	for (i = 0; i + 1 < UDB_OCL_MAX_RETIRED_EPOCHS; i++)
		memcpy(origin->retired_epochs[i], origin->retired_epochs[i + 1], UDB_OCL_EPOCH_LEN + 1);
	strlcpy(origin->retired_epochs[UDB_OCL_MAX_RETIRED_EPOCHS - 1], epoch, UDB_OCL_EPOCH_LEN + 1);
}

static Client *udb_ocl_find_server(const char *sid)
{
	Client *acptr;

	if (!sid || !*sid)
		return NULL;
	if (!strcmp(sid, me.id))
		return &me;
	list_for_each_entry(acptr, &global_server_list, client_node)
	{
		if (IsServer(acptr) && !strcmp(acptr->id, sid))
			return acptr;
	}
	return NULL;
}

static int udb_ocl_is_lower_hex(const char *s, size_t len)
{
	size_t i;

	if (!s || strlen(s) != len)
		return 0;
	for (i = 0; i < len; i++)
	{
		char c = s[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return 0;
	}
	return 1;
}

static int udb_ocl_is_participant(Client *server)
{
	return server && IsServer(server) && !IsULine(server);
}

/* Keep cached origins aligned with the visible topology. The quit hook runs
 * after dependants have been removed, but before the quitting root leaves the
 * global server list, so callers remove that root first when necessary. */
static void udb_ocl_membership_prune_invisible(void)
{
	UdbOclOrigin *origin;
	Client *server;

	for (origin = udb_ocl_origins; origin; origin = origin->next)
	{
		if (!origin->member)
			continue;
		server = udb_ocl_find_server(origin->sid);
		if (!udb_ocl_is_participant(server) || IsMe(server))
			origin->member = 0;
	}
}

static void udb_ocl_membership_rebuild(void)
{
	Client *server;
	UdbOclOrigin *origin;

	for (origin = udb_ocl_origins; origin; origin = origin->next)
		origin->member = 0;
	list_for_each_entry(server, &global_server_list, client_node) if (udb_ocl_is_participant(server) && !IsMe(server))
		udb_ocl_member_add(server->id);
	udb_ocl_membership_prune_invisible();
	udb_ocl_purge_nonmembers();
}

/* Canonical serialization and digests */

typedef struct UdbOclBuf
{
	unsigned char *data;
	size_t len;
	size_t cap;
	int overflow;
} UdbOclBuf;

static void udb_ocl_buf_put(UdbOclBuf *buf, const void *data, size_t len)
{
	if (buf->overflow)
		return;
	if (buf->len + len > UDB_OCL_CANONICAL_MAX || buf->len + len < buf->len)
	{
		buf->overflow = 1;
		return;
	}
	if (buf->len + len > buf->cap)
	{
		size_t ncap = buf->cap ? buf->cap : 4096;
		unsigned char *nd;
		while (ncap < buf->len + len)
		{
			if (ncap > SIZE_MAX / 2)
			{
				buf->overflow = 1;
				return;
			}
			ncap *= 2;
		}
		nd = realloc(buf->data, ncap);
		if (!nd)
		{
			buf->overflow = 1;
			return;
		}
		buf->data = nd;
		buf->cap = ncap;
	}
	memcpy(buf->data + buf->len, data, len);
	buf->len += len;
}

static void udb_ocl_buf_put_u32(UdbOclBuf *buf, unsigned int v)
{
	unsigned char b[4];

	b[0] = (unsigned char)((v >> 24) & 0xff);
	b[1] = (unsigned char)((v >> 16) & 0xff);
	b[2] = (unsigned char)((v >> 8) & 0xff);
	b[3] = (unsigned char)(v & 0xff);
	udb_ocl_buf_put(buf, b, 4);
}

static void udb_ocl_buf_put_str(UdbOclBuf *buf, const char *s)
{
	size_t len = s ? strlen(s) : 0;

	udb_ocl_buf_put_u32(buf, (unsigned int)len);
	if (len)
		udb_ocl_buf_put(buf, s, len);
}

static void udb_ocl_hex(const unsigned char *raw, size_t rawlen, char *out, size_t outlen)
{
	static const char hex[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < rawlen && (i * 2 + 2) < outlen; i++)
	{
		out[i * 2] = hex[raw[i] >> 4];
		out[i * 2 + 1] = hex[raw[i] & 0x0f];
	}
	out[i * 2] = '\0';
}

static void udb_ocl_sha256(const unsigned char *data, size_t len, unsigned char out[32])
{
	memset(out, 0, 32);
	SHA256(data, len, out);
}

/* Runtime-structure serialization: tagged, length-prefixed, order-preserving.
 * AddListItem() prepends, so the in-memory order is the evaluation order and
 * must be preserved verbatim. */
static int udb_ocl_serialize_acl(OperClassACL *acl, int depth, UdbOclBuf *buf)
{
	OperClassACLEntry *entry;
	OperClassACLEntryVar *var;
	OperClassACL *sub;
	unsigned int count = 0;

	if (!acl || depth > UDB_OCL_ACL_DEPTH_MAX)
	{
		buf->overflow = 1;
		return 0;
	}
	udb_ocl_buf_put_str(buf, acl->name);
	for (entry = acl->entries; entry; entry = entry->next)
		count++;
	udb_ocl_buf_put_u32(buf, count);
	for (entry = acl->entries; entry; entry = entry->next)
	{
		udb_ocl_buf_put_u32(buf, (unsigned int)entry->type);
		count = 0;
		for (var = entry->variables; var; var = var->next)
			count++;
		udb_ocl_buf_put_u32(buf, count);
		for (var = entry->variables; var; var = var->next)
		{
			udb_ocl_buf_put_str(buf, var->name);
			udb_ocl_buf_put_u32(buf, var->value ? 1u : 0u);
			if (var->value)
				udb_ocl_buf_put_str(buf, var->value);
		}
	}
	count = 0;
	for (sub = acl->acls; sub; sub = sub->next)
		count++;
	udb_ocl_buf_put_u32(buf, count);
	for (sub = acl->acls; sub; sub = sub->next)
	{
		if (!udb_ocl_serialize_acl(sub, depth + 1, buf))
			return 0;
	}
	return 1;
}

static int udb_ocl_serialize_operclass(OperClass *oc, UdbOclBuf *buf)
{
	OperClassACL *acl;
	unsigned int count = 0;

	if (!oc)
	{
		buf->overflow = 1;
		return 0;
	}
	udb_ocl_buf_put_str(buf, oc->name);
	udb_ocl_buf_put_str(buf, oc->ISA);
	for (acl = oc->acls; acl; acl = acl->next)
		count++;
	udb_ocl_buf_put_u32(buf, count);
	for (acl = oc->acls; acl; acl = acl->next)
	{
		if (!udb_ocl_serialize_acl(acl, 1, buf))
			return 0;
	}
	return 1;
}

static int udb_ocl_own_digest(OperClass *oc, unsigned char out[32])
{
	UdbOclBuf buf;
	unsigned char prefix[] = "UDB-OCL-STRUCT-v1";
	unsigned char zero = 0;
	int ok;

	memset(&buf, 0, sizeof(buf));
	udb_ocl_buf_put(&buf, prefix, sizeof(prefix) - 1);
	udb_ocl_buf_put(&buf, &zero, 1);
	ok = udb_ocl_serialize_operclass(oc, &buf) && !buf.overflow;
	if (ok)
		udb_ocl_sha256(buf.data, buf.len, out);
	free(buf.data);
	return ok;
}

static void udb_ocl_effective_digest(const unsigned char own[32], const unsigned char parent[32], unsigned char out[32])
{
	unsigned char data[4 + 20 + 1 + 32 + 32];
	unsigned char prefix[] = "UDB-OCL-EFFECTIVE-v1";
	size_t len = 0;

	data[len++] = (unsigned char)((((unsigned int)sizeof(data)) >> 24) & 0xff);
	data[len++] = (unsigned char)((((unsigned int)sizeof(data)) >> 16) & 0xff);
	data[len++] = (unsigned char)((((unsigned int)sizeof(data)) >> 8) & 0xff);
	data[len++] = (unsigned char)(((unsigned int)sizeof(data)) & 0xff);
	memcpy(data + len, prefix, sizeof(prefix) - 1);
	len += sizeof(prefix) - 1;
	data[len++] = 0;
	memcpy(data + len, own, 32);
	len += 32;
	memcpy(data + len, parent, 32);
	len += 32;
	udb_ocl_sha256(data, len, out);
}

static int udb_ocl_entry_cmp(const void *a, const void *b)
{
	return strcmp(((const UdbOclEntry *)a)->name, ((const UdbOclEntry *)b)->name);
}

static int udb_ocl_inventory_digest(UdbOclInventory *inv, unsigned char out[32])
{
	UdbOclBuf buf;
	unsigned char prefix[] = "UDB-OCL-INVENTORY-v1";
	unsigned char zero = 0;
	unsigned int i;
	int ok;

	memset(&buf, 0, sizeof(buf));
	udb_ocl_buf_put(&buf, prefix, sizeof(prefix) - 1);
	udb_ocl_buf_put(&buf, &zero, 1);
	udb_ocl_buf_put_u32(&buf, inv->count);
	for (i = 0; i < inv->count; i++)
	{
		udb_ocl_buf_put_str(&buf, inv->entries[i].name);
		udb_ocl_buf_put(&buf, inv->entries[i].digest, UDB_OCL_DIGEST_HEX_LEN);
		udb_ocl_buf_put(&buf, &zero, 1);
	}
	ok = !buf.overflow && buf.data;
	if (ok)
		udb_ocl_sha256(buf.data, buf.len, out);
	free(buf.data);
	return ok;
}

static int udb_oclg_view_digest(UdbOclInventory *view, int ready, unsigned char out[32])
{
	UdbOclBuf buf;
	unsigned char prefix[] = "UDB-OCLG-VIEW-v1";
	unsigned char zero = 0;
	unsigned int i;
	int ok;

	memset(&buf, 0, sizeof(buf));
	udb_ocl_buf_put(&buf, prefix, sizeof(prefix) - 1);
	udb_ocl_buf_put(&buf, &zero, 1);
	udb_ocl_buf_put_u32(&buf, ready ? 1u : 0u);
	udb_ocl_buf_put_u32(&buf, view ? view->count : 0u);
	if (view)
	{
		for (i = 0; i < view->count; i++)
		{
			udb_ocl_buf_put_str(&buf, view->entries[i].name);
			udb_ocl_buf_put(&buf, view->entries[i].digest, UDB_OCL_DIGEST_HEX_LEN);
			udb_ocl_buf_put(&buf, &zero, 1);
		}
	}
	ok = !buf.overflow && buf.data;
	if (ok)
		udb_ocl_sha256(buf.data, buf.len, out);
	free(buf.data);
	return ok;
}

/* Local inventory construction */

typedef struct UdbOclClassNode
{
	char name[OPERCLASSLEN + 1];
	int state; /* 0=unvisited 1=visiting 2=valid 3=invalid */
	unsigned char effective[32];
	ConfigItem_operclass *conf;
} UdbOclClassNode;

static int udb_ocl_node_find(UdbOclClassNode *nodes, unsigned int count, const char *name)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (!strcmp(nodes[i].name, name))
			return (int)i;
	return -1;
}

/* DFS with white/gray/black marking: a class with a missing parent, a parent
 * cycle, an excessive depth or an unserializable structure is invalid, and so
 * is every class inheriting from it.  Only the affected classes are excluded;
 * the rest of the inventory stays usable. */
static int udb_ocl_effective_of(UdbOclClassNode *nodes, unsigned int count, unsigned int idx, int depth,
								unsigned char out[32])
{
	UdbOclClassNode *node = &nodes[idx];
	unsigned char own[32], parent[32], eff[32];

	if (node->state == 2)
	{
		memcpy(out, node->effective, 32);
		return 1;
	}
	if (node->state == 3)
		return 0;
	if (depth > UDB_OCL_MAX_PARENT_DEPTH)
	{
		node->state = 3;
		return 0;
	}
	if (node->state == 1)
	{
		/* Parent chain re-entered the node: cycle. */
		node->state = 3;
		return 0;
	}
	node->state = 1;
	const char *parent_name = udb_compat_operclass_parent_name(node->conf);
	OperClass *contract = udb_compat_operclass_struct(node->conf);

	if (parent_name)
	{
		int pidx = udb_ocl_node_find(nodes, count, parent_name);

		if (pidx < 0 || !udb_ocl_effective_of(nodes, count, (unsigned int)pidx, depth + 1, parent))
		{
			node->state = 3;
			return 0;
		}
	}
	else
	{
		memset(parent, 0, sizeof(parent));
	}
	if (!contract || !udb_ocl_own_digest(contract, own))
	{
		node->state = 3;
		return 0;
	}
	udb_ocl_effective_digest(own, parent, eff);
	node->state = 2;
	memcpy(node->effective, eff, 32);
	memcpy(out, eff, 32);
	return 1;
}

static UdbOclInventory *udb_ocl_build_local_inventory(void)
{
	ConfigItem_operclass *oc;
	UdbOclClassNode *nodes = NULL;
	unsigned int count = 0, cap = 0, i;
	UdbOclInventory *inv = NULL;
	unsigned char raw[32];
	int limited = 0;

	for (oc = udb_compat_operclass_head(); oc; oc = udb_compat_operclass_next(oc))
	{
		const char *name = udb_compat_operclass_name(oc);

		if (!name)
			continue;
		if (count >= UDB_OCL_MAX_CLASSES)
		{
			limited = 1;
			break;
		}
		if (udb_ocl_node_find(nodes, count, name) >= 0)
			continue; /* first occurrence wins, matching find_operclass() */
		if (count >= cap)
		{
			unsigned int ncap = cap ? cap * 2 : 32;
			UdbOclClassNode *nn = realloc(nodes, ncap * sizeof(*nodes));

			if (!nn)
				goto done;
			nodes = nn;
			cap = ncap;
		}
		memset(&nodes[count], 0, sizeof(nodes[count]));
		strlcpy(nodes[count].name, name, sizeof(nodes[count].name));
		nodes[count].conf = oc;
		count++;
	}
	if (limited)
	{
		udb_log(ULOG_WARNING, "UDB_OCL_LIMIT", NULL,
				"More than $max operclasses configured; classes beyond the limit are excluded from OCL",
				log_data_integer("max", UDB_OCL_MAX_CLASSES));
	}
	inv = safe_alloc(sizeof(*inv));
	strlcpy(inv->epoch, udb_ocl_epoch, sizeof(inv->epoch));
	inv->generation = udb_ocl_local_generation + 1;
	inv->count = 0;
	inv->entries = safe_alloc(sizeof(UdbOclEntry) * (count ? count : 1));
	for (i = 0; i < count; i++)
	{
		unsigned char eff[32];

		if (!udb_ocl_effective_of(nodes, count, i, 0, eff))
			continue;
		strlcpy(inv->entries[inv->count].name, nodes[i].name, sizeof(inv->entries[inv->count].name));
		udb_ocl_hex(eff, 32, inv->entries[inv->count].digest, sizeof(inv->entries[inv->count].digest));
		inv->count++;
	}
	qsort(inv->entries, inv->count, sizeof(UdbOclEntry), udb_ocl_entry_cmp);
	if (!udb_ocl_inventory_digest(inv, raw))
	{
		udb_ocl_free_inventory(inv);
		inv = NULL;
	}
	else
	{
		udb_ocl_hex(raw, 32, inv->inventory_digest, sizeof(inv->inventory_digest));
	}
done:
	free(nodes);
	return inv;
}

/* Local publish, send and replay */

static void udb_ocl_send_inventory_to(Client *to, const char *origin_sid, UdbOclInventory *inv)
{
	unsigned int i;

	if (!to || !inv || !udb_ocl_is_participant(to))
		return;
	if (!udb_send_db_to_one(to, ":%s DB * OCL BEGIN %s %s %lu %u %s", me.id, origin_sid, inv->epoch, inv->generation,
							inv->count, inv->inventory_digest))
		return;
	for (i = 0; i < inv->count; i++)
	{
		if (!udb_send_db_to_one(to, ":%s DB * OCL ITEM %s %s %lu %s %s", me.id, origin_sid, inv->epoch, inv->generation,
								inv->entries[i].name, inv->entries[i].digest))
			return;
	}
	udb_send_db_to_one(to, ":%s DB * OCL END %s %s %lu", me.id, origin_sid, inv->epoch, inv->generation);
}

static void udb_ocl_send_inventory_broadcast(Client *except, const char *origin_sid, UdbOclInventory *inv)
{
	Client *server;
	unsigned int i;

	if (!inv)
		return;
	list_for_each_entry(server, &server_list, special_node)
	{
		if (server == except || (except && server == except->direction) || !udb_ocl_is_participant(server) ||
			!udb_has_hello(server))
			continue;
		udb_send_db_to_one(server, ":%s DB * OCL BEGIN %s %s %lu %u %s", me.id, origin_sid, inv->epoch, inv->generation,
						   inv->count, inv->inventory_digest);
	}
	for (i = 0; i < inv->count; i++)
		list_for_each_entry(server, &server_list, special_node)
		{
			if (server == except || (except && server == except->direction) || !udb_ocl_is_participant(server) ||
				!udb_has_hello(server))
				continue;
			udb_send_db_to_one(server, ":%s DB * OCL ITEM %s %s %lu %s %s", me.id, origin_sid, inv->epoch,
							   inv->generation, inv->entries[i].name, inv->entries[i].digest);
		}
	list_for_each_entry(server, &server_list, special_node)
	{
		if (server == except || (except && server == except->direction) || !udb_ocl_is_participant(server) ||
			!udb_has_hello(server))
			continue;
		udb_send_db_to_one(server, ":%s DB * OCL END %s %s %lu", me.id, origin_sid, inv->epoch, inv->generation);
	}
}

static void udb_ocl_local_publish(void)
{
	UdbOclInventory *inv, *old;

	inv = udb_ocl_build_local_inventory();
	if (!inv)
		return;
	if (udb_ocl_local && udb_ocl_local->count == inv->count &&
		!strcmp(udb_ocl_local->inventory_digest, inv->inventory_digest))
	{
		udb_ocl_free_inventory(inv);
		return;
	}
	udb_ocl_local_generation++;
	inv->generation = udb_ocl_local_generation;
	udb_ocl_free_inventory(udb_ocl_local);
	udb_ocl_local = inv;
	udb_oclg_recompute();
	udb_log(ULOG_INFO, "UDB_OCL_LOCAL_CHANGED", NULL,
			"Local operclass inventory changed: generation $gen, $count classes, digest $digest",
			log_data_integer("gen", (long long)inv->generation), log_data_integer("count", (long long)inv->count),
			log_data_string("digest", inv->inventory_digest));
	udb_ocl_send_inventory_broadcast(NULL, me.id, udb_ocl_local);
}

static int udb_ocl_local_rebuild(void)
{
	if (!udb_ctx)
		return 0;
	udb_ocl_ensure_epoch();
	udb_ocl_local_publish();
	return 1;
}

static int udb_oclg_push(Client *to)
{
	unsigned int i;

	if (!to || !udb_ctx || !udb_oclg_view)
		return 0;
	if (!udb_send_db_to_one(to, ":%s DB %s OCLG BEGIN %s %lu %s %u %s", me.id, to->id, udb_ocl_epoch,
							udb_oclg_generation, udb_oclg_ready ? "READY" : "INCOMPLETE", udb_oclg_view->count,
							udb_oclg_view->inventory_digest))
		return 0;
	for (i = 0; i < udb_oclg_view->count; i++)
	{
		if (!udb_send_db_to_one(to, ":%s DB %s OCLG ITEM %s %lu %s %s", me.id, to->id, udb_ocl_epoch,
								udb_oclg_generation, udb_oclg_view->entries[i].name, udb_oclg_view->entries[i].digest))
			return 0;
	}
	udb_send_db_to_one(to, ":%s DB %s OCLG END %s %lu", me.id, to->id, udb_ocl_epoch, udb_oclg_generation);
	return 1;
}

/* OCL receive path */

static void udb_ocl_stage_discard(UdbOclOrigin *origin)
{
	udb_ocl_free_inventory(origin->staging);
	origin->staging = NULL;
	origin->stage_received = 0;
	origin->stage_deadline = 0;
}

static void udb_ocl_stage_abort(UdbOclOrigin *origin, const char *reason)
{
	udb_log(ULOG_WARNING, "UDB_OCL_STAGE_ABORT", NULL,
			"Aborted operclass inventory staging for origin $origin: $reason", log_data_string("origin", origin->sid),
			log_data_string("reason", reason));
	udb_ocl_stage_discard(origin);
	udb_oclg_recompute();
}

/* Validate that this frame's originSID is a real visible participant IRCd
 * reachable through the very peer that delivered the frame. */
static UdbOclOrigin *udb_ocl_validate_frame(Client *direct_peer, const char *origin_sid, int create)
{
	Client *origin_client;

	if (!origin_sid || !*origin_sid || !strcmp(origin_sid, me.id))
		return NULL;
	origin_client = udb_ocl_find_server(origin_sid);
	if (!origin_client || !udb_ocl_is_participant(origin_client) || IsMe(origin_client) ||
		!udb_ocl_member_exists(origin_sid) || origin_client->direction != direct_peer)
		return NULL;
	return create ? udb_ocl_get_origin(origin_sid) : udb_ocl_find_origin(origin_sid);
}

static void udb_ocl_handle_begin(Client *direct_peer, const char *parv[])
{
	const char *origin_sid = parv[4];
	const char *epoch = parv[5];
	const char *digest = parv[8];
	unsigned long generation = 0;
	unsigned int count = 0;
	UdbOclOrigin *origin;
	UdbOclInventory *staging;

	if (!udb_strtoul_strict(parv[6], &generation) || !generation ||
		!udb_parse_uint_strict(parv[7], &count, 0, UDB_OCL_MAX_CLASSES) ||
		!udb_ocl_is_lower_hex(epoch, UDB_OCL_EPOCH_LEN) || !udb_ocl_is_lower_hex(digest, UDB_OCL_DIGEST_HEX_LEN))
		return;
	origin = udb_ocl_validate_frame(direct_peer, origin_sid, 1);
	if (!origin || !udb_ocl_member_exists(origin_sid))
		return;
	if (!strcmp(origin_sid, direct_peer->id) && !udb_hello_epoch_matches(direct_peer, epoch))
		return;

	if (udb_ocl_epoch_retired(origin, epoch))
		return; /* frames from a superseded epoch are stale */

	if (origin->accepted_valid && !strcmp(origin->accepted_epoch, epoch))
	{
		if (generation < origin->accepted_generation)
			return; /* stale */
		if (generation == origin->accepted_generation)
		{
			if (origin->accepted_count != count || strcmp(origin->accepted_digest, digest))
			{
				/* The high-water descriptor remains immutable even after an abort. */
				udb_log(ULOG_ERROR, "UDB_OCL_PROTOCOL_VIOLATION", direct_peer,
						"Origin $origin re-advertised generation $gen with a different descriptor; frame ignored",
						log_data_string("origin", origin->sid), log_data_integer("gen", (long long)generation));
				return;
			}
			if (origin->current && !strcmp(origin->current->epoch, epoch) && origin->current->generation == generation)
				return; /* already committed */
			if (origin->staging && !strcmp(origin->staging->epoch, epoch) && origin->staging->generation == generation)
				return; /* already staging */
		}
	}
	else if (origin->accepted_valid && strcmp(origin->accepted_epoch, epoch))
	{
		udb_ocl_retire_epoch(origin, origin->accepted_epoch);
	}
	else if (!origin->accepted_valid && origin->current)
	{
		/* Defensive compatibility for an origin created before watermark state. */
		if (!strcmp(origin->current->epoch, epoch) && origin->current->generation > generation)
			return;
	}

	if (origin->staging)
		udb_ocl_stage_discard(origin);
	if (origin->current)
	{
		/* A newer snapshot was announced: the previous one stops participating
		 * immediately, fail-closed, even if the new one never completes. */
		if (strcmp(origin->current->epoch, epoch))
			udb_ocl_retire_epoch(origin, origin->current->epoch);
		udb_ocl_free_inventory(origin->current);
		origin->current = NULL;
	}
	if (!origin->accepted_valid || strcmp(origin->accepted_epoch, epoch) || generation > origin->accepted_generation)
	{
		strlcpy(origin->accepted_epoch, epoch, sizeof(origin->accepted_epoch));
		origin->accepted_generation = generation;
		origin->accepted_count = count;
		strlcpy(origin->accepted_digest, digest, sizeof(origin->accepted_digest));
		origin->accepted_valid = 1;
	}
	staging = safe_alloc(sizeof(*staging));
	strlcpy(staging->epoch, epoch, sizeof(staging->epoch));
	staging->generation = generation;
	staging->count = count;
	strlcpy(staging->inventory_digest, digest, sizeof(staging->inventory_digest));
	staging->entries = safe_alloc(sizeof(UdbOclEntry) * (count ? count : 1));
	origin->staging = staging;
	origin->stage_received = 0;
	origin->stage_deadline = time(NULL) + UDB_OCL_STAGE_TIMEOUT;
	udb_oclg_recompute();
}

static void udb_ocl_handle_item(Client *direct_peer, const char *parv[])
{
	const char *origin_sid = parv[4];
	const char *epoch = parv[5];
	const char *name = parv[7];
	const char *digest = parv[8];
	unsigned long generation = 0;
	UdbOclOrigin *origin;
	UdbOclInventory *st;
	unsigned int i;

	if (!udb_strtoul_strict(parv[6], &generation) || !generation || !udb_ocl_is_lower_hex(epoch, UDB_OCL_EPOCH_LEN) ||
		!udb_ocl_is_lower_hex(digest, UDB_OCL_DIGEST_HEX_LEN) || !valid_operclass_name(name))
		return;
	origin = udb_ocl_validate_frame(direct_peer, origin_sid, 0);
	if (!origin || !origin->member || !origin->staging)
		return;
	st = origin->staging;
	if (strcmp(st->epoch, epoch) || st->generation != generation)
		return; /* stale or from a superseded epoch */
	for (i = 0; i < origin->stage_received; i++)
	{
		if (!strcmp(st->entries[i].name, name))
		{
			udb_ocl_stage_abort(origin, "duplicate item");
			return;
		}
	}
	if (origin->stage_received >= st->count)
	{
		udb_ocl_stage_abort(origin, "too many items");
		return;
	}
	strlcpy(st->entries[origin->stage_received].name, name, sizeof(st->entries[0].name));
	strlcpy(st->entries[origin->stage_received].digest, digest, sizeof(st->entries[0].digest));
	origin->stage_received++;
}

static void udb_ocl_handle_end(Client *direct_peer, const char *parv[])
{
	const char *origin_sid = parv[4];
	const char *epoch = parv[5];
	unsigned long generation = 0;
	UdbOclOrigin *origin;
	UdbOclInventory *st, *old;
	unsigned char raw[32];
	char hex[UDB_OCL_DIGEST_HEX_LEN + 1];

	if (!udb_strtoul_strict(parv[6], &generation) || !generation || !udb_ocl_is_lower_hex(epoch, UDB_OCL_EPOCH_LEN))
		return;
	origin = udb_ocl_validate_frame(direct_peer, origin_sid, 0);
	if (!origin || !origin->member || !origin->staging)
		return;
	st = origin->staging;
	if (strcmp(st->epoch, epoch) || st->generation != generation)
		return; /* stale */
	if (!origin->accepted_valid || strcmp(origin->accepted_epoch, st->epoch) ||
		origin->accepted_generation != st->generation || origin->accepted_count != st->count ||
		strcmp(origin->accepted_digest, st->inventory_digest))
	{
		udb_ocl_stage_abort(origin, "staging is no longer the high-water snapshot");
		return;
	}
	if (origin->stage_received != st->count)
	{
		udb_ocl_stage_abort(origin, "item count mismatch");
		return;
	}
	if (!udb_ocl_inventory_digest(st, raw))
	{
		udb_ocl_stage_abort(origin, "inventory digest calculation failure");
		return;
	}
	udb_ocl_hex(raw, 32, hex, sizeof(hex));
	if (strcmp(hex, st->inventory_digest))
	{
		udb_ocl_stage_abort(origin, "inventory digest mismatch");
		return;
	}
	origin->staging = NULL;
	old = origin->current;
	origin->current = st;
	origin->stage_received = 0;
	origin->stage_deadline = 0;
	if (old && strcmp(old->epoch, st->epoch))
		udb_ocl_retire_epoch(origin, old->epoch);
	udb_ocl_free_inventory(old);
	udb_log(ULOG_INFO, "UDB_OCL_REMOTE_COMMITTED", direct_peer,
			"Committed operclass inventory for origin $origin: generation $gen, $count classes",
			log_data_string("origin", origin->sid), log_data_integer("gen", (long long)st->generation),
			log_data_integer("count", (long long)st->count));
	udb_oclg_recompute();
	/* Forward only after a validated atomic commit. */
	udb_ocl_send_inventory_broadcast(direct_peer, origin->sid, origin->current);
}

static void udb_ocl_handle(Client *client, Client *direct_peer, int parc, const char *parv[], int is_broadcast)
{
	const char *verb;

	(void)client;
	if (!udb_ctx || !direct_peer || !is_broadcast || parc < 4 || !parv[3])
		return;
	verb = parv[3];
	if (!strcasecmp(verb, "BEGIN"))
	{
		if (parc == 9)
			udb_ocl_handle_begin(direct_peer, parv);
	}
	else if (!strcasecmp(verb, "ITEM"))
	{
		if (parc == 9)
			udb_ocl_handle_item(direct_peer, parv);
	}
	else if (!strcasecmp(verb, "END"))
	{
		if (parc == 7)
			udb_ocl_handle_end(direct_peer, parv);
	}
}

/* UDB IRCd nodes never subscribe to OCLG; the projection is consumed by
 * explicit OCLG subscribers (Services), which ignore these frames here. */
static void udb_oclg_handle(Client *client, int is_for_me)
{
	(void)client;
	(void)is_for_me;
}

static void udb_ocl_maybe_replay_to_peer(Client *server)
{
	UdbHelloPeer *peer;
	UdbOclOrigin *origin;

	if (!udb_ctx || !server)
		return;
	peer = udb_hello_peer(server, 0);
	if (!peer || peer->state != UDB_HEL_CONFIRMED || !peer->ocl_capable)
		return;
	if (udb_ocl_is_participant(server) && !peer->ocl_replayed)
	{
		peer->ocl_replayed = 1;
		if (udb_ocl_local)
			udb_ocl_send_inventory_to(server, me.id, udb_ocl_local);
		for (origin = udb_ocl_origins; origin; origin = origin->next)
		{
			if (origin->member && origin->current)
				udb_ocl_send_inventory_to(server, origin->sid, origin->current);
		}
	}
	if (peer->oclg_subscribed && !peer->oclg_replayed)
	{
		peer->oclg_replayed = 1;
		udb_oclg_push(server);
	}
}

static void udb_ocl_peer_instance_changed(Client *server)
{
	UdbOclOrigin *origin;

	if (!server || !udb_ocl_is_participant(server))
		return;
	origin = udb_ocl_find_origin(server->id);
	if (!origin)
		return;
	if (origin->accepted_valid)
		udb_ocl_retire_epoch(origin, origin->accepted_epoch);
	if (origin->current)
	{
		udb_ocl_retire_epoch(origin, origin->current->epoch);
		udb_ocl_free_inventory(origin->current);
		origin->current = NULL;
	}
	if (origin->staging)
	{
		udb_ocl_retire_epoch(origin, origin->staging->epoch);
		udb_ocl_free_inventory(origin->staging);
		origin->staging = NULL;
	}
	origin->stage_received = 0;
	origin->stage_deadline = 0;
	udb_oclg_recompute();
}

static void udb_ocl_membership_changed(void)
{
	if (!udb_ctx)
		return;
	udb_ocl_membership_rebuild();
	udb_oclg_recompute();
}

static void udb_ocl_origin_quit(Client *client)
{
	if (!udb_ctx || !client || !*client->id)
		return;
	/* Descendants of a split may already be gone without receiving this hook. */
	udb_ocl_member_remove(client->id);
	udb_ocl_membership_prune_invisible();
	udb_ocl_purge_nonmembers();
	udb_oclg_recompute();
}

static void udb_ocl_stage_timeout_check(time_t now)
{
	UdbOclOrigin *origin;
	int changed = 0;

	for (origin = udb_ocl_origins; origin; origin = origin->next)
	{
		if (origin->staging && origin->stage_deadline && origin->stage_deadline <= now)
		{
			udb_log(ULOG_WARNING, "UDB_OCL_STAGE_ABORT", NULL,
					"Operclass inventory staging for origin $origin timed out", log_data_string("origin", origin->sid));
			udb_ocl_stage_discard(origin);
			changed = 1;
		}
	}
	if (changed)
		udb_oclg_recompute();
}

static void udb_ocl_shutdown(void)
{
	while (udb_ocl_origins)
	{
		UdbOclOrigin *next = udb_ocl_origins->next;

		udb_ocl_free_inventory(udb_ocl_origins->current);
		udb_ocl_free_inventory(udb_ocl_origins->staging);
		safe_free(udb_ocl_origins);
		udb_ocl_origins = next;
	}
	udb_ocl_free_inventory(udb_ocl_local);
	udb_ocl_local = NULL;
	udb_ocl_free_inventory(udb_oclg_view);
	udb_oclg_view = NULL;
	udb_ocl_local_generation = 0;
	udb_oclg_ready = 0;
	udb_oclg_generation = 0;
	udb_ocl_epoch[0] = '\0';
}

/* Generation is monotonic only within an instance epoch. A fresh epoch lets
 * receivers distinguish a reloaded module's generation-1 inventory from replay. */
static void udb_ocl_ensure_epoch(void)
{
	unsigned char raw[8];
	int i;

	if (*udb_ocl_epoch)
		return;
	if (!RAND_bytes(raw, (int)sizeof(raw)))
	{
		unsigned long fallback = (unsigned long)time(NULL) ^ (unsigned long)(void *)&udb_ocl_origins;

		snprintf(udb_ocl_epoch, sizeof(udb_ocl_epoch), "%016lx", fallback);
		return;
	}
	for (i = 0; i < (int)sizeof(raw); i++)
		snprintf(udb_ocl_epoch + (i * 2), 3, "%02x", raw[i]);
	udb_ocl_epoch[UDB_OCL_EPOCH_LEN] = '\0';
}

/* Registry completeness, OCLG projection */

static UdbOclEntry *udb_ocl_find_entry(UdbOclInventory *inv, const char *name)
{
	unsigned int i, lo = 0, hi = inv ? inv->count : 0;

	while (lo < hi)
	{
		unsigned int mid = (lo + hi) / 2;
		int c = strcmp(inv->entries[mid].name, name);

		if (c == 0)
			return &inv->entries[mid];
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}

	for (i = 0; inv && i < inv->count; i++)
		if (!strcmp(inv->entries[i].name, name))
			return &inv->entries[i];
	return NULL;
}

static int udb_ocl_registry_complete(void)
{
	UdbOclOrigin *origin;

	if (!udb_ocl_local)
		return 0;
	for (origin = udb_ocl_origins; origin; origin = origin->next)
	{
		if (!origin->member)
			continue;
		if (!origin->current)
			return 0;
	}
	return 1;
}

static void udb_oclg_log_diff(UdbOclInventory *old_view, int old_ready, UdbOclInventory *new_view, int new_ready)
{
	unsigned int oi = 0, ni = 0;

	if (!new_ready)
	{
		if (old_ready)
			udb_log(ULOG_WARNING, "UDB_OCL_REGISTRY_INCOMPLETE", NULL,
					"Operclass registry incomplete; OCLG availability withdrawn");
		return;
	}
	if (old_ready && !new_ready)
		udb_log(ULOG_WARNING, "UDB_OCL_REGISTRY_INCOMPLETE", NULL,
				"Operclass registry incomplete; OCLG availability withdrawn");
	while (oi < (old_ready && old_view ? old_view->count : 0) && ni < (new_view ? new_view->count : 0))
	{
		const char *on = old_view->entries[oi].name;
		const char *nn = new_view->entries[ni].name;
		int c = strcmp(on, nn);

		if (c < 0)
		{
			udb_log(ULOG_INFO, "UDB_OCL_GLOBAL_DEL", NULL, "Operclass $name left the global operclass view",
					log_data_string("name", on));
			oi++;
		}
		else if (c > 0)
		{
			udb_log(ULOG_INFO, "UDB_OCL_GLOBAL_ADD", NULL, "Operclass $name entered the global operclass view",
					log_data_string("name", nn));
			ni++;
		}
		else
		{
			oi++;
			ni++;
		}
	}
	while (oi < (old_ready && old_view ? old_view->count : 0))
	{
		udb_log(ULOG_INFO, "UDB_OCL_GLOBAL_DEL", NULL, "Operclass $name left the global operclass view",
				log_data_string("name", old_view->entries[oi].name));
		oi++;
	}
	while (ni < (new_view ? new_view->count : 0))
	{
		udb_log(ULOG_INFO, "UDB_OCL_GLOBAL_ADD", NULL, "Operclass $name entered the global operclass view",
				log_data_string("name", new_view->entries[ni].name));
		ni++;
	}
}

static void udb_oclg_push_subscribers(void)
{
	UdbHelloPeer *peer;

	for (peer = udb_hello_peers; peer; peer = peer->next)
	{
		if (peer->state == UDB_HEL_CONFIRMED && peer->oclg_subscribed && peer->peer)
			udb_oclg_push(peer->peer);
	}
}

static void udb_oclg_recompute(void)
{
	UdbOclInventory *old_view = udb_oclg_view;
	int old_ready = udb_oclg_ready;
	int complete = udb_ocl_registry_complete();
	UdbOclInventory *view;
	UdbOclOrigin *origin;
	unsigned char raw[32];
	char hex[UDB_OCL_DIGEST_HEX_LEN + 1];

	view = safe_alloc(sizeof(*view));
	strlcpy(view->epoch, udb_ocl_epoch, sizeof(view->epoch));
	udb_oclg_ready = complete;
	if (complete)
	{
		unsigned int i;

		view->entries = safe_alloc(sizeof(UdbOclEntry) * (udb_ocl_local->count ? udb_ocl_local->count : 1));
		for (i = 0; i < udb_ocl_local->count; i++)
		{
			UdbOclEntry *entry = &udb_ocl_local->entries[i];
			UdbOclEntry *out = &view->entries[view->count];
			UdbOclOrigin *origin;
			int ok = 1;

			for (origin = udb_ocl_origins; origin; origin = origin->next)
			{
				UdbOclEntry *match;

				if (!origin->member)
					continue;
				if (!origin->current)
				{
					ok = 0;
					break;
				}
				match = udb_ocl_find_entry(origin->current, entry->name);
				if (!match || strcmp(match->digest, entry->digest))
				{
					ok = 0;
					break;
				}
			}
			if (ok)
			{
				*out = *entry;
				view->count++;
			}
		}
	}
	if (!udb_oclg_view_digest(view, udb_oclg_ready, raw))
	{
		udb_ocl_free_inventory(view);
		return;
	}
	udb_ocl_hex(raw, 32, hex, sizeof(hex));
	if (udb_oclg_ready == old_ready && old_view && !strcmp(old_view->epoch, view->epoch) &&
		old_view->count == view->count && !strcmp(old_view->inventory_digest, hex))
	{
		/* No effective change: keep generation stable to avoid churn. */
		udb_ocl_free_inventory(view);
		return;
	}
	udb_oclg_generation++;
	view->generation = udb_oclg_generation;
	strlcpy(view->inventory_digest, hex, sizeof(view->inventory_digest));
	udb_oclg_view = view;
	udb_oclg_log_diff(old_view, old_ready, view, udb_oclg_ready);
	udb_ocl_free_inventory(old_view);
	udb_oclg_push_subscribers();
}

/* End of udb_operclasses.c.inc */

/* Authorized real-time mutations: validation, effects, persistence, forwarding */
/* Inlined: udb_mutation.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Real-time Database Mutations & Propagator Resolution
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static const char *udb_propagator_policy(UdbContext *ctx)
{
	if (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator)
		return udb_cfg->propagator;
	if (ctx && ctx->propagator_setting && *ctx->propagator_setting)
		return ctx->propagator_setting;
	return NULL;
}

static int udb_propagator_policy_present(UdbContext *ctx)
{
	return udb_propagator_policy(ctx) != NULL;
}

/* Resolve the first locally usable candidate. Network-wide visibility is never
 * enough: a remote candidate must be a direct server peer and staged-sync use
 * additionally requires a completed HEL 4 negotiation. */
static int udb_select_propagator(UdbContext *ctx, int require_hello, UdbPropagatorSelection *selected)
{
	const char *cursor = udb_propagator_policy(ctx);
	char name[HOSTLEN + 1];
	int result;

	if (!selected)
		return 0;
	memset(selected, 0, sizeof(*selected));
	while (cursor && (result = udb_propagator_list_next(&cursor, name)) > 0)
	{
		Client *candidate;

		if (me.name[0] && !strcasecmp(name, me.name))
		{
			memcpy(selected->name, name, strlen(name) + 1);
			selected->is_local = 1;
			return 1;
		}
		candidate = find_server(name, NULL);
		if (!candidate || !IsServer(candidate) || !MyConnect(candidate))
			continue;
		if (require_hello && !udb_has_hello(candidate))
			continue;
		selected->peer = candidate;
		memcpy(selected->name, name, strlen(name) + 1);
		return 1;
	}
	return 0;
}

static int udb_is_propagator(UdbContext *ctx, Client *server)
{
	UdbPropagatorSelection selected;

	if (!server || !IsServer(server) || !MyConnect(server))
		return 0;
	return udb_select_propagator(ctx, 1, &selected) && !selected.is_local && selected.peer == server;
}

/* Copied foreground delivery and replay envelopes share the engine's 8 MiB
 * queue bound. Only stable IDs and revisions cross the writer boundary. */
typedef struct UdbNativeDelivery
{
	struct UdbNativeDelivery *next;
	UdbStorageEngine *engine;
	uint64_t owner, job;
	uint64_t module_revision, authority_revision;
	uint64_t replay_round;
	size_t length, reservation, credit;
	uint32_t kind;
	char peer_sid[4], peer_epoch[17];
	int broadcast, local, replay, cancelled;
	unsigned char bytes[1];
} UdbNativeDelivery;

static UdbNativeDelivery *udb_native_deliveries;
static UdbNativeDelivery *udb_native_delivery_tail;

static int udb_native_deliveries_present(void)
{
	return udb_native_deliveries != NULL;
}

static void udb_mutation_trigger_gap_recovery(Client *peer)
{
	(void)peer;
	udb_ready = 0;
	udb_sync_status = UDB_SYNC_DEGRADED;
	if (!udb_degraded_since)
		udb_degraded_since = time(NULL);
	udb_sync_hello_refresh_all();
	if (!udb_reconcile.next_retry_at)
		udb_reconcile.next_retry_at = time(NULL) + UDB_RECONCILE_RETRY_BASE;
}

static void udb_native_delivery_drop(UdbNativeDelivery *delivery)
{
	UdbNativeDelivery **link = &udb_native_deliveries;
	while (*link && *link != delivery)
		link = &(*link)->next;
	if (!*link)
		abort();
	*link = delivery->next;
	if (udb_native_delivery_tail == delivery)
	{
		udb_native_delivery_tail = NULL;
		for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
			udb_native_delivery_tail = item;
	}
	if (udb_storage_engine_delivery_release(delivery->engine, delivery->reservation) != UDB_STORAGE_OK)
		abort();
	udb_storage_free(delivery);
}

static int udb_native_delivery_current(UdbNativeDelivery *delivery)
{
	UdbStorageEngine *engine = delivery->engine;
	udb_storage_mutex_lock(&engine->mutex);
	int current = !engine->failed && !engine->stopping && engine->admission &&
				  delivery->module_revision == engine->module_revision &&
				  delivery->authority_revision == engine->authority_revision;
	udb_storage_mutex_unlock(&engine->mutex);
	if (!current || delivery->cancelled || !udb_ctx || udb_ctx->storage != engine)
		return 0;
	if (delivery->local)
	{
		UdbPropagatorSelection selected;
		return !udb_propagator_policy_present(udb_ctx) ||
			   (udb_select_propagator(udb_ctx, 1, &selected) && selected.is_local);
	}
	Client *peer = udb_ocl_find_server(delivery->peer_sid);
	UdbHelloPeer *hello = peer ? udb_hello_peer(peer, 0) : NULL;
	return peer && !IsDead(peer) && !IsDeadSocket(peer) && MyConnect(peer) && hello && hello->remote_epoch_known &&
		   !strcmp(hello->remote_epoch, delivery->peer_epoch) && udb_is_authorized_sync_source(udb_ctx, peer);
}

static UdbStorageResult udb_native_delivery_submit(UdbNativeDelivery *delivery)
{
	if (delivery->job || !delivery->credit || !udb_native_delivery_current(delivery))
		return UDB_STORAGE_BUSY;
	UdbStorageResult result = udb_storage_engine_submit_credit(
		delivery->engine, delivery->kind, delivery->owner, delivery->module_revision, delivery->authority_revision,
		delivery->bytes, delivery->length, delivery->credit, &delivery->job);
	if (result == UDB_STORAGE_OK)
	{
		delivery->reservation -= delivery->credit;
		delivery->credit = 0;
	}
	return result;
}

static UdbStorageResult udb_native_delivery_create(UdbStorageEngine *engine, Client *peer, uint32_t kind,
												   const void *bytes, size_t length, int broadcast, int local,
												   int replay, UdbNativeDelivery **out)
{
	*out = NULL;
	if (!engine || !bytes || length < UDB_STORAGE_MUTATION_HEADER ||
		length > UDB_STORAGE_QUEUE_LIMIT - sizeof(UdbNativeDelivery))
		return UDB_STORAGE_REJECTED;
	size_t credit = udb_storage_engine_job_charge(length);
	size_t envelope = udb_storage_allocation_charge(sizeof(UdbNativeDelivery) + length);
	if (!credit || !envelope || envelope > UDB_STORAGE_QUEUE_LIMIT - credit)
		return UDB_STORAGE_LIMIT;
	uint64_t owner = udb_native_owner_next(), module_revision, authority_revision;
	if (!owner)
		return UDB_STORAGE_LIMIT;
	udb_storage_mutex_lock(&engine->mutex);
	module_revision = engine->module_revision;
	authority_revision = engine->authority_revision;
	udb_storage_mutex_unlock(&engine->mutex);
	UdbStorageResult result =
		udb_storage_engine_delivery_reserve(engine, module_revision, authority_revision, envelope + credit);
	if (result != UDB_STORAGE_OK)
		return result;
	UdbNativeDelivery *delivery =
		udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*delivery) + length);
	if (!delivery)
	{
		udb_storage_engine_delivery_release(engine, envelope + credit);
		return UDB_STORAGE_LIMIT;
	}
	delivery->engine = engine;
	delivery->owner = owner;
	delivery->module_revision = module_revision;
	delivery->authority_revision = authority_revision;
	delivery->reservation = envelope + credit;
	delivery->credit = credit;
	delivery->kind = kind;
	delivery->length = length;
	delivery->broadcast = broadcast;
	delivery->local = local;
	delivery->replay = replay;
	memcpy(delivery->bytes, bytes, length);
	if (peer)
	{
		UdbHelloPeer *hello = udb_hello_peer(peer, 0);
		if (!hello || !hello->remote_epoch_known)
		{
			udb_storage_engine_delivery_release(engine, delivery->reservation);
			udb_storage_free(delivery);
			return UDB_STORAGE_REJECTED;
		}
		memcpy(delivery->peer_sid, peer->id, 4);
		memcpy(delivery->peer_epoch, hello->remote_epoch, 17);
	}
	if (udb_native_delivery_tail)
		udb_native_delivery_tail->next = delivery;
	else
		udb_native_deliveries = delivery;
	udb_native_delivery_tail = delivery;
	*out = delivery;
	return UDB_STORAGE_OK;
}

/* Bind/prune copied live traffic against the first agreed INF origin and
 * watermark. Pre-INF traffic is provisional, never an origin authority. */
static int udb_native_mutation_bind(Client *peer, unsigned long round, const char *sid, const char *epoch,
									uint64_t watermark)
{
	uint64_t expected = watermark;
	uint64_t number = udb_native_epoch_number(epoch);
	for (UdbNativeDelivery *item = udb_native_deliveries, *next; item; item = next)
	{
		next = item->next;
		if (!item->replay || item->cancelled)
			continue;
		if (!udb_native_delivery_current(item) || strcmp(item->peer_sid, peer->id) || memcmp(item->bytes, sid, 4) ||
			udb_storage_get_u64(item->bytes + 4) != number || (item->replay_round && item->replay_round != round))
			return 0;
		uint64_t sequence = udb_storage_get_u64(item->bytes + 12);
		if (!item->job && sequence <= watermark)
		{
			udb_native_delivery_drop(item);
			continue;
		}
		if (expected == UINT64_MAX || sequence != expected + 1)
			return 0;
		expected = sequence;
		item->replay_round = round;
	}
	return 1;
}

static int udb_native_mutation_pending(unsigned long round)
{
	for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
		if (!item->cancelled && item->replay && item->replay_round == round)
			return 1;
	return 0;
}

static int udb_native_mutation_replay(unsigned long round)
{
	for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
	{
		if (item->cancelled || !item->replay || item->replay_round != round || item->job)
			continue;
		if (udb_native_delivery_submit(item) != UDB_STORAGE_OK)
			return 0;
	}
	return 1;
}

static void udb_native_mutation_abort(unsigned long round)
{
	for (UdbNativeDelivery *item = udb_native_deliveries, *next; item; item = next)
	{
		next = item->next;
		if (round && (!item->replay || (item->replay_round && item->replay_round != round)))
			continue;
		if (item->job)
		{
			item->cancelled = 1;
			udb_storage_engine_cancel_owner(item->engine, item->owner);
		}
		else
			udb_native_delivery_drop(item);
	}
}

static void udb_native_mutation_round_begin(Client *peer)
{
	if (!udb_ctx || !udb_ctx->storage)
		return;
	UdbStorageEngine *engine = udb_ctx->storage;
	uint64_t old_revision, revision;
	udb_storage_mutex_lock(&engine->mutex);
	old_revision = engine->authority_revision;
	udb_storage_mutex_unlock(&engine->mutex);
	if (udb_storage_engine_authority(engine, &revision) != UDB_STORAGE_OK)
		return;
	for (UdbNativeDelivery *item = udb_native_deliveries, *next; item; item = next)
	{
		next = item->next;
		/* Only a provisional, unsubmitted tail from this still-selected peer
		 * survives into the first INF-bound round. It grants no root lineage. */
		UdbHelloPeer *hello = peer ? udb_hello_peer(peer, 0) : NULL;
		if (!item->job && item->replay && !item->replay_round && !item->cancelled &&
			item->authority_revision == old_revision && peer && !strcmp(peer->id, item->peer_sid) && hello &&
			hello->remote_epoch_known && !strcmp(hello->remote_epoch, item->peer_epoch))
		{
			item->authority_revision = revision;
			continue;
		}
		if (item->job)
		{
			item->cancelled = 1;
			udb_storage_engine_cancel_owner(engine, item->owner);
		}
		else
			udb_native_delivery_drop(item);
	}
}

static void udb_native_mutation_cleanup(void)
{
	/* Lifecycle stops/joins the writer before dropping submitted envelopes. */
	while (udb_native_deliveries)
	{
		udb_storage_engine_cancel_owner(udb_native_deliveries->engine, udb_native_deliveries->owner);
		udb_native_delivery_drop(udb_native_deliveries);
	}
}

static int udb_native_mutation_ingress(UdbContext *ctx, Client *source, Client *peer, uint32_t kind, const char *epoch,
									   uint64_t sequence, const char *path, const char *value, char block,
									   int broadcast)
{
	if (!ctx || !ctx->storage || !source || !IsServer(source) || strlen(source->id) != 3 ||
		!udb_storage_sid_valid(source->id) || !udb_hello_epoch_valid(epoch) || !sequence ||
		!udb_is_authorized_sync_source(ctx, peer))
		return UDB_ERR_FORBIDDEN;
	UdbStorageEngine *engine = ctx->storage;
	UdbNativeReceiveScratch *scratch = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	unsigned char *payload = NULL;
	if (!scratch)
		return UDB_ERR_FATAL;
	size_t row_length = 0;
	UdbStorageResult result = UDB_STORAGE_OK;
	if (kind == UDB_STORAGE_JOB_DROP)
	{
		if (udb_storage_stage_block_index((unsigned char)block) < 0)
			result = UDB_STORAGE_REJECTED;
	}
	else
	{
		result = udb_native_key_parse(path, &scratch->key);
		if (result == UDB_STORAGE_OK && kind == UDB_STORAGE_JOB_POINT)
			result = udb_native_value_decode(value, &scratch->key.row, scratch->value, sizeof(scratch->value));
		UdbStorageRecord record = {0};
		record.row = scratch->key.row;
		record.operation = kind == UDB_STORAGE_JOB_POINT ? UDB_STORAGE_PUT : UDB_STORAGE_PREFIX_DELETE;
		record.stamp.sequence = 1;
		if (result == UDB_STORAGE_OK)
			result = udb_native_row_validate(&record, &ctx->native_schema);
		if (result == UDB_STORAGE_OK)
			result = udb_storage_row_encode(&record.row, scratch->bytes, sizeof(scratch->bytes), &row_length);
		block = (char)record.row.block;
	}
	if (result != UDB_STORAGE_OK)
	{
		udb_storage_free(scratch);
		return result == UDB_STORAGE_REJECTED ? UDB_ERR_PARAMS : UDB_ERR_FATAL;
	}
	int replay = udb_reconcile.active || !udb_ready;
	uint64_t number = udb_native_epoch_number(epoch);
	uint64_t expected = ctx->last_applied_seq;
	if (!replay)
	{
		if (!ctx->authority_epoch_known || strcmp(source->id, ctx->authority_sid) ||
			strcmp(epoch, ctx->authority_epoch))
		{
			udb_storage_free(scratch);
			udb_mutation_trigger_gap_recovery(peer);
			return UDB_ERR_NO_SYNC;
		}
		if (sequence <= expected)
		{
			udb_storage_free(scratch);
			return 0;
		}
	}
	else if (udb_reconcile.source_known)
	{
		if (udb_reconcile.authority_peer != peer || strcmp(source->id, udb_reconcile.source_sid) ||
			strcmp(epoch, udb_reconcile.source_epoch))
		{
			udb_storage_free(scratch);
			return UDB_ERR_FORBIDDEN;
		}
		expected = udb_reconcile.watermark_seq;
		if (sequence <= expected)
		{
			udb_storage_free(scratch);
			return 0;
		}
	}
	else
		expected = sequence - 1; /* Provisional until INF validates/prunes it. */
	for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
	{
		if (item->cancelled || item->local || item->replay != replay)
			continue;
		if (memcmp(item->bytes, source->id, 4) || udb_storage_get_u64(item->bytes + 4) != number ||
			strcmp(item->peer_sid, peer->id) || !udb_native_delivery_current(item))
		{
			udb_storage_free(scratch);
			return UDB_ERR_FORBIDDEN;
		}
		expected = udb_storage_get_u64(item->bytes + 12);
	}
	if (expected == UINT64_MAX || sequence != expected + 1)
	{
		udb_storage_free(scratch);
		udb_mutation_trigger_gap_recovery(peer);
		return UDB_ERR_NO_SYNC;
	}
	size_t length = UDB_STORAGE_MUTATION_HEADER + row_length;
	payload = udb_storage_alloc(&engine->budget, UDB_STORAGE_MEMORY_AUX, length);
	if (!payload)
	{
		udb_storage_free(scratch);
		return UDB_ERR_FATAL;
	}
	memcpy(payload, source->id, 4);
	udb_storage_put_u64(payload + 4, number);
	udb_storage_put_u64(payload + 12, sequence);
	payload[20] = (unsigned char)block;
	if (row_length)
		memcpy(payload + UDB_STORAGE_MUTATION_HEADER, scratch->bytes, row_length);
	UdbNativeDelivery *delivery = NULL;
	result = udb_native_delivery_create(engine, peer, kind, payload, length, broadcast, 0, replay, &delivery);
	udb_storage_free(payload);
	udb_storage_free(scratch);
	if (result != UDB_STORAGE_OK)
		return UDB_ERR_FATAL;
	if (replay && udb_reconcile.source_known)
		delivery->replay_round = udb_reconcile.round_id;
	if (!replay && udb_native_delivery_submit(delivery) != UDB_STORAGE_OK)
	{
		udb_native_delivery_drop(delivery);
		return UDB_ERR_FATAL;
	}
	if (replay && delivery->replay_round)
		udb_native_receive_replay_pump();
	return 0;
}

static void udb_mutation_ins(UdbContext *ctx, Client *source, Client *peer, const char *target, const char *epoch,
							 uint64_t sequence, const char *path, const char *value, int is_for_me, int broadcast)
{
	(void)target;
	int error = is_for_me ? udb_native_mutation_ingress(ctx, source, peer, UDB_STORAGE_JOB_POINT, epoch, sequence, path,
														value, 0, broadcast)
						  : UDB_ERR_FORBIDDEN;
	if (error)
		udb_protocol_mutation_error(source, "INS", error, path && *path ? *path : '0');
}

static void udb_mutation_del(UdbContext *ctx, Client *source, Client *peer, const char *target, const char *epoch,
							 uint64_t sequence, const char *path, int is_for_me, int broadcast)
{
	(void)target;
	int error = is_for_me ? udb_native_mutation_ingress(ctx, source, peer, UDB_STORAGE_JOB_PREFIX, epoch, sequence,
														path, NULL, 0, broadcast)
						  : UDB_ERR_FORBIDDEN;
	if (error)
		udb_protocol_mutation_error(source, "DEL", error, path && *path ? *path : '0');
}

static void udb_mutation_drp(UdbContext *ctx, Client *source, Client *peer, const char *target, const char *epoch,
							 uint64_t sequence, char block, int is_for_me, int broadcast)
{
	(void)target;
	int error = is_for_me ? udb_native_mutation_ingress(ctx, source, peer, UDB_STORAGE_JOB_DROP, epoch, sequence, NULL,
														NULL, block, broadcast)
						  : UDB_ERR_FORBIDDEN;
	if (error)
		udb_protocol_mutation_error(source, "DRP", error, block);
}

static int udb_mutation_expire_local(UdbContext *ctx, const char *path, time_t expected)
{
	UdbPropagatorSelection selected;
	if (!ctx || !ctx->storage || !udb_ready || udb_reconcile.active || expected <= 0 || expected > TStime() ||
		!ctx->authority_epoch_known || strcmp(ctx->authority_sid, me.id) ||
		(udb_propagator_policy_present(ctx) && (!udb_select_propagator(ctx, 1, &selected) || !selected.is_local)))
		return 0;
	UdbNativeReceiveScratch *scratch =
		udb_storage_alloc(&ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
		return 0;
	size_t row_length = 0;
	UdbStorageResult result = udb_native_key_parse(path, &scratch->key);
	if (result == UDB_STORAGE_OK && (scratch->key.row.block != 'K' || scratch->key.row.depth != 2))
		result = UDB_STORAGE_REJECTED;
	if (result == UDB_STORAGE_OK)
		result = udb_storage_row_encode(&scratch->key.row, scratch->bytes, sizeof(scratch->bytes), &row_length);
	size_t length = UDB_STORAGE_MUTATION_HEADER + 8 + row_length;
	unsigned char *bytes =
		result == UDB_STORAGE_OK ? udb_storage_alloc(&ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, length) : NULL;
	UdbNativeDelivery *delivery = NULL;
	if (bytes)
	{
		memcpy(bytes, me.id, 4);
		udb_storage_put_u64(bytes + 4, udb_native_epoch_number(ctx->authority_epoch));
		bytes[20] = 'K';
		bytes[21] = 1;
		udb_storage_put_u64(bytes + UDB_STORAGE_MUTATION_HEADER, (uint64_t)expected);
		memcpy(bytes + UDB_STORAGE_MUTATION_HEADER + 8, scratch->bytes, row_length);
		result =
			udb_native_delivery_create(ctx->storage, NULL, UDB_STORAGE_JOB_EXPIRE, bytes, length, 1, 1, 0, &delivery);
	}
	else
		result = UDB_STORAGE_LIMIT;
	udb_storage_free(bytes);
	udb_storage_free(scratch);
	if (result != UDB_STORAGE_OK)
		return 0;
	if (udb_native_delivery_submit(delivery) != UDB_STORAGE_OK)
	{
		udb_native_delivery_drop(delivery);
		return 0;
	}
	return 1;
}

static void udb_mutation_exp(UdbContext *ctx, Client *client, Client *peer, const char *target, const char *path,
							 time_t expected, int is_for_me, int broadcast)
{
	(void)target;
	if (!is_for_me || broadcast || client != peer || !udb_peer_authorizes_us(peer) || expected <= 0)
	{
		udb_protocol_mutation_error(client, "EXP", UDB_ERR_FORBIDDEN, 'K');
		return;
	}
	UdbPropagatorSelection selected = {0};
	if (udb_propagator_policy_present(ctx) && (!udb_select_propagator(ctx, 1, &selected) || !selected.is_local))
	{
		if (!selected.peer)
			return;
		UdbNativeKey *key =
			ctx && ctx->storage ? udb_storage_alloc(&ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*key)) : NULL;
		char pattern[UDB_STORAGE_COMPONENT_MAX + 1];
		if (key && udb_native_key_parse(path, key) == UDB_STORAGE_OK && key->row.block == 'K' && key->row.depth == 2 &&
			udb_native_span_string(key->row.components[1], pattern, sizeof(pattern)))
		{
			char type = (char)(key->row.components[0].data[0] - 'a' + 'A');
			UdbLineExpiryPending *pending = udb_line_expiry_pending_add(type, pattern, expected);
			if (pending && !pending->sent &&
				udb_send_db_to_one(selected.peer, ":%s DB %s EXP %s %" PRIu64, me.id, selected.peer->id, path,
								   (uint64_t)expected))
				pending->sent = 1;
		}
		udb_storage_free(key);
		return;
	}
	udb_mutation_expire_local(ctx, path, expected);
}

static void udb_native_mutation_relay(UdbNativeDelivery *delivery, UdbStorageJob *job)
{
	if (!delivery->broadcast || !udb_native_delivery_current(delivery))
		return;
	Client *except = delivery->local ? NULL : udb_ocl_find_server(delivery->peer_sid);
	char epoch[17];
	snprintf(epoch, sizeof(epoch), "%016" PRIx64, job->committed_source_epoch);
	if (job->kind == UDB_STORAGE_JOB_DROP)
	{
		udb_sendto_confirmed_servers(except, ":%s DB * DRP %s %" PRIu64 " %c", job->committed_source_sid, epoch,
									 job->committed_source_sequence,
									 *((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 20));
		return;
	}
	size_t offset = UDB_STORAGE_MUTATION_HEADER + (job->kind == UDB_STORAGE_JOB_EXPIRE ? 8 : 0);
	UdbStorageRow row;
	if (job->length <= offset ||
		udb_storage_row_decode(job->payload + offset, job->length - offset, &row) != UDB_STORAGE_OK)
		return;
	UdbNativeSendScratch *scratch = udb_storage_alloc(&job->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
	{
		udb_mutation_trigger_gap_recovery(except);
		return;
	}
	if (udb_native_key_path(&row, scratch->path, sizeof(scratch->path)) == UDB_STORAGE_OK)
	{
		if (job->kind == UDB_STORAGE_JOB_POINT &&
			udb_native_value_encode(&row, scratch->value, sizeof(scratch->value)) == UDB_STORAGE_OK)
			udb_sendto_confirmed_servers(except, ":%s DB * INS %s %" PRIu64 " %s :%s", job->committed_source_sid, epoch,
										 job->committed_source_sequence, scratch->path, scratch->value);
		else if (job->kind != UDB_STORAGE_JOB_POINT)
			udb_sendto_confirmed_servers(except, ":%s DB * DEL %s %" PRIu64 " %s", job->committed_source_sid, epoch,
										 job->committed_source_sequence, scratch->path);
	}
	udb_storage_free(scratch);
}

static int udb_native_local_bind(UdbContext *ctx)
{
	if (!ctx || !ctx->storage || udb_reconcile.active)
		return 0;
	UdbPropagatorSelection selected;
	if (udb_propagator_policy_present(ctx) && (!udb_select_propagator(ctx, 1, &selected) || !selected.is_local))
		return 0;
	for (UdbNativeDelivery *item = udb_native_deliveries; item; item = item->next)
		if (!item->cancelled && item->kind == UDB_STORAGE_JOB_BIND)
			return 1;
	unsigned char bytes[UDB_STORAGE_MUTATION_HEADER] = {0};
	uint64_t epoch = 0;
	if (RAND_bytes((unsigned char *)&epoch, sizeof(epoch)) != 1 || !epoch)
		return 0;
	memcpy(bytes, me.id, 4);
	udb_storage_put_u64(bytes + 4, epoch);
	UdbNativeDelivery *delivery = NULL;
	if (udb_native_delivery_create(ctx->storage, NULL, UDB_STORAGE_JOB_BIND, bytes, sizeof(bytes), 0, 1, 0,
								   &delivery) != UDB_STORAGE_OK)
		return 0;
	if (udb_native_delivery_submit(delivery) != UDB_STORAGE_OK)
	{
		udb_native_delivery_drop(delivery);
		return 0;
	}
	return 1;
}

static void udb_native_mutation_complete(UdbStorageJob *job)
{
	UdbNativeDelivery *delivery = udb_native_deliveries;
	while (delivery && (delivery->owner != job->owner || delivery->job != job->id))
		delivery = delivery->next;
	if (!delivery || delivery->engine != job->engine)
		return;
	int current = udb_native_delivery_current(delivery);
	if (job->result == UDB_STORAGE_OK && job->committed_generation && job->before && job->after)
	{
		udb_policy_notify_deferred = 1;
		UdbStorageResult result = udb_native_runtime_publication(job);
		const UdbStorageState *state = &job->after->state;
		memcpy(udb_ctx->authority_sid, state->source_sid, 4);
		snprintf(udb_ctx->authority_epoch, sizeof(udb_ctx->authority_epoch), "%016" PRIx64, state->source_epoch);
		udb_ctx->authority_epoch_known = 1;
		udb_ctx->last_applied_seq = state->source_sequence;
		udb_ctx->current_seq = !strcmp(state->source_sid, me.id) ? state->source_sequence : 0;
		udb_policy_notify_deferred = 0;
		udb_propagator_policy_flush(udb_ctx);
		if (result == UDB_STORAGE_OK && current && udb_native_delivery_current(delivery))
		{
			if (job->kind == UDB_STORAGE_JOB_BIND && !udb_reconcile.active)
			{
				udb_ready = 1;
				udb_last_successful_sync = time(NULL);
				udb_sync_status = UDB_SYNC_OK;
				udb_degraded_since = 0;
				udb_bootstrap_peer = NULL;
				udb_offer_reconciliation_to_downstreams();
			}
			else
				udb_native_mutation_relay(delivery, job);
		}
		else
		{
			udb_ready = 0;
			udb_sync_status = UDB_SYNC_DEGRADED;
			udb_native_mutation_abort(0);
			if (udb_reconcile.active)
				udb_reconcile_abort(udb_ctx, "native durable mutation effects unavailable", 1);
			udb_mutation_trigger_gap_recovery(NULL);
		}
	}
	else if (job->result != UDB_STORAGE_STAGED && !delivery->cancelled)
	{
		Client *peer = udb_ocl_find_server(delivery->peer_sid);
		udb_protocol_mutation_error(peer, job->kind == UDB_STORAGE_JOB_POINT ? "INS" : "DEL", UDB_ERR_FATAL,
									*((const unsigned char *)job + offsetof(UdbStorageJob, payload) + 20));
		udb_native_mutation_abort(0);
		if (udb_reconcile.active)
			udb_reconcile_abort(udb_ctx, "native live mutation failed", 1);
		udb_mutation_trigger_gap_recovery(peer);
	}
	udb_native_delivery_drop(delivery);
}

/* End of udb_mutation.c.inc */

/* S2S protocol handler: DB command parsing, routing, and server sync */
/* Inlined: udb_protocol.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: DB Frame Parsing, Routing & HEL Capability Negotiation
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static int udb_send_db_to_one(Client *to, const char *fmt, ...)
{
	char *line;
	va_list args;
	int n;

	if (!to)
		return 0;

	line = safe_alloc(UDB_S2S_LINE_MAX);
	va_start(args, fmt);
	n = vsnprintf(line, UDB_S2S_LINE_MAX, fmt, args);
	va_end(args);

	if (n < 0 || (size_t)n >= UDB_S2S_LINE_MAX)
	{
		udb_log(ULOG_ERROR, "UDB_S2S_OVERSIZE_FRAME", to,
				"Oversized S2S frame ($length bytes) discarded to prevent truncation", log_data_integer("length", n));
		safe_free(line);
		return 0;
	}

	sendto_one(to, NULL, "%s", line);
	safe_free(line);
	return 1;
}

/* Relay only to HEL-confirmed direct peers, excluding the incoming direction.
 * Reject an oversized frame as a whole rather than sending truncated grammar. */
static int udb_sendto_confirmed_servers(Client *except, const char *fmt, ...)
{
	char *line;
	va_list args;
	int n;

	line = safe_alloc(UDB_S2S_LINE_MAX);
	va_start(args, fmt);
	n = vsnprintf(line, UDB_S2S_LINE_MAX, fmt, args);
	va_end(args);

	if (n >= 0 && (size_t)n < UDB_S2S_LINE_MAX)
	{
		Client *server;
		list_for_each_entry(server, &server_list, special_node)
		{
			if (server == except || (except && server == except->direction))
				continue;
			if (IsServer(server) && MyConnect(server) && udb_has_hello(server))
				sendto_one(server, NULL, "%s", line);
		}
	}
	else
	{
		udb_log(ULOG_ERROR, "UDB_S2S_OVERSIZE_BROADCAST", except,
				"Oversized broadcast S2S frame ($length bytes) discarded to prevent truncation",
				log_data_integer("length", n));
		safe_free(line);
		return 0;
	}
	safe_free(line);
	return 1;
}

static unsigned long udb_error_correlation_sequence = 0;

static unsigned long udb_protocol_next_error_correlation(void)
{
	if (!++udb_error_correlation_sequence)
		++udb_error_correlation_sequence;
	return udb_error_correlation_sequence;
}

static void udb_protocol_round_error(Client *client, const char *subcmd, int error, unsigned long round_id, char letter)
{
	if (!client || !subcmd || !round_id)
		return;
	udb_send_db_to_one(client, ":%s DB %s ERR %s %d %lu %c", me.id, client->id, subcmd, error, round_id,
					   letter ? letter : '0');
}

static void udb_protocol_mutation_error(Client *client, const char *subcmd, int error, char letter)
{
	udb_protocol_round_error(client, subcmd, error, udb_protocol_next_error_correlation(), letter);
}

typedef struct UdbDbSubcommand
{
	const char *name;
	int affects_reconciliation;
} UdbDbSubcommand;

static const UdbDbSubcommand udb_db_subcommands[] = {
	{"HEL", 0}, {"BEGIN", 1}, {"PUT", 1}, {"EXP", 0}, {"END", 1},	   {"ERR", 0}, {"ACK", 0},	{"INF", 1},
	{"INS", 0}, {"RES", 1},	  {"DEL", 0}, {"DRP", 0}, {"MANIFEST", 1}, {"OCL", 0}, {"OCLG", 0},
};

static int udb_protocol_error_affects_reconciliation(const char *subcmd)
{
	if (!subcmd)
		return 0;
	for (size_t i = 0; i < sizeof(udb_db_subcommands) / sizeof(udb_db_subcommands[0]); i++)
		if (!strcasecmp(subcmd, udb_db_subcommands[i].name))
			return udb_db_subcommands[i].affects_reconciliation;
	return 0;
}

static int udb_sync_to_server(Client *server)
{
	return udb_native_sync_offer(server);
}
static Client *udb_direct_peer(Client *client)
{
	if (!client)
		return NULL;
	if (MyConnect(client))
		return client;
	if (client->direction && MyConnect(client->direction))
		return client->direction;
	return NULL;
}

static int udb_hook_server_sync(Client *client)
{
	if (!client || !IsServer(client) || !MyConnect(client))
		return 0;
	udb_sync_hello_start(client);
	return 0;
}

static int udb_hook_server_connect(Client *client)
{
	if (!client || !IsServer(client))
		return 0;
	/* The OCL participant set follows the full server list, including servers
	 * that arrive remotely during a netburst. */
	udb_ocl_membership_changed();
	return 0;
}

int udb_hook_server_quit(Client *client, MessageTag *mtags)
{
	udb_native_sync_cleanup(client);
	udb_sync_server_quit(client);
	return 0;
}

CMD_FUNC(cmd_db)
{
	if (parc < 4)
		return;

	const char *target = parv[1];
	const char *subcmd = parv[2];
	UdbContext *ctx = udb_ctx;
	Client *direct_peer = udb_direct_peer(client);
	char logbuf[512];

	if (!target || !*target || !subcmd || !*subcmd)
		return;

	/* HEL is the sole DB frame accepted before UDB capability confirmation. */
	if (!strcasecmp(subcmd, "HEL"))
	{
		if (!IsServer(client) || !MyConnect(client) || (strcmp(target, me.id) && strcmp(target, me.name)) || parc < 4 ||
			strcmp(parv[3], UDB_PROTOCOL_HEL))
			return;
		snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
		unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));
		if (parc >= 5 && !strcasecmp(parv[4], "ACK"))
		{
			int oclg_subscribe;
			if ((parc != 8 && parc != 9) || !parv[5] || !parv[6] || strcasecmp(parv[7], "OCL") ||
				(parc == 9 && strcasecmp(parv[8], "OCLG")))
				return;
			oclg_subscribe = parc == 9;
			if (!udb_hello_peer_advertisement(client, parv[5], parv[6], oclg_subscribe))
				return;
			udb_sync_hello_ack(client);
			return;
		}
		/* OCL is part of the mandatory HEL 4 contract: a request without the
		 * OCL token belongs to a legacy peer that can never be confirmed. */
		if (parc == 5 || (parc == 6 && !strcasecmp(parv[5], "OCL")) || (parc >= 7 && strcasecmp(parv[6], "OCL")))
		{
			if (parc == 5 || (parc >= 7 && strcasecmp(parv[6], "OCL")))
			{
				udb_log(ULOG_ERROR, "UDB_HEL_OCL_REQUIRED", client,
						"Link aborted: HEL " UDB_PROTOCOL_HEL " request without the mandatory OCL capability token");
				exit_client_fmt(client, NULL, "Link aborted: server does not support UDB OCL capability");
			}
			else
				udb_log(ULOG_ERROR, "UDB_HEL_EPOCH_REQUIRED", client,
						"HEL " UDB_PROTOCOL_HEL " request missing the mandatory instance epoch");
			return;
		}
		if (parc != 7 && parc != 8)
			return;
		if (parc == 8 && strcasecmp(parv[7], "OCLG"))
			return;
		if (!parv[4] || !*parv[4] || !parv[5] || !udb_hello_epoch_valid(parv[5]))
			return;
		{
			const char *prop = parv[4];
			const char *epoch = parv[5];
			int oclg_subscribe = (parc == 8);
			int new_auth = 0;

			if (!udb_hello_parse_propagator(prop, &new_auth))
				return;

			if (!udb_hello_peer_advertisement(client, prop, epoch, oclg_subscribe))
				return;

			udb_log(ULOG_INFO, "UDB_HEL_AUTHORIZATION", client,
					"Direct peer selected $propagator as its staged-sync source (authorizes_us=$auth, oclg=$oclg)",
					log_data_string("propagator", prop), log_data_integer("auth", new_auth),
					log_data_integer("oclg", oclg_subscribe));

			/* Each side sends its own request, so only an ACK confirms outbound data. */
			if (!udb_has_hello(client))
				udb_sync_hello_start(client);
			{
				UdbPropagatorSelection selected;
				const char *local_prop = udb_sync_hello_propagator(&selected);
				udb_send_db_to_one(client, ":%s DB %s HEL " UDB_PROTOCOL_HEL " ACK %s %s OCL", me.id, client->id,
								   local_prop, udb_ocl_epoch_value());
			}
			udb_ocl_maybe_replay_to_peer(client);
			/* A repeated authorized selection is also the explicit retry trigger. */
			udb_maybe_start_reconciliation(client, new_auth);
		}
		return;
	}

	if (!direct_peer || !udb_has_hello(direct_peer))
		return;

	snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
	unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));

	int is_broadcast = !strcmp(target, "*");
	int is_for_me = is_broadcast || !strcmp(target, me.id) || !strcmp(target, me.name);

	if (!is_broadcast && !is_for_me)
		return;

	switch (toupper((unsigned char)subcmd[0]))
	{
	case 'B':
		if (!strcasecmp(subcmd, "BEGIN"))
		{
			UdbBlock *block;
			unsigned long round_id = 0;
			uint64_t watermark_seq = 0;
			if (parc != 8 || !udb_strtoul_strict(parv[3], &round_id) || !round_id || client != direct_peer ||
				is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer) || !parv[4][0] || parv[4][1])
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (!udb_digest_parse(parv[6], NULL))
			{
				udb_protocol_round_error(client, "BEGIN", UDB_ERR_PARAMS, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (!udb_parse_uint64_strict(parv[7], &watermark_seq))
			{
				udb_protocol_round_error(client, "BEGIN", UDB_ERR_PARAMS, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (!udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_protocol_round_error(client, "BEGIN", UDB_ERR_FORBIDDEN, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[4]);
			if (!block)
			{
				if (is_for_me)
					udb_protocol_round_error(client, "BEGIN", UDB_ERR_NO_BLOCK, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (is_for_me)
			{
				if (block->session)
				{
					udb_protocol_round_error(client, "BEGIN", UDB_ERR_SYNC_ACTIVE, round_id, parv[4] ? *parv[4] : '0');
					return;
				}
				if (block->pending_from != direct_peer || block->pending_round_id != round_id)
				{
					udb_protocol_round_error(client, "BEGIN", UDB_ERR_NO_SYNC, round_id, parv[4] ? *parv[4] : '0');
					return;
				}
				int error = udb_sync_begin(block, direct_peer, round_id, parv[5], parv[6], watermark_seq);
				if (error)
				{
					udb_protocol_round_error(client, "BEGIN", error, round_id, parv[4] ? *parv[4] : '0');
					return;
				}
				if (!is_broadcast)
					return;
			}
			return;
		}
		break;

	case 'P':
		if (!strcasecmp(subcmd, "PUT"))
		{
			UdbBlock *block;
			int error;
			unsigned long round_id = 0;
			if (parc != 8 || !udb_strtoul_strict(parv[3], &round_id) || !round_id || client != direct_peer ||
				is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer))
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (!udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_protocol_round_error(client, "PUT", UDB_ERR_FORBIDDEN, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[4]);
			if (!block)
			{
				if (is_for_me)
					udb_protocol_round_error(client, "PUT", UDB_ERR_NO_BLOCK, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (is_for_me)
			{
				error = udb_sync_put(block, direct_peer, round_id, parv[5], parv[6], parv[7]);
				if (error)
				{
					udb_protocol_round_error(client, "PUT", error, round_id, parv[4] ? *parv[4] : '0');
					return;
				}
				if (!is_broadcast)
					return;
			}
			return;
		}
		break;

	case 'E':
		if (!strcasecmp(subcmd, "EXP"))
		{
			time_t expected_expires;
			if (parc != 5 || !udb_parse_time_t(parv[4], &expected_expires) || expected_expires <= 0)
			{
				udb_protocol_mutation_error(client, "EXP", UDB_ERR_PARAMS, 'K');
				return;
			}
			udb_mutation_exp(ctx, client, direct_peer, target, parv[3], expected_expires, is_for_me, is_broadcast);
			return;
		}
		if (!strcasecmp(subcmd, "END"))
		{
			UdbBlock *block;
			int error;
			unsigned long round_id = 0;
			uint64_t watermark_seq = 0;
			if (parc != 8 || !udb_strtoul_strict(parv[3], &round_id) || !round_id || client != direct_peer ||
				is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer) || !parv[4][0] || parv[4][1])
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (!udb_digest_parse(parv[6], NULL))
			{
				udb_protocol_round_error(client, "END", UDB_ERR_PARAMS, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (!udb_parse_uint64_strict(parv[7], &watermark_seq))
			{
				udb_protocol_round_error(client, "END", UDB_ERR_PARAMS, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (!udb_is_authorized_sync_source(ctx, direct_peer))
			{
				udb_protocol_round_error(client, "END", UDB_ERR_FORBIDDEN, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[4]);
			if (!block)
			{
				if (is_for_me)
					udb_protocol_round_error(client, "END", UDB_ERR_NO_BLOCK, round_id, parv[4] ? *parv[4] : '0');
				return;
			}
			if (is_for_me)
			{
				error = udb_sync_end(ctx, block, direct_peer, round_id, parv[5], parv[6], watermark_seq);
				if (error)
				{
					udb_protocol_round_error(client, "END", error, round_id, parv[4] ? *parv[4] : '0');
					return;
				}
				/* END is acknowledged only by its native durable completion. */
				if (!is_broadcast)
					return;
			}
			return;
		}
		else if (!strcasecmp(subcmd, "ERR"))
		{
			unsigned long errcode;
			unsigned long error_round;
			UdbBlock *eblock;
			int active_authority_matches;

			if (parc != 7 || !udb_strtoul_strict(parv[4], &errcode) || errcode > 255 ||
				!udb_strtoul_strict(parv[5], &error_round) || !error_round || !parv[6] || !*parv[6] || parv[6][1])
				return;
			if (is_for_me)
			{
				udb_log(ULOG_INFO, "UDB_EVENT", client, "Error from $client: cmd=$cmd err=$errcode",
						log_data_client("client", client), log_data_string("cmd", parv[3]),
						log_data_integer("errcode", (int)errcode));

				if (!strcasecmp(parv[3], "MANIFEST"))
				{
					if (client == direct_peer && !is_broadcast && udb_anti_entropy.pending &&
						udb_anti_entropy.round_id == error_round)
					{
						udb_anti_entropy_handle_err(direct_peer, error_round, (int)errcode);
						if (!is_broadcast)
							return;
					}
				}

				eblock = udb_block_by_letter(ctx, *parv[6]);
				active_authority_matches = client == direct_peer && !is_broadcast && eblock &&
										   udb_protocol_error_affects_reconciliation(parv[3]) && udb_reconcile.active &&
										   udb_reconcile.authority_peer == direct_peer &&
										   udb_reconcile.round_id == error_round;
				if (!active_authority_matches)
				{
					udb_log(ULOG_WARNING, "UDB_STALE_ERR_IGNORED", client,
							"Ignored ERR for stale round $round (active round is $active_round)",
							log_data_integer("round", (int)error_round),
							log_data_integer("active_round", (int)udb_reconcile.round_id));
					return;
				}
				udb_reconcile_abort(ctx, "authority returned ERR", 1);
				if (!is_broadcast)
					return;
			}
		}
		break;

	case 'A':
		if (!strcasecmp(subcmd, "ACK"))
		{
			unsigned long round_id = 0;
			uint64_t watermark_seq = 0;
			if (parc != 8 || !udb_strtoul_strict(parv[3], &round_id) || !round_id || client != direct_peer ||
				is_broadcast || !is_for_me || !udb_has_staged_sync(direct_peer) || !parv[4][0] || parv[4][1])
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (!udb_parse_uint64_strict(parv[7], &watermark_seq))
			{
				udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (!udb_digest_parse(parv[6], NULL))
			{
				udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			if (is_for_me)
			{
				if (parv[4][0] && !parv[4][1] &&
					udb_native_sync_ack(direct_peer, round_id, parv[4][0], parv[5], parv[6], watermark_seq))
					udb_sync_ack(direct_peer, parv[4]);
				if (!is_broadcast)
					return;
			}
			return;
		}
		break;

	case 'I':
		if (!strcasecmp(subcmd, "INF"))
		{
			unsigned long round_id = 0;
			uint64_t watermark_seq = 0;
			uint64_t remote_count = 0;
			char remote_sha[UDB_SHA256_HEX_LEN + 1];
			time_t remote_ts = 0;

			if (parc != 11 || !udb_strtoul_strict(parv[3], &round_id) || !round_id)
			{
				if (round_id)
					udb_protocol_round_error(client, subcmd, UDB_ERR_PARAMS, round_id, parc > 4 ? *parv[4] : '0');
				return;
			}
			char letter = *parv[4];
			UdbBlock *block = udb_block_by_letter(ctx, letter);

			if (!block)
			{
				if (is_for_me)
					udb_protocol_round_error(client, "INF", UDB_ERR_NO_BLOCK, round_id, letter);
				return;
			}
			if (!udb_digest_parse(parv[5], remote_sha) || !udb_parse_uint64_strict(parv[6], &remote_count) ||
				!udb_timestamp_parse(parv[7], &remote_ts))
			{
				udb_protocol_round_error(client, "INF", UDB_ERR_PARAMS, round_id, letter);
				return;
			}
			if (!udb_parse_uint64_strict(parv[8], &watermark_seq) || strlen(parv[9]) != 3 ||
				!udb_storage_sid_valid(parv[9]) || !udb_hello_epoch_valid(parv[10]) || !parv[4][0] || parv[4][1])
			{
				udb_protocol_round_error(client, "INF", UDB_ERR_PARAMS, round_id, letter);
				return;
			}
			(void)remote_ts;

			if (client == direct_peer && is_for_me && udb_is_authorized_sync_source(ctx, direct_peer))
			{
				int error = udb_native_receive_inf(direct_peer, round_id, letter, remote_sha, remote_count, parv[9],
												   parv[10], watermark_seq);
				if (error)
				{
					udb_protocol_round_error(client, "INF", error, round_id, letter);
					udb_reconcile_abort(ctx, "native INF admission rejected", 1);
					return;
				}
			}
			udb_reconcile_check(ctx);
			if (!is_broadcast)
				return;
		}
		/* Reconciliation inventory is hop-by-hop and is never forwarded. */
		else if (!strcasecmp(subcmd, "INS"))
		{
			uint64_t seq = 0;
			if (parc != 7 || !udb_hello_epoch_valid(parv[3]) || !udb_parse_uint64_strict(parv[4], &seq) || seq == 0)
			{
				udb_protocol_mutation_error(client, subcmd, UDB_ERR_PARAMS, '0');
				return;
			}
			udb_mutation_ins(ctx, client, direct_peer, target, parv[3], seq, parv[5], parv[6], is_for_me, is_broadcast);
		}
		break;

	case 'R':
		if (!strcasecmp(subcmd, "RES"))
		{
			unsigned long round_id = 0;
			if (parc != 5 || !udb_strtoul_strict(parv[3], &round_id) || !round_id)
				return;
			char letter = *parv[4];
			UdbBlock *block = udb_block_by_letter(ctx, letter);
			if (client != direct_peer || is_broadcast || !is_for_me || !udb_ready)
			{
				udb_protocol_round_error(client, "RES", UDB_ERR_FORBIDDEN, round_id, letter);
				return;
			}
			if (!udb_peer_authorizes_us(direct_peer))
			{
				udb_protocol_round_error(client, "RES", UDB_ERR_FORBIDDEN, round_id, letter);
				return;
			}

			if (is_for_me)
			{
				if (!block)
				{
					udb_protocol_round_error(client, "RES", UDB_ERR_NO_BLOCK, round_id, letter);
					return;
				}
				if (block->session || (block->syncing_from && block->syncing_from != direct_peer))
				{
					udb_protocol_round_error(client, "RES", UDB_ERR_SYNC_ACTIVE, round_id, letter);
					return;
				}

				if (!udb_sync_send_stage(direct_peer, block, round_id))
					udb_protocol_round_error(client, "RES", UDB_ERR_FATAL, round_id, letter);

				if (!is_broadcast)
					return;
			}
			return;
		}
		break;

	case 'D':
		if (!strcasecmp(subcmd, "DEL"))
		{
			uint64_t seq = 0;
			if (parc != 6 || !udb_hello_epoch_valid(parv[3]) || !udb_parse_uint64_strict(parv[4], &seq) || seq == 0)
			{
				udb_protocol_mutation_error(client, subcmd, UDB_ERR_PARAMS, '0');
				return;
			}
			udb_mutation_del(ctx, client, direct_peer, target, parv[3], seq, parv[5], is_for_me, is_broadcast);
		}
		else if (!strcasecmp(subcmd, "DRP"))
		{
			uint64_t seq = 0;
			if (parc != 6 || !udb_hello_epoch_valid(parv[3]) || !udb_parse_uint64_strict(parv[4], &seq) || seq == 0 ||
				!parv[5] || strlen(parv[5]) != 1)
			{
				udb_protocol_mutation_error(client, subcmd, UDB_ERR_PARAMS, '0');
				return;
			}
			udb_mutation_drp(ctx, client, direct_peer, target, parv[3], seq, *parv[5], is_for_me, is_broadcast);
		}
		break;

	case 'M':
		if (!strcasecmp(subcmd, "MANIFEST"))
		{
			if (parc < 5)
				return;
			const char *action = parv[3];
			unsigned long round_id = 0;
			if (!udb_strtoul_strict(parv[4], &round_id) || !round_id)
				return;

			if (!strcasecmp(action, "REQ"))
			{
				if (parc != 5 || client != direct_peer || is_broadcast || !is_for_me || !udb_ready)
				{
					if (is_for_me && round_id)
						udb_protocol_round_error(client, "MANIFEST", UDB_ERR_FORBIDDEN, round_id, '0');
					return;
				}
				if (!udb_peer_authorizes_us(direct_peer))
				{
					udb_protocol_round_error(client, "MANIFEST", UDB_ERR_FORBIDDEN, round_id, '0');
					return;
				}
				if (!udb_native_sync_offer_round(client, round_id, 1))
					udb_protocol_round_error(client, "MANIFEST", UDB_ERR_FATAL, round_id, '0');
				return;
			}
			else if (!strcasecmp(action, "ACK"))
			{
				if (parc != 9 || client != direct_peer || is_broadcast || !is_for_me)
					return;
				if (!udb_is_authorized_sync_source(ctx, direct_peer))
					return;

				char letter = *parv[5];
				uint64_t remote_count = 0;
				char remote_sha[UDB_SHA256_HEX_LEN + 1];
				uint64_t watermark_seq = 0;

				if (!udb_parse_uint64_strict(parv[6], &remote_count) || !parv[5][0] || parv[5][1] ||
					!udb_digest_parse(parv[7], remote_sha) || !udb_parse_uint64_strict(parv[8], &watermark_seq))
				{
					return;
				}

				udb_anti_entropy_record_ack(direct_peer, round_id, letter, remote_count, remote_sha, watermark_seq);
				return;
			}
		}
		break;

	case 'O':
		if (!strcasecmp(subcmd, "OCL"))
		{
			udb_ocl_handle(client, direct_peer, parc, parv, is_broadcast);
		}
		else if (!strcasecmp(subcmd, "OCLG"))
		{
			udb_oclg_handle(client, is_for_me);
		}
		break;
	}
}

static int udb_protocol_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DB", cmd_db, MAXPARA, CMD_SERVER | CMD_BIGLINES);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, udb_hook_server_sync);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_CONNECT, 0, udb_hook_server_connect);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, udb_config_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_POSTCONF, 0, udb_config_postconf);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH_COMPLETE, 0, udb_config_postconf);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, udb_hook_server_quit);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, -100, udb_hook_readiness_pre_connect);
	EventAdd(modinfo->handle, "udb_sync_timeout", udb_sync_timeout_event, NULL, 1000, 0);
	EventAdd(modinfo->handle, "udb_native_snapshot", udb_native_sync_tick_event, NULL, 10, 0);
	EventAdd(modinfo->handle, "udb_native_completions", udb_native_receive_tick_event, NULL, 10, 0);

	return 0;
}

/* End of udb_protocol.c.inc */

/* Nick management: registration, identification, ghost, vhost, oper */
/* Inlined: udb_nicks.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Nick Authentication, Identity & Runtime Effect Ownership
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

#include <openssl/evp.h>

typedef struct UdbNickPasswordCache UdbNickPasswordCache;
struct UdbNickPasswordCache
{
	char nick[NICKLEN + 1];
	char *pass;
	int validated;
};

/* Active UDB identity marker. It exists only while an authenticated profile is
 * bound to the current nick; it is destroyed by suspend and by leaving the
 * nick, and is never reconstructed after the fact. Credential and access
 * values are re-checked live on every use: changing pass, or an access list
 * that still permits the holder, does not revoke an active identity. */
typedef struct UdbNickIdentity UdbNickIdentity;
struct UdbNickIdentity
{
	char nick[NICKLEN + 1];
};

/* Preflight password/access success for a destination nick. It never grants
 * identity: only a confirmed nick change promotes it to active identity. */
typedef struct UdbNickPendingAuth UdbNickPendingAuth;
struct UdbNickPendingAuth
{
	char nick[NICKLEN + 1];
	char policy_digest[65];
};

/* Runtime effect ownership marker. It records exactly which runtime state UDB
 * changed for the current nick. Profile records describe desired state and can
 * never be used to reconstruct what UDB applied in the past: external sources
 * may have replaced a value UDB once set. */
typedef struct UdbNickEffects UdbNickEffects;
struct UdbNickEffects
{
	char nick[NICKLEN + 1];
	long modes_added;
	int vhost_owned;
	char applied_vhost[HOSTLEN + 1];
	int oper_vhost_owned;
	char *oper_previous_vhost;
	char *oper_applied_vhost;
	char oper_previous_username[USERLEN + 1];
	char oper_applied_username[USERLEN + 1];
	int oper_username_owned;
	int oper_previous_sethost;
	int oper_vhost_attempted;
	int snomask_owned;
	char *previous_snomask;
	char *applied_snomask;
	char *applied_snomask_expr;
	int swhois_owned;
};

static ModDataInfo *udb_nick_password_cache_md = NULL;
static ModDataInfo *udb_nick_identity_md = NULL;
static ModDataInfo *udb_nick_pending_auth_md = NULL;
static ModDataInfo *udb_nick_effects_md = NULL;
static ModDataInfo *udb_nick_oper_owned_md = NULL;
/* Only set during a synchronous N-tree replacement; never retained per client. */
static int udb_nick_rehash_keep_identity = 0;
/* Foreground-only scope for hooks re-entered by a publication's SVSNICK.
 * The caller retains the exact committed generation until this scope ends. */
static Client *udb_nick_publication_client = NULL;
static UdbStorageRead *udb_nick_publication_read = NULL;

static void udb_nick_password_cache_entry_free(UdbNickPasswordCache *cache)
{
	if (!cache)
		return;
	safe_free_sensitive(cache->pass);
	safe_free(cache);
}

static void udb_nick_password_cache_free(ModData *m)
{
	udb_nick_password_cache_entry_free(m->ptr);
	m->ptr = NULL;
}

static void udb_nick_password_cache_clear(Client *client)
{
	UdbNickPasswordCache *cache;

	if (!client || !udb_nick_password_cache_md)
		return;
	cache = moddata_local_client(client, udb_nick_password_cache_md).ptr;
	if (cache)
	{
		udb_nick_password_cache_entry_free(cache);
		moddata_local_client(client, udb_nick_password_cache_md).ptr = NULL;
	}
}

/* One-shot /NICK credential: plaintext lives only until CAN_USE_NICK consumes
 * it, and never becomes session state. */
static void udb_nick_password_cache_set(Client *client, const char *nick, const char *pass, int validated)
{
	UdbNickPasswordCache *cache;

	if (!client || !nick || !pass || !udb_nick_password_cache_md)
		return;
	udb_nick_password_cache_clear(client);
	cache = safe_alloc(sizeof(*cache));
	strlcpy(cache->nick, nick, sizeof(cache->nick));
	safe_strdup_sensitive(cache->pass, pass);
	cache->validated = validated ? 1 : 0;
	moddata_local_client(client, udb_nick_password_cache_md).ptr = cache;
}

static UdbNickPasswordCache *udb_nick_password_cache_take(Client *client, const char *nick)
{
	UdbNickPasswordCache *cache;
	ModData *m;

	if (!client || !nick || !udb_nick_password_cache_md)
		return NULL;
	m = &moddata_local_client(client, udb_nick_password_cache_md);
	cache = m->ptr;
	if (!cache)
		return NULL;
	if (strcasecmp(cache->nick, nick))
	{
		udb_nick_password_cache_clear(client);
		return NULL;
	}
	m->ptr = NULL;
	return cache;
}

static const char *const udb_nick_fields[] = {NKEY_ACCESS, NKEY_PASS,  NKEY_VHOST,	  NKEY_FORBID, NKEY_SUSPEND,
											  NKEY_OPER,   NKEY_MODES, NKEY_SNOMASKS, NKEY_SWHOIS};

static UdbStorageField *udb_nick_field(UdbStorageProfile *profile, const char *name)
{
	if (!profile || profile->key.block != 'N' || profile->count != 9 || !name)
		return NULL;
	for (unsigned int i = 0; i < 9; i++)
		if (!strcmp(udb_nick_fields[i], name))
			return profile->fields[i].present ? profile->fields + i : NULL;
	return NULL;
}

static UdbStorageResult udb_native_nick_load(const char *nick, UdbStorageProfile **out)
{
	if (!out)
		return UDB_STORAGE_ERROR;
	*out = NULL;
	if (!udb_ready)
		return UDB_STORAGE_ERROR;
	return udb_native_profile_load(udb_ctx, 'N', nick, udb_nick_fields, 9, out);
}

static void udb_nick_identity_free(ModData *m)
{
	safe_free(m->ptr);
	m->ptr = NULL;
}

static void udb_nick_identity_clear(Client *client)
{
	UdbNickIdentity *identity;

	if (!client || !udb_nick_identity_md)
		return;
	identity = moddata_local_client(client, udb_nick_identity_md).ptr;
	if (identity)
	{
		safe_free(identity);
		moddata_local_client(client, udb_nick_identity_md).ptr = NULL;
	}
}

/* Feed distinct labels, presence bits and fixed-width lengths to SHA-256 so
 * distinct pass/access policies cannot share an ambiguous encoding. */
static int udb_nick_auth_policy_digest_field(EVP_MD_CTX *ctx, char label, const char *value)
{
	unsigned char present = value ? 1 : 0;
	unsigned char lenbuf[4];
	size_t len = value ? strlen(value) : 0;

	if (len > UDB_RECORD_VALUE_MAX)
		return 0;
	lenbuf[0] = (unsigned char)(len >> 24);
	lenbuf[1] = (unsigned char)(len >> 16);
	lenbuf[2] = (unsigned char)(len >> 8);
	lenbuf[3] = (unsigned char)len;
	return EVP_DigestUpdate(ctx, &label, sizeof(label)) == 1 && EVP_DigestUpdate(ctx, &present, sizeof(present)) == 1 &&
		   EVP_DigestUpdate(ctx, lenbuf, sizeof(lenbuf)) == 1 && (!len || EVP_DigestUpdate(ctx, value, len) == 1);
}

/* Binds a one-shot pending credential to the exact pass/access policy that was
 * validated. Active identity deliberately does not use this digest: changing
 * pass, or an access list that still permits the holder, must not revoke it. */
static int udb_nick_auth_policy_digest(UdbStorageProfile *nick_rec, char out[65])
{
	EVP_MD_CTX *ctx;
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digestlen = 0;
	UdbStorageField *rec;
	const char *pass = NULL;
	const char *access = NULL;
	unsigned int i;

	if (!nick_rec || !out)
		return 0;
	rec = udb_nick_field(nick_rec, NKEY_PASS);
	if (!rec || BadPtr(rec->string))
		return 0;
	pass = rec->string;
	rec = udb_nick_field(nick_rec, NKEY_ACCESS);
	if (rec)
		access = rec->string;
	ctx = EVP_MD_CTX_new();
	if (!ctx)
		return 0;
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 || !udb_nick_auth_policy_digest_field(ctx, 'P', pass) ||
		!udb_nick_auth_policy_digest_field(ctx, 'A', access) || EVP_DigestFinal_ex(ctx, digest, &digestlen) != 1 ||
		digestlen != 32)
	{
		EVP_MD_CTX_free(ctx);
		return 0;
	}
	EVP_MD_CTX_free(ctx);
	for (i = 0; i < digestlen; i++)
		snprintf(out + (i * 2), 3, "%02x", digest[i]);
	out[64] = '\0';
	return 1;
}

/* Having pass means UDB may own identity effects; it never means the current
 * holder is authenticated. Passless profiles can never have applied effects. */
static int udb_nick_profile_has_pass(UdbStorageProfile *nick_rec)
{
	return nick_rec && udb_nick_field(nick_rec, NKEY_PASS) != NULL;
}

/* Called only after a confirmed nick change or initial registration consumes
 * valid pending auth. Policy checks and revocation belong to the callers;
 * this setter must never turn a matching nick or +r into authentication. */
static void udb_nick_identity_set(Client *client, UdbStorageProfile *nick_rec)
{
	UdbNickIdentity *identity;

	if (!client || !MyConnect(client) || !nick_rec || BadPtr(nick_rec->entity) ||
		!udb_nick_profile_has_pass(nick_rec) || !udb_nick_identity_md)
		return;
	identity = safe_alloc(sizeof(*identity));
	strlcpy(identity->nick, nick_rec->entity, sizeof(identity->nick));
	udb_nick_identity_clear(client);
	moddata_local_client(client, udb_nick_identity_md).ptr = identity;
}

static int udb_nick_identity_valid(Client *client, UdbStorageProfile *nick_rec)
{
	UdbNickIdentity *identity;

	if (!client || !nick_rec || BadPtr(nick_rec->entity) || !udb_nick_identity_md)
		return 0;
	identity = moddata_local_client(client, udb_nick_identity_md).ptr;
	if (!identity || strcasecmp(identity->nick, nick_rec->entity) || strcasecmp(client->name, identity->nick) ||
		!udb_nick_profile_has_pass(nick_rec) || !udb_nick_access_allowed(client, nick_rec))
		return 0;
	return 1;
}

static int udb_nick_identity_present(Client *client)
{
	if (!client || !MyUser(client) || !udb_nick_identity_md)
		return 0;
	return moddata_local_client(client, udb_nick_identity_md).ptr != NULL;
}

/* Identity revocation removes only the public projection UDB materialized:
 * account, +r and the identity marker. It never touches effects, and it leaves
 * externally supplied account/+r alone when UDB never held an identity. */
static void udb_nick_revoke_identity(Client *client)
{
	long old_umodes;

	if (!client || !MyUser(client))
		return;
	if (!udb_nick_identity_present(client))
		return;
	if (client->user)
		strlcpy(client->user->account, "*", sizeof(client->user->account));
	old_umodes = client->umodes & ALL_UMODES;
	client->umodes &= ~UMODE_REGNICK;
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
	udb_nick_identity_clear(client);
}

static void udb_nick_pending_auth_free(ModData *m)
{
	safe_free(m->ptr);
	m->ptr = NULL;
}

static void udb_nick_pending_auth_clear(Client *client)
{
	UdbNickPendingAuth *auth;

	if (!client || !udb_nick_pending_auth_md)
		return;
	auth = moddata_local_client(client, udb_nick_pending_auth_md).ptr;
	if (auth)
	{
		safe_free(auth);
		moddata_local_client(client, udb_nick_pending_auth_md).ptr = NULL;
	}
}

static void udb_nick_pending_auth_set(Client *client, UdbStorageProfile *nick_rec)
{
	UdbNickPendingAuth *auth;

	if (!client || !MyConnect(client) || !nick_rec || BadPtr(nick_rec->entity) ||
		!udb_nick_profile_has_pass(nick_rec) || !udb_nick_pending_auth_md)
		return;
	auth = safe_alloc(sizeof(*auth));
	if (!udb_nick_auth_policy_digest(nick_rec, auth->policy_digest))
	{
		safe_free(auth);
		return;
	}
	strlcpy(auth->nick, nick_rec->entity, sizeof(auth->nick));
	udb_nick_pending_auth_clear(client);
	moddata_local_client(client, udb_nick_pending_auth_md).ptr = auth;
}

static int udb_nick_pending_auth_valid(Client *client, UdbStorageProfile *nick_rec)
{
	UdbNickPendingAuth *auth;
	char digest[65];

	if (!client || !nick_rec || BadPtr(nick_rec->entity) || !udb_nick_pending_auth_md)
		return 0;
	auth = moddata_local_client(client, udb_nick_pending_auth_md).ptr;
	if (!auth || strcasecmp(auth->nick, nick_rec->entity) || strcasecmp(client->name, auth->nick) ||
		!udb_nick_profile_has_pass(nick_rec) || !udb_nick_auth_policy_digest(nick_rec, digest) ||
		strcmp(auth->policy_digest, digest) || !udb_nick_access_allowed(client, nick_rec))
		return 0;
	return 1;
}

/* The nick change is already confirmed: the preflight credential is consumed
 * and can only become active identity through the caller. Invalid or stale
 * pending state is always discarded without granting anything, and it never
 * survives as suspended proof. */
static int udb_nick_pending_auth_consume(Client *client, UdbStorageProfile *nick_rec)
{
	if (!udb_nick_pending_auth_valid(client, nick_rec))
	{
		udb_nick_pending_auth_clear(client);
		return 0;
	}
	udb_nick_pending_auth_clear(client);
	return 1;
}

/* A pending credential only survives for the attempt that created it. Once a
 * local NICK command finishes without establishing the requested nick, the
 * credential can never be reused by a later forced change such as SVSNICK. */
static void udb_nick_pending_auth_expire(Client *client, const char *requested_nick)
{
	if (!client || !MyConnect(client) || IsDead(client) || !IsUser(client))
		return;
	if (!requested_nick || strcasecmp(client->name, requested_nick))
		udb_nick_pending_auth_clear(client);
}

static UdbNickEffects *udb_nick_effects_get(Client *client)
{
	if (!client || !MyUser(client) || !udb_nick_effects_md)
		return NULL;
	return moddata_local_client(client, udb_nick_effects_md).ptr;
}

static UdbNickEffects *udb_nick_effects_ensure(Client *client)
{
	UdbNickEffects *fx;

	if (!client || !MyUser(client) || !udb_nick_effects_md)
		return NULL;
	fx = moddata_local_client(client, udb_nick_effects_md).ptr;
	if (!fx)
	{
		fx = safe_alloc(sizeof(*fx));
		moddata_local_client(client, udb_nick_effects_md).ptr = fx;
	}
	strlcpy(fx->nick, client->name, sizeof(fx->nick));
	return fx;
}

static void udb_nick_effects_free(ModData *m)
{
	UdbNickEffects *fx = m ? m->ptr : NULL;

	if (!fx)
		return;
	safe_free(fx->previous_snomask);
	safe_free(fx->applied_snomask);
	safe_free(fx->applied_snomask_expr);
	safe_free(fx->oper_previous_vhost);
	safe_free(fx->oper_applied_vhost);
	safe_free(fx);
	m->ptr = NULL;
}

static void udb_nick_effects_clear(Client *client)
{
	if (!client || !MyUser(client) || !udb_nick_effects_md)
		return;
	if (moddata_local_client(client, udb_nick_effects_md).ptr)
	{
		udb_nick_effects_free(&moddata_local_client(client, udb_nick_effects_md));
		moddata_local_client(client, udb_nick_effects_md).ptr = NULL;
	}
}

static void udb_nick_effects_revoke_modes(Client *client);
static void udb_nick_effects_revoke_vhost(Client *client);
static void udb_nick_effects_revoke_snomasks(Client *client);
static void udb_nick_effects_revoke_swhois(Client *client);

/* Ownership of a user mode is exactly the set of bits UDB changed from 0 to 1.
 * Bits that were already active belong to another source and must survive
 * every revoke, even when the profile also lists them. */
static void udb_nick_effects_apply_modes(Client *client, UdbStorageField *mode_rec)
{
	UdbNickEffects *fx;
	long desired;
	long old_umodes;
	long removed;
	long added;

	if (!client || !mode_rec || BadPtr(mode_rec->string) || !MyUser(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	/* Host privacy and custom-host state are not revocable profile modes. */
	desired = set_usermode(mode_rec->string) & ~(UMODE_OPER | UMODE_HIDE | UMODE_SETHOST);
	old_umodes = client->umodes & ALL_UMODES;
	removed = fx->modes_added & ~desired & ~(UMODE_HIDE | UMODE_SETHOST);
	added = desired & ~old_umodes;
	client->umodes = (client->umodes & ~removed) | desired;
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
	fx->modes_added = (fx->modes_added & desired) | added;
}

static void udb_nick_effects_revoke_modes(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);
	long old_umodes;

	if (!fx || !fx->modes_added)
		return;
	old_umodes = client->umodes & ALL_UMODES;
	client->umodes &= ~(fx->modes_added & ~(UMODE_HIDE | UMODE_SETHOST));
	fx->modes_added = 0;
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
}

/* UnrealIRCd stores snomasks as a sorted, de-duplicated letter set. Canonical
 * form lets us compare a configured value with the live value without churn. */
static void udb_nick_snomask_materialize(Client *client, const char *value)
{
	long old_umodes = client->umodes & ALL_UMODES;

	set_snomask(client, NULL);
	if (value && *value)
		set_snomask(client, value);
	if (client->user->snomask && *client->user->snomask)
	{
		client->umodes |= UMODE_SERVNOTICE;
		if (MyUser(client))
			sendnumeric(client, RPL_SNOMASK, client->user->snomask);
	}
	else
	{
		client->umodes &= ~UMODE_SERVNOTICE;
	}
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
}

static void udb_nick_snomask_apply_expression(Client *client, const char *expr)
{
	long old_umodes = client->umodes & ALL_UMODES;

	if (expr && *expr)
		set_snomask(client, expr);
	if (client->user->snomask && *client->user->snomask)
	{
		client->umodes |= UMODE_SERVNOTICE;
		if (MyUser(client))
			sendnumeric(client, RPL_SNOMASK, client->user->snomask);
	}
	else
	{
		client->umodes &= ~UMODE_SERVNOTICE;
	}
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
}

/* The previous value is preserved so revocation can restore an external
 * snomask source; a value another source changed while UDB owned it is never
 * overwritten on revoke. */
static void udb_nick_effects_apply_snomasks(Client *client, UdbStorageField *snomask_rec)
{
	UdbNickEffects *fx;
	const char *expr;
	const char *current;

	if (!client || !client->user || !snomask_rec || BadPtr(snomask_rec->string) || !MyUser(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	expr = snomask_rec->string;
	current = client->user->snomask ? client->user->snomask : "";

	if (fx->snomask_owned && fx->applied_snomask_expr && !strcmp(fx->applied_snomask_expr, expr))
	{
		const char *applied = fx->applied_snomask ? fx->applied_snomask : "";
		if (!strcmp(current, applied))
			return;
	}

	if (fx->snomask_owned)
	{
		/* The profile changed its desired value: release the old ownership
		 * first so an external replacement is preserved, not destroyed. */
		udb_nick_effects_revoke_snomasks(client);
		fx = udb_nick_effects_ensure(client);
		if (!fx)
			return;
	}

	safe_free(fx->previous_snomask);
	fx->previous_snomask = client->user->snomask ? strdup(client->user->snomask) : NULL;
	udb_nick_snomask_apply_expression(client, expr);
	safe_free(fx->applied_snomask);
	fx->applied_snomask = client->user->snomask ? strdup(client->user->snomask) : NULL;
	safe_free(fx->applied_snomask_expr);
	fx->applied_snomask_expr = strdup(expr);
	fx->snomask_owned = 1;
}

static void udb_nick_effects_revoke_snomasks(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);
	const char *current;
	const char *applied;

	if (!fx || !fx->snomask_owned)
		return;
	current = client->user && client->user->snomask ? client->user->snomask : "";
	applied = fx->applied_snomask ? fx->applied_snomask : "";
	if (!strcmp(current, applied))
		udb_nick_snomask_materialize(client, fx->previous_snomask ? fx->previous_snomask : "");
	/* Otherwise another source changed the value: preserve it. */
	safe_free(fx->previous_snomask);
	safe_free(fx->applied_snomask);
	safe_free(fx->applied_snomask_expr);
	fx->previous_snomask = NULL;
	fx->applied_snomask = NULL;
	fx->applied_snomask_expr = NULL;
	fx->snomask_owned = 0;
}

static void udb_nick_effects_apply_vhost(Client *client, UdbStorageField *vhost_rec)
{
	UdbNickEffects *fx;
	const char *desired;

	if (!client || !client->user || !vhost_rec || !udb_ip_safe_host(client, vhost_rec->string) || !MyUser(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	desired = vhost_rec->string;
	udb_nick_effects_revoke_oper_vhost(client);

	if (fx->vhost_owned && !strcmp(fx->applied_vhost, desired))
	{
		/* Still materialized as UDB left it: nothing to do. If an external
		 * source replaced it, the profile still desires this value, so fall
		 * through and reassert it. */
		if (client->user->virthost && !strcmp(client->user->virthost, desired) && IsHidden(client) && IsSetHost(client))
		{
			fx->oper_vhost_attempted = 0;
			return;
		}
	}
	else if (!fx->vhost_owned && client->user->virthost && !strcmp(client->user->virthost, desired) &&
			 IsHidden(client) && IsSetHost(client))
	{
		/* Desired vhost already active without a UDB mutation: no claim. */
		fx->oper_vhost_attempted = 1;
		return;
	}
	else if (fx->vhost_owned)
	{
		/* The profile changed its desired vhost: release the old value first,
		 * preserving it only when it is still the UDB-applied one. */
		udb_nick_effects_revoke_vhost(client);
		fx = udb_nick_effects_ensure(client);
		if (!fx)
			return;
	}

	udb_nick_set_vhost(client, desired);
	fx->vhost_owned = 1;
	fx->oper_vhost_attempted = 0;
	strlcpy(fx->applied_vhost, desired, sizeof(fx->applied_vhost));
}

static void udb_nick_effects_revoke_vhost(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);

	if (!fx || !fx->vhost_owned)
		return;
	if (client->user && client->user->virthost && fx->applied_vhost[0] &&
		!strcmp(client->user->virthost, fx->applied_vhost))
		udb_nick_remove_vhost(client);
	/* An external replacement is preserved. */
	fx->vhost_owned = 0;
	fx->applied_vhost[0] = '\0';
}

static int udb_nick_nullable_equal(const char *left, const char *right)
{
	if (!left || !*left)
		return !right || !*right;
	return right && !strcmp(left, right);
}

static int udb_nick_expected_oper_vhost(Client *client, const char *format, char *expected_vhost, size_t vhost_size,
										char *expected_username, size_t username_size, int *expected_sethost)
{
	char expanded[HOSTLEN + 1];
	char uhost[HOSTLEN + USERLEN + 1];
	char *hostpart;

	if (!client || !client->user || !expected_vhost || !expected_username || !expected_sethost)
		return 0;
	expected_vhost[0] = '\0';
	strlcpy(expected_username, client->user->username, username_size);
	*expected_sethost = IsSetHost(client) ? 1 : 0;

	if (format)
	{
		*expanded = '\0';
		unreal_expand_string(format, expanded, sizeof(expanded), NULL, 0, client);
		if (!valid_vhost(expanded))
			return 0;
		strlcpy(uhost, expanded, sizeof(uhost));
		hostpart = strchr(uhost, '@');
		if (hostpart)
		{
			*hostpart++ = '\0';
			strlcpy(expected_username, uhost, username_size);
		}
		else
		{
			hostpart = uhost;
		}
		if (!udb_ip_safe_host(client, hostpart))
			return 0;
		strlcpy(expected_vhost, hostpart, vhost_size);
		*expected_sethost = 1;
		return 1;
	}

	if (((client->umodes | OPER_MODES) & UMODE_HIDE) && !client->user->virthost)
	{
		strlcpy(expected_vhost, client->user->cloakedhost, vhost_size);
		return 1;
	}
	return 0;
}

static int udb_nick_expand_oper_vhost_default(Client *client, const char *operclass, char *expanded, size_t size)
{
	NameValuePrioList *nvp = NULL;

	if (!client || BadPtr(operclass) || !expanded || !size || !iConf.oper_vhost)
		return 0;
	/* A regular /OPER stores its oper login before make_oper(), so these
	 * variables are already available when set::oper-vhost is expanded. UDB
	 * has no oper block to store, therefore provide the pending identity
	 * explicitly without publishing it as live client state before the grant. */
	add_nvplist(&nvp, -1, "operlogin", "UDB");
	add_nvplist(&nvp, -1, "operclass", operclass);
	unreal_expand_string(iConf.oper_vhost, expanded, size, nvp, 0, client);
	return 1;
}

static void udb_nick_effects_capture_oper_vhost(Client *client, const char *previous_vhost,
												const char *previous_username, int previous_sethost,
												const char *expected_vhost, const char *expected_username,
												int expected_sethost)
{
	UdbNickEffects *fx;
	int host_owned;
	int username_owned;

	if (!client || !client->user || !MyUser(client))
		return;
	host_owned = (!udb_nick_nullable_equal(previous_vhost, expected_vhost) || previous_sethost != expected_sethost) &&
				 udb_nick_nullable_equal(client->user->virthost, expected_vhost) &&
				 (IsSetHost(client) ? 1 : 0) == expected_sethost;
	username_owned = strcmp(previous_username, expected_username) && !strcmp(client->user->username, expected_username);
	if (!host_owned && !username_owned)
		return;

	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	if (host_owned)
	{
		safe_strdup(fx->oper_previous_vhost, previous_vhost);
		safe_strdup(fx->oper_applied_vhost, expected_vhost);
		fx->oper_previous_sethost = previous_sethost;
		fx->oper_vhost_owned = 1;
	}
	strlcpy(fx->oper_previous_username, previous_username, sizeof(fx->oper_previous_username));
	strlcpy(fx->oper_applied_username, expected_username, sizeof(fx->oper_applied_username));
	fx->oper_username_owned = username_owned;
}

static void udb_nick_effects_revoke_oper_vhost(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);
	const char *current_vhost;

	if (!fx || (!fx->oper_vhost_owned && !fx->oper_username_owned) || !client->user)
		return;
	current_vhost = client->user->virthost;
	if (fx->oper_vhost_owned && udb_nick_nullable_equal(current_vhost, fx->oper_applied_vhost))
	{
		if (udb_ip_safe_host(client, fx->oper_previous_vhost))
		{
			userhost_save_current(client);
			safe_strdup(client->user->virthost, fx->oper_previous_vhost);
			client->umodes |= UMODE_HIDE;
			if (fx->oper_previous_sethost)
				client->umodes |= UMODE_SETHOST;
			else
				client->umodes &= ~UMODE_SETHOST;
			sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, client->user->virthost);
			if (!IsSetHost(client))
				sendto_server(client, 0, 0, NULL, ":%s UMODE2 -t", client->id);
			userhost_changed(client);
		}
		else
		{
			udb_nick_remove_vhost(client);
		}
	}
	if (fx->oper_username_owned && !strcmp(client->user->username, fx->oper_applied_username))
	{
		userhost_save_current(client);
		strlcpy(client->user->username, fx->oper_previous_username, sizeof(client->user->username));
		sendto_server(NULL, 0, 0, NULL, ":%s SETIDENT %s", client->id, client->user->username);
		userhost_changed(client);
	}

	safe_free(fx->oper_previous_vhost);
	safe_free(fx->oper_applied_vhost);
	fx->oper_previous_vhost = NULL;
	fx->oper_applied_vhost = NULL;
	fx->oper_previous_username[0] = '\0';
	fx->oper_applied_username[0] = '\0';
	fx->oper_username_owned = 0;
	fx->oper_previous_sethost = 0;
	fx->oper_vhost_owned = 0;
}

static void udb_nick_set_oper_vhost(Client *client, const char *format)
{
	char uhost[HOSTLEN + USERLEN + 1];
	char newhost[HOSTLEN + 1];
	char *hostpart;

	if (!client || !client->user || BadPtr(format))
		return;
	*newhost = '\0';
	unreal_expand_string(format, newhost, sizeof(newhost), NULL, 0, client);
	if (!valid_vhost(newhost))
		return;
	strlcpy(uhost, newhost, sizeof(uhost));
	hostpart = strchr(uhost, '@');
	if (hostpart)
	{
		*hostpart++ = '\0';
		strlcpy(client->user->username, uhost, sizeof(client->user->username));
		sendto_server(NULL, 0, 0, NULL, ":%s SETIDENT %s", client->id, client->user->username);
	}
	else
	{
		hostpart = uhost;
	}
	safe_strdup(client->user->virthost, hostpart);
	sendto_server(NULL, 0, 0, NULL, ":%s SETHOST :%s", client->id, client->user->virthost);
	client->umodes |= UMODE_SETHOST | UMODE_HIDE;
}

static void udb_nick_effects_apply_oper_vhost_default(Client *client)
{
	UdbNickEffects *fx;
	char *previous_vhost;
	char previous_username[USERLEN + 1];
	char expected_vhost[HOSTLEN + 1];
	char expected_username[USERLEN + 1];
	char expanded_default[HOSTLEN + 1];
	const char *operclass;
	const char *format;
	long old_umodes;
	int previous_sethost;
	int expected_sethost;

	if (!client || !client->user || !MyUser(client) || !IsOper(client) || !udb_oper_owned(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx || fx->oper_vhost_attempted || fx->oper_vhost_owned || fx->oper_username_owned)
		return;
	/* One default attempt belongs to the oper grant (or to the transition
	 * that removed an owned N::vhost). An external replacement must not be
	 * overwritten by unrelated later profile reconciliation. */
	fx->oper_vhost_attempted = 1;
	operclass = get_operclass(client);
	format = udb_nick_expand_oper_vhost_default(client, operclass, expanded_default, sizeof(expanded_default))
				 ? expanded_default
				 : NULL;
	if (!udb_nick_expected_oper_vhost(client, format, expected_vhost, sizeof(expected_vhost), expected_username,
									  sizeof(expected_username), &expected_sethost))
		return;

	previous_vhost = client->user->virthost ? strdup(client->user->virthost) : NULL;
	strlcpy(previous_username, client->user->username, sizeof(previous_username));
	previous_sethost = IsSetHost(client) ? 1 : 0;
	old_umodes = client->umodes & ALL_UMODES;
	userhost_save_current(client);
	if (format)
		udb_nick_set_oper_vhost(client, format);
	else if (IsHidden(client) && !client->user->virthost)
		safe_strdup(client->user->virthost, client->user->cloakedhost);
	userhost_changed(client);
	udb_nick_effects_capture_oper_vhost(client, previous_vhost, previous_username, previous_sethost, expected_vhost,
										expected_username, expected_sethost);
	safe_free(previous_vhost);
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);
}

static void udb_nick_effects_apply_swhois(Client *client, UdbStorageField *swhois_rec)
{
	UdbNickEffects *fx;

	if (!client || !client->user || !swhois_rec || BadPtr(swhois_rec->string) || !MyUser(client))
		return;
	fx = udb_nick_effects_ensure(client);
	if (!fx)
		return;
	swhois_delete(client, "udb", "*", &me, NULL);
	swhois_add(client, "udb", 100, swhois_rec->string, &me, NULL);
	fx->swhois_owned = 1;
}

static void udb_nick_effects_revoke_swhois(Client *client)
{
	UdbNickEffects *fx = udb_nick_effects_get(client);

	if (!fx || !fx->swhois_owned)
		return;
	swhois_delete(client, "udb", "*", &me, NULL);
	fx->swhois_owned = 0;
}

/* Revocation uses only runtime ownership metadata; it never inspects the
 * current profile to decide what UDB may remove. */
static void udb_nick_revoke_effects(Client *client)
{
	if (!client || !MyUser(client))
		return;
	udb_nick_revoke_oper(client);
	udb_nick_effects_revoke_modes(client);
	udb_nick_effects_revoke_vhost(client);
	udb_nick_effects_revoke_swhois(client);
	udb_nick_effects_revoke_snomasks(client);
	udb_nick_effects_clear(client);
}

/* Desired state comes from the profile; owned state comes from UdbNickEffects.
 * Reconcile removes owned effects no longer desired, preserves external state
 * UDB never owned, and records exactly what UDB changes. */
static void udb_nick_reconcile_effects(Client *client, UdbStorageProfile *nick_rec)
{
	UdbStorageField *effect_rec;

	if (!client || !MyUser(client) || !nick_rec)
		return;

	effect_rec = udb_nick_field(nick_rec, NKEY_VHOST);
	if (effect_rec)
		udb_nick_effects_apply_vhost(client, effect_rec);
	else
	{
		udb_nick_effects_revoke_vhost(client);
		udb_nick_effects_apply_oper_vhost_default(client);
	}

	effect_rec = udb_nick_field(nick_rec, NKEY_OPER);
	if (effect_rec)
	{
		if (!udb_nick_grant_oper(client, nick_rec, effect_rec))
			return;
	}
	else
		udb_nick_revoke_oper(client);

	effect_rec = udb_nick_field(nick_rec, NKEY_MODES);
	if (effect_rec)
		udb_nick_effects_apply_modes(client, effect_rec);
	else
		udb_nick_effects_revoke_modes(client);

	effect_rec = udb_nick_field(nick_rec, NKEY_SWHOIS);
	if (effect_rec)
		udb_nick_effects_apply_swhois(client, effect_rec);
	else
		udb_nick_effects_revoke_swhois(client);

	effect_rec = udb_nick_field(nick_rec, NKEY_SNOMASKS);
	if (effect_rec)
		udb_nick_effects_apply_snomasks(client, effect_rec);
	else
		udb_nick_effects_revoke_snomasks(client);
}

static void udb_nick_set_vhost(Client *client, const char *vhost)
{
	if (!client || !client->user || !udb_ip_safe_host(client, vhost))
		return;

	userhost_save_current(client);
	safe_strdup(client->user->virthost, vhost);
	client->umodes |= UMODE_HIDE;
	client->umodes |= UMODE_SETHOST;

	if (IsUser(client))
	{
		sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, client->user->virthost);
	}

	if (MyConnect(client))
	{
		sendto_one(client, NULL, ":%s MODE %s :+tx", client->name, client->name);
		udb_send_service_notice(client, SKEY_IPSERV, "*** Your vhost is now %s", client->user->virthost);
	}

	userhost_changed(client);
}

static void udb_nick_remove_vhost(Client *client)
{
	if (!client || !client->user)
		return;
	/* No realhost fallback: failed recovery leaves the protected overlay intact. */
	udb_ip_reapply_vhost(client);
}

static int udb_oper_owned(Client *client)
{
	if (!client || !udb_nick_oper_owned_md)
		return 0;
	return moddata_local_client(client, udb_nick_oper_owned_md).i ? 1 : 0;
}

static int udb_nick_grant_oper(Client *client, UdbStorageProfile *nick_rec, UdbStorageField *oper_rec)
{
	UdbNickEffects *fx;
	const char *operclass;
	UdbStorageField *vhost_rec;
	const char *oper_vhost;
	char *previous_vhost;
	char previous_username[USERLEN + 1];
	char expected_vhost[HOSTLEN + 1];
	char expected_username[USERLEN + 1];
	char expanded_default[HOSTLEN + 1];
	int previous_sethost;
	int expected_sethost = 0;
	int expect_oper_vhost = 0;
	int has_profile_vhost;

	if (!client || !oper_rec)
		return 1;

	operclass = oper_rec->string;
	if (BadPtr(operclass))
		return 1;

	if (!find_operclass(operclass))
	{
		udb_log(ULOG_WARNING, "UDB_OPERCLASS_NOT_FOUND", client,
				"operclass '$operclass' for $client.details does not exist in unrealircd.conf",
				log_data_string("operclass", operclass));
		return 1;
	}

	if (IsOper(client) && !udb_oper_owned(client))
	{
		/* external oper: do not touch */
		return 1;
	}

	if (udb_oper_owned(client))
	{
		const char *curr_class = get_operclass(client);
		if (curr_class && !strcmp(curr_class, operclass))
			return 1;
		udb_nick_revoke_oper(client);
	}

	if (!IsOper(client))
	{
		vhost_rec = nick_rec ? udb_nick_field(nick_rec, NKEY_VHOST) : NULL;
		has_profile_vhost = vhost_rec && udb_ip_safe_host(client, vhost_rec->string);
		if (has_profile_vhost)
			oper_vhost = vhost_rec->string;
		else if (udb_nick_expand_oper_vhost_default(client, operclass, expanded_default, sizeof(expanded_default)))
			oper_vhost = expanded_default;
		else
			oper_vhost = NULL;
		previous_vhost = (client->user && client->user->virthost) ? strdup(client->user->virthost) : NULL;
		strlcpy(previous_username, client->user->username, sizeof(previous_username));
		previous_sethost = IsSetHost(client) ? 1 : 0;
		expect_oper_vhost =
			udb_nick_expected_oper_vhost(client, oper_vhost, expected_vhost, sizeof(expected_vhost), expected_username,
										 sizeof(expected_username), &expected_sethost);
		/* NULL asks native make_oper() to retry the global template. An
		 * empty explicit template instead rejects it without changing host. */
		if (oper_vhost && !expect_oper_vhost)
			oper_vhost = "";
		if (!make_oper(client, "UDB", operclass, NULL, 0, NULL, oper_vhost, NULL))
		{
			safe_free(previous_vhost);
			return 0;
		}
		if (IsOper(client) && udb_nick_oper_owned_md)
		{
			moddata_local_client(client, udb_nick_oper_owned_md).i = 1;
			fx = udb_nick_effects_ensure(client);
			if (fx && !has_profile_vhost)
				fx->oper_vhost_attempted = 1;
			if (expect_oper_vhost)
				udb_nick_effects_capture_oper_vhost(client, previous_vhost, previous_username, previous_sethost,
													expected_vhost, expected_username, expected_sethost);
			/* N::vhost is a literal profile effect. make_oper() treats its vhost
			 * argument as an expandable ident@host template, so undo only the
			 * transformation that survived synchronous hooks. The normal profile
			 * marker remains responsible for the literal value. */
			if (has_profile_vhost)
				udb_nick_effects_revoke_oper_vhost(client);
		}
		safe_free(previous_vhost);
	}
	return 1;
}

static void udb_nick_revoke_oper(Client *client)
{
	UdbNickEffects *fx;
	long old_umodes;

	if (!client || !MyUser(client) || !udb_oper_owned(client))
		return;

	if (!IsOper(client))
	{
		/* Oper status was removed elsewhere: UDB keeps no claim over it. */
		old_umodes = client->umodes & ALL_UMODES;
		moddata_local_client(client, udb_nick_oper_owned_md).i = 0;
		udb_nick_effects_revoke_oper_vhost(client);
		fx = udb_nick_effects_get(client);
		if (fx)
			fx->oper_vhost_attempted = 0;
		if ((client->umodes & ALL_UMODES) != old_umodes)
			send_umode_out(client, 1, old_umodes);
		return;
	}

	old_umodes = client->umodes & ALL_UMODES;
	client->umodes &= ~UMODE_OPER;
	if (client->umodes & UMODE_HIDEOPER)
	{
		client->umodes &= ~UMODE_HIDEOPER;
	}
	else if (irccounts.operators > 0)
	{
		irccounts.operators--;
	}

	if (MyUser(client) && !list_empty(&client->special_node))
	{
		list_del(&client->special_node);
		INIT_LIST_HEAD(&client->special_node);
	}
	if (MyUser(client))
		RunHook(HOOKTYPE_LOCAL_OPER, client, 0, NULL, NULL);
	remove_oper_privileges(client, 0);
	moddata_local_client(client, udb_nick_oper_owned_md).i = 0;
	udb_nick_effects_revoke_oper_vhost(client);
	fx = udb_nick_effects_get(client);
	if (fx)
		fx->oper_vhost_attempted = 0;
	send_umode_out(client, 1, old_umodes);
}

static void udb_nick_force_rename_with_notice(Client *client, const char *nick_in_db, const char *notice,
											  int announce_profile, int offer_identify)
{
	char newnick[32];
	char rand_suffix[6];
	const char *args[5];
	char tsbuf[32];

	gen_random_alnum(rand_suffix, 5);
	rand_suffix[5] = '\0';
	snprintf(newnick, sizeof(newnick), "Guest%s", rand_suffix);
	if (notice)
		udb_send_service_notice(client, SKEY_NICKSERV, "%s", notice);
	else if (announce_profile)
		udb_send_service_notice(client, SKEY_NICKSERV,
								"This nickname (%s) has been registered or synced in the UDB database.", nick_in_db);
	if (offer_identify)
		udb_send_service_notice(client, SKEY_NICKSERV,
								"You have been renamed. If you are the owner, please identify: /NICK %s:Password",
								nick_in_db);

	tsbuf[0] = '\0';
	snprintf(tsbuf, sizeof(tsbuf), "%lld", (long long)TStime());
	args[0] = NULL;
	args[1] = client->name;
	args[2] = newnick;
	args[3] = tsbuf;
	args[4] = NULL;
	do_cmd(&me, NULL, "SVSNICK", 4, args);
}

static void udb_nick_force_rename(Client *client, const char *nick_in_db)
{
	udb_nick_force_rename_with_notice(client, nick_in_db, NULL, 1, 1);
}

static void udb_nick_force_rename_after_unsuspend(Client *client, const char *nick_in_db)
{
	char notice[256];

	snprintf(notice, sizeof(notice), "The nickname %s is no longer suspended.", nick_in_db);
	udb_nick_force_rename_with_notice(client, nick_in_db, notice, 0, 1);
}

static void udb_nick_apply(Client *client, UdbStorageProfile *nick_rec, UdbNickApplyReason reason)
{
	UdbStorageField *forbid;
	UdbStorageField *pass_rec;
	UdbStorageField *suspend;
	char notice[UDB_RECORD_VALUE_MAX + 64];

	if (!client || !nick_rec)
		return;
	forbid = udb_nick_field(nick_rec, NKEY_FORBID);
	if (forbid)
	{
		/* A forbid always wins: revoke UDB-owned state before the rename. */
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		snprintf(notice, sizeof(notice), "This nickname is forbidden. Reason: %s", forbid->string);
		udb_nick_force_rename_with_notice(client, nick_rec->entity, notice, 0, 0);
		return;
	}
	pass_rec = udb_nick_field(nick_rec, NKEY_PASS);
	suspend = udb_nick_field(nick_rec, NKEY_SUSPEND);

	/* Access denial is fail-closed for adoption and refresh alike. UDB only
	 * removes effects it actually owned; a passless profile never had any. */
	if (!udb_nick_access_allowed(client, nick_rec))
	{
		udb_nick_password_cache_clear(client);
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		snprintf(notice, sizeof(notice), "Access to %s is not permitted from your IP address.", nick_rec->entity);
		udb_nick_force_rename_with_notice(client, nick_rec->entity, notice, 0, 0);
		return;
	}

	/* A profile without a credential controls only nick use. It can never turn
	 * a matching nick, account or +r into UDB identity or profile effects, and
	 * it never creates an ownership marker of its own. */
	if (!pass_rec)
	{
		udb_nick_password_cache_clear(client);
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		if (suspend)
			udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s", suspend->string);
		return;
	}

	/* A refreshed suspension revokes identity/effects without requiring a new
	 * authentication: the nick may stay occupied but no auth survives it. */
	if (reason == UDB_NICK_APPLY_REFRESH && suspend)
	{
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s", suspend->string);
		return;
	}

	/* A protected profile materializes identity only with a valid active UDB
	 * proof. SVSNICK and forced changes never carry one, and credential/access
	 * values are checked live, so a holder without proof is renamed away. */
	if (!udb_nick_identity_valid(client, nick_rec))
	{
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		if (reason == UDB_NICK_APPLY_UNSUSPEND)
			udb_nick_force_rename_after_unsuspend(client, nick_rec->entity);
		else
			udb_nick_force_rename(client, nick_rec->entity);
		return;
	}

	/* Adoption of a suspended profile validates pass/access but materializes
	 * nothing: no identity, account/+r or effects, and no revoke of state UDB
	 * never applied. */
	if (reason == UDB_NICK_APPLY_ADOPT && suspend)
	{
		udb_nick_revoke_identity(client);
		udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is suspended. Reason: %s", suspend->string);
		return;
	}

	/* Materialize the public identity projection before its effects, then let
	 * the reconciler decide effect-by-effect against runtime ownership. */
	if (client->user)
		strlcpy(client->user->account, nick_rec->entity, sizeof(client->user->account));

	long old_umodes = client->umodes & ALL_UMODES;
	client->umodes |= UMODE_REGNICK;
	if ((client->umodes & ALL_UMODES) != old_umodes)
		send_umode_out(client, 1, old_umodes);

	udb_nick_reconcile_effects(client, nick_rec);
}

static void udb_nick_adopt(Client *client, UdbStorageProfile *nick_rec)
{
	int authenticated_now = udb_nick_pending_auth_consume(client, nick_rec);

	if (authenticated_now)
		udb_nick_identity_set(client, nick_rec);
	udb_nick_apply(client, nick_rec, UDB_NICK_APPLY_ADOPT);
	if (authenticated_now && udb_nick_identity_valid(client, nick_rec))
		udb_send_service_notice(client, SKEY_NICKSERV, "You are now identified for nickname %s.", nick_rec->entity);
}

static UdbPasswordFailure *udb_password_failure_find(UdbStorageProfile *profile_rec, Client *client, int create,
													 int *saturated)
{
	UdbPasswordFailure *free_slot = NULL;
	const char *ip = client ? client->ip : NULL;
	time_t now = TStime();
	int period = udb_cfg ? udb_cfg->flood_period : 60;
	unsigned int i;

	if (period <= 0)
		period = 60;
	if (saturated)
		*saturated = 0;
	if (!profile_rec || !*profile_rec->entity || BadPtr(ip))
		return NULL;
	for (i = 0; i < UDB_PASSWORD_FAILURE_SLOTS; i++)
	{
		UdbPasswordFailure *entry = &udb_password_failures[i];
		if (entry->since && now - entry->since >= period)
			memset(entry, 0, sizeof(*entry));
		if (entry->since && entry->block_idx == udb_storage_stage_block_index(profile_rec->key.block) &&
			!strcmp(entry->profile, profile_rec->entity) && !strcmp(entry->ip, ip))
			return entry;
		if (!entry->since && !free_slot)
			free_slot = entry;
	}
	if (!free_slot)
	{
		if (saturated)
			*saturated = 1;
		return NULL;
	}
	if (!create)
		return NULL;
	memset(free_slot, 0, sizeof(*free_slot));
	strlcpy(free_slot->profile, profile_rec->entity, sizeof(free_slot->profile));
	strlcpy(free_slot->ip, ip, sizeof(free_slot->ip));
	free_slot->block_idx = udb_storage_stage_block_index(profile_rec->key.block);
	free_slot->since = now;
	return free_slot;
}

static int udb_password_flooded(UdbStorageProfile *profile_rec, Client *client)
{
	int saturated = 0;
	UdbPasswordFailure *entry = udb_password_failure_find(profile_rec, client, 0, &saturated);
	int attempts = udb_cfg ? udb_cfg->flood_attempts : 5;

	if (attempts <= 0)
		attempts = 5;
	return saturated || (entry && entry->attempts >= (unsigned int)attempts);
}

static void udb_password_failure_record(UdbStorageProfile *profile_rec, Client *client, int success)
{
	UdbPasswordFailure *entry = udb_password_failure_find(profile_rec, client, !success, NULL);

	if (!entry)
		return;
	if (success)
		memset(entry, 0, sizeof(*entry));
	else if (entry->attempts != (unsigned int)-1)
		entry->attempts++;
}

static void udb_nick_password_failure_notice(Client *client, UdbStorageProfile *profile_rec, const char *nick)
{
	if (udb_password_flooded(profile_rec, client))
		udb_send_service_notice(client, SKEY_NICKSERV, "Too many failed password attempts for %s; try again later.",
								nick);
	else
		udb_send_service_notice(client, SKEY_NICKSERV, "Invalid password for %s.", nick);
}

static int udb_password_type(const char *stored_pass, const char **hash)
{
	if (!strncmp(stored_pass, "argon2id:", 9))
	{
		*hash = stored_pass + 9;
		return AUTHTYPE_ARGON2;
	}
	if (!strncmp(stored_pass, "sha256:", 7))
	{
		*hash = stored_pass + 7;
		return UDB_AUTHTYPE_SHA256;
	}
	if (!strncmp(stored_pass, "crypt:", 6))
	{
		*hash = stored_pass + 6;
		return AUTHTYPE_UNIXCRYPT;
	}
	*hash = stored_pass;
	return AUTHTYPE_INVALID;
}

static int udb_is_sha256_hash(const char *hash)
{
	size_t i;

	if (!hash || strlen(hash) != 64)
		return 0;
	for (i = 0; hash[i]; i++)
		if (!isxdigit((unsigned char)hash[i]))
			return 0;
	return 1;
}

static int udb_check_password(const char *pass, UdbStorageProfile *profile_rec, Client *client)
{
	UdbStorageField *pass_rec;
	const char *stored_pass;
	const char *hash;
	AuthConfig as;
	int type;
	int success;

	if (!pass || !profile_rec || udb_password_flooded(profile_rec, client))
		return 0;
	pass_rec = udb_nick_field(profile_rec, NKEY_PASS);
	if (!pass_rec || BadPtr(pass_rec->string))
	{
		udb_password_failure_record(profile_rec, client, 0);
		return 0;
	}
	stored_pass = pass_rec->string;
	type = udb_password_type(stored_pass, &hash);
	if (type == AUTHTYPE_ARGON2 && strncmp(hash, "$argon2id$", 10))
		type = AUTHTYPE_INVALID;
	if (type == UDB_AUTHTYPE_SHA256 && !udb_is_sha256_hash(hash))
		type = AUTHTYPE_INVALID;
	if (type == AUTHTYPE_INVALID)
	{
		udb_password_failure_record(profile_rec, client, 0);
		return 0;
	}
	if (type == UDB_AUTHTYPE_SHA256)
	{
		char digest[65];
		sha256hash(digest, pass, strlen(pass));
		char expected[64];
		for (unsigned int i = 0; i < 64; i++)
			expected[i] = hash[i] >= 'A' && hash[i] <= 'F' ? hash[i] + ('a' - 'A') : hash[i];
		success = CRYPTO_memcmp(digest, expected, sizeof(expected)) == 0;
	}
	else
	{
		memset(&as, 0, sizeof(as));
		as.type = type;
		as.data = (char *)hash;
		success = Auth_Check(client, &as, pass);
	}
	udb_password_failure_record(profile_rec, client, success);
	return success;
}

static int udb_cidr_matches(const char *ip, const char *cidr)
{
	unsigned char address[16], network[16];
	char network_text[INET6_ADDRSTRLEN];
	const char *slash = strchr(cidr, '/');
	unsigned int prefix;
	int family;
	int bytes;

	if (!slash || (size_t)(slash - cidr) >= sizeof(network_text))
		return 0;
	memcpy(network_text, cidr, slash - cidr);
	network_text[slash - cidr] = '\0';
	if (!udb_parse_uint_strict(slash + 1, &prefix, 0, 128))
		return 0;
	if (inet_pton(AF_INET, ip, address) == 1)
		family = AF_INET;
	else if (inet_pton(AF_INET6, ip, address) == 1)
		family = AF_INET6;
	else
		return 0;
	if (inet_pton(family, network_text, network) != 1 || prefix > (family == AF_INET ? 32 : 128))
		return 0;
	bytes = prefix / 8;
	if (bytes && memcmp(address, network, bytes))
		return 0;
	if (prefix % 8 &&
		(address[bytes] & (0xff << (8 - (prefix % 8)))) != (network[bytes] & (0xff << (8 - (prefix % 8)))))
		return 0;
	return 1;
}

static int udb_nick_access_allowed(Client *client, UdbStorageProfile *nick_rec)
{
	UdbStorageField *access_rec = udb_nick_field(nick_rec, NKEY_ACCESS);
	const char *p;

	if (!access_rec)
		return 1;
	if (BadPtr(access_rec->string) || !client || BadPtr(client->ip))
		return 0;
	for (p = access_rec->string; *p;)
	{
		char cidr[INET6_ADDRSTRLEN + 5];
		size_t len = 0;
		while (*p == ',' || isspace((unsigned char)*p))
			p++;
		while (*p && *p != ',' && !isspace((unsigned char)*p) && len + 1 < sizeof(cidr))
			cidr[len++] = *p++;
		cidr[len] = '\0';
		while (*p && *p != ',' && !isspace((unsigned char)*p))
			p++;
		if (len && udb_cidr_matches(client->ip, cidr))
			return 1;
	}
	return 0;
}

CMD_FUNC(cmd_ghost)
{
	UdbStorageProfile *nick_rec = NULL;
	if (parc < 3)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "GHOST");
		goto cleanup;
	}
	const char *target_nick = parv[1];
	const char *pass = parv[2];

	if (!udb_ctx || !udb_ctx->storage || !udb_ready)
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "UDB is not fully initialized.");
		goto cleanup;
	}

	UdbStorageResult loaded = udb_native_nick_load(target_nick, &nick_rec);
	if (loaded < UDB_STORAGE_NOT_FOUND)
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Nickname policy data unavailable.");
		goto cleanup;
	}
	if (!nick_rec)
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Nick %s is not registered.", target_nick);
		goto cleanup;
	}
	if (!udb_nick_field(nick_rec, NKEY_PASS))
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Nick %s has no UDB password.", target_nick);
		goto cleanup;
	}

	if (!udb_check_password(pass, nick_rec, client))
	{
		udb_nick_password_failure_notice(client, nick_rec, target_nick);
		goto cleanup;
	}
	if (!udb_nick_access_allowed(client, nick_rec))
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
								target_nick);
		goto cleanup;
	}

	Client *target = find_client(target_nick, NULL);
	if (target)
	{
		if (target == client)
		{
			udb_send_service_notice(client, SKEY_NICKSERV, "You cannot ghost yourself.");
			goto cleanup;
		}
		udb_send_service_notice(client, SKEY_NICKSERV, "Ghosting %s...", target_nick);
		exit_client(target, NULL, "GHOST command used");
	}
	else
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "%s is not online.", target_nick);
	}
cleanup:
	udb_storage_profile_free(&nick_rec);
}

CMD_OVERRIDE_FUNC(udb_override_nick)
{
	UdbStorageProfile *rec = NULL;
	if (parc <= 1 || !MyConnect(client) || IsServer(client))
		goto passthrough;

	/* An unnamed client cannot receive service notices. Reject admission
	 * explicitly instead of swallowing NICK until registration times out. */
	if (!IsUser(client) && udb_hook_readiness_pre_connect(client) == HOOK_DENY)
		return;

	/* A /NICK password belongs only to this attempt; it is never session
	 * state and is consumed by CAN_USE_NICK or discarded here. */
	udb_nick_password_cache_clear(client);

	const char *nick = parv[1];
	char clean_nick[NICKLEN + 64];
	strlcpy(clean_nick, nick, sizeof(clean_nick));
	char *pass_colon = strchr(clean_nick, ':');
	char *pass_bang = strchr(clean_nick, '!');

	char *pass = NULL;
	int force_ghost = 0;

	if (pass_colon && (!pass_bang || pass_colon < pass_bang))
	{
		*pass_colon = '\0';
		pass = pass_colon + 1;
		force_ghost = 0;
	}
	else if (pass_bang && (!pass_colon || pass_bang < pass_colon))
	{
		*pass_bang = '\0';
		pass = pass_bang + 1;
		force_ghost = 1;
	}

	UdbStorageResult loaded = udb_native_nick_load(clean_nick, &rec);
	if (loaded < UDB_STORAGE_NOT_FOUND)
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Nickname policy data unavailable.");
		goto cleanup;
	}
	if (rec)
	{
		UdbStorageField *forbid = udb_nick_field(rec, NKEY_FORBID);
		if (forbid)
		{
			udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is forbidden. Reason: %s", forbid->string);
			goto cleanup;
		}
	}
	if (!pass)
		goto passthrough;
	if (rec && !udb_nick_field(rec, NKEY_PASS))
		goto dispatch_clean_nick;

	Client *acptr = rec ? find_client(clean_nick, NULL) : NULL;
	int validated = 0;

	/* The core NICK path checks the password too. Only pre-check when a
	 * collision needs the password for ghost/recovery, and stop on failure so
	 * the flood tracker is consumed exactly once. */
	if (rec && acptr && acptr != client)
	{
		if (!udb_check_password(pass, rec, client))
		{
			udb_nick_password_failure_notice(client, rec, clean_nick);
			sendnumeric(client, ERR_ERRONEUSNICKNAME, clean_nick, "Nickname requires a valid UDB password.");
			goto cleanup;
		}
		if (!udb_nick_access_allowed(client, rec))
		{
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									clean_nick);
			sendnumeric(client, ERR_ERRONEUSNICKNAME, clean_nick,
						"Nickname requires a valid UDB password and authorized IP.");
			goto cleanup;
		}
		validated = 1;
		if (force_ghost)
		{
			char quit_msg[128];
			snprintf(quit_msg, sizeof(quit_msg), "Ghosted (Nick taken by %s)", client->name);
			exit_client(acptr, NULL, quit_msg);
		}
		else
		{
			udb_send_service_notice(client, SKEY_NICKSERV,
									"This nickname is currently in use. If you are the owner, you can recover it by "
									"typing /NICK %s!Password",
									clean_nick);
		}
	}
	if (rec)
		udb_nick_password_cache_set(client, clean_nick, pass, validated);

dispatch_clean_nick:;
	const char *new_parv[MAXPARA + 1];
	for (int i = 0; i < parc; i++)
		new_parv[i] = parv[i];
	new_parv[1] = clean_nick;
	CallCommandOverride(ovr, clictx, client, recv_mtags, parc, new_parv);
	/* The credential is bound to this attempt: a rejected change must not
	 * leave a promotable pending proof behind for a later forced rename. */
	udb_nick_pending_auth_expire(client, clean_nick);
	goto cleanup;

passthrough:
	CALL_NEXT_COMMAND_OVERRIDE();
	udb_nick_pending_auth_expire(client, parc > 1 ? parv[1] : NULL);
cleanup:
	udb_storage_profile_free(&rec);
}

static int udb_nick_can_use_profile(Client *client, const char *newnick, const char **reject_reason,
									UdbStorageProfile *nick_rec)
{
	if (nick_rec)
	{
		UdbStorageField *forbid = udb_nick_field(nick_rec, NKEY_FORBID);
		UdbStorageField *pass_rec;
		if (forbid)
		{
			static char forbid_reject[UDB_RECORD_VALUE_MAX + 64];
			snprintf(forbid_reject, sizeof(forbid_reject), "This nickname is forbidden. Reason: %s", forbid->string);
			*reject_reason = forbid_reject;
			return HOOK_DENY;
		}
		pass_rec = udb_nick_field(nick_rec, NKEY_PASS);
		if (!pass_rec)
		{
			udb_nick_password_cache_clear(client);
			if (udb_nick_access_allowed(client, nick_rec))
				return HOOK_CONTINUE;
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									newnick);
			*reject_reason = "Access to this nickname is not permitted from your IP address.";
			return HOOK_DENY;
		}

		/* A same-nick +r retry bypasses password entry here, but does not create
		 * UDB identity; access restrictions still apply. */
		if (!strcasecmp(client->name, newnick) && has_user_mode(client, 'r'))
		{
			udb_nick_password_cache_clear(client);
			if (udb_nick_access_allowed(client, nick_rec))
				return HOOK_CONTINUE;
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									newnick);
			return HOOK_DENY;
		}

		UdbNickPasswordCache *cred = udb_nick_password_cache_take(client, newnick);
		if (cred)
		{
			int password_ok = cred->validated || udb_check_password(cred->pass, nick_rec, client);
			int access_ok = password_ok && udb_nick_access_allowed(client, nick_rec);

			if (password_ok && access_ok)
			{
				udb_nick_password_cache_entry_free(cred);
				udb_nick_pending_auth_set(client, nick_rec);
				return HOOK_CONTINUE;
			}
			if (password_ok)
				udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
										newnick);
			else
				udb_nick_password_failure_notice(client, nick_rec, newnick);
			udb_nick_password_cache_entry_free(cred);
		}
		else
		{
			udb_send_service_notice(client, SKEY_NICKSERV,
									"Nickname is unavailable: This nick is registered and requires a password and an "
									"authorized IP. Use /NICK %s:Password",
									newnick);
		}

		static char reject_buf[256];
		snprintf(reject_buf, sizeof(reject_buf),
				 "This nick is registered and requires a password and an authorized IP. Use /NICK %s:Password",
				 newnick);
		*reject_reason = reject_buf;
		return HOOK_DENY;
	}
	udb_nick_password_cache_clear(client);
	return HOOK_CONTINUE;
}

static int udb_hook_can_use_nick(Client *client, const char *newnick, const char **reject_reason)
{
	if (!MyConnect(client))
		return HOOK_CONTINUE;
	UdbStorageProfile *profile = NULL;
	UdbStorageResult loaded = udb_native_nick_load(newnick, &profile);
	if (loaded < UDB_STORAGE_NOT_FOUND)
	{
		udb_nick_password_cache_clear(client);
		udb_nick_pending_auth_clear(client);
		*reject_reason = "UDB nickname policy data unavailable.";
		return HOOK_DENY;
	}
	int result = udb_nick_can_use_profile(client, newnick, reject_reason, profile);
	udb_storage_profile_free(&profile);
	return result;
}

static int udb_hook_nick_change(Client *client, MessageTag *mtags, const char *newnick)
{
	if (MyConnect(client) && strcasecmp(client->name, newnick))
	{
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		udb_nick_password_cache_clear(client);
	}
	return HOOK_CONTINUE;
}

static int udb_nick_confirmed(Client *client, UdbStorageRead *publication)
{
	UdbStorageProfile *profile = NULL;
	UdbStorageResult result;
	if (publication)
	{
		UdbStorageRow entity = {0};
		entity.block = 'N';
		entity.depth = 1;
		entity.components[0].data = (const unsigned char *)client->name;
		entity.components[0].length = (uint32_t)strlen(client->name);
		result = udb_storage_profile_load(publication, &entity, udb_nick_fields, 9, &profile);
	}
	else
		result = udb_native_nick_load(client->name, &profile);
	if (result == UDB_STORAGE_OK)
		udb_nick_adopt(client, profile);
	else
	{
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		udb_nick_pending_auth_clear(client);
		udb_nick_password_cache_clear(client);
	}
	udb_storage_profile_free(&profile);
	if (result < UDB_STORAGE_NOT_FOUND && !IsDead(client))
		exit_client(client, NULL, "UDB nickname policy data unavailable");
	return HOOK_CONTINUE;
}

static int udb_hook_post_nick_change(Client *client, MessageTag *recv_mtags, const char *oldnick)
{
	return MyConnect(client)
			   ? udb_nick_confirmed(client, client == udb_nick_publication_client ? udb_nick_publication_read : NULL)
			   : HOOK_CONTINUE;
}

static int udb_hook_local_connect(Client *client)
{
	return udb_nick_confirmed(client, NULL);
}

static void udb_nick_preserve_on_rehash(int preserve)
{
	udb_nick_rehash_keep_identity = preserve;
}

static void udb_nick_reconcile_after_reload(void)
{
	Client *client;
	list_for_each_entry(client, &lclient_list, lclient_node)
	{
		if (!MyUser(client))
			continue;
		UdbStorageProfile *profile = NULL;
		UdbStorageResult result = udb_native_nick_load(client->name, &profile);
		if (result == UDB_STORAGE_OK)
		{
			udb_storage_profile_free(&profile);
			continue;
		}
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		udb_nick_pending_auth_clear(client);
		udb_nick_password_cache_clear(client);
	}
}

/* The durable job supplies both exact generations. No old-tree replacement
 * pointer or mutable per-record unsuspend flag participates in reconciliation. */
static UdbStorageResult udb_nick_reconcile_generations(Client *client, UdbStorageRead *before, UdbStorageRead *after)
{
	UdbStorageProfile *old = NULL, *current = NULL;
	UdbStorageRow entity = {0};
	UdbStorageResult result, previous;
	if (!client || !MyUser(client) || !before || !after)
		return UDB_STORAGE_ERROR;
	entity.block = 'N';
	entity.depth = 1;
	entity.components[0].data = (const unsigned char *)client->name;
	entity.components[0].length = (uint32_t)strlen(client->name);
	previous = udb_storage_profile_load(before, &entity, udb_nick_fields, 9, &old);
	result = previous < UDB_STORAGE_NOT_FOUND ? previous
											  : udb_storage_profile_load(after, &entity, udb_nick_fields, 9, &current);
	if (result < UDB_STORAGE_NOT_FOUND || result == UDB_STORAGE_NOT_FOUND)
	{
		udb_nick_revoke_effects(client);
		udb_nick_revoke_identity(client);
		udb_nick_pending_auth_clear(client);
		udb_nick_password_cache_clear(client);
	}
	else
	{
		int unsuspended = old && udb_nick_field(old, NKEY_SUSPEND) && !udb_nick_field(current, NKEY_SUSPEND) &&
						  udb_nick_profile_has_pass(current);
		/* A pending preflight credential is bound to its original policy; a
		 * committed profile replacement cannot silently carry it forward. */
		udb_nick_pending_auth_clear(client);
		Client *previous_client = udb_nick_publication_client;
		UdbStorageRead *previous_read = udb_nick_publication_read;
		udb_nick_publication_client = client;
		udb_nick_publication_read = after;
		udb_nick_apply(client, current, unsuspended ? UDB_NICK_APPLY_UNSUSPEND : UDB_NICK_APPLY_REFRESH);
		udb_nick_publication_client = previous_client;
		udb_nick_publication_read = previous_read;
	}
	udb_storage_profile_free(&old);
	udb_storage_profile_free(&current);
	return result;
}

static void udb_nick_shutdown(void)
{
	Client *client;
	list_for_each_entry(client, &lclient_list, lclient_node)
	{
		if (!MyUser(client))
			continue;
		udb_nick_revoke_effects(client);
		if (!udb_nick_rehash_keep_identity)
			udb_nick_revoke_identity(client);
		udb_nick_pending_auth_clear(client);
		udb_nick_password_cache_clear(client);
	}
}

int udb_nicks_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_password_cache";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_password_cache_free;
	udb_nick_password_cache_md = ModDataAdd(modinfo->handle, mreq);
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_identity";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_identity_free;
	udb_nick_identity_md = ModDataAdd(modinfo->handle, mreq);
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_pending_auth";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_pending_auth_free;
	udb_nick_pending_auth_md = ModDataAdd(modinfo->handle, mreq);
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_effects";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_effects_free;
	udb_nick_effects_md = ModDataAdd(modinfo->handle, mreq);
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_oper_owned";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	udb_nick_oper_owned_md = ModDataAdd(modinfo->handle, mreq);

	CommandAdd(modinfo->handle, "GHOST", cmd_ghost, 3, CMD_USER);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_USE_NICK, 0, udb_hook_can_use_nick);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_NICKCHANGE, 0, udb_hook_nick_change);
	HookAdd(modinfo->handle, HOOKTYPE_POST_LOCAL_NICKCHANGE, 0, udb_hook_post_nick_change);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, udb_hook_local_connect);
	return MOD_SUCCESS;
}

int udb_nicks_load(ModuleInfo *modinfo)
{
	CommandOverrideAdd(modinfo->handle, "NICK", 0, udb_override_nick);
	return 0;
}

/* End of udb_nicks.c.inc */

/* Channel management: registration, founder, modes, topic, access */
/* Inlined: udb_channels.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Channel Registration, Founder +q & Channel Options
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

typedef struct UdbChannelModeState UdbChannelModeState;
typedef struct UdbBanOwner UdbBanOwner;
typedef struct UdbBanSnapshot UdbBanSnapshot;

struct UdbChannelModeState
{
	char *value;
};

struct UdbBanOwner
{
	UdbBanOwner *next;
	char *ban;
	char *owner;
};

struct UdbBanSnapshot
{
	UdbBanSnapshot *next;
	char *ban;
};

static ModDataInfo *udb_channel_modes_md = NULL;
static ModDataInfo *udb_channel_registration_md = NULL;
static ModDataInfo *udb_channel_oper_only_md = NULL;
static ModDataInfo *udb_channel_ban_owners_md = NULL;

static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg);
static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key);
static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_pre_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf,
								 const char *parabuf, time_t sendts, int samode);
static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic);
CMD_OVERRIDE_FUNC(udb_override_mode);

static const char *const udb_native_channel_fields[] = {CKEY_FOUNDER, CKEY_MODES,	CKEY_TOPIC, CKEY_FORBID,
														CKEY_SUSPEND, CKEY_OPTIONS, CKEY_ACCESS};
static UdbStorageField *udb_channel_field(UdbStorageProfile *profile, const char *name)
{
	if (!profile || profile->key.block != UDB_BLOCK_CHANNELS || profile->count != 7)
		return NULL;
	for (unsigned int i = 0; i < 7; i++)
		if (!strcmp(name, udb_native_channel_fields[i]))
			return profile->fields[i].present ? profile->fields + i : NULL;
	return NULL;
}
static UdbStorageResult udb_native_channel_load(UdbStorageRead *read, const char *name, UdbStorageProfile **out)
{
	if (!read)
		return udb_native_profile_load(udb_ctx, UDB_BLOCK_CHANNELS, name, udb_native_channel_fields, 7, out);
	UdbStorageRow entity = {0};
	if (!name || !*name || strlen(name) > UDB_STORAGE_COMPONENT_MAX)
		return UDB_STORAGE_REJECTED;
	entity.block = UDB_BLOCK_CHANNELS;
	entity.depth = 1;
	entity.components[0].data = (const unsigned char *)name;
	entity.components[0].length = (uint32_t)strlen(name);
	return udb_storage_profile_load(read, &entity, udb_native_channel_fields, 7, out);
}

static int udb_channel_is_identified_founder(Client *client, UdbStorageProfile *chan_rec)
{
	UdbStorageField *founder_rec;

	if (!client || !chan_rec)
		return 0;
	founder_rec = udb_channel_field(chan_rec, CKEY_FOUNDER);
	return founder_rec && founder_rec->kind == UDB_STORAGE_VALUE_STRING &&
		   !strcasecmp(client->name, founder_rec->string) && has_user_mode(client, 'r');
}

static void udb_channel_modes_free(ModData *m)
{
	UdbChannelModeState *state = m->ptr;

	if (state)
	{
		safe_free(state->value);
		safe_free(state);
	}
	m->ptr = NULL;
}

static void udb_channel_ban_owners_free(ModData *m)
{
	UdbBanOwner *owner = m->ptr;

	while (owner)
	{
		UdbBanOwner *next = owner->next;
		safe_free(owner->ban);
		safe_free(owner->owner);
		safe_free(owner);
		owner = next;
	}
	m->ptr = NULL;
}

static void udb_channel_do_mode(Channel *channel, MessageTag *mtags, const char *modes, const char *parameters)
{
	char buf[512];
	char channel_name[CHANNELLEN + 1];
	char *p, *param;
	int myparc = 1;
	int i;
	char *myparv[MAXMODEPARAMS + 2];
	Client *source;

	if (!channel || !modes || !*modes || !do_mode)
		return;
	/* Removing +P may destroy an empty channel inside do_mode(). Keep only the
	 * immutable name needed by the post-operation debug notice. */
	strlcpy(channel_name, channel->name, sizeof(channel_name));
	source = udb_service_source(SKEY_CHANSERV);
	memset(myparv, 0, sizeof(myparv));
	myparv[0] = raw_strdup(modes);
	strlcpy(buf, parameters ? parameters : "", sizeof(buf));
	for (param = strtoken(&p, buf, " "); param; param = strtoken(&p, NULL, " "))
	{
		if (myparc > MAXMODEPARAMS)
		{
			for (i = 0; i < myparc; i++)
				safe_free(myparv[i]);
			return;
		}
		myparv[myparc++] = raw_strdup(param);
	}
	myparv[myparc] = NULL;

	do_mode(channel, source, mtags, myparc, (const char **)myparv, 0, 1);
	for (i = 0; i < myparc; i++)
		safe_free(myparv[i]);

	if (parameters && *parameters)
		udb_send_to_debugs(NULL, "Mode change on %s: %s %s", channel_name, modes,
						   strchr(modes, 'k') ? "<redacted>" : parameters);
	else
		udb_send_to_debugs(NULL, "Mode change on %s: %s", channel_name, modes);
}

static void udb_channel_set_modes(Channel *channel, const char *value)
{
	char modebuf[512];
	char *parabuf;

	if (!value || !*value)
		return;
	strlcpy(modebuf, value, sizeof(modebuf));
	parabuf = strchr(modebuf, ' ');
	if (parabuf)
		*parabuf++ = '\0';
	udb_channel_do_mode(channel, NULL, modebuf, parabuf ? parabuf : "");
}

static void udb_channel_reverse_modes(Channel *channel, const char *value)
{
	char modebuf[512];
	char inverse[512];
	char *parabuf;
	char *src;
	char *dst = inverse;

	if (!value || !*value)
		return;
	strlcpy(modebuf, value, sizeof(modebuf));
	parabuf = strchr(modebuf, ' ');
	if (parabuf)
		*parabuf++ = '\0';
	for (src = modebuf; *src && (size_t)(dst - inverse) < sizeof(inverse) - 1; src++)
	{
		if (*src == '+')
			*dst++ = '-';
		else if (*src == '-')
			*dst++ = '+';
		else
			*dst++ = *src;
	}
	*dst = '\0';
	udb_channel_do_mode(channel, NULL, inverse, parabuf ? parabuf : "");
}

static void udb_channel_apply_modes(Channel *channel, const char *value)
{
	UdbChannelModeState *state;

	if (!udb_channel_modes_md)
		return;
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (!state)
	{
		state = safe_alloc(sizeof(*state));
		moddata_channel(channel, udb_channel_modes_md).ptr = state;
	}
	if (state->value && !strcmp(state->value, value))
		return;
	udb_channel_reverse_modes(channel, state->value);
	udb_channel_set_modes(channel, value);
	safe_strdup(state->value, value);
}

static void udb_channel_remove_modes(Channel *channel)
{
	UdbChannelModeState *state;

	if (!udb_channel_modes_md)
		return;
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (state)
	{
		safe_free(state->value);
		safe_free(state);
		moddata_channel(channel, udb_channel_modes_md).ptr = NULL;
	}
}

static int udb_channel_option_enabled(UdbContext *ctx, UdbStorageProfile *chan_rec, unsigned long option)
{
	if (!chan_rec)
		return 0;
	UdbStorageField *rec = udb_channel_field(chan_rec, CKEY_OPTIONS);
	return rec && rec->kind == UDB_STORAGE_VALUE_NUMBER && (rec->number & option);
}

static int udb_channel_is_persistent(UdbContext *ctx, UdbStorageProfile *chan_rec)
{
	if (!chan_rec || udb_channel_field(chan_rec, CKEY_SUSPEND))
		return 0;
	return udb_channel_option_enabled(ctx, chan_rec, UDB_CHOPT_PERSISTENT);
}

static int udb_channel_is_oper_only(UdbContext *ctx, UdbStorageProfile *chan_rec)
{
	if (!chan_rec || udb_channel_field(chan_rec, CKEY_SUSPEND))
		return 0;
	return udb_channel_option_enabled(ctx, chan_rec, UDB_CHOPT_OPER_ONLY);
}

static int udb_channel_is_lock_modes(UdbContext *ctx, UdbStorageProfile *chan_rec)
{
	if (!chan_rec || udb_channel_field(chan_rec, CKEY_SUSPEND))
		return 0;
	UdbStorageField *rec = udb_channel_field(chan_rec, CKEY_OPTIONS);
	return rec && rec->kind == UDB_STORAGE_VALUE_NUMBER && (rec->number & UDB_CHOPT_LOCK_MODES);
}

static int udb_channel_is_lock_topic(UdbContext *ctx, UdbStorageProfile *chan_rec)
{
	if (!chan_rec || udb_channel_field(chan_rec, CKEY_SUSPEND))
		return 0;
	UdbStorageField *rec = udb_channel_field(chan_rec, CKEY_OPTIONS);
	return rec && rec->kind == UDB_STORAGE_VALUE_NUMBER && (rec->number & UDB_CHOPT_LOCK_TOPIC);
}

static int udb_channel_is_protect_bans(UdbContext *ctx, UdbStorageProfile *chan_rec)
{
	if (!chan_rec || udb_channel_field(chan_rec, CKEY_SUSPEND))
		return 0;
	UdbStorageField *rec = udb_channel_field(chan_rec, CKEY_OPTIONS);
	return rec && rec->kind == UDB_STORAGE_VALUE_NUMBER && (rec->number & UDB_CHOPT_PROTECT_BANS);
}

static void udb_channel_set_persistent(Channel *channel, int enabled)
{
	/* +P is supplied by an optional native module; do not emulate it. */
	if (!find_channel_mode_handler('P'))
		return;
	if (enabled && !has_channel_mode(channel, 'P'))
		udb_channel_do_mode(channel, NULL, "+P", "");
	else if (!enabled && has_channel_mode(channel, 'P'))
		udb_channel_do_mode(channel, NULL, "-P", "");
}

static void udb_channel_set_oper_only(Channel *channel, int enabled)
{
	int *owned;
	Cmode *oper_mode;

	/* +O is supplied by an optional native module. JOIN policy remains
	 * fail-closed from C::options even when the display mode is unavailable. */
	if (!udb_channel_oper_only_md)
		return;
	owned = &moddata_channel(channel, udb_channel_oper_only_md).i;
	oper_mode = find_channel_mode_handler('O');
	if (!oper_mode || oper_mode->unloaded || oper_mode->type != CMODE_NORMAL)
	{
		*owned = 0;
		return;
	}
	if (enabled && !has_channel_mode(channel, 'O'))
	{
		udb_channel_do_mode(channel, NULL, "+O", "");
		*owned = has_channel_mode(channel, 'O');
	}
	else if (!enabled && *owned && has_channel_mode(channel, 'O'))
	{
		*owned = 0;
		udb_channel_do_mode(channel, NULL, "-O", "");
	}
	else if (!enabled)
	{
		*owned = 0;
	}
}

static void udb_channel_reconcile_options(Channel *channel, int oper_only, int persistent)
{
	/* +P must keep a newly created empty channel alive before +O is applied.
	 * When removing +P, clear UDB-owned +O before -P can destroy it. */
	if (persistent)
		udb_channel_set_persistent(channel, 1);
	udb_channel_set_oper_only(channel, oper_only);
	if (!persistent)
		udb_channel_set_persistent(channel, 0);
}

static int udb_hook_rehash_complete_oper_only(void)
{
	Cmode *oper_mode = find_channel_mode_handler('O');
	Channel *channel;

	if (!udb_ctx || !udb_channel_oper_only_md)
		return 0;
	for (channel = channels; channel; channel = channel->nextch)
	{
		UdbStorageProfile *chan_rec;

		/* The core clears +O directly after REHASH_COMPLETE if its handler
		 * remains unloaded. MODECHAR_DEL is not emitted for that change. */
		if (!oper_mode || oper_mode->unloaded || oper_mode->type != CMODE_NORMAL)
		{
			moddata_channel(channel, udb_channel_oper_only_md).i = 0;
			continue;
		}
		chan_rec = NULL;
		if (udb_native_channel_load(NULL, channel->name, &chan_rec) == UDB_STORAGE_OK)
			udb_channel_set_oper_only(channel, udb_channel_is_oper_only(udb_ctx, chan_rec));
		else
			udb_channel_set_oper_only(channel, 0);
		udb_storage_profile_free(&chan_rec);
	}
	return 0;
}

static int udb_hook_modechar_del(Channel *channel, int modechar)
{
	if (modechar == 'O' && udb_channel_oper_only_md)
		moddata_channel(channel, udb_channel_oper_only_md).i = 0;
	return 0;
}

static UdbBanOwner *udb_channel_ban_owner_find(Channel *channel, const char *ban)
{
	UdbBanOwner *owner;

	if (!udb_channel_ban_owners_md || !ban)
		return NULL;
	for (owner = moddata_channel(channel, udb_channel_ban_owners_md).ptr; owner; owner = owner->next)
		if (!mycmp(owner->ban, ban))
			return owner;
	return NULL;
}

static void udb_channel_ban_owners_prune(Channel *channel)
{
	UdbBanOwner **owner;

	if (!udb_channel_ban_owners_md)
		return;
	for (owner = (UdbBanOwner **)&moddata_channel(channel, udb_channel_ban_owners_md).ptr; *owner;)
	{
		UdbBanOwner *current = *owner;
		if (ban_exists(channel->banlist, current->ban))
		{
			owner = &current->next;
			continue;
		}
		*owner = current->next;
		safe_free(current->ban);
		safe_free(current->owner);
		safe_free(current);
	}
}

static UdbBanSnapshot *udb_channel_ban_snapshot(Channel *channel)
{
	Ban *ban;
	UdbBanSnapshot *snapshot = NULL;

	for (ban = channel->banlist; ban; ban = ban->next)
	{
		UdbBanSnapshot *entry = safe_alloc(sizeof(*entry));
		safe_strdup(entry->ban, ban->banstr);
		entry->next = snapshot;
		snapshot = entry;
	}
	return snapshot;
}

static int udb_channel_ban_was_present(UdbBanSnapshot *snapshot, const char *ban)
{
	for (; snapshot; snapshot = snapshot->next)
		if (!mycmp(snapshot->ban, ban))
			return 1;
	return 0;
}

static void udb_channel_ban_snapshot_free(UdbBanSnapshot *snapshot)
{
	while (snapshot)
	{
		UdbBanSnapshot *next = snapshot->next;
		safe_free(snapshot->ban);
		safe_free(snapshot);
		snapshot = next;
	}
}

static void udb_channel_track_new_bans(Channel *channel, Client *client, UdbBanSnapshot *snapshot)
{
	Ban *ban;

	if (!udb_channel_ban_owners_md)
		return;
	for (ban = channel->banlist; ban; ban = ban->next)
	{
		UdbBanOwner *owner;
		if (udb_channel_ban_was_present(snapshot, ban->banstr) || udb_channel_ban_owner_find(channel, ban->banstr))
			continue;
		owner = safe_alloc(sizeof(*owner));
		safe_strdup(owner->ban, ban->banstr);
		safe_strdup(owner->owner, client->name);
		owner->next = moddata_channel(channel, udb_channel_ban_owners_md).ptr;
		moddata_channel(channel, udb_channel_ban_owners_md).ptr = owner;
	}
}

static void udb_channel_reconcile_founder(Channel *channel, UdbStorageProfile *chan_rec, int suspended)
{
	Member *member;

	for (member = channel->members; member; member = member->next)
	{
		/* The member's home server emits the network MODE exactly once. */
		if (!MyUser(member->client))
			continue;
		int is_founder = !suspended && udb_channel_is_identified_founder(member->client, chan_rec);
		if (is_founder)
		{
			if (!check_channel_access_member(member, "q"))
				udb_channel_do_mode(channel, NULL, "+q", member->client->name);
		}
		else if (check_channel_access_member(member, "q"))
		{
			/* UDB owns founder +q, so a profile replacement has one owner only. */
			udb_channel_do_mode(channel, NULL, "-q", member->client->name);
		}
	}
}

static void udb_channel_reconcile_registered(Channel *channel, int registered, int suspended, MessageTag *mtags)
{
	if (!channel)
		return;
	if (udb_channel_registration_md)
		moddata_channel(channel, udb_channel_registration_md).i = registered != 0;
	if (!find_channel_mode_handler('r'))
		return;
	if (registered && !suspended)
	{
		if (!has_channel_mode(channel, 'r'))
			udb_channel_do_mode(channel, mtags, "+r", "");
	}
	else if (has_channel_mode(channel, 'r'))
	{
		udb_channel_do_mode(channel, mtags, "-r", "");
	}
}

static void udb_channel_clear_topic(Channel *channel)
{
	Client *source = udb_service_source(SKEY_CHANSERV);

	safe_free(channel->topic);
	safe_free(channel->topic_nick);
	channel->topic_time = 0;
	if (channel->users > 0)
	{
		sendto_channel(channel, source, NULL, 0, 0, SEND_LOCAL, NULL, ":%s TOPIC %s :", source->name, channel->name);
	}
}

static void udb_channel_apply_topic(Channel *channel, UdbStorageField *topic_rec)
{
	Client *source;

	if (!topic_rec || topic_rec->kind != UDB_STORAGE_VALUE_STRING)
		return;
	if (channel->topic && !strcmp(channel->topic, topic_rec->string))
		return;
	source = udb_service_source(SKEY_CHANSERV);
	if (set_channel_topic)
		set_channel_topic(source, channel, NULL, topic_rec->string, source->name, TStime());
}

static void udb_channel_apply_subrecord(UdbContext *ctx, Channel *channel, UdbStorageProfile *chan_rec,
										const char *subkey, int is_new)
{
	int suspended = udb_channel_field(chan_rec, CKEY_SUSPEND) != NULL;

	/* A channel-profile replacement revokes every UDB-owned channel state;
	 * mirror that removal so the surviving profile restores it all. */
	if (!strcasecmp(subkey, chan_rec->entity))
	{
		UdbStorageField *mode_rec = udb_channel_field(chan_rec, CKEY_MODES);
		UdbStorageField *topic_rec = udb_channel_field(chan_rec, CKEY_TOPIC);

		udb_channel_reconcile_founder(channel, chan_rec, suspended);
		if (!suspended && mode_rec && mode_rec->kind == UDB_STORAGE_VALUE_STRING)
			udb_channel_apply_modes(channel, mode_rec->string);
		if (!suspended)
			udb_channel_apply_topic(channel, topic_rec);
		udb_channel_reconcile_options(channel, udb_channel_is_oper_only(ctx, chan_rec),
									  udb_channel_is_persistent(ctx, chan_rec));
		return;
	}

	if (!strcmp(subkey, CKEY_OPTIONS))
	{
		udb_channel_reconcile_options(channel, udb_channel_is_oper_only(ctx, chan_rec),
									  udb_channel_is_persistent(ctx, chan_rec));
		return;
	}

	UdbStorageField *sub_rec = udb_channel_field(chan_rec, subkey);
	if (!sub_rec || sub_rec->kind != UDB_STORAGE_VALUE_STRING)
		return;

	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, chan_rec, suspended);
	}
	else if (!strcmp(subkey, CKEY_MODES))
	{
		if (!suspended)
			udb_channel_apply_modes(channel, sub_rec->string);
	}
	else if (!strcmp(subkey, CKEY_TOPIC))
	{
		if (!suspended)
			udb_channel_apply_topic(channel, sub_rec);
	}
	else if (!strcmp(subkey, CKEY_SUSPEND))
	{
		udb_channel_reconcile_founder(channel, chan_rec, 1);
		udb_channel_reconcile_registered(channel, 1, 1, NULL);
		udb_channel_set_oper_only(channel, 0);
		udb_channel_set_persistent(channel, 0);
	}
}

static void udb_channel_remove_subrecord(UdbContext *ctx, Channel *channel, UdbStorageProfile *chan_rec,
										 const char *subkey)
{
	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, NULL, 0);
	}
	else if (!strcmp(subkey, CKEY_MODES))
	{
		udb_channel_remove_modes(channel);
	}
	else if (!strcmp(subkey, CKEY_OPTIONS))
	{
		udb_channel_set_oper_only(channel, 0);
		udb_channel_set_persistent(channel, 0);
	}
	else if (!strcmp(subkey, CKEY_TOPIC))
	{
		udb_channel_clear_topic(channel);
	}
	else if (!strcmp(subkey, CKEY_SUSPEND))
	{
		/* Lifting a suspension restores the registered marker, founder +q,
		 * modes, topic, OPER_ONLY +O, and persistent +P. */
		UdbStorageField *mode_rec = udb_channel_field(chan_rec, CKEY_MODES);
		UdbStorageField *topic_rec = udb_channel_field(chan_rec, CKEY_TOPIC);

		udb_channel_reconcile_founder(channel, chan_rec, 0);
		udb_channel_reconcile_registered(channel, 1, 0, NULL);
		if (mode_rec && mode_rec->kind == UDB_STORAGE_VALUE_STRING)
			udb_channel_apply_modes(channel, mode_rec->string);
		if (topic_rec)
			udb_channel_apply_topic(channel, topic_rec);
		/* DEL effects run before the candidate tree replaces the active tree,
		 * so read the surviving option bits without consulting old suspend. */
		udb_channel_reconcile_options(channel, udb_channel_option_enabled(ctx, chan_rec, UDB_CHOPT_OPER_ONLY),
									  udb_channel_option_enabled(ctx, chan_rec, UDB_CHOPT_PERSISTENT));
	}
	else if (!strcasecmp(subkey, chan_rec->entity))
	{
		int had_topic = udb_channel_field(chan_rec, CKEY_TOPIC) != NULL;

		udb_channel_reconcile_founder(channel, NULL, 0);
		udb_channel_reconcile_registered(channel, 0, 0, NULL);
		udb_channel_remove_modes(channel);
		if (had_topic)
			udb_channel_clear_topic(channel);
		/* -P may destroy an empty channel, so -O is always applied first. */
		udb_channel_set_oper_only(channel, 0);
		udb_channel_set_persistent(channel, 0);
	}
}

/* Publication operates only on copied scalar profiles. An empty channel may
 * disappear when -P is emitted; re-resolve by immutable name after that step. */
static void udb_channel_project_profile(Channel *channel, UdbStorageProfile *old, UdbStorageProfile *current)
{
	if (!current)
	{
		if (old)
			udb_channel_remove_subrecord(udb_ctx, channel, old, old->entity);
		return;
	}
	if (old && udb_channel_field(old, CKEY_TOPIC) && !udb_channel_field(current, CKEY_TOPIC))
		udb_channel_clear_topic(channel);
	if (old && udb_channel_field(old, CKEY_MODES) && !udb_channel_field(current, CKEY_MODES))
		udb_channel_remove_modes(channel);
	int suspended = udb_channel_field(current, CKEY_SUSPEND) != NULL;
	udb_channel_reconcile_founder(channel, current, suspended);
	udb_channel_reconcile_registered(channel, 1, suspended, NULL);
	if (!suspended)
	{
		UdbStorageField *modes = udb_channel_field(current, CKEY_MODES);
		if (modes && modes->kind == UDB_STORAGE_VALUE_STRING)
			udb_channel_apply_modes(channel, modes->string);
		udb_channel_apply_topic(channel, udb_channel_field(current, CKEY_TOPIC));
	}
	udb_channel_reconcile_options(channel, udb_channel_is_oper_only(udb_ctx, current),
								  udb_channel_is_persistent(udb_ctx, current));
	/* No use of channel after reconcile_options may have removed +P. */
}

/* A point/subtree mutation belongs to one channel; never enumerate the C
 * container to materialize that single owner's changed policy. */
static UdbStorageResult udb_channel_reconcile_entity(UdbStorageRead *before, UdbStorageRead *after, const char *name)
{
	UdbStorageProfile *old = NULL, *current = NULL;
	UdbStorageResult prior = udb_native_channel_load(before, name, &old);
	UdbStorageResult result = udb_native_channel_load(after, name, &current);
	if (prior < UDB_STORAGE_NOT_FOUND || result < UDB_STORAGE_NOT_FOUND)
		result = prior < UDB_STORAGE_NOT_FOUND ? prior : result;
	else
	{
		Channel *channel = find_channel(name);
		if (!channel && current && udb_channel_is_persistent(udb_ctx, current) && find_channel_mode_handler('P'))
		{
			channel = make_channel(current->entity);
			channel->creationtime = TStime();
			udb_channel_set_persistent(channel, 1);
		}
		if (channel)
			udb_channel_project_profile(channel, old, current);
		result = UDB_STORAGE_OK;
	}
	udb_storage_profile_free(&old);
	udb_storage_profile_free(&current);
	return result;
}

static UdbStorageResult udb_channel_reconcile_generations(UdbStorageRead *before, UdbStorageRead *after)
{
	UdbStorageResult result = UDB_STORAGE_OK;
	Channel *channel, *next;
	for (channel = channels; channel; channel = next)
	{
		next = channel->nextch;
		UdbStorageProfile *old = NULL, *current = NULL;
		UdbStorageResult prior = udb_native_channel_load(before, channel->name, &old);
		UdbStorageResult loaded = udb_native_channel_load(after, channel->name, &current);
		if (prior < UDB_STORAGE_NOT_FOUND || loaded < UDB_STORAGE_NOT_FOUND)
			result = prior < UDB_STORAGE_NOT_FOUND ? prior : loaded;
		else
			udb_channel_project_profile(channel, old, current);
		udb_storage_profile_free(&old);
		udb_storage_profile_free(&current);
	}
	if (result != UDB_STORAGE_OK)
		return result;
	/* Persistent policies may create channels not present in daemon memory.
	 * Only their unique options scalar is visited; no resident channel keydir. */
	UdbStorageBlockIterator *iterator = NULL;
	result = udb_storage_read_iterator(after, UDB_BLOCK_CHANNELS, NULL, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	UdbStorageRecord record;
	while ((result = udb_storage_block_iterator_next(iterator, &record)) == UDB_STORAGE_OK)
	{
		const UdbStorageRow *row = &record.row;
		if (row->depth != 2 || row->components[1].length != strlen(CKEY_OPTIONS) ||
			memcmp(row->components[1].data, CKEY_OPTIONS, row->components[1].length) ||
			row->kind != UDB_STORAGE_VALUE_NUMBER || !(row->number & UDB_CHOPT_PERSISTENT))
			continue;
		char name[CHANNELLEN + 1];
		if (!udb_native_span_string(row->components[0], name, sizeof(name)))
		{
			result = UDB_STORAGE_CORRUPT;
			break;
		}
		if (find_channel(name) || !find_channel_mode_handler('P'))
			continue;
		UdbStorageProfile *current = NULL;
		result = udb_native_channel_load(after, name, &current);
		if (result != UDB_STORAGE_OK)
			break;
		if (udb_channel_is_persistent(udb_ctx, current))
		{
			Channel *created = make_channel(current->entity);
			created->creationtime = TStime();
			/* +P must be materialized before any reentrant effect can remove
			 * this newly created empty channel. */
			udb_channel_set_persistent(created, 1);
			udb_channel_project_profile(created, NULL, current);
		}
		udb_storage_profile_free(&current);
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = UDB_STORAGE_OK;
	if (udb_storage_block_iterator_close(&iterator) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static void udb_channels_shutdown(void)
{
	Channel *channel, *next;
	for (channel = channels; channel; channel = next)
	{
		next = channel->nextch;
		/* Never revoke external ranks or modes on unregistered channels. */
		if (!udb_channel_registration_md || !moddata_channel(channel, udb_channel_registration_md).i)
			continue;
		udb_channel_reconcile_founder(channel, NULL, 0);
		udb_channel_reconcile_registered(channel, 0, 0, NULL);
		udb_channel_remove_modes(channel);
		udb_channel_set_oper_only(channel, 0);
		/* +P removal may destroy this channel; it is the final operation. */
		udb_channel_set_persistent(channel, 0);
	}
}

static int udb_channel_pre_join_profile(Client *client, Channel *channel, const char *key, UdbStorageProfile *chan_rec)
{
	(void)key;
	if (!chan_rec)
		return HOOK_CONTINUE;
	if (udb_channel_field(chan_rec, CKEY_FORBID))
		return HOOK_CONTINUE; /* Let CAN_JOIN provide the numeric. */
	if (udb_channel_field(chan_rec, CKEY_SUSPEND))
		return HOOK_CONTINUE;
	if (udb_channel_is_identified_founder(client, chan_rec))
		return HOOK_ALLOW; /* Preserve the established founder exemption. */
	return HOOK_CONTINUE;
}

static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key)
{
	UdbStorageProfile *profile = NULL;
	UdbStorageResult loaded = udb_native_channel_load(NULL, channel->name, &profile);
	if (loaded < UDB_STORAGE_NOT_FOUND)
		return HOOK_DENY;
	int result = udb_channel_pre_join_profile(client, channel, key, profile);
	udb_storage_profile_free(&profile);
	return result;
}

static int udb_channel_oper_join_profile(Client *client, Channel *channel, const char *key, UdbStorageProfile *chan_rec)
{
	if (!chan_rec)
		return HOOK_CONTINUE;
	if (IsOper(client) && udb_channel_is_oper_only(udb_ctx, chan_rec) && udb_channel_oper_only_md &&
		moddata_channel(channel, udb_channel_oper_only_md).i && has_channel_mode(channel, 'O') &&
		!ValidatePermissionsForPath("channel:operonly:join", client, NULL, channel, NULL))
	{
		Cmode *oper_mode = find_channel_mode_handler('O');
		char *errmsg = NULL;
		int result;

		if (!oper_mode || oper_mode->unloaded || oper_mode->type != CMODE_NORMAL)
			return HOOK_CONTINUE;
		/* The native +O handler requires an operclass ACL that OPER_ONLY does
		 * not require. Run the complete native JOIN check with only UDB's +O
		 * hidden, then restore it before any join or error is emitted. */
		channel->mode.mode &= ~oper_mode->mode;
		result = can_join(client, channel, key, &errmsg);
		channel->mode.mode |= oper_mode->mode;
		if (result)
		{
			if (result != -1)
			{
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
				sendnumericfmt(client, result, errmsg, channel->name);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
			}
			return HOOK_DENY;
		}
		return HOOK_ALLOW;
	}
	return HOOK_CONTINUE;
}

static int udb_hook_pre_local_join_oper_only(Client *client, Channel *channel, const char *key)
{
	UdbStorageProfile *profile = NULL;
	UdbStorageResult loaded = udb_native_channel_load(NULL, channel->name, &profile);
	if (loaded < UDB_STORAGE_NOT_FOUND)
		return HOOK_DENY;
	int result = udb_channel_oper_join_profile(client, channel, key, profile);
	udb_storage_profile_free(&profile);
	return result;
}

static int udb_channel_modes_has_added(const char *value, char wanted)
{
	char modebuf[512];
	char *p;
	const char *modes;
	int what = MODE_ADD;
	int enabled = 0;

	if (!value || !*value || strlen(value) >= sizeof(modebuf))
		return 0;
	strlcpy(modebuf, value, sizeof(modebuf));
	modes = strtoken(&p, modebuf, " ");
	if (!modes)
		return 0;
	for (; *modes; modes++)
	{
		if (*modes == '+')
			what = MODE_ADD;
		else if (*modes == '-')
			what = MODE_DEL;
		else if (*modes == wanted)
			enabled = what == MODE_ADD;
	}
	return enabled;
}

static int udb_channel_modes_key(const char *value, char *key_out, size_t key_out_size)
{
	char modebuf[512];
	char *p, *param;
	const char *modes;
	int what = MODE_ADD;

	if (key_out && key_out_size)
		key_out[0] = '\0';
	if (!value || !*value || strlen(value) >= sizeof(modebuf))
		return 0;
	strlcpy(modebuf, value, sizeof(modebuf));
	modes = strtoken(&p, modebuf, " ");
	if (!modes)
		return 0;
	for (; *modes; modes++)
	{
		Cmode *handler;
		int takes_parameter;
		if (*modes == '+')
		{
			what = MODE_ADD;
			continue;
		}
		if (*modes == '-')
		{
			what = MODE_DEL;
			continue;
		}
		handler = find_channel_mode_handler(*modes);
		if (!handler)
			return 0;
		takes_parameter = handler->paracount && (what == MODE_ADD || handler->unset_with_param);
		if (!takes_parameter)
			continue;
		param = strtoken(&p, NULL, " ");
		if (!param)
			return 0;
		if (*modes == 'k' && what == MODE_ADD)
		{
			if (!key_out || !key_out_size || strlcpy(key_out, param, key_out_size) >= key_out_size)
				return 0;
			return 1;
		}
	}
	return 0;
}

static void udb_escape_percent(const char *src, char *dst, size_t dst_size)
{
	size_t pos = 0;

	if (!dst || dst_size == 0)
		return;
	if (src)
	{
		for (; *src && pos + 1 < dst_size; src++)
		{
			if (*src == '%')
			{
				if (pos + 2 >= dst_size)
					break;
				dst[pos++] = '%';
			}
			dst[pos++] = *src;
		}
	}
	dst[pos] = '\0';
}

static int udb_channel_can_join_profile(Client *client, Channel *channel, const char *key, char **errmsg,
										UdbStorageProfile *chan_rec, UdbStorageRead *read)
{
	static char errbuf[512];
	UdbStorageField *forbid_rec;
	int is_founder;

	if (!chan_rec)
		return 0;
	if (IsOper(client))
		return 0;
	forbid_rec = udb_channel_field(chan_rec, CKEY_FORBID);
	if (forbid_rec)
	{
		char escaped_reason[512];

		udb_escape_percent(forbid_rec->kind == UDB_STORAGE_VALUE_STRING ? forbid_rec->string : "No reason given",
						   escaped_reason, sizeof(escaped_reason));
		snprintf(errbuf, sizeof(errbuf), "%%s :This channel is forbidden. Reason: %s", escaped_reason);
		*errmsg = errbuf;
		return ERR_FORBIDDENCHANNEL;
	}
	if (udb_channel_field(chan_rec, CKEY_SUSPEND))
		return 0;
	is_founder = udb_channel_is_identified_founder(client, chan_rec);
	if (udb_channel_is_oper_only(udb_ctx, chan_rec) && !is_founder)
	{
		*errmsg = STR_ERR_OPERONLY;
		return ERR_OPERONLY;
	}
	if (channel->users == 0 && !is_founder)
	{
		UdbStorageField *modes_rec = udb_channel_field(chan_rec, CKEY_MODES);
		const char *configured_modes =
			modes_rec && modes_rec->kind == UDB_STORAGE_VALUE_STRING ? modes_rec->string : NULL;

		if (!has_channel_mode(channel, 'i') && udb_channel_modes_has_added(configured_modes, 'i') &&
			!find_invex(channel, client))
		{
			*errmsg = STR_ERR_INVITEONLYCHAN;
			return ERR_INVITEONLYCHAN;
		}
		if (!has_channel_mode(channel, 'R') && udb_channel_modes_has_added(configured_modes, 'R') &&
			!IsLoggedIn(client))
		{
			*errmsg = STR_ERR_NEEDREGGEDNICK;
			return ERR_NEEDREGGEDNICK;
		}
		if (!has_channel_mode(channel, 'z') && udb_channel_modes_has_added(configured_modes, 'z') &&
			!IsSecureConnect(client))
		{
			*errmsg = STR_ERR_SECUREONLYCHAN;
			return ERR_SECUREONLYCHAN;
		}
	}
	/* Native +k normally materializes after the first successful JOIN, but a
	 * persistent empty channel can already have one.  Never let the persisted
	 * C::modes key compete with the native mode when it is materialized. */
	if (channel->users == 0 && !cm_getparameter(channel, 'k') && !is_founder)
	{
		UdbStorageField *modes_rec = udb_channel_field(chan_rec, CKEY_MODES);
		char configured_key[512];
		if (modes_rec && modes_rec->kind == UDB_STORAGE_VALUE_STRING &&
			udb_channel_modes_key(modes_rec->string, configured_key, sizeof(configured_key)) &&
			(!key || strcmp(key, configured_key)))
		{
			*errmsg = STR_ERR_BADCHANNELKEY;
			return ERR_BADCHANNELKEY;
		}
	}
	if (!is_founder)
	{
		UdbStorageRow prefix = {0};
		prefix.block = UDB_BLOCK_CHANNELS;
		prefix.depth = 2;
		prefix.components[0].data = (const unsigned char *)chan_rec->entity;
		prefix.components[0].length = (uint32_t)strlen(chan_rec->entity);
		prefix.components[1].data = (const unsigned char *)CKEY_ACCESS;
		prefix.components[1].length = (uint32_t)strlen(CKEY_ACCESS);
		UdbStorageResult access = udb_storage_read_exists(read, &prefix);
		if (access == UDB_STORAGE_OK)
		{
			prefix.components[2].data = (const unsigned char *)client->name;
			prefix.components[2].length = (uint32_t)strlen(client->name);
			prefix.depth = 3;
			access = udb_storage_read_exists(read, &prefix);
			if (access != UDB_STORAGE_OK || !has_user_mode(client, 'r'))
			{
				snprintf(errbuf, sizeof(errbuf), "%%s :You must be authorized and identified to join this channel");
				*errmsg = errbuf;
				return ERR_FORBIDDENCHANNEL;
			}
		}
		else if (access < UDB_STORAGE_NOT_FOUND)
		{
			*errmsg = "%s :Channel authorization unavailable";
			return ERR_FORBIDDENCHANNEL;
		}
	}
	return 0;
}

static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg)
{
	UdbStorageRead *read = NULL;
	UdbStorageProfile *profile = NULL;
	UdbStorageResult loaded =
		udb_ctx && udb_ctx->storage ? udb_storage_read_open(udb_ctx->storage, &read) : UDB_STORAGE_ERROR;
	if (loaded == UDB_STORAGE_OK)
		loaded = udb_native_channel_load(read, channel->name, &profile);
	int result;
	if (loaded < UDB_STORAGE_NOT_FOUND)
	{
		*errmsg = "%s :Channel authorization unavailable";
		result = ERR_FORBIDDENCHANNEL;
	}
	else
		result = udb_channel_can_join_profile(client, channel, key, errmsg, profile, read);
	udb_storage_profile_free(&profile);
	if (read && udb_storage_read_close(&read) != UDB_STORAGE_OK)
	{
		*errmsg = "%s :Channel authorization unavailable";
		result = ERR_FORBIDDENCHANNEL;
	}
	return result;
}

static int udb_channel_invite_profile(Client *client, Channel *channel, UdbStorageProfile *chan_rec)
{

	if (!chan_rec || IsOper(client))
		return HOOK_CONTINUE;
	if (udb_channel_field(chan_rec, CKEY_FORBID))
		return HOOK_DENY;
	if (udb_channel_field(chan_rec, CKEY_SUSPEND))
		return HOOK_CONTINUE;
	if (udb_channel_is_oper_only(udb_ctx, chan_rec) && !udb_channel_is_identified_founder(client, chan_rec))
		return HOOK_DENY;
	return HOOK_CONTINUE;
}

static int udb_hook_invite_bypass(Client *client, Channel *channel)
{
	UdbStorageProfile *profile = NULL;
	UdbStorageResult loaded = udb_native_channel_load(NULL, channel->name, &profile);
	if (loaded < UDB_STORAGE_NOT_FOUND)
		return HOOK_DENY;
	int result = udb_channel_invite_profile(client, channel, profile);
	udb_storage_profile_free(&profile);
	return result;
}

static void udb_channel_join_profile(Client *client, Channel *channel, MessageTag *mtags, UdbStorageProfile *chan_rec)
{
	UdbStorageField *suspend_rec;
	int suspended;

	if (!client || !MyUser(client) || !chan_rec)
		return;

	suspend_rec = udb_channel_field(chan_rec, CKEY_SUSPEND);
	suspended = suspend_rec != NULL;
	if (suspended && !IsULine(client))
		udb_send_service_notice(client, SKEY_CHANSERV, "This channel is suspended. Reason: %s",
								suspend_rec->kind == UDB_STORAGE_VALUE_STRING ? suspend_rec->string
																			  : "No reason given");

	if (channel->users == 1)
	{
		if (!suspended)
		{
			/* A registered channel assigns founder authority exclusively as +q. */
			if (!IsServer(client) && !IsULine(client))
			{
				udb_channel_do_mode(channel, mtags, "-o", client->name);
			}

			udb_channel_reconcile_registered(channel, 1, 0, mtags);
			udb_channel_apply_subrecord(udb_ctx, channel, chan_rec, CKEY_MODES, 0);
			udb_channel_apply_subrecord(udb_ctx, channel, chan_rec, CKEY_TOPIC, 0);
			udb_channel_reconcile_options(channel, udb_channel_is_oper_only(udb_ctx, chan_rec),
										  udb_channel_is_persistent(udb_ctx, chan_rec));
		}
		else
		{
			udb_channel_reconcile_registered(channel, 1, 1, mtags);
			udb_channel_set_oper_only(channel, 0);
			udb_channel_set_persistent(channel, 0);
		}
	}

	/* Founder +q is UDB-owned and must have exactly one current holder. */
	udb_channel_reconcile_founder(channel, chan_rec, suspended);
}

static void handle_join(Client *client, Channel *channel, MessageTag *mtags)
{
	if (!client || !MyUser(client))
		return;
	UdbStorageProfile *profile = NULL;
	if (udb_native_channel_load(NULL, channel->name, &profile) == UDB_STORAGE_OK)
		udb_channel_join_profile(client, channel, mtags, profile);
	udb_storage_profile_free(&profile);
}

static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags)
{
	handle_join(client, channel, mtags);
	return 0;
}

static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags)
{
	handle_join(client, channel, mtags);
	return 0;
}

static int udb_channel_mode_has_change(const char *modes)
{
	int what = 0;

	for (; modes && *modes; modes++)
	{
		if (*modes == '+')
		{
			what = MODE_ADD;
			continue;
		}
		if (*modes == '-')
		{
			what = MODE_DEL;
			continue;
		}
		if (what != 0)
		{
			/* List modes and member ranks are not locked by lock_modes. */
			if (*modes == 'b' || *modes == 'e' || *modes == 'I' || strchr("qaohv", *modes))
				continue;
			return 1;
		}
	}
	return 0;
}

static int udb_channel_mode_has_ban_add(const char *modes)
{
	int what = 0;

	for (; modes && *modes; modes++)
	{
		if (*modes == '+')
			what = MODE_ADD;
		else if (*modes == '-')
			what = MODE_DEL;
		else if (*modes == 'b' && what == MODE_ADD)
			return 1;
	}
	return 0;
}

static int udb_channel_member_rank(Client *client, Channel *channel)
{
	if (check_channel_access(client, channel, "q"))
		return RANK_CHANOWNER;
	if (check_channel_access(client, channel, "a"))
		return RANK_CHANADMIN;
	if (check_channel_access(client, channel, "o"))
		return RANK_CHANOP;
	if (check_channel_access(client, channel, "h"))
		return RANK_HALFOP;
	if (check_channel_access(client, channel, "v"))
		return RANK_VOICE;
	return RANK_VOICE - 1;
}

static int udb_channel_blocks_secure_ops(Client *client, Channel *channel, int parc, const char *parv[])
{
	const char *modes = parv[2];
	int what = MODE_ADD;
	int param = 3;
	int actor_rank = udb_channel_member_rank(client, channel);

	for (; modes && *modes; modes++)
	{
		Cmode *handler;
		int takes_parameter;
		int mode_rank = 0;
		const char *target_name = NULL;

		if (*modes == '+')
		{
			what = MODE_ADD;
			continue;
		}
		if (*modes == '-')
		{
			what = MODE_DEL;
			continue;
		}
		if (*modes == 'b' || *modes == 'e' || *modes == 'I')
			takes_parameter = 1;
		else
		{
			handler = find_channel_mode_handler(*modes);
			takes_parameter = handler && handler->paracount && (what == MODE_ADD || handler->unset_with_param);
		}
		if (takes_parameter && param < parc)
			target_name = parv[param++];

		switch (*modes)
		{
		case 'q':
			mode_rank = RANK_CHANOWNER;
			break;
		case 'a':
			mode_rank = RANK_CHANADMIN;
			break;
		case 'o':
			mode_rank = RANK_CHANOP;
			break;
		case 'h':
			mode_rank = RANK_HALFOP;
			break;
		case 'v':
			mode_rank = RANK_VOICE;
			break;
		default:
			continue;
		}
		if (!target_name)
			continue; /* Native MODE reports or ignores missing parameters. */
		if (actor_rank == RANK_CHANOWNER)
			continue;
		if (what == MODE_ADD)
			return 1;
		if (actor_rank > mode_rank)
			continue;
		if (find_user(target_name, NULL) == client)
			continue; /* Preserve native self-demotion rules. */
		return 1;
	}
	return 0;
}

static int udb_channel_blocks_ban_removal(Client *client, Channel *channel, int parc, const char *parv[])
{
	const char *modes = parv[2];
	int what = 0;
	int param = 3;

	for (; modes && *modes; modes++)
	{
		Cmode *handler;
		int takes_parameter;

		if (*modes == '+')
		{
			what = MODE_ADD;
			continue;
		}
		if (*modes == '-')
		{
			what = MODE_DEL;
			continue;
		}
		if (*modes == 'b' || *modes == 'e' || *modes == 'I')
		{
			takes_parameter = 1;
		}
		else
		{
			handler = find_channel_mode_handler(*modes);
			takes_parameter = handler && handler->paracount && (what == MODE_ADD || handler->unset_with_param);
		}
		if (!takes_parameter || param >= parc)
			continue;
		if (*modes == 'b' && what == MODE_DEL)
		{
			const char *ban = clean_ban_mask(parv[param], MODE_DEL, EXBTYPE_BAN, client, channel, 0);
			UdbBanOwner *owner = udb_channel_ban_owner_find(channel, ban);
			if (owner && strcasecmp(owner->owner, client->name))
			{
				udb_send_service_notice(client, SKEY_CHANSERV, "You may not remove the UDB-protected ban %s",
										ban ? ban : parv[param]);
				return 1;
			}
		}
		param++;
	}
	return 0;
}

static void udb_channel_override_mode_profile(CommandOverride *ovr, ClientContext *clictx, Client *client,
											  MessageTag *recv_mtags, int parc, const char *parv[],
											  UdbStorageProfile *chan_rec)
{
	Channel *channel;
	char channel_name[CHANNELLEN + 1];
	UdbBanSnapshot *snapshot = NULL;
	int is_founder;
	int had_ban_add = 0;

	if (!MyUser(client) || parc < 3 || !IsChannelName(parv[1]))
	{
		CallCommandOverride(ovr, clictx, client, recv_mtags, parc, parv);
		return;
	}
	channel = find_channel(parv[1]);
	if (!chan_rec || udb_channel_field(chan_rec, CKEY_SUSPEND))
	{
		CallCommandOverride(ovr, clictx, client, recv_mtags, parc, parv);
		return;
	}
	strlcpy(channel_name, channel->name, sizeof(channel_name));
	is_founder = udb_channel_is_identified_founder(client, chan_rec);
	if (udb_channel_is_lock_modes(udb_ctx, chan_rec) && udb_channel_mode_has_change(parv[2]))
	{
		udb_send_service_notice(client, SKEY_CHANSERV,
								"You do not have permission to change modes in %s (locked by UDB)", channel->name);
		return;
	}
	/* Late identification must not require a rejoin. Elevate only an isolated
	 * self-recovery request, never the rest of a caller-supplied mode command. */
	if (!strcmp(ovr->command->cmd, "MODE") && parc == 4 && !strcmp(parv[2], "+q") && !BadPtr(parv[3]) && is_founder &&
		IsMember(client, channel) && find_user(parv[3], NULL) == client)
	{
		if (!check_channel_access(client, channel, "q"))
			udb_channel_do_mode(channel, recv_mtags, "+q", client->name);
		return;
	}
	if (strcmp(ovr->command->cmd, "SAMODE") && udb_channel_option_enabled(udb_ctx, chan_rec, UDB_CHOPT_SECURE_OPS) &&
		udb_channel_blocks_secure_ops(client, channel, parc, parv))
	{
		udb_send_service_notice(client, SKEY_CHANSERV,
								"You do not have permission to change member ranks in %s (SECURE_OPS)", channel->name);
		return;
	}
	udb_channel_ban_owners_prune(channel);
	if (udb_channel_is_protect_bans(udb_ctx, chan_rec) && !is_founder && !IsOper(client) &&
		udb_channel_blocks_ban_removal(client, channel, parc, parv))
		return;
	/* Retain local ban ownership even before the protection option is enabled. */
	if (udb_channel_mode_has_ban_add(parv[2]))
	{
		had_ban_add = 1;
		snapshot = udb_channel_ban_snapshot(channel);
	}
	CallCommandOverride(ovr, clictx, client, recv_mtags, parc, parv);
	channel = find_channel(channel_name);
	if (had_ban_add)
	{
		if (channel)
			udb_channel_track_new_bans(channel, client, snapshot);
		udb_channel_ban_snapshot_free(snapshot);
	}
	if (channel)
		udb_channel_ban_owners_prune(channel);
}

CMD_OVERRIDE_FUNC(udb_override_mode)
{
	UdbStorageProfile *profile = NULL;
	Channel *channel = MyUser(client) && parc >= 2 && IsChannelName(parv[1]) ? find_channel(parv[1]) : NULL;
	if (channel && udb_native_channel_load(NULL, channel->name, &profile) < UDB_STORAGE_NOT_FOUND)
	{
		udb_send_service_notice(client, SKEY_CHANSERV, "Channel policy unavailable");
		return;
	}
	udb_channel_override_mode_profile(ovr, clictx, client, recv_mtags, parc, parv, profile);
	udb_storage_profile_free(&profile);
}

static void udb_channel_override_topic_profile(CommandOverride *ovr, ClientContext *clictx, Client *client,
											   MessageTag *recv_mtags, int parc, const char *parv[],
											   UdbStorageProfile *chan_rec)
{
	Channel *channel;

	if (!MyUser(client) || parc < 2 || !IsChannelName(parv[1]))
	{
		CallCommandOverride(ovr, clictx, client, recv_mtags, parc, parv);
		return;
	}
	if (parc < 3 || BadPtr(parv[2]))
	{
		CallCommandOverride(ovr, clictx, client, recv_mtags, parc, parv);
		return;
	}
	channel = find_channel(parv[1]);
	if (!chan_rec || udb_channel_field(chan_rec, CKEY_SUSPEND))
	{
		CallCommandOverride(ovr, clictx, client, recv_mtags, parc, parv);
		return;
	}
	if (udb_channel_is_lock_topic(udb_ctx, chan_rec))
	{
		udb_send_service_notice(client, SKEY_CHANSERV,
								"You do not have permission to change the topic in %s (locked by UDB)", channel->name);
		return;
	}
	UdbStorageField *topic_rec = udb_channel_field(chan_rec, CKEY_TOPIC);
	if (topic_rec)
	{
		int is_founder = udb_channel_is_identified_founder(client, chan_rec);
		if (!is_founder)
		{
			sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
			return;
		}
	}
	CallCommandOverride(ovr, clictx, client, recv_mtags, parc, parv);
}

CMD_OVERRIDE_FUNC(udb_override_topic)
{
	UdbStorageProfile *profile = NULL;
	Channel *channel = MyUser(client) && parc >= 2 && IsChannelName(parv[1]) ? find_channel(parv[1]) : NULL;
	if (channel && udb_native_channel_load(NULL, channel->name, &profile) < UDB_STORAGE_NOT_FOUND)
	{
		udb_send_service_notice(client, SKEY_CHANSERV, "Channel policy unavailable");
		return;
	}
	udb_channel_override_topic_profile(ovr, clictx, client, recv_mtags, parc, parv, profile);
	udb_storage_profile_free(&profile);
}

static const char *udb_channel_pre_topic_profile(Client *client, Channel *channel, const char *topic,
												 UdbStorageProfile *chan_rec)
{
	if (IsServer(client))
		return topic;

	if (!chan_rec || udb_channel_field(chan_rec, CKEY_SUSPEND))
		return topic;

	if (udb_channel_is_lock_topic(udb_ctx, chan_rec))
	{
		udb_send_service_notice(client, SKEY_CHANSERV,
								"You do not have permission to change the topic in %s (locked by UDB)", channel->name);
		return NULL;
	}

	UdbStorageField *topic_rec = udb_channel_field(chan_rec, CKEY_TOPIC);
	if (topic_rec)
	{
		int is_founder = udb_channel_is_identified_founder(client, chan_rec);
		if (!is_founder)
		{
			sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
			return NULL;
		}
	}
	return topic;
}

static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic)
{
	if (IsServer(client))
		return topic;
	UdbStorageProfile *profile = NULL;
	if (udb_native_channel_load(NULL, channel->name, &profile) < UDB_STORAGE_NOT_FOUND)
		return NULL;
	const char *result = udb_channel_pre_topic_profile(client, channel, topic, profile);
	udb_storage_profile_free(&profile);
	return result;
}

static void udb_channels_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_modes";
	mreq.type = MODDATATYPE_CHANNEL;
	mreq.free = udb_channel_modes_free;
	udb_channel_modes_md = ModDataAdd(modinfo->handle, mreq);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_registration";
	mreq.type = MODDATATYPE_CHANNEL;
	udb_channel_registration_md = ModDataAdd(modinfo->handle, mreq);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_oper_only";
	mreq.type = MODDATATYPE_CHANNEL;
	udb_channel_oper_only_md = ModDataAdd(modinfo->handle, mreq);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_ban_owners";
	mreq.type = MODDATATYPE_CHANNEL;
	mreq.free = udb_channel_ban_owners_free;
	udb_channel_ban_owners_md = ModDataAdd(modinfo->handle, mreq);

	HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, udb_hook_can_join);
	HookAdd(modinfo->handle, HOOKTYPE_INVITE_BYPASS, 0, udb_hook_invite_bypass);
	HookAdd(modinfo->handle, HOOKTYPE_MODECHAR_DEL, 0, udb_hook_modechar_del);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH_COMPLETE, 0, udb_hook_rehash_complete_oper_only);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, udb_hook_pre_local_join);
	/* Run after native pre-JOIN hooks before the scoped +O compatibility check. */
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, INT_MAX, udb_hook_pre_local_join_oper_only);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, udb_hook_local_join);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_JOIN, 0, udb_hook_remote_join);
	HookAddConstString(modinfo->handle, HOOKTYPE_PRE_LOCAL_TOPIC, 0, udb_hook_pre_topic);
}

static int udb_channels_load(ModuleInfo *modinfo)
{
	CommandOverrideAdd(modinfo->handle, "MODE", 0, udb_override_mode);
	CommandOverrideAdd(modinfo->handle, "SAMODE", 0, udb_override_mode);
	CommandOverrideAdd(modinfo->handle, "TOPIC", 0, udb_override_topic);
	return 0;
}

/* End of udb_channels.c.inc */

/* IP management: clones, nolines, host overrides */
/* Inlined: udb_ips.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Clone Limits, Protected Base Cloaks & IP Host Overlays
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

typedef struct UdbIpHostState UdbIpHostState;
/* Track the I::host overlay UDB applied; revocation must preserve a later
 * external host replacement and otherwise recover the protected base. */
struct UdbIpHostState
{
	char key[256];
	char applied_host[HOSTLEN + 1];
};

static ModDataInfo *udb_ip_host_md = NULL;
static ModDataInfo *udb_base_cloak_md = NULL;

typedef struct UdbBaseCloak
{
	Client *client; /* Bound only to the client that owns this ModData. */
	char host[HOSTLEN + 1];
} UdbBaseCloak;

static void udb_ip_restore_host(Client *client, const char *ip_key);

static const char *udb_ip_visible_host(Client *client)
{
	if (client && client->user && IsHidden(client) && client->user->virthost)
		return client->user->virthost;
	return client && client->user ? client->user->realhost : "";
}

static void udb_ip_notify_host_change(Client *client, const char *notice)
{
	if (!MyUser(client))
		return;
	sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, udb_ip_visible_host(client));
	/* Native SETHOST implicitly adds +t on receivers. Base-host transitions
	 * must explicitly leave the custom-host flag unset there too. */
	if (!IsSetHost(client))
		sendto_server(client, 0, 0, NULL, ":%s UMODE2 -t", client->id);
	userhost_changed(client);
	if (notice)
		udb_send_service_notice(client, SKEY_IPSERV, "%s", notice);
}

static void udb_ip_host_state_free(ModData *m)
{
	safe_free(m->ptr);
}

static void udb_ip_save_host_state(Client *client, const char *key)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (state)
		return;
	state = safe_alloc(sizeof(*state));
	strlcpy(state->key, key, sizeof(state->key));
	moddata_local_client(client, udb_ip_host_md).ptr = state;
}

static int udb_ip_safe_host(Client *client, const char *host)
{
	unsigned char a[16], b[16];
	const int families[] = {AF_INET, AF_INET6};
	size_t i;

	if (!udb_vhost_valid(host))
		return 0;
	if (!client || !client->user)
		return 1;
	if (!strcasecmp(host, client->user->realhost))
		return 0;
	if (!client->ip)
		return 1;
	if (!strcasecmp(host, client->ip))
		return 0;
	for (i = 0; i < sizeof(families) / sizeof(families[0]); i++)
	{
		if (inet_pton(families[i], host, a) == 1 && inet_pton(families[i], client->ip, b) == 1 &&
			!memcmp(a, b, families[i] == AF_INET ? 4 : 16))
			return 0;
	}
	return 1;
}

static UdbBaseCloak *udb_ip_base_state(Client *client, int create)
{
	UdbBaseCloak *base;

	if (!client || !udb_base_cloak_md)
		return NULL;
	base = moddata_client(client, udb_base_cloak_md).ptr;
	if (!base && create)
		base = moddata_client(client, udb_base_cloak_md).ptr = safe_alloc(sizeof(*base));
	if (base)
		base->client = client;
	return base;
}

static int udb_ip_install_base(Client *client)
{
	UdbBaseCloak *base = udb_ip_base_state(client, 0);

	if (!base || !valid_host(base->host, 0) || !udb_ip_safe_host(client, base->host))
		return 0;
	strlcpy(client->user->cloakedhost, base->host, sizeof(client->user->cloakedhost));
	/* Existing users may have deliberately chosen -x. Keep their base
	 * available for +x without cancelling that choice during refresh/burst. */
	if (!IsUser(client))
		client->umodes |= UMODE_HIDE;
	if (!IsSetHost(client) || !udb_ip_safe_host(client, client->user->virthost))
	{
		safe_strdup(client->user->virthost, base->host);
		client->umodes &= ~UMODE_SETHOST;
	}
	return 1;
}

static const char *udb_ip_base_serialize(ModData *m)
{
	UdbBaseCloak *base = m->ptr;
	return base && valid_host(base->host, 0) && udb_ip_safe_host(base->client, base->host) ? base->host : NULL;
}

static void udb_ip_base_unserialize(const char *host, ModData *m)
{
	UdbBaseCloak *base = m->ptr;
	char oldhost[HOSTLEN + 1];
	Client *client = base ? base->client : NULL;

	/* Invalid/empty updates never erase the last accepted protected base. */
	if (!udb_ip_safe_host(client, host) || !valid_host(host, 0))
		return;
	if (!base)
		base = m->ptr = safe_alloc(sizeof(*base));
	if (client && client->user)
	{
		strlcpy(oldhost, udb_ip_visible_host(client), sizeof(oldhost));
		userhost_save_current(client);
	}
	strlcpy(base->host, host, sizeof(base->host));
	if (client && client->user && udb_ip_install_base(client) && strcmp(oldhost, udb_ip_visible_host(client)))
		userhost_changed(client);
}

static int udb_ip_tkl_is_owned(TKL *tkl)
{
	return tkl && TKLIsBanException(tkl) && tkl->set_by && tkl->ptr.banexception && tkl->ptr.banexception->reason &&
		   !strcmp(tkl->set_by, "UDB") && !strcmp(tkl->ptr.banexception->reason, "UDB Nolines Exemption");
}

static void udb_ip_apply_host(Client *client, const char *ip_key, const char *host)
{
	UdbIpHostState *state;

	if (!udb_ip_safe_host(client, host))
		return;
	udb_ip_save_host_state(client, ip_key);
	state = moddata_local_client(client, udb_ip_host_md).ptr;
	strlcpy(state->applied_host, host, sizeof(state->applied_host));
	safe_strdup(client->user->virthost, host);
	client->umodes |= UMODE_HIDE | UMODE_SETHOST;
}

static void udb_ip_restore_host(Client *client, const char *ip_key)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (!state || strcasecmp(state->key, ip_key))
		return;
	/* Release only this I::host overlay, never an external replacement. */
	if (client->user && client->user->virthost && !strcmp(client->user->virthost, state->applied_host))
	{
		UdbBaseCloak *base = udb_ip_base_state(client, 0);
		if (base && udb_ip_safe_host(client, base->host))
		{
			safe_strdup(client->user->virthost, base->host);
			client->umodes = (client->umodes | UMODE_HIDE) & ~UMODE_SETHOST;
		}
	}
	udb_ip_host_state_free(&moddata_local_client(client, udb_ip_host_md));
	moddata_local_client(client, udb_ip_host_md).ptr = NULL;
}

static int udb_ip_derive_vhost(Client *client, char *host, size_t hostlen)
{
	const char *realhost = client->user->realhost;
	unsigned char key[32], digest[EVP_MAX_MD_SIZE];
	unsigned int digestlen;
	char input[HOSTLEN + INET6_ADDRSTRLEN + 32];
	size_t i;

	if (!udb_ctx || !udb_ctx->encryption_key || !udb_ctx->suffix || !client->ip)
		return 0;
	for (i = 0; i < sizeof(key); i++)
		sscanf(udb_ctx->encryption_key + (i * 2), "%2hhx", &key[i]);
	snprintf(input, sizeof(input), "UDB-vhost-v1|%s|%s", client->ip, realhost);
	if (!HMAC(EVP_sha256(), key, sizeof(key), (unsigned char *)input, strlen(input), digest, &digestlen) ||
		digestlen < 16)
		return 0;
	for (i = 0; i < 16; i++)
		snprintf(host + (i * 2), hostlen - (i * 2), "%02x", digest[i]);
	strlcat(host, udb_ctx->suffix, hostlen);
	return 1;
}

static int udb_ip_reapply_vhost(Client *client)
{
	UdbBaseCloak *base;
	if (!client || !MyUser(client))
		return 0;
	base = udb_ip_base_state(client, 0);
	if (!base || !valid_host(base->host, 0) || !udb_ip_safe_host(client, base->host))
		return 0;
	userhost_save_current(client);
	safe_strdup(client->user->virthost, base->host);
	client->umodes = (client->umodes | UMODE_HIDE) & ~UMODE_SETHOST;
	udb_ip_notify_host_change(client, "*** Your base cloak has been restored");
	return 1;
}

static void udb_ip_refresh_derived_host(Client *client)
{
	UdbBaseCloak *base;
	char host[HOSTLEN + 1], oldhost[HOSTLEN + 1];
	int changed;

	if (!udb_base_cloak_md || !client || !client->user || !MyConnect(client))
		return;
	base = udb_ip_base_state(client, 1);
	if (!udb_ip_derive_vhost(client, host, sizeof(host)))
	{
		if (udb_ip_safe_host(client, base->host))
			strlcpy(host, base->host, sizeof(host));
		else
			make_cloakedhost(client, client->user->realhost, host, sizeof(host));
	}
	if (!udb_ip_safe_host(client, host))
		return;
	changed = strcmp(base->host, host);
	strlcpy(oldhost, udb_ip_visible_host(client), sizeof(oldhost));
	if (MyUser(client))
		userhost_save_current(client);
	strlcpy(base->host, host, sizeof(base->host));
	udb_ip_install_base(client);
	if (MyUser(client) && changed)
		broadcast_md_client_cmd(NULL, &me, client, "udb_base_cloak", base->host);
	if (MyUser(client) && strcmp(oldhost, udb_ip_visible_host(client)))
		udb_ip_notify_host_change(client, "*** Your base cloak is now active");
}

static void udb_ip_refresh_derived_hosts(void)
{
	Client *client;

	if (!udb_ip_host_md)
		return;
	list_for_each_entry(client, &lclient_list, lclient_node) udb_ip_refresh_derived_host(client);
}

static void udb_ip_set_nolines(const char *key, const char *value)
{
	TKL *tkl = find_tkl_banexception(TKL_EXCEPTION, "*", key, 0);
	if (tkl && !udb_ip_tkl_is_owned(tkl))
		return; /* Never replace an externally owned exemption. */
	if (tkl && value && tkl->ptr.banexception->bantypes && !strcmp(tkl->ptr.banexception->bantypes, value))
		return;
	if (tkl)
		tkl_del_line(tkl);
	if (value && *value)
		tkl_add_banexception(TKL_EXCEPTION, "*", key, NULL, "UDB Nolines Exemption", "UDB", 0, TStime(), 0, value, 0);
}

/* Visit exactly one nolines scalar per key, with no resident mask inventory. */
static UdbStorageResult udb_ip_nolines_scan(UdbStorageRead *source, UdbStorageRead *replacement, int remove_missing)
{
	UdbStorageBlockIterator *iterator = NULL;
	UdbStorageField *field = NULL;
	UdbStorageRecord record;
	UdbStorageResult result = udb_storage_read_iterator(source, UDB_BLOCK_IPS, NULL, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	if (remove_missing)
	{
		field = udb_storage_alloc(&source->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*field));
		if (!field)
		{
			result = UDB_STORAGE_LIMIT;
			goto finish;
		}
	}
	while ((result = udb_storage_block_iterator_next(iterator, &record)) == UDB_STORAGE_OK)
	{
		const UdbStorageRow *row = &record.row;
		if (row->depth != 2 || row->components[1].length != strlen(IKEY_NOLINES) ||
			memcmp(row->components[1].data, IKEY_NOLINES, row->components[1].length))
			continue;
		char key[HOSTLEN + 1], value[UDB_STORAGE_VALUE_MAX + 1];
		if (row->kind != UDB_STORAGE_VALUE_STRING || !udb_native_span_string(row->components[0], key, sizeof(key)) ||
			!udb_native_span_string(row->value, value, sizeof(value)))
		{
			result = UDB_STORAGE_CORRUPT;
			break;
		}
		if (remove_missing)
		{
			result = udb_storage_read_field(replacement, row, field);
			if (result < UDB_STORAGE_NOT_FOUND)
				break;
			if (result == UDB_STORAGE_NOT_FOUND)
				udb_ip_set_nolines(key, NULL);
		}
		else
			udb_ip_set_nolines(key, value);
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = UDB_STORAGE_OK;
finish:
	udb_storage_free(field);
	if (udb_storage_block_iterator_close(&iterator) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_ip_client_profile(UdbStorageRead *read, Client *client, UdbStorageProfile **out)
{
	static const char *const fields[] = {IKEY_CLONES, IKEY_HOST, IKEY_NOLINES};
	*out = NULL;
	if (!client || !client->ip || !client->user)
		return UDB_STORAGE_NOT_FOUND;
	UdbStorageRow entity = {0};
	entity.block = UDB_BLOCK_IPS;
	entity.depth = 1;
	entity.components[0].data = (const unsigned char *)client->ip;
	entity.components[0].length = (uint32_t)strlen(client->ip);
	UdbStorageResult result = udb_storage_profile_load(read, &entity, fields, 3, out);
	if (result == UDB_STORAGE_NOT_FOUND && client->user->realhost[0])
	{
		entity.components[0].data = (const unsigned char *)client->user->realhost;
		entity.components[0].length = (uint32_t)strlen(client->user->realhost);
		result = udb_storage_profile_load(read, &entity, fields, 3, out);
	}
	return result;
}

static UdbStorageResult udb_ip_reconcile_scope(UdbStorageRead *before, UdbStorageRead *after, const char *entity)
{
	UdbStorageResult result = UDB_STORAGE_OK;
	if (entity)
	{
		UdbStorageField *field = udb_storage_alloc(&after->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*field));
		if (!field)
			return UDB_STORAGE_LIMIT;
		result = udb_native_read_scalar(after, UDB_BLOCK_IPS, entity, IKEY_NOLINES, field);
		if (result == UDB_STORAGE_OK && field->kind != UDB_STORAGE_VALUE_STRING)
			result = UDB_STORAGE_CORRUPT;
		if (result >= UDB_STORAGE_NOT_FOUND)
		{
			udb_ip_set_nolines(entity, result == UDB_STORAGE_OK ? field->string : NULL);
			result = UDB_STORAGE_OK;
		}
		udb_storage_free(field);
	}
	else
		result = udb_ip_nolines_scan(before, after, 1);
	if (result == UDB_STORAGE_OK && !entity)
		result = udb_ip_nolines_scan(after, NULL, 0);
	if (result != UDB_STORAGE_OK)
		return result;
	Client *client, *next;
	list_for_each_entry_safe(client, next, &lclient_list, lclient_node)
	{
		if (!MyUser(client) || (entity && (!client->ip || strcasecmp(entity, client->ip)) &&
								(!client->user || strcasecmp(entity, client->user->realhost))))
			continue;
		UdbStorageProfile *old = NULL, *current = NULL;
		UdbStorageResult prior = udb_ip_client_profile(before, client, &old);
		UdbStorageResult loaded = udb_ip_client_profile(after, client, &current);
		if (prior < UDB_STORAGE_NOT_FOUND || loaded < UDB_STORAGE_NOT_FOUND)
		{
			result = prior < UDB_STORAGE_NOT_FOUND ? prior : loaded;
			udb_storage_profile_free(&old);
			udb_storage_profile_free(&current);
			continue;
		}
		const UdbStorageField *oldhost = old && old->fields[1].present ? old->fields + 1 : NULL;
		const UdbStorageField *host = current && current->fields[1].present ? current->fields + 1 : NULL;
		int changed = (oldhost != NULL) != (host != NULL) ||
					  (oldhost && host &&
					   (oldhost->kind != host->kind || strcmp(oldhost->string, host->string) ||
						strcmp(old->entity, current->entity)));
		if (changed)
		{
			char visible[HOSTLEN + 1];
			strlcpy(visible, udb_ip_visible_host(client), sizeof(visible));
			userhost_save_current(client);
			UdbIpHostState *owned = udb_ip_host_md ? moddata_local_client(client, udb_ip_host_md).ptr : NULL;
			if (owned)
			{
				char key[HOSTLEN + 1];
				strlcpy(key, owned->key, sizeof(key));
				udb_ip_restore_host(client, key);
			}
			if (host && host->kind == UDB_STORAGE_VALUE_STRING)
				udb_ip_apply_host(client, current->entity, host->string);
			else if (host)
				result = UDB_STORAGE_CORRUPT;
			if (strcmp(visible, udb_ip_visible_host(client)))
				udb_ip_notify_host_change(client, "*** Your IP host policy has changed");
		}
		udb_storage_profile_free(&old);
		udb_storage_profile_free(&current);
	}
	return result;
}

static UdbStorageResult udb_ip_reconcile_generations(UdbStorageRead *before, UdbStorageRead *after)
{
	return udb_ip_reconcile_scope(before, after, NULL);
}

static int udb_hook_pre_connect(Client *client)
{
	static const char *const fields[] = {IKEY_CLONES, IKEY_HOST, IKEY_NOLINES};
	UdbStorageRead *read = NULL;
	UdbStorageProfile *profile = NULL;
	UdbStorageField *globals = NULL;
	UdbStorageResult result;
	int limit = 0, exempt = 0;
	char host[HOSTLEN + 1] = "", ip_key[HOSTLEN + 1] = "";
	char quit[UDB_STORAGE_VALUE_MAX + 1] = "Too many connections from your IP";

	if (!client || !client->ip || !client->local)
		return HOOK_CONTINUE;
	if (!udb_ctx || !udb_ctx->storage || !udb_ready)
		goto unavailable;
	result = udb_storage_read_open(udb_ctx->storage, &read);
	if (result != UDB_STORAGE_OK)
		goto unavailable;
	UdbStorageRow entity = {0};
	entity.block = 'I';
	entity.depth = 1;
	entity.components[0].data = (const unsigned char *)client->ip;
	entity.components[0].length = (uint32_t)strlen(client->ip);
	result = udb_storage_profile_load(read, &entity, fields, 3, &profile);
	if (result == UDB_STORAGE_NOT_FOUND && client->user && client->user->realhost[0])
	{
		entity.components[0].data = (const unsigned char *)client->user->realhost;
		entity.components[0].length = (uint32_t)strlen(client->user->realhost);
		result = udb_storage_profile_load(read, &entity, fields, 3, &profile);
	}
	if (result < UDB_STORAGE_NOT_FOUND)
		goto unavailable;
	if (profile)
	{
		UdbStorageField *clones = profile->fields, *vhost = profile->fields + 1, *nolines = profile->fields + 2;
		if ((clones->present && (clones->kind != UDB_STORAGE_VALUE_NUMBER || clones->number > INT_MAX)) ||
			(vhost->present && (vhost->kind != UDB_STORAGE_VALUE_STRING || vhost->length > HOSTLEN)) ||
			(nolines->present && nolines->kind != UDB_STORAGE_VALUE_STRING) || entity.components[0].length > HOSTLEN)
			goto unavailable;
		limit = clones->present ? (int)clones->number : 0;
		if (vhost->present)
			memcpy(host, vhost->string, vhost->length + 1);
		memcpy(ip_key, entity.components[0].data, entity.components[0].length);
		ip_key[entity.components[0].length] = 0;
		exempt = nolines->present && (strchr(nolines->string, 'c') || strchr(nolines->string, 'C'));
	}
	if (!exempt)
	{
		globals = udb_storage_alloc(&read->engine->budget, UDB_STORAGE_MEMORY_AUX, 2 * sizeof(*globals));
		if (!globals)
			goto unavailable;
		if (!limit)
		{
			result = udb_native_read_scalar(read, 'S', NULL, SKEY_CLONES, globals);
			if (result < UDB_STORAGE_NOT_FOUND ||
				(result == UDB_STORAGE_OK &&
				 (globals[0].kind != UDB_STORAGE_VALUE_NUMBER || globals[0].number > INT_MAX)))
				goto unavailable;
			limit = result == UDB_STORAGE_OK ? (int)globals[0].number : 0;
		}
		if (limit)
		{
			result = udb_native_read_scalar(read, 'S', NULL, SKEY_QUIT_CLONES, globals + 1);
			if (result < UDB_STORAGE_NOT_FOUND ||
				(result == UDB_STORAGE_OK && globals[1].kind != UDB_STORAGE_VALUE_STRING))
				goto unavailable;
			if (result == UDB_STORAGE_OK)
				memcpy(quit, globals[1].string, globals[1].length + 1);
		}
	}
	udb_storage_profile_free(&profile);
	udb_storage_free(globals);
	globals = NULL;
	if (udb_storage_read_close(&read) != UDB_STORAGE_OK)
		goto unavailable;
	/* Privacy is mandatory even for a connect-flood-exempt entity. */
	if (*host && client->user)
		udb_ip_apply_host(client, ip_key, host);
	udb_ip_refresh_derived_host(client);
	if (!udb_ip_base_state(client, 0) || !udb_ip_safe_host(client, udb_ip_base_state(client, 0)->host))
	{
		exit_client(client, NULL, "No safe cloak is available");
		return HOOK_DENY;
	}
	if (exempt || !limit)
		return HOOK_CONTINUE;
	int clone_count = 0;
	Client *other;
	list_for_each_entry(other, &client_list, client_node)
	{
		if (IsUser(other) && !IsDead(other) && other != client && other->ip && !strcmp(other->ip, client->ip))
		{
			/* Stop at the validated INT_MAX-bounded threshold. */
			if (++clone_count >= limit)
				break;
		}
	}
	if (clone_count < limit)
		return HOOK_CONTINUE;
	udb_log(ULOG_INFO, "UDB_CLONES", client, "Rejecting $client.ip (Exceeds UDB clone limit of $limit)",
			log_data_integer("limit", limit));
	exit_client(client, NULL, quit);
	return HOOK_DENY;
unavailable:
	udb_storage_profile_free(&profile);
	udb_storage_free(globals);
	if (read)
		udb_storage_read_close(&read);
	exit_client(client, NULL, "UDB policy data unavailable");
	return HOOK_DENY;
}

static int udb_hook_local_quit(Client *client, MessageTag *mtags, const char *comment)
{
	(void)client;
	(void)mtags;
	(void)comment;
	/* Client ModData callbacks free state after the QUIT has been serialized. */
	return HOOK_CONTINUE;
}

static void udb_ips_shutdown(void)
{
	TKL *tkl, *next;
	for (unsigned int i = 0; i < TKLISTLEN; i++)
		for (tkl = tklines[i]; tkl; tkl = next)
		{
			next = tkl->next;
			if (udb_ip_tkl_is_owned(tkl))
				tkl_del_line(tkl);
		}
	for (unsigned int i = 0; i < TKLIPHASHLEN1; i++)
		for (unsigned int j = 0; j < TKLIPHASHLEN2; j++)
			for (tkl = tklines_ip_hash[i][j]; tkl; tkl = next)
			{
				next = tkl->next;
				if (udb_ip_tkl_is_owned(tkl))
					tkl_del_line(tkl);
			}
	Client *client, *following;
	list_for_each_entry_safe(client, following, &lclient_list, lclient_node)
	{
		UdbIpHostState *owned = udb_ip_host_md ? moddata_local_client(client, udb_ip_host_md).ptr : NULL;
		if (!owned || !client->user)
			continue;
		char key[HOSTLEN + 1], visible[HOSTLEN + 1];
		strlcpy(key, owned->key, sizeof(key));
		strlcpy(visible, udb_ip_visible_host(client), sizeof(visible));
		if (MyUser(client))
			userhost_save_current(client);
		udb_ip_restore_host(client, key);
		if (strcmp(visible, udb_ip_visible_host(client)))
			udb_ip_notify_host_change(client, NULL);
	}
}

static int udb_ip_welcome(Client *client, int after_numeric)
{
	if (!after_numeric)
		udb_ip_install_base(client);
	return 0;
}

static int udb_ip_remote_connect(Client *client)
{
	/* EARLY metadata has been extracted before this hook. */
	udb_ip_install_base(client);
	return 0;
}

static int udb_ip_restore_requested(const char *modes)
{
	int add = 1, restore = 0;
	const char *p;
	if (BadPtr(modes))
		return 0;
	for (p = modes; *p; p++)
	{
		if (*p == '+')
			add = 1;
		else if (*p == '-')
			add = 0;
		else if (*p == 't')
			restore = add;
	}
	return restore;
}

CMD_OVERRIDE_FUNC(udb_override_safe_sethost)
{
	if (IsUser(client) && parc > 1 && !udb_ip_safe_host(client, parv[1]))
		return;
	CALL_NEXT_COMMAND_OVERRIDE();
}

CMD_OVERRIDE_FUNC(udb_override_safe_chghost)
{
	Client *target = parc > 2 ? find_user(parv[1], NULL) : NULL;
	if (target && !udb_ip_safe_host(target, parv[2]))
		return;
	CALL_NEXT_COMMAND_OVERRIDE();
}

static void udb_ip_restore_nick_vhost(Client *client)
{
	UdbStorageProfile *nick = NULL;
	if (!client || !MyUser(client) || IsDead(client))
		return;
	UdbStorageResult result = udb_native_nick_load(client->name, &nick);
	if (result < UDB_STORAGE_NOT_FOUND)
	{
		sendnotice(client, "UDB: nickname policy data unavailable");
		return;
	}
	UdbStorageField *vhost = udb_nick_field(nick, NKEY_VHOST);
	if (!has_user_mode(client, 'r') || !nick || !udb_nick_identity_valid(client, nick) ||
		udb_nick_field(nick, NKEY_SUSPEND) || udb_nick_field(nick, NKEY_FORBID) || !vhost ||
		!udb_ip_safe_host(client, vhost->string))
	{
		sendnotice(client, "UDB: +t requires an authenticated nickname with an authorized N vhost");
		goto cleanup;
	}
	if (IsSetHost(client) && client->user->virthost && !strcmp(client->user->virthost, vhost->string))
		goto cleanup;
	if (UHOST_ALLOWED == UHALLOW_NEVER || (UHOST_ALLOWED == UHALLOW_NOCHANS && client->user->joined) ||
		(!ValidatePermissionsForPath("immune:vhost-flood", client, NULL, NULL, NULL) &&
		 flood_limit_exceeded(client, FLD_VHOST)))
	{
		sendnotice(client, "UDB: vhost change is currently restricted");
		goto cleanup;
	}
	udb_nick_effects_apply_vhost(client, vhost);
cleanup:
	udb_storage_profile_free(&nick);
}

CMD_OVERRIDE_FUNC(udb_override_host_mode)
{
	Client *target = parc > 2 ? find_user(parv[1], NULL) : NULL;
	int was_hidden = IsHidden(client);
	int restore = udb_ip_restore_requested(parc > 2 ? parv[2] : NULL);
	CALL_NEXT_COMMAND_OVERRIDE();
	/* Native +x emits SETHOST, which adds +t on remote receivers. */
	if (target == client && MyUser(client) && !was_hidden && IsHidden(client) && !IsSetHost(client))
		sendto_server(NULL, 0, 0, NULL, ":%s UMODE2 -t", client->id);
	if (target == client && restore && MyUser(client))
		udb_ip_restore_nick_vhost(client);
}

CMD_OVERRIDE_FUNC(udb_override_host_umode2)
{
	int was_hidden = IsHidden(client);
	int restore = udb_ip_restore_requested(parc > 1 ? parv[1] : NULL);
	CALL_NEXT_COMMAND_OVERRIDE();
	if (MyUser(client) && !was_hidden && IsHidden(client) && !IsSetHost(client))
		sendto_server(NULL, 0, 0, NULL, ":%s UMODE2 -t", client->id);
	if (restore && MyUser(client))
		udb_ip_restore_nick_vhost(client);
}

CMD_OVERRIDE_FUNC(udb_override_base_md)
{
	if (parc > 3 && !strcmp(parv[1], "client") && !strcmp(parv[3], "udb_base_cloak"))
	{
		Client *target = find_user(parv[2], NULL);
		/* Native MD blocks remote writes to local clients, but does not enforce
		 * the origin of writes to remote clients. Protect that boundary here. */
		if (!target || target->uplink != client || parc < 5 || !udb_ip_safe_host(target, parv[4]) ||
			!valid_host(parv[4], 0))
			return;
	}
	CALL_NEXT_COMMAND_OVERRIDE();
}

static int udb_ips_load(ModuleInfo *modinfo)
{
	Client *client;
	const char *commands[] = {"MODE", "UMODE2", "MD", "SETHOST", "CHGHOST"};
	OverrideCmdFunc handlers[] = {udb_override_host_mode, udb_override_host_umode2, udb_override_base_md,
								  udb_override_safe_sethost, udb_override_safe_chghost};

	size_t i;
	for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
	{
		if (!CommandExists(commands[i]))
		{
			if (i == 0 || i == 2)
				return 0;
			continue;
		}
		if (!CommandOverrideAdd(modinfo->handle, commands[i], -100, handlers[i]))
			return 0;
	}
	udb_ip_refresh_derived_hosts();
	list_for_each_entry(client, &client_list, client_node)
	{
		if (IsUser(client) && !MyUser(client))
			udb_ip_install_base(client);
	}
	return 1;
}

static int udb_ips_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_ip_host_state";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_ip_host_state_free;
	udb_ip_host_md = ModDataAdd(modinfo->handle, mreq);
	if (!udb_ip_host_md)
		return 0;
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_base_cloak";
	mreq.type = MODDATATYPE_CLIENT;
	mreq.sync = MODDATA_SYNC_EARLY;
	mreq.free = udb_ip_host_state_free;
	mreq.serialize = udb_ip_base_serialize;
	mreq.unserialize = udb_ip_base_unserialize;
	udb_base_cloak_md = ModDataAdd(modinfo->handle, mreq);
	if (!udb_base_cloak_md)
		return 0;
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, udb_hook_pre_connect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, udb_hook_local_quit);
	HookAdd(modinfo->handle, HOOKTYPE_WELCOME, -100, udb_ip_welcome);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_CONNECT, -100, udb_ip_remote_connect);
	return 1;
}

/* End of udb_ips.c.inc */

/* Distributed *lines: glines, zlines, shuns, qlines, spamfilters */
/* Inlined: udb_lines.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Distributed G/Z/S/Q Lines, Spamfilters & Expiry
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static UdbLineExpiryPending *udb_line_expiry_pending = NULL;

static void udb_line_split_mask(const char *mask, char *user, size_t usersz, char *host, size_t hostsz)
{
	const char *at = strchr(mask, '@');

	if (at)
	{
		size_t userlen = at - mask;
		if (userlen >= usersz)
			userlen = usersz - 1;
		memcpy(user, mask, userlen);
		user[userlen] = '\0';
		strlcpy(host, at + 1, hostsz);
	}
	else
	{
		strlcpy(user, "*", usersz);
		strlcpy(host, mask, hostsz);
	}
}

static int udb_spamfilter_pattern(const char *stored, char *pattern, size_t patternsz)
{
	const char *encoded;
	size_t encoded_len, padding = 0, decoded_len, i;
	char canonical[UDB_SPAMFILTER_PATTERN_MAX * 4 / 3 + 5];
	int n;

	/* A raw regex is not a second spelling of a b64 pattern. */
	if (strncmp(stored, UDB_SPAMFILTER_B64_PREFIX, strlen(UDB_SPAMFILTER_B64_PREFIX)))
		return 0;

	encoded = stored + strlen(UDB_SPAMFILTER_B64_PREFIX);
	encoded_len = strlen(encoded);
	if (!encoded_len || encoded_len % 4 || encoded_len > ((patternsz - 1 + 2) / 3) * 4)
		return 0;
	if (encoded[encoded_len - 1] == '=')
		padding++;
	if (encoded_len > 1 && encoded[encoded_len - 2] == '=')
		padding++;
	if (padding > 2)
		return 0;
	for (i = 0; i < encoded_len - padding; i++)
	{
		if (!isalnum((unsigned char)encoded[i]) && encoded[i] != '+' && encoded[i] != '/')
			return 0;
	}
	for (; i < encoded_len; i++)
	{
		if (encoded[i] != '=')
			return 0;
	}
	decoded_len = (encoded_len / 4) * 3 - padding;
	if (!decoded_len || decoded_len > UDB_SPAMFILTER_PATTERN_MAX || decoded_len >= patternsz)
		return 0;
	n = b64_decode(encoded, (unsigned char *)pattern, patternsz);
	if (n < 0 || (size_t)n != decoded_len || memchr(pattern, '\0', decoded_len))
		return 0;
	pattern[decoded_len] = '\0';
	if (b64_encode((unsigned char *)pattern, decoded_len, canonical, sizeof(canonical)) != (int)encoded_len ||
		strcmp(canonical, encoded))
		return 0;
	return 1;
}

static int udb_line_matches_tkl(TKL *tkl, char type, const char *pattern)
{
	char user[128], host[128];

	if (!tkl || !tkl->set_by || strcmp(tkl->set_by, UDB_TKL_SET_BY))
		return 0;
	if (type == 'F')
		return TKLIsSpamfilter(tkl) && (tkl->type & TKL_GLOBAL) && tkl->ptr.spamfilter && tkl->ptr.spamfilter->match &&
			   !strcmp(tkl->ptr.spamfilter->match->str, pattern);
	if (type == 'Q')
		return TKLIsNameBan(tkl) && (tkl->type & TKL_GLOBAL) && tkl->ptr.nameban &&
			   !strcasecmp(tkl->ptr.nameban->name, pattern);

	udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
	return TKLIsServerBan(tkl) && (tkl->type & TKL_GLOBAL) &&
		   ((type == 'G' && (tkl->type & TKL_KILL)) || (type == 'Z' && (tkl->type & TKL_ZAP)) ||
			(type == 'S' && (tkl->type & TKL_SHUN))) &&
		   tkl->ptr.serverban && !strcasecmp(tkl->ptr.serverban->usermask, user) &&
		   !strcasecmp(tkl->ptr.serverban->hostmask, host);
}

static void udb_line_remove_owned_list(TKL *head, char type, const char *pattern)
{
	for (TKL *line = head, *next; line; line = next)
	{
		next = line->next;
		if (udb_line_matches_tkl(line, type, pattern))
		{
			if (type == 'S')
				tkl_check_local_remove_shun(line);
			tkl_del_line(line);
		}
	}
}

static void udb_line_remove_owned(char type, const char *pattern)
{
	if (!strchr("GZSQF", type) || !pattern)
		return;
	int bucket = tkl_hash((unsigned char)type);
	if (bucket >= 0 && bucket < TKLISTLEN)
		udb_line_remove_owned_list(tklines[bucket], type, pattern);
	int ip_type = tkl_ip_hash_type(type);
	if (ip_type < 0 || ip_type >= TKLIPHASHLEN1)
		return;
	char user[128], host[128];
	udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
	int ip_bucket = tkl_ip_hash(host);
	if (ip_bucket >= 0 && ip_bucket < TKLIPHASHLEN2)
		udb_line_remove_owned_list(tklines_ip_hash[ip_type][ip_bucket], type, pattern);
}

static const char *const udb_native_line_fields[] = {KKEY_ACTION,	  KKEY_BAN_TIME, KKEY_EXPIRES,
													 KKEY_MATCH_TYPE, KKEY_REASON,	 KKEY_TARGETS};
static UdbStorageField *udb_line_field(UdbStorageProfile *profile, const char *name)
{
	if (!profile || profile->key.block != UDB_BLOCK_LINES || profile->key.depth != 2 || profile->count != 6)
		return NULL;
	for (unsigned int i = 0; i < 6; i++)
		if (!strcmp(name, udb_native_line_fields[i]))
			return profile->fields[i].present ? profile->fields + i : NULL;
	return NULL;
}
static int udb_line_expiry(UdbStorageProfile *profile, time_t *expires)
{
	UdbStorageField *field = udb_line_field(profile, KKEY_EXPIRES);
	if (!field)
		return 0;
	if (field->kind != UDB_STORAGE_VALUE_NUMBER || !field->number || field->number > udb_time_t_max_val())
		return -1;
	if (expires)
		*expires = (time_t)field->number;
	return 1;
}
static int udb_line_path(UdbStorageProfile *profile, char *path, size_t capacity)
{
	return profile && udb_native_key_path(&profile->key, path, capacity) == UDB_STORAGE_OK;
}
static char udb_line_type(UdbStorageProfile *profile)
{
	if (!profile || profile->key.depth != 2 || profile->key.components[0].length != 1)
		return 0;
	unsigned char type = profile->key.components[0].data[0];
	return (char)(type >= 'a' && type <= 'z' ? type - ('a' - 'A') : type);
}
static int udb_line_stored_pattern(UdbStorageProfile *profile, char *out, size_t capacity)
{
	return profile && profile->key.depth == 2 && udb_native_span_string(profile->key.components[1], out, capacity);
}
static int udb_line_runtime_pattern(UdbStorageProfile *profile, char *out, size_t capacity)
{
	char stored[UDB_STORAGE_COMPONENT_MAX + 1];
	if (!udb_line_stored_pattern(profile, stored, sizeof(stored)))
		return 0;
	if (udb_line_type(profile) == 'F')
		return udb_spamfilter_pattern(stored, out, capacity);
	if (strlen(stored) >= capacity)
		return 0;
	memcpy(out, stored, strlen(stored) + 1);
	return 1;
}

static UdbLineExpiryPending *udb_line_expiry_pending_find(char type, const char *pattern, time_t expires)
{
	UdbLineExpiryPending *pending;

	for (pending = udb_line_expiry_pending; pending; pending = pending->next)
		if (pending->type == type && pending->expires == expires && !strcmp(pending->pattern, pattern))
			return pending;
	return NULL;
}

static UdbLineExpiryPending *udb_line_expiry_pending_add(char type, const char *pattern, time_t expires)
{
	UdbLineExpiryPending *pending = udb_line_expiry_pending_find(type, pattern, expires);

	if (pending)
		return pending;
	if (!udb_ctx || !udb_ctx->storage)
		return NULL;
	unsigned int count = 0;
	for (UdbLineExpiryPending *entry = udb_line_expiry_pending; entry; entry = entry->next)
		count++;
	if (count >= UDB_LINE_EXPIRY_SWEEP_MAX || strlen(pattern) > UDB_STORAGE_COMPONENT_MAX)
		return NULL;
	pending = udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*pending));
	if (!pending)
		return NULL;
	pending->pattern = udb_storage_alloc(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, strlen(pattern) + 1);
	if (!pending->pattern)
	{
		udb_storage_free(pending);
		return NULL;
	}
	pending->type = type;
	pending->expires = expires;
	memcpy(pending->pattern, pattern, strlen(pattern) + 1);
	pending->next = udb_line_expiry_pending;
	udb_line_expiry_pending = pending;
	return pending;
}

static void udb_line_expiry_pending_clear(char type, const char *pattern)
{
	UdbLineExpiryPending **link;

	for (link = &udb_line_expiry_pending; *link;)
	{
		UdbLineExpiryPending *pending = *link;
		if (pending->type == type && !strcmp(pending->pattern, pattern))
		{
			*link = pending->next;
			udb_storage_free(pending->pattern);
			udb_storage_free(pending);
			continue;
		}
		link = &pending->next;
	}
}

static UdbStorageResult udb_line_apply_profile(UdbStorageProfile *line_rec)
{
	char type;
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	UdbStorageField *reason_rec;
	time_t expires = 0;
	int expiry_state;
	const char *reason;

	type = udb_line_type(line_rec);
	if (!type || !strchr("GZSQF", type) || !udb_line_runtime_pattern(line_rec, pattern, sizeof(pattern)))
		return UDB_STORAGE_CORRUPT;
	/* Complete native profile validation preceded durable publication. */
	expiry_state = udb_line_expiry(line_rec, &expires);
	if (expiry_state < 0)
	{
		udb_log(ULOG_ERROR, "UDB_K_EXPIRES_INVALID", NULL, "Ignoring invalid K expires record for $pattern",
				log_data_string("pattern", pattern), NULL);
		return UDB_STORAGE_CORRUPT;
	}
	if (expiry_state > 0 && expires <= TStime())
	{
		udb_line_remove_owned(type, pattern);
		return UDB_STORAGE_OK;
	}
	reason_rec = udb_line_field(line_rec, KKEY_REASON);
	reason = reason_rec && reason_rec->kind == UDB_STORAGE_VALUE_STRING ? reason_rec->string : NULL;
	if (!reason)
	{
		/* A valid partial profile is deliberately inert. */
		udb_line_remove_owned(type, pattern);
		return UDB_STORAGE_OK;
	}

	if (type == 'F')
	{
		UdbStorageField *match_type_rec = udb_line_field(line_rec, KKEY_MATCH_TYPE);
		UdbStorageField *targets_rec = udb_line_field(line_rec, KKEY_TARGETS);
		UdbStorageField *action_rec = udb_line_field(line_rec, KKEY_ACTION);
		UdbStorageField *ban_time_rec = udb_line_field(line_rec, KKEY_BAN_TIME);
		const char *err = NULL;
		int target, match_type;
		BanActionValue action_value;
		BanAction *action;
		Match *match;
		time_t ban_time = 0;

		if (!match_type_rec || !targets_rec || !action_rec || match_type_rec->kind != UDB_STORAGE_VALUE_STRING ||
			targets_rec->kind != UDB_STORAGE_VALUE_STRING || action_rec->kind != UDB_STORAGE_VALUE_STRING)
		{
			udb_line_remove_owned(type, pattern);
			return UDB_STORAGE_OK;
		}
		match_type = udb_spamfilter_match_type_value(match_type_rec->string);
		target = spamfilter_gettargets(targets_rec->string, NULL);
		action_value = banact_stringtoval(action_rec->string);
		action = action_value && !banact_config_only(action_value) ? banact_value_to_struct(action_value) : NULL;
		if (ban_time_rec)
			ban_time = (time_t)ban_time_rec->number;
		/* Compile before removing the old effect. */
		match = target > 0 && action ? unreal_create_match(match_type, pattern, &err) : NULL;
		if (!match)
		{
			udb_log(ULOG_ERROR, "UDB_SPAMF_ERROR", NULL, "Failed to prepare spamfilter candidate: $pattern ($err)",
					log_data_string("pattern", pattern), log_data_string("err", err ? err : "unknown error"), NULL);
			safe_free_all_ban_actions(action);
			return UDB_STORAGE_ERROR;
		}
		udb_line_remove_owned(type, pattern);
		if (!tkl_add_spamfilter(TKL_SPAMF | TKL_GLOBAL, pattern, (unsigned short)target, action, match, pattern, NULL,
								UDB_TKL_SET_BY, expires, TStime(), ban_time, reason, INPUT_CONVERSION_DEFAULT, 0, 0))
		{
			unreal_delete_match(match);
			safe_free_all_ban_actions(action);
			udb_log(ULOG_ERROR, "UDB_SPAMF_ADD_FAILED", NULL, "Failed to materialize spamfilter candidate: $pattern",
					log_data_string("pattern", pattern), NULL);
			return UDB_STORAGE_ERROR;
		}
		return UDB_STORAGE_OK;
	}

	udb_line_remove_owned(type, pattern);
	TKL *added = NULL;
	if (type == 'G')
	{
		char user[128], host[128];
		udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
		added =
			tkl_add_serverban(TKL_KILL | TKL_GLOBAL, user, host, NULL, reason, UDB_TKL_SET_BY, expires, TStime(), 0, 0);
	}
	else if (type == 'Z')
		added = tkl_add_serverban(TKL_ZAP | TKL_GLOBAL, "*", pattern, NULL, reason, UDB_TKL_SET_BY, expires, TStime(),
								  0, 0);
	else if (type == 'S')
	{
		char user[128], host[128];
		udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
		added =
			tkl_add_serverban(TKL_SHUN | TKL_GLOBAL, user, host, NULL, reason, UDB_TKL_SET_BY, expires, TStime(), 0, 0);
	}
	else if (type == 'Q')
		added = tkl_add_nameban(TKL_NAME | TKL_GLOBAL, pattern, 0, reason, UDB_TKL_SET_BY, expires, TStime(), 0);
	return added ? UDB_STORAGE_OK : UDB_STORAGE_ERROR;
}

static void udb_line_remove_profile(UdbStorageProfile *profile)
{
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	char type = udb_line_type(profile);
	if (type && strchr("GZSQF", type) && udb_line_runtime_pattern(profile, pattern, sizeof(pattern)))
		udb_line_remove_owned(type, pattern);
}

static int udb_line_profiles_equal(UdbStorageProfile *a, UdbStorageProfile *b)
{
	if (!a || !b)
		return a == b;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbStorageField *left = a->fields + i, *right = b->fields + i;
		if (left->present != right->present ||
			(left->present &&
			 (left->kind != right->kind || left->number != right->number || left->length != right->length ||
			  (left->kind == UDB_STORAGE_VALUE_STRING && memcmp(left->string, right->string, left->length)))))
			return 0;
	}
	return 1;
}

/* One representative scalar per complete owner. Radix leaves need not keep
 * all of an entity's keys adjacent, so no stream adjacency assumption is used. */
static UdbStorageResult udb_line_profile_scan(UdbStorageRead *source, UdbStorageRead *other, int old_pass,
											  const UdbStorageRow *prefix)
{
	UdbStorageBlockIterator *iterator = NULL;
	UdbStorageRecord record;
	UdbStorageResult result = udb_storage_read_iterator(source, UDB_BLOCK_LINES, prefix, &iterator);
	if (result != UDB_STORAGE_OK)
		return result;
	while ((result = udb_storage_block_iterator_next(iterator, &record)) == UDB_STORAGE_OK)
	{
		if (record.row.depth < 2)
			continue;
		UdbStorageRow key = record.row;
		key.depth = 2;
		key.kind = UDB_STORAGE_VALUE_NONE;
		key.number = 0;
		key.value.data = NULL;
		key.value.length = 0;
		UdbStorageProfile *profile = NULL, *replacement = NULL;
		result = udb_storage_profile_load(source, &key, udb_native_line_fields, 6, &profile);
		if (result != UDB_STORAGE_OK)
			break;
		unsigned int representative = 0;
		while (representative < 6 && !profile->fields[representative].present)
			representative++;
		int chosen = representative == 6
						 ? record.row.depth == 2
						 : record.row.depth == 3 &&
							   record.row.components[2].length == strlen(udb_native_line_fields[representative]) &&
							   !memcmp(record.row.components[2].data, udb_native_line_fields[representative],
									   record.row.components[2].length);
		if (chosen)
		{
			result = other ? udb_storage_profile_load(other, &key, udb_native_line_fields, 6, &replacement)
						   : UDB_STORAGE_NOT_FOUND;
			if (result < UDB_STORAGE_NOT_FOUND)
			{
				udb_storage_profile_free(&profile);
				break;
			}
			if (old_pass && !udb_line_profiles_equal(profile, replacement))
			{
				if (replacement)
					result = udb_line_apply_profile(replacement);
				else
					udb_line_remove_profile(profile);
			}
			else if (!old_pass && !replacement)
				result = udb_line_apply_profile(profile);
		}
		udb_storage_profile_free(&profile);
		udb_storage_profile_free(&replacement);
		if (result < UDB_STORAGE_NOT_FOUND)
			break;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		result = UDB_STORAGE_OK;
	if (udb_storage_block_iterator_close(&iterator) != UDB_STORAGE_OK)
		result = UDB_STORAGE_ERROR;
	return result;
}

static UdbStorageResult udb_lines_reconcile_scope(UdbStorageRead *before, UdbStorageRead *after,
												  const UdbStorageRow *changed)
{
	UdbStorageRow key = *changed;
	key.kind = UDB_STORAGE_VALUE_NONE;
	key.number = 0;
	key.value.data = NULL;
	key.value.length = 0;
	if (key.depth < 2)
	{
		UdbStorageResult result = udb_line_profile_scan(before, after, 1, &key);
		return result == UDB_STORAGE_OK ? udb_line_profile_scan(after, before, 0, &key) : result;
	}
	key.depth = 2;
	UdbStorageProfile *old = NULL, *current = NULL;
	UdbStorageResult prior = udb_storage_profile_load(before, &key, udb_native_line_fields, 6, &old);
	UdbStorageResult result = udb_storage_profile_load(after, &key, udb_native_line_fields, 6, &current);
	if (prior < UDB_STORAGE_NOT_FOUND || result < UDB_STORAGE_NOT_FOUND)
		result = prior < UDB_STORAGE_NOT_FOUND ? prior : result;
	else
	{
		result = UDB_STORAGE_OK;
		if (!udb_line_profiles_equal(old, current))
		{
			if (current)
				result = udb_line_apply_profile(current);
			else if (old)
				udb_line_remove_profile(old);
			char stored[UDB_STORAGE_COMPONENT_MAX + 1];
			if (udb_native_span_string(key.components[1], stored, sizeof(stored)))
				udb_line_expiry_pending_clear((char)(key.components[0].data[0] - 'a' + 'A'), stored);
		}
	}
	udb_storage_profile_free(&old);
	udb_storage_profile_free(&current);
	return result;
}

static UdbStorageResult udb_lines_reconcile_generations(UdbStorageRead *before, UdbStorageRead *after)
{
	UdbStorageResult result = udb_line_profile_scan(before, after, 1, NULL);
	if (result == UDB_STORAGE_OK)
		result = udb_line_profile_scan(after, before, 0, NULL);
	return result;
}

static UdbStorageRead *udb_line_expiry_read;
static UdbStorageBlockIterator *udb_line_expiry_iterator;
static void udb_line_expiry_cursor_close(void)
{
	if (udb_line_expiry_iterator)
		udb_storage_block_iterator_close(&udb_line_expiry_iterator);
	if (udb_line_expiry_read)
		udb_storage_read_close(&udb_line_expiry_read);
}

static void udb_lines_expiry_sweep(time_t now)
{
	if (!udb_ctx || !udb_ctx->storage || !udb_ready || udb_reconcile.active)
	{
		udb_line_expiry_cursor_close();
		return;
	}
	if (!udb_line_expiry_read && udb_storage_read_open(udb_ctx->storage, &udb_line_expiry_read) != UDB_STORAGE_OK)
		return;
	if (!udb_line_expiry_iterator && udb_storage_read_iterator(udb_line_expiry_read, UDB_BLOCK_LINES, NULL,
															   &udb_line_expiry_iterator) != UDB_STORAGE_OK)
	{
		udb_line_expiry_cursor_close();
		return;
	}
	unsigned int expired = 0;
	for (unsigned int visited = 0; visited < 8192 && expired < UDB_LINE_EXPIRY_SWEEP_MAX; visited++)
	{
		UdbStorageRecord record;
		UdbStorageResult result = udb_storage_block_iterator_next(udb_line_expiry_iterator, &record);
		if (result != UDB_STORAGE_OK)
		{
			udb_line_expiry_cursor_close();
			break;
		}
		UdbStorageRow key = record.row;
		if (key.depth != 3 || key.components[2].length != strlen(KKEY_EXPIRES) ||
			memcmp(key.components[2].data, KKEY_EXPIRES, key.components[2].length) ||
			key.kind != UDB_STORAGE_VALUE_NUMBER || !key.number || key.number > (uint64_t)now)
			continue;
		time_t expiry = (time_t)key.number;
		key.depth = 2;
		key.kind = UDB_STORAGE_VALUE_NONE;
		key.number = 0;
		UdbStorageProfile *profile = NULL;
		if (udb_storage_profile_load(udb_line_expiry_read, &key, udb_native_line_fields, 6, &profile) != UDB_STORAGE_OK)
		{
			udb_line_expiry_cursor_close();
			break;
		}
		char path[UDB_RECORD_PATH_MAX + 1], pattern[UDB_STORAGE_COMPONENT_MAX + 1];
		UdbPropagatorSelection selected;
		if (udb_line_path(profile, path, sizeof(path)) && udb_line_stored_pattern(profile, pattern, sizeof(pattern)))
		{
			UdbStorageRead *current = NULL;
			UdbStorageProfile *live = NULL;
			UdbStorageResult checked = udb_storage_read_open(udb_ctx->storage, &current);
			if (checked == UDB_STORAGE_OK)
				checked = udb_storage_profile_load(current, &key, udb_native_line_fields, 6, &live);
			time_t live_expiry = 0;
			int matching =
				checked == UDB_STORAGE_OK && udb_line_expiry(live, &live_expiry) == 1 && live_expiry == expiry;
			udb_storage_profile_free(&live);
			if (current && udb_storage_read_close(&current) != UDB_STORAGE_OK)
				matching = 0;
			if (!matching)
			{
				udb_storage_profile_free(&profile);
				continue;
			}
			udb_line_remove_profile(profile);
			if (!udb_propagator_policy_present(udb_ctx) ||
				(udb_select_propagator(udb_ctx, 1, &selected) && selected.is_local))
				udb_mutation_expire_local(udb_ctx, path, expiry);
			else
			{
				UdbLineExpiryPending *pending = udb_line_expiry_pending_add(udb_line_type(profile), pattern, expiry);
				if (pending && !pending->sent && udb_select_propagator(udb_ctx, 1, &selected) && selected.peer &&
					udb_send_db_to_one(selected.peer, ":%s DB %s EXP %s %" PRIu64, me.id, selected.peer->id, path,
									   (uint64_t)expiry))
					pending->sent = 1;
			}
			expired++;
		}
		udb_storage_profile_free(&profile);
	}
}

static void udb_lines_expiry_peer_disconnected(Client *client)
{
	UdbLineExpiryPending *pending;
	(void)client;
	/* A reconnect is an explicit controlled retry boundary. */
	for (pending = udb_line_expiry_pending; pending; pending = pending->next)
		pending->sent = 0;
}

static void udb_lines_shutdown(void)
{
	/* Revoke only this module's global lines, not externally replaced TKLs. */
	for (unsigned int i = 0; i < TKLISTLEN; i++)
		for (TKL *line = tklines[i], *next; line; line = next)
		{
			next = line->next;
			if (line->set_by && !strcmp(line->set_by, UDB_TKL_SET_BY) && (line->type & TKL_GLOBAL) &&
				(TKLIsServerBan(line) || TKLIsNameBan(line) || TKLIsSpamfilter(line)))
			{
				if (line->type & TKL_SHUN)
					tkl_check_local_remove_shun(line);
				tkl_del_line(line);
			}
		}
	for (unsigned int i = 0; i < TKLIPHASHLEN1; i++)
		for (unsigned int j = 0; j < TKLIPHASHLEN2; j++)
			for (TKL *line = tklines_ip_hash[i][j], *next; line; line = next)
			{
				next = line->next;
				if (line->set_by && !strcmp(line->set_by, UDB_TKL_SET_BY) && (line->type & TKL_GLOBAL) &&
					TKLIsServerBan(line))
					tkl_del_line(line);
			}
	udb_line_expiry_cursor_close();
	while (udb_line_expiry_pending)
	{
		UdbLineExpiryPending *next = udb_line_expiry_pending->next;
		udb_storage_free(udb_line_expiry_pending->pattern);
		udb_storage_free(udb_line_expiry_pending);
		udb_line_expiry_pending = next;
	}
}

static void udb_lines_init(ModuleInfo *modinfo)
{
	(void)modinfo;
	/* The shared one-second sync event invokes the lightweight K expiry sweep. */
}

/* End of udb_lines.c.inc */

/* DBQ query command for users and opers */
/* Inlined: udb_query.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: DBQ Query Command & Secret Redaction
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

/* Secret classification uses the canonical block/depth/property tuple, never
 * display text or a record's parent pointers. */
static int udb_query_is_secret(const UdbStorageRow *row)
{
	if (!row || !row->depth)
		return 0;
	UdbStorageSpan part = row->components[row->depth - 1];
	return (row->block == 'N' && row->depth == 2 && part.length == sizeof(NKEY_PASS) - 1 &&
			!memcmp(part.data, NKEY_PASS, sizeof(NKEY_PASS) - 1)) ||
		   (row->block == 'S' && row->depth == 1 && part.length == sizeof(SKEY_CRYPT_KEY) - 1 &&
			!memcmp(part.data, SKEY_CRYPT_KEY, sizeof(SKEY_CRYPT_KEY) - 1)) ||
		   (row->block == 'C' && row->depth == 2 && part.length == sizeof(CKEY_MODES) - 1 &&
			!memcmp(part.data, CKEY_MODES, sizeof(CKEY_MODES) - 1));
}

static void udb_query_send_status(Client *client)
{
	UdbContext *ctx = udb_ctx;
	UdbPropagatorSelection selected;
	int has_selected = ctx ? udb_select_propagator(ctx, 1, &selected) : 0;
	int policy_present = udb_propagator_policy_present(ctx);
	const char *sync_status_str;
	const char *selected_str;
	const char *advertised_str;
	const char *policy_source_str;
	const char *policy_str = udb_propagator_policy(ctx);
	const char *clients_str;
	unsigned long time_without_propagator = 0;
	time_t now = time(NULL);

	switch (udb_sync_status)
	{
	case UDB_SYNC_OK:
		sync_status_str = "OK";
		break;
	case UDB_SYNC_DEGRADED:
		sync_status_str = "DEGRADED";
		break;
	case UDB_SYNC_STALE:
		sync_status_str = "STALE";
		break;
	default:
		sync_status_str = "UNKNOWN";
		break;
	}

	if (has_selected)
		selected_str = selected.name;
	else
		selected_str = "none";

	if (has_selected)
		advertised_str = selected.name;
	else if (policy_present)
		advertised_str = "-";
	else
		advertised_str = "?";

	if (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator)
		policy_source_str = "local";
	else if (ctx && ctx->propagator_setting && *ctx->propagator_setting)
		policy_source_str = "S";
	else
		policy_source_str = "none";

	if (!policy_str || !*policy_str)
		policy_str = "none";

	if (udb_propagator_unavailable_since > 0 && now >= udb_propagator_unavailable_since)
		time_without_propagator = (unsigned long)(now - udb_propagator_unavailable_since);
	else
		time_without_propagator = 0;

	if (!udb_ready)
		clients_str = "DENIED";
	else
		clients_str = "ALLOWED";

	sendto_one(client, NULL, ":%s 339 %s :Database readiness: %s", me.name, client->name,
			   udb_ready ? "READY" : "BOOTSTRAPPING");
	sendto_one(client, NULL, ":%s 339 %s :UDB synchronization: %s", me.name, client->name, sync_status_str);
	sendto_one(client, NULL, ":%s 339 %s :Recovery: %s", me.name, client->name,
			   (udb_reconcile.active || udb_reconcile.next_retry_at) ? "ACTIVE" : "IDLE");
	sendto_one(client, NULL, ":%s 339 %s :Selected propagator: %s", me.name, client->name, selected_str);
	sendto_one(client, NULL, ":%s 339 %s :Selected direct source: %s", me.name, client->name, selected_str);
	sendto_one(client, NULL, ":%s 339 %s :Advertised state: HEL " UDB_PROTOCOL_HEL " %s", me.name, client->name,
			   advertised_str);
	sendto_one(client, NULL, ":%s 339 %s :Serving downstream: %s", me.name, client->name, udb_ready ? "YES" : "NO");
	sendto_one(client, NULL, ":%s 339 %s :Policy source: %s", me.name, client->name, policy_source_str);
	sendto_one(client, NULL, ":%s 339 %s :Policy: %s", me.name, client->name, policy_str);
	sendto_one(client, NULL, ":%s 339 %s :Configured authority: %s", me.name, client->name, policy_str);
	sendto_one(client, NULL, ":%s 339 %s :Time without propagator: %lu", me.name, client->name,
			   time_without_propagator);
	sendto_one(client, NULL, ":%s 339 %s :New local clients: %s", me.name, client->name, clients_str);

	if (udb_last_successful_sync > 0)
	{
		char timebuf[64];
		snprintf(timebuf, sizeof(timebuf), "%lu", (unsigned long)udb_last_successful_sync);
		sendto_one(client, NULL, ":%s 339 %s :Last successful synchronization: %s", me.name, client->name, timebuf);
	}
	else
	{
		sendto_one(client, NULL, ":%s 339 %s :Last successful synchronization: none", me.name, client->name);
	}
}

static void udb_query_send_operclasses(Client *client, const char *filter)
{
	UdbOclOrigin *origin;
	int shown = 0;

	if (!udb_ctx)
	{
		sendto_one(client, NULL, ":%s 339 %s :Operclass registry: unavailable", me.name, client->name);
		return;
	}
	sendto_one(client, NULL, ":%s 339 %s :Operclass registry: %s", me.name, client->name,
			   udb_ocl_registry_complete() ? "READY" : "INCOMPLETE");
	sendto_one(client, NULL, ":%s 339 %s :Local inventory: generation %lu epoch %s classes %u digest %s", me.name,
			   client->name, udb_ocl_local ? udb_ocl_local->generation : 0, udb_ocl_local ? udb_ocl_local->epoch : "-",
			   udb_ocl_local ? udb_ocl_local->count : 0, udb_ocl_local ? udb_ocl_local->inventory_digest : "-");
	if (!filter || match_simple(filter, me.name))
	{
		shown++;
		sendto_one(client, NULL, ":%s 339 %s :%s SID %s epoch %s gen %lu classes %u digest %s", me.name, client->name,
				   me.name, me.id, udb_ocl_local ? udb_ocl_local->epoch : "-",
				   udb_ocl_local ? udb_ocl_local->generation : 0, udb_ocl_local ? udb_ocl_local->count : 0,
				   udb_ocl_local ? udb_ocl_local->inventory_digest : "-");
	}
	for (origin = udb_ocl_origins; origin; origin = origin->next)
	{
		Client *acptr = udb_ocl_find_server(origin->sid);
		UdbOclOrigin *current_origin;

		if (!origin->member || !udb_ocl_is_participant(acptr) || IsMe(acptr))
			continue;
		if (filter && !match_simple(filter, acptr->name))
			continue;
		shown++;
		current_origin = udb_ocl_find_origin(acptr->id);
		if (current_origin && current_origin->current)
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s epoch %s gen %lu classes %u digest %s", me.name,
					   client->name, acptr->name, acptr->id, current_origin->current->epoch,
					   current_origin->current->generation, current_origin->current->count,
					   current_origin->current->inventory_digest);
		}
		else
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s MISSING inventory", me.name, client->name, acptr->name,
					   acptr->id);
		}
	}
	if (!shown)
		sendto_one(client, NULL, ":%s 339 %s :No servers match '%s'", me.name, client->name, filter);
}

static void udb_query_send_operclass(Client *client, const char *name)
{
	UdbOclOrigin *origin;
	UdbOclEntry *local_entry;
	int mismatch;

	if (!udb_ctx)
	{
		sendto_one(client, NULL, ":%s 339 %s :Operclass registry: unavailable", me.name, client->name);
		return;
	}
	if (!valid_operclass_name(name))
	{
		sendto_one(client, NULL, ":%s 339 %s :Invalid operclass name '%s'", me.name, client->name, name);
		return;
	}
	if (!udb_ocl_registry_complete())
	{
		sendto_one(client, NULL, ":%s 339 %s :Operclass registry: INCOMPLETE", me.name, client->name);
		return;
	}
	local_entry = udb_ocl_local ? udb_ocl_find_entry(udb_ocl_local, name) : NULL;
	if (!local_entry)
	{
		sendto_one(client, NULL, ":%s 339 %s :Operclass %s: NOT GLOBAL (missing locally)", me.name, client->name, name);
		return;
	}
	mismatch = 0;
	for (origin = udb_ocl_origins; origin; origin = origin->next)
	{
		Client *acptr = udb_ocl_find_server(origin->sid);
		UdbOclEntry *entry;

		if (!origin->member || !udb_ocl_is_participant(acptr) || IsMe(acptr))
			continue;
		entry = (origin && origin->current) ? udb_ocl_find_entry(origin->current, name) : NULL;
		if (!entry)
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s MISSING", me.name, client->name, acptr->name, acptr->id);
			mismatch = 1;
		}
		else if (strcmp(entry->digest, local_entry->digest))
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s MISMATCH %s", me.name, client->name, acptr->name, acptr->id,
					   entry->digest);
			mismatch = 1;
		}
		else
		{
			sendto_one(client, NULL, ":%s 339 %s :%s SID %s OK %s", me.name, client->name, acptr->name, acptr->id,
					   entry->digest);
		}
	}
	sendto_one(client, NULL, ":%s 339 %s :Operclass %s: %s %s", me.name, client->name, name,
			   mismatch ? "NOT GLOBAL" : "GLOBAL", local_entry->digest);
}

CMD_FUNC(cmd_udb)
{
	if (!IsUser(client) && !IsServer(client))
		return;

	if (IsUser(client) && !IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if (parc < 2 || !strcasecmp(parv[1], "STATUS"))
	{
		udb_query_send_status(client);
		return;
	}

	if (!strcasecmp(parv[1], "OPERCLASSES"))
	{
		udb_query_send_operclasses(client, parc >= 3 ? parv[2] : NULL);
		return;
	}

	if (!strcasecmp(parv[1], "OPERCLASS"))
	{
		if (parc < 3)
			sendto_one(client, NULL, ":%s 339 %s :Syntax: /UDB OPERCLASS <name>", me.name, client->name);
		else
			udb_query_send_operclass(client, parv[2]);
		return;
	}

	sendto_one(client, NULL, ":%s 339 %s :Syntax: /UDB STATUS | /UDB OPERCLASSES [filter] | /UDB OPERCLASS <name>",
			   me.name, client->name);
}

#define UDB_QUERY_ROWS 64
#define UDB_QUERY_BYTES ((size_t)32 * 1024)

typedef struct UdbQueryScratch
{
	UdbNativeKey key;
	UdbStorageField field;
	char path[UDB_STORAGE_PATH_MAX * 3 + 1];
	char value[UDB_STORAGE_VALUE_MAX + 1];
} UdbQueryScratch;

static UdbStorageResult udb_query_send_row(Client *client, const UdbStorageRow *row, UdbQueryScratch *scratch,
										   size_t *bytes)
{
	UdbStorageResult result = udb_native_key_path(row, scratch->path, sizeof(scratch->path));
	const char *value = scratch->value;
	if (result != UDB_STORAGE_OK)
		return result;
	if (udb_query_is_secret(row))
		value = "<redacted>";
	else if (row->kind == UDB_STORAGE_VALUE_STRING)
	{
		memcpy(scratch->value, row->value.data, row->value.length);
		scratch->value[row->value.length] = 0;
	}
	else if (row->kind == UDB_STORAGE_VALUE_NUMBER)
		snprintf(scratch->value, sizeof(scratch->value), "%" PRIu64, row->number);
	else
		value = "(empty)";
	size_t size = strlen(scratch->path) + strlen(value) + UDB_S2S_OVERHEAD_MAX;
	if (size > UDB_S2S_LINE_MAX || size > UDB_QUERY_BYTES - *bytes)
		return UDB_STORAGE_LIMIT;
	*bytes += size;
	sendto_one(client, NULL, ":%s 339 %s :DBQ %s %s", me.name, client->name, scratch->path, value);
	return UDB_STORAGE_OK;
}

CMD_FUNC(cmd_dbq)
{
	const char *query;
	UdbStorageRead *read = NULL;
	UdbStorageBlockIterator *iterator = NULL;
	UdbQueryScratch *scratch = NULL;
	UdbStorageResult result;
	UdbStorageRecord record;
	size_t bytes = 0;
	unsigned int shown = 0;
	int index;

	if (!IsUser(client) && !IsServer(client))
		return;
	if (IsUser(client) && !IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}
	if (parc < 2)
	{
		sendto_one(client, NULL, ":%s 339 %s :Syntax: /DBQ [server] <block>[::path]", me.name, client->name);
		return;
	}
	if (parc >= 3 && !match_simple(parv[1], me.name))
	{
		Client *server = find_server_quick(parv[1]);
		if (server)
			sendto_one(server, NULL, ":%s DBQ %s %s", client->id, parv[1], parv[2]);
		else
			sendnumeric(client, ERR_NOSUCHSERVER, parv[1]);
		return;
	}
	query = parv[parc >= 3 ? 2 : 1];
	if (!strcasecmp(query, "STATUS"))
	{
		udb_query_send_status(client);
		return;
	}
	index = *query ? udb_storage_stage_block_index((unsigned char)*query) : -1;
	if (index < 0)
	{
		sendto_one(client, NULL, ":%s 339 %s :Invalid block", me.name, client->name);
		return;
	}
	if (!udb_ctx || !udb_ctx->storage)
	{
		sendto_one(client, NULL, ":%s 339 %s :Native database unavailable", me.name, client->name);
		return;
	}
	result = udb_storage_read_open(udb_ctx->storage, &read);
	if (result != UDB_STORAGE_OK)
		goto finish;
	if (!query[1])
	{
		const UdbStorageBlockRoot *root = read->generation->state.blocks + index;
		char digest[65];
		for (unsigned int i = 0; i < 32; i++)
			snprintf(digest + 2 * i, 3, "%02x", root->summary.digest[i]);
		sendto_one(client, NULL, ":%s 339 %s :%c rows=%" PRIu64 " generation=%" PRIu64 " watermark=%" PRIu64 " %s",
				   me.name, client->name, root->block, root->summary.count, read->generation->state.generation,
				   read->generation->state.source_sequence, digest);
		goto finish;
	}
	scratch = udb_storage_alloc(&read->engine->budget, UDB_STORAGE_MEMORY_AUX, sizeof(*scratch));
	if (!scratch)
	{
		result = UDB_STORAGE_LIMIT;
		goto finish;
	}
	result = udb_native_key_parse(query, &scratch->key);
	if (result != UDB_STORAGE_OK)
		goto finish;
	result = udb_storage_read_field(read, &scratch->key.row, &scratch->field);
	if (result < UDB_STORAGE_NOT_FOUND)
		goto finish;
	if (result == UDB_STORAGE_OK && scratch->field.kind != UDB_STORAGE_VALUE_NONE)
	{
		UdbStorageRow row = scratch->key.row;
		row.kind = scratch->field.kind;
		row.number = scratch->field.number;
		row.value.data = (const unsigned char *)scratch->field.string;
		row.value.length = scratch->field.kind == UDB_STORAGE_VALUE_STRING ? scratch->field.length : 0;
		result = udb_query_send_row(client, &row, scratch, &bytes);
		goto finish;
	}
	result = udb_storage_read_iterator(read, scratch->key.row.block, &scratch->key.row, &iterator);
	if (result != UDB_STORAGE_OK)
		goto finish;
	while ((result = udb_storage_block_iterator_next(iterator, &record)) == UDB_STORAGE_OK)
	{
		if (shown == UDB_QUERY_ROWS)
		{
			result = UDB_STORAGE_LIMIT;
			break;
		}
		result = udb_query_send_row(client, &record.row, scratch, &bytes);
		if (result != UDB_STORAGE_OK)
			break;
		shown++;
	}
	if (result == UDB_STORAGE_NOT_FOUND && shown)
		result = UDB_STORAGE_OK;
finish:
	if (iterator)
	{
		UdbStorageResult closed = udb_storage_block_iterator_close(&iterator);
		if (closed != UDB_STORAGE_OK)
			result = closed;
	}
	udb_storage_free(scratch);
	if (read)
	{
		UdbStorageResult closed = udb_storage_read_close(&read);
		if (closed != UDB_STORAGE_OK)
			result = closed;
	}
	if (result == UDB_STORAGE_NOT_FOUND)
		sendto_one(client, NULL, ":%s 339 %s :No records found", me.name, client->name);
	else if (result == UDB_STORAGE_LIMIT)
		sendto_one(client, NULL, ":%s 339 %s :Query limit reached; narrow the prefix (maximum 64 rows / 32 KiB)",
				   me.name, client->name);
	else if (result != UDB_STORAGE_OK)
		sendto_one(client, NULL, ":%s 339 %s :Database query failed (%d); results may be incomplete", me.name,
				   client->name, (int)result);
}

static void udb_query_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DBQ", cmd_dbq, MAXPARA, CMD_USER | CMD_SERVER);
	CommandAdd(modinfo->handle, "UDB", cmd_udb, MAXPARA, CMD_USER | CMD_SERVER);
}

/* End of udb_query.c.inc */

/* Engine, block, configuration, and module lifecycle coordination */
/* Inlined: udb_lifecycle.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Module and Database Lifecycle Coordination
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static char *udb_block_filepath(char letter)
{
	const char *directory = PERMDATADIR;
	if (!directory || !*directory || strlen(directory) > UDB_BLOCK_PATH_MAX - 32)
		return NULL;
	size_t length = strlen(directory) + 32;
	char *path = safe_alloc(length);
	snprintf(path, length, "%s/udb/udb_%c.db", directory, letter);
	return path;
}

static UdbBlock *udb_block_create(UdbContext *ctx, char letter, const char *name)
{
	(void)name;
	if (!ctx || udb_storage_stage_block_index((unsigned char)letter) < 0)
		return NULL;
	UdbBlock *block = safe_alloc(sizeof(*block));
	block->letter = letter;
	block->filepath = udb_block_filepath(letter);
	if (!block->filepath)
	{
		safe_free(block);
		return NULL;
	}
	block->load_state = UDB_LOAD_UNINITIALIZED;
	ctx->blocks[(unsigned char)letter] = block;
	block->next = ctx->block_list;
	ctx->block_list = block;
	ctx->block_count++;
	return block;
}

static UdbBlock *udb_block_by_letter(UdbContext *ctx, char letter)
{
	return ctx ? ctx->blocks[(unsigned char)letter] : NULL;
}

static void udb_block_clear_pending(UdbBlock *block)
{
	if (!block)
		return;
	block->pending_from = NULL;
	block->pending_deadline = 0;
	block->pending_round_id = 0;
}

static int udb_rehash_reload;
static int udb_module_reload_pending(ModuleInfo *modinfo)
{
	if (!modinfo || !modinfo->handle || !modinfo->handle->header)
		return 0;
	for (Module *module = Modules; module; module = module->next)
		if (module != modinfo->handle && module->header && module->header->name &&
			!strcmp(module->header->name, modinfo->handle->header->name))
			return 1;
	return 0;
}

static int udb_native_metadata_restore(UdbContext *ctx)
{
	UdbStorageGeneration *pin = NULL;
	if (udb_storage_engine_pin(ctx->storage, &pin) != UDB_STORAGE_OK)
		return 0;
	const UdbStorageState *state = &pin->state;
	memcpy(ctx->authority_sid, state->source_sid, 4);
	snprintf(ctx->authority_epoch, sizeof(ctx->authority_epoch), "%016" PRIx64, state->source_epoch);
	ctx->authority_epoch_known = state->source_sid[0] != 0;
	ctx->last_applied_seq = state->source_sequence;
	ctx->current_seq = !strcmp(state->source_sid, me.id) ? state->source_sequence : 0;
	for (unsigned int i = 0; i < 6; i++)
	{
		UdbBlock *block = udb_block_by_letter(ctx, (char)state->blocks[i].block);
		if (!block)
		{
			udb_storage_engine_release(ctx->storage, &pin);
			return 0;
		}
		udb_native_digest_hex(state->blocks[i].summary.digest, block->sha256);
		block->record_count = state->blocks[i].summary.count;
		block->generation = state->generation;
		block->load_state = state->blocks[i].summary.count ? UDB_LOAD_SUCCESS : UDB_LOAD_EMPTY;
	}
	return udb_storage_engine_release(ctx->storage, &pin) == UDB_STORAGE_OK;
}

static void udb_engine_cleanup(UdbContext *ctx)
{
	if (!ctx)
		return;
	udb_ready = 0;
	UdbStorageEngine *engine = ctx->storage;
	/* Cancel private work and join before releasing callbacks, schema, pins,
	 * copied envelopes or any allocator whose budget the writer still uses. */
	if (engine && engine->budget.initialized && udb_storage_engine_stop(engine, 0) != UDB_STORAGE_OK)
		abort();
	udb_native_sync_cleanup(NULL);
	udb_native_receive_cleanup();
	udb_native_mutation_cleanup();
	udb_nick_preserve_on_rehash(udb_rehash_reload);
	udb_nick_shutdown();
	udb_channels_shutdown();
	udb_nick_preserve_on_rehash(0);
	udb_ips_shutdown();
	udb_lines_shutdown();
	udb_hello_cleanup_all();
	udb_ocl_shutdown();
	for (UdbBlock *block = ctx->block_list, *next; block; block = next)
	{
		next = block->next;
		udb_sync_session_free(block);
		safe_free(block->filepath);
		safe_free(block);
	}
	/* Derived settings are charged to this budget; free them before close. */
	udb_config_free(ctx);
	if (engine)
	{
		if (ctx->native_charge)
		{
			udb_storage_budget_uncharge(&engine->budget, UDB_STORAGE_MEMORY_AUX, ctx->native_charge);
			ctx->native_charge = 0;
		}
		if (engine->budget.initialized && udb_storage_engine_close(engine) != UDB_STORAGE_OK)
			abort();
		safe_free(engine);
	}
	udb_reconcile_reset();
	udb_bootstrap_peer = NULL;
	safe_free(ctx);
	udb_ctx = NULL;
}

static int udb_engine_init(void)
{
	if (!udb_block_descriptors_validate() || !PERMDATADIR || !*PERMDATADIR)
		return 0;
	if (!udb_cfg)
		udb_cfg = safe_alloc(sizeof(*udb_cfg));
	udb_config_defaults();
	udb_ctx = safe_alloc(sizeof(*udb_ctx));
	udb_ctx->storage = safe_alloc(sizeof(*udb_ctx->storage));
	udb_native_schema_capture(&udb_ctx->native_schema, &udb_ctx->storage->budget);
	for (unsigned int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		const UdbBlockDescriptor *descriptor = udb_block_descriptor(i);
		if (!descriptor || !udb_block_create(udb_ctx, descriptor->letter, descriptor->name))
		{
			udb_engine_cleanup(udb_ctx);
			return 0;
		}
	}
	udb_ready = 0;
	UdbStorageResult result = udb_storage_engine_open(
		udb_ctx->storage, PERMDATADIR, udb_cfg->cache_size, udb_storage_jobs_prepare, udb_storage_jobs_finish,
		udb_storage_jobs_cleanup, udb_native_row_validate, udb_native_stage_validate, udb_native_mutation_validate,
		udb_native_mutation_normalize, udb_native_block_profiles_validate, &udb_ctx->native_schema);
	if (result != UDB_STORAGE_OK || !udb_native_metadata_restore(udb_ctx))
	{
		udb_log(ULOG_ERROR, "UDB_NATIVE_OPEN_FAILED", NULL,
				"Native storage recovery failed; refusing admission (result $result)",
				log_data_integer("result", result));
		udb_engine_cleanup(udb_ctx);
		return 0;
	}
	/* Fixed foreground context/descriptors and their allocator overhead are
	 * included in the managed budget, even though daemon allocation owns them. */
	udb_ctx->native_charge = udb_storage_allocation_charge(sizeof(*udb_ctx));
	for (UdbBlock *block = udb_ctx->block_list; block; block = block->next)
		udb_ctx->native_charge +=
			udb_storage_allocation_charge(sizeof(*block)) + udb_storage_allocation_charge(strlen(PERMDATADIR) + 32);
	if (udb_storage_budget_charge(&udb_ctx->storage->budget, UDB_STORAGE_MEMORY_AUX, udb_ctx->native_charge) !=
		UDB_STORAGE_OK)
	{
		udb_ctx->native_charge = 0;
		udb_engine_cleanup(udb_ctx);
		return 0;
	}
	udb_policy_notify_deferred = 1;
	result = udb_native_runtime_startup(udb_ctx);
	udb_policy_notify_deferred = 0;
	if (result != UDB_STORAGE_OK)
	{
		udb_engine_cleanup(udb_ctx);
		return 0;
	}
	udb_propagator_policy_flush(udb_ctx);
	udb_propagator_policy_changed(udb_ctx);
	UdbPropagatorSelection selected;
	int local =
		!udb_propagator_policy_present(udb_ctx) || (udb_select_propagator(udb_ctx, 1, &selected) && selected.is_local);
	if (local && (!udb_ctx->authority_epoch_known || strcmp(udb_ctx->authority_sid, me.id)))
	{
		if (!udb_native_local_bind(udb_ctx))
		{
			udb_engine_cleanup(udb_ctx);
			return 0;
		}
	}
	else if (udb_ctx->authority_epoch_known)
	{
		/* Recovery has validated all six active roots and source metadata. */
		udb_ready = 1;
		udb_last_successful_sync = time(NULL);
	}
	udb_propagator_availability_refresh();
	udb_sync_status_refresh();
	return 1;
}

static void udb_engine_shutdown(void)
{
	udb_engine_cleanup(udb_ctx);
}

extern MODVAR Log *logs[NUM_LOG_DESTINATIONS];

static void udb_log_snomask_filter_init(void)
{
	Log *ld;
	for (ld = logs[LOG_DEST_SNOMASK]; ld; ld = ld->next)
	{
		LogSource *ls;
		int already_set = 0;
		for (ls = ld->sources; ls; ls = ls->next)
		{
			if (ls->negative && !strcmp(ls->subsystem, "udb"))
			{
				already_set = 1;
				break;
			}
		}
		if (!already_set)
		{
			ls = safe_alloc(sizeof(LogSource));
			ls->loglevel = ULOG_INVALID;
			ls->negative = 1;
			strlcpy(ls->subsystem, "udb", sizeof(ls->subsystem));
			AddListItem(ls, ld->sources);
		}
	}
	for (ld = logs[LOG_DEST_OPER]; ld; ld = ld->next)
	{
		LogSource *ls;
		int already_set = 0;
		for (ls = ld->sources; ls; ls = ls->next)
		{
			if (ls->negative && !strcmp(ls->subsystem, "udb"))
			{
				already_set = 1;
				break;
			}
		}
		if (!already_set)
		{
			ls = safe_alloc(sizeof(LogSource));
			ls->loglevel = ULOG_INVALID;
			ls->negative = 1;
			strlcpy(ls->subsystem, "udb", sizeof(ls->subsystem));
			AddListItem(ls, ld->sources);
		}
	}
}

static void udb_log_snomask_filter_free(void)
{
	Log *ld;
	for (ld = logs[LOG_DEST_SNOMASK]; ld; ld = ld->next)
	{
		LogSource *ls, *ls_next;
		for (ls = ld->sources; ls; ls = ls_next)
		{
			ls_next = ls->next;
			if (ls->negative && !strcmp(ls->subsystem, "udb"))
			{
				DelListItem(ls, ld->sources);
				safe_free(ls);
			}
		}
	}
	for (ld = logs[LOG_DEST_OPER]; ld; ld = ld->next)
	{
		LogSource *ls, *ls_next;
		for (ls = ld->sources; ls; ls = ls_next)
		{
			ls_next = ls->next;
			if (ls->negative && !strcmp(ls->subsystem, "udb"))
			{
				DelListItem(ls, ld->sources);
				safe_free(ls);
			}
		}
	}
}

static void udb_sync_snomask_filter(void)
{
	if (udb_is_debug_enabled())
		udb_log_snomask_filter_free();
	else
		udb_log_snomask_filter_init();
}

static int udb_module_test(ModuleInfo *modinfo)
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, udb_config_test);
	return MOD_SUCCESS;
}

static int udb_module_init(ModuleInfo *modinfo)
{
	ModuleSetOptions(modinfo->handle, MOD_OPT_PERM_RELOADABLE, 1);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, udb_config_run);
	udb_sync_snomask_filter();
	udb_protocol_init(modinfo);
	udb_nicks_init(modinfo);
	udb_channels_init(modinfo);
	if (!udb_ips_init(modinfo))
		return MOD_FAILED;
	udb_lines_init(modinfo);
	udb_query_init(modinfo);
	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

static int udb_module_load(ModuleInfo *modinfo)
{
	Client *server;

	if (udb_engine_init() == 0)
	{
		config_error("[UDB] Failed to initialize database engine");
		udb_nick_reconcile_after_reload();
		return MOD_FAILED;
	}
	udb_nick_reconcile_after_reload();
	udb_sync_snomask_filter();
	udb_ocl_membership_rebuild();
	udb_ocl_local_rebuild();
	udb_nicks_load(modinfo);
	udb_channels_load(modinfo);
	if (!udb_ips_load(modinfo))
	{
		config_error("[UDB] Failed to install host privacy protections");
		udb_engine_cleanup(udb_ctx);
		return MOD_FAILED;
	}
	udb_log(ULOG_INFO, "UDB_LOADED", NULL, "Unreal Database System v" UDB_VERSION " loaded successfully");

	list_for_each_entry(server, &server_list, special_node)
	{
		if (IsServer(server) && MyConnect(server) && !IsMe(server))
			udb_sync_hello_start(server);
	}

	return MOD_SUCCESS;
}

static int udb_module_unload(ModuleInfo *modinfo)
{
	udb_rehash_reload = (loop.rehashing && udb_module_reload_pending(modinfo)) ? 1 : 0;
	udb_log_snomask_filter_init();
	udb_log(ULOG_INFO, "UDB_UNLOADING", NULL, "Stopping native writer and shutting down...");
	udb_engine_shutdown();
	udb_log_snomask_filter_free();
	udb_rehash_reload = 0;
	return MOD_SUCCESS;
}

/* End of udb_lifecycle.c.inc */

MOD_TEST()
{
	return udb_module_test(modinfo);
}

MOD_INIT()
{
	return udb_module_init(modinfo);
}

MOD_LOAD()
{
	return udb_module_load(modinfo);
}

MOD_UNLOAD()
{
	return udb_module_unload(modinfo);
}
