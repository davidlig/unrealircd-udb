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

#define UDB_VERSION "4.0.0"

/* ========================================================================
 * Block Identifiers
 * ======================================================================== */
#define UDB_BLOCK_NICKS 'N'
#define UDB_BLOCK_CHANNELS 'C'
#define UDB_BLOCK_IPS 'I'
#define UDB_BLOCK_SETTINGS 'S'
#define UDB_BLOCK_LINKS 'L'
#define UDB_BLOCK_LINES 'K'

#define UDB_NUM_BLOCKS 6

/* ========================================================================
 * Sub-record Keys
 * ======================================================================== */

/* Nick sub-records: N::<nick>::<key> <value> */
#define NKEY_ACCESS "access"	   /* IP/CIDR access restriction */
#define NKEY_PASS "pass"		   /* Password hash */
#define NKEY_VHOST "vhost"		   /* Virtual host */
#define NKEY_FORBID "forbid"	   /* Forbidden nick (value = reason) */
#define NKEY_SUSPENDED "suspended" /* Suspended nick (value = reason) */
#define NKEY_OPER "oper"		   /* Operclass name string (e.g. "locop", "netadmin-with-override") */
#define NKEY_CHALLENGE "challenge" /* Password hash method */
#define NKEY_MODES "modes"		   /* Allowed oper modes */
#define NKEY_SNOMASKS "snomasks"   /* Allowed snomasks */
#define NKEY_SWHOIS "swhois"	   /* Custom SWHOIS line */

/* Channel sub-records: C::<#chan>::<key> <value> */
#define CKEY_FOUNDER "founder"	   /* Founder nick */
#define CKEY_MODES "modes"		   /* Locked channel modes */
#define CKEY_TOPIC "topic"		   /* Persistent topic */
#define CKEY_ACCESS "access"	   /* Access list (has sub-records per nick) */
#define CKEY_FORBID "forbid"	   /* Forbidden channel (value = reason) */
#define CKEY_SUSPENDED "suspended" /* Suspended channel */
#define CKEY_PASS "pass"		   /* Channel password for +ao */
#define CKEY_CHALLENGE "challenge" /* Channel password hash method */
#define CKEY_OPTIONS "options"	   /* Channel option flags (*N) */

/* IP sub-records: I::<ip|host>::<key> <value> */
#define IKEY_CLONES "clones"   /* Max clones allowed (*N) */
#define IKEY_NOLINES "nolines" /* Ban exception types (eg. GZQSTmc) */
#define IKEY_HOST "host"	   /* Reverse DNS override */

/* Settings sub-records: S::<key> <value> */
#define SKEY_CRYPT_KEY "encryption_key" /* Host cloaking key */
#define SKEY_SUFFIX "suffix"			/* Virtual host suffix */
#define SKEY_NICKSERV "nickserv"		/* NickServ bot mask */
#define SKEY_CHANSERV "chanserv"		/* ChanServ bot mask */
#define SKEY_IPSERV "ipserv"			/* IpServ bot mask */
#define SKEY_CLONES "clones"			/* Global max clones (*N) */
#define SKEY_QUIT_IPS "quit_ips"		/* Quit message for IP limit */
#define SKEY_QUIT_CLONES "quit_clones"	/* Quit message for clone limit */
#define SKEY_FLOOD "flood"				/* Password flood limit V:S */
#define SKEY_PROPAGATOR "propagator"	/* Cluster authoritative propagator(s) */

/* Link sub-records: L::<server>::<key> <value> */
#define LKEY_OPTIONS "options" /* Link option flags (*N) */

/* Line sub-records: K::<type>::<pattern>::<key> <value> */
#define KKEY_TYPE "type"		 /* Spamfilter target type */
#define KKEY_ACTION "action"	 /* Spamfilter action */
#define KKEY_DURATION "duration" /* TKL duration */
#define KKEY_REASON "reason"	 /* Ban reason */

/* Spamfilter pattern encoding: K::F::b64:<RFC 4648 base64>::... */
#define UDB_SPAMFILTER_B64_PREFIX "b64:"
#define UDB_SPAMFILTER_PATTERN_MAX 3072

/* ========================================================================
 * Error Codes (for DB ERR protocol messages)
 * ======================================================================== */
#define UDB_ERR_NO_BLOCK 1	  /* Block does not exist */
#define UDB_ERR_PARAMS 2	  /* Missing parameters */
#define UDB_ERR_FATAL 3		  /* Fatal / internal error */
#define UDB_ERR_SYNC_ACTIVE 4 /* Sync already in progress */
#define UDB_ERR_NO_SYNC 5	  /* No sync was requested */
#define UDB_ERR_FORBIDDEN 6	  /* Forbidden server */

/* SHA-256 is deliberately handled by UDB, not Auth_Check(). */
#define UDB_AUTHTYPE_SHA256 1001

/* ========================================================================
 * Channel Option Flags (bitmask in C::<#chan>::options *<value>)
 * ======================================================================== */
#define UDB_CHOPT_PROTECT_BANS 0x1 /* Only ban author can remove their bans */
#define UDB_CHOPT_LOCK_MODES 0x2   /* Channel modes are locked */
#define UDB_CHOPT_LOCK_TOPIC 0x4   /* Channel topic is locked */
#define UDB_CHOPT_PERSISTENT 0x8   /* Keep the channel alive through native +P */

/* ========================================================================
 * Link Option Flags (bitmask in L::<server>::options *<value>)
 * ======================================================================== */
#define UDB_LNKOPT_DEBUG 0x1 /* Debug: receives all UDB mode changes */

#endif /* UDB_H */

#include "unrealircd.h"
#include <errno.h>
#include <openssl/hmac.h>

#define UDB_DEFAULT_DB_DIRECTORY PERMDATADIR
#define UDB_BLOCK_PATH_MAX 1024
#define UDB_RECORD_PATH_MAX 8192
#define UDB_COMPONENT_RAW_MAX 4608
#define UDB_COMPONENT_ENCODED_MAX 4608
#define UDB_COMPONENT_MAX UDB_COMPONENT_ENCODED_MAX
#define UDB_RECORD_VALUE_MAX 4096
#define UDB_RECORD_LINE_MAX (UDB_RECORD_PATH_MAX + UDB_RECORD_VALUE_MAX + 32)
#define UDB_S2S_LINE_MAX MAXLINELENGTH
#define UDB_S2S_OVERHEAD_MAX 256
#define UDB_TXID_MAX 31
#define UDB_SYNC_INACTIVITY_TIMEOUT 60
#define UDB_SYNC_ABSOLUTE_TIMEOUT 300
#define UDB_SYNC_TIMEOUT UDB_SYNC_INACTIVITY_TIMEOUT
#define UDB_DEFAULT_MAX_STAGED_BYTES (64 * 1024 * 1024) /* 64 MB */
#define UDB_MIN_MAX_STAGED_BYTES 1024
#define UDB_MAX_MAX_STAGED_BYTES (1024ULL * 1024 * 1024)
#define UDB_DEFAULT_MAX_STAGED_RECORDS 500000
#define UDB_MIN_MAX_STAGED_RECORDS 1
#define UDB_MAX_MAX_STAGED_RECORDS 10000000
#define UDB_HASH_SIZE 2048
#define UDB_HASH_MASK (UDB_HASH_SIZE - 1)
#define UDB_PASSWORD_FAILURE_SLOTS 256

typedef struct UdbRecord UdbRecord;
typedef struct UdbBlock UdbBlock;
typedef struct UdbSyncSession UdbSyncSession;

typedef enum UdbValueType
{
	UDB_VAL_NONE = 0, /* No value allowed (container node) */
	UDB_VAL_STRING,	  /* Must be a string (no '*' prefix) */
	UDB_VAL_NUMERIC,  /* Must be a numeric string ('*' prefix followed by digits) */
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

struct UdbRecord
{
	char *key;
	unsigned int id;
	char *data_str;
	unsigned long data_num;
	UdbRecord *hash_next;
	UdbRecord *parent;
	UdbRecord *sibling;
	UdbRecord *child;
	unsigned char block_idx;
	unsigned int is_b64 : 1;
	unsigned int is_dynamic_key : 1;
};

typedef enum UdbBlockLoadState
{
	UDB_LOAD_UNINITIALIZED = 0,
	UDB_LOAD_SUCCESS,
	UDB_LOAD_EMPTY,
	UDB_LOAD_FAILED
} UdbBlockLoadState;

typedef enum UdbSnapshotResult
{
	UDB_SNAPSHOT_FAILED_BEFORE_COMMIT = 0,
	UDB_SNAPSHOT_COMMITTED = 1,
	UDB_SNAPSHOT_COMMITTED_DURABILITY_UNCERTAIN = 2
} UdbSnapshotResult;

struct UdbBlock
{
	UdbRecord *tree;
	UdbBlock *next;
	unsigned long checksum;
	char *filepath;
	unsigned int id;
	unsigned long filesize;
	time_t modified_at;
	Client *syncing_from;
	UdbSyncSession *session;
	unsigned int record_count;
	char letter;
	unsigned int version;
	UdbBlockLoadState load_state;
};

struct UdbSyncSession
{
	Client *peer;
	char txid[UDB_TXID_MAX + 1];
	time_t started_at;
	time_t last_activity;
	time_t deadline;
	time_t absolute_deadline;
	UdbRecord *tree;
	size_t received_bytes;
	unsigned int received_puts;
	unsigned int record_count;
};

typedef struct UdbPasswordFailure
{
	char profile[CHANNELLEN + 1];
	char ip[INET6_ADDRSTRLEN];
	unsigned char block_idx;
	unsigned int attempts;
	time_t since;
} UdbPasswordFailure;

typedef struct UdbConfig
{
	char *db_directory;
	char *propagator;
	int max_global_clones;
	int flood_attempts;
	int flood_period;
	int config_flood_attempts;
	int config_flood_period;
	unsigned int max_staged_records;
	size_t max_staged_bytes;
	int sync_inactivity_timeout;
	int sync_absolute_timeout;
} UdbConfig;

typedef struct UdbContext
{
	UdbBlock *blocks[256];
	UdbBlock *block_list;
	UdbRecord *nicks;
	UdbRecord *channels;
	UdbRecord *ips;
	UdbRecord *settings;
	UdbRecord *links;
	UdbRecord *lines;
	UdbRecord **hash_table[UDB_NUM_BLOCKS];
	Client *propagator;
	char *propagator_setting;
	char *quit_ips;
	char *quit_clones;
	char *encryption_key;
	char *suffix;
	char *nickserv_mask;
	char *chanserv_mask;
	char *ipserv_mask;
	int block_count;
	int total_records;
} UdbContext;

static UdbContext *udb_ctx = NULL;
static UdbPasswordFailure udb_password_failures[UDB_PASSWORD_FAILURE_SLOTS];

static int udb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
static int udb_config_run(ConfigFile *cf, ConfigEntry *ce, int type);
static int udb_config_posttest(int *errs);
static void udb_config_free(UdbContext *ctx);
static int udb_database_directory_valid(const char *value);
static char *udb_block_filepath(char letter);
static int udb_module_test(ModuleInfo *modinfo);
static int udb_module_init(ModuleInfo *modinfo);
static int udb_module_load(ModuleInfo *modinfo);
static int udb_module_unload(void);
static int udb_engine_init(void);
static void udb_engine_cleanup(UdbContext *ctx);
static void udb_engine_shutdown(void);
static UdbBlock *udb_block_create(UdbContext *ctx, char letter, const char *name);
static void udb_block_set_context_root(UdbContext *ctx, UdbBlock *block);
static int udb_block_load(UdbContext *ctx, UdbBlock *block);
static void udb_block_unload(UdbContext *ctx, UdbBlock *block);
static void udb_block_reset(UdbContext *ctx, UdbBlock *block);
static int udb_blocks_load_all(UdbContext *ctx);
static int udb_blocks_save_all(UdbContext *ctx);
static UdbBlock *udb_block_by_letter(UdbContext *ctx, char letter);
static int udb_record_fits_limits(const char *path, const char *value);
static int udb_path_encode_component(const char *raw, char *buf, size_t bufsz);
static int udb_path_decode_component(const char *encoded, char *buf, size_t bufsz);
static int udb_path_append(char *dst, size_t dst_size, size_t *used, const char *component);
static int udb_path_append_component(char *pathbuf, size_t bufsz, const char *raw_component);
static int udb_strtoull_strict(const char *s, unsigned long long *out);
static int udb_strtoul_strict(const char *s, unsigned long *out);
static int udb_parse_uint_strict(const char *s, unsigned int *out, unsigned int min_val, unsigned int max_val);
static int udb_parse_ulong_strict(const char *s, unsigned long *out, unsigned long min_val, unsigned long max_val);
static int udb_parse_size_strict(const char *s, size_t *out, size_t min_val, size_t max_val);
static unsigned long long udb_time_t_max_val(void);
static int udb_parse_time_t(const char *s, time_t *out);
static int udb_time_add(time_t base, unsigned long duration, time_t *result);
static int udb_timestamp_parse(const char *s, time_t *out);
static int udb_checksum_parse(const char *input, unsigned long *checksum);
static UdbRecord *udb_record_find(UdbContext *ctx, const char *key, UdbRecord *parent);
static UdbRecord *udb_record_create(UdbRecord *parent);
static UdbRecord *udb_record_insert(UdbContext *ctx, UdbBlock *block, UdbRecord *parent, const char *key,
									const char *data_str, unsigned long data_num, int persist);
static UdbRecord *udb_record_find_path(UdbContext *ctx, UdbBlock *block, const char *path);
static UdbRecord *udb_record_delete(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int persist);
static void udb_record_free_tree(UdbRecord *rec);
static UdbRecord *udb_record_clone_tree(UdbRecord *rec, UdbRecord *needle, UdbRecord **needle_clone);
static unsigned int udb_record_count_tree(UdbRecord *rec);
static UdbRecord *udb_record_insert_path(UdbRecord *tree, const char *path, const char *data);
static void udb_record_delete_tree(UdbRecord *rec);
static void udb_hash_init(UdbContext *ctx);
static void udb_hash_destroy(UdbContext *ctx);
static void udb_hash_insert_record(UdbContext *ctx, UdbRecord *rec, int block_idx, const char *key);
static int udb_hash_remove_record(UdbContext *ctx, UdbRecord *rec, int block_idx, const char *key);
static UdbRecord *udb_hash_find(UdbContext *ctx, int block_idx, const char *key);
static UdbSnapshotResult udb_file_write_snapshot(UdbBlock *block, UdbRecord *tree, unsigned int record_count);
static int udb_file_save_block(UdbContext *ctx, UdbBlock *block);
static void udb_block_replace_tree(UdbContext *ctx, UdbBlock *block, UdbRecord *tree, unsigned int record_count);
static int udb_file_load_block(UdbContext *ctx, UdbBlock *block);
static UdbRecord *udb_file_parse_line(UdbContext *ctx, UdbBlock *block, char *line);
static int udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf, size_t pathlen);
static unsigned long udb_crc32(const char *data, size_t len);
static int udb_compute_block_checksum(UdbBlock *block, unsigned long *checksum);
static int udb_compute_tree_checksum(UdbRecord *tree, unsigned long *checksum);
static int udb_stage_parse_line(UdbBlock *block, UdbSyncSession *session, const char *line);
static int udb_block_commit_stage(UdbContext *ctx, UdbBlock *block, UdbSyncSession *session, unsigned long checksum);
static void udb_sync_session_free(UdbBlock *block);
static int udb_block_letter_to_index(char letter);

static int udb_sync_to_server(Client *server);
static int udb_remote_wins_equal_timestamp(Client *server);
static int udb_has_hello(Client *server);
static int udb_has_staged_sync(Client *server);
static int udb_peer_authorizes_us(Client *server);
static int udb_sync_hello_start(Client *server);
static int udb_sync_hello_ack(Client *server);
static void udb_sync_abort(UdbBlock *block, const char *reason);
static int udb_sync_begin(UdbBlock *block, Client *peer, const char *txid);
static int udb_sync_put(UdbBlock *block, Client *peer, const char *txid, const char *path, const char *data);
static int udb_sync_end(UdbContext *ctx, UdbBlock *block, Client *peer, const char *txid, const char *checksum,
						unsigned long *digest);
static void udb_sync_ack(Client *peer, const char *block);
static int udb_sync_send_tree(Client *server, UdbRecord *rec, int depth, char *pathbuf, size_t pathlen, char letter,
							  const char *txid);
static int udb_sync_send_stage(Client *server, UdbBlock *block);
static void udb_sync_server_quit(Client *client);
static int udb_is_propagator(UdbContext *ctx, Client *server);
static const char *udb_selected_propagator(UdbContext *ctx);
static int udb_send_db_to_one(Client *to, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int udb_send_db_to_confirmed_servers(Client *except, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int udb_sendto_confirmed_servers(Client *except, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void udb_protocol_params_error(Client *client, const char *subcmd);
static void udb_mutation_ins(UdbContext *ctx, Client *client, const char *target, const char *path, const char *data,
							 int is_for_me, int is_broadcast);
static void udb_mutation_del(UdbContext *ctx, Client *client, const char *target, const char *path, int is_for_me,
							 int is_broadcast);
static void udb_mutation_drp(UdbContext *ctx, Client *client, const char *target, char letter, int is_for_me,
							 int is_broadcast);
static void udb_mutation_opt(UdbContext *ctx, Client *client, const char *target, char letter, const char *modified_at,
							 int is_for_me, int is_broadcast);
static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync);
static void udb_nick_strip(Client *client, UdbRecord *nick_rec);
static void udb_nick_remove_record(UdbBlock *block, UdbRecord *rec);
static void udb_nick_revoke_oper(Client *client);
static int udb_check_password(const char *pass, UdbRecord *profile_rec, Client *client);
static int udb_nick_access_allowed(Client *client, UdbRecord *nick_rec);
static void udb_nick_set_vhost(Client *client, UdbRecord *vhost_rec);
static void udb_nick_remove_vhost(Client *client);
static void udb_nick_grant_oper(Client *client, UdbRecord *nick_rec, UdbRecord *oper_rec);
static void udb_nick_set_modes(Client *client, UdbRecord *nick_rec, UdbRecord *mode_rec, const char *modes);
static void udb_nick_set_swhois(Client *client, UdbRecord *nick_rec, UdbRecord *swhois_rec);
static void udb_nick_set_snomasks(Client *client, UdbRecord *nick_rec, UdbRecord *snomask_rec);
static void udb_channel_apply_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new);
static void udb_channel_remove_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static int udb_channels_load(ModuleInfo *modinfo);
static void udb_ips_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new);
static void udb_ips_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static void udb_ip_refresh_derived_hosts(void);
static void udb_ips_shutdown(void);
static void udb_config_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static void udb_config_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static void udb_lines_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new);
static void udb_lines_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static int udb_spamfilter_pattern(const char *stored, char *pattern, size_t patternsz);
static const char *udb_get_bot_nick(const char *service_key, int force_default);
static const char *udb_get_bot_mask(const char *service_key, int force_default);
static Client *udb_service_source(const char *service_key);
static void udb_send_service_notice(Client *target, const char *service_key, FORMAT_STRING(const char *pattern), ...)
	__attribute__((format(printf, 3, 4)));
static int udb_ip_reapply_vhost(Client *client);
/* Runtime dispatcher; concrete per-block effects stay in their own modules. */
static int udb_apply_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new);
static void udb_remove_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static void udb_apply_tree_effects(UdbContext *ctx, UdbBlock *block);
static void udb_remove_tree_effects(UdbContext *ctx, UdbBlock *block);
static void udb_send_to_debugs(Client *source, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static int udb_protocol_init(ModuleInfo *modinfo);
int udb_nicks_init(ModuleInfo *modinfo);
int udb_nicks_load(ModuleInfo *modinfo);
static void udb_channels_init(ModuleInfo *modinfo);
static void udb_ips_init(ModuleInfo *modinfo);
static void udb_lines_init(ModuleInfo *modinfo);
static void udb_query_init(ModuleInfo *modinfo);
static void udb_sync_snomask_filter(void);

static inline int udb_is_debug_enabled(void)
{
	if (udb_ctx && udb_ctx->links)
	{
		UdbRecord *me_rec = udb_record_find(udb_ctx, me.name, udb_ctx->links);
		if (me_rec)
		{
			UdbRecord *opt_rec = udb_record_find(udb_ctx, LKEY_OPTIONS, me_rec);
			if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG))
				return 1;
		}
	}
	return 0;
}

#define udb_log(level, event_id, client, msg, ...)                                                                     \
	unreal_log(level, "udb", event_id, client, "[UDB] " msg, ##__VA_ARGS__)
#define udb_strdup(dest, src) safe_strdup(dest, src)

#endif /* UDB_INTERNAL_H */

/* ========================================================================
 * Module Header
 * ======================================================================== */

ModuleHeader MOD_HEADER = {"third/udb", "4.0.0", "UDB 4 - Unreal Database System (nick/channel/IP registration & sync)",
						   "David Abuín Fontán ('davidlig')", "unrealircd-6"};

/* ========================================================================
 * Implementation Files
 *
 * Each file implements a specific subsystem. They share the same compilation
 * unit, so all functions are static and can call each other freely.
 * ======================================================================== */

/* Record store: tree, hash, path, and file persistence primitives */
/* Inlined: udb_store.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Record Tree, Indexing, Hashing & File Persistence
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

/* ========================================================================
 * Block Index and Hash Operations
 * ======================================================================== */
static int udb_block_letter_to_index(char letter)
{
	switch (letter)
	{
	case 'N':
		return 0;
	case 'C':
		return 1;
	case 'I':
		return 2;
	case 'S':
		return 3;
	case 'L':
		return 4;
	case 'K':
		return 5;
	default:
		return 0;
	}
}

static void udb_hash_init(UdbContext *ctx)
{
	for (int i = 0; i < UDB_NUM_BLOCKS; i++)
		ctx->hash_table[i] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
}

static void udb_hash_destroy(UdbContext *ctx)
{
	for (int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		if (ctx->hash_table[i])
		{
			safe_free(ctx->hash_table[i]);
			ctx->hash_table[i] = NULL;
		}
	}
}

static void udb_hash_clear_block(UdbContext *ctx, int block_idx)
{
	if (block_idx < 0 || block_idx >= UDB_NUM_BLOCKS)
		return;
	safe_free(ctx->hash_table[block_idx]);
	ctx->hash_table[block_idx] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
}

static unsigned int udb_hash_str(const char *str)
{
	unsigned int hash = 5381;
	int c;

	while ((c = *str++))
	{
		if (c >= 'A' && c <= 'Z')
			c += ('a' - 'A');
		hash = ((hash << 5) + hash) + c; // djb2
	}
	return hash & UDB_HASH_MASK;
}

static void udb_hash_insert_record(UdbContext *ctx, UdbRecord *rec, int block_idx, const char *key)
{
	unsigned int h = udb_hash_str(key);

	rec->hash_next = ctx->hash_table[block_idx][h];
	ctx->hash_table[block_idx][h] = rec;
}

static int udb_hash_remove_record(UdbContext *ctx, UdbRecord *rec, int block_idx, const char *key)
{
	unsigned int h = udb_hash_str(key);
	UdbRecord *curr = ctx->hash_table[block_idx][h];
	UdbRecord *prev = NULL;

	while (curr)
	{
		if (curr == rec)
		{
			if (prev)
				prev->hash_next = curr->hash_next;
			else
				ctx->hash_table[block_idx][h] = curr->hash_next;
			return 1;
		}
		prev = curr;
		curr = curr->hash_next;
	}
	return 0;
}

static UdbRecord *udb_hash_find(UdbContext *ctx, int block_idx, const char *key)
{
	unsigned int h;
	UdbRecord *curr;

	if (!key)
		return NULL;
	h = udb_hash_str(key);
	curr = ctx->hash_table[block_idx][h];
	while (curr)
	{
		if (!strcasecmp(curr->key, key))
			return curr;
		curr = curr->hash_next;
	}
	return NULL;
}

/* ========================================================================
 * Record Tree and Path Operations
 * ======================================================================== */
static const char *udb_get_shared_subkey(const char *key)
{
	static const char *known_keys[] = {"pass",		 "vhost",	 "oper",	  "swhois",	   "snomasks",		 "modes",
									   "access",	 "forbid",	 "suspended", "challenge", "founder",		 "topic",
									   "options",	 "clones",	 "nolines",	  "host",	   "encryption_key", "suffix",
									   "nickserv",	 "chanserv", "ipserv",	  "quit_ips",  "quit_clones",	 "flood",
									   "propagator", "type",	 "action",	  "duration",  "reason",		 NULL};

	for (int i = 0; known_keys[i]; i++)
		if (!strcasecmp(known_keys[i], key))
			return known_keys[i];
	return NULL;
}

static UdbRecord *udb_record_create(UdbRecord *parent)
{
	UdbRecord *rec = safe_alloc(sizeof(UdbRecord));

	rec->parent = parent;
	if (parent)
	{
		rec->block_idx = parent->block_idx;
		rec->sibling = parent->child;
		parent->child = rec;
	}
	return rec;
}

static UdbRecord *udb_record_find(UdbContext *ctx, const char *key, UdbRecord *parent)
{
	UdbRecord *child;

	if (!parent)
		return NULL;
	if (parent->parent == NULL && ctx)
		return udb_hash_find(ctx, parent->block_idx, key);
	for (child = parent->child; child; child = child->sibling)
		if (!strcasecmp(child->key, key))
			return child;
	return NULL;
}

/* ========================================================================
 * Record Limits and Feasibility Validation
 * ======================================================================== */
static int udb_record_fits_limits(const char *path, const char *value)
{
	size_t path_len;
	size_t val_len = 0;
	size_t serialized_line_len;
	const char *comp;
	const char *sep;

	if (!path || !*path)
		return 0;

	path_len = strlen(path);
	if (path_len > UDB_RECORD_PATH_MAX)
		return 0;

	/* Validate each encoded component in path */
	comp = path;
	while ((sep = strstr(comp, "::")))
	{
		if (sep == comp || !sep[2])
			return 0;
		size_t clen = sep - comp;
		if (clen > UDB_COMPONENT_ENCODED_MAX)
			return 0;
		char enc_buf[UDB_COMPONENT_ENCODED_MAX + 1];
		char dec_buf[UDB_COMPONENT_RAW_MAX + 1];
		memcpy(enc_buf, comp, clen);
		enc_buf[clen] = '\0';
		if (!udb_path_decode_component(enc_buf, dec_buf, sizeof(dec_buf)))
			return 0;
		if (strlen(dec_buf) > UDB_COMPONENT_RAW_MAX)
			return 0;
		comp = sep + 2;
	}
	if (!*comp)
		return 0;
	size_t last_clen = strlen(comp);
	if (last_clen > UDB_COMPONENT_ENCODED_MAX)
		return 0;
	char dec_buf[UDB_COMPONENT_RAW_MAX + 1];
	if (!udb_path_decode_component(comp, dec_buf, sizeof(dec_buf)))
		return 0;
	if (strlen(dec_buf) > UDB_COMPONENT_RAW_MAX)
		return 0;

	if (value)
	{
		val_len = strlen(value);
		if (val_len > UDB_RECORD_VALUE_MAX)
			return 0;
		if (strpbrk(value, "\r\n"))
			return 0;
	}

	/* Serialized line check: path + ' ' + value + '\n' */
	serialized_line_len = path_len + (value && *value ? 1 + val_len : 0) + 1;
	if (serialized_line_len > UDB_RECORD_LINE_MAX)
		return 0;

	/* S2S feasibility check */
	if (path_len + (value ? val_len : 0) + UDB_S2S_OVERHEAD_MAX > UDB_S2S_LINE_MAX)
		return 0;

	return 1;
}

/* ========================================================================
 * Path Component Encoding & Decoding
 * ======================================================================== */
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
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = (char)val;
			p += 2;
		}
		else
		{
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = *p;
		}
	}
	buf[out_len] = '\0';
	return 1;
}

