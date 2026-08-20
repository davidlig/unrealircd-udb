/*
 * UDB - Unreal Database System for UnrealIRCd 6
 * A distributed database engine for nick/channel/IP management and sync.
 * (C) 2026 David Abuín Fontán ('davidlig')
 * License: GPLv2+
 */

/*** <<<MODULE MANAGER START>>>
module
{
	documentation "https://github.com/davidlig/unrealircd-udb";
	troubleshooting "In case of problems, report issues at https://github.com/davidlig/unrealircd-udb/issues";
	min-unrealircd-version "6.*";
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
static void       udb_block_reset(UdbBlock *block);
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
static int  udb_check_password(const char *pass, UdbRecord *profile_rec,
                               Client *client);
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


/* ========================================================================
 * Module Header
 * ======================================================================== */

ModuleHeader MOD_HEADER = {
	"third/udb",
	"4.0.0",
	"UDB - Unreal Database System (nick/channel/IP registration & sync)",
	"David Abuín Fontán ('davidlig')",
	"unrealircd-6"
};

/* ========================================================================
 * Implementation Files
 *
 * Each file implements a specific subsystem. They share the same compilation
 * unit, so all functions are static and can call each other freely.
 * ======================================================================== */

/* Core database engine: tree, hash, file I/O, record management */
/* Inlined: udb_core.c.inc */
/*
 * UDB Core Engine for UnrealIRCd 6
 * Implements the database structures, hash, file I/O, and record manipulation.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Forward declare prototypes from other inc files to prevent compiler warnings */
static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync);
static void udb_nick_strip(Client *client, UdbRecord *nick_rec);
static void udb_nick_revoke_oper(Client *client);
static void udb_channel_apply_record(Channel *channel, UdbRecord *chan_rec, const char *subkey, int is_new);
static void udb_channel_remove_record(Channel *channel, UdbRecord *chan_rec, const char *subkey);
static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey, int is_new);
static void udb_ip_remove_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey);
static void udb_line_apply_record(UdbRecord *line_rec, int is_new);
static void udb_line_remove_record(UdbRecord *line_rec);

/* ========================================================================
 * Block Index Helpers
 * ======================================================================== */
static int udb_block_letter_to_index(char letter) {
    switch (letter) {
        case 'N': return 0;
        case 'C': return 1;
        case 'I': return 2;
        case 'S': return 3;
        case 'L': return 4;
        case 'K': return 5;
        default: return 0;
    }
}

/* ========================================================================
 * Hash Operations
 * ======================================================================== */
static void udb_hash_init(void) {
    for (int i = 0; i < UDB_NUM_BLOCKS; i++) {
        udb_ctx->hash_table[i] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
    }
}

static void udb_hash_destroy(void) {
    for (int i = 0; i < UDB_NUM_BLOCKS; i++) {
        if (udb_ctx->hash_table[i]) {
            safe_free(udb_ctx->hash_table[i]);
            udb_ctx->hash_table[i] = NULL;
        }
    }
}

static void udb_hash_clear_block(int block_idx) {
    if (block_idx < 0 || block_idx >= UDB_NUM_BLOCKS)
        return;
    safe_free(udb_ctx->hash_table[block_idx]);
    udb_ctx->hash_table[block_idx] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
}

static unsigned int udb_hash_str(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
        hash = ((hash << 5) + hash) + c; // djb2
    }
    return hash & UDB_HASH_MASK;
}

static void udb_hash_insert_record(UdbRecord *rec, int block_idx, const char *key) {
    unsigned int h = udb_hash_str(key);
    rec->hash_next = udb_ctx->hash_table[block_idx][h];
    udb_ctx->hash_table[block_idx][h] = rec;
}

static int udb_hash_remove_record(UdbRecord *rec, int block_idx, const char *key) {
    unsigned int h = udb_hash_str(key);
    UdbRecord *curr = udb_ctx->hash_table[block_idx][h];
    UdbRecord *prev = NULL;
    
    while (curr) {
        if (curr == rec) {
            if (prev) prev->hash_next = curr->hash_next;
            else udb_ctx->hash_table[block_idx][h] = curr->hash_next;
            return 1;
        }
        prev = curr;
        curr = curr->hash_next;
    }
    return 0;
}

static UdbRecord *udb_hash_find(int block_idx, const char *key) {
    if (!key) return NULL;
    unsigned int h = udb_hash_str(key);
    UdbRecord *curr = udb_ctx->hash_table[block_idx][h];
    while (curr) {
        if (!strcasecmp(curr->key, key)) return curr;
        curr = curr->hash_next;
    }
    return NULL;
}

/* ========================================================================
 * Record Operations
 * ======================================================================== */
static const char *udb_get_shared_subkey(const char *key) {
    static const char *known_keys[] = {
        "pass", "vhost", "oper", "swhois", "snomasks", "modes", "access",
        "forbid", "suspended", "challenge", "founder", "topic", "options",
        "clones", "nolines", "host", "encryption_key", "suffix", "nickserv",
        "chanserv", "ipserv", "quit_ips", "quit_clones", "flood", "prefixes",
        "type", "action", "duration", "reason", NULL
    };
    for (int i = 0; known_keys[i]; i++) {
        if (!strcasecmp(known_keys[i], key)) return known_keys[i];
    }
    return NULL;
}

static UdbRecord *udb_record_create(UdbRecord *parent) {
    UdbRecord *rec = safe_alloc(sizeof(UdbRecord));
    rec->parent = parent;
    
    if (parent) {
        rec->block_idx = parent->block_idx;
        rec->sibling = parent->child;
        parent->child = rec;
    }
    return rec;
}

static UdbRecord *udb_record_find(const char *key, UdbRecord *parent) {
    if (!parent) return NULL;
    
    if (parent->parent == NULL && udb_ctx) {
        return udb_hash_find(parent->block_idx, key);
    }
    
    UdbRecord *child = parent->child;
    while (child) {
        if (!strcasecmp(child->key, key)) {
            return child;
        }
        child = child->sibling;
    }
    return NULL;
}

static UdbRecord *udb_record_find_path(UdbBlock *block, const char *path) {
    if (!block || !block->tree || !path) return NULL;
    char pathbuf[512];
    strlcpy(pathbuf, path, sizeof(pathbuf));
    char *cur = pathbuf;
    char *ds;
    UdbRecord *rec = block->tree;
    while ((ds = strstr(cur, "::"))) {
        *ds = '\0';
        rec = udb_record_find(cur, rec);
        if (!rec) return NULL;
        cur = ds + 2;
    }
    return udb_record_find(cur, rec);
}

static UdbRecord *udb_record_insert(UdbBlock *block, UdbRecord *parent, const char *key, const char *data_str, unsigned long data_num, int persist) {
    if (!parent) parent = block->tree;
    UdbRecord *rec = udb_record_find(key, parent);
    if (!rec) {
        rec = udb_record_create(parent);
        if (key) {
            if (parent == block->tree) {
                safe_strdup(rec->key, key);
                rec->is_dynamic_key = 1;
            } else {
                const char *shared = udb_get_shared_subkey(key);
                if (shared) {
                    rec->key = (char *)shared;
                    rec->is_dynamic_key = 0;
                } else {
                    safe_strdup(rec->key, key);
                    rec->is_dynamic_key = 1;
                }
            }
        }
        if (parent == block->tree) {
            udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), key);
        }
        block->record_count++;
        udb_ctx->total_records++;
    }
    
    if (rec->data_str) {
        safe_free(rec->data_str);
    }
    
    // Auto-detect numeric data if it starts with *
    if (data_str && *data_str == '*') {
        rec->data_num = atoi(data_str + 1);
        rec->data_str = NULL;
    } else if (data_str) {
        safe_strdup(rec->data_str, data_str);
        rec->data_num = 0;
    } else {
        rec->data_str = NULL;
        rec->data_num = data_num;
    }
    
    if (persist) {
        udb_file_save_block(block);
    }
    
    udb_apply_special_record(block, rec, 1);
    
    return rec;
}

static void udb_record_free_tree(UdbRecord *rec) {
    if (!rec) return;
    
    UdbRecord *child = rec->child;
    while (child) {
        UdbRecord *next = child->sibling;
        udb_record_free_tree(child);
        child = next;
    }
    
    if (rec->key && rec->is_dynamic_key) safe_free(rec->key);
    if (rec->data_str) safe_free(rec->data_str);
    safe_free(rec);
}

static UdbRecord *udb_record_delete(UdbBlock *block, UdbRecord *rec, int persist) {
    if (!rec) return NULL;
    
    udb_remove_special_record(block, rec);
    
    if (rec->parent) {
        if (rec->parent->parent == NULL) {
            udb_hash_remove_record(rec, udb_block_letter_to_index(block->letter), rec->key);
        }
        
        UdbRecord *curr = rec->parent->child;
        UdbRecord *prev = NULL;
        while (curr) {
            if (curr == rec) {
                if (prev) prev->sibling = curr->sibling;
                else rec->parent->child = curr->sibling;
                break;
            }
            prev = curr;
            curr = curr->sibling;
        }
    }
    
    if (block->record_count > 0) block->record_count--;
    if (udb_ctx->total_records > 0) udb_ctx->total_records--;
    
    udb_record_free_tree(rec);
    
    if (persist) {
        udb_file_save_block(block);
    }
    return NULL;
}

/* ========================================================================
 * Checksum Operations
 * ======================================================================== */
static unsigned long udb_crc32_step(unsigned long crc, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= (unsigned char)data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320UL;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static unsigned long udb_crc32(const char *data, size_t len) {
    return udb_crc32_step(0xFFFFFFFFUL, data, len) ^ 0xFFFFFFFFUL;
}

static unsigned long udb_compute_block_checksum(UdbBlock *block) {
    if (!block || !block->filepath) return 0;
    FILE *fp = fopen(block->filepath, "rb");
    if (!fp) return 0;
    
    unsigned long crc = 0xFFFFFFFFUL;
    char buf[4096];
    size_t len;
    
    while ((len = fread(buf, 1, sizeof(buf), fp)) > 0) {
        crc = udb_crc32_step(crc, buf, len);
    }
    fclose(fp);
    return crc ^ 0xFFFFFFFFUL;
}

/* ========================================================================
 * File I/O Operations
 * ======================================================================== */
static UdbRecord *udb_file_parse_line(UdbBlock *block, char *line) {
    if (!line || !*line || *line == ';') return NULL;
    
    char *value = strchr(line, ' ');
    char *data_str = NULL;
    unsigned long data_num = 0;
    
    if (value) {
        *value++ = '\0';
        if (*value == '*') {
            data_num = strtoul(value + 1, NULL, 10);
        } else {
            data_str = value;
        }
    }
    
    char *p = line;
    UdbRecord *parent = block->tree;
    UdbRecord *leaf_rec = NULL;
    while (p && *p) {
        char *next = strstr(p, "::");
        if (next) {
            *next = '\0';
            next += 2;
        }
        
        UdbRecord *rec = udb_record_find(p, parent);
        if (!rec) {
            rec = udb_record_create(parent);
            if (p && *p) {
                if (parent == block->tree) {
                    safe_strdup(rec->key, p);
                    rec->is_dynamic_key = 1;
                } else {
                    const char *shared = udb_get_shared_subkey(p);
                    if (shared) {
                        rec->key = (char *)shared;
                        rec->is_dynamic_key = 0;
                    } else {
                        safe_strdup(rec->key, p);
                        rec->is_dynamic_key = 1;
                    }
                }
            }
            if (parent == block->tree) {
                udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), p);
            }
            block->record_count++;
            udb_ctx->total_records++;
        }
        
        if (!next) {
            if (data_str && *data_str == '*') {
                rec->data_num = atoi(data_str + 1);
                if (rec->data_str) {
                    safe_free(rec->data_str);
                    rec->data_str = NULL;
                }
            } else if (data_str) {
                safe_strdup(rec->data_str, data_str);
                rec->data_num = 0;
            } else {
                if (rec->data_str) {
                    safe_free(rec->data_str);
                    rec->data_str = NULL;
                }
                rec->data_num = data_num;
            }
            leaf_rec = rec;
        }
        
        parent = rec;
        p = next;
    }
    return leaf_rec;
}

