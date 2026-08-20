/* UDB - Unreal Database System for UnrealIRCd 6
 * Originally by Trocotronic & MaD (UDB 3.6.1 for UnrealIRCd 3.2.8)
 * Migrated to UnrealIRCd 6 module API - 2026
 *
 * This header defines all shared structures, constants, and function
 * prototypes used across the UDB module suite.
 *
 * Architecture: Multiple implementation files (#include'd from udb.c)
 *   udb_core.inc.c    - Database engine, tree, hash, file I/O
 *   udb_protocol.inc.c - S2S protocol (DB command) and sync
 *   udb_nicks.inc.c   - Nick registration, identification, ghost
 *   udb_channels.inc.c - Channel registration, founder, modes
 *   udb_ips.inc.c     - IP management, clones, host overrides
 *   udb_lines.inc.c   - Distributed *lines (gline, zline, spamfilter)
 *   udb_query.inc.c   - DBQ user command for querying the database
 */

#ifndef UDB_H
#define UDB_H

#include "unrealircd.h"

#define UDB_VERSION       "4.0.0"
#define UDB_DB_SUBDIR     "data"

/* ========================================================================
 * Data Structures
 * ========================================================================
 * Records form a tree. Example for nick "John" with password and vhost:
 *
 *   N (block root)
 *   ├── John            (nick record)
 *   │   ├── pass "sha256:abc..."    (password sub-record)
 *   │   ├── vhost "john.users.net"  (vhost sub-record)
 *   │   └── oper "*3"              (oper level sub-record)
 *   └── Jane
 *       └── pass "sha256:xyz..."
 *
 * On disk, each record is stored as a text line:
 *   John::pass sha256:abc...
 *   John::vhost john.users.net
 *   John::oper *3
 *   Jane::pass sha256:xyz...
 *
 * The S2S protocol transmits these lines as:
 *   :SID DB * INS N::John::pass sha256:abc...
 *   :SID DB * DEL N::John::vhost
 * ======================================================================== */

typedef struct UdbRecord UdbRecord;
typedef struct UdbBlock  UdbBlock;

struct UdbRecord {
	char          *key;         /* Record key (nick, #channel, ip, etc.) */
	unsigned int   id;          /* Internal sequence ID */
	char          *data_str;    /* String data value (NULL if numeric) */
	unsigned long  data_num;    /* Numeric data value */
	UdbRecord     *hash_next;   /* Next entry in hash bucket */
	UdbRecord     *parent;      /* Parent record */
	UdbRecord     *sibling;     /* Next sibling at same level */
	UdbRecord     *child;       /* First child record */
	unsigned char  block_idx;   /* Block index (0-5) for fast parent lookups */
	unsigned int   is_b64:1;    /* 1 if key is base64-encoded */
	unsigned int   is_dynamic_key:1; /* 1 if key was dynamically allocated */
};

struct UdbBlock {
	UdbRecord     *tree;        /* Root node of the record tree */
	UdbBlock      *next;        /* Next block in global chain */
	unsigned long  checksum;    /* CRC32 of serialized data */
	char          *filepath;    /* Absolute path to block file on disk */
	unsigned int   id;          /* Block sequence ID */
	unsigned long  filesize;    /* Current serialized data size */
	time_t         modified_at; /* Timestamp of last modification */
	Client        *syncing_from;/* Server currently syncing this block to us */
	unsigned int   record_count;/* Total number of records in tree */
	char           letter;      /* Block identifier: N, C, I, S, L, K */
	unsigned int   version;     /* Data format version */
};

/* ========================================================================
 * Block Identifiers
 * ======================================================================== */
#define UDB_BLOCK_NICKS     'N'
#define UDB_BLOCK_CHANNELS  'C'
#define UDB_BLOCK_IPS       'I'
#define UDB_BLOCK_SETTINGS  'S'
#define UDB_BLOCK_LINKS     'L'
#define UDB_BLOCK_LINES     'K'

#define UDB_NUM_BLOCKS       6

/* ========================================================================
 * Sub-record Keys
 * ======================================================================== */