static int udb_path_append(char *dst, size_t dst_size, size_t *used, const char *component)
{
	char encoded[UDB_COMPONENT_ENCODED_MAX + 1];
	size_t cur_len;
	size_t enc_len;

	if (!dst || !component || !udb_path_encode_component(component, encoded, sizeof(encoded)))
		return 0;
	cur_len = used ? *used : strlen(dst);
	enc_len = strlen(encoded);
	if (cur_len > 0)
	{
		if (cur_len + 2 + enc_len >= dst_size)
			return 0;
		memcpy(dst + cur_len, "::", 2);
		cur_len += 2;
	}
	else
	{
		if (enc_len >= dst_size)
			return 0;
	}
	memcpy(dst + cur_len, encoded, enc_len + 1);
	cur_len += enc_len;
	if (used)
		*used = cur_len;
	return 1;
}

static int udb_path_append_component(char *pathbuf, size_t bufsz, const char *raw_component)
{
	return udb_path_append(pathbuf, bufsz, NULL, raw_component);
}

static UdbRecord *udb_record_find_path(UdbContext *ctx, UdbBlock *block, const char *path)
{
	char decoded_part[UDB_COMPONENT_RAW_MAX + 1];
	char pathbuf[UDB_RECORD_PATH_MAX + 1];
	char *cur;
	char *ds;
	UdbRecord *rec;

	if (!block || !block->tree || !path || strlen(path) > UDB_RECORD_PATH_MAX || !udb_record_fits_limits(path, NULL))
		return NULL;
	strlcpy(pathbuf, path, sizeof(pathbuf));
	cur = pathbuf;
	rec = block->tree;
	while ((ds = strstr(cur, "::")))
	{
		*ds = '\0';
		if (!udb_path_decode_component(cur, decoded_part, sizeof(decoded_part)))
			return NULL;
		rec = udb_record_find(ctx, decoded_part, rec);
		if (!rec)
			return NULL;
		cur = ds + 2;
	}
	if (!udb_path_decode_component(cur, decoded_part, sizeof(decoded_part)))
		return NULL;
	return udb_record_find(ctx, decoded_part, rec);
}

static void udb_record_free_tree(UdbRecord *rec)
{
	UdbRecord *child;

	if (!rec)
		return;
	child = rec->child;
	while (child)
	{
		UdbRecord *next = child->sibling;

		udb_record_free_tree(child);
		child = next;
	}
	if (rec->key && rec->is_dynamic_key)
		safe_free(rec->key);
	if (rec->data_str)
		safe_free(rec->data_str);
	safe_free(rec);
}

/* Candidate trees are deliberately unindexed: they are private until persisted. */
static UdbRecord *udb_record_clone_tree(UdbRecord *rec, UdbRecord *needle, UdbRecord **needle_clone)
{
	UdbRecord *copy;
	UdbRecord **child;
	UdbRecord *source_child;

	if (!rec)
		return NULL;
	copy = safe_alloc(sizeof(*copy));
	if (rec->key)
	{
		safe_strdup(copy->key, rec->key);
		copy->is_dynamic_key = 1;
	}
	if (rec->data_str)
		safe_strdup(copy->data_str, rec->data_str);
	copy->id = rec->id;
	copy->data_num = rec->data_num;
	copy->block_idx = rec->block_idx;
	copy->is_b64 = rec->is_b64;
	if (rec == needle && needle_clone)
		*needle_clone = copy;
	child = &copy->child;
	for (source_child = rec->child; source_child; source_child = source_child->sibling)
	{
		*child = udb_record_clone_tree(source_child, needle, needle_clone);
		(*child)->parent = copy;
		child = &(*child)->sibling;
	}
	return copy;
}

static unsigned int udb_record_count_tree(UdbRecord *rec)
{
	unsigned int count = 0;
	UdbRecord *child;

	if (!rec)
		return 0;
	for (child = rec->child; child; child = child->sibling)
		count += 1 + udb_record_count_tree(child);
	return count;
}

static UdbRecord *udb_record_insert_path(UdbRecord *tree, const char *path, const char *data)
{
	char pathbuf[UDB_RECORD_PATH_MAX + 1];
	char decoded_part[UDB_COMPONENT_RAW_MAX + 1];
	char *part;
	char *next;
	UdbRecord *parent = tree;

	if (!tree || !path || !*path || strlen(path) > UDB_RECORD_PATH_MAX || !udb_record_fits_limits(path, data))
		return NULL;
	strlcpy(pathbuf, path, sizeof(pathbuf));
	for (part = pathbuf; part; part = next)
	{
		next = strstr(part, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
			if (!*next)
				return NULL;
		}
		if (!*part)
			return NULL;
		if (!udb_path_decode_component(part, decoded_part, sizeof(decoded_part)))
			return NULL;
		UdbRecord *rec = udb_record_find(NULL, decoded_part, parent);
		if (!rec)
		{
			rec = udb_record_create(parent);
			safe_strdup(rec->key, decoded_part);
			rec->is_dynamic_key = 1;
		}
		parent = rec;
	}
	if (parent->data_str)
		safe_free(parent->data_str);
	if (data && *data == '*')
	{
		if (!udb_strtoul_strict(data + 1, &parent->data_num))
			return NULL;
		parent->data_str = NULL;
	}
	else if (data)
	{
		safe_strdup(parent->data_str, data);
		parent->data_num = 0;
	}
	return parent;
}

static void udb_record_delete_tree(UdbRecord *rec)
{
	UdbRecord **link;

	if (!rec || !rec->parent)
		return;
	for (link = &rec->parent->child; *link; link = &(*link)->sibling)
		if (*link == rec)
		{
			*link = rec->sibling;
			udb_record_free_tree(rec);
			return;
		}
}

/* ========================================================================
 * File Persistence
 * ======================================================================== */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf, size_t pathlen)
{
	UdbRecord *child;
	size_t old_len;
	char encoded_key[UDB_COMPONENT_ENCODED_MAX + 1];

	if (!rec || !rec->key)
		return 1;
	old_len = strlen(pathbuf);
	if (!udb_path_encode_component(rec->key, encoded_key, sizeof(encoded_key)))
		return 0;
	size_t enc_len = strlen(encoded_key);
	if (depth > 0)
	{
		if (old_len + 2 + enc_len >= pathlen)
			return 0;
		strlcat(pathbuf, "::", pathlen);
	}
	else
	{
		if (enc_len >= pathlen)
			return 0;
	}
	strlcat(pathbuf, encoded_key, pathlen);
	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		int ret;
		size_t line_len;
		char numbuf[32];
		const char *val_str;

		if (rec->data_str)
		{
			val_str = rec->data_str;
		}
		else
		{
			snprintf(numbuf, sizeof(numbuf), "*%lu", rec->data_num);
			val_str = numbuf;
		}

		if (!udb_record_fits_limits(pathbuf, val_str))
		{
			pathbuf[old_len] = '\0';
			return 0;
		}

		/* path + space + value + newline */
		line_len = strlen(pathbuf) + 1 + strlen(val_str) + 1;
		if (line_len > UDB_RECORD_LINE_MAX)
		{
			pathbuf[old_len] = '\0';
			return 0;
		}

		ret = fprintf(fp, "%s %s\n", pathbuf, val_str);
		if (ret < 0 || (size_t)ret != line_len || ferror(fp))
		{
			pathbuf[old_len] = '\0';
			return 0;
		}
	}
	for (child = rec->child; child; child = child->sibling)
	{
		if (!udb_serialize_tree(child, depth + 1, fp, pathbuf, pathlen))
		{
			pathbuf[old_len] = '\0';
			return 0;
		}
	}
	pathbuf[old_len] = '\0';
	return 1;
}

static int udb_file_cleanup_snapshot_temp(UdbBlock *block)
{
	char tmp_path[UDB_BLOCK_PATH_MAX];
	struct stat st;
	int saved_errno;

	if (!block || !block->filepath)
		return 1;
	if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath) >= (int)sizeof(tmp_path))
	{
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL, "Temporary snapshot path is too long for block $block",
				log_data_string("block", block->filepath));
		return 0;
	}

	if (lstat(tmp_path, &st) != 0)
	{
		if (ENOENT == errno)
			return 1;
		saved_errno = errno;
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL, "Cannot inspect temporary snapshot $path: $error",
				log_data_string("path", tmp_path), log_data_string("error", strerror(saved_errno)));
		return 0;
	}
	if (S_ISLNK(st.st_mode))
	{
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL, "Refusing to remove symlink temporary snapshot $path",
				log_data_string("path", tmp_path));
		return 0;
	}
	if (!S_ISREG(st.st_mode))
	{
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL,
				"Refusing to remove non-regular temporary snapshot $path", log_data_string("path", tmp_path));
		return 0;
	}

	if (unlink(tmp_path) != 0)
	{
		if (ENOENT == errno)
			return 1;
		saved_errno = errno;
		udb_log(ULOG_WARNING, "UDB_SNAPSHOT_TEMP_CLEANUP", NULL, "Cannot remove temporary snapshot $path: $error",
				log_data_string("path", tmp_path), log_data_string("error", strerror(saved_errno)));
		return 0;
	}

	return 1;
}

static UdbSnapshotResult udb_file_write_snapshot(UdbBlock *block, UdbRecord *tree, unsigned int record_count)
{
	char dir_path[UDB_BLOCK_PATH_MAX];
	char tmp_path[UDB_BLOCK_PATH_MAX];
	char pathbuf[UDB_RECORD_PATH_MAX + 1] = "";
	char *slash;
	FILE *fp = NULL;
	UdbRecord *rec;
	int dir_fd = -1;
	int fd = -1;
	int flags = O_WRONLY | O_CREAT | O_EXCL;
	int tmp_created = 0;
	int saved_errno;

	if (!block || !block->filepath)
		return UDB_SNAPSHOT_FAILED_BEFORE_COMMIT;
	if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath) >= (int)sizeof(tmp_path))
		return UDB_SNAPSHOT_FAILED_BEFORE_COMMIT;
	strlcpy(dir_path, block->filepath, sizeof(dir_path));
	slash = strrchr(dir_path, '/');
	if (!slash)
		strlcpy(dir_path, ".", sizeof(dir_path));
	else if (slash == dir_path)
		slash[1] = '\0';
	else
		*slash = '\0';

#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	fd = open(tmp_path, flags, 0600);
	if (fd < 0)
		return UDB_SNAPSHOT_FAILED_BEFORE_COMMIT;
	tmp_created = 1;
	dir_fd = open(dir_path, O_RDONLY
#ifdef O_DIRECTORY
								| O_DIRECTORY
#endif
	);
	if (dir_fd < 0)
		goto cleanup;
	if (fchmod(fd, 0600) != 0)
		goto cleanup;
	fp = fdopen(fd, "w");
	if (!fp)
		goto cleanup;
	fd = -1;
	if (fprintf(fp, "; UDB Block %c - Version %d\n", block->letter, block->version) < 0 ||
		fprintf(fp, "; Saved: %ld\n", (long)time(NULL)) < 0 || fprintf(fp, "; Records: %u\n", record_count) < 0)
		goto cleanup;
	if (tree)
	{
		for (rec = tree->child; rec; rec = rec->sibling)
		{
			if (!udb_serialize_tree(rec, 0, fp, pathbuf, sizeof(pathbuf)))
				goto cleanup;
		}
	}
	if (fflush(fp) != 0 || ferror(fp))
		goto cleanup;
	if (fsync(fileno(fp)) != 0)
		goto cleanup;
	if (fclose(fp) != 0)
	{
		fp = NULL;
		goto cleanup;
	}
	fp = NULL;
	if (rename(tmp_path, block->filepath) != 0)
		goto cleanup;
	tmp_created = 0;
	if (fsync(dir_fd) != 0)
	{
		saved_errno = errno;
		udb_log(ULOG_ERROR, "UDB_SNAPSHOT_DIR_FSYNC_FAILED", NULL,
				"Failed to fsync directory for block $block after commit: $error",
				log_data_string("block", block->filepath), log_data_string("error", strerror(saved_errno)));
		if (close(dir_fd) != 0)
		{
			saved_errno = errno;
			udb_log(ULOG_ERROR, "UDB_SNAPSHOT_DIR_CLOSE_FAILED", NULL,
					"Failed to close directory descriptor for block $block after commit: $error",
					log_data_string("block", block->filepath), log_data_string("error", strerror(saved_errno)));
		}
		return UDB_SNAPSHOT_COMMITTED_DURABILITY_UNCERTAIN;
	}
	if (close(dir_fd) != 0)
	{
		saved_errno = errno;
		udb_log(ULOG_ERROR, "UDB_SNAPSHOT_DIR_CLOSE_FAILED", NULL,
				"Failed to close directory descriptor for block $block after commit: $error",
				log_data_string("block", block->filepath), log_data_string("error", strerror(saved_errno)));
		return UDB_SNAPSHOT_COMMITTED_DURABILITY_UNCERTAIN;
	}
	return UDB_SNAPSHOT_COMMITTED;

cleanup:
	if (fp)
		fclose(fp);
	else if (fd >= 0)
		close(fd);
	if (dir_fd >= 0)
		close(dir_fd);
	if (tmp_created)
		unlink(tmp_path);
	return UDB_SNAPSHOT_FAILED_BEFORE_COMMIT;
}

static int udb_file_save_block(UdbContext *ctx, UdbBlock *block)
{
	struct stat st;
	UdbSnapshotResult res;

	if (!block)
		return 0;
	res = udb_file_write_snapshot(block, block->tree, block->record_count);
	if (res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		return 0;
	(void)ctx;
	if (!udb_compute_block_checksum(block, &block->checksum))
	{
		block->checksum = 0;
		udb_log(ULOG_ERROR, "UDB_CHECKSUM_CALC_FAILED", NULL, "Failed to compute checksum for block $block after save",
				log_data_string("block", (char[]){block->letter, '\0'}));
	}
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
	return 1;
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

static int udb_flood_valid(const char *value, int *attempts, int *period);

static int udb_database_directory_valid(const char *value)
{
	char *path = NULL;
	int valid;

	if (!value || !*value || strstr(value, "://") || strpbrk(value, "\r\n"))
		return 0;
	safe_strdup(path, value);
	convert_to_absolute_path(&path, PERMDATADIR);
	valid = strlen(path) + sizeof("/udb_N.db") <= UDB_BLOCK_PATH_MAX;
	safe_free(path);
	return valid;
}

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
			if (!udb_database_directory_valid(cep->value))
			{
				config_error("%s:%i: udb::database-directory must be a local path that leaves room for UDB block files",
							 cep->file->filename, cep->line_number);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "propagator"))
		{
			if (!cep->value || !*cep->value)
			{
				config_error("%s:%i: udb::propagator requires a server name", cep->file->filename, cep->line_number);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "max-global-clones"))
		{
			unsigned int val;
			if (!cep->value || !udb_parse_uint_strict(cep->value, &val, 0, 1000000))
			{
				config_error("%s:%i: udb::max-global-clones requires a non-negative integer", cep->file->filename,
							 cep->line_number);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "password-flood"))
		{
			int attempts, period;
			if (!cep->value || !udb_flood_valid(cep->value, &attempts, &period))
			{
				config_error(
					"%s:%i: udb::password-flood requires format attempts:seconds with positive integers (e.g. 5:30)",
					cep->file->filename, cep->line_number);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "max-staged-records"))
		{
			unsigned int val;
			if (!cep->value ||
				!udb_parse_uint_strict(cep->value, &val, UDB_MIN_MAX_STAGED_RECORDS, UDB_MAX_MAX_STAGED_RECORDS))
			{
				config_error("%s:%i: udb::max-staged-records must be between %d and %d (default %d)",
							 cep->file->filename, cep->line_number, UDB_MIN_MAX_STAGED_RECORDS,
							 UDB_MAX_MAX_STAGED_RECORDS, UDB_DEFAULT_MAX_STAGED_RECORDS);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "max-staged-bytes"))
		{
			size_t val;
			if (!cep->value ||
				!udb_parse_size_strict(cep->value, &val, UDB_MIN_MAX_STAGED_BYTES, (size_t)UDB_MAX_MAX_STAGED_BYTES))
			{
				config_error("%s:%i: udb::max-staged-bytes must be between %llu and %llu bytes", cep->file->filename,
							 cep->line_number, (unsigned long long)UDB_MIN_MAX_STAGED_BYTES,
							 (unsigned long long)UDB_MAX_MAX_STAGED_BYTES);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "sync-inactivity-timeout"))
		{
			unsigned int val;
			if (!cep->value || !udb_parse_uint_strict(cep->value, &val, 1, 86400))
			{
				config_error("%s:%i: udb::sync-inactivity-timeout must be between 1 and 86400 seconds",
							 cep->file->filename, cep->line_number);
				errors++;
			}
		}
		else if (!strcmp(cep->name, "sync-absolute-timeout"))
		{
			unsigned int val;
			if (!cep->value || !udb_parse_uint_strict(cep->value, &val, 1, 86400))
			{
				config_error("%s:%i: udb::sync-absolute-timeout must be between 1 and 86400 seconds",
							 cep->file->filename, cep->line_number);
				errors++;
			}
		}
		else
		{
			config_error("%s:%i: unknown directive udb::%s", cep->file->filename, cep->line_number, cep->name);
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
			unsigned int val = 0;
			if (udb_parse_uint_strict(cep->value, &val, 0, 1000000))
				udb_cfg->max_global_clones = (int)val;
		}
		else if (!strcmp(cep->name, "max-staged-records"))
		{
			unsigned int val = 0;
			if (udb_parse_uint_strict(cep->value, &val, UDB_MIN_MAX_STAGED_RECORDS, UDB_MAX_MAX_STAGED_RECORDS))
				udb_cfg->max_staged_records = val;
		}
		else if (!strcmp(cep->name, "max-staged-bytes"))
		{
			size_t val = 0;
			if (udb_parse_size_strict(cep->value, &val, UDB_MIN_MAX_STAGED_BYTES, (size_t)UDB_MAX_MAX_STAGED_BYTES))
				udb_cfg->max_staged_bytes = val;
		}
		else if (!strcmp(cep->name, "sync-inactivity-timeout"))
		{
			unsigned int val = 0;
			if (udb_parse_uint_strict(cep->value, &val, 1, 86400))
				udb_cfg->sync_inactivity_timeout = (int)val;
		}
		else if (!strcmp(cep->name, "sync-absolute-timeout"))
		{
			unsigned int val = 0;
			if (udb_parse_uint_strict(cep->value, &val, 1, 86400))
				udb_cfg->sync_absolute_timeout = (int)val;
		}
		else if (!strcmp(cep->name, "password-flood"))
		{
			udb_flood_valid(cep->value, &udb_cfg->flood_attempts, &udb_cfg->flood_period);
		}
	}

	/* Set defaults if not configured */
	if (!udb_cfg->db_directory)
		safe_strdup(udb_cfg->db_directory, UDB_DEFAULT_DB_DIRECTORY);
	convert_to_absolute_path(&udb_cfg->db_directory, PERMDATADIR);
	if (udb_cfg->max_staged_records == 0)
		udb_cfg->max_staged_records = UDB_DEFAULT_MAX_STAGED_RECORDS;
	if (udb_cfg->max_staged_bytes == 0)
		udb_cfg->max_staged_bytes = UDB_DEFAULT_MAX_STAGED_BYTES;
	if (udb_cfg->sync_inactivity_timeout == 0)
		udb_cfg->sync_inactivity_timeout = UDB_SYNC_INACTIVITY_TIMEOUT;
	if (udb_cfg->sync_absolute_timeout == 0)
		udb_cfg->sync_absolute_timeout = UDB_SYNC_ABSOLUTE_TIMEOUT;
	if (udb_cfg->flood_attempts == 0)
		udb_cfg->flood_attempts = 5;
	if (udb_cfg->flood_period == 0)
		udb_cfg->flood_period = 60;
	udb_cfg->config_flood_attempts = udb_cfg->flood_attempts;
	udb_cfg->config_flood_period = udb_cfg->flood_period;

	return 1;
}

static void udb_config_free(UdbContext *ctx)
{
	if (ctx)
	{
		safe_free(ctx->quit_ips);
		safe_free(ctx->quit_clones);
		safe_free(ctx->encryption_key);
		safe_free(ctx->suffix);
		safe_free(ctx->nickserv_mask);
		safe_free(ctx->chanserv_mask);
		safe_free(ctx->ipserv_mask);
		safe_free(ctx->propagator_setting);
	}
	if (udb_cfg)
	{
		safe_free(udb_cfg->db_directory);
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
	const unsigned char *p = (const unsigned char *)value;
	const char *label;

	/* The derived label is 32 hexadecimal characters. */
	if (!value || !*value || strlen(value) > HOSTLEN - 32 || value[0] != '.')
		return 0;
	label = value + 1;
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

static void udb_settings_replace(char **destination, const char *value)
{
	safe_free(*destination);
	if (value)
		safe_strdup(*destination, value);
}

static void udb_settings_restore_flood(void)
{
	if (!udb_cfg)
		return;
	udb_cfg->flood_attempts = udb_cfg->config_flood_attempts;
	udb_cfg->flood_period = udb_cfg->config_flood_period;
}

static int udb_settings_apply_record(UdbContext *ctx, UdbRecord *rec)
{
	int attempts;
	int period;

	if (!rec || !rec->key)
		return 0;
	if (!strcmp(rec->key, SKEY_QUIT_IPS))
	{
		if (!udb_setting_string_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->quit_ips, rec->data_str);
	}
	else if (!strcmp(rec->key, SKEY_QUIT_CLONES))
	{
		if (!udb_setting_string_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->quit_clones, rec->data_str);
	}
	else if (!strcmp(rec->key, SKEY_CLONES))
	{
		if (rec->data_str || rec->data_num == 0)
			return 0;
	}
	else if (!strcmp(rec->key, SKEY_FLOOD))
	{
		if (!udb_flood_valid(rec->data_str, &attempts, &period))
			return 0;
		if (udb_cfg)
		{
			udb_cfg->flood_attempts = attempts;
			udb_cfg->flood_period = period;
		}
	}
	else if (!strcmp(rec->key, SKEY_SUFFIX))
	{
		if (!udb_suffix_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->suffix, rec->data_str);
		udb_ip_refresh_derived_hosts();
	}
	else if (!strcmp(rec->key, SKEY_CRYPT_KEY))
	{
		if (!udb_encryption_key_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->encryption_key, rec->data_str);
		udb_ip_refresh_derived_hosts();
	}
	else if (!strcmp(rec->key, SKEY_NICKSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->nickserv_mask, rec->data_str);
	}
	else if (!strcmp(rec->key, SKEY_CHANSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->chanserv_mask, rec->data_str);
	}
	else if (!strcmp(rec->key, SKEY_IPSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->ipserv_mask, rec->data_str);
	}
	else if (!strcmp(rec->key, SKEY_PROPAGATOR))
	{
		if (!udb_setting_string_valid(rec->data_str))
			return 0;
		udb_settings_replace(&ctx->propagator_setting, rec->data_str);
		ctx->propagator = NULL;
	}
	else
	{
		return 0;
	}
	return 1;
}

static void udb_settings_remove_record(UdbContext *ctx, UdbRecord *rec)
{
	if (!rec || !rec->key)
		return;
	if (!strcmp(rec->key, SKEY_QUIT_IPS))
		udb_settings_replace(&ctx->quit_ips, NULL);
	else if (!strcmp(rec->key, SKEY_QUIT_CLONES))
		udb_settings_replace(&ctx->quit_clones, NULL);
	else if (!strcmp(rec->key, SKEY_FLOOD))
		udb_settings_restore_flood();
	else if (!strcmp(rec->key, SKEY_SUFFIX))
	{
		udb_settings_replace(&ctx->suffix, NULL);
		udb_ip_refresh_derived_hosts();
	}
	else if (!strcmp(rec->key, SKEY_CRYPT_KEY))
	{
		udb_settings_replace(&ctx->encryption_key, NULL);
		udb_ip_refresh_derived_hosts();
	}
	else if (!strcmp(rec->key, SKEY_NICKSERV))
		udb_settings_replace(&ctx->nickserv_mask, NULL);
	else if (!strcmp(rec->key, SKEY_CHANSERV))
		udb_settings_replace(&ctx->chanserv_mask, NULL);
	else if (!strcmp(rec->key, SKEY_IPSERV))
		udb_settings_replace(&ctx->ipserv_mask, NULL);
	else if (!strcmp(rec->key, SKEY_PROPAGATOR))
	{
		udb_settings_replace(&ctx->propagator_setting, NULL);
		ctx->propagator = NULL;
	}
}

static void udb_link_apply_record(UdbContext *ctx, UdbRecord *rec)
{
	if (!rec || !rec->parent || !rec->key || strcmp(rec->key, LKEY_OPTIONS))
		return;
	if (rec->data_str || (rec->data_num & ~UDB_LNKOPT_DEBUG))
		udb_log(ULOG_WARNING, "UDB_LINK_OPTIONS", NULL, "Ignoring invalid L::options for $server",
				log_data_string("server", rec->parent->key));
}

static void udb_link_remove_record(UdbContext *ctx, UdbRecord *rec)
{
	(void)ctx;
	(void)rec;
}

static void udb_config_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (block->letter == 'S')
	{
		if (!udb_settings_apply_record(ctx, rec))
			udb_log(ULOG_WARNING, "UDB_SETTING_INVALID", NULL, "Ignoring invalid or unsupported S::$setting",
					log_data_string("setting", rec->key));
	}
	else if (block->letter == 'L')
	{
		udb_link_apply_record(ctx, rec);
	}
}

static void udb_config_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (block->letter == 'S')
		udb_settings_remove_record(ctx, rec);
	else if (block->letter == 'L')
		udb_link_remove_record(ctx, rec);
}