static void udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf, int pathlen) {
    if (!rec) return;
    
    size_t old_len = strlen(pathbuf);
    
    if (depth > 0) {
        strlcat(pathbuf, "::", pathlen);
    }
    strlcat(pathbuf, rec->key, pathlen);
    
    if (rec->data_str || rec->data_num > 0 || !rec->child) {
        if (rec->data_str) {
            fprintf(fp, "%s %s\n", pathbuf, rec->data_str);
        } else if (rec->data_num > 0 || !rec->child) {
            fprintf(fp, "%s *%lu\n", pathbuf, rec->data_num);
        }
    }
    
    if (rec->child) {
        UdbRecord *child = rec->child;
        while (child) {
            udb_serialize_tree(child, depth + 1, fp, pathbuf, pathlen);
            child = child->sibling;
        }
    }
    
    // Restore pathbuf
    pathbuf[old_len] = '\0';
}

static int udb_file_save_block(UdbBlock *block) {
    if (!block || !block->filepath) return 0;
    
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath);
    FILE *fp = fopen(tmp_path, "w");
    if (!fp) return 0;
    
    fprintf(fp, "; UDB Block %c - Version %d\n", block->letter, block->version);
    fprintf(fp, "; Saved: %ld\n", (long)time(NULL));
    fprintf(fp, "; Records: %u\n", block->record_count);
    
    char pathbuf[4096];
    pathbuf[0] = '\0';
    
    if (block->tree) {
        UdbRecord *rec = block->tree->child;
        while (rec) {
            udb_serialize_tree(rec, 0, fp, pathbuf, sizeof(pathbuf));
            rec = rec->sibling;
        }
    }
    
    fclose(fp);
    
    rename(tmp_path, block->filepath);
    block->checksum = udb_compute_block_checksum(block);
    block->modified_at = time(NULL);
    
    struct stat st;
    if (stat(block->filepath, &st) == 0) {
        block->filesize = st.st_size;
    }
    
    return 1;
}

static int udb_file_load_block(UdbBlock *block) {
    if (!block || !block->filepath) return 0;
    FILE *fp = fopen(block->filepath, "r");
    if (!fp) return 0;
    
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        char *p = strchr(line, '\n');
        if (p) *p = '\0';
        p = strchr(line, '\r');
        if (p) *p = '\0';
        
        if (line[0] == ';' || line[0] == '\0') continue;
        
        udb_file_parse_line(block, line);
    }
    fclose(fp);
    
    UdbRecord *curr = block->tree->child;
    while (curr) {
        UdbRecord *sub = curr->child;
        while (sub) {
            udb_apply_special_record(block, sub, 1);
            sub = sub->sibling;
        }
        udb_apply_special_record(block, curr, 1);
        curr = curr->sibling;
    }
    
    block->checksum = udb_compute_block_checksum(block);
    
    struct stat st;
    if (stat(block->filepath, &st) == 0) {
        block->filesize = st.st_size;
        block->modified_at = st.st_mtime;
    }
    char logbuf[512];
    snprintf(logbuf, sizeof(logbuf), "[UDB] Loaded block %c from %s (%u records)", block->letter, block->filepath, block->record_count);
    unreal_log(ULOG_INFO, "udb", "UDB_FILE_LOADED", NULL, "$msg", log_data_string("msg", logbuf));
    
    return 1;
}

/* ========================================================================
 * Block Management
 * ======================================================================== */
static UdbBlock *udb_block_create(char letter, const char *name) {
    UdbBlock *b = safe_alloc(sizeof(UdbBlock));
    b->letter = letter;
    b->version = 1;
    b->tree = udb_record_create(NULL);
    b->tree->block_idx = (unsigned char)udb_block_letter_to_index(letter);
    safe_strdup(b->tree->key, name);
    b->tree->data_num = 1;
    
    char path[512];
    snprintf(path, sizeof(path), "udb_%c.db", letter);
    safe_strdup(b->filepath, path);
    convert_to_absolute_path(&b->filepath, PERMDATADIR);
    
    udb_ctx->blocks[(unsigned char)letter] = b;
    b->next = udb_ctx->block_list;
    udb_ctx->block_list = b;
    
    udb_ctx->block_count++;
    return b;
}

static void udb_block_set_context_root(UdbBlock *block) {
    if (!udb_ctx || !block)
        return;
    switch (block->letter) {
        case 'N': udb_ctx->nicks = block->tree; break;
        case 'C': udb_ctx->channels = block->tree; break;
        case 'I': udb_ctx->ips = block->tree; break;
        case 'S': udb_ctx->settings = block->tree; break;
        case 'L': udb_ctx->links = block->tree; break;
        case 'K': udb_ctx->lines = block->tree; break;
    }
}

static void udb_block_reset(UdbBlock *block) {
    char *name = NULL;
    int block_idx;

    if (!block)
        return;

    if (block->tree && block->tree->key)
        safe_strdup(name, block->tree->key);
    else
        safe_strdup(name, "UDB");
    block_idx = udb_block_letter_to_index(block->letter);

    if (block->tree) {
        if (udb_ctx->total_records >= block->record_count)
            udb_ctx->total_records -= block->record_count;
        else
            udb_ctx->total_records = 0;
        udb_record_free_tree(block->tree);
    }
    udb_hash_clear_block(block_idx);

    block->tree = udb_record_create(NULL);
    block->tree->block_idx = (unsigned char)block_idx;
    safe_strdup(block->tree->key, name);
    block->tree->is_dynamic_key = 1;
    block->tree->data_num = 1;
    block->record_count = 0;
    udb_block_set_context_root(block);
    safe_free(name);
}

static int udb_block_load(UdbBlock *block) {
    return udb_file_load_block(block);
}

static void udb_block_unload(UdbBlock *block) {
    if (block->tree) {
        udb_record_free_tree(block->tree);
        block->tree = NULL;
    }
}

static void udb_blocks_load_all(void) {
    UdbBlock *b = udb_ctx->block_list;
    while (b) {
        udb_block_load(b);
        b = b->next;
    }
}

static void udb_blocks_save_all(void) {
    UdbBlock *b = udb_ctx->block_list;
    while (b) {
        udb_file_save_block(b);
        b = b->next;
    }
}

static UdbBlock *udb_block_by_letter(char letter) {
    return udb_ctx ? udb_ctx->blocks[(unsigned char)letter] : NULL;
}

/* ========================================================================
 * Initialization and Shutdown
 * ======================================================================== */
static int udb_engine_init(void) {
    udb_ctx = safe_alloc(sizeof(UdbContext));
    udb_hash_init();
    
    struct stat st = {0};
    const char *dir = udb_cfg && udb_cfg->db_directory ? udb_cfg->db_directory : "data/udb";
    if (stat(dir, &st) == -1) {
        mkdir(dir, 0700);
    }
    
    udb_block_create('N', "Nicks");
    udb_block_create('C', "Channels");
    udb_block_create('I', "IPs");
    udb_block_create('S', "Settings");
    udb_block_create('L', "Links");
    udb_block_create('K', "Lines");
    
    udb_ctx->nicks = udb_ctx->blocks['N']->tree;
    udb_ctx->channels = udb_ctx->blocks['C']->tree;
    udb_ctx->ips = udb_ctx->blocks['I']->tree;
    udb_ctx->settings = udb_ctx->blocks['S']->tree;
    udb_ctx->links = udb_ctx->blocks['L']->tree;
    udb_ctx->lines = udb_ctx->blocks['K']->tree;
    
    udb_blocks_load_all();
    return 1;
}