/* Nick sub-records: N::<nick>::<key> <value> */
#define NKEY_ACCESS     "access"     /* IP/CIDR access restriction */
#define NKEY_PASS       "pass"       /* Password hash */
#define NKEY_VHOST      "vhost"      /* Virtual host */
#define NKEY_FORBID     "forbid"     /* Forbidden nick (value = reason) */
#define NKEY_SUSPENDED  "suspended"  /* Suspended nick (value = reason) */
#define NKEY_OPER       "oper"       /* Oper level bitmask (*N) */
#define NKEY_CHALLENGE  "challenge"  /* Password hash method */
#define NKEY_MODES      "modes"      /* Allowed oper modes */
#define NKEY_SNOMASKS   "snomasks"   /* Allowed snomasks */
#define NKEY_SWHOIS     "swhois"     /* Custom SWHOIS line */

/* Channel sub-records: C::<#chan>::<key> <value> */
#define CKEY_FOUNDER    "founder"    /* Founder nick */
#define CKEY_MODES      "modes"      /* Locked channel modes */
#define CKEY_TOPIC      "topic"      /* Persistent topic */
#define CKEY_ACCESS     "access"     /* Access list (has sub-records per nick) */
#define CKEY_FORBID     "forbid"     /* Forbidden channel (value = reason) */
#define CKEY_SUSPENDED  "suspended"  /* Suspended channel */
#define CKEY_PASS       "pass"       /* Channel password for +ao */
#define CKEY_CHALLENGE  "challenge"  /* Channel password hash method */
#define CKEY_OPTIONS    "options"    /* Channel option flags (*N) */

/* IP sub-records: I::<ip|host>::<key> <value> */
#define IKEY_CLONES     "clones"     /* Max clones allowed (*N) */
#define IKEY_NOLINES    "nolines"    /* Exempt from *lines (GZQST chars) */
#define IKEY_HOST       "host"       /* Reverse DNS override */

/* Settings sub-records: S::<key> <value> */
#define SKEY_CRYPT_KEY  "encryption_key"  /* Host cloaking key */
#define SKEY_SUFFIX     "suffix"          /* Virtual host suffix */
#define SKEY_NICKSERV   "nickserv"        /* NickServ bot mask */
#define SKEY_CHANSERV   "chanserv"        /* ChanServ bot mask */
#define SKEY_IPSERV     "ipserv"          /* IpServ bot mask */
#define SKEY_CLONES     "clones"          /* Global max clones (*N) */
#define SKEY_QUIT_IPS   "quit_ips"        /* Quit message for IP limit */
#define SKEY_QUIT_CLONES "quit_clones"    /* Quit message for clone limit */
#define SKEY_CHALLENGE  "challenge"       /* Global hash method */
#define SKEY_FLOOD      "flood"           /* Password flood limit V:S */
#define SKEY_PREFIXES   "prefixes"        /* Channel mode prefixes */

/* Link sub-records: L::<server>::<key> <value> */
#define LKEY_OPTIONS    "options"         /* Link option flags (*N) */

/* Line sub-records: K::<type>::<pattern>::<key> <value> */
#define KKEY_TYPE       "type"            /* Spamfilter target type */
#define KKEY_ACTION     "action"          /* Spamfilter action */
#define KKEY_DURATION   "duration"        /* TKL duration */
#define KKEY_REASON     "reason"          /* Ban reason */

/* ========================================================================
 * Error Codes (for DB ERR protocol messages)
 * ======================================================================== */
#define UDB_ERR_NO_BLOCK    1   /* Block does not exist */
#define UDB_ERR_OFFSET      2   /* Data offset mismatch */
#define UDB_ERR_NOT_HUB     3   /* Only hub can insert/delete */
#define UDB_ERR_PARAMS      4   /* Missing parameters */
#define UDB_ERR_CANNOT_OPEN 5   /* Cannot open block file */
#define UDB_ERR_FATAL       6   /* Fatal / internal error */
#define UDB_ERR_SYNC_ACTIVE 7   /* Sync already in progress */
#define UDB_ERR_NO_SYNC     8   /* No sync was requested */
#define UDB_ERR_FORBIDDEN   9   /* Forbidden server */
#define UDB_ERR_DUPLICATE  10   /* Duplicate record */