/* End of udb_config.c.inc */

/* Core database engine: records, checksums, sync staging, and file I/O */
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

static int udb_checksum_parse(const char *input, unsigned long *checksum)
{
	char *end;
	const char *p;

	if (!input || !*input || strlen(input) > 8)
		return 0;
	for (p = input; *p; p++)
		if (!isxdigit((unsigned char)*p))
			return 0;
	errno = 0;
	unsigned long val = strtoul(input, &end, 16);
	if (errno == ERANGE || *end != '\0')
		return 0;
	if (checksum)
		*checksum = val;
	return 1;
}

static int udb_numeric_record_valid(const char *value)
{
	unsigned long val;

	if (!value || value[0] != '*' || value[1] == '\0')
		return 0;
	return udb_strtoul_strict(value + 1, &val);
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

		cm = find_channel_mode_handler(*c);
		if (!cm)
			return 0;

		mode_letters++;
		if (cm->type == CMODE_MEMBER)
		{
			expected_params++;
		}
		else if (what == MODE_ADD && cm->paracount > 0)
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

	return expected_params == actual_params;
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
		if (!isalpha((unsigned char)*c))
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

static int udb_challenge_valid(const char *value)
{
	return value && (!strcasecmp(value, "argon2id") || !strcasecmp(value, "sha256") || !strcasecmp(value, "crypt"));
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
	const char *c;
	if (!srv || !*srv || strlen(srv) > HOSTLEN)
		return 0;
	for (c = srv; *c; c++)
	{
		if (*c <= ' ' || (unsigned char)*c > 126)
			return 0;
	}
	return 1;
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

static int udb_spamfilter_type_valid(const char *value)
{
	return value && *value && spamfilter_getconftargets(value) > 0;
}

static int udb_spamfilter_action_valid(const char *value)
{
	return value && *value && banact_stringtoval(value) > 0;
}

static const UdbKeyDescriptor udb_schema_n_subkeys[] = {
	{NKEY_ACCESS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_PASS, UDB_VAL_STRING, udb_password_hash_valid, 0, NULL},
	{NKEY_VHOST, UDB_VAL_STRING, udb_vhost_valid, 0, NULL},
	{NKEY_FORBID, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_SUSPENDED, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{NKEY_OPER, UDB_VAL_STRING, udb_oper_record_valid, 0, NULL},
	{NKEY_CHALLENGE, UDB_VAL_STRING, udb_challenge_valid, 0, NULL},
	{NKEY_MODES, UDB_VAL_STRING, udb_user_modes_record_valid, 0, NULL},
	{NKEY_SNOMASKS, UDB_VAL_STRING, udb_snomasks_record_valid, 0, NULL},
	{NKEY_SWHOIS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_c_subkeys[] = {
	{CKEY_FOUNDER, UDB_VAL_STRING, udb_nick_name_valid, 0, NULL},
	{CKEY_MODES, UDB_VAL_STRING, udb_channel_modes_record_valid, 0, NULL},
	{CKEY_TOPIC, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_ACCESS, UDB_VAL_ANY, NULL, 1, udb_nick_name_valid},
	{CKEY_FORBID, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_SUSPENDED, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{CKEY_PASS, UDB_VAL_STRING, udb_password_hash_valid, 0, NULL},
	{CKEY_CHALLENGE, UDB_VAL_STRING, udb_challenge_valid, 0, NULL},
	{CKEY_OPTIONS, UDB_VAL_NUMERIC, udb_options_record_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_i_subkeys[] = {
	{IKEY_CLONES, UDB_VAL_NUMERIC, udb_numeric_record_valid, 0, NULL},
	{IKEY_NOLINES, UDB_VAL_STRING, udb_nolines_record_valid, 0, NULL},
	{IKEY_HOST, UDB_VAL_STRING, udb_vhost_valid, 0, NULL}};

static const UdbKeyDescriptor udb_schema_s_subkeys[] = {
	{SKEY_CLONES, UDB_VAL_NUMERIC, udb_numeric_record_valid, 0, NULL},
	{SKEY_QUIT_IPS, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{SKEY_QUIT_CLONES, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL},
	{SKEY_FLOOD, UDB_VAL_STRING, udb_flood_setting_valid, 0, NULL},
	{SKEY_CRYPT_KEY, UDB_VAL_STRING, udb_encryption_key_valid, 0, NULL},
	{SKEY_SUFFIX, UDB_VAL_STRING, udb_suffix_valid, 0, NULL},
	{SKEY_NICKSERV, UDB_VAL_STRING, udb_service_mask_valid, 0, NULL},
	{SKEY_CHANSERV, UDB_VAL_STRING, udb_service_mask_valid, 0, NULL},
	{SKEY_IPSERV, UDB_VAL_STRING, udb_service_mask_valid, 0, NULL},
	{SKEY_PROPAGATOR, UDB_VAL_STRING, udb_non_empty_string_valid, 0, NULL}};

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

static const UdbBlockSchema *udb_get_block_schema(char letter)
{
	for (size_t i = 0; i < sizeof(udb_block_schemas) / sizeof(udb_block_schemas[0]); i++)
	{
		if (udb_block_schemas[i].letter == letter)
			return &udb_block_schemas[i];
	}
	return NULL;
}

static int udb_record_validate(UdbBlock *block, const char *path, const char *value)
{
	char pathbuf[UDB_RECORD_PATH_MAX + 1];
	char *decoded_parts = NULL;
	char *parts[8];
	int depth = 0;
	char *p, *next;
	const UdbBlockSchema *schema;
	const UdbKeyDescriptor *desc = NULL;
	int result = 0;

	if (!block || !path || !*path || strlen(path) > UDB_RECORD_PATH_MAX)
		return 0;

	if (!udb_record_fits_limits(path, value))
		return 0;

	schema = udb_get_block_schema(block->letter);
	if (!schema)
		return 0;

	decoded_parts = safe_alloc(8 * (UDB_COMPONENT_RAW_MAX + 1));
	for (int i = 0; i < 8; i++)
		parts[i] = decoded_parts + (i * (UDB_COMPONENT_RAW_MAX + 1));

	strlcpy(pathbuf, path, sizeof(pathbuf));
	p = pathbuf;
	while (p && *p)
	{
		next = strstr(p, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
			if (!*next)
				goto done;
		}
		if (depth >= 8)
			goto done;
		if (!udb_path_decode_component(p, parts[depth], UDB_COMPONENT_RAW_MAX + 1))
			goto done;
		depth++;
		p = next;
	}

	if (depth < schema->min_depth || depth > schema->max_depth)
		goto done;

	/* Special handling for Block S: depth == 1 */
	if (block->letter == UDB_BLOCK_SETTINGS)
	{
		const char *key = parts[0];
		for (size_t i = 0; i < schema->subkey_count; i++)
		{
			if (!strcasecmp(schema->subkeys[i].key, key))
			{
				desc = &schema->subkeys[i];
				break;
			}
		}
		if (!desc)
			goto done;

		if (desc->val_type == UDB_VAL_NUMERIC)
		{
			if (!udb_numeric_record_valid(value))
				goto done;
		}
		else if (desc->val_type == UDB_VAL_STRING)
		{
			if (!value || value[0] == '*' || !*value)
				goto done;
		}
		if (desc->validator && !desc->validator(value))
			goto done;
		result = 1;
		goto done;
	}

	/* Special handling for Block K (TKL / Spamfilter) */
	if (block->letter == UDB_BLOCK_LINES)
	{
		const char *type = parts[0];
		if (!udb_tkl_type_valid(type))
			goto done;

		if (type[0] == 'F') /* Spamfilter */
		{
			if (depth != 3)
				goto done;
			const char *pattern = parts[1];
			const char *subkey = parts[2];
			char pat_buf[UDB_SPAMFILTER_PATTERN_MAX + 1];
			if (!pattern || !*pattern || !udb_spamfilter_pattern(pattern, pat_buf, sizeof(pat_buf)))
				goto done;
			if (!strcasecmp(subkey, KKEY_TYPE))
			{
				result = udb_spamfilter_type_valid(value);
				goto done;
			}
			if (!strcasecmp(subkey, KKEY_ACTION))
			{
				result = udb_spamfilter_action_valid(value);
				goto done;
			}
			if (!strcasecmp(subkey, KKEY_DURATION))
			{
				result = udb_numeric_record_valid(value);
				goto done;
			}
			if (!strcasecmp(subkey, KKEY_REASON))
			{
				result = udb_non_empty_string_valid(value) && value[0] != '*';
				goto done;
			}
			goto done;
		}
		else /* G, Z, S, Q */
		{
			const char *pattern = parts[1];
			if (!pattern || !*pattern)
				goto done;
			if (depth == 2)
			{
				result = udb_non_empty_string_valid(value) && value[0] != '*';
				goto done;
			}
			else if (depth == 3)
			{
				const char *subkey = parts[2];
				if (!strcasecmp(subkey, KKEY_REASON))
				{
					result = udb_non_empty_string_valid(value) && value[0] != '*';
					goto done;
				}
				if (!strcasecmp(subkey, KKEY_DURATION))
				{
					result = udb_numeric_record_valid(value);
					goto done;
				}
				goto done;
			}
			goto done;
		}
	}

	/* Validate root container node */
	if (schema->root_key_validator && !schema->root_key_validator(parts[0]))
		goto done;

	/* Depth 1 is a valid root profile/container node for N, C, I, L */
	if (depth == 1)
	{
		result = 1;
		goto done;
	}

	const char *subkey = parts[1];
	for (size_t i = 0; i < schema->subkey_count; i++)
	{
		if (!strcasecmp(schema->subkeys[i].key, subkey))
		{
			desc = &schema->subkeys[i];
			break;
		}
	}
	if (!desc)
		goto done;

	if (depth == 2)
	{
		if (desc->val_type == UDB_VAL_NUMERIC)
		{
			if (!udb_numeric_record_valid(value))
				goto done;
		}
		else if (desc->val_type == UDB_VAL_STRING)
		{
			if (!value || value[0] == '*' || !*value)
				goto done;
		}
		else if (desc->val_type == UDB_VAL_NONE)
		{
			if (value && *value)
				goto done;
		}
		if (desc->validator && !desc->validator(value))
			goto done;
		result = 1;
		goto done;
	}
	else if (depth == 3)
	{
		if (!desc->allow_children)
			goto done;
		if (desc->child_validator && !desc->child_validator(parts[2]))
			goto done;
		if (value && *value)
		{
			if (value[0] == '*')
			{
				if (!udb_numeric_record_valid(value))
					goto done;
			}
			else
			{
				if (!udb_non_empty_string_valid(value))
					goto done;
			}
		}
		result = 1;
		goto done;
	}

done:
	if (decoded_parts)
		safe_free(decoded_parts);
	return result;
}

static void udb_block_replace_tree(UdbContext *ctx, UdbBlock *block, UdbRecord *tree, unsigned int record_count)
{
	UdbRecord *rec;
	struct stat st;

	if (ctx->total_records >= block->record_count)
		ctx->total_records -= block->record_count;
	else
		ctx->total_records = 0;
	udb_record_free_tree(block->tree);
	udb_hash_clear_block(ctx, udb_block_letter_to_index(block->letter));
	block->tree = tree;
	block->record_count = record_count;
	ctx->total_records += record_count;
	for (rec = tree->child; rec; rec = rec->sibling)
		udb_hash_insert_record(ctx, rec, udb_block_letter_to_index(block->letter), rec->key);
	udb_block_set_context_root(ctx, block);
	if (!udb_compute_block_checksum(block, &block->checksum))
	{
		block->checksum = 0;
		udb_log(ULOG_ERROR, "UDB_CHECKSUM_CALC_FAILED", NULL,
				"Failed to compute checksum for block $block after commit",
				log_data_string("block", (char[]){block->letter, '\0'}));
	}
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
}

static UdbRecord *udb_record_insert(UdbContext *ctx, UdbBlock *block, UdbRecord *parent, const char *key,
									const char *data_str, unsigned long data_num, int persist)
{
	if (persist)
	{
		UdbRecord *candidate_parent = NULL;
		UdbRecord *candidate;
		char value[32];

		if (!parent)
			parent = block->tree;
		if (!data_str)
		{
			snprintf(value, sizeof(value), "*%lu", data_num);
			data_str = value;
		}
		if (!udb_record_fits_limits(key, data_str))
			return NULL;
		candidate = udb_record_clone_tree(block->tree, parent, &candidate_parent);
		if (!candidate_parent)
			candidate_parent = candidate;
		UdbRecord *rec = udb_record_find(NULL, key, candidate_parent);
		if (!rec)
		{
			rec = udb_record_create(candidate_parent);
			safe_strdup(rec->key, key);
			rec->is_dynamic_key = 1;
		}
		if (rec->data_str)
			safe_free(rec->data_str);
		if (*data_str == '*')
		{
			if (!udb_strtoul_strict(data_str + 1, &rec->data_num))
			{
				udb_record_free_tree(candidate);
				return NULL;
			}
			rec->data_str = NULL;
		}
		else
		{
			safe_strdup(rec->data_str, data_str);
			rec->data_num = 0;
		}
		UdbSnapshotResult res = udb_file_write_snapshot(block, candidate, udb_record_count_tree(candidate));
		if (res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_record_free_tree(candidate);
			return NULL;
		}
		udb_block_replace_tree(ctx, block, candidate, udb_record_count_tree(candidate));
		udb_apply_special_record(ctx, block, rec, 1);
		return rec;
	}
	if (!parent)
		parent = block->tree;
	UdbRecord *rec = udb_record_find(ctx, key, parent);
	if (!rec)
	{
		rec = udb_record_create(parent);
		if (key)
		{
			if (parent == block->tree)
			{
				safe_strdup(rec->key, key);
				rec->is_dynamic_key = 1;
			}
			else
			{
				const char *shared = udb_get_shared_subkey(key);
				if (shared)
				{
					rec->key = (char *)shared;
					rec->is_dynamic_key = 0;
				}
				else
				{
					safe_strdup(rec->key, key);
					rec->is_dynamic_key = 1;
				}
			}
		}
		if (parent == block->tree)
		{
			udb_hash_insert_record(ctx, rec, udb_block_letter_to_index(block->letter), key);
		}
		block->record_count++;
		ctx->total_records++;
	}

	if (rec->data_str)
	{
		safe_free(rec->data_str);
	}

	// Auto-detect numeric data if it starts with *
	if (data_str && *data_str == '*')
	{
		udb_strtoul_strict(data_str + 1, &rec->data_num);
		rec->data_str = NULL;
	}
	else if (data_str)
	{
		safe_strdup(rec->data_str, data_str);
		rec->data_num = 0;
	}
	else
	{
		rec->data_str = NULL;
		rec->data_num = data_num;
	}

	udb_apply_special_record(ctx, block, rec, 1);

	return rec;
}

static UdbRecord *udb_record_delete(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int persist)
{
	UdbRecord *line_rec = NULL;
	if (!rec)
		return NULL;
	if (persist)
	{
		UdbRecord *candidate_rec = NULL;
		UdbRecord *candidate_line = NULL;
		UdbRecord *candidate = udb_record_clone_tree(block->tree, rec, &candidate_rec);
		if (!candidate_rec)
		{
			udb_record_free_tree(candidate);
			return rec;
		}
		if (block->letter == 'K' && candidate_rec->parent && candidate_rec->parent->parent &&
			candidate_rec->parent->parent != candidate)
			candidate_line = candidate_rec->parent;
		udb_record_delete_tree(candidate_rec);
		UdbSnapshotResult res = udb_file_write_snapshot(block, candidate, udb_record_count_tree(candidate));
		if (res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_record_free_tree(candidate);
			return rec;
		}
		udb_remove_special_record(ctx, block, rec);
		udb_block_replace_tree(ctx, block, candidate, udb_record_count_tree(candidate));
		if (candidate_line)
			udb_lines_apply_effect(ctx, block, candidate_line, 0);
		return NULL;
	}

	/* A K property deletion changes its owning pattern, not a line of its own. */
	if (block->letter == 'K' && rec->parent && rec->parent->parent && rec->parent->parent != block->tree)
		line_rec = rec->parent;

	udb_remove_special_record(ctx, block, rec);

	if (rec->parent)
	{
		if (rec->parent->parent == NULL)
		{
			udb_hash_remove_record(ctx, rec, udb_block_letter_to_index(block->letter), rec->key);
		}

		UdbRecord *curr = rec->parent->child;
		UdbRecord *prev = NULL;
		while (curr)
		{
			if (curr == rec)
			{
				if (prev)
					prev->sibling = curr->sibling;
				else
					rec->parent->child = curr->sibling;
				break;
			}
			prev = curr;
			curr = curr->sibling;
		}
	}

	if (block->record_count > 0)
		block->record_count--;
	if (ctx->total_records > 0)
		ctx->total_records--;

	udb_record_free_tree(rec);

	/* Rebuild a surviving K pattern after one of its properties was removed. */
	if (line_rec)
		udb_lines_apply_effect(ctx, block, line_rec, 0);

	return NULL;
}

/* ========================================================================
 * Checksum Operations
 * ======================================================================== */
static unsigned long udb_crc32_step(unsigned long crc, const char *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		crc ^= (unsigned char)data[i];
		for (int j = 0; j < 8; j++)
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320UL;
			else
				crc >>= 1;
		}
	}
	return crc;
}

static unsigned long udb_crc32(const char *data, size_t len)
{
	return udb_crc32_step(0xFFFFFFFFUL, data, len) ^ 0xFFFFFFFFUL;
}

typedef struct UdbDigestLine
{
	char *line;
	struct UdbDigestLine *next;
} UdbDigestLine;

static void udb_digest_lines_free(UdbDigestLine *lines)
{
	while (lines)
	{
		UdbDigestLine *next = lines->next;
		if (lines->line)
			safe_free(lines->line);
		safe_free(lines);
		lines = next;
	}
}

static int udb_digest_collect(UdbRecord *rec, int depth, char *pathbuf, size_t pathlen, UdbDigestLine **lines)
{
	size_t orig_len;
	UdbRecord *child;
	char encoded_key[UDB_COMPONENT_ENCODED_MAX + 1];

	if (!rec || !rec->key)
		return 1;
	orig_len = strlen(pathbuf);
	if (!udb_path_encode_component(rec->key, encoded_key, sizeof(encoded_key)))
		return 0;
	size_t enc_len = strlen(encoded_key);
	if (depth > 0)
	{
		if (orig_len + 2 + enc_len >= pathlen)
			return 0;
		strlcat(pathbuf, "::", pathlen);
	}
	else
	{
		if (enc_len >= pathlen)
			return 0;
	}
	strlcat(pathbuf, encoded_key, pathlen);

	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		char numbuf[32];
		const char *val_str;

		if (rec->data_str)
		{
			val_str = rec->data_str;
		}
		else
		{
			snprintf(numbuf, sizeof(numbuf), "*%lu", rec->data_num);
			val_str = numbuf;
		}

		if (!udb_record_fits_limits(pathbuf, val_str))
		{
			pathbuf[orig_len] = '\0';
			return 0;
		}

		UdbDigestLine *line = safe_alloc(sizeof(*line));
		size_t len = strlen(pathbuf) + 1 + strlen(val_str) + 1;
		line->line = safe_alloc(len);
		int n = snprintf(line->line, len, "%s %s", pathbuf, val_str);
		if (n < 0 || (size_t)n >= len)
		{
			safe_free(line->line);
			safe_free(line);
			pathbuf[orig_len] = '\0';
			return 0;
		}
		line->next = *lines;
		*lines = line;
	}
	for (child = rec->child; child; child = child->sibling)
	{
		if (!udb_digest_collect(child, depth + 1, pathbuf, pathlen, lines))
		{
			pathbuf[orig_len] = '\0';
			return 0;
		}
	}
	pathbuf[orig_len] = '\0';
	return 1;
}

static int udb_digest_line_cmp(const void *a, const void *b)
{
	const UdbDigestLine *const *left = a;
	const UdbDigestLine *const *right = b;
	return strcmp((*left)->line, (*right)->line);
}

/* The digest covers sorted logical records, never save-time file headers/order. */
static int udb_compute_tree_checksum(UdbRecord *tree, unsigned long *checksum)
{
	UdbDigestLine *lines = NULL;
	UdbDigestLine *line;
	UdbDigestLine **sorted;
	unsigned long crc = 0xFFFFFFFFUL;
	unsigned int count = 0;
	unsigned int i = 0;
	char pathbuf[UDB_RECORD_PATH_MAX + 1] = "";

	if (checksum)
		*checksum = 0;
	if (!tree)
		return 1;

	for (UdbRecord *rec = tree->child; rec; rec = rec->sibling)
	{
		if (!udb_digest_collect(rec, 0, pathbuf, sizeof(pathbuf), &lines))
		{
			udb_digest_lines_free(lines);
			return 0;
		}
	}
	for (line = lines; line; line = line->next)
		count++;
	if (count == 0)
	{
		if (checksum)
			*checksum = 0;
		return 1;
	}
	sorted = safe_alloc(sizeof(*sorted) * count);
	for (line = lines; line; line = line->next)
		sorted[i++] = line;
	qsort(sorted, count, sizeof(*sorted), udb_digest_line_cmp);
	for (i = 0; i < count; i++)
	{
		crc = udb_crc32_step(crc, sorted[i]->line, strlen(sorted[i]->line));
		crc = udb_crc32_step(crc, "\n", 1);
		safe_free(sorted[i]->line);
		safe_free(sorted[i]);
	}
	safe_free(sorted);
	if (checksum)
		*checksum = crc ^ 0xFFFFFFFFUL;
	return 1;
}

static int udb_compute_block_checksum(UdbBlock *block, unsigned long *checksum)
{
	if (!block || !block->tree)
	{
		if (checksum)
			*checksum = 0;
		return 1;
	}
	return udb_compute_tree_checksum(block->tree, checksum);
}

static UdbRecord *udb_stage_find(UdbRecord *parent, const char *key)
{
	UdbRecord *rec;
	for (rec = parent ? parent->child : NULL; rec; rec = rec->sibling)
		if (!strcasecmp(rec->key, key))
			return rec;
	return NULL;
}

static UdbRecord *udb_stage_insert(UdbRecord *parent, const char *key, UdbSyncSession *session)
{
	UdbRecord *rec = udb_stage_find(parent, key);
	if (!rec)
	{
		rec = udb_record_create(parent);
		safe_strdup(rec->key, key);
		rec->is_dynamic_key = 1;
		session->record_count++;
	}
	return rec;
}

static int udb_stage_parse_line(UdbBlock *block, UdbSyncSession *session, const char *input)
{
	char *line;
	char decoded_part[UDB_COMPONENT_RAW_MAX + 1];
	char *value, *part, *next;
	UdbRecord *parent;
	int result = 0;

	if (!block || !session || !session->tree || !input || !*input || strlen(input) > UDB_RECORD_LINE_MAX)
		return 0;
	line = safe_alloc(UDB_RECORD_LINE_MAX + 2);
	strlcpy(line, input, UDB_RECORD_LINE_MAX + 2);
	value = strchr(line, ' ');
	if (value)
		*value++ = '\0';
	if (!*line)
		goto done;
	if (!udb_record_fits_limits(line, value))
		goto done;
	if (!udb_record_validate(block, line, value))
		goto done;
	parent = session->tree;
	part = line;
	while (part && *part)
	{
		next = strstr(part, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
			if (!*next)
				goto done;
		}
		if (!*part)
			goto done;
		if (!udb_path_decode_component(part, decoded_part, sizeof(decoded_part)))
			goto done;
		parent = udb_stage_insert(parent, decoded_part, session);
		part = next;
	}
	if (value && *value == '*')
	{
		if (!udb_strtoul_strict(value + 1, &parent->data_num))
			goto done;
		safe_free(parent->data_str);
	}
	else if (value)
	{
		safe_free(parent->data_str);
		safe_strdup(parent->data_str, value);
		parent->data_num = 0;
	}
	else
	{
		goto done;
	}
	result = 1;

done:
	safe_free(line);
	return result;
}

static void udb_sync_session_free(UdbBlock *block)
{
	if (!block || !block->session)
		return;
	udb_record_free_tree(block->session->tree);
	safe_free(block->session);
	block->session = NULL;
	block->syncing_from = NULL;
}

static int udb_block_commit_stage(UdbContext *ctx, UdbBlock *block, UdbSyncSession *session, unsigned long checksum)
{
	UdbRecord *rec;
	struct stat st;

	if (!block || !session || block->session != session)
		return 0;
	/* Persistence succeeded before this point; only now may active state move. */
	unsigned int real_count = udb_record_count_tree(session->tree);
	udb_block_reset(ctx, block);
	udb_record_free_tree(block->tree);
	block->tree = session->tree;
	session->tree = NULL;
	block->record_count = real_count;
	ctx->total_records += block->record_count;
	for (rec = block->tree->child; rec; rec = rec->sibling)
		udb_hash_insert_record(ctx, rec, udb_block_letter_to_index(block->letter), rec->key);
	udb_block_set_context_root(ctx, block);
	block->checksum = checksum;
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
	udb_sync_session_free(block);

	udb_apply_tree_effects(ctx, block);
	if (block->letter == 'L')
		udb_sync_snomask_filter();
	return 1;
}

/* ========================================================================
 * File I/O Operations
 * ======================================================================== */
static int udb_record_path_valid(const char *path)
{
	const char *component;
	const char *p;

	if (!path || !*path || strlen(path) > UDB_RECORD_PATH_MAX || !udb_record_fits_limits(path, NULL))
		return 0;
	component = path;
	for (p = path; *p; p++)
	{
		if (*p == ':')
		{
			if (p == component || p[1] != ':' || !p[2])
				return 0;
			char decoded[UDB_COMPONENT_RAW_MAX + 1];
			char comp_buf[UDB_COMPONENT_ENCODED_MAX + 1];
			size_t clen = p - component;
			if (clen >= sizeof(comp_buf))
				return 0;
			memcpy(comp_buf, component, clen);
			comp_buf[clen] = '\0';
			if (!udb_path_decode_component(comp_buf, decoded, sizeof(decoded)))
				return 0;
			p++;
			component = p + 1;
		}
	}
	if (!*component)
		return 0;
	char decoded[UDB_COMPONENT_RAW_MAX + 1];
	if (!udb_path_decode_component(component, decoded, sizeof(decoded)))
		return 0;
	return 1;
}

static UdbRecord *udb_file_parse_line_to_tree(UdbBlock *block, UdbRecord *tree, char *line)
{
	if (!line || !*line || *line == ';')
		return NULL;

	char *value = strchr(line, ' ');
	char *data_str = NULL;
	unsigned long data_num = 0;

	if (value)
	{
		*value++ = '\0';
		if (*value == '*')
		{
			if (!udb_strtoul_strict(value + 1, &data_num))
				return NULL;
		}
		else
		{
			data_str = value;
		}
	}
	const char *val_for_validation = value ? (*value == '*' ? value : data_str) : NULL;
	if (!udb_record_fits_limits(line, val_for_validation))
		return NULL;
	if (!udb_record_path_valid(line))
		return NULL;
	if (!udb_record_validate(block, line, val_for_validation))
		return NULL;

	char *p = line;
	UdbRecord *parent = tree;
	UdbRecord *leaf_rec = NULL;
	while (p && *p)
	{
		char decoded_part[UDB_COMPONENT_RAW_MAX + 1];
		char *next = strstr(p, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
			if (!*next)
				return NULL;
		}
		if (!*p)
			return NULL;
		if (!udb_path_decode_component(p, decoded_part, sizeof(decoded_part)))
			return NULL;

		UdbRecord *rec = udb_record_find(NULL, decoded_part, parent);
		if (!rec)
		{
			rec = udb_record_create(parent);
			if (parent == tree)
			{
				safe_strdup(rec->key, decoded_part);
				rec->is_dynamic_key = 1;
			}
			else
			{
				const char *shared = udb_get_shared_subkey(decoded_part);
				if (shared)
				{
					rec->key = (char *)shared;
					rec->is_dynamic_key = 0;
				}
				else
				{
					safe_strdup(rec->key, decoded_part);
					rec->is_dynamic_key = 1;
				}
			}
		}

		if (!next)
		{
			if (value && *value == '*')
			{
				rec->data_num = data_num;
				if (rec->data_str)
				{
					safe_free(rec->data_str);
					rec->data_str = NULL;
				}
			}
			else if (data_str)
			{
				if (rec->data_str)
					safe_free(rec->data_str);
				safe_strdup(rec->data_str, data_str);
				rec->data_num = 0;
			}
			else
			{
				if (rec->data_str)
				{
					safe_free(rec->data_str);
					rec->data_str = NULL;
				}
				rec->data_num = 0;
			}
			leaf_rec = rec;
		}

		parent = rec;
		p = next;
	}
	return leaf_rec;
}

static UdbRecord *udb_file_parse_line(UdbContext *ctx, UdbBlock *block, char *line)
{
	(void)ctx;
	return udb_file_parse_line_to_tree(block, block->tree, line);
}

static int udb_file_load_block(UdbContext *ctx, UdbBlock *block)
{
	if (!block || !block->filepath)
		return 0;

	FILE *fp = fopen(block->filepath, "r");
	if (!fp)
	{
		if (errno == ENOENT)
		{
			udb_block_reset(ctx, block);
			block->load_state = UDB_LOAD_EMPTY;
			block->checksum = 0;
			block->modified_at = 0;
			block->filesize = 0;
			return 1;
		}
		int saved_errno = errno;
		block->load_state = UDB_LOAD_FAILED;
		udb_log(ULOG_ERROR, "UDB_FILE_OPEN_FAILED", NULL, "Cannot open database file $file for block $block: $error",
				log_data_string("file", block->filepath), log_data_string("block", (char[]){block->letter, '\0'}),
				log_data_string("error", strerror(saved_errno)));
		return 0;
	}

	char *line = safe_alloc(UDB_RECORD_LINE_MAX + 2);
	unsigned int line_number = 0;
	int parse_failed = 0;
	UdbRecord *candidate = udb_record_create(NULL);
	candidate->block_idx = (unsigned char)udb_block_letter_to_index(block->letter);
	safe_strdup(candidate->key, block->tree && block->tree->key ? block->tree->key : "UDB");
	candidate->is_dynamic_key = 1;
	candidate->data_num = 1;

	while (fgets(line, UDB_RECORD_LINE_MAX + 2, fp))
	{
		size_t l_len = strlen(line);
		int has_newline = (l_len > 0 && line[l_len - 1] == '\n');
		int overlong = !has_newline && !feof(fp);
		line_number++;
		if (overlong || l_len > UDB_RECORD_LINE_MAX + 1)
		{
			char logbuf[512];
			snprintf(logbuf, sizeof(logbuf), "Overlong persisted record in block %c at line %u", block->letter,
					 line_number);
			udb_log(ULOG_ERROR, "UDB_FILE_LINE_REJECTED", NULL, "$msg", log_data_string("msg", logbuf));
			parse_failed = 1;
			break;
		}
		char *p = strchr(line, '\n');
		if (p)
			*p = '\0';
		p = strchr(line, '\r');
		if (p)
			*p = '\0';

		if (line[0] == ';' || line[0] == '\0')
			continue;

		if (!udb_file_parse_line_to_tree(block, candidate, line))
		{
			char logbuf[512];
			snprintf(logbuf, sizeof(logbuf), "Malformed persisted record in block %c at line %u", block->letter,
					 line_number);
			udb_log(ULOG_ERROR, "UDB_FILE_LINE_REJECTED", NULL, "$msg", log_data_string("msg", logbuf));
			parse_failed = 1;
			break;
		}
	}

	if (parse_failed || ferror(fp))
	{
		int saved_errno = errno;
		if (ferror(fp))
		{
			udb_log(ULOG_ERROR, "UDB_FILE_READ_ERROR", NULL, "Read error occurred on block $block file $file: $error",
					log_data_string("block", (char[]){block->letter, '\0'}), log_data_string("file", block->filepath),
					log_data_string("error", strerror(saved_errno)));
		}
		safe_free(line);
		udb_record_free_tree(candidate);
		block->load_state = UDB_LOAD_FAILED;
		fclose(fp);
		return 0;
	}

	safe_free(line);

	if (fclose(fp) != 0)
	{
		int saved_errno = errno;
		udb_log(ULOG_ERROR, "UDB_FILE_CLOSE_ERROR", NULL, "Close error occurred on block $block file $file: $error",
				log_data_string("block", (char[]){block->letter, '\0'}), log_data_string("file", block->filepath),
				log_data_string("error", strerror(saved_errno)));
		udb_record_free_tree(candidate);
		block->load_state = UDB_LOAD_FAILED;
		return 0;
	}

	unsigned int record_count = udb_record_count_tree(candidate);
	unsigned long checksum = 0;
	if (!udb_compute_tree_checksum(candidate, &checksum))
	{
		udb_log(ULOG_ERROR, "UDB_FILE_CHECKSUM_FAILED", NULL, "Failed to compute checksum for block $block file $file",
				log_data_string("block", (char[]){block->letter, '\0'}), log_data_string("file", block->filepath));
		udb_record_free_tree(candidate);
		block->load_state = UDB_LOAD_FAILED;
		return 0;
	}

	udb_block_replace_tree(ctx, block, candidate, record_count);
	block->checksum = checksum;

	for (UdbRecord *curr = block->tree->child; curr; curr = curr->sibling)
	{
		for (UdbRecord *sub = curr->child; sub; sub = sub->sibling)
			udb_apply_special_record(ctx, block, sub, 1);
		udb_apply_special_record(ctx, block, curr, 1);
	}

	block->load_state = UDB_LOAD_SUCCESS;

	struct stat st;
	if (stat(block->filepath, &st) == 0)
	{
		block->filesize = st.st_size;
		block->modified_at = st.st_mtime;
	}

	char logbuf[512];
	snprintf(logbuf, sizeof(logbuf), "Loaded block %c from %s (%u records)", block->letter, block->filepath,
			 block->record_count);
	udb_log(ULOG_INFO, "UDB_FILE_LOADED", NULL, "$msg", log_data_string("msg", logbuf));

	if (block->letter == 'L')
		udb_sync_snomask_filter();

	return 1;
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */
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

	Client *client;
	list_for_each_entry(client, &client_list, client_node)
	{
		if (IsServer(client) && client != source)
		{
			if (udb_ctx && udb_ctx->links)
			{
				UdbRecord *srv_rec = udb_record_find(udb_ctx, client->name, udb_ctx->links);
				if (srv_rec)
				{
					UdbRecord *opt_rec = udb_record_find(udb_ctx, LKEY_OPTIONS, srv_rec);
					if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG))
					{
						sendto_one(client, NULL, ":%s NOTICE %s :[UDB Debug] %s", me.id, client->id, buf);
					}
				}
			}
		}
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

/* Runtime effects: special-record dispatch only */
/* Inlined: udb_effects.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Runtime Effects Dispatch & Live State Reconciliation
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static int udb_apply_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new)
{
	if (!rec)
		return 0;
	if (block->letter == 'N')
	{
		UdbRecord *nick_rec = rec->parent == block->tree ? rec : rec->parent;
		Client *client = find_user(nick_rec->key, NULL);
		if (client && MyUser(client))
		{
			udb_nick_apply(client, nick_rec, is_new);
		}
	}
	else if (block->letter == 'C')
	{
		udb_channel_apply_record(ctx, block, rec, is_new);
	}
	else if (block->letter == 'I')
	{
		udb_ips_apply_effect(ctx, block, rec, is_new);
	}
	else if (block->letter == 'S' || block->letter == 'L')
	{
		udb_config_apply_effect(ctx, block, rec);
	}
	else if (block->letter == 'K')
	{
		udb_lines_apply_effect(ctx, block, rec, is_new);
	}
	return 1;
}

static void udb_remove_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (!rec)
		return;
	if (block->letter == 'N')
	{
		udb_nick_remove_record(block, rec);
	}
	else if (block->letter == 'C')
	{
		udb_channel_remove_record(ctx, block, rec);
	}
	else if (block->letter == 'I')
	{
		udb_ips_remove_effect(ctx, block, rec);
	}
	else if (block->letter == 'S' || block->letter == 'L')
	{
		udb_config_remove_effect(ctx, block, rec);
	}
	else if (block->letter == 'K')
	{
		udb_lines_remove_effect(ctx, block, rec);
	}
}

static void udb_tree_effects_walk(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, unsigned int depth, int remove)
{
	UdbRecord *child;
	int apply = 0;

	if (!rec)
		return;
	for (child = rec->child; child; child = child->sibling)
		udb_tree_effects_walk(ctx, block, child, depth + 1, remove);

	/* Select each runtime owner once; K patterns are below their line type. */
	switch (block->letter)
	{
	case 'N':
		apply = depth == 1;
		break;
	case 'C':
	case 'I':
		apply = depth == (remove ? 1 : 2);
		break;
	case 'K':
		apply = depth == 2;
		break;
	case 'S':
	case 'L':
		apply = 1;
		break;
	}
	if (apply)
	{
		if (remove)
			udb_remove_special_record(ctx, block, rec);
		else
			udb_apply_special_record(ctx, block, rec, 1);
	}
}

static void udb_apply_tree_effects(UdbContext *ctx, UdbBlock *block)
{
	if (ctx && block && block->tree)
		udb_tree_effects_walk(ctx, block, block->tree, 0, 0);
}

static void udb_remove_tree_effects(UdbContext *ctx, UdbBlock *block)
{
	if (ctx && block && block->tree)
		udb_tree_effects_walk(ctx, block, block->tree, 0, 1);
}

/* End of udb_effects.c.inc */

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
	UdbHelloPeer *next;
};

#define UDB_HEL_WAITING 1
#define UDB_HEL_CONFIRMED 2
#define UDB_HEL_UNSUPPORTED 3

static UdbHelloPeer *udb_hello_peers = NULL;

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

static int udb_sync_hello_start(Client *server)
{
	UdbHelloPeer *peer;
	const char *propagator;

	if (!server || !IsServer(server) || !MyConnect(server))
		return 0;
	peer = udb_hello_peer(server, 1);
	if (peer->state)
		return 0;
	peer->state = UDB_HEL_WAITING;
	peer->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	propagator = udb_selected_propagator(udb_ctx);
	return udb_send_db_to_one(server, ":%s DB %s HEL 4 %s", me.id, server->id, propagator ? propagator : "?");
}

static int udb_sync_hello_ack(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 1);

	if (peer->state != UDB_HEL_WAITING)
		return 0;
	peer->state = UDB_HEL_CONFIRMED;
	udb_log(ULOG_INFO, "UDB_HEL_CONFIRMED", server, "UDB HEL 4 capability confirmed for directly linked server");
	udb_sync_to_server(server);
	return 1;
}

static void udb_sync_abort(UdbBlock *block, const char *reason)
{
	if (!block || !block->session)
		return;
	udb_log(ULOG_WARNING, "UDB_SYNC_ABORT", block->session->peer, "Aborted staged sync of block $block: $reason",
			log_data_string("block", (char[]){block->letter, '\0'}), log_data_string("reason", reason));
	udb_sync_session_free(block);
}

static int udb_sync_begin(UdbBlock *block, Client *peer, const char *txid)
{
	UdbSyncSession *session;
	int inact =
		(udb_cfg && udb_cfg->sync_inactivity_timeout) ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT;
	int abs_to =
		(udb_cfg && udb_cfg->sync_absolute_timeout) ? udb_cfg->sync_absolute_timeout : UDB_SYNC_ABSOLUTE_TIMEOUT;
	size_t txid_len;
	const char *p;

	if (!block)
		return UDB_ERR_NO_BLOCK;
	if (!peer || !txid || !*txid)
		return UDB_ERR_PARAMS;

	txid_len = strlen(txid);
	if (txid_len > UDB_TXID_MAX)
		return UDB_ERR_PARAMS;

	for (p = txid; *p; p++)
	{
		unsigned char c = (unsigned char)*p;
		if (!isalnum(c) && c != '-' && c != '_')
			return UDB_ERR_PARAMS;
	}

	if (block->session)
		return UDB_ERR_SYNC_ACTIVE;

	session = safe_alloc(sizeof(*session));
	session->peer = peer;
	memcpy(session->txid, txid, txid_len + 1);
	session->started_at = time(NULL);
	session->last_activity = session->started_at;
	session->deadline = session->started_at + inact;
	session->absolute_deadline = session->started_at + abs_to;
	session->tree = udb_record_create(NULL);
	session->tree->block_idx = (unsigned char)udb_block_letter_to_index(block->letter);
	safe_strdup(session->tree->key, "UDB");
	session->tree->is_dynamic_key = 1;
	session->received_puts = 0;
	session->received_bytes = 0;
	session->record_count = 0;
	block->session = session;
	block->syncing_from = peer;
	return 0;
}

static int udb_sync_put(UdbBlock *block, Client *peer, const char *txid, const char *path, const char *data)
{
	UdbSyncSession *session = block ? block->session : NULL;
	char *line;
	time_t now = time(NULL);
	int inact =
		(udb_cfg && udb_cfg->sync_inactivity_timeout) ? udb_cfg->sync_inactivity_timeout : UDB_SYNC_INACTIVITY_TIMEOUT;
	size_t max_bytes =
		(udb_cfg && udb_cfg->max_staged_bytes) ? udb_cfg->max_staged_bytes : UDB_DEFAULT_MAX_STAGED_BYTES;
	unsigned int max_records =
		(udb_cfg && udb_cfg->max_staged_records) ? udb_cfg->max_staged_records : UDB_DEFAULT_MAX_STAGED_RECORDS;
	int parse_ok;

	if (!session || session->peer != peer || strcmp(session->txid, txid))
	{
		if (session && session->peer == peer)
			udb_sync_abort(block, "invalid PUT sequence");
		return UDB_ERR_NO_SYNC;
	}
	if (now >= session->deadline)
	{
		udb_sync_abort(block, "inactivity sync timeout exceeded");
		return UDB_ERR_PARAMS;
	}
	if (now >= session->absolute_deadline)
	{
		udb_sync_abort(block, "absolute sync timeout exceeded");
		return UDB_ERR_PARAMS;
	}
	if (!udb_record_fits_limits(path, data))
	{
		udb_sync_abort(block, "PUT record exceeds limits");
		return UDB_ERR_PARAMS;
	}
	session->received_puts++;
	size_t payload_len = (path ? strlen(path) : 0) + (data ? strlen(data) : 0);
	session->received_bytes += payload_len;
	if (session->received_bytes > max_bytes)
	{
		udb_sync_abort(block, "staged byte limit exceeded");
		return UDB_ERR_PARAMS;
	}
	line = safe_alloc(UDB_RECORD_LINE_MAX + 2);
	if (snprintf(line, UDB_RECORD_LINE_MAX + 2, "%s %s", path ? path : "", data ? data : "") >=
		(int)UDB_RECORD_LINE_MAX)
	{
		safe_free(line);
		udb_sync_abort(block, "invalid PUT payload");
		return UDB_ERR_PARAMS;
	}
	parse_ok = udb_stage_parse_line(block, session, line);
	safe_free(line);
	if (!parse_ok)
	{
		udb_sync_abort(block, "invalid PUT payload");
		return UDB_ERR_PARAMS;
	}
	if (session->record_count > max_records)
	{
		udb_sync_abort(block, "staged record count limit exceeded");
		return UDB_ERR_PARAMS;
	}
	session->last_activity = now;
	session->deadline = now + inact;
	return 0;
}

static int udb_sync_end(UdbContext *ctx, UdbBlock *block, Client *peer, const char *txid, const char *checksum,
						unsigned long *digest)
{
	UdbSyncSession *session = block ? block->session : NULL;
	unsigned long received_digest;

	if (!session || session->peer != peer || strcmp(session->txid, txid))
	{
		if (session && session->peer == peer)
			udb_sync_abort(block, "invalid END sequence");
		return UDB_ERR_NO_SYNC;
	}
	time_t now = time(NULL);
	if (now >= session->deadline)
	{
		udb_sync_abort(block, "inactivity sync timeout exceeded");
		return UDB_ERR_PARAMS;
	}
	if (now >= session->absolute_deadline)
	{
		udb_sync_abort(block, "absolute sync timeout exceeded");
		return UDB_ERR_PARAMS;
	}
	session->record_count = udb_record_count_tree(session->tree);
	if (!udb_compute_tree_checksum(session->tree, digest))
	{
		udb_sync_abort(block, "digest calculation failure");
		return UDB_ERR_FATAL;
	}
	UdbSnapshotResult snap_res = UDB_SNAPSHOT_FAILED_BEFORE_COMMIT;
	if (!udb_checksum_parse(checksum, &received_digest) || *digest != received_digest ||
		(snap_res = udb_file_write_snapshot(block, session->tree, session->record_count)) ==
			UDB_SNAPSHOT_FAILED_BEFORE_COMMIT ||
		!udb_block_commit_stage(ctx, block, session, *digest))
	{
		udb_sync_abort(block, "digest or persistence failure");
		return UDB_ERR_FATAL;
	}
	return 0;
}

static void udb_sync_ack(Client *peer, const char *block)
{
	udb_log(ULOG_INFO, "UDB_SYNC_ACK", peer, "Staged sync acknowledged for block $block",
			log_data_string("block", block));
}

static int udb_sync_send_tree(Client *server, UdbRecord *rec, int depth, char *pathbuf, size_t pathlen, char letter,
							  const char *txid)
{
	size_t orig_len;
	UdbRecord *child;
	char encoded_key[UDB_COMPONENT_ENCODED_MAX + 1];

	if (!rec || !rec->key)
		return 1;
	orig_len = strlen(pathbuf);
	if (!udb_path_encode_component(rec->key, encoded_key, sizeof(encoded_key)))
		return 0;
	size_t enc_len = strlen(encoded_key);
	if (depth > 0)
	{
		if (orig_len + 2 + enc_len >= pathlen)
			return 0;
		strlcat(pathbuf, "::", pathlen);
	}
	else
	{
		if (enc_len >= pathlen)
			return 0;
	}
	strlcat(pathbuf, encoded_key, pathlen);
	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		char numbuf[32];
		const char *val_str;
		int ok;

		if (rec->data_str)
		{
			val_str = rec->data_str;
		}
		else
		{
			snprintf(numbuf, sizeof(numbuf), "*%lu", rec->data_num);
			val_str = numbuf;
		}

		if (!udb_record_fits_limits(pathbuf, val_str))
		{
			pathbuf[orig_len] = '\0';
			return 0;
		}

		if (rec->data_str)
			ok = udb_send_db_to_one(server, ":%s DB %s PUT %c %s %s :%s", me.id, server->id, letter, txid, pathbuf,
									rec->data_str);
		else
			ok = udb_send_db_to_one(server, ":%s DB %s PUT %c %s %s *%lu", me.id, server->id, letter, txid, pathbuf,
									rec->data_num);
		if (!ok)
		{
			pathbuf[orig_len] = '\0';
			return 0;
		}
	}
	for (child = rec->child; child; child = child->sibling)
	{
		if (!udb_sync_send_tree(server, child, depth + 1, pathbuf, pathlen, letter, txid))
		{
			pathbuf[orig_len] = '\0';
			return 0;
		}
	}
	pathbuf[orig_len] = '\0';
	return 1;
}

static int udb_sync_send_stage(Client *server, UdbBlock *block)
{
	char txid[UDB_TXID_MAX + 1];
	char pathbuf[UDB_RECORD_PATH_MAX + 1] = "";
	UdbRecord *rec;
	int success = 1;

	/* HEL confirms protocol support; snapshots require the selected data source. */
	if (!udb_peer_authorizes_us(server) || !block)
		return 0;
	snprintf(txid, sizeof(txid), "%08lx", ++udb_sync_txid);
	if (!udb_send_db_to_one(server, ":%s DB %s BEGIN %c %s %08lX", me.id, server->id, block->letter, txid,
							block->checksum))
	{
		udb_log(ULOG_ERROR, "UDB_SYNC_SEND_FAILED", server, "Failed to send staged sync BEGIN for block $block",
				log_data_string("block", (char[]){block->letter, '\0'}));
		return 0;
	}
	if (block->tree)
	{
		for (rec = block->tree->child; rec; rec = rec->sibling)
		{
			if (!udb_sync_send_tree(server, rec, 0, pathbuf, sizeof(pathbuf), block->letter, txid))
			{
				success = 0;
				break;
			}
		}
	}
	if (!success)
	{
		udb_log(ULOG_ERROR, "UDB_SYNC_SEND_FAILED", server,
				"Failed to serialize/send tree records during staged sync for block $block, aborting transfer",
				log_data_string("block", (char[]){block->letter, '\0'}));
		udb_send_db_to_one(server, ":%s DB %s ERR PUT %d %c", me.id, server->id, UDB_ERR_FATAL, block->letter);
		return 0;
	}
	if (!udb_send_db_to_one(server, ":%s DB %s END %c %s %08lX", me.id, server->id, block->letter, txid,
							block->checksum))
	{
		udb_log(ULOG_ERROR, "UDB_SYNC_SEND_FAILED", server, "Failed to send staged sync END for block $block",
				log_data_string("block", (char[]){block->letter, '\0'}));
		return 0;
	}
	return 1;
}

EVENT(udb_sync_timeout_event)
{
	UdbBlock *block;
	UdbHelloPeer *peer, *next;
	time_t now = time(NULL);

	for (block = udb_ctx ? udb_ctx->block_list : NULL; block; block = block->next)
	{
		if (block->session)
		{
			if (block->session->deadline <= now)
				udb_sync_abort(block, "inactivity timeout");
			else if (block->session->absolute_deadline <= now)
				udb_sync_abort(block, "absolute timeout");
		}
	}
	for (peer = udb_hello_peers; peer; peer = next)
	{
		next = peer->next;
		if (peer->state == UDB_HEL_WAITING && peer->deadline <= now)
		{
			Client *c = peer->peer;
			peer->state = UDB_HEL_UNSUPPORTED;
			udb_log(ULOG_ERROR, "UDB_HEL_TIMEOUT", c,
					"No UDB HEL 4 acknowledgement from directly linked server; link aborted");
			if (c)
			{
				exit_client_fmt(c, NULL, "Link aborted: server does not support UDB protocol (HEL timeout)");
			}
		}
	}
}

static void udb_sync_server_quit(Client *client)
{
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
		if (block->session && block->session->peer == client)
			udb_sync_abort(block, "peer quit");
		else if (block->syncing_from == client)
			block->syncing_from = NULL;
}

/* End of udb_sync.c.inc */

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

static const char *udb_selected_propagator(UdbContext *ctx)
{
	static char selected_buf[HOSTLEN + 1];

	/* 1. Priority 1: Explicit local override in unrealircd.conf */
	if (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator)
		return udb_cfg->propagator;

	/* 2. Priority 2: Priority list in S::propagator */
	if (ctx && ctx->propagator_setting && *ctx->propagator_setting)
	{
		char *list_copy = NULL;
		char *srv, *saveptr;
		char *first_srv = NULL;

		safe_strdup(list_copy, ctx->propagator_setting);
		for (srv = strtok_r(list_copy, ",", &saveptr); srv; srv = strtok_r(NULL, ",", &saveptr))
		{
			while (*srv == ' ')
				srv++;
			char *end = srv + strlen(srv) - 1;
			while (end > srv && *end == ' ')
				*end-- = '\0';
			if (!*srv)
				continue;

			if (!first_srv)
				first_srv = srv;

			/* Check if online in UnrealIRCd or if it is the local server */
			if (find_server(srv, NULL) || (me.name[0] && !strcasecmp(srv, me.name)))
			{
				strlcpy(selected_buf, srv, sizeof(selected_buf));
				safe_free(list_copy);
				return selected_buf;
			}
		}
		if (first_srv)
		{
			strlcpy(selected_buf, first_srv, sizeof(selected_buf));
			safe_free(list_copy);
			return selected_buf;
		}
		safe_free(list_copy);
	}

	return NULL;
}

static int udb_is_propagator(UdbContext *ctx, Client *server)
{
	const char *selected;

	if (!server || !IsServer(server))
		return 0;
	selected = udb_selected_propagator(ctx);
	if (selected && !strcasecmp(server->name, selected))
	{
		ctx->propagator = server;
		return 1;
	}
	return 0;
}

static UdbBlock *udb_mutation_path_block(UdbContext *ctx, const char *path)
{
	const char *component;
	const char *separator;
	size_t len;
	UdbBlock *block;

	if (!path || !udb_record_fits_limits(path, NULL))
		return NULL;

	len = strlen(path);
	if (len < 4 || len > UDB_RECORD_PATH_MAX || path[1] != ':' || path[2] != ':' || !path[3])
		return NULL;

	block = udb_block_by_letter(ctx, path[0]);
	if (!block)
		return NULL;

	component = path + 3;
	while ((separator = strstr(component, "::")))
	{
		if (separator == component || !separator[2])
			return NULL;
		char decoded[UDB_COMPONENT_RAW_MAX + 1];
		char comp_buf[UDB_COMPONENT_ENCODED_MAX + 1];
		size_t clen = separator - component;
		if (clen >= sizeof(comp_buf))
			return NULL;
		memcpy(comp_buf, component, clen);
		comp_buf[clen] = '\0';
		if (!udb_path_decode_component(comp_buf, decoded, sizeof(decoded)))
			return NULL;
		component = separator + 2;
	}
	char decoded[UDB_COMPONENT_RAW_MAX + 1];
	if (!*component || !udb_path_decode_component(component, decoded, sizeof(decoded)))
		return NULL;

	return block;
}

static int udb_mutation_forward_ins(Client *source, Client *except, const char *target, const char *path,
									const char *data)
{
	return udb_sendto_confirmed_servers(except, ":%s DB %s INS %s %s", source->id, target, path, data);
}

/* The stored value already equals the mutation payload, so no runtime effect
 * may change: an identical INS must never revoke and re-apply effects. */
static int udb_record_data_equals(UdbRecord *rec, const char *data)
{
	unsigned long val;

	if (!rec || !data)
		return 0;
	if (*data == '*')
		return !rec->data_str && udb_strtoul_strict(data + 1, &val) && rec->data_num == val;
	if (!rec->data_str)
		return 0;
	return !strcmp(rec->data_str, data);
}

static int udb_mutation_forward_del(Client *source, Client *except, const char *target, const char *path)
{
	return udb_sendto_confirmed_servers(except, ":%s DB %s DEL %s", source->id, target, path);
}

static int udb_mutation_persist_error(Client *client, const char *subcmd, char letter)
{
	return udb_send_db_to_one(client, ":%s DB %s ERR %s %d %c", me.id, client->id, subcmd, UDB_ERR_FATAL, letter);
}

static void udb_mutation_ins(UdbContext *ctx, Client *client, const char *target, const char *path, const char *data,
							 int is_for_me, int is_broadcast)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	char letter = path && *path ? path[0] : '0';

	if (!block || !udb_record_fits_limits(path, data))
	{
		udb_protocol_params_error(client, "INS");
		return;
	}
	if (is_for_me)
	{
		if (block->session || (block->syncing_from && block->syncing_from != client))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (block->syncing_from != client && !udb_is_propagator(ctx, client))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		UdbRecord *old_rec;
		UdbRecord *tree;
		UdbRecord *rec = NULL;
		int unchanged;

		if (!udb_record_validate(block, path + 3, data))
		{
			udb_log(ULOG_WARNING, "UDB_INS_SCHEMA_REJECT", client,
					"Rejected S2S INS for invalid or unknown record: $path", log_data_string("path", path));
			udb_send_db_to_one(client, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_PARAMS, letter);
			return;
		}
		old_rec = udb_record_find_path(ctx, block, path + 3);
		unchanged = old_rec && udb_record_data_equals(old_rec, data);
		if (unchanged)
		{
			if (ctx->propagator && block->syncing_from == client)
			{
				udb_mutation_forward_ins(ctx->propagator, client, target, path, data);
				return;
			}
			if (!is_broadcast)
				return;
			udb_mutation_forward_ins(client, client, target, path, data);
			return;
		}
		tree = udb_record_clone_tree(block->tree, old_rec, &rec);
		rec = udb_record_insert_path(tree, path + 3, data);
		if (!rec)
		{
			udb_record_free_tree(tree);
			udb_mutation_persist_error(client, "INS", letter);
			return;
		}
		UdbSnapshotResult snap_res = udb_file_write_snapshot(block, tree, udb_record_count_tree(tree));
		if (snap_res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_record_free_tree(tree);
			udb_mutation_persist_error(client, "INS", letter);
			return;
		}
		/* Revoke the old value's effects only when the value actually changes:
		 * an identical INS must not churn channel modes nor revoke +q ranks. */
		if (old_rec && !unchanged)
		{
			if (block->letter != 'C' || old_rec->parent == block->tree ||
				(strcmp(old_rec->key, CKEY_MODES) && strcmp(old_rec->key, CKEY_TOPIC) &&
				 strcmp(old_rec->key, CKEY_OPTIONS)))
				udb_remove_special_record(ctx, block, old_rec);
		}
		udb_block_replace_tree(ctx, block, tree, udb_record_count_tree(tree));
		if (!unchanged)
		{
			udb_apply_special_record(ctx, block, rec, 1);
			if (block->letter == 'L')
				udb_sync_snomask_filter();
		}
		char logbuf[512];
		snprintf(logbuf, sizeof(logbuf), "Inserted record via S2S: %s -> %s", path, data);
		udb_log(ULOG_INFO, "UDB_INS_RECEIVED", client, "$msg", log_data_string("msg", logbuf));
		if (ctx->propagator && block->syncing_from == client)
		{
			udb_mutation_forward_ins(ctx->propagator, client, target, path, data);
			return;
		}
		if (!is_broadcast)
			return;
	}
	udb_mutation_forward_ins(client, client, target, path, data);
}

static void udb_mutation_del(UdbContext *ctx, Client *client, const char *target, const char *path, int is_for_me,
							 int is_broadcast)
{
	UdbBlock *block = udb_mutation_path_block(ctx, path);
	char letter = path && *path ? path[0] : '0';

	if (!block || !udb_record_fits_limits(path, NULL))
	{
		udb_protocol_params_error(client, "DEL");
		return;
	}
	if (is_for_me)
	{
		if (block->session || (block->syncing_from && block->syncing_from != client))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (!block->syncing_from && !udb_is_propagator(ctx, client))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		UdbRecord *old_rec = udb_record_find_path(ctx, block, path + 3);
		if (!old_rec)
		{
			if (ctx->propagator && block->syncing_from == client)
			{
				udb_mutation_forward_del(ctx->propagator, client, target, path);
				return;
			}
			if (!is_broadcast)
				return;
			udb_mutation_forward_del(client, client, target, path);
			return;
		}
		UdbRecord *candidate_rec = NULL;
		UdbRecord *candidate_line = NULL;
		UdbRecord *tree = udb_record_clone_tree(block->tree, old_rec, &candidate_rec);
		unsigned int record_count;
		if (block->letter == 'K' && candidate_rec && candidate_rec->parent && candidate_rec->parent->parent &&
			candidate_rec->parent->parent != tree)
			candidate_line = candidate_rec->parent;
		if (candidate_rec)
			udb_record_delete_tree(candidate_rec);
		record_count = udb_record_count_tree(tree);
		UdbSnapshotResult snap_res = udb_file_write_snapshot(block, tree, record_count);
		if (snap_res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_record_free_tree(tree);
			udb_mutation_persist_error(client, "DEL", letter);
			return;
		}
		if (old_rec)
			udb_remove_special_record(ctx, block, old_rec);
		udb_block_replace_tree(ctx, block, tree, record_count);
		if (candidate_line)
			udb_lines_apply_effect(ctx, block, candidate_line, 0);
		if (block->letter == 'L')
			udb_sync_snomask_filter();
		if (ctx->propagator && block->syncing_from == client)
		{
			udb_mutation_forward_del(ctx->propagator, client, target, path);
			return;
		}
		if (!is_broadcast)
			return;
	}
	udb_mutation_forward_del(client, client, target, path);
}

static void udb_mutation_drp(UdbContext *ctx, Client *client, const char *target, char letter, int is_for_me,
							 int is_broadcast)
{
	if (is_for_me)
	{
		UdbBlock *block = udb_block_by_letter(ctx, letter);
		if (!block)
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
			return;
		}
		if (block->session || (block->syncing_from && block->syncing_from != client))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (!udb_is_propagator(ctx, client))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		if (!block->tree->child)
		{
			if (!is_broadcast)
				return;
			udb_sendto_confirmed_servers(client, ":%s DB %s DRP %c", client->id, target, letter);
			return;
		}
		UdbRecord *tree = udb_record_clone_tree(block->tree, NULL, NULL);
		while (tree->child)
			udb_record_delete_tree(tree->child);
		UdbSnapshotResult snap_res = udb_file_write_snapshot(block, tree, 0);
		if (snap_res == UDB_SNAPSHOT_FAILED_BEFORE_COMMIT)
		{
			udb_record_free_tree(tree);
			udb_mutation_persist_error(client, "DRP", letter);
			return;
		}
		/* Reset removes the old block's runtime effects only after persistence. */
		udb_block_reset(ctx, block);
		udb_block_replace_tree(ctx, block, tree, 0);
		if (!is_broadcast)
			return;
	}
	udb_sendto_confirmed_servers(client, ":%s DB %s DRP %c", client->id, target, letter);
}