static void udb_engine_shutdown(void) {
    if (!udb_ctx) return;
    
    udb_blocks_save_all();
    
    UdbBlock *b = udb_ctx->block_list;
    while (b) {
        UdbBlock *next = b->next;
        udb_block_unload(b);
        safe_free(b->filepath);
        safe_free(b);
        b = next;
    }
    
    udb_hash_destroy();
    safe_free(udb_ctx);
    udb_ctx = NULL;
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */
static const char *udb_get_bot_nick(const char *service_key, int force_default) {
    if (!force_default && udb_ctx && udb_ctx->settings) {
        UdbRecord *rec = udb_record_find(service_key, udb_ctx->settings);
        if (rec && rec->data_str) {
            static char buf[64];
            strlcpy(buf, rec->data_str, sizeof(buf));
            char *p = strchr(buf, '!');
            if (p) *p = '\0';
            return buf;
        }
    }
    if (!strcasecmp(service_key, SKEY_NICKSERV)) return "NickServ";
    if (!strcasecmp(service_key, SKEY_CHANSERV)) return "ChanServ";
    if (!strcasecmp(service_key, SKEY_IPSERV)) return "IpServ";
    return "UDB";
}

static const char *udb_get_bot_mask(const char *service_key, int force_default) {
    if (!force_default && udb_ctx && udb_ctx->settings) {
        UdbRecord *rec = udb_record_find(service_key, udb_ctx->settings);
        if (rec && rec->data_str) {
            return rec->data_str;
        }
    }
    if (!strcasecmp(service_key, SKEY_NICKSERV)) return "NickServ!*@*";
    if (!strcasecmp(service_key, SKEY_CHANSERV)) return "ChanServ!*@*";
    if (!strcasecmp(service_key, SKEY_IPSERV)) return "IpServ!*@*";
    return "UDB!*@*";
}

static int udb_apply_special_record(UdbBlock *block, UdbRecord *rec, int is_new) {
    if (!rec) return 0;
    if (block->letter == 'N') {
        UdbRecord *nick_rec = rec->parent == block->tree ? rec : rec->parent;
        Client *client = find_user(nick_rec->key, NULL);
        if (client && MyUser(client)) {
            udb_nick_apply(client, nick_rec, is_new);
        }
    } else if (block->letter == 'C') {
        UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
        Channel *channel = find_channel(chan_rec->key);
        if (channel) {
            udb_channel_apply_record(channel, chan_rec, rec->key, is_new);
        }
    } else if (block->letter == 'I') {
        UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
        udb_ip_apply_record(ip_rec->key, ip_rec, rec->key, is_new);
    } else if (block->letter == 'K') {
        udb_line_apply_record(rec, is_new);
    }
    return 1;
}

static void udb_remove_special_record(UdbBlock *block, UdbRecord *rec) {
    if (!rec) return;
    if (block->letter == 'N') {
        if (rec->parent != block->tree) {
            UdbRecord *nick_rec = rec->parent;
            Client *client = find_user(nick_rec->key, NULL);
            if (client && MyUser(client)) {
                if (!strcmp(rec->key, NKEY_VHOST)) {
                    udb_nick_remove_vhost(client);
                } else if (!strcmp(rec->key, NKEY_OPER)) {
                    udb_nick_revoke_oper(client);
                } else if (!strcmp(rec->key, NKEY_SWHOIS)) {
                    swhois_delete(client, "udb", "*", &me, NULL);
                } else if (!strcmp(rec->key, NKEY_MODES)) {
                    long old_umodes = client->umodes & ALL_UMODES;
                    UdbRecord *mode_rec = udb_record_find(NKEY_MODES, nick_rec);
                    if (mode_rec && mode_rec->data_str)
                        client->umodes &= ~(set_usermode(mode_rec->data_str) & ~UMODE_OPER);
                    send_umode_out(client, 1, old_umodes);
                } else if (!strcmp(rec->key, NKEY_SNOMASKS)) {
                    set_snomask(client, NULL);
                } else if (!strcmp(rec->key, NKEY_SUSPENDED)) {
                    long old_umodes = client->umodes & ALL_UMODES;
                    client->umodes &= ~set_usermode("S");
                    send_umode_out(client, 1, old_umodes);
                } else if (!strcmp(rec->key, NKEY_PASS)) {
                    udb_nick_strip(client, nick_rec);
                }
            }
        } else {
            Client *client = find_user(rec->key, NULL);
            if (client && MyUser(client)) {
                udb_nick_strip(client, rec);
            }
        }
    } else if (block->letter == 'C') {
        UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
        Channel *channel = find_channel(chan_rec->key);
        if (channel) {
            udb_channel_remove_record(channel, chan_rec, rec->key);
        }
    } else if (block->letter == 'I') {
        UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
        udb_ip_remove_record(ip_rec->key, ip_rec, rec->key);
    } else if (block->letter == 'K') {
        udb_line_remove_record(rec);
    }
}

static void udb_send_to_debugs(Client *source, const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    Client *client;
    list_for_each_entry(client, &client_list, client_node) {
        if (IsServer(client) && client != source) {
            if (udb_ctx && udb_ctx->links) {
                UdbRecord *srv_rec = udb_record_find(client->name, udb_ctx->links);
                if (srv_rec) {
                    UdbRecord *opt_rec = udb_record_find(LKEY_OPTIONS, srv_rec);
                    if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG)) {
                        sendto_one(client, NULL, ":%s NOTICE %s :[UDB Debug] %s", me.id, client->id, buf);
                    }
                }
            }
        }
    }
    
    // Also send to local opers if our own server has the debug option
    if (udb_ctx && udb_ctx->links) {
        UdbRecord *me_rec = udb_record_find(me.name, udb_ctx->links);
        if (me_rec) {
            UdbRecord *opt_rec = udb_record_find(LKEY_OPTIONS, me_rec);
            if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG)) {
                unreal_log(ULOG_INFO, "udb", "UDB_DEBUG_OPER", source, 
                           "[UDB Debug] $msg", log_data_string("msg", buf));
            }
        }
    }
    
    unreal_log(ULOG_DEBUG, "udb", "UDB_DEBUG", source, "[UDB Debug] $msg", log_data_string("msg", buf));
}

/* End of udb_core.c.inc */

/* S2S protocol handler: DB command, server sync */
/* Inlined: udb_protocol.c.inc */
/* UDB - Unreal Database System for UnrealIRCd 6
 * Protocol implementation (S2S DB command and sync)
 */

static ModDataInfo *udb_server_md = NULL;

static const char *udb_md_serialize(ModData *m) {
    return m->i ? "1" : NULL;
}

static void udb_md_unserialize(const char *str, ModData *m) {
    m->i = (str && *str == '1') ? 1 : 0;
}

static int udb_is_udb_server(Client *server) {
    if (!server || !IsServer(server)) return 0;
    return udb_server_md && moddata_client(server, udb_server_md).i;
}

static int udb_is_propagator(Client *server) {
    if (!server || !IsServer(server)) return 0;
    if (udb_cfg && udb_cfg->propagator && !strcasecmp(server->name, udb_cfg->propagator)) {
        udb_ctx->propagator = server;
        return 1;
    }
    return 0;
}

/* Paths are passed to the file parser without the block prefix. */
static UdbBlock *udb_protocol_path_block(const char *path) {
    const char *component;
    const char *separator;
    size_t len;
    UdbBlock *block;

    if (!path)
        return NULL;

    len = strlen(path);
    if (len < 4 || len >= 512 || path[1] != ':' || path[2] != ':' || !path[3])
        return NULL;

    block = udb_block_by_letter(path[0]);
    if (!block)
        return NULL;

    component = path + 3;
    while ((separator = strstr(component, "::"))) {
        if (separator == component || !separator[2])
            return NULL;
        component = separator + 2;
    }

    return block;
}

static void udb_protocol_params_error(Client *client, const char *subcmd) {
    sendto_one(client, NULL, ":%s DB %s ERR %s %d 0", me.id, client->id,
               subcmd ? subcmd : "0", UDB_ERR_PARAMS);
}

static void udb_sync_to_server(Client *server) {
    UdbBlock *block = udb_ctx->block_list;
    while (block) {
        sendto_one(server, NULL, ":%s DB %s INF %c %lX %lu",
                   me.id, server->id, block->letter, block->checksum, (unsigned long)block->modified_at);
        block = block->next;
    }
}

static int udb_hook_server_sync(Client *client) {
    if (udb_is_udb_server(client)) {
        udb_sync_to_server(client);
    }
    return 0;
}

static int udb_hook_server_quit(Client *client, MessageTag *mtags) {
    UdbBlock *block;

    if (!udb_ctx || !client)
        return 0;

    if (udb_ctx->propagator == client)
        udb_ctx->propagator = NULL;

    for (block = udb_ctx->block_list; block; block = block->next) {
        if (block->syncing_from != client)
            continue;

        /* Discard an incomplete in-memory snapshot and restore the last
         * durable block instead of leaving a partially synchronized tree. */
        block->syncing_from = NULL;
        udb_block_reset(block);
        udb_block_load(block);
    }
    return 0;
}