/* ========================================================================
 * Oper Levels (bitmask stored in N::<nick>::oper *<value>)
 * ======================================================================== */
#define UDB_OPER_HELPER    0x1  /* Pre-operator: receives +h automatically */
#define UDB_OPER_ADMIN     0x2  /* Admin: receives +oa */
#define UDB_OPER_ROOT      0x4  /* Root: receives +oN, can /rehash /restart */

/* ========================================================================
 * Channel Option Flags (bitmask in C::<#chan>::options *<value>)
 * ======================================================================== */
#define UDB_CHOPT_PROTECT_BANS  0x1  /* Only ban author can remove their bans */
#define UDB_CHOPT_LOCK_MODES    0x2  /* Channel modes are locked */

/* ========================================================================
 * Link Option Flags (bitmask in L::<server>::options *<value>)
 * ======================================================================== */
#define UDB_LNKOPT_DEBUG         0x1  /* Debug: receives all UDB mode changes */
#define UDB_LNKOPT_PROPAGATOR    0x2  /* Propagator: only server that can push data */
#define UDB_LNKOPT_ALLOW_CLIENTS 0x4  /* Allow clients on non-UDB leaf uline */

/* ========================================================================
 * Hash Table Configuration
 * ======================================================================== */
#define UDB_HASH_SIZE  2048
#define UDB_HASH_MASK  (UDB_HASH_SIZE - 1)

/* ========================================================================
 * Module Configuration (parsed from unrealircd.conf udb { } block)
 * ======================================================================== */
typedef struct UdbConfig {
	char *db_directory;     /* Directory for database files */
	char *propagator;       /* Propagator server name */
	int   max_global_clones;/* Global clone limit (0 = use ircd default) */
	int   flood_attempts;   /* Password flood: max attempts */
	int   flood_period;     /* Password flood: time window in seconds */
} UdbConfig;

/* ========================================================================
 * UDB Context - Central state for the entire module
 * ======================================================================== */
typedef struct UdbContext {
	/* Block pointers (indexed by letter for fast lookup) */
	UdbBlock  *blocks[256];  /* blocks['N'], blocks['C'], etc. */
	UdbBlock  *block_list;   /* Linked list of all blocks */

	/* Root trees (convenience aliases for blocks[X]->tree) */
	UdbRecord *nicks;
	UdbRecord *channels;
	UdbRecord *ips;
	UdbRecord *settings;
	UdbRecord *links;
	UdbRecord *lines;

	/* Hash table for O(1) record lookup */
	UdbRecord **hash_table[UDB_NUM_BLOCKS];

	/* State */
	Client    *propagator;   /* Currently known propagator server */
	int        block_count;
	int        total_records;
} UdbContext;

/* ========================================================================
 * Global State (defined in udb_core.inc.c)
 * ======================================================================== */
static UdbContext *udb_ctx = NULL;
static UdbConfig  *udb_cfg = NULL;

/* ========================================================================
 * Core Engine API (udb_core.inc.c)
 * ======================================================================== */

/* Initialization and shutdown */
static int  udb_engine_init(void);
static void udb_engine_shutdown(void);

/* Block management */
static UdbBlock  *udb_block_create(char letter, const char *name);
static int        udb_block_load(UdbBlock *block);
static void       udb_block_unload(UdbBlock *block);
static void       udb_blocks_load_all(void);
static void       udb_blocks_save_all(void);
static UdbBlock  *udb_block_by_letter(char letter);

/* Record operations */
static UdbRecord *udb_record_find(const char *key, UdbRecord *parent);
static UdbRecord *udb_record_create(UdbRecord *parent);
static UdbRecord *udb_record_insert(UdbBlock *block, UdbRecord *parent,
                                     const char *key, const char *data_str,
                                     unsigned long data_num, int persist);
static UdbRecord *udb_record_find_path(UdbBlock *block, const char *path);
static UdbRecord *udb_record_delete(UdbBlock *block, UdbRecord *rec, int persist);
static void       udb_record_free_tree(UdbRecord *rec);