static void udb_mutation_opt(UdbContext *ctx, Client *client, const char *target, char letter, const char *modified_at,
							 int is_for_me, int is_broadcast)
{
	if (is_for_me)
	{
		UdbBlock *block = udb_block_by_letter(ctx, letter);
		if (!block)
		{
			udb_send_db_to_one(client, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
			return;
		}
		if (block->session)
		{
			udb_send_db_to_one(client, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
			return;
		}
		if (!udb_is_propagator(ctx, client))
		{
			udb_send_db_to_one(client, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
			return;
		}
		if (!udb_file_save_block(ctx, block))
		{
			udb_mutation_persist_error(client, "OPT", letter);
			return;
		}
		if (!is_broadcast)
			return;
	}
	if (modified_at)
		udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c %s", client->id, target, letter, modified_at);
	else
		udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c", client->id, target, letter);
}

/* End of udb_mutation.c.inc */

/* S2S protocol handler: DB command parsing, routing, and server sync */
/* Inlined: udb_protocol.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Server-to-Server (S2S) Protocol & HEL Capability Negotiation
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

static int udb_send_db_to_confirmed_servers(Client *except, const char *fmt, ...)
{
	Client *server;
	char *line;
	va_list args;
	int n;
	int sent = 0;

	line = safe_alloc(UDB_S2S_LINE_MAX);
	va_start(args, fmt);
	n = vsnprintf(line, UDB_S2S_LINE_MAX, fmt, args);
	va_end(args);

	if (n < 0 || (size_t)n >= UDB_S2S_LINE_MAX)
	{
		udb_log(ULOG_ERROR, "UDB_S2S_OVERSIZE_BROADCAST", except,
				"Oversized broadcast S2S frame ($length bytes) discarded to prevent truncation",
				log_data_integer("length", n));
		safe_free(line);
		return 0;
	}

	list_for_each_entry(server, &server_list, special_node)
	{
		if (server == except || (except && server == except->direction))
			continue;
		if (IsServer(server) && MyConnect(server) && udb_has_hello(server))
		{
			sendto_one(server, NULL, "%s", line);
			sent++;
		}
	}
	safe_free(line);
	return 1;
}

static int udb_sendto_confirmed_servers(Client *except, const char *fmt, ...)
{
	char *line;
	va_list args;
	int n;
	int sent = 0;

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
			{
				sendto_one(server, NULL, "%s", line);
				sent++;
			}
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

static void udb_protocol_params_error(Client *client, const char *subcmd)
{
	udb_send_db_to_one(client, ":%s DB %s ERR %s %d 0", me.id, client->id, subcmd ? subcmd : "0", UDB_ERR_PARAMS);
}

static int udb_sync_to_server(Client *server)
{
	UdbBlock *block = udb_ctx->block_list;
	if (!udb_has_hello(server))
		return 0;
	while (block)
	{
		if (!udb_send_db_to_one(server, ":%s DB %s INF %c %lX %lu", me.id, server->id, block->letter, block->checksum,
								(unsigned long)block->modified_at))
			return 0;
		block = block->next;
	}
	return 1;
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

/* Server SIDs identify servers independently of names, links, and frame order. */
static int udb_remote_wins_equal_timestamp(Client *server)
{
	return server && *server->id && *me.id && strcmp(server->id, me.id) > 0;
}

static int udb_hook_server_sync(Client *client)
{
	if (!client || !IsServer(client) || !MyConnect(client))
		return 0;
	udb_sync_hello_start(client);
	return 0;
}

int udb_hook_server_quit(Client *client, MessageTag *mtags)
{
	if (udb_ctx->propagator == client)
		udb_ctx->propagator = NULL;
	udb_sync_server_quit(client);
	return 0;
}

CMD_FUNC(cmd_db)
{
	/* Process DB protocol messages sent via server-to-server connection */

	if (parc < 4)
	{
		udb_send_db_to_one(client, ":%s DB %s ERR 0 %i 0", me.id, client->id, UDB_ERR_PARAMS);
		return;
	}

	const char *target = parv[1];
	const char *subcmd = parv[2];
	UdbContext *ctx = udb_ctx;
	Client *direct_peer = udb_direct_peer(client);
	char logbuf[512];

	if (!target || !*target || !subcmd || !*subcmd)
	{
		udb_protocol_params_error(client, subcmd);
		return;
	}

	/* HEL is the sole DB frame accepted before UDB capability confirmation. */
	if (!strcasecmp(subcmd, "HEL"))
	{
		if (!IsServer(client) || !MyConnect(client) || (strcmp(target, me.id) && strcmp(target, me.name)) || parc < 4 ||
			strcmp(parv[3], "4"))
			return;
		snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
		unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));
		if (parc == 5 && !strcasecmp(parv[4], "ACK"))
		{
			udb_sync_hello_ack(client);
			return;
		}
		const char *prop = (parc >= 5 && parv[4]) ? parv[4] : "?";
		udb_hello_peer(client, 1)->authorizes_us = !strcasecmp(prop, me.name) || !strcmp(prop, "?");
		udb_log(ULOG_INFO, "UDB_HEL_AUTHORIZATION", client,
				"Direct peer selected $propagator as its staged-sync source", log_data_string("propagator", prop));
		/* Each side sends its own request, so only an ACK confirms outbound data. */
		if (!udb_has_hello(client))
			udb_sync_hello_start(client);
		udb_send_db_to_one(client, ":%s DB %s HEL 4 ACK", me.id, client->id);
		return;
	}

	if (!direct_peer || !udb_has_hello(direct_peer))
	{
		udb_send_db_to_one(client, ":%s DB %s ERR %s %d 0", me.id, client->id, subcmd, UDB_ERR_FORBIDDEN);
		return;
	}

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
			if (parc < 6 || !udb_has_staged_sync(client))
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			if (udb_selected_propagator(ctx) && !udb_is_propagator(ctx, client))
			{
				udb_send_db_to_one(client, ":%s DB %s ERR BEGIN %d %c", me.id, client->id, UDB_ERR_FORBIDDEN,
								   parv[3] ? *parv[3] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[3]);
			if (!block)
			{
				if (is_for_me)
					udb_send_db_to_one(client, ":%s DB %s ERR BEGIN %d %c", me.id, client->id, UDB_ERR_NO_BLOCK,
									   parv[3] ? *parv[3] : '0');
				return;
			}
			if (is_for_me)
			{
				int error = udb_sync_begin(block, client, parv[4]);
				if (error)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR BEGIN %d %c", me.id, client->id, error,
									   parv[3] ? *parv[3] : '0');
					return;
				}
				if (!is_broadcast)
					return;
			}
			udb_sendto_confirmed_servers(client, ":%s DB %s BEGIN %s %s %s", client->id, target, parv[3], parv[4],
										 parv[5]);
		}
		break;

	case 'P':
		if (!strcasecmp(subcmd, "PUT"))
		{
			UdbBlock *block;
			int error;
			if (parc < 7 || !udb_has_staged_sync(client))
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			if (udb_selected_propagator(ctx) && !udb_is_propagator(ctx, client))
			{
				udb_send_db_to_one(client, ":%s DB %s ERR PUT %d %c", me.id, client->id, UDB_ERR_FORBIDDEN,
								   parv[3] ? *parv[3] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[3]);
			if (!block)
			{
				if (is_for_me)
					udb_send_db_to_one(client, ":%s DB %s ERR PUT %d %c", me.id, client->id, UDB_ERR_NO_BLOCK,
									   parv[3] ? *parv[3] : '0');
				return;
			}
			if (is_for_me)
			{
				error = udb_sync_put(block, client, parv[4], parv[5], parv[6]);
				if (error)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR PUT %d %c", me.id, client->id, error,
									   parv[3] ? *parv[3] : '0');
					return;
				}
				if (!is_broadcast)
					return;
			}
			udb_sendto_confirmed_servers(client, ":%s DB %s PUT %s %s %s :%s", client->id, target, parv[3], parv[4],
										 parv[5], parv[6]);
		}
		break;

	case 'E':
		if (!strcasecmp(subcmd, "END"))
		{
			UdbBlock *block;
			unsigned long digest;
			int error;
			if (parc < 6 || !udb_has_staged_sync(client))
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			if (udb_selected_propagator(ctx) && !udb_is_propagator(ctx, client))
			{
				udb_send_db_to_one(client, ":%s DB %s ERR END %d %c", me.id, client->id, UDB_ERR_FORBIDDEN,
								   parv[3] ? *parv[3] : '0');
				return;
			}
			block = udb_block_by_letter(ctx, *parv[3]);
			if (!block)
			{
				if (is_for_me)
					udb_send_db_to_one(client, ":%s DB %s ERR END %d %c", me.id, client->id, UDB_ERR_NO_BLOCK,
									   parv[3] ? *parv[3] : '0');
				return;
			}
			if (is_for_me)
			{
				error = udb_sync_end(ctx, block, client, parv[4], parv[5], &digest);
				if (error)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR END %d %c", me.id, client->id, error,
									   parv[3] ? *parv[3] : '0');
					return;
				}
				udb_send_db_to_one(client, ":%s DB %s ACK %c %s %08lX", me.id, client->id, *parv[3], parv[4], digest);
				if (!is_broadcast)
					return;
			}
			udb_sendto_confirmed_servers(client, ":%s DB %s END %s %s %s", client->id, target, parv[3], parv[4],
										 parv[5]);
		}
		else if (!strcasecmp(subcmd, "ERR"))
		{
			if (parc < 5)
				return;
			if (is_for_me)
			{
				unsigned long errcode = 0;
				if (!udb_strtoul_strict(parv[4], &errcode) || errcode > 255)
					errcode = UDB_ERR_FATAL;
				udb_log(ULOG_INFO, "UDB_EVENT", client, "Error from $client: cmd=$cmd err=$errcode",
						log_data_client("client", client), log_data_string("cmd", parv[3]),
						log_data_integer("errcode", (int)errcode));
				if (!is_broadcast)
					return;
			}
			if (parc >= 6)
			{
				udb_sendto_confirmed_servers(client, ":%s DB %s ERR %s %s %s", client->id, target, parv[3], parv[4],
											 parv[5]);
			}
			else
			{
				udb_sendto_confirmed_servers(client, ":%s DB %s ERR %s %s", client->id, target, parv[3], parv[4]);
			}
		}
		break;

	case 'A':
		if (!strcasecmp(subcmd, "ACK"))
		{
			if (parc < 6 || !udb_has_staged_sync(client))
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			if (is_for_me)
			{
				udb_sync_ack(client, parv[3]);
				if (!is_broadcast)
					return;
			}
			udb_sendto_confirmed_servers(client, ":%s DB %s ACK %s %s %s", client->id, target, parv[3], parv[4],
										 parv[5]);
		}
		break;

	case 'I':
		if (!strcasecmp(subcmd, "INF"))
		{
			if (parc < 6)
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			char letter = *parv[3];
			UdbBlock *block = udb_block_by_letter(ctx, letter);
			unsigned long crc32 = 0;
			time_t remote_ts = 0;

			if (!block)
			{
				if (is_for_me)
					udb_send_db_to_one(client, ":%s DB %s ERR INF %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
				return;
			}
			if (!udb_checksum_parse(parv[4], &crc32) || !udb_timestamp_parse(parv[5], &remote_ts))
			{
				udb_protocol_params_error(client, "INF");
				return;
			}

			if (is_for_me)
			{
				if (crc32 != block->checksum)
				{
					if (remote_ts > block->modified_at)
					{
						udb_send_db_to_one(client, ":%s DB %s RES %c", me.id, client->id, letter);
					}
					else if (remote_ts == block->modified_at && udb_remote_wins_equal_timestamp(client))
					{
						udb_send_db_to_one(client, ":%s DB %s RES %c", me.id, client->id, letter);
					}
				}
				if (!is_broadcast)
					return;
			}
			udb_sendto_confirmed_servers(client, ":%s DB %s INF %c %08lX %lu", client->id, target, letter, crc32,
										 (unsigned long)remote_ts);
		}
		else if (!strcasecmp(subcmd, "INS"))
		{
			if (parc < 5)
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			udb_mutation_ins(ctx, client, target, parv[3], parv[4], is_for_me, is_broadcast);
		}
		break;

	case 'R':
		if (!strcasecmp(subcmd, "RES"))
		{
			if (parc < 4)
				return;
			char letter = *parv[3];
			UdbBlock *block = udb_block_by_letter(ctx, letter);
			if (!udb_peer_authorizes_us(client))
			{
				udb_send_db_to_one(client, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
				return;
			}

			if (is_for_me)
			{
				if (!block)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
					return;
				}
				if (block->syncing_from && block->syncing_from != client)
				{
					udb_send_db_to_one(client, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE,
									   letter);
					return;
				}

				udb_sync_send_stage(client, block);
				block->syncing_from = NULL;

				if (!is_broadcast)
					return;
			}
			udb_sendto_confirmed_servers(client, ":%s DB %s RES %c", client->id, target, letter);
		}
		break;

	case 'D':
		if (!strcasecmp(subcmd, "DEL"))
		{
			if (parc < 4)
			{
				udb_protocol_params_error(client, subcmd);
				return;
			}
			udb_mutation_del(ctx, client, target, parv[3], is_for_me, is_broadcast);
		}
		else if (!strcasecmp(subcmd, "DRP"))
		{
			if (parc < 4)
				return;
			udb_mutation_drp(ctx, client, target, *parv[3], is_for_me, is_broadcast);
		}
		break;

	case 'O':
		if (!strcasecmp(subcmd, "OPT"))
		{
			if (parc < 4)
				return;
			udb_mutation_opt(ctx, client, target, *parv[3], parc >= 5 ? parv[4] : NULL, is_for_me, is_broadcast);
		}
		break;
	}
}