CMD_FUNC(cmd_db) {
    /* Process DB protocol messages sent via server-to-server connection */

    if (parc < 4) {
        sendto_one(client, NULL, ":%s DB %s ERR 0 %i 0", me.id, client->id, UDB_ERR_PARAMS);
        return;
    }

    const char *target = parv[1];
    const char *subcmd = parv[2];

    if (!target || !*target || !subcmd || !*subcmd) {
        udb_protocol_params_error(client, subcmd);
        return;
    }

    if (!udb_is_udb_server(client)) {
        sendto_one(client, NULL, ":%s DB %s ERR %s %d 0", me.id, client->id,
                   subcmd, UDB_ERR_FORBIDDEN);
        return;
    }
    
    char logbuf[512];
    snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
    unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));

    int is_broadcast = !strcmp(target, "*");
    int is_for_me = is_broadcast || !strcmp(target, me.id) || !strcmp(target, me.name);

    switch (toupper((unsigned char)subcmd[0])) {
        case 'I':
            if (!strcasecmp(subcmd, "INF")) {
                if (parc < 6) return;
                char letter = *parv[3];
                UdbBlock *block = udb_block_by_letter(letter);
                
                if (is_for_me) {
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR INF %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    unsigned long crc32 = strtoul(parv[4], NULL, 16);
                    time_t remote_ts = atol(parv[5]);
                    
                    if (crc32 != block->checksum) {
                        if (remote_ts > block->modified_at) {
                            udb_block_reset(block);
                            sendto_one(client, NULL, ":%s DB %s RES %c", me.id, client->id, letter);
                            block->syncing_from = client;
                            block->modified_at = remote_ts;
                        } else if (remote_ts == block->modified_at) {
                            sendto_one(client, NULL, ":%s DB %s RES %c", me.id, client->id, letter);
                            block->syncing_from = client;
                        }
                    }
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s INF %c %s %s", client->id, target, letter, parv[4], parv[5]);
            }
            else if (!strcasecmp(subcmd, "INS")) {
                if (parc < 5) {
                    udb_protocol_params_error(client, subcmd);
                    return;
                }
                const char *path = parv[3];
                const char *data = parv[4];
                UdbBlock *block = udb_protocol_path_block(path);
                char letter = path && *path ? path[0] : '0';

                if (!block) {
                    udb_protocol_params_error(client, subcmd);
                    return;
                }
                
                if (is_for_me) {
                    if (block->syncing_from && block->syncing_from != client) {
                        sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
                        return;
                    }
                    if (block->syncing_from != client && !udb_is_propagator(client)) {
                        sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
                        return;
                    }

                    int len = strlen(path) + strlen(data) + 2;
                    char *line = safe_alloc(len);
                    snprintf(line, len, "%s %s", path + 3, data);
                    UdbRecord *rec = udb_file_parse_line(block, line);
                    safe_free(line);
                    
                    if (rec) {
                        udb_apply_special_record(block, rec, 1);
                    }
                    if (!block->syncing_from)
                        udb_file_save_block(block);
                    
                    char logbuf[512];
                    snprintf(logbuf, sizeof(logbuf), "[UDB] Inserted record via S2S: %s -> %s", path, data);
                    unreal_log(ULOG_INFO, "udb", "UDB_INS_RECEIVED", client, "$msg", log_data_string("msg", logbuf));
                    
                    if (udb_ctx->propagator && block->syncing_from == client) {
                        sendto_server(client, 0, 0, NULL, ":%s DB %s INS %s %s", udb_ctx->propagator->id, target, path, data);
                        return;
                    }
                    
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s INS %s %s", client->id, target, path, data);
            }
            break;

        case 'R':
            if (!strcasecmp(subcmd, "RES")) {
                if (parc < 4) return;
                char letter = *parv[3];
                UdbBlock *block = udb_block_by_letter(letter);
                
                if (is_for_me) {
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    if (block->syncing_from && block->syncing_from != client) {
                        sendto_one(client, NULL, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
                        return;
                    }
                    
                    FILE *fp = fopen(block->filepath, "r");
                    if (fp) {
                        char line[1024];
                        while (fgets(line, sizeof(line), fp)) {
                            size_t len = strlen(line);
                            while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) {
                                line[--len] = '\0';
                            }
                            if (len > 0) {
                                if (strchr(line, ' '))
                                    sendto_one(client, NULL, ":%s DB * INS %c::%s", me.id, letter, line);
                                else
                                    sendto_one(client, NULL, ":%s DB * DEL %c::%s", me.id, letter, line);
                            }
                        }
                        fclose(fp);
                    }
                    sendto_one(client, NULL, ":%s DB %s FDR %c", me.id, client->id, letter);
                    block->syncing_from = NULL;
                    
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s RES %c", client->id, target, letter);
            }
            break;

        case 'D':
            if (!strcasecmp(subcmd, "DEL")) {
                if (parc < 4) {
                    udb_protocol_params_error(client, subcmd);
                    return;
                }
                const char *path = parv[3];
                UdbBlock *block = udb_protocol_path_block(path);
                char letter = path && *path ? path[0] : '0';

                if (!block) {
                    udb_protocol_params_error(client, subcmd);
                    return;
                }
                
                if (is_for_me) {
                    if (block->syncing_from) {
                        if (block->syncing_from != client) {
                            sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
                            return;
                        }
                    } else if (!udb_is_propagator(client)) {
                        sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
                        return;
                    }
                    
                    UdbRecord *rec = udb_record_find_path(block, path + 3);
                    if (rec) {
                        udb_record_delete(block, rec, 1);
                    }
                    
                    if (udb_ctx->propagator && block->syncing_from == client) {
                        sendto_server(client, 0, 0, NULL, ":%s DB %s DEL %s", udb_ctx->propagator->id, target, path);
                        return;
                    }
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s DEL %s", client->id, target, path);
            }
            else if (!strcasecmp(subcmd, "DRP")) {
                if (parc < 4) return;
                char letter = *parv[3];
                
                if (is_for_me) {
                    UdbBlock *block = udb_block_by_letter(letter);
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    if (block->syncing_from && block->syncing_from != client) {
                        sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
                        return;
                    }
                    if (!udb_is_propagator(client)) {
                        sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
                        return;
                    }
                    
                    udb_block_reset(block);
                    block->checksum = 0;
                    block->filesize = 0;
                    if (!block->syncing_from)
                        udb_file_save_block(block);
                    
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s DRP %c", client->id, target, *parv[3]);
            }
            break;

        case 'F':
            if (!strcasecmp(subcmd, "FDR")) {
                if (parc < 4) return;
                char letter = *parv[3];
                
                if (is_for_me) {
                    UdbBlock *block = udb_block_by_letter(letter);
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    if (block->syncing_from != client) {
                        sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_NO_SYNC, letter);
                        return;
                    }
                    
                    udb_file_save_block(block);
                    block->syncing_from = NULL;
                    
                    if (!is_broadcast) return;
                }
                sendto_server(client, 0, 0, NULL, ":%s DB %s FDR %c", client->id, target, *parv[3]);
            }
            break;

        case 'O':
            if (!strcasecmp(subcmd, "OPT")) {
                if (parc < 4) return;
                char letter = *parv[3];
                
                if (is_for_me) {
                    UdbBlock *block = udb_block_by_letter(letter);
                    if (!block) {
                        sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
                        return;
                    }
                    if (!udb_is_propagator(client)) {
                        sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
                        return;
                    }
                    if (parc >= 5) {
                        block->modified_at = atol(parv[4]);
                    }
                    udb_file_save_block(block);
                    
                    if (!is_broadcast) return;
                }
                if (parc >= 5)
                    sendto_server(client, 0, 0, NULL, ":%s DB %s OPT %c %s", client->id, target, *parv[3], parv[4]);
                else
                    sendto_server(client, 0, 0, NULL, ":%s DB %s OPT %c", client->id, target, *parv[3]);
            }
            break;

        case 'E':
            if (!strcasecmp(subcmd, "ERR")) {
                if (parc < 5) return;
                if (is_for_me) {
                    int errcode = atoi(parv[4]);
                    udb_log(ULOG_INFO, "UDB_EVENT", client, "Error from $client: cmd=$cmd err=$errcode",
                               log_data_client("client", client), log_data_string("cmd", parv[3]), log_data_integer("errcode", errcode));
                    if (!is_broadcast) return;
                }
                if (parc >= 6) {
                    sendto_server(client, 0, 0, NULL, ":%s DB %s ERR %s %s %s", client->id, target, parv[3], parv[4], parv[5]);
                } else {
                    sendto_server(client, 0, 0, NULL, ":%s DB %s ERR %s %s", client->id, target, parv[3], parv[4]);
                }
            }
            break;
    }
}

static int udb_protocol_init(ModuleInfo *modinfo) {
    ModDataInfo mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.name = "udb_server";
    mreq.type = MODDATATYPE_CLIENT;
    mreq.serialize = udb_md_serialize;
    mreq.unserialize = udb_md_unserialize;
    mreq.sync = MODDATA_SYNC_NORMAL;
    mreq.self_write = 1;
    udb_server_md = ModDataAdd(modinfo->handle, mreq);

    if (!udb_server_md) {
        return -1; // Failed to register ModData
    }

    // Mark ourselves as a UDB server
    moddata_client((&me), udb_server_md).i = 1;

    CommandAdd(modinfo->handle, "DB", cmd_db, MAXPARA, CMD_SERVER);
    HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, udb_hook_server_sync);
    HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, udb_hook_server_quit);

    return 0;
}

/* End of udb_protocol.c.inc */

/* Nick management: registration, identification, ghost, vhost, oper */
/* Inlined: udb_nicks.c.inc */
#include <openssl/evp.h>

static void udb_nick_set_vhost(Client *client, UdbRecord *vhost_rec) {
    if (!client || !client->user || !vhost_rec || !vhost_rec->data_str) return;
    
    /* If the vhost is already active and set to this exact value, nothing to do */
    if (client->user->virthost && !strcmp(client->user->virthost, vhost_rec->data_str) && IsHidden(client) && IsSetHost(client))
        return;

    userhost_save_current(client);
    safe_strdup(client->user->virthost, vhost_rec->data_str);
    client->umodes |= UMODE_HIDE;
    client->umodes |= UMODE_SETHOST;

    if (IsUser(client)) {
        sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, client->user->virthost);
    }

    if (MyConnect(client)) {
        sendto_one(client, NULL, ":%s MODE %s :+tx", client->name, client->name);
        sendnotice(client, "*** Your vhost is now %s", client->user->virthost);
    }

    userhost_changed(client);
}

static void udb_nick_remove_vhost(Client *client) {
    if (!client || !client->user) return;
    
    userhost_save_current(client);
    
    if (*client->user->cloakedhost) {
        safe_strdup(client->user->virthost, client->user->cloakedhost);
    } else {
        safe_strdup(client->user->virthost, client->user->realhost);
    }
    
    client->umodes &= ~UMODE_SETHOST;
    
    if (IsUser(client)) {
        sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, client->user->virthost);
    }
    if (MyConnect(client)) {
        sendto_one(client, NULL, ":%s MODE %s :-t", client->name, client->name);
        sendnotice(client, "*** Your vhost has been removed");
    }
    
    userhost_changed(client);
}

static void udb_nick_grant_oper(Client *client, UdbRecord *nick_rec, UdbRecord *oper_rec) {
    if (!oper_rec) return;
    
    unsigned long level = oper_rec->data_num;
    const char *operclass = NULL;
    
    if (level & UDB_OPER_ROOT) {
        operclass = "netadmin";
    } else if (level & UDB_OPER_ADMIN) {
        operclass = "admin";
    } else if (level & UDB_OPER_HELPER) {
        operclass = "locop";
    }
    
    if (operclass) {
        if (IsOper(client)) {
            const char *curr_class = get_operclass(client);
            if (curr_class && !strcmp(curr_class, operclass))
                return;
            udb_nick_revoke_oper(client);
        }
        make_oper(client, "UDB", operclass, NULL, UMODE_OPER, NULL, NULL, NULL);
    }
}

static void udb_nick_revoke_oper(Client *client) {
    long old_umodes;

    if (!client || !IsOper(client))
        return;

    old_umodes = client->umodes & ALL_UMODES;
    client->umodes &= ~UMODE_OPER;
    if (MyUser(client) && !list_empty(&client->special_node)) {
        list_del(&client->special_node);
        INIT_LIST_HEAD(&client->special_node);
    }
    if (irccounts.operators > 0)
        irccounts.operators--;
    remove_oper_privileges(client, 0);
    send_umode_out(client, 1, old_umodes);
}

static void udb_nick_set_modes(Client *client, UdbRecord *nick_rec, UdbRecord *mode_rec, const char *modes) {
    if (!modes) return;
    long m = set_usermode(modes);
    long old_umodes = client->umodes & ALL_UMODES;
    /* Oper status is controlled exclusively by N::oper. */
    client->umodes |= m & ~UMODE_OPER;
    send_umode_out(client, 1, old_umodes);
}

static void udb_nick_set_swhois(Client *client, UdbRecord *nick_rec, UdbRecord *swhois_rec) {
    if (!client || !client->user || !swhois_rec || !swhois_rec->data_str) return;
    swhois_delete(client, "udb", "*", &me, NULL);
    swhois_add(client, "udb", 100, swhois_rec->data_str, &me, NULL);
}

static void udb_nick_set_snomasks(Client *client, UdbRecord *nick_rec, UdbRecord *snomask_rec) {
    if (!snomask_rec || !snomask_rec->data_str) return;
    set_snomask(client, snomask_rec->data_str);
}

static void udb_nick_force_rename(Client *client, const char *nick_in_db) {
    char newnick[32];
    char rand_suffix[6];
    
    gen_random_alnum(rand_suffix, 5);
    rand_suffix[5] = '\0';
    snprintf(newnick, sizeof(newnick), "Guest%s", rand_suffix);
    
    sendnotice(client, "This nickname (%s) has been registered or synced in the UDB database.", nick_in_db);
    sendnotice(client, "You have been renamed. If you are the owner, please identify: /NICK %s:Password", nick_in_db);

    const char *args[5];
    char tsbuf[32];
    snprintf(tsbuf, sizeof(tsbuf), "%lld", (long long)TStime());
    args[0] = NULL;
    args[1] = client->name;
    args[2] = newnick;
    args[3] = tsbuf;
    args[4] = NULL;

    do_cmd(&me, NULL, "SVSNICK", 4, args);
}