/* Hash operations */
static void udb_hash_init(void);
static void udb_hash_destroy(void);
static void udb_hash_insert_record(UdbRecord *rec, int block_idx, const char *key);
static int  udb_hash_remove_record(UdbRecord *rec, int block_idx, const char *key);
static UdbRecord *udb_hash_find(int block_idx, const char *key);

/* File I/O */
static int        udb_file_save_block(UdbBlock *block);
static int        udb_file_load_block(UdbBlock *block);
static UdbRecord *udb_file_parse_line(UdbBlock *block, char *line);
static void       udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp,
                                     char *pathbuf, int pathlen);

/* Checksum */
static unsigned long udb_crc32(const char *data, size_t len);
static unsigned long udb_compute_block_checksum(UdbBlock *block);

/* Block index helpers */
static int  udb_block_letter_to_index(char letter);

/* ========================================================================
 * Protocol API (udb_protocol.inc.c)
 * ======================================================================== */
static void udb_sync_to_server(Client *server);
static int  udb_is_udb_server(Client *server);
static int  udb_is_propagator(Client *server);

/* ========================================================================
 * Nick API (udb_nicks.inc.c)
 * ======================================================================== */
static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync);
static void udb_nick_strip(Client *client, UdbRecord *nick_rec);
static int  udb_nick_check_password(const char *nick, const char *pass,
                                     UdbRecord *nick_rec, Client *client);
static void udb_nick_set_vhost(Client *client, UdbRecord *vhost_rec);
static void udb_nick_remove_vhost(Client *client);
static void udb_nick_grant_oper(Client *client, UdbRecord *nick_rec,
                                 UdbRecord *oper_rec);
static void udb_nick_set_modes(Client *client, UdbRecord *nick_rec,
                                UdbRecord *mode_rec, const char *modes);
static void udb_nick_set_swhois(Client *client, UdbRecord *nick_rec,
                                 UdbRecord *swhois_rec);
static void udb_nick_set_snomasks(Client *client, UdbRecord *nick_rec,
                                   UdbRecord *snomask_rec);

/* ========================================================================
 * Channel API (udb_channels.inc.c)
 * ======================================================================== */
static void udb_channel_apply_record(Channel *channel, UdbRecord *chan_rec,
                                      const char *subkey, int is_new);
static void udb_channel_remove_record(Channel *channel, UdbRecord *chan_rec,
                                       const char *subkey);

/* ========================================================================
 * IP API (udb_ips.inc.c)
 * ======================================================================== */
static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec,
                                 const char *subkey, int is_new);
static void udb_ip_remove_record(const char *ip_key, UdbRecord *ip_rec,
                                  const char *subkey);

/* ========================================================================
 * Lines API (udb_lines.inc.c)
 * ======================================================================== */
static void udb_line_apply_record(UdbRecord *line_rec, int is_new);
static void udb_line_remove_record(UdbRecord *line_rec);

/* ========================================================================
 * Query API (udb_query.inc.c)
 * ======================================================================== */
/* (command handler only, no exported functions) */

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

/* Get bot identity from settings block */
static const char *udb_get_bot_nick(const char *service_key, int force_default);
static const char *udb_get_bot_mask(const char *service_key, int force_default);

/* Apply/remove special record effects (dispatcher) */
static int  udb_apply_special_record(UdbBlock *block, UdbRecord *rec, int is_new);
static void udb_remove_special_record(UdbBlock *block, UdbRecord *rec);

/* Debug output */
static void udb_send_to_debugs(Client *source, const char *fmt, ...)
                                __attribute__((format(printf, 2, 3)));

/* Logging helpers - wrap unreal_log for consistent subsystem */
#define udb_log(level, event_id, client, msg, ...) \
	unreal_log(level, "udb", event_id, client, "[UDB] " msg, ##__VA_ARGS__)

/* Convenience macro for string replacement (like the old ircstrdup) */
#define udb_strdup(dest, src) safe_strdup(dest, src)

#endif /* UDB_H */