static int udb_protocol_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DB", cmd_db, MAXPARA, CMD_SERVER | CMD_BIGLINES);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, udb_hook_server_sync);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, udb_hook_server_quit);
	EventAdd(modinfo->handle, "udb_sync_timeout", udb_sync_timeout_event, NULL, 1000, 0);

	return 0;
}

/* End of udb_protocol.c.inc */

/* Nick management: registration, identification, ghost, vhost, oper */
/* Inlined: udb_nicks.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Nick Registration, SHA-256 Identification & Forced Nick Migrations
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
	int valid;
};

static ModDataInfo *udb_nick_password_cache_md = NULL;

static void udb_nick_password_cache_free(ModData *m)
{
	safe_free(m->ptr);
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
		safe_free(cache);
		moddata_local_client(client, udb_nick_password_cache_md).ptr = NULL;
	}
}

static void udb_nick_password_cache_set(Client *client, const char *nick)
{
	UdbNickPasswordCache *cache;

	if (!client || !nick || !udb_nick_password_cache_md)
		return;
	udb_nick_password_cache_clear(client);
	cache = safe_alloc(sizeof(*cache));
	strlcpy(cache->nick, nick, sizeof(cache->nick));
	cache->valid = 1;
	moddata_local_client(client, udb_nick_password_cache_md).ptr = cache;
}

static int udb_nick_password_cache_take(Client *client, const char *nick)
{
	UdbNickPasswordCache *cache;
	int valid;

	if (!client || !nick || !udb_nick_password_cache_md)
		return 0;
	cache = moddata_local_client(client, udb_nick_password_cache_md).ptr;
	if (!cache)
		return 0;
	if (strcasecmp(cache->nick, nick))
	{
		udb_nick_password_cache_clear(client);
		return 0;
	}
	valid = cache->valid;
	udb_nick_password_cache_clear(client);
	return valid;
}

static void udb_nick_set_vhost(Client *client, UdbRecord *vhost_rec)
{
	if (!client || !client->user || !vhost_rec || !vhost_rec->data_str)
		return;

	/* If the vhost is already active and set to this exact value, nothing to do */
	if (client->user->virthost && !strcmp(client->user->virthost, vhost_rec->data_str) && IsHidden(client) &&
		IsSetHost(client))
		return;

	userhost_save_current(client);
	safe_strdup(client->user->virthost, vhost_rec->data_str);
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
	if (udb_ip_reapply_vhost(client))
		return;

	userhost_save_current(client);

	if (*client->user->cloakedhost)
	{
		safe_strdup(client->user->virthost, client->user->cloakedhost);
	}
	else
	{
		safe_strdup(client->user->virthost, client->user->realhost);
	}

	client->umodes &= ~UMODE_SETHOST;

	if (IsUser(client))
	{
		sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, client->user->virthost);
	}
	if (MyConnect(client))
	{
		sendto_one(client, NULL, ":%s MODE %s :-t", client->name, client->name);
		udb_send_service_notice(client, SKEY_IPSERV, "*** Your vhost has been removed");
	}

	userhost_changed(client);
}