static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync) {
    if (!client || !nick_rec) return;

    UdbRecord *forbid = udb_record_find(NKEY_FORBID, nick_rec);
    if (forbid) {
        udb_nick_force_rename(client, nick_rec->key);
        return;
    }

    /* If this is a hot sync, check if the user is identified */
    if (is_hot_sync) {
        if (!has_user_mode(client, 'r')) {
            UdbRecord *pass_rec = udb_record_find(NKEY_PASS, nick_rec);
            if (pass_rec) {
                udb_nick_force_rename(client, nick_rec->key);
            }
            return; /* Abort applying vhosts/opers to this unauthorized user */
        }
    }
    
    if (client->user) {
        strlcpy(client->user->account, nick_rec->key, sizeof(client->user->account));
    }
    
    long old_umodes = client->umodes & ALL_UMODES;
    client->umodes |= UMODE_REGNICK;
    
    UdbRecord *susp = udb_record_find(NKEY_SUSPENDED, nick_rec);
    if (susp) {
        client->umodes |= set_usermode("S");
    }
    
    send_umode_out(client, 1, old_umodes);
    
    UdbRecord *vhost_rec = udb_record_find(NKEY_VHOST, nick_rec);
    if (vhost_rec) udb_nick_set_vhost(client, vhost_rec);
    
    UdbRecord *oper_rec = udb_record_find(NKEY_OPER, nick_rec);
    if (oper_rec) udb_nick_grant_oper(client, nick_rec, oper_rec);
    
    UdbRecord *modes_rec = udb_record_find(NKEY_MODES, nick_rec);
    if (modes_rec && modes_rec->data_str) {
        udb_nick_set_modes(client, nick_rec, modes_rec, modes_rec->data_str);
    }
    
    UdbRecord *swhois_rec = udb_record_find(NKEY_SWHOIS, nick_rec);
    if (swhois_rec) udb_nick_set_swhois(client, nick_rec, swhois_rec);
    
    UdbRecord *sno_rec = udb_record_find(NKEY_SNOMASKS, nick_rec);
    if (sno_rec) udb_nick_set_snomasks(client, nick_rec, sno_rec);
}

static void udb_nick_strip(Client *client, UdbRecord *nick_rec) {
    if (!client) return;
    
    if (client->user) {
        strlcpy(client->user->account, "*", sizeof(client->user->account));
    }
    
    udb_nick_revoke_oper(client);
    
    long old_umodes = client->umodes & ALL_UMODES;
    if (nick_rec) {
        UdbRecord *mode_rec = udb_record_find(NKEY_MODES, nick_rec);
        if (mode_rec && mode_rec->data_str)
            client->umodes &= ~(set_usermode(mode_rec->data_str) & ~UMODE_OPER);
    }
    client->umodes &= ~UMODE_REGNICK;
    client->umodes &= ~set_usermode("S");
    send_umode_out(client, 1, old_umodes);

    set_snomask(client, NULL);
    
    udb_nick_remove_vhost(client);
    
    if (nick_rec) {
        UdbRecord *swhois_rec = udb_record_find(NKEY_SWHOIS, nick_rec);
        if (swhois_rec && swhois_rec->data_str) {
            swhois_delete(client, "udb", "*", &me, NULL);
        }
    }
}

static int udb_check_password(const char *pass, UdbRecord *profile_rec, Client *client) {
    UdbRecord *pass_rec = udb_record_find(NKEY_PASS, profile_rec);
    if (!pass_rec || !pass_rec->data_str) return 0;
    
    const char *challenge = "plain";
    UdbRecord *chall_rec = udb_record_find(NKEY_CHALLENGE, profile_rec);
    if (chall_rec && chall_rec->data_str) {
        challenge = chall_rec->data_str;
    } else {
        if (udb_ctx && udb_ctx->settings) {
            UdbRecord *g_chall = udb_record_find(SKEY_CHALLENGE, udb_ctx->settings);
            if (g_chall && g_chall->data_str) challenge = g_chall->data_str;
        }
    }
    
    const char *stored_pass = pass_rec->data_str;
    if (!strncmp(stored_pass, "plain:", 6)) {
        return !strcmp(pass, stored_pass + 6);
    } else if (!strncmp(stored_pass, "sha256:", 7)) {
        challenge = "sha256";
        stored_pass += 7;
    } else if (!strncmp(stored_pass, "md5:", 4)) {
        challenge = "md5";
        stored_pass += 4;
    } else if (!strncmp(stored_pass, "crypt:", 6)) {
        challenge = "crypt";
        stored_pass += 6;
    }

    if (!strcasecmp(challenge, "plain")) {
        return !strcmp(pass, stored_pass);
    } else if (!strcasecmp(challenge, "crypt")) {
        AuthConfig as;
        memset(&as, 0, sizeof(as));
        as.type = AUTHTYPE_UNIXCRYPT;
        as.data = pass_rec->data_str;
        return Auth_Check(client, &as, pass);
    } else if (!strcasecmp(challenge, "md5") || !strcasecmp(challenge, "sha256")) {
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int md_len;
        const EVP_MD *evp_md = EVP_get_digestbyname(challenge);
        if (!evp_md) return 0;
        
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (!ctx) return 0;
        EVP_DigestInit_ex(ctx, evp_md, NULL);
        EVP_DigestUpdate(ctx, pass, strlen(pass));
        EVP_DigestFinal_ex(ctx, md, &md_len);
        EVP_MD_CTX_free(ctx);
        
        char hex[EVP_MAX_MD_SIZE * 2 + 1];
        for (unsigned int i = 0; i < md_len; i++) {
            sprintf(&hex[i*2], "%02x", md[i]);
        }
        return !strcasecmp(stored_pass, hex);
    }
    return 0;
}

CMD_FUNC(cmd_ghost) {
    if (parc < 3) {
        sendnumeric(client, ERR_NEEDMOREPARAMS, "GHOST");
        return;
    }
    const char *target_nick = parv[1];
    const char *pass = parv[2];
    
    if (!udb_ctx || !udb_ctx->nicks) {
        sendnotice(client, "UDB is not fully initialized.");
        return;
    }
    
    UdbRecord *nick_rec = udb_record_find(target_nick, udb_ctx->nicks);
    if (!nick_rec) {
        sendnotice(client, "Nick %s is not registered.", target_nick);
        return;
    }
    
    if (!udb_check_password(pass, nick_rec, client)) {
        sendnotice(client, "Invalid password for %s.", target_nick);
        return;
    }
    
    Client *target = find_client(target_nick, NULL);
    if (target) {
        if (target == client) {
            sendnotice(client, "You cannot ghost yourself.");
            return;
        }
        sendnotice(client, "Ghosting %s...", target_nick);
        exit_client(target, NULL, "GHOST command used");
    } else {
        sendnotice(client, "%s is not online.", target_nick);
    }
}

CMD_OVERRIDE_FUNC(udb_override_nick) {
    if (parc <= 1)
        goto passthrough;

    const char *nick = parv[1];
    char clean_nick[NICKLEN+64];
    strlcpy(clean_nick, nick, sizeof(clean_nick));
    char *pass_colon = strchr(clean_nick, ':');
    char *pass_bang = strchr(clean_nick, '!');
    
    char *pass = NULL;
    int force_ghost = 0;
    
    if (pass_colon && (!pass_bang || pass_colon < pass_bang)) {
        pass = pass_colon;
        force_ghost = 0;
    } else if (pass_bang && (!pass_colon || pass_bang < pass_colon)) {
        pass = pass_bang;
        force_ghost = 1;
    }
    
    if (!pass)
        goto passthrough;

    *pass++ = '\0';
    if (client->local)
        safe_strdup(client->local->passwd, pass);

    UdbRecord *rec = (udb_ctx && udb_ctx->nicks) ? udb_record_find(clean_nick, udb_ctx->nicks) : NULL;
    
    if (rec && udb_check_password(pass, rec, client)) {
        Client *acptr = find_client(clean_nick, NULL);
        if (acptr && acptr != client) {
            if (force_ghost) {
                char quit_msg[128];
                snprintf(quit_msg, sizeof(quit_msg), "Ghosted (Nick taken by %s)", client->name);
                exit_client(acptr, NULL, quit_msg);
            } else {
                sendnotice(client, "This nickname is currently in use. If you are the owner, you can recover it by typing /NICK %s!Password", clean_nick);
            }
        }
    }
    
    const char *new_parv[MAXPARA+1];
    for (int i = 0; i < parc; i++) new_parv[i] = parv[i];
    new_parv[1] = clean_nick;
    CallCommandOverride(ovr, clictx, client, recv_mtags, parc, new_parv);
    return;

passthrough:
    CALL_NEXT_COMMAND_OVERRIDE();
}

static int udb_hook_can_use_nick(Client *client, const char *newnick, const char **reject_reason) {
    if (!udb_ctx || !udb_ctx->nicks) return HOOK_CONTINUE;
    if (!MyConnect(client)) return HOOK_CONTINUE;
    
    UdbRecord *nick_rec = udb_record_find(newnick, udb_ctx->nicks);
    if (nick_rec) {
        UdbRecord *forbid = udb_record_find(NKEY_FORBID, nick_rec);
        if (forbid) {
            *reject_reason = "This nick is forbidden.";
            return HOOK_DENY;
        }
        
        /* If client is already this nick and identified with +r, allow without re-entering password */
        if (!strcasecmp(client->name, newnick) && has_user_mode(client, 'r')) {
            return HOOK_CONTINUE;
        }
        
        const char *pass = client->local ? client->local->passwd : NULL;
        if (pass && udb_check_password(pass, nick_rec, client)) {
            return HOOK_CONTINUE;
        }
        
        static char reject_buf[256];
        snprintf(reject_buf, sizeof(reject_buf), "This nick is registered and requires a password. Use /NICK %s:Password", newnick);
        *reject_reason = reject_buf;
        return HOOK_DENY;
    }
    return HOOK_CONTINUE;
}

static int udb_hook_nick_change(Client *client, MessageTag *mtags, const char *newnick) {
    if (!udb_ctx || !udb_ctx->nicks) return 0;
    if (!MyConnect(client)) return 0;
    
    UdbRecord *old_rec = udb_record_find(client->name, udb_ctx->nicks);
    UdbRecord *new_rec = udb_record_find(newnick, udb_ctx->nicks);
    
    if (old_rec && old_rec != new_rec) {
        udb_nick_strip(client, old_rec);
    }
    
    return 0;
}

static int udb_hook_post_nick_change(Client *client, MessageTag *recv_mtags, const char *oldnick) {
    if (!udb_ctx || !udb_ctx->nicks) return 0;
    if (!MyConnect(client)) return 0;
    
    UdbRecord *new_rec = udb_record_find(client->name, udb_ctx->nicks);
    if (new_rec) {
        udb_nick_apply(client, new_rec, 0);
    }
    return 0;
}

static int udb_hook_local_connect(Client *client) {
    if (!udb_ctx || !udb_ctx->nicks) return 0;
    
    UdbRecord *nick_rec = udb_record_find(client->name, udb_ctx->nicks);
    if (nick_rec) {
        udb_nick_apply(client, nick_rec, 0);
    }
    return 0;
}