static void udb_nick_grant_oper(Client *client, UdbRecord *nick_rec, UdbRecord *oper_rec)
{
	if (!client || !oper_rec)
		return;

	const char *operclass = oper_rec->data_str;
	if (BadPtr(operclass))
		return;

	if (!find_operclass(operclass))
	{
		udb_log(ULOG_WARNING, "UDB_OPERCLASS_NOT_FOUND", client,
				"operclass '$operclass' for $client.details does not exist in unrealircd.conf",
				log_data_string("operclass", operclass));
		return;
	}

	if (IsOper(client))
	{
		const char *curr_class = get_operclass(client);
		if (curr_class && !strcmp(curr_class, operclass))
			return;
		udb_nick_revoke_oper(client);
	}

	make_oper(client, "UDB", operclass, NULL, 0, NULL, NULL, NULL);
}

static void udb_nick_revoke_oper(Client *client)
{
	long old_umodes;

	if (!client || !IsOper(client))
		return;

	old_umodes = client->umodes & ALL_UMODES;
	client->umodes &= ~UMODE_OPER;
	if (MyUser(client) && !list_empty(&client->special_node))
	{
		list_del(&client->special_node);
		INIT_LIST_HEAD(&client->special_node);
	}
	if (irccounts.operators > 0)
		irccounts.operators--;
	remove_oper_privileges(client, 0);
	send_umode_out(client, 1, old_umodes);
}

static void udb_nick_set_modes(Client *client, UdbRecord *nick_rec, UdbRecord *mode_rec, const char *modes)
{
	if (!modes)
		return;
	long m = set_usermode(modes);
	long old_umodes = client->umodes & ALL_UMODES;
	/* Oper status is controlled exclusively by N::oper. */
	client->umodes |= m & ~UMODE_OPER;
	send_umode_out(client, 1, old_umodes);
}

static void udb_nick_set_swhois(Client *client, UdbRecord *nick_rec, UdbRecord *swhois_rec)
{
	if (!client || !client->user || !swhois_rec || !swhois_rec->data_str)
		return;
	swhois_delete(client, "udb", "*", &me, NULL);
	swhois_add(client, "udb", 100, swhois_rec->data_str, &me, NULL);
}

static void udb_nick_set_snomasks(Client *client, UdbRecord *nick_rec, UdbRecord *snomask_rec)
{
	if (!client || !client->user || !snomask_rec || !snomask_rec->data_str)
		return;

	long old_umodes = client->umodes & ALL_UMODES;
	set_snomask(client, snomask_rec->data_str);

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

	send_umode_out(client, 1, old_umodes);
}

static void udb_nick_force_rename(Client *client, const char *nick_in_db)
{
	char newnick[32];
	char rand_suffix[6];

	gen_random_alnum(rand_suffix, 5);
	rand_suffix[5] = '\0';
	snprintf(newnick, sizeof(newnick), "Guest%s", rand_suffix);

	udb_send_service_notice(client, SKEY_NICKSERV,
							"This nickname (%s) has been registered or synced in the UDB database.", nick_in_db);
	udb_send_service_notice(client, SKEY_NICKSERV,
							"You have been renamed. If you are the owner, please identify: /NICK %s:Password",
							nick_in_db);

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

static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync)
{
	if (!client || !nick_rec)
		return;

	UdbRecord *forbid = udb_record_find(udb_ctx, NKEY_FORBID, nick_rec);
	if (forbid)
	{
		udb_nick_force_rename(client, nick_rec->key);
		return;
	}

	/* If this is a hot sync, check if the user is identified */
	if (is_hot_sync)
	{
		if (!has_user_mode(client, 'r'))
		{
			UdbRecord *pass_rec = udb_record_find(udb_ctx, NKEY_PASS, nick_rec);
			if (pass_rec)
			{
				udb_nick_force_rename(client, nick_rec->key);
			}
			return; /* Abort applying vhosts/opers to this unauthorized user */
		}
	}

	if (client->user)
	{
		strlcpy(client->user->account, nick_rec->key, sizeof(client->user->account));
	}

	long old_umodes = client->umodes & ALL_UMODES;
	client->umodes |= UMODE_REGNICK;

	UdbRecord *susp = udb_record_find(udb_ctx, NKEY_SUSPENDED, nick_rec);
	if (susp)
	{
		client->umodes |= set_usermode("S");
	}

	send_umode_out(client, 1, old_umodes);

	UdbRecord *vhost_rec = udb_record_find(udb_ctx, NKEY_VHOST, nick_rec);
	if (vhost_rec)
		udb_nick_set_vhost(client, vhost_rec);

	UdbRecord *oper_rec = udb_record_find(udb_ctx, NKEY_OPER, nick_rec);
	if (oper_rec)
		udb_nick_grant_oper(client, nick_rec, oper_rec);

	UdbRecord *modes_rec = udb_record_find(udb_ctx, NKEY_MODES, nick_rec);
	if (modes_rec && modes_rec->data_str)
	{
		udb_nick_set_modes(client, nick_rec, modes_rec, modes_rec->data_str);
	}

	UdbRecord *swhois_rec = udb_record_find(udb_ctx, NKEY_SWHOIS, nick_rec);
	if (swhois_rec)
		udb_nick_set_swhois(client, nick_rec, swhois_rec);

	UdbRecord *sno_rec = udb_record_find(udb_ctx, NKEY_SNOMASKS, nick_rec);
	if (sno_rec)
		udb_nick_set_snomasks(client, nick_rec, sno_rec);
}

static void udb_nick_strip(Client *client, UdbRecord *nick_rec)
{
	if (!client)
		return;

	if (client->user)
	{
		strlcpy(client->user->account, "*", sizeof(client->user->account));
	}

	udb_nick_revoke_oper(client);

	long old_umodes = client->umodes & ALL_UMODES;
	if (nick_rec)
	{
		UdbRecord *mode_rec = udb_record_find(udb_ctx, NKEY_MODES, nick_rec);
		if (mode_rec && mode_rec->data_str)
			client->umodes &= ~(set_usermode(mode_rec->data_str) & ~UMODE_OPER);
	}
	client->umodes &= ~UMODE_REGNICK;
	client->umodes &= ~set_usermode("S");
	send_umode_out(client, 1, old_umodes);

	set_snomask(client, NULL);

	udb_nick_remove_vhost(client);

	if (nick_rec)
	{
		UdbRecord *swhois_rec = udb_record_find(udb_ctx, NKEY_SWHOIS, nick_rec);
		if (swhois_rec && swhois_rec->data_str)
		{
			swhois_delete(client, "udb", "*", &me, NULL);
		}
	}
}

static void udb_nick_remove_record(UdbBlock *block, UdbRecord *rec)
{
	if (rec->parent != block->tree)
	{
		UdbRecord *nick_rec = rec->parent;
		Client *client = find_user(nick_rec->key, NULL);
		if (client && MyUser(client))
		{
			if (!strcmp(rec->key, NKEY_VHOST))
			{
				udb_nick_remove_vhost(client);
			}
			else if (!strcmp(rec->key, NKEY_OPER))
			{
				udb_nick_revoke_oper(client);
			}
			else if (!strcmp(rec->key, NKEY_SWHOIS))
			{
				swhois_delete(client, "udb", "*", &me, NULL);
			}
			else if (!strcmp(rec->key, NKEY_MODES))
			{
				long old_umodes = client->umodes & ALL_UMODES;
				if (rec->data_str)
					client->umodes &= ~(set_usermode(rec->data_str) & ~UMODE_OPER);
				UdbRecord *oper_rec = udb_record_find(udb_ctx, NKEY_OPER, nick_rec);
				if (oper_rec && IsOper(client))
					client->umodes |= OPER_MODES;
				send_umode_out(client, 1, old_umodes);
			}
			else if (!strcmp(rec->key, NKEY_SNOMASKS))
			{
				long old_umodes = client->umodes & ALL_UMODES;
				set_snomask(client, NULL);
				UdbRecord *oper_rec = udb_record_find(udb_ctx, NKEY_OPER, nick_rec);
				if (oper_rec && IsOper(client))
				{
					set_snomask(client, OPER_SNOMASK);
					if (client->user->snomask && *client->user->snomask)
					{
						client->umodes |= UMODE_SERVNOTICE;
						sendnumeric(client, RPL_SNOMASK, client->user->snomask);
					}
				}
				else
				{
					client->umodes &= ~UMODE_SERVNOTICE;
				}
				send_umode_out(client, 1, old_umodes);
			}
			else if (!strcmp(rec->key, NKEY_SUSPENDED))
			{
				long old_umodes = client->umodes & ALL_UMODES;
				client->umodes &= ~set_usermode("S");
				send_umode_out(client, 1, old_umodes);
			}
			else if (!strcmp(rec->key, NKEY_PASS))
			{
				udb_nick_strip(client, nick_rec);
			}
		}
	}
	else
	{
		Client *client = find_user(rec->key, NULL);
		if (client && MyUser(client))
		{
			udb_nick_strip(client, rec);
		}
	}
}

static UdbPasswordFailure *udb_password_failure_find(UdbRecord *profile_rec, Client *client, int create)
{
	UdbPasswordFailure *oldest = NULL;
	const char *ip = client ? client->ip : NULL;
	time_t now = TStime();
	int period = udb_cfg ? udb_cfg->flood_period : 60;
	unsigned int i;

	if (period <= 0)
		period = 60;
	if (!profile_rec || !profile_rec->key || BadPtr(ip))
		return NULL;
	for (i = 0; i < UDB_PASSWORD_FAILURE_SLOTS; i++)
	{
		UdbPasswordFailure *entry = &udb_password_failures[i];
		if (entry->since && now - entry->since >= period)
			memset(entry, 0, sizeof(*entry));
		if (entry->since && entry->block_idx == profile_rec->block_idx && !strcmp(entry->profile, profile_rec->key) &&
			!strcmp(entry->ip, ip))
			return entry;
		if (!entry->since)
			oldest = entry;
		else if (!oldest || entry->since < oldest->since)
			oldest = entry;
	}
	if (!create || !oldest)
		return NULL;
	memset(oldest, 0, sizeof(*oldest));
	strlcpy(oldest->profile, profile_rec->key, sizeof(oldest->profile));
	strlcpy(oldest->ip, ip, sizeof(oldest->ip));
	oldest->block_idx = profile_rec->block_idx;
	oldest->since = now;
	return oldest;
}

static int udb_password_flooded(UdbRecord *profile_rec, Client *client)
{
	UdbPasswordFailure *entry = udb_password_failure_find(profile_rec, client, 0);
	int attempts = udb_cfg ? udb_cfg->flood_attempts : 5;

	if (attempts <= 0)
		attempts = 5;
	return entry && entry->attempts >= (unsigned int)attempts;
}

static void udb_password_failure_record(UdbRecord *profile_rec, Client *client, int success)
{
	UdbPasswordFailure *entry = udb_password_failure_find(profile_rec, client, !success);

	if (!entry)
		return;
	if (success)
		memset(entry, 0, sizeof(*entry));
	else if (entry->attempts != (unsigned int)-1)
		entry->attempts++;
}

static void udb_nick_password_failure_notice(Client *client, UdbRecord *profile_rec, const char *nick)
{
	if (udb_password_flooded(profile_rec, client))
		udb_send_service_notice(client, SKEY_NICKSERV, "Too many failed password attempts for %s; try again later.",
								nick);
	else
		udb_send_service_notice(client, SKEY_NICKSERV, "Invalid password for %s.", nick);
}

static int udb_password_type(const char *challenge, const char *stored_pass, const char **hash)
{
	if (!strncmp(stored_pass, "argon2id:", 9))
	{
		*hash = stored_pass + 9;
		return !strcasecmp(challenge, "argon2id") || !*challenge ? AUTHTYPE_ARGON2 : AUTHTYPE_INVALID;
	}
	if (!strncmp(stored_pass, "sha256:", 7))
	{
		*hash = stored_pass + 7;
		return !strcasecmp(challenge, "sha256") || !*challenge ? UDB_AUTHTYPE_SHA256 : AUTHTYPE_INVALID;
	}
	if (!strncmp(stored_pass, "crypt:", 6))
	{
		*hash = stored_pass + 6;
		return !strcasecmp(challenge, "crypt") || !*challenge ? AUTHTYPE_UNIXCRYPT : AUTHTYPE_INVALID;
	}
	*hash = stored_pass;
	if (!strcasecmp(challenge, "argon2id"))
		return AUTHTYPE_ARGON2;
	if (!strcasecmp(challenge, "sha256"))
		return UDB_AUTHTYPE_SHA256;
	if (!strcasecmp(challenge, "crypt"))
		return AUTHTYPE_UNIXCRYPT;
	if (!*challenge && !strncmp(stored_pass, "$argon2id$", 10))
		return AUTHTYPE_ARGON2;
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

static int udb_check_password(const char *pass, UdbRecord *profile_rec, Client *client)
{
	UdbRecord *pass_rec;
	UdbRecord *chall_rec;
	const char *challenge = "";
	const char *stored_pass;
	const char *hash;
	AuthConfig as;
	int type;
	int success;

	if (!pass || !profile_rec || udb_password_flooded(profile_rec, client))
		return 0;
	pass_rec = udb_record_find(udb_ctx, NKEY_PASS, profile_rec);
	if (!pass_rec || BadPtr(pass_rec->data_str))
	{
		udb_password_failure_record(profile_rec, client, 0);
		return 0;
	}
	chall_rec = udb_record_find(udb_ctx, NKEY_CHALLENGE, profile_rec);
	if (chall_rec && chall_rec->data_str)
		challenge = chall_rec->data_str;
	stored_pass = pass_rec->data_str;
	type = udb_password_type(challenge, stored_pass, &hash);
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
		success = !strcasecmp(digest, hash);
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

static int udb_nick_access_allowed(Client *client, UdbRecord *nick_rec)
{
	UdbRecord *access_rec = udb_record_find(udb_ctx, NKEY_ACCESS, nick_rec);
	const char *p;

	if (!access_rec)
		return 1;
	if (BadPtr(access_rec->data_str) || !client || BadPtr(client->ip))
		return 0;
	for (p = access_rec->data_str; *p;)
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
	if (parc < 3)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "GHOST");
		return;
	}
	const char *target_nick = parv[1];
	const char *pass = parv[2];

	if (!udb_ctx || !udb_ctx->nicks)
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "UDB is not fully initialized.");
		return;
	}

	UdbRecord *nick_rec = udb_record_find(udb_ctx, target_nick, udb_ctx->nicks);
	if (!nick_rec)
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Nick %s is not registered.", target_nick);
		return;
	}

	if (!udb_check_password(pass, nick_rec, client))
	{
		udb_nick_password_failure_notice(client, nick_rec, target_nick);
		return;
	}
	if (!udb_nick_access_allowed(client, nick_rec))
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
								target_nick);
		return;
	}

	Client *target = find_client(target_nick, NULL);
	if (target)
	{
		if (target == client)
		{
			udb_send_service_notice(client, SKEY_NICKSERV, "You cannot ghost yourself.");
			return;
		}
		udb_send_service_notice(client, SKEY_NICKSERV, "Ghosting %s...", target_nick);
		exit_client(target, NULL, "GHOST command used");
	}
	else
	{
		udb_send_service_notice(client, SKEY_NICKSERV, "%s is not online.", target_nick);
	}
}

CMD_OVERRIDE_FUNC(udb_override_nick)
{
	if (parc <= 1)
		goto passthrough;

	const char *nick = parv[1];
	char clean_nick[NICKLEN + 64];
	strlcpy(clean_nick, nick, sizeof(clean_nick));
	char *pass_colon = strchr(clean_nick, ':');
	char *pass_bang = strchr(clean_nick, '!');

	char *pass = NULL;
	int force_ghost = 0;

	if (pass_colon && (!pass_bang || pass_colon < pass_bang))
	{
		pass = pass_colon;
		force_ghost = 0;
	}
	else if (pass_bang && (!pass_colon || pass_bang < pass_colon))
	{
		pass = pass_bang;
		force_ghost = 1;
	}

	if (!pass)
		goto passthrough;

	*pass++ = '\0';
	if (client->local)
		safe_strdup(client->local->passwd, pass);

	UdbRecord *rec = (udb_ctx && udb_ctx->nicks) ? udb_record_find(udb_ctx, clean_nick, udb_ctx->nicks) : NULL;
	Client *acptr = rec ? find_client(clean_nick, NULL) : NULL;

	/* The core NICK path checks the password too. Only pre-check when a
	 * collision needs the password for ghost/recovery, and stop on failure so
	 * the flood tracker is consumed exactly once. */
	if (rec && acptr && acptr != client)
	{
		if (!udb_check_password(pass, rec, client))
		{
			udb_nick_password_failure_notice(client, rec, clean_nick);
			sendnumeric(client, ERR_ERRONEUSNICKNAME, clean_nick, "Nickname requires a valid UDB password.");
			return;
		}
		if (!udb_nick_access_allowed(client, rec))
		{
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									clean_nick);
			sendnumeric(client, ERR_ERRONEUSNICKNAME, clean_nick,
						"Nickname requires a valid UDB password and authorized IP.");
			return;
		}
		udb_nick_password_cache_set(client, clean_nick);
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

	const char *new_parv[MAXPARA + 1];
	for (int i = 0; i < parc; i++)
		new_parv[i] = parv[i];
	new_parv[1] = clean_nick;
	CallCommandOverride(ovr, clictx, client, recv_mtags, parc, new_parv);
	return;

passthrough:
	CALL_NEXT_COMMAND_OVERRIDE();
}

static int udb_hook_can_use_nick(Client *client, const char *newnick, const char **reject_reason)
{
	if (!udb_ctx || !udb_ctx->nicks)
		return HOOK_CONTINUE;
	if (!MyConnect(client))
		return HOOK_CONTINUE;

	UdbRecord *nick_rec = udb_record_find(udb_ctx, newnick, udb_ctx->nicks);
	if (nick_rec)
	{
		UdbRecord *forbid = udb_record_find(udb_ctx, NKEY_FORBID, nick_rec);
		if (forbid)
		{
			udb_send_service_notice(client, SKEY_NICKSERV, "This nickname is forbidden.");
			*reject_reason = "This nick is forbidden.";
			return HOOK_DENY;
		}

		/* If client is already this nick and identified with +r, allow without re-entering password */
		if (!strcasecmp(client->name, newnick) && has_user_mode(client, 'r'))
		{
			udb_nick_password_cache_clear(client);
			if (udb_nick_access_allowed(client, nick_rec))
				return HOOK_CONTINUE;
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									newnick);
			return HOOK_DENY;
		}

		if (udb_nick_password_cache_take(client, newnick))
			return HOOK_CONTINUE;

		const char *pass = client->local ? client->local->passwd : NULL;
		if (!pass)
		{
			udb_send_service_notice(client, SKEY_NICKSERV,
									"Nickname is unavailable: This nick is registered and requires a password and an "
									"authorized IP. Use /NICK %s:Password",
									newnick);
		}
		else if (udb_check_password(pass, nick_rec, client))
		{
			if (udb_nick_access_allowed(client, nick_rec))
				return HOOK_CONTINUE;
			udb_send_service_notice(client, SKEY_NICKSERV, "Access to %s is not permitted from your IP address.",
									newnick);
		}
		else
		{
			udb_nick_password_failure_notice(client, nick_rec, newnick);
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

static int udb_hook_nick_change(Client *client, MessageTag *mtags, const char *newnick)
{
	if (!udb_ctx || !udb_ctx->nicks)
		return 0;
	if (!MyConnect(client))
		return 0;

	UdbRecord *old_rec = udb_record_find(udb_ctx, client->name, udb_ctx->nicks);
	UdbRecord *new_rec = udb_record_find(udb_ctx, newnick, udb_ctx->nicks);

	if (old_rec && old_rec != new_rec)
	{
		udb_nick_strip(client, old_rec);
	}

	return 0;
}

static int udb_hook_post_nick_change(Client *client, MessageTag *recv_mtags, const char *oldnick)
{
	if (!udb_ctx || !udb_ctx->nicks)
		return 0;
	if (!MyConnect(client))
		return 0;

	UdbRecord *new_rec = udb_record_find(udb_ctx, client->name, udb_ctx->nicks);
	if (new_rec)
	{
		udb_nick_apply(client, new_rec, 0);
	}
	return 0;
}

static int udb_hook_local_connect(Client *client)
{
	if (!udb_ctx || !udb_ctx->nicks)
		return 0;

	UdbRecord *nick_rec = udb_record_find(udb_ctx, client->name, udb_ctx->nicks);
	if (nick_rec)
	{
		udb_nick_apply(client, nick_rec, 0);
	}
	return 0;
}

int udb_nicks_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_nick_password_cache";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_nick_password_cache_free;
	udb_nick_password_cache_md = ModDataAdd(modinfo->handle, mreq);

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

typedef struct UdbPendingChannelAuth UdbPendingChannelAuth;
typedef struct UdbChannelModeState UdbChannelModeState;
typedef struct UdbInviteGrant UdbInviteGrant;
typedef struct UdbBanOwner UdbBanOwner;
typedef struct UdbBanSnapshot UdbBanSnapshot;

struct UdbPendingChannelAuth
{
	UdbPendingChannelAuth *next;
	char channel[CHANNELLEN + 1];
};

struct UdbChannelModeState
{
	char *value;
};

struct UdbInviteGrant
{
	UdbInviteGrant *next;
	char channel[CHANNELLEN + 1];
	time_t expires;
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

#define UDB_INVITE_GRANT_TTL 300

static ModDataInfo *udb_channel_auth_pending_md = NULL;
static ModDataInfo *udb_channel_auth_member_md = NULL;
static ModDataInfo *udb_channel_modes_md = NULL;
static ModDataInfo *udb_channel_invite_grant_md = NULL;
static ModDataInfo *udb_channel_ban_owners_md = NULL;

/* Forward declarations */
static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg);
static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key);
static int udb_hook_local_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_remote_join(Client *client, Channel *channel, MessageTag *mtags);
static int udb_hook_pre_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf,
								 const char *parabuf, time_t sendts, int samode);
static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic);
CMD_OVERRIDE_FUNC(udb_override_invite);
CMD_OVERRIDE_FUNC(udb_override_mode);

static int udb_channel_is_identified_founder(Client *client, UdbRecord *chan_rec)
{
	UdbRecord *founder_rec;

	if (!client || !chan_rec)
		return 0;
	founder_rec = udb_record_find(udb_ctx, CKEY_FOUNDER, chan_rec);
	return founder_rec && founder_rec->data_str && !strcasecmp(client->name, founder_rec->data_str) &&
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

	if (state)
	{
		safe_free(state->value);
		safe_free(state);
	}
	m->ptr = NULL;
}