int udb_nicks_init(ModuleInfo *modinfo) {
    CommandAdd(modinfo->handle, "GHOST", cmd_ghost, 3, CMD_USER);
    HookAdd(modinfo->handle, HOOKTYPE_CAN_USE_NICK, 0, udb_hook_can_use_nick);
    HookAdd(modinfo->handle, HOOKTYPE_LOCAL_NICKCHANGE, 0, udb_hook_nick_change);
    HookAdd(modinfo->handle, HOOKTYPE_POST_LOCAL_NICKCHANGE, 0, udb_hook_post_nick_change);
    HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, udb_hook_local_connect);
    return MOD_SUCCESS;
}

int udb_nicks_load(ModuleInfo *modinfo) {
    CommandOverrideAdd(modinfo->handle, "NICK", 0, udb_override_nick);
    return 0;
}

/* End of udb_nicks.c.inc */

/* Channel management: registration, founder, modes, topic, access */
/* Inlined: udb_channels.c.inc */
/* UDB Channels Module for UnrealIRCd 6 */

typedef struct UdbPendingChannelAuth UdbPendingChannelAuth;
typedef struct UdbChannelModeState UdbChannelModeState;

struct UdbPendingChannelAuth {
	UdbPendingChannelAuth *next;
	char channel[CHANNELLEN + 1];
};

struct UdbChannelModeState {
	char *value;
};

static ModDataInfo *udb_channel_auth_pending_md = NULL;
static ModDataInfo *udb_channel_auth_member_md = NULL;
static ModDataInfo *udb_channel_modes_md = NULL;

/* Forward declarations */
static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg);
static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key);
static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_pre_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode);
static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic);

static int udb_channel_is_identified_founder(Client *client, UdbRecord *chan_rec)
{
	UdbRecord *founder_rec;

	if (!client || !chan_rec)
		return 0;
	founder_rec = udb_record_find(CKEY_FOUNDER, chan_rec);
	return founder_rec && founder_rec->data_str &&
	       !strcasecmp(client->name, founder_rec->data_str) &&
	       has_user_mode(client, 'r');
}

static void udb_channel_pending_auth_free(ModData *m)
{
	UdbPendingChannelAuth *entry = m->ptr;

	while (entry)
	{
		UdbPendingChannelAuth *next = entry->next;
		safe_free(entry);
		entry = next;
	}
	m->ptr = NULL;
}

static void udb_channel_modes_free(ModData *m)
{
	UdbChannelModeState *state = m->ptr;

	if (state) {
		safe_free(state->value);
		safe_free(state);
	}
	m->ptr = NULL;
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
	set_channel_mode(channel, NULL, modebuf, parabuf ? parabuf : "");
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
	for (src = modebuf; *src && (size_t)(dst - inverse) < sizeof(inverse) - 1; src++) {
		if (*src == '+')
			*dst++ = '-';
		else if (*src == '-')
			*dst++ = '+';
		else
			*dst++ = *src;
	}
	*dst = '\0';
	set_channel_mode(channel, NULL, inverse, parabuf ? parabuf : "");
}

static void udb_channel_apply_modes(Channel *channel, const char *value)
{
	UdbChannelModeState *state;

	if (!udb_channel_modes_md)
		return;
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (!state) {
		state = safe_alloc(sizeof(*state));
		moddata_channel(channel, udb_channel_modes_md).ptr = state;
	}
	if (state->value && !strcmp(state->value, value))
		return;
	udb_channel_reverse_modes(channel, state->value);
	udb_channel_set_modes(channel, value);
	safe_strdup(state->value, value);
}

static void udb_channel_remove_modes(Channel *channel, const char *fallback_value)
{
	UdbChannelModeState *state;
	const char *value = fallback_value;

	if (!udb_channel_modes_md) {
		udb_channel_reverse_modes(channel, fallback_value);
		return;
	}
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (state && state->value)
		value = state->value;
	udb_channel_reverse_modes(channel, value);
	if (state) {
		safe_free(state->value);
		safe_free(state);
		moddata_channel(channel, udb_channel_modes_md).ptr = NULL;
	}
}

static void udb_channel_pending_auth_set(Client *client, Channel *channel)
{
	UdbPendingChannelAuth *entry;

	if (!MyUser(client) || !udb_channel_auth_pending_md)
		return;
	for (entry = moddata_local_client(client, udb_channel_auth_pending_md).ptr;
	     entry; entry = entry->next)
	{
		if (!strcasecmp(entry->channel, channel->name))
			return;
	}
	entry = safe_alloc(sizeof(*entry));
	strlcpy(entry->channel, channel->name, sizeof(entry->channel));
	entry->next = moddata_local_client(client, udb_channel_auth_pending_md).ptr;
	moddata_local_client(client, udb_channel_auth_pending_md).ptr = entry;
}

static int udb_channel_pending_auth_take(Client *client, Channel *channel)
{
	UdbPendingChannelAuth *entry;
	UdbPendingChannelAuth *previous = NULL;

	if (!MyUser(client) || !udb_channel_auth_pending_md)
		return 0;
	entry = moddata_local_client(client, udb_channel_auth_pending_md).ptr;
	while (entry)
	{
		if (!strcasecmp(entry->channel, channel->name))
		{
			if (previous)
				previous->next = entry->next;
			else
				moddata_local_client(client, udb_channel_auth_pending_md).ptr = entry->next;
			safe_free(entry);
			return 1;
		}
		previous = entry;
		entry = entry->next;
	}
	return 0;
}

static void udb_channel_reconcile_founder(Channel *channel, UdbRecord *chan_rec)
{
	Member *member;
	int suspended = chan_rec && udb_record_find(CKEY_SUSPENDED, chan_rec);

	for (member = channel->members; member; member = member->next)
	{
		/* The member's home server emits the network MODE exactly once. */
		if (!MyUser(member->client))
			continue;
		int is_founder = !suspended &&
		                 udb_channel_is_identified_founder(member->client, chan_rec);
		if (is_founder)
		{
			if (!check_channel_access_member(member, "q"))
				set_channel_mode(channel, NULL, "+q", member->client->name);
		}
		else if (check_channel_access_member(member, "q"))
		{
			/* UDB owns founder +q, so a profile replacement has one owner only. */
			set_channel_mode(channel, NULL, "-q", member->client->name);
		}
	}
}

static void udb_channel_revoke_udb_admins(Channel *channel)
{
	Member *member;

	if (!udb_channel_auth_member_md)
		return;
	for (member = channel->members; member; member = member->next)
	{
		if (!moddata_member(member, udb_channel_auth_member_md).i)
			continue;
		if (check_channel_access_member(member, "a"))
			set_channel_mode(channel, NULL, "-a", member->client->name);
		moddata_member(member, udb_channel_auth_member_md).i = 0;
	}
}

static void udb_channel_grant_pending_admin(Client *client, Channel *channel,
	                                             MessageTag *mtags)
{
	Member *member;

	if (!udb_channel_pending_auth_take(client, channel) ||
	    !udb_channel_auth_member_md || !find_channel_mode_handler('a'))
		return;
	member = find_member_link(channel->members, client);
	if (!member || check_channel_access_member(member, "a"))
		return;
	set_channel_mode(channel, mtags, "+a", client->name);
	moddata_member(member, udb_channel_auth_member_md).i = 1;
}

static void udb_channel_clear_topic(Channel *channel)
{
	safe_free(channel->topic);
	safe_free(channel->topic_nick);
	channel->topic_time = 0;
	if (channel->users > 0)
	{
		sendto_channel(channel, &me, NULL, 0, 0, SEND_LOCAL, NULL,
		               ":%s TOPIC %s :", me.name, channel->name);
	}
}

static void udb_channel_apply_record(Channel *channel, UdbRecord *chan_rec, const char *subkey, int is_new)
{
    UdbRecord *sub_rec = udb_record_find(subkey, chan_rec);
    if (!sub_rec || !sub_rec->data_str) return;

    if (!strcmp(subkey, CKEY_FOUNDER)) {
        udb_channel_reconcile_founder(channel, chan_rec);
    } else if (!strcmp(subkey, CKEY_MODES)) {
        udb_channel_apply_modes(channel, sub_rec->data_str);
    } else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE)) {
        udb_channel_revoke_udb_admins(channel);
    } else if (!strcmp(subkey, CKEY_TOPIC)) {
        if (!channel->topic || strcmp(channel->topic, sub_rec->data_str)) {
            safe_strdup(channel->topic, sub_rec->data_str);
            channel->topic_time = TStime();
            safe_strdup(channel->topic_nick, udb_get_bot_nick(SKEY_CHANSERV, 0));
            if (channel->users > 0) {
                sendto_channel(channel, &me, NULL, 0, 0, SEND_LOCAL, NULL,
                               ":%s TOPIC %s :%s", me.name, channel->name, channel->topic);
            }
        }
    }
}

static void udb_channel_remove_record(Channel *channel, UdbRecord *chan_rec, const char *subkey)
{
    if (!strcmp(subkey, CKEY_FOUNDER)) {
        udb_channel_reconcile_founder(channel, NULL);
    } else if (!strcmp(subkey, CKEY_MODES)) {
        UdbRecord *mode_rec = udb_record_find(CKEY_MODES, chan_rec);
        udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
    } else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE)) {
        udb_channel_revoke_udb_admins(channel);
    } else if (!strcmp(subkey, CKEY_TOPIC)) {
        udb_channel_clear_topic(channel);
    } else if (!strcasecmp(subkey, chan_rec->key)) {
        UdbRecord *mode_rec = udb_record_find(CKEY_MODES, chan_rec);
        udb_channel_reconcile_founder(channel, NULL);
        udb_channel_revoke_udb_admins(channel);
        udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
        if (udb_record_find(CKEY_TOPIC, chan_rec))
            udb_channel_clear_topic(channel);
    }
}

static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key)
{
    UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
    UdbRecord *pass_rec;
    if (!chan_rec) return HOOK_CONTINUE;

    UdbRecord *forbid_rec = udb_record_find(CKEY_FORBID, chan_rec);
    if (forbid_rec) {
        return HOOK_CONTINUE; /* Let can_join handle the reject with proper numeric */
    }

    if (udb_channel_is_identified_founder(client, chan_rec))
        return HOOK_ALLOW; /* Bypass bans/keys/invite */

    /* CAN_JOIN already verified this credential. Record it only after all
     * regular join checks have succeeded, immediately before membership. */
    pass_rec = udb_record_find(CKEY_PASS, chan_rec);
    if (pass_rec && pass_rec->data_str && *pass_rec->data_str && key &&
        udb_check_password(key, chan_rec, client))
        udb_channel_pending_auth_set(client, channel);
    return HOOK_CONTINUE;
}

static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg)
{
    static char errbuf[512];
    UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
    if (!chan_rec) return 0;

    UdbRecord *forbid_rec = udb_record_find(CKEY_FORBID, chan_rec);
    if (forbid_rec) {
        snprintf(errbuf, sizeof(errbuf), "%%s :%s", forbid_rec->data_str ? forbid_rec->data_str : "Channel is forbidden");
        *errmsg = errbuf;
        return ERR_FORBIDDENCHANNEL;
    }

    int is_founder = udb_channel_is_identified_founder(client, chan_rec);

    UdbRecord *pass_rec = udb_record_find(CKEY_PASS, chan_rec);
    if (pass_rec && pass_rec->data_str && *pass_rec->data_str && !is_founder) {
        if (!key || !udb_check_password(key, chan_rec, client)) {
            *errmsg = STR_ERR_BADCHANNELKEY;
            return ERR_BADCHANNELKEY;
        }
    }

    UdbRecord *access_rec = udb_record_find(CKEY_ACCESS, chan_rec);
    if (access_rec && !is_founder) {
        UdbRecord *acc_entry = udb_record_find(client->name, access_rec);
        if (!acc_entry || !has_user_mode(client, 'r')) {
            snprintf(errbuf, sizeof(errbuf), "%%s :You must be authorized and identified to join this channel");
            *errmsg = errbuf;
            return ERR_FORBIDDENCHANNEL;
        }
    }
    return 0;
}

static void handle_join(Client *client, Channel *channel, MessageTag *mtags)
{
    UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
    if (!chan_rec) return;

    int is_founder = udb_channel_is_identified_founder(client, chan_rec);

    if (channel->users == 1) {
        UdbRecord *susp_rec = udb_record_find(CKEY_SUSPENDED, chan_rec);
        
        /* A registered channel assigns founder authority exclusively as +q. */
        if (!IsServer(client) && !IsULine(client)) {
            set_channel_mode(channel, mtags, "-o", client->name);
        }
        
        if (!susp_rec) {
            set_channel_mode(channel, mtags, "+r", "");
        }

        udb_channel_apply_record(channel, chan_rec, CKEY_MODES, 0);
        udb_channel_apply_record(channel, chan_rec, CKEY_TOPIC, 0);
    }
    
    /* Founder +q is UDB-owned and must have exactly one current holder. */
    udb_channel_reconcile_founder(channel, chan_rec);
}

static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags)
{
    handle_join(client, channel, mtags);
    udb_channel_grant_pending_admin(client, channel, mtags);
    return 0;
}

static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags)
{
    handle_join(client, channel, mtags);
    return 0;
}

static int udb_hook_pre_command(Client *client, MessageTag *mtags, const char *buf)
{
    if (IsServer(client)) return HOOK_CONTINUE;

    if (strncasecmp(buf, "MODE ", 5) && strncasecmp(buf, "SAMODE ", 7))
        return HOOK_CONTINUE;

    const char *p = strchr(buf, '#');
    if (!p) return HOOK_CONTINUE;

    const char *space_after = strchr(p, ' ');
    if (!space_after) return HOOK_CONTINUE;

    char chan_name[64];
    int i = 0;
    while (p[i] && p[i] != ' ' && i < 63) {
        chan_name[i] = p[i];
        i++;
    }
    chan_name[i] = '\0';

    UdbRecord *chan_rec = udb_record_find(chan_name, udb_ctx->channels);
    if (!chan_rec) return HOOK_CONTINUE;

    UdbRecord *opt_rec = udb_record_find(CKEY_OPTIONS, chan_rec);
    if (!opt_rec || !(opt_rec->data_num & UDB_CHOPT_LOCK_MODES))
        return HOOK_CONTINUE;

    int is_founder = udb_channel_is_identified_founder(client, chan_rec);
    
    if (!is_founder) {
        sendnotice(client, "You do not have permission to change modes in %s (locked by UDB)", chan_name);
        return HOOK_DENY;
    }

    return HOOK_CONTINUE;
}

static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic)
{
    if (IsServer(client)) return topic;

    UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
    if (!chan_rec) return topic;

    UdbRecord *topic_rec = udb_record_find(CKEY_TOPIC, chan_rec);
    if (topic_rec) {
        int is_founder = udb_channel_is_identified_founder(client, chan_rec);
        if (!is_founder) {
            sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
            return NULL;
        }
    }
    return topic;
}

static void udb_channels_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_auth_pending";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_channel_pending_auth_free;
	udb_channel_auth_pending_md = ModDataAdd(modinfo->handle, mreq);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_auth_admin";
	mreq.type = MODDATATYPE_MEMBER;
	udb_channel_auth_member_md = ModDataAdd(modinfo->handle, mreq);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_modes";
	mreq.type = MODDATATYPE_CHANNEL;
	mreq.free = udb_channel_modes_free;
	udb_channel_modes_md = ModDataAdd(modinfo->handle, mreq);

    HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, udb_hook_can_join);
    HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, udb_hook_pre_local_join);
    HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, udb_hook_local_join);
    HookAdd(modinfo->handle, HOOKTYPE_REMOTE_JOIN, 0, udb_hook_remote_join);
    HookAdd(modinfo->handle, HOOKTYPE_PRE_COMMAND, 0, udb_hook_pre_command);
    HookAddConstString(modinfo->handle, HOOKTYPE_PRE_LOCAL_TOPIC, 0, udb_hook_pre_topic);
}

/* End of udb_channels.c.inc */

/* IP management: clones, nolines, host overrides */
/* Inlined: udb_ips.c.inc */
/* udb_ips.inc.c
 * Implements IP and host tracking and restrictions for UDB.
 */

static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey, int is_new)
{
	if (!strcmp(subkey, IKEY_NOLINES)) {
		if (ip_rec->data_str) {
			tkl_add_banexception(TKL_EXCEPTION, "*", ip_key, NULL,
			                     "UDB Nolines Exemption", "UDB", 0, TStime(), 0,
			                     ip_rec->data_str, 0);
		}
	}
}

static void udb_ip_remove_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey)
{
	if (!strcmp(subkey, IKEY_NOLINES)) {
		TKL *tkl;
		if ((tkl = find_tkl_banexception(TKL_EXCEPTION, "*", ip_key, 0))) {
			tkl_del_line(tkl);
		}
	}
}

static int udb_hook_pre_connect(Client *client)
{
	UdbRecord *ip_rec;
	UdbRecord *sub_rec;
	int limit = 0;

	if (!client || !client->ip || !client->local)
		return 0;

	/* Lookup IP or Host in UDB */
	ip_rec = udb_hash_find(udb_block_letter_to_index('I'), client->ip);
	if (!ip_rec && client->user)
		ip_rec = udb_hash_find(udb_block_letter_to_index('I'), client->user->realhost);

	if (ip_rec) {
		/* Apply Clone Limit */
		sub_rec = udb_record_find(IKEY_CLONES, ip_rec);
		if (sub_rec && sub_rec->data_num > 0)
			limit = (int)sub_rec->data_num;
		
		/* Apply Host Override */
		sub_rec = udb_record_find(IKEY_HOST, ip_rec);
		if (sub_rec && sub_rec->data_str && client->user) {
			strlcpy(client->user->realhost, sub_rec->data_str, sizeof(client->user->realhost));
			strlcpy(client->user->cloakedhost, sub_rec->data_str, sizeof(client->user->cloakedhost));
			safe_strdup(client->user->virthost, sub_rec->data_str);
		}
	}

	/* Fallback to global clones if no specific IP limit */
	if (limit == 0 && udb_ctx && udb_ctx->settings) {
		UdbRecord *g_clones = udb_record_find(SKEY_CLONES, udb_ctx->settings);
		if (g_clones && g_clones->data_num > 0)
			limit = (int)g_clones->data_num;
	}

	if (limit <= 0)
		return 0;

	int clone_count = 0;
	Client *c;
	list_for_each_entry(c, &lclient_list, lclient_node) {
		if (c->ip && !strcmp(c->ip, client->ip))
			clone_count++;
	}

	if (clone_count < limit)
		return 0;

	const char *quit_msg = "Too many connections from your IP";
	if (udb_ctx && udb_ctx->settings) {
		UdbRecord *qmsg = udb_record_find("quit_clones", udb_ctx->settings);
		if (qmsg && qmsg->data_str)
			quit_msg = qmsg->data_str;
	}

	udb_log(ULOG_INFO, "UDB_CLONES", client,
	        "Rejecting $client.ip (Exceeds UDB clone limit of $limit)",
	        log_data_integer("limit", limit));
	exit_client(client, NULL, quit_msg);
	return HOOK_DENY;
}

static void udb_ips_init(ModuleInfo *modinfo)
{
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, udb_hook_pre_connect);
}

/* End of udb_ips.c.inc */

/* Distributed *lines: glines, zlines, shuns, qlines, spamfilters */
/* Inlined: udb_lines.c.inc */
/* udb_lines.inc.c
 * Implements K-line, Z-line, Shun, Q-line, and Spamfilter support for UDB.
 */