static void udb_channel_invite_grant_free(ModData *m)
{
	UdbInviteGrant *grant = m->ptr;

	while (grant)
	{
		UdbInviteGrant *next = grant->next;
		safe_free(grant);
		grant = next;
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
	char *p, *param;
	int myparc = 1;
	int i;
	char *myparv[512];
	Client *source;

	if (!channel || !modes || !*modes || !do_mode)
		return;
	source = udb_service_source(SKEY_CHANSERV);
	memset(myparv, 0, sizeof(myparv));
	myparv[0] = raw_strdup(modes);
	strlcpy(buf, parameters ? parameters : "", sizeof(buf));
	for (param = strtoken(&p, buf, " "); param && myparc < (int)(sizeof(myparv) / sizeof(myparv[0])) - 1;
		 param = strtoken(&p, NULL, " "))
		myparv[myparc++] = raw_strdup(param);
	myparv[myparc] = NULL;

	do_mode(channel, source, mtags, myparc, (const char **)myparv, 0, 1);
	for (i = 0; i < myparc; i++)
		safe_free(myparv[i]);

	if (parameters && *parameters)
		udb_send_to_debugs(NULL, "Mode change on %s: %s %s", channel->name, modes, parameters);
	else
		udb_send_to_debugs(NULL, "Mode change on %s: %s", channel->name, modes);
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

static void udb_channel_remove_modes(Channel *channel, const char *fallback_value)
{
	UdbChannelModeState *state;

	(void)fallback_value;
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

static int udb_channel_is_persistent(UdbContext *ctx, UdbRecord *chan_rec)
{
	UdbRecord *rec = chan_rec ? udb_record_find(ctx, CKEY_OPTIONS, chan_rec) : NULL;
	return rec && !rec->data_str && (rec->data_num & UDB_CHOPT_PERSISTENT);
}

static int udb_channel_is_lock_modes(UdbContext *ctx, UdbRecord *chan_rec)
{
	UdbRecord *rec = chan_rec ? udb_record_find(ctx, CKEY_OPTIONS, chan_rec) : NULL;
	return rec && !rec->data_str && (rec->data_num & UDB_CHOPT_LOCK_MODES);
}

static int udb_channel_is_lock_topic(UdbContext *ctx, UdbRecord *chan_rec)
{
	UdbRecord *rec = chan_rec ? udb_record_find(ctx, CKEY_OPTIONS, chan_rec) : NULL;
	return rec && !rec->data_str && (rec->data_num & UDB_CHOPT_LOCK_TOPIC);
}

static int udb_channel_is_protect_bans(UdbContext *ctx, UdbRecord *chan_rec)
{
	UdbRecord *rec = chan_rec ? udb_record_find(ctx, CKEY_OPTIONS, chan_rec) : NULL;
	return rec && !rec->data_str && (rec->data_num & UDB_CHOPT_PROTECT_BANS);
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

static void udb_channel_invite_grant_set(Client *client, Channel *channel)
{
	UdbInviteGrant *grant;

	if (!MyUser(client) || !udb_channel_invite_grant_md)
		return;
	for (grant = moddata_local_client(client, udb_channel_invite_grant_md).ptr; grant; grant = grant->next)
	{
		if (!strcasecmp(grant->channel, channel->name))
		{
			udb_time_add(TStime(), UDB_INVITE_GRANT_TTL, &grant->expires);
			return;
		}
	}
	grant = safe_alloc(sizeof(*grant));
	strlcpy(grant->channel, channel->name, sizeof(grant->channel));
	udb_time_add(TStime(), UDB_INVITE_GRANT_TTL, &grant->expires);
	grant->next = moddata_local_client(client, udb_channel_invite_grant_md).ptr;
	moddata_local_client(client, udb_channel_invite_grant_md).ptr = grant;
}

static int udb_channel_invite_grant_take(Client *client, Channel *channel, int consume)
{
	UdbInviteGrant *grant, *previous = NULL;

	if (!MyUser(client) || !udb_channel_invite_grant_md)
		return 0;
	grant = moddata_local_client(client, udb_channel_invite_grant_md).ptr;
	while (grant)
	{
		UdbInviteGrant *next = grant->next;
		if (grant->expires <= TStime() || !strcasecmp(grant->channel, channel->name))
		{
			if (grant->expires > TStime() && !strcasecmp(grant->channel, channel->name) && !consume)
				return 1;
			if (previous)
				previous->next = next;
			else
				moddata_local_client(client, udb_channel_invite_grant_md).ptr = next;
			if (grant->expires > TStime() && !strcasecmp(grant->channel, channel->name))
			{
				safe_free(grant);
				return 1;
			}
			safe_free(grant);
			grant = next;
			continue;
		}
		previous = grant;
		grant = next;
	}
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

static void udb_channel_pending_auth_set(Client *client, Channel *channel)
{
	UdbPendingChannelAuth *entry;

	if (!MyUser(client) || !udb_channel_auth_pending_md)
		return;
	for (entry = moddata_local_client(client, udb_channel_auth_pending_md).ptr; entry; entry = entry->next)
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
	int suspended = chan_rec && udb_record_find(udb_ctx, CKEY_SUSPENDED, chan_rec);

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
			udb_channel_do_mode(channel, NULL, "-a", member->client->name);
		moddata_member(member, udb_channel_auth_member_md).i = 0;
	}
}

static void udb_channel_grant_pending_admin(Client *client, Channel *channel, MessageTag *mtags)
{
	Member *member;

	if (!udb_channel_pending_auth_take(client, channel) || !udb_channel_auth_member_md ||
		!find_channel_mode_handler('a'))
		return;
	member = find_member_link(channel->members, client);
	if (!member || check_channel_access_member(member, "a"))
		return;
	udb_channel_do_mode(channel, mtags, "+a", client->name);
	moddata_member(member, udb_channel_auth_member_md).i = 1;
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

static void udb_channel_apply_topic(Channel *channel, UdbRecord *topic_rec)
{
	Client *source;

	if (!topic_rec || !topic_rec->data_str)
		return;
	if (channel->topic && !strcmp(channel->topic, topic_rec->data_str))
		return;
	source = udb_service_source(SKEY_CHANSERV);
	if (set_channel_topic)
		set_channel_topic(source, channel, NULL, topic_rec->data_str, source->name, TStime());
}

static void udb_channel_apply_subrecord(UdbContext *ctx, Channel *channel, UdbRecord *chan_rec, const char *subkey,
										int is_new)
{
	/* A channel-profile replacement revokes every UDB-owned channel state;
	 * mirror that removal so the surviving profile restores it all. */
	if (!strcasecmp(subkey, chan_rec->key))
	{
		UdbRecord *mode_rec = udb_record_find(ctx, CKEY_MODES, chan_rec);
		UdbRecord *topic_rec = udb_record_find(ctx, CKEY_TOPIC, chan_rec);

		udb_channel_reconcile_founder(channel, chan_rec);
		if (mode_rec && mode_rec->data_str)
			udb_channel_apply_modes(channel, mode_rec->data_str);
		udb_channel_set_persistent(channel, udb_channel_is_persistent(ctx, chan_rec));
		udb_channel_apply_topic(channel, topic_rec);
		return;
	}

	if (!strcmp(subkey, CKEY_OPTIONS))
	{
		udb_channel_set_persistent(channel, udb_channel_is_persistent(ctx, chan_rec));
		return;
	}

	UdbRecord *sub_rec = udb_record_find(ctx, subkey, chan_rec);
	if (!sub_rec || !sub_rec->data_str)
		return;

	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, chan_rec);
	}
	else if (!strcmp(subkey, CKEY_MODES))
	{
		udb_channel_apply_modes(channel, sub_rec->data_str);
	}
	else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE))
	{
		udb_channel_revoke_udb_admins(channel);
	}
	else if (!strcmp(subkey, CKEY_TOPIC))
	{
		udb_channel_apply_topic(channel, sub_rec);
	}
}

static void udb_channel_remove_subrecord(UdbContext *ctx, Channel *channel, UdbRecord *chan_rec, const char *subkey)
{
	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, NULL);
	}
	else if (!strcmp(subkey, CKEY_MODES))
	{
		UdbRecord *mode_rec = udb_record_find(ctx, CKEY_MODES, chan_rec);
		udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
	}
	else if (!strcmp(subkey, CKEY_OPTIONS))
	{
		udb_channel_set_persistent(channel, 0);
	}
	else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE))
	{
		udb_channel_revoke_udb_admins(channel);
	}
	else if (!strcmp(subkey, CKEY_TOPIC))
	{
		udb_channel_clear_topic(channel);
	}
	else if (!strcasecmp(subkey, chan_rec->key))
	{
		UdbRecord *mode_rec = udb_record_find(ctx, CKEY_MODES, chan_rec);
		udb_channel_reconcile_founder(channel, NULL);
		udb_channel_revoke_udb_admins(channel);
		udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
		udb_channel_set_persistent(channel, 0);
		if (udb_record_find(ctx, CKEY_TOPIC, chan_rec))
			udb_channel_clear_topic(channel);
	}
}

static void udb_channel_apply_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new)
{
	UdbRecord *chan_rec;
	Channel *channel;

	if (!ctx || !block || !rec)
		return;
	chan_rec = rec->parent == block->tree ? rec : rec->parent;
	channel = find_channel(chan_rec->key);
	if (!channel && udb_channel_is_persistent(ctx, chan_rec) && find_channel_mode_handler('P'))
	{
		channel = make_channel(chan_rec->key);
		channel->creationtime = TStime();
	}
	if (channel)
		udb_channel_apply_subrecord(ctx, channel, chan_rec, rec->key, is_new);
}

static void udb_channel_remove_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	UdbRecord *chan_rec;
	Channel *channel;

	if (!ctx || !block || !rec)
		return;
	chan_rec = rec->parent == block->tree ? rec : rec->parent;
	channel = find_channel(chan_rec->key);
	if (channel)
		udb_channel_remove_subrecord(ctx, channel, chan_rec, rec->key);
}

static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key)
{
	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	UdbRecord *pass_rec;
	if (!chan_rec)
		return HOOK_CONTINUE;

	UdbRecord *forbid_rec = udb_record_find(udb_ctx, CKEY_FORBID, chan_rec);
	if (forbid_rec)
	{
		return HOOK_CONTINUE; /* Let can_join handle the reject with proper numeric */
	}

	if (udb_channel_is_identified_founder(client, chan_rec))
		return HOOK_ALLOW; /* Bypass bans/keys/invite */

	/* CAN_JOIN already verified this credential. Record it only after all
	 * regular join checks have succeeded, immediately before membership. */
	pass_rec = udb_record_find(udb_ctx, CKEY_PASS, chan_rec);
	if (pass_rec && pass_rec->data_str && *pass_rec->data_str && key && udb_check_password(key, chan_rec, client))
	{
		/* A supplied password remains an admin authentication, even if invited. */
		udb_channel_invite_grant_take(client, channel, 1);
		udb_channel_pending_auth_set(client, channel);
	}
	else
	{
		udb_channel_invite_grant_take(client, channel, 1);
	}
	return HOOK_CONTINUE;
}

static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg)
{
	static char errbuf[512];
	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	if (!chan_rec)
		return 0;

	UdbRecord *forbid_rec = udb_record_find(udb_ctx, CKEY_FORBID, chan_rec);
	if (forbid_rec)
	{
		snprintf(errbuf, sizeof(errbuf), "%%s :%s",
				 forbid_rec->data_str ? forbid_rec->data_str : "Channel is forbidden");
		*errmsg = errbuf;
		return ERR_FORBIDDENCHANNEL;
	}

	int is_founder = udb_channel_is_identified_founder(client, chan_rec);
	int has_invite_grant = udb_channel_invite_grant_take(client, channel, 0);

	UdbRecord *pass_rec = udb_record_find(udb_ctx, CKEY_PASS, chan_rec);
	if (pass_rec && pass_rec->data_str && *pass_rec->data_str && !is_founder && !has_invite_grant)
	{
		if (!key || !udb_check_password(key, chan_rec, client))
		{
			*errmsg = STR_ERR_BADCHANNELKEY;
			return ERR_BADCHANNELKEY;
		}
	}

	UdbRecord *access_rec = udb_record_find(udb_ctx, CKEY_ACCESS, chan_rec);
	if (access_rec && !is_founder)
	{
		UdbRecord *acc_entry = udb_record_find(udb_ctx, client->name, access_rec);
		if (!acc_entry || !has_user_mode(client, 'r'))
		{
			snprintf(errbuf, sizeof(errbuf), "%%s :You must be authorized and identified to join this channel");
			*errmsg = errbuf;
			return ERR_FORBIDDENCHANNEL;
		}
	}
	return 0;
}

static void handle_join(Client *client, Channel *channel, MessageTag *mtags)
{
	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	if (!chan_rec)
		return;

	int is_founder = udb_channel_is_identified_founder(client, chan_rec);

	if (channel->users == 1)
	{
		UdbRecord *susp_rec = udb_record_find(udb_ctx, CKEY_SUSPENDED, chan_rec);

		/* A registered channel assigns founder authority exclusively as +q. */
		if (!IsServer(client) && !IsULine(client))
		{
			udb_channel_do_mode(channel, mtags, "-o", client->name);
		}

		if (!susp_rec)
		{
			udb_channel_do_mode(channel, mtags, "+r", "");
		}

		udb_channel_apply_subrecord(udb_ctx, channel, chan_rec, CKEY_MODES, 0);
		udb_channel_apply_subrecord(udb_ctx, channel, chan_rec, CKEY_TOPIC, 0);
		udb_channel_set_persistent(channel, udb_channel_is_persistent(udb_ctx, chan_rec));
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
			/* List modes b, e, I are channel lists and are not locked by lock_modes */
			if (*modes == 'b' || *modes == 'e' || *modes == 'I')
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

CMD_OVERRIDE_FUNC(udb_override_invite)
{
	Channel *channel;
	Client *target;
	UdbRecord *chan_rec, *pass_rec;

	if (!MyUser(client) || parc < 4 || !udb_ctx || !udb_ctx->channels)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	channel = find_channel(parv[2]);
	target = find_user(parv[1], NULL);
	chan_rec = channel ? udb_record_find(udb_ctx, channel->name, udb_ctx->channels) : NULL;
	pass_rec = chan_rec ? udb_record_find(udb_ctx, CKEY_PASS, chan_rec) : NULL;
	if (!pass_rec || !pass_rec->data_str || !*pass_rec->data_str)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	if (!channel || !target)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	if (!MyUser(target))
	{
		udb_send_service_notice(client, SKEY_CHANSERV, "Password INVITE requires a local target");
		return;
	}
	if (!udb_check_password(parv[3], chan_rec, client))
	{
		sendnumeric(client, ERR_BADCHANNELKEY, channel->name);
		return;
	}
	CALL_NEXT_COMMAND_OVERRIDE();
	if (is_invited(target, channel))
		udb_channel_invite_grant_set(target, channel);
}

CMD_OVERRIDE_FUNC(udb_override_mode)
{
	Channel *channel;
	char channel_name[CHANNELLEN + 1];
	UdbRecord *chan_rec;
	UdbBanSnapshot *snapshot = NULL;
	int is_founder;
	int had_ban_add = 0;

	if (!MyUser(client) || parc < 3 || !IsChannelName(parv[1]))
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	channel = find_channel(parv[1]);
	chan_rec =
		(channel && udb_ctx && udb_ctx->channels) ? udb_record_find(udb_ctx, channel->name, udb_ctx->channels) : NULL;
	if (!chan_rec)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
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
	CALL_NEXT_COMMAND_OVERRIDE();
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

CMD_OVERRIDE_FUNC(udb_override_topic)
{
	Channel *channel;
	UdbRecord *chan_rec;

	if (!MyUser(client) || parc < 2 || !IsChannelName(parv[1]))
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	if (parc < 3 || BadPtr(parv[2]))
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	channel = find_channel(parv[1]);
	chan_rec =
		(channel && udb_ctx && udb_ctx->channels) ? udb_record_find(udb_ctx, channel->name, udb_ctx->channels) : NULL;
	if (!chan_rec)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	if (udb_channel_is_lock_topic(udb_ctx, chan_rec))
	{
		udb_send_service_notice(client, SKEY_CHANSERV,
								"You do not have permission to change the topic in %s (locked by UDB)", channel->name);
		return;
	}
	UdbRecord *topic_rec = udb_record_find(udb_ctx, CKEY_TOPIC, chan_rec);
	if (topic_rec)
	{
		int is_founder = udb_channel_is_identified_founder(client, chan_rec);
		if (!is_founder)
		{
			sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
			return;
		}
	}
	CALL_NEXT_COMMAND_OVERRIDE();
}

static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic)
{
	if (IsServer(client))
		return topic;

	UdbRecord *chan_rec = udb_record_find(udb_ctx, channel->name, udb_ctx->channels);
	if (!chan_rec)
		return topic;

	if (udb_channel_is_lock_topic(udb_ctx, chan_rec))
	{
		udb_send_service_notice(client, SKEY_CHANSERV,
								"You do not have permission to change the topic in %s (locked by UDB)", channel->name);
		return NULL;
	}

	UdbRecord *topic_rec = udb_record_find(udb_ctx, CKEY_TOPIC, chan_rec);
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

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_invite_grant";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_channel_invite_grant_free;
	udb_channel_invite_grant_md = ModDataAdd(modinfo->handle, mreq);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_channel_ban_owners";
	mreq.type = MODDATATYPE_CHANNEL;
	mreq.free = udb_channel_ban_owners_free;
	udb_channel_ban_owners_md = ModDataAdd(modinfo->handle, mreq);

	HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, udb_hook_can_join);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, udb_hook_pre_local_join);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, udb_hook_local_join);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_JOIN, 0, udb_hook_remote_join);
	HookAddConstString(modinfo->handle, HOOKTYPE_PRE_LOCAL_TOPIC, 0, udb_hook_pre_topic);
}

static int udb_channels_load(ModuleInfo *modinfo)
{
	CommandOverrideAdd(modinfo->handle, "INVITE", 0, udb_override_invite);
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
 * Subsystem: IP Management, Cloaks, Clone Limits & Virtual Hosts
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

typedef struct UdbIpHostState UdbIpHostState;
struct UdbIpHostState
{
	char key[256];
	char realhost[HOSTLEN + 1];
	char cloakedhost[HOSTLEN + 1];
	char *virthost;
	long host_umodes;
	int derived_vhost;
};

static ModDataInfo *udb_ip_host_md = NULL;

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
	userhost_changed(client);
	if (notice)
		udb_send_service_notice(client, SKEY_IPSERV, "%s", notice);
}

static void udb_ip_host_state_free(ModData *m)
{
	UdbIpHostState *state = m->ptr;

	if (!state)
		return;
	safe_free(state->virthost);
	safe_free(state);
}

static void udb_ip_save_host_state(Client *client, const char *key)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (state)
		return;
	state = safe_alloc(sizeof(*state));
	strlcpy(state->key, key, sizeof(state->key));
	strlcpy(state->realhost, client->user->realhost, sizeof(state->realhost));
	strlcpy(state->cloakedhost, client->user->cloakedhost, sizeof(state->cloakedhost));
	safe_strdup(state->virthost, client->user->virthost);
	state->host_umodes = client->umodes & (UMODE_HIDE | UMODE_SETHOST);
	moddata_local_client(client, udb_ip_host_md).ptr = state;
}

static int udb_ip_tkl_is_owned(TKL *tkl)
{
	return tkl && tkl->set_by && tkl->ptr.banexception && tkl->ptr.banexception->reason &&
		   !strcmp(tkl->set_by, "UDB") && !strcmp(tkl->ptr.banexception->reason, "UDB Nolines Exemption");
}

static int udb_ip_is_throttle_exempt(UdbRecord *ip_rec)
{
	UdbRecord *nolines = udb_record_find(udb_ctx, IKEY_NOLINES, ip_rec);

	/* 'c' is UnrealIRCd's connect-flood exception type. */
	return nolines && nolines->data_str && (strchr(nolines->data_str, 'c') || strchr(nolines->data_str, 'C'));
}

static int udb_ip_client_matches(Client *client, const char *ip_key)
{
	UdbIpHostState *state;

	if (!client || !MyConnect(client) || !client->user)
		return 0;
	state = udb_ip_host_md ? moddata_local_client(client, udb_ip_host_md).ptr : NULL;
	return (client->ip && !strcasecmp(client->ip, ip_key)) || (state && !strcasecmp(state->key, ip_key)) ||
		   (!state && !strcasecmp(client->user->realhost, ip_key));
}

static void udb_ip_apply_host(Client *client, const char *ip_key, const char *host)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (state && state->derived_vhost)
		udb_ip_restore_host(client, state->key);
	udb_ip_save_host_state(client, ip_key);

	strlcpy(client->user->realhost, host, sizeof(client->user->realhost));
	strlcpy(client->user->cloakedhost, host, sizeof(client->user->cloakedhost));
	safe_strdup(client->user->virthost, host);
	client->umodes |= UMODE_HIDE | UMODE_SETHOST;
}

static void udb_ip_restore_host(Client *client, const char *ip_key)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (!state || strcasecmp(state->key, ip_key))
		return;
	strlcpy(client->user->realhost, state->realhost, sizeof(client->user->realhost));
	strlcpy(client->user->cloakedhost, state->cloakedhost, sizeof(client->user->cloakedhost));
	safe_strdup(client->user->virthost, state->virthost);
	client->umodes = (client->umodes & ~(UMODE_HIDE | UMODE_SETHOST)) | state->host_umodes;
	udb_ip_host_state_free(&moddata_local_client(client, udb_ip_host_md));
	moddata_local_client(client, udb_ip_host_md).ptr = NULL;
}

static const char *udb_ip_explicit_vhost(Client *client)
{
	UdbRecord *nick_rec;
	UdbRecord *vhost_rec;

	if (!client || !client->user || !has_user_mode(client, 'r') || !udb_ctx || !udb_ctx->nicks)
		return NULL;
	nick_rec = udb_record_find(udb_ctx, client->name, udb_ctx->nicks);
	if (!nick_rec && strcmp(client->user->account, "*"))
		nick_rec = udb_record_find(udb_ctx, client->user->account, udb_ctx->nicks);
	vhost_rec = nick_rec ? udb_record_find(udb_ctx, NKEY_VHOST, nick_rec) : NULL;
	return vhost_rec ? vhost_rec->data_str : NULL;
}

static int udb_ip_derive_vhost(Client *client, char *host, size_t hostlen)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;
	const char *realhost = state ? state->realhost : client->user->realhost;
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
	UdbIpHostState *state;
	UdbRecord *ip_rec;
	UdbRecord *host_rec;
	char host[HOSTLEN + 1];
	char notice[HOSTLEN + 96];

	if (!client || !MyUser(client) || !udb_ip_host_md)
		return 0;
	state = moddata_local_client(client, udb_ip_host_md).ptr;
	if (!state)
	{
		if (!udb_ip_derive_vhost(client, host, sizeof(host)))
			return 0;
		userhost_save_current(client);
		udb_ip_save_host_state(client, client->ip);
		state = moddata_local_client(client, udb_ip_host_md).ptr;
		safe_strdup(client->user->virthost, host);
		client->umodes |= UMODE_HIDE | UMODE_SETHOST;
		state->derived_vhost = 1;
		snprintf(notice, sizeof(notice), "*** Your IP-derived vhost has been restored: %s", host);
		udb_ip_notify_host_change(client, notice);
		return 1;
	}
	if (state->derived_vhost)
	{
		if (!udb_ip_derive_vhost(client, host, sizeof(host)))
			return 0;
		userhost_save_current(client);
		safe_strdup(client->user->virthost, host);
		client->umodes |= UMODE_HIDE | UMODE_SETHOST;
		snprintf(notice, sizeof(notice), "*** Your IP-derived vhost has been restored: %s", host);
		udb_ip_notify_host_change(client, notice);
		return 1;
	}

	ip_rec = udb_hash_find(udb_ctx, udb_block_letter_to_index('I'), state->key);
	host_rec = ip_rec ? udb_record_find(udb_ctx, IKEY_HOST, ip_rec) : NULL;
	if (!host_rec || !host_rec->data_str || !*host_rec->data_str)
		return 0;
	userhost_save_current(client);
	udb_ip_apply_host(client, state->key, host_rec->data_str);
	snprintf(notice, sizeof(notice), "*** Your explicit IP vhost has been restored: %s", host_rec->data_str);
	udb_ip_notify_host_change(client, notice);
	return 1;
}

static void udb_ip_refresh_derived_hosts(void)
{
	Client *client;

	if (!udb_ip_host_md)
		return;
	list_for_each_entry(client, &lclient_list, lclient_node)
	{
		UdbIpHostState *state;
		const char *explicit_vhost;
		char host[HOSTLEN + 1];

		if (!client->user || !MyConnect(client))
			continue;
		state = moddata_local_client(client, udb_ip_host_md).ptr;
		explicit_vhost = udb_ip_explicit_vhost(client);
		if (explicit_vhost)
		{
			/* A nick vhost supersedes a derived vhost without restoring over it.
			 * Keep the original state so removing the nick vhost can restore the
			 * derived vhost instead of losing the precedence relationship. */
			if ((!state || state->derived_vhost) && strcmp(udb_ip_visible_host(client), explicit_vhost))
			{
				char notice[HOSTLEN + 96];
				userhost_save_current(client);
				safe_strdup(client->user->virthost, explicit_vhost);
				client->umodes |= UMODE_HIDE | UMODE_SETHOST;
				snprintf(notice, sizeof(notice), "*** Your vhost is now %s", explicit_vhost);
				udb_ip_notify_host_change(client, notice);
			}
			continue;
		}
		if (!udb_ip_derive_vhost(client, host, sizeof(host)))
		{
			if (state && state->derived_vhost)
			{
				userhost_save_current(client);
				udb_ip_restore_host(client, state->key);
				udb_ip_notify_host_change(client, "*** Your IP-derived vhost has been removed");
			}
			continue;
		}
		if (state && !state->derived_vhost)
			continue; /* I::host remains a stronger, explicit IP override. */
		if (state && state->derived_vhost && !strcmp(client->user->virthost, host))
			continue;
		userhost_save_current(client);
		udb_ip_save_host_state(client, client->ip);
		state = moddata_local_client(client, udb_ip_host_md).ptr;
		safe_strdup(client->user->virthost, host);
		client->umodes |= UMODE_HIDE | UMODE_SETHOST;
		state->derived_vhost = 1;
		{
			char notice[HOSTLEN + 96];
			snprintf(notice, sizeof(notice), "*** Your IP-derived vhost is now %s", host);
			udb_ip_notify_host_change(client, notice);
		}
	}
}