static void udb_line_apply_record(UdbRecord *line_rec, int is_new)
{
	char type;
	UdbRecord *raz;

	if (!line_rec || !line_rec->parent || !line_rec->parent->key)
		return;

	type = line_rec->parent->key[0];
	
	const char *reason = NULL;
	raz = udb_record_find(KKEY_REASON, line_rec);
	if (raz && raz->data_str) {
		reason = raz->data_str;
	} else if (line_rec->data_str) {
		reason = line_rec->data_str;
	}
	
	if (!reason)
		return;

	if (type == 'F') {
		UdbRecord *tip = udb_record_find(KKEY_TYPE, line_rec);
		UdbRecord *acc = udb_record_find(KKEY_ACTION, line_rec);
		UdbRecord *dur = udb_record_find(KKEY_DURATION, line_rec);
		
		if (tip && acc && tip->data_str && acc->data_str) {
			int target = spamfilter_getconftargets(tip->data_str);
			BanActionValue act_val = banact_stringtoval(acc->data_str);
			BanAction *action = banact_value_to_struct(act_val);
			long duration = dur ? dur->data_num : 0;
			
			const char *err = NULL;
			Match *match = unreal_create_match(MATCH_PCRE_REGEX, line_rec->key, &err);
			if (match) {
				tkl_add_spamfilter(TKL_SPAMF|TKL_GLOBAL, line_rec->key, target, action, match,
				                   line_rec->key, NULL, "UDB", 0, TStime(),
				                   duration, reason, 0, 0, 0);
			} else {
				udb_log(ULOG_ERROR, "UDB_SPAMF_ERROR", NULL, "Failed to compile spamfilter regex: $regex ($err)",
				        log_data_string("regex", line_rec->key),
				        log_data_string("err", err ? err : "unknown error"), NULL);
			}
		}
	} else {
		char user[128];
		char host[128];
		const char *p = strchr(line_rec->key, '@');
		if (p) {
			strlcpy(user, line_rec->key, p - line_rec->key + 1);
			strlcpy(host, p + 1, sizeof(host));
		} else {
			strlcpy(user, "*", sizeof(user));
			strlcpy(host, line_rec->key, sizeof(host));
		}

		if (type == 'G') {
			tkl_add_serverban(TKL_KILL|TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", 0, TStime(), 0, 0);
		} else if (type == 'Z') {
			tkl_add_serverban(TKL_ZAP|TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", 0, TStime(), 0, 0);
		} else if (type == 'S') {
			tkl_add_serverban(TKL_SHUN|TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", 0, TStime(), 0, 0);
		} else if (type == 'Q') {
			tkl_add_nameban(TKL_NAME|TKL_GLOBAL, line_rec->key, 0, reason, "UDB",
			                0, TStime(), 0);
		}
	}
}

static void udb_line_remove_record(UdbRecord *line_rec)
{
	char type;
	TKL *tkl = NULL;

	if (!line_rec || !line_rec->parent || !line_rec->parent->key)
		return;

	type = line_rec->parent->key[0];

	if (type == 'F') {
		UdbRecord *tip = udb_record_find(KKEY_TYPE, line_rec);
		UdbRecord *acc = udb_record_find(KKEY_ACTION, line_rec);
		if (tip && acc && tip->data_str && acc->data_str) {
			int target = spamfilter_getconftargets(tip->data_str);
			BanActionValue act_val = banact_stringtoval(acc->data_str);
			tkl = find_tkl_spamfilter(TKL_SPAMF|TKL_GLOBAL, line_rec->key, act_val, target);
		}
	} else {
		char user[128];
		char host[128];
		const char *p = strchr(line_rec->key, '@');
		if (p) {
			strlcpy(user, line_rec->key, p - line_rec->key + 1);
			strlcpy(host, p + 1, sizeof(host));
		} else {
			strlcpy(user, "*", sizeof(user));
			strlcpy(host, line_rec->key, sizeof(host));
		}

		if (type == 'G') {
			tkl = find_tkl_serverban(TKL_KILL|TKL_GLOBAL, user, host, 0);
		} else if (type == 'Z') {
			tkl = find_tkl_serverban(TKL_ZAP|TKL_GLOBAL, user, host, 0);
		} else if (type == 'S') {
			tkl = find_tkl_serverban(TKL_SHUN|TKL_GLOBAL, user, host, 0);
		} else if (type == 'Q') {
			tkl = find_tkl_nameban(TKL_NAME|TKL_GLOBAL, line_rec->key, 0);
		}
	}

	if (tkl) {
		if (type == 'S') {
			tkl_check_local_remove_shun(tkl);
		}
		tkl_del_line(tkl);
	}
}

static void udb_lines_init(ModuleInfo *modinfo)
{
	/* TKL system handles network bans automatically, no hooks required here */
}

/* End of udb_lines.c.inc */

/* DBQ query command for users and opers */
/* Inlined: udb_query.c.inc */
/* udb_query.inc.c
 * Implements the DBQ command to query the database manually.
 */

CMD_FUNC(cmd_dbq)
{
	char *query_str = NULL;
	char *cur, *ds;
	UdbBlock *block;
	UdbRecord *rec;

	if (!IsUser(client) && !IsServer(client))
		return;

	if (IsUser(client) && !IsOper(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if (parc < 2) {
		sendto_one(client, NULL, ":%s 339 %s :Insufficient parameters. Syntax: /DBQ [server] <block>[::path]",
		           me.name, client->name);
		return;
	}

	if (parc >= 3) {
		if (!match_simple(parv[1], me.name)) {
			Client *target_server = find_server_quick(parv[1]);
			if (target_server)
				sendto_one(target_server, NULL, ":%s DBQ %s %s", client->id, parv[1], parv[2]);
			else
				sendnumeric(client, ERR_NOSUCHSERVER, parv[1]);
			return;
		}
		safe_strdup(query_str, parv[2]);
	} else {
		safe_strdup(query_str, parv[1]);
	}

	block = udb_block_by_letter(query_str[0]);
	if (!block) {
		sendto_one(client, NULL, ":%s 339 %s :Block %c does not exist.",
		           me.name, client->name, query_str[0]);
		safe_free(query_str);
		return;
	}

	/* Query for block summary only (e.g. "/DBQ N") */
	if (query_str[1] == '\0') {
		sendto_one(client, NULL, ":%s 339 %s :%c %u %lu %lu %lX %s",
		           me.name, client->name,
		           block->letter, block->record_count, block->filesize,
		           (unsigned long)block->modified_at, block->checksum,
		           block->syncing_from ? "*" : "");
		safe_free(query_str);
		return;
	}

	/* Parse path (e.g. "N::davidlig::vhost") */
	cur = query_str + 1;
	if (cur[0] != ':' || cur[1] != ':' || cur[2] == '\0') {
		sendto_one(client, NULL, ":%s 339 %s :Invalid block format.",
		           me.name, client->name);
		safe_free(query_str);
		return;
	}
	cur += 2;

	rec = block->tree;
	while ((ds = strstr(cur, "::"))) {
		*ds = '\0';
		rec = udb_record_find(cur, rec);
		if (!rec) goto notfound;
		cur = ds + 2;
	}
	rec = udb_record_find(cur, rec);

	if (!rec) {
notfound:
		sendto_one(client, NULL, ":%s 339 %s :Block not found: %s",
		           me.name, client->name, query_str);
		safe_free(query_str);
		return;
	}

	/* Display the found record */
	if (rec->data_str) {
		sendto_one(client, NULL, ":%s 339 %s :DBQ %s %s",
		           me.name, client->name, query_str, rec->data_str);
	} else if (rec->data_num) {
		sendto_one(client, NULL, ":%s 339 %s :DBQ %s %lu",
		           me.name, client->name, query_str, rec->data_num);
	} else {
		UdbRecord *child;
		for (child = rec->child; child; child = child->sibling) {
			if (child->data_str)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s %s",
				           me.name, client->name, query_str, child->key, child->data_str);
			else if (child->data_num)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s %lu",
				           me.name, client->name, query_str, child->key, child->data_num);
			else if (child->child)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s (has sub-records)",
				           me.name, client->name, query_str, child->key);
			else
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s (empty)",
				           me.name, client->name, query_str, child->key);
		}
	}

	safe_free(query_str);
}

static void udb_query_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DBQ", cmd_dbq, MAXPARA, CMD_USER|CMD_SERVER);
}

/* End of udb_query.c.inc */

/* ========================================================================
 * Configuration Test (MOD_TEST)
 *
 * Validates the udb { } configuration block at config load time.
 * ======================================================================== */

static int udb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
static int udb_config_run(ConfigFile *cf, ConfigEntry *ce, int type);
static int udb_config_posttest(int *errs);

MOD_TEST()
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, udb_config_test);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, udb_config_posttest);
	return MOD_SUCCESS;
}

/* ========================================================================
 * Module Initialization (MOD_INIT)
 *
 * Registers all commands, hooks, ModData, and initializes the DB engine.
 * ======================================================================== */

MOD_INIT()
{
	/* Configuration */
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, udb_config_run);

	/* Initialize the database engine */
	if (udb_engine_init() == 0)
	{
		config_error("[UDB] Failed to initialize database engine");
		return MOD_FAILED;
	}

	/* Register subsystem hooks and commands */
	udb_protocol_init(modinfo);
	udb_nicks_init(modinfo);
	udb_channels_init(modinfo);
	udb_ips_init(modinfo);
	udb_lines_init(modinfo);
	udb_query_init(modinfo);

	/* Mark as global: all servers in the network should load this module */
	MARK_AS_GLOBAL_MODULE(modinfo);

	return MOD_SUCCESS;
}

/* ========================================================================
 * Module Load (MOD_LOAD)
 *
 * Called after all modules are initialized. Load database files.
 * ======================================================================== */

MOD_LOAD()
{
	udb_blocks_load_all();
	udb_nicks_load(modinfo);

	unreal_log(ULOG_INFO, "udb", "UDB_LOADED", NULL,
	           "[UDB] Unreal Database System v" UDB_VERSION " loaded successfully");

	return MOD_SUCCESS;
}

/* ========================================================================
 * Module Unload (MOD_UNLOAD)
 *
 * Save all data and free resources.
 * ======================================================================== */

MOD_UNLOAD()
{
	unreal_log(ULOG_INFO, "udb", "UDB_UNLOADING", NULL,
	           "[UDB] Saving databases and shutting down...");

	/* Save all blocks to disk before unloading */
	udb_blocks_save_all();

	/* Free all memory */
	udb_engine_shutdown();

	return MOD_SUCCESS;
}

/* ========================================================================
 * Configuration: udb { } block
 *
 * Example configuration:
 *
 *   udb {
 *       database-directory "data/udb";
 *       propagator "services.mynetwork.org";
 *       max-global-clones 3;
 *       password-flood 5:30;
 *   };
 * ======================================================================== */

static int udb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	ConfigEntry *cep;

	/* We only handle CONFIG_MAIN blocks named "udb" */
	if (type != CONFIG_MAIN)
		return 0;
	if (!ce || !ce->name || strcmp(ce->name, "udb"))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!strcmp(cep->name, "database-directory"))
		{
			if (!cep->value || !*cep->value)
			{
				config_error("%s:%i: udb::database-directory requires a value",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "propagator"))
		{
			if (!cep->value || !*cep->value)
			{
				config_error("%s:%i: udb::propagator requires a server name",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "max-global-clones"))
		{
			if (!cep->value || atoi(cep->value) < 0)
			{
				config_error("%s:%i: udb::max-global-clones requires a non-negative integer",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "password-flood"))
		{
			if (!cep->value || !strchr(cep->value, ':'))
			{
				config_error("%s:%i: udb::password-flood requires format attempts:seconds (e.g. 5:30)",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		}
		else
		{
			config_error("%s:%i: unknown directive udb::%s",
			             cep->file->filename, cep->line_number, cep->name);
			errors++;
		}
	}

	*errs = errors;
	return errors ? -1 : 1;
}

static int udb_config_posttest(int *errs)
{
	/* Could validate that propagator is a known link, etc. */
	return 0;
}

static int udb_config_run(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if (type != CONFIG_MAIN)
		return 0;
	if (!ce || !ce->name || strcmp(ce->name, "udb"))
		return 0;

	/* Allocate config if needed */
	if (!udb_cfg)
		udb_cfg = safe_alloc(sizeof(UdbConfig));

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!strcmp(cep->name, "database-directory"))
		{
			safe_strdup(udb_cfg->db_directory, cep->value);
		}
		else if (!strcmp(cep->name, "propagator"))
		{
			safe_strdup(udb_cfg->propagator, cep->value);
		}
		else if (!strcmp(cep->name, "max-global-clones"))
		{
			udb_cfg->max_global_clones = atoi(cep->value);
		}
		else if (!strcmp(cep->name, "password-flood"))
		{
			const char *colon = strchr(cep->value, ':');
			if (colon)
			{
				udb_cfg->flood_attempts = atoi(cep->value);
				udb_cfg->flood_period = atoi(colon + 1);
			}
		}
	}

	/* Set defaults if not configured */
	if (!udb_cfg->db_directory)
		safe_strdup(udb_cfg->db_directory, UDB_DB_SUBDIR);
	if (udb_cfg->flood_attempts == 0)
		udb_cfg->flood_attempts = 5;
	if (udb_cfg->flood_period == 0)
		udb_cfg->flood_period = 60;

	return 1;
}