static void udb_ip_reconcile_host(const char *ip_key, const char *host)
{
	Client *client;

	list_for_each_entry(client, &lclient_list, lclient_node)
	{
		char oldhost[HOSTLEN + 1];
		char notice[HOSTLEN + 96];

		if (!udb_ip_client_matches(client, ip_key))
			continue;
		strlcpy(oldhost, udb_ip_visible_host(client), sizeof(oldhost));
		if (host)
		{
			if (MyUser(client))
				userhost_save_current(client);
			udb_ip_apply_host(client, ip_key, host);
			if (MyUser(client) && strcmp(oldhost, udb_ip_visible_host(client)))
			{
				snprintf(notice, sizeof(notice), "*** Your explicit IP vhost is now %s", host);
				udb_ip_notify_host_change(client, notice);
			}
		}
		else
		{
			if (MyUser(client))
				userhost_save_current(client);
			udb_ip_restore_host(client, ip_key);
			if (MyUser(client) && strcmp(oldhost, udb_ip_visible_host(client)))
				udb_ip_notify_host_change(client, "*** Your explicit IP vhost has been removed");
		}
	}
}

static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey, int is_new)
{
	(void)is_new;
	if (!strcmp(subkey, IKEY_NOLINES))
	{
		UdbRecord *nolines = udb_record_find(udb_ctx, IKEY_NOLINES, ip_rec);
		TKL *tkl = find_tkl_banexception(TKL_EXCEPTION, "*", ip_key, 0);

		if (udb_ip_tkl_is_owned(tkl))
		{
			tkl_del_line(tkl);
			tkl = NULL;
		}
		if (!tkl && nolines && nolines->data_str && *nolines->data_str)
		{
			tkl_add_banexception(TKL_EXCEPTION, "*", ip_key, NULL, "UDB Nolines Exemption", "UDB", 0, TStime(), 0,
								 nolines->data_str, 0);
		}
	}
	else if (!strcmp(subkey, IKEY_HOST))
	{
		UdbRecord *host = udb_record_find(udb_ctx, IKEY_HOST, ip_rec);
		if (host && host->data_str && *host->data_str)
			udb_ip_reconcile_host(ip_key, host->data_str);
	}
}

static void udb_ip_remove_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey)
{
	if (!strcmp(subkey, IKEY_NOLINES))
	{
		TKL *tkl;
		if ((tkl = find_tkl_banexception(TKL_EXCEPTION, "*", ip_key, 0)) && udb_ip_tkl_is_owned(tkl))
		{
			tkl_del_line(tkl);
		}
	}
	else if (!strcmp(subkey, IKEY_HOST))
	{
		udb_ip_reconcile_host(ip_key, NULL);
		udb_ip_refresh_derived_hosts();
	}
}

static void udb_ips_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new)
{
	UdbRecord *ip_rec;

	if (!ctx || !block || !rec)
		return;
	ip_rec = rec->parent == block->tree ? rec : rec->parent;
	udb_ip_apply_record(ip_rec->key, ip_rec, rec->key, is_new);
}

static void udb_ips_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	UdbRecord *ip_rec;

	if (!ctx || !block || !rec)
		return;
	ip_rec = rec->parent == block->tree ? rec : rec->parent;
	if (rec->parent == block->tree)
	{
		/* Deleting I::<key> must remove effects owned by every child. */
		UdbRecord *child;

		for (child = ip_rec->child; child; child = child->sibling)
			udb_ip_remove_record(ip_rec->key, ip_rec, child->key);
	}
	else
	{
		udb_ip_remove_record(ip_rec->key, ip_rec, rec->key);
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
	ip_rec = udb_hash_find(udb_ctx, udb_block_letter_to_index('I'), client->ip);
	if (!ip_rec && client->user)
		ip_rec = udb_hash_find(udb_ctx, udb_block_letter_to_index('I'), client->user->realhost);

	if (ip_rec)
	{
		/* Apply Clone Limit */
		sub_rec = udb_record_find(udb_ctx, IKEY_CLONES, ip_rec);
		if (sub_rec && sub_rec->data_num > 0)
			limit = (int)sub_rec->data_num;

		/* Apply Host Override */
		sub_rec = udb_record_find(udb_ctx, IKEY_HOST, ip_rec);
		if (sub_rec && sub_rec->data_str && *sub_rec->data_str && client->user)
			udb_ip_apply_host(client, ip_rec->key, sub_rec->data_str);

		if (udb_ip_is_throttle_exempt(ip_rec))
			return HOOK_CONTINUE;
	}
	udb_ip_refresh_derived_hosts();

	/* Fallback to global clones if no specific IP limit */
	if (limit == 0 && udb_ctx && udb_ctx->settings)
	{
		UdbRecord *g_clones = udb_record_find(udb_ctx, SKEY_CLONES, udb_ctx->settings);
		if (g_clones && g_clones->data_num > 0)
			limit = (int)g_clones->data_num;
	}

	if (limit <= 0)
		return 0;

	int clone_count = 0;
	Client *c;
	list_for_each_entry(c, &client_list, client_node)
	{
		if (!IsUser(c) || IsDead(c) || c == client)
			continue;
		if (c->ip && !strcmp(c->ip, client->ip))
			clone_count++;
	}

	if (clone_count < limit)
		return 0;

	const char *quit_msg =
		(udb_ctx && udb_ctx->quit_clones) ? udb_ctx->quit_clones : "Too many connections from your IP";

	udb_log(ULOG_INFO, "UDB_CLONES", client, "Rejecting $client.ip (Exceeds UDB clone limit of $limit)",
			log_data_integer("limit", limit));
	exit_client(client, NULL, quit_msg);
	return HOOK_DENY;
}

static int udb_hook_local_quit(Client *client, MessageTag *mtags, const char *comment)
{
	UdbIpHostState *state;

	(void)mtags;
	(void)comment;
	if (!client || !client->user || !udb_ip_host_md)
		return HOOK_CONTINUE;
	state = moddata_local_client(client, udb_ip_host_md).ptr;
	if (state)
		udb_ip_restore_host(client, state->key);
	return HOOK_CONTINUE;
}

static void udb_ips_shutdown(void)
{
	Client *client;
	UdbRecord *ip_rec;

	if (udb_ip_host_md)
	{
		list_for_each_entry(client, &lclient_list, lclient_node)
		{
			UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;
			if (state && client->user)
				udb_ip_restore_host(client, state->key);
		}
	}
	if (udb_ctx && udb_ctx->ips)
	{
		for (ip_rec = udb_ctx->ips->child; ip_rec; ip_rec = ip_rec->sibling)
			udb_ip_remove_record(ip_rec->key, ip_rec, IKEY_NOLINES);
	}
}

static void udb_ips_init(ModuleInfo *modinfo)
{
	ModDataInfo mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "udb_ip_host_state";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = udb_ip_host_state_free;
	udb_ip_host_md = ModDataAdd(modinfo->handle, mreq);
	if (!udb_ip_host_md)
		return;
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, udb_hook_pre_connect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, udb_hook_local_quit);
}

/* End of udb_ips.c.inc */

/* Distributed *lines: glines, zlines, shuns, qlines, spamfilters */
/* Inlined: udb_lines.c.inc */
/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Distributed *lines (K, Z, Shun, Q) and Spamfilter Rules
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

static UdbRecord *udb_line_owner(UdbRecord *rec)
{
	while (rec && rec->parent && rec->parent->parent && rec->parent->parent->parent)
		rec = rec->parent;

	if (!rec || !rec->parent || !rec->parent->parent || rec->parent->parent->parent)
		return NULL;
	return rec;
}

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

	if (strncmp(stored, UDB_SPAMFILTER_B64_PREFIX, strlen(UDB_SPAMFILTER_B64_PREFIX)))
	{
		if (!*stored || strlen(stored) >= patternsz)
			return 0;
		strlcpy(pattern, stored, patternsz);
		return 1;
	}

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

	if (!tkl || !tkl->set_by || strcmp(tkl->set_by, "UDB"))
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

static void udb_line_remove_owned(char type, const char *pattern)
{
	TKL *tkl, *next;
	int i, j;

	for (i = 0; i < TKLISTLEN; i++)
	{
		for (tkl = tklines[i]; tkl; tkl = next)
		{
			next = tkl->next;
			if (udb_line_matches_tkl(tkl, type, pattern))
			{
				if (type == 'S')
					tkl_check_local_remove_shun(tkl);
				tkl_del_line(tkl);
			}
		}
	}
	for (i = 0; i < TKLIPHASHLEN1; i++)
	{
		for (j = 0; j < TKLIPHASHLEN2; j++)
		{
			for (tkl = tklines_ip_hash[i][j]; tkl; tkl = next)
			{
				next = tkl->next;
				if (udb_line_matches_tkl(tkl, type, pattern))
					tkl_del_line(tkl);
			}
		}
	}
}

static void udb_line_apply_record(UdbRecord *rec, int is_new)
{
	char type;
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	UdbRecord *line_rec, *raz, *dur;
	time_t expires;

	(void)is_new;
	line_rec = udb_line_owner(rec);
	if (!line_rec || !line_rec->parent || !line_rec->parent->key)
		return;
	type = line_rec->parent->key[0];
	if (!strchr("GZSQF", type))
		return;
	if (type == 'F' && !udb_spamfilter_pattern(line_rec->key, pattern, sizeof(pattern)))
	{
		udb_log(ULOG_ERROR, "UDB_SPAMF_PATTERN", NULL, "Invalid spamfilter pattern: $pattern",
				log_data_string("pattern", line_rec->key), NULL);
		return;
	}
	if (type != 'F')
		strlcpy(pattern, line_rec->key, sizeof(pattern));

	udb_line_remove_owned(type, pattern);

	const char *reason = NULL;
	raz = udb_record_find(udb_ctx, KKEY_REASON, line_rec);
	if (raz && raz->data_str)
	{
		reason = raz->data_str;
	}
	else if (line_rec->data_str)
	{
		reason = line_rec->data_str;
	}

	if (!reason)
		return;

	dur = udb_record_find(udb_ctx, KKEY_DURATION, line_rec);
	if (dur && dur->data_num)
	{
		if (!udb_time_add(TStime(), dur->data_num, &expires))
			expires = 0;
	}
	else
	{
		expires = 0;
	}

	if (type == 'F')
	{
		UdbRecord *tip = udb_record_find(udb_ctx, KKEY_TYPE, line_rec);
		UdbRecord *acc = udb_record_find(udb_ctx, KKEY_ACTION, line_rec);

		if (tip && acc && tip->data_str && acc->data_str)
		{
			int target = spamfilter_getconftargets(tip->data_str);
			BanActionValue act_val = banact_stringtoval(acc->data_str);
			BanAction *action = banact_value_to_struct(act_val);

			const char *err = NULL;
			Match *match = target > 0 && action ? unreal_create_match(MATCH_PCRE_REGEX, pattern, &err) : NULL;
			if (match)
			{
				tkl_add_spamfilter(TKL_SPAMF | TKL_GLOBAL, pattern, target, action, match, pattern, NULL, "UDB",
								   expires, TStime(), dur ? (time_t)dur->data_num : 0, reason, 0, 0, 0);
			}
			else
			{
				udb_log(ULOG_ERROR, "UDB_SPAMF_ERROR", NULL, "Failed to compile spamfilter regex: $regex ($err)",
						log_data_string("regex", pattern), log_data_string("err", err ? err : "unknown error"), NULL);
			}
		}
	}
	else
	{
		char user[128];
		char host[128];
		udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));

		if (type == 'G')
		{
			tkl_add_serverban(TKL_KILL | TKL_GLOBAL, user, host, NULL, reason, "UDB", expires, TStime(), 0, 0);
		}
		else if (type == 'Z')
		{
			tkl_add_serverban(TKL_ZAP | TKL_GLOBAL, user, host, NULL, reason, "UDB", expires, TStime(), 0, 0);
		}
		else if (type == 'S')
		{
			tkl_add_serverban(TKL_SHUN | TKL_GLOBAL, user, host, NULL, reason, "UDB", expires, TStime(), 0, 0);
		}
		else if (type == 'Q')
		{
			tkl_add_nameban(TKL_NAME | TKL_GLOBAL, pattern, 0, reason, "UDB", expires, TStime(), 0);
		}
	}
}

static void udb_line_remove_record(UdbRecord *rec)
{
	char type;
	char pattern[UDB_SPAMFILTER_PATTERN_MAX + 1];
	UdbRecord *line_rec = udb_line_owner(rec);

	if (!line_rec || !line_rec->parent || !line_rec->parent->key)
		return;

	type = line_rec->parent->key[0];
	if (!strchr("GZSQF", type))
		return;
	if (type == 'F')
	{
		if (!udb_spamfilter_pattern(line_rec->key, pattern, sizeof(pattern)))
			return;
	}
	else
	{
		strlcpy(pattern, line_rec->key, sizeof(pattern));
	}
	udb_line_remove_owned(type, pattern);
}

static void udb_lines_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new)
{
	if (!ctx || !block || !rec)
		return;
	udb_line_apply_record(rec, is_new);
}

static void udb_lines_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec)
{
	if (!ctx || !block || !rec)
		return;
	udb_line_remove_record(rec);
}

static void udb_lines_init(ModuleInfo *modinfo)
{
	/* TKL system handles network bans automatically, no hooks required here */
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

static int udb_query_is_secret(const UdbRecord *rec)
{
	return rec && rec->key &&
		   (!strcmp(rec->key, NKEY_PASS) || !strcmp(rec->key, NKEY_CHALLENGE) || !strcmp(rec->key, SKEY_CRYPT_KEY));
}

CMD_FUNC(cmd_dbq)
{
	char *query_str = NULL;
	char *cur, *ds;
	UdbBlock *block;
	UdbRecord *rec;

	if (!IsUser(client) && !IsServer(client))
		return;

	if (IsUser(client) && !IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if (parc < 2)
	{
		sendto_one(client, NULL, ":%s 339 %s :Insufficient parameters. Syntax: /DBQ [server] <block>[::path]", me.name,
				   client->name);
		return;
	}

	if (parc >= 3)
	{
		if (!match_simple(parv[1], me.name))
		{
			Client *target_server = find_server_quick(parv[1]);
			if (target_server)
				sendto_one(target_server, NULL, ":%s DBQ %s %s", client->id, parv[1], parv[2]);
			else
				sendnumeric(client, ERR_NOSUCHSERVER, parv[1]);
			return;
		}
		safe_strdup(query_str, parv[2]);
	}
	else
	{
		safe_strdup(query_str, parv[1]);
	}

	block = udb_block_by_letter(udb_ctx, query_str[0]);
	if (!block)
	{
		sendto_one(client, NULL, ":%s 339 %s :Block %c does not exist.", me.name, client->name, query_str[0]);
		safe_free(query_str);
		return;
	}

	/* Query for block summary only (e.g. "/DBQ N") */
	if (query_str[1] == '\0')
	{
		sendto_one(client, NULL, ":%s 339 %s :%c %u %lu %lu %lX %s", me.name, client->name, block->letter,
				   block->record_count, block->filesize, (unsigned long)block->modified_at, block->checksum,
				   block->syncing_from ? "*" : "");
		safe_free(query_str);
		return;
	}

	/* Parse path (e.g. "N::davidlig::vhost") */
	cur = query_str + 1;
	if (cur[0] != ':' || cur[1] != ':' || cur[2] == '\0')
	{
		sendto_one(client, NULL, ":%s 339 %s :Invalid block format.", me.name, client->name);
		safe_free(query_str);
		return;
	}
	cur += 2;

	rec = block->tree;
	while ((ds = strstr(cur, "::")))
	{
		*ds = '\0';
		rec = udb_record_find(udb_ctx, cur, rec);
		if (!rec)
			goto notfound;
		cur = ds + 2;
	}
	rec = udb_record_find(udb_ctx, cur, rec);

	if (!rec)
	{
	notfound:
		sendto_one(client, NULL, ":%s 339 %s :Block not found: %s", me.name, client->name, query_str);
		safe_free(query_str);
		return;
	}

	/* Display the found record */
	if (rec->data_str)
	{
		sendto_one(client, NULL, ":%s 339 %s :DBQ %s %s", me.name, client->name, query_str,
				   udb_query_is_secret(rec) ? "<redacted>" : rec->data_str);
	}
	else if (rec->data_num)
	{
		sendto_one(client, NULL, ":%s 339 %s :DBQ %s %lu", me.name, client->name, query_str, rec->data_num);
	}
	else
	{
		UdbRecord *child;
		for (child = rec->child; child; child = child->sibling)
		{
			if (child->data_str)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s %s", me.name, client->name, query_str, child->key,
						   udb_query_is_secret(child) ? "<redacted>" : child->data_str);
			else if (child->data_num)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s %lu", me.name, client->name, query_str, child->key,
						   child->data_num);
			else if (child->child)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s (has sub-records)", me.name, client->name, query_str,
						   child->key);
			else
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s (empty)", me.name, client->name, query_str,
						   child->key);
		}
	}

	safe_free(query_str);
}

static void udb_query_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DBQ", cmd_dbq, MAXPARA, CMD_USER | CMD_SERVER);
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

static UdbBlock *udb_block_create(UdbContext *ctx, char letter, const char *name)
{
	UdbBlock *b = safe_alloc(sizeof(UdbBlock));

	b->letter = letter;
	b->version = 1;
	b->load_state = UDB_LOAD_UNINITIALIZED;
	b->tree = udb_record_create(NULL);
	b->tree->block_idx = (unsigned char)udb_block_letter_to_index(letter);
	safe_strdup(b->tree->key, name);
	b->tree->data_num = 1;

	b->filepath = udb_block_filepath(letter);
	if (!b->filepath)
	{
		udb_record_free_tree(b->tree);
		safe_free(b);
		return NULL;
	}

	ctx->blocks[(unsigned char)letter] = b;
	b->next = ctx->block_list;
	ctx->block_list = b;
	ctx->block_count++;
	return b;
}

static char *udb_block_filepath(char letter)
{
	const char *directory = udb_cfg ? udb_cfg->db_directory : NULL;
	char *path;
	size_t directory_length;
	size_t path_length;

	if (!directory || !*directory)
		return NULL;
	directory_length = strlen(directory);
	path_length = directory_length + sizeof("/udb_N.db");
	if (path_length > UDB_BLOCK_PATH_MAX)
		return NULL;
	path = safe_alloc(path_length);
	snprintf(path, path_length, "%s%sudb_%c.db", directory, directory[directory_length - 1] == '/' ? "" : "/", letter);
	return path;
}

static void udb_block_set_context_root(UdbContext *ctx, UdbBlock *block)
{
	if (!ctx || !block)
		return;
	switch (block->letter)
	{
	case 'N':
		ctx->nicks = block->tree;
		break;
	case 'C':
		ctx->channels = block->tree;
		break;
	case 'I':
		ctx->ips = block->tree;
		break;
	case 'S':
		ctx->settings = block->tree;
		break;
	case 'L':
		ctx->links = block->tree;
		break;
	case 'K':
		ctx->lines = block->tree;
		break;
	}
}

static void udb_block_reset(UdbContext *ctx, UdbBlock *block)
{
	char *name = NULL;
	int block_idx;

	if (!block)
		return;
	udb_remove_tree_effects(ctx, block);

	if (block->tree && block->tree->key)
		safe_strdup(name, block->tree->key);
	else
		safe_strdup(name, "UDB");
	block_idx = udb_block_letter_to_index(block->letter);
	if (block->tree)
	{
		if (ctx->total_records >= block->record_count)
			ctx->total_records -= block->record_count;
		else
			ctx->total_records = 0;
		udb_record_free_tree(block->tree);
	}
	udb_hash_clear_block(ctx, block_idx);

	block->tree = udb_record_create(NULL);
	block->tree->block_idx = (unsigned char)block_idx;
	safe_strdup(block->tree->key, name);
	block->tree->is_dynamic_key = 1;
	block->tree->data_num = 1;
	block->record_count = 0;
	udb_block_set_context_root(ctx, block);
	safe_free(name);
}

static int udb_block_load(UdbContext *ctx, UdbBlock *block)
{
	(void)udb_file_cleanup_snapshot_temp(block);

	return udb_file_load_block(ctx, block);
}

static void udb_block_unload(UdbContext *ctx, UdbBlock *block)
{
	(void)ctx;
	if (block->tree)
	{
		udb_record_free_tree(block->tree);
		block->tree = NULL;
	}
}

static int udb_blocks_load_all(UdbContext *ctx)
{
	UdbBlock *b;
	if (!ctx)
		return 0;
	for (b = ctx->block_list; b; b = b->next)
	{
		if (!udb_block_load(ctx, b))
			return 0;
	}
	return 1;
}

static int udb_blocks_save_all(UdbContext *ctx)
{
	UdbBlock *b;
	int success = 1;

	if (!ctx)
		return 0;
	for (b = ctx->block_list; b; b = b->next)
	{
		if (b->load_state != UDB_LOAD_SUCCESS && b->load_state != UDB_LOAD_EMPTY)
		{
			udb_log(ULOG_ERROR, "UDB_BLOCK_SAVE_SKIPPED", NULL,
					"Skipping save of block $block because it is not in safe initialized state",
					log_data_string("block", (char[]){b->letter, '\0'}));
			success = 0;
			continue;
		}
		if (!udb_file_save_block(ctx, b))
			success = 0;
	}
	return success;
}

static UdbBlock *udb_block_by_letter(UdbContext *ctx, char letter)
{
	return ctx ? ctx->blocks[(unsigned char)letter] : NULL;
}

static void udb_engine_cleanup(UdbContext *ctx)
{
	UdbBlock *b;

	if (!ctx)
		return;
	udb_ips_shutdown();
	for (b = ctx->block_list; b;)
	{
		UdbBlock *next = b->next;
		udb_sync_session_free(b);
		udb_block_unload(ctx, b);
		safe_free(b->filepath);
		safe_free(b);
		b = next;
	}
	udb_hash_destroy(ctx);
	udb_config_free(ctx);
	safe_free(ctx);
	if (udb_ctx == ctx)
		udb_ctx = NULL;
}

static int udb_engine_init(void)
{
	struct stat st = {0};
	const char *dir;

	if (!udb_cfg)
		udb_cfg = safe_alloc(sizeof(UdbConfig));
	if (!udb_cfg->db_directory)
		safe_strdup(udb_cfg->db_directory, UDB_DEFAULT_DB_DIRECTORY);
	if (udb_cfg->max_staged_records == 0)
		udb_cfg->max_staged_records = UDB_DEFAULT_MAX_STAGED_RECORDS;
	udb_ctx = safe_alloc(sizeof(UdbContext));
	udb_hash_init(udb_ctx);
	dir = udb_cfg && udb_cfg->db_directory ? udb_cfg->db_directory : NULL;
	if (!dir)
		return 0;
	if (stat(dir, &st) == -1)
	{
		if (mkdir(dir, 0700) != 0)
		{
			udb_log(ULOG_ERROR, "UDB_DIRECTORY_CREATE_FAILED", NULL,
					"Cannot create database directory $directory: $error", log_data_string("directory", dir),
					log_data_string("error", strerror(errno)));
			return 0;
		}
	}
	else if (!S_ISDIR(st.st_mode))
	{
		udb_log(ULOG_ERROR, "UDB_DIRECTORY_INVALID", NULL, "Database directory $directory is not a directory",
				log_data_string("directory", dir));
		return 0;
	}

	if (!udb_block_create(udb_ctx, 'N', "Nicks") || !udb_block_create(udb_ctx, 'C', "Channels") ||
		!udb_block_create(udb_ctx, 'I', "IPs") || !udb_block_create(udb_ctx, 'S', "Settings") ||
		!udb_block_create(udb_ctx, 'L', "Links") || !udb_block_create(udb_ctx, 'K', "Lines"))
	{
		udb_engine_cleanup(udb_ctx);
		return 0;
	}
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['N']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['C']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['I']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['S']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['L']);
	udb_block_set_context_root(udb_ctx, udb_ctx->blocks['K']);
	if (!udb_blocks_load_all(udb_ctx))
	{
		udb_engine_cleanup(udb_ctx);
		return 0;
	}
	return 1;
}

static void udb_engine_shutdown(void)
{
	if (!udb_ctx)
		return;
	udb_blocks_save_all(udb_ctx);
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
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, udb_config_posttest);
	return MOD_SUCCESS;
}

static int udb_module_init(ModuleInfo *modinfo)
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, udb_config_run);
	udb_sync_snomask_filter();
	udb_protocol_init(modinfo);
	udb_nicks_init(modinfo);
	udb_channels_init(modinfo);
	udb_ips_init(modinfo);
	udb_lines_init(modinfo);
	udb_query_init(modinfo);
	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

static int udb_module_load(ModuleInfo *modinfo)
{
	if (udb_engine_init() == 0)
	{
		config_error("[UDB] Failed to initialize database engine");
		return MOD_FAILED;
	}
	udb_sync_snomask_filter();
	udb_nicks_load(modinfo);
	udb_channels_load(modinfo);
	udb_log(ULOG_INFO, "UDB_LOADED", NULL, "Unreal Database System v" UDB_VERSION " loaded successfully");
	return MOD_SUCCESS;
}

static int udb_module_unload(void)
{
	udb_log(ULOG_INFO, "UDB_UNLOADING", NULL, "Saving databases and shutting down...");
	udb_log_snomask_filter_free();
	udb_engine_shutdown();
	return MOD_SUCCESS;
}

/* End of udb_lifecycle.c.inc */

/* ========================================================================
 * Configuration Test (MOD_TEST)
 *
 * Validates the udb { } configuration block at config load time.
 * ======================================================================== */

MOD_TEST()
{
	return udb_module_test(modinfo);
}

/* ========================================================================
 * Module Initialization (MOD_INIT)
 *
 * Registers all commands, hooks, ModData, and initializes the DB engine.
 * ======================================================================== */

MOD_INIT()
{
	return udb_module_init(modinfo);
}

/* ========================================================================
 * Module Load (MOD_LOAD)
 *
 * Called after all modules are initialized. Load database files.
 * ======================================================================== */

MOD_LOAD()
{
	return udb_module_load(modinfo);
}

/* ========================================================================
 * Module Unload (MOD_UNLOAD)
 *
 * Save all data and free resources.
 * ======================================================================== */

MOD_UNLOAD()
{
	return udb_module_unload();
}
