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

/* UDB internal module interface.
 *
 * This header is intentionally included only by the bundled implementation
 * unit. It centralizes daemon-dependent state and cross-subsystem interfaces.
 */

#ifndef UDB_INTERNAL_H
#define UDB_INTERNAL_H

/* UDB - Unreal Database System for UnrealIRCd 6
 * Originally by Trocotronic & MaD (UDB 3.6.1 for UnrealIRCd 3.2.8)
 * Migrated to UnrealIRCd 6 module API - 2026
 *
 * This header defines the public UDB constants shared with module consumers.
 * Implementation-only state and cross-subsystem interfaces live in
 * udb_internal.h.
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

#define UDB_VERSION "4.0.0"

/* ========================================================================
 * Block Identifiers
 * ======================================================================== */
#define UDB_BLOCK_NICKS    'N'
#define UDB_BLOCK_CHANNELS 'C'
#define UDB_BLOCK_IPS      'I'
#define UDB_BLOCK_SETTINGS 'S'
#define UDB_BLOCK_LINKS    'L'
#define UDB_BLOCK_LINES    'K'

#define UDB_NUM_BLOCKS 6

/* ========================================================================
 * Sub-record Keys
 * ======================================================================== */

/* Nick sub-records: N::<nick>::<key> <value> */
#define NKEY_ACCESS    "access"    /* IP/CIDR access restriction */
#define NKEY_PASS      "pass"      /* Password hash */
#define NKEY_VHOST     "vhost"     /* Virtual host */
#define NKEY_FORBID    "forbid"    /* Forbidden nick (value = reason) */
#define NKEY_SUSPENDED "suspended" /* Suspended nick (value = reason) */
#define NKEY_OPER      "oper"      /* Oper level bitmask (*N) */
#define NKEY_CHALLENGE "challenge" /* Password hash method */
#define NKEY_MODES     "modes"     /* Allowed oper modes */
#define NKEY_SNOMASKS  "snomasks"  /* Allowed snomasks */
#define NKEY_SWHOIS    "swhois"    /* Custom SWHOIS line */

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
#define CKEY_PERSISTENT "persistent" /* Keep the channel alive through native +P */

/* IP sub-records: I::<ip|host>::<key> <value> */
#define IKEY_CLONES  "clones"  /* Max clones allowed (*N) */
#define IKEY_NOLINES "nolines" /* Ban exception types (eg. GZQSTmc) */
#define IKEY_HOST    "host"    /* Reverse DNS override */

/* Settings sub-records: S::<key> <value> */
#define SKEY_CRYPT_KEY   "encryption_key" /* Host cloaking key */
#define SKEY_SUFFIX      "suffix"         /* Virtual host suffix */
#define SKEY_NICKSERV    "nickserv"       /* NickServ bot mask */
#define SKEY_CHANSERV    "chanserv"       /* ChanServ bot mask */
#define SKEY_IPSERV      "ipserv"         /* IpServ bot mask */
#define SKEY_CLONES      "clones"         /* Global max clones (*N) */
#define SKEY_QUIT_IPS    "quit_ips"       /* Quit message for IP limit */
#define SKEY_QUIT_CLONES "quit_clones"    /* Quit message for clone limit */
#define SKEY_CHALLENGE   "challenge"      /* Global hash method */
#define SKEY_FLOOD       "flood"          /* Password flood limit V:S */

/* Link sub-records: L::<server>::<key> <value> */
#define LKEY_OPTIONS "options" /* Link option flags (*N) */

/* Line sub-records: K::<type>::<pattern>::<key> <value> */
#define KKEY_TYPE     "type"     /* Spamfilter target type */
#define KKEY_ACTION   "action"   /* Spamfilter action */
#define KKEY_DURATION "duration" /* TKL duration */
#define KKEY_REASON   "reason"   /* Ban reason */

/* Spamfilter pattern encoding: K::F::b64:<RFC 4648 base64>::... */
#define UDB_SPAMFILTER_B64_PREFIX  "b64:"
#define UDB_SPAMFILTER_PATTERN_MAX 3072

/* ========================================================================
 * Error Codes (for DB ERR protocol messages)
 * ======================================================================== */
#define UDB_ERR_NO_BLOCK    1  /* Block does not exist */
#define UDB_ERR_OFFSET      2  /* Data offset mismatch */
#define UDB_ERR_NOT_HUB     3  /* Only hub can insert/delete */
#define UDB_ERR_PARAMS      4  /* Missing parameters */
#define UDB_ERR_CANNOT_OPEN 5  /* Cannot open block file */
#define UDB_ERR_FATAL       6  /* Fatal / internal error */
#define UDB_ERR_SYNC_ACTIVE 7  /* Sync already in progress */
#define UDB_ERR_NO_SYNC     8  /* No sync was requested */
#define UDB_ERR_FORBIDDEN   9  /* Forbidden server */
#define UDB_ERR_DUPLICATE   10 /* Duplicate record */

/* SHA-256 is deliberately handled by UDB, not Auth_Check(). */
#define UDB_AUTHTYPE_SHA256 1001

/* ========================================================================
 * Oper Levels (bitmask stored in N::<nick>::oper *<value>)
 * ======================================================================== */
#define UDB_OPER_HELPER 0x1 /* Pre-operator: receives +h automatically */
#define UDB_OPER_ADMIN  0x2 /* Admin: receives +oa */
#define UDB_OPER_ROOT   0x4 /* Root: receives +oN, can /rehash /restart */

/* ========================================================================
 * Channel Option Flags (bitmask in C::<#chan>::options *<value>)
 * ======================================================================== */
#define UDB_CHOPT_PROTECT_BANS 0x1 /* Only ban author can remove their bans */
#define UDB_CHOPT_LOCK_MODES   0x2 /* Channel modes are locked */

/* ========================================================================
 * Link Option Flags (bitmask in L::<server>::options *<value>)
 * ======================================================================== */
#define UDB_LNKOPT_DEBUG      0x1 /* Debug: receives all UDB mode changes */
#define UDB_LNKOPT_PROPAGATOR 0x2 /* Propagator: only server that can push data */

#endif /* UDB_H */


#include "unrealircd.h"
#include <openssl/hmac.h>

#define UDB_DB_SUBDIR              "data"
#define UDB_SYNC_TIMEOUT           60
#define UDB_HASH_SIZE              2048
#define UDB_HASH_MASK              (UDB_HASH_SIZE - 1)
#define UDB_PASSWORD_FAILURE_SLOTS 256

typedef struct UdbRecord UdbRecord;
typedef struct UdbBlock UdbBlock;
typedef struct UdbSyncSession UdbSyncSession;

struct UdbRecord {
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

struct UdbBlock {
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
};

struct UdbSyncSession {
	Client *peer;
	char txid[32];
	time_t deadline;
	UdbRecord *tree;
	unsigned int record_count;
};

typedef struct UdbPasswordFailure {
	char profile[CHANNELLEN + 1];
	char ip[INET6_ADDRSTRLEN];
	unsigned char block_idx;
	unsigned int attempts;
	time_t since;
} UdbPasswordFailure;

typedef struct UdbConfig {
	char *db_directory;
	char *propagator;
	int max_global_clones;
	int flood_attempts;
	int flood_period;
	int config_flood_attempts;
	int config_flood_period;
} UdbConfig;

typedef struct UdbContext {
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
static void udb_config_free(void);
static int udb_module_test(ModuleInfo *modinfo);
static int udb_module_init(ModuleInfo *modinfo);
static int udb_module_load(ModuleInfo *modinfo);
static int udb_module_unload(void);
static int udb_engine_init(void);
static void udb_engine_shutdown(void);
static UdbBlock *udb_block_create(char letter, const char *name);
static void udb_block_set_context_root(UdbBlock *block);
static int udb_block_load(UdbBlock *block);
static void udb_block_unload(UdbBlock *block);
static void udb_block_reset(UdbBlock *block);
static void udb_blocks_load_all(void);
static void udb_blocks_save_all(void);
static UdbBlock *udb_block_by_letter(char letter);
static UdbRecord *udb_record_find(const char *key, UdbRecord *parent);
static UdbRecord *udb_record_create(UdbRecord *parent);
static UdbRecord *udb_record_insert(UdbBlock *block, UdbRecord *parent,
                                    const char *key, const char *data_str,
                                    unsigned long data_num, int persist);
static UdbRecord *udb_record_find_path(UdbBlock *block, const char *path);
static UdbRecord *udb_record_delete(UdbBlock *block, UdbRecord *rec, int persist);
static void udb_record_free_tree(UdbRecord *rec);
static void udb_hash_init(void);
static void udb_hash_destroy(void);
static void udb_hash_insert_record(UdbRecord *rec, int block_idx, const char *key);
static int udb_hash_remove_record(UdbRecord *rec, int block_idx, const char *key);
static UdbRecord *udb_hash_find(int block_idx, const char *key);
static int udb_file_save_block(UdbBlock *block);
static int udb_file_load_block(UdbBlock *block);
static UdbRecord *udb_file_parse_line(UdbBlock *block, char *line);
static void udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf,
                               int pathlen);
static unsigned long udb_crc32(const char *data, size_t len);
static unsigned long udb_compute_block_checksum(UdbBlock *block);
static unsigned long udb_compute_tree_checksum(UdbRecord *tree);
static int udb_stage_parse_line(UdbBlock *block, UdbSyncSession *session,
                                const char *line);
static int udb_stage_persist_block(UdbBlock *block, UdbSyncSession *session);
static int udb_block_commit_stage(UdbBlock *block, UdbSyncSession *session,
                                  unsigned long checksum);
static void udb_sync_session_free(UdbBlock *block);
static int udb_block_letter_to_index(char letter);

static void udb_sync_to_server(Client *server);
static int udb_is_propagator(Client *server);
static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync);
static void udb_nick_strip(Client *client, UdbRecord *nick_rec);
static void udb_nick_revoke_oper(Client *client);
static int udb_check_password(const char *pass, UdbRecord *profile_rec,
                              Client *client);
static int udb_nick_access_allowed(Client *client, UdbRecord *nick_rec);
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
static void udb_channel_apply_record(Channel *channel, UdbRecord *chan_rec,
                                     const char *subkey, int is_new);
static void udb_channel_remove_record(Channel *channel, UdbRecord *chan_rec,
                                      const char *subkey);
static int udb_channels_load(ModuleInfo *modinfo);
static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec,
                                const char *subkey, int is_new);
static void udb_ip_remove_record(const char *ip_key, UdbRecord *ip_rec,
                                 const char *subkey);
static void udb_ip_refresh_derived_hosts(void);
static void udb_ips_shutdown(void);
static int udb_settings_apply_record(UdbRecord *rec);
static void udb_settings_remove_record(UdbRecord *rec);
static void udb_link_apply_record(UdbRecord *rec);
static void udb_link_remove_record(UdbRecord *rec);
static void udb_line_apply_record(UdbRecord *line_rec, int is_new);
static void udb_line_remove_record(UdbRecord *line_rec);
static const char *udb_get_bot_nick(const char *service_key, int force_default);
static const char *udb_get_bot_mask(const char *service_key, int force_default);
/* Runtime dispatcher; concrete per-block effects stay in their own modules. */
static int udb_apply_special_record(UdbBlock *block, UdbRecord *rec, int is_new);
static void udb_remove_special_record(UdbBlock *block, UdbRecord *rec);
static void udb_send_to_debugs(Client *source, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static int udb_protocol_init(ModuleInfo *modinfo);
int udb_nicks_init(ModuleInfo *modinfo);
int udb_nicks_load(ModuleInfo *modinfo);
static void udb_channels_init(ModuleInfo *modinfo);
static void udb_ips_init(ModuleInfo *modinfo);
static void udb_lines_init(ModuleInfo *modinfo);
static void udb_query_init(ModuleInfo *modinfo);

#define udb_log(level, event_id, client, msg, ...) \
	unreal_log(level, "udb", event_id, client, "[UDB] " msg, ##__VA_ARGS__)
#define udb_strdup(dest, src) safe_strdup(dest, src)

#endif /* UDB_INTERNAL_H */


/* ========================================================================
 * Module Header
 * ======================================================================== */

ModuleHeader MOD_HEADER = {
    "third/udb",
    "4.0.0",
    "UDB - Unreal Database System (nick/channel/IP registration & sync)",
    "David Abuín Fontán ('davidlig')",
    "unrealircd-6"};

/* ========================================================================
 * Implementation Files
 *
 * Each file implements a specific subsystem. They share the same compilation
 * unit, so all functions are static and can call each other freely.
 * ======================================================================== */

/* Record store: tree, hash, path, and file persistence primitives */
/* Inlined: udb_store.c.inc */
/*
 * UDB record store primitives.
 *
 * These helpers only manage record trees, indexes, paths, and on-disk
 * serialization. Runtime effects remain in udb_core.c.inc.
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

static void udb_hash_init(void)
{
	for (int i = 0; i < UDB_NUM_BLOCKS; i++)
		udb_ctx->hash_table[i] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
}

static void udb_hash_destroy(void)
{
	for (int i = 0; i < UDB_NUM_BLOCKS; i++)
	{
		if (udb_ctx->hash_table[i])
		{
			safe_free(udb_ctx->hash_table[i]);
			udb_ctx->hash_table[i] = NULL;
		}
	}
}

static void udb_hash_clear_block(int block_idx)
{
	if (block_idx < 0 || block_idx >= UDB_NUM_BLOCKS)
		return;
	safe_free(udb_ctx->hash_table[block_idx]);
	udb_ctx->hash_table[block_idx] = safe_alloc(sizeof(UdbRecord *) * UDB_HASH_SIZE);
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

static void udb_hash_insert_record(UdbRecord *rec, int block_idx, const char *key)
{
	unsigned int h = udb_hash_str(key);

	rec->hash_next = udb_ctx->hash_table[block_idx][h];
	udb_ctx->hash_table[block_idx][h] = rec;
}

static int udb_hash_remove_record(UdbRecord *rec, int block_idx, const char *key)
{
	unsigned int h = udb_hash_str(key);
	UdbRecord *curr = udb_ctx->hash_table[block_idx][h];
	UdbRecord *prev = NULL;

	while (curr)
	{
		if (curr == rec)
		{
			if (prev)
				prev->hash_next = curr->hash_next;
			else
				udb_ctx->hash_table[block_idx][h] = curr->hash_next;
			return 1;
		}
		prev = curr;
		curr = curr->hash_next;
	}
	return 0;
}

static UdbRecord *udb_hash_find(int block_idx, const char *key)
{
	unsigned int h;
	UdbRecord *curr;

	if (!key)
		return NULL;
	h = udb_hash_str(key);
	curr = udb_ctx->hash_table[block_idx][h];
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
	static const char *known_keys[] = {
	    "pass", "vhost", "oper", "swhois", "snomasks", "modes",
	    "access", "forbid", "suspended", "challenge", "founder", "topic",
	    "options", "clones", "nolines", "host", "encryption_key", "suffix",
	    "nickserv", "chanserv", "ipserv", "quit_ips", "quit_clones", "flood",
	    "type", "action", "duration", "reason", NULL};

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

static UdbRecord *udb_record_find(const char *key, UdbRecord *parent)
{
	UdbRecord *child;

	if (!parent)
		return NULL;
	if (parent->parent == NULL && udb_ctx)
		return udb_hash_find(parent->block_idx, key);
	for (child = parent->child; child; child = child->sibling)
		if (!strcasecmp(child->key, key))
			return child;
	return NULL;
}

static UdbRecord *udb_record_find_path(UdbBlock *block, const char *path)
{
	char pathbuf[512];
	char *cur;
	char *ds;
	UdbRecord *rec;

	if (!block || !block->tree || !path)
		return NULL;
	strlcpy(pathbuf, path, sizeof(pathbuf));
	cur = pathbuf;
	rec = block->tree;
	while ((ds = strstr(cur, "::")))
	{
		*ds = '\0';
		rec = udb_record_find(cur, rec);
		if (!rec)
			return NULL;
		cur = ds + 2;
	}
	return udb_record_find(cur, rec);
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

/* ========================================================================
 * File Persistence
 * ======================================================================== */
static void udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf, int pathlen)
{
	UdbRecord *child;
	size_t old_len;

	if (!rec)
		return;
	old_len = strlen(pathbuf);
	if (depth > 0)
		strlcat(pathbuf, "::", pathlen);
	strlcat(pathbuf, rec->key, pathlen);
	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		if (rec->data_str)
			fprintf(fp, "%s %s\n", pathbuf, rec->data_str);
		else if (rec->data_num > 0 || !rec->child)
			fprintf(fp, "%s *%lu\n", pathbuf, rec->data_num);
	}
	for (child = rec->child; child; child = child->sibling)
		udb_serialize_tree(child, depth + 1, fp, pathbuf, pathlen);
	pathbuf[old_len] = '\0';
}

static int udb_file_save_block(UdbBlock *block)
{
	char tmp_path[512];
	char pathbuf[4096] = "";
	FILE *fp;
	struct stat st;
	UdbRecord *rec;

	if (!block || !block->filepath)
		return 0;
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath);
	fp = fopen(tmp_path, "w");
	if (!fp)
		return 0;
	fprintf(fp, "; UDB Block %c - Version %d\n", block->letter, block->version);
	fprintf(fp, "; Saved: %ld\n", (long)time(NULL));
	fprintf(fp, "; Records: %u\n", block->record_count);
	if (block->tree)
		for (rec = block->tree->child; rec; rec = rec->sibling)
			udb_serialize_tree(rec, 0, fp, pathbuf, sizeof(pathbuf));
	fclose(fp);
	rename(tmp_path, block->filepath);
	block->checksum = udb_compute_block_checksum(block);
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
	return 1;
}

/* End of udb_store.c.inc */

/* Configuration: daemon block parsing and UDB settings state */
/* Inlined: udb_config.c.inc */
/* UDB configuration and settings state. */

static UdbConfig *udb_cfg = NULL;

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
		} else if (!strcmp(cep->name, "propagator"))
		{
			if (!cep->value || !*cep->value)
			{
				config_error("%s:%i: udb::propagator requires a server name",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else if (!strcmp(cep->name, "max-global-clones"))
		{
			if (!cep->value || atoi(cep->value) < 0)
			{
				config_error("%s:%i: udb::max-global-clones requires a non-negative integer",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else if (!strcmp(cep->name, "password-flood"))
		{
			if (!cep->value || !strchr(cep->value, ':'))
			{
				config_error("%s:%i: udb::password-flood requires format attempts:seconds (e.g. 5:30)",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else
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
		} else if (!strcmp(cep->name, "propagator"))
		{
			safe_strdup(udb_cfg->propagator, cep->value);
		} else if (!strcmp(cep->name, "max-global-clones"))
		{
			udb_cfg->max_global_clones = atoi(cep->value);
		} else if (!strcmp(cep->name, "password-flood"))
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
	udb_cfg->config_flood_attempts = udb_cfg->flood_attempts;
	udb_cfg->config_flood_period = udb_cfg->flood_period;

	return 1;
}

static void udb_config_free(void)
{
	if (udb_ctx)
	{
		safe_free(udb_ctx->quit_ips);
		safe_free(udb_ctx->quit_clones);
		safe_free(udb_ctx->encryption_key);
		safe_free(udb_ctx->suffix);
		safe_free(udb_ctx->nickserv_mask);
		safe_free(udb_ctx->chanserv_mask);
		safe_free(udb_ctx->ipserv_mask);
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
	return value && *value && !strpbrk(value, "\r\n");
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
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || *p == '.' || *p == '-'))
			return 0;
		if (*p == '.')
		{
			if (p == (const unsigned char *)label || p[-1] == '-')
				return 0;
			label = (const char *)p + 1;
		} else if (*p == '-' && p == (const unsigned char *)label)
		{
			return 0;
		}
	}
	return value[1] && *label && value[strlen(value) - 1] != '-' &&
	       value[strlen(value) - 1] != '.';
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
	char *end;
	long parsed_attempts;
	long parsed_period;

	if (!value || !*value)
		return 0;
	parsed_attempts = strtol(value, &end, 10);
	if (end == value || *end++ != ':')
		return 0;
	parsed_period = strtol(end, &end, 10);
	if (*end || parsed_attempts <= 0 || parsed_period <= 0 ||
	    parsed_attempts > INT_MAX || parsed_period > INT_MAX)
		return 0;
	*attempts = (int)parsed_attempts;
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

static int udb_settings_apply_record(UdbRecord *rec)
{
	int attempts;
	int period;

	if (!rec || !rec->key)
		return 0;
	if (!strcmp(rec->key, SKEY_QUIT_IPS))
	{
		if (!udb_setting_string_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->quit_ips, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_QUIT_CLONES))
	{
		if (!udb_setting_string_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->quit_clones, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_FLOOD))
	{
		if (!udb_flood_valid(rec->data_str, &attempts, &period))
			return 0;
		if (udb_cfg)
		{
			udb_cfg->flood_attempts = attempts;
			udb_cfg->flood_period = period;
		}
	} else if (!strcmp(rec->key, SKEY_SUFFIX))
	{
		if (!udb_suffix_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->suffix, rec->data_str);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_CRYPT_KEY))
	{
		if (!udb_encryption_key_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->encryption_key, rec->data_str);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_NICKSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->nickserv_mask, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_CHANSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->chanserv_mask, rec->data_str);
	} else if (!strcmp(rec->key, SKEY_IPSERV))
	{
		if (!udb_service_mask_valid(rec->data_str))
			return 0;
		udb_settings_replace(&udb_ctx->ipserv_mask, rec->data_str);
	} else
	{
		return 0;
	}
	return 1;
}

static void udb_settings_remove_record(UdbRecord *rec)
{
	if (!rec || !rec->key)
		return;
	if (!strcmp(rec->key, SKEY_QUIT_IPS))
		udb_settings_replace(&udb_ctx->quit_ips, NULL);
	else if (!strcmp(rec->key, SKEY_QUIT_CLONES))
		udb_settings_replace(&udb_ctx->quit_clones, NULL);
	else if (!strcmp(rec->key, SKEY_FLOOD))
		udb_settings_restore_flood();
	else if (!strcmp(rec->key, SKEY_SUFFIX))
	{
		udb_settings_replace(&udb_ctx->suffix, NULL);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_CRYPT_KEY))
	{
		udb_settings_replace(&udb_ctx->encryption_key, NULL);
		udb_ip_refresh_derived_hosts();
	} else if (!strcmp(rec->key, SKEY_NICKSERV))
		udb_settings_replace(&udb_ctx->nickserv_mask, NULL);
	else if (!strcmp(rec->key, SKEY_CHANSERV))
		udb_settings_replace(&udb_ctx->chanserv_mask, NULL);
	else if (!strcmp(rec->key, SKEY_IPSERV))
		udb_settings_replace(&udb_ctx->ipserv_mask, NULL);
}

static void udb_link_apply_record(UdbRecord *rec)
{
	if (!rec || !rec->parent || !rec->key || strcmp(rec->key, LKEY_OPTIONS))
		return;
	udb_ctx->propagator = NULL;
	if (rec->data_str || (rec->data_num & ~(UDB_LNKOPT_DEBUG | UDB_LNKOPT_PROPAGATOR)))
		udb_log(ULOG_WARNING, "UDB_LINK_OPTIONS", NULL,
		        "Ignoring invalid L::options for $server", log_data_string("server", rec->parent->key));
}

static void udb_link_remove_record(UdbRecord *rec)
{
	if (rec && rec->key && !strcmp(rec->key, LKEY_OPTIONS))
		udb_ctx->propagator = NULL;
}

/* End of udb_config.c.inc */

/* Core database engine: records, checksums, sync staging, and file I/O */
/* Inlined: udb_core.c.inc */
/*
 * UDB Core Engine for UnrealIRCd 6
 * Implements the database lifecycle, checksums, sync staging, and record manipulation.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

static int udb_password_record_valid(UdbBlock *block, const char *path,
                                     const char *value)
{
	const char *leaf;
	size_t i;

	if (!block || (block->letter != 'N' && block->letter != 'C') || !path)
		return 1;
	leaf = strrchr(path, ':');
	leaf = leaf ? leaf + 1 : path;
	if (!strcasecmp(leaf, NKEY_CHALLENGE))
		return value && (!strcasecmp(value, "argon2id") ||
		                 !strcasecmp(value, "sha256") ||
		                 !strcasecmp(value, "crypt"));
	if (strcasecmp(leaf, NKEY_PASS))
		return 1;
	if (!value)
		return 0;
	if (!strncmp(value, "argon2id:$argon2id$", 19))
		return 1;
	if (!strncmp(value, "crypt:", 6))
		return value[6] != '\0';
	if (strncmp(value, "sha256:", 7) || strlen(value + 7) != 64)
		return 0;
	for (i = 7; value[i]; i++)
		if (!isxdigit((unsigned char)value[i]))
			return 0;
	return 1;
}

static UdbRecord *udb_record_insert(UdbBlock *block, UdbRecord *parent, const char *key, const char *data_str, unsigned long data_num, int persist)
{
	if (!parent)
		parent = block->tree;
	UdbRecord *rec = udb_record_find(key, parent);
	if (!rec)
	{
		rec = udb_record_create(parent);
		if (key)
		{
			if (parent == block->tree)
			{
				safe_strdup(rec->key, key);
				rec->is_dynamic_key = 1;
			} else
			{
				const char *shared = udb_get_shared_subkey(key);
				if (shared)
				{
					rec->key = (char *)shared;
					rec->is_dynamic_key = 0;
				} else
				{
					safe_strdup(rec->key, key);
					rec->is_dynamic_key = 1;
				}
			}
		}
		if (parent == block->tree)
		{
			udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), key);
		}
		block->record_count++;
		udb_ctx->total_records++;
	}

	if (rec->data_str)
	{
		safe_free(rec->data_str);
	}

	// Auto-detect numeric data if it starts with *
	if (data_str && *data_str == '*')
	{
		rec->data_num = atoi(data_str + 1);
		rec->data_str = NULL;
	} else if (data_str)
	{
		safe_strdup(rec->data_str, data_str);
		rec->data_num = 0;
	} else
	{
		rec->data_str = NULL;
		rec->data_num = data_num;
	}

	if (persist)
	{
		udb_file_save_block(block);
	}

	udb_apply_special_record(block, rec, 1);

	return rec;
}


static UdbRecord *udb_record_delete(UdbBlock *block, UdbRecord *rec, int persist)
{
	UdbRecord *line_rec = NULL;
	if (!rec)
		return NULL;

	/* A K property deletion changes its owning pattern, not a line of its own. */
	if (block->letter == 'K' && rec->parent && rec->parent->parent &&
	    rec->parent->parent != block->tree)
		line_rec = rec->parent;

	udb_remove_special_record(block, rec);

	if (rec->parent)
	{
		if (rec->parent->parent == NULL)
		{
			udb_hash_remove_record(rec, udb_block_letter_to_index(block->letter), rec->key);
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
	if (udb_ctx->total_records > 0)
		udb_ctx->total_records--;

	udb_record_free_tree(rec);

	/* Rebuild a surviving K pattern after one of its properties was removed. */
	if (line_rec)
		udb_line_apply_record(line_rec, 0);

	if (persist)
	{
		udb_file_save_block(block);
	}
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

static unsigned long udb_compute_block_checksum(UdbBlock *block)
{
	return block ? udb_compute_tree_checksum(block->tree) : 0;
}

typedef struct UdbDigestLine {
	char *line;
	struct UdbDigestLine *next;
} UdbDigestLine;

static void udb_digest_collect(UdbRecord *rec, int depth, char *pathbuf, int pathlen,
                               UdbDigestLine **lines)
{
	size_t old_len;
	UdbRecord *child;

	if (!rec || !rec->key)
		return;
	old_len = strlen(pathbuf);
	if (depth > 0)
		strlcat(pathbuf, "::", pathlen);
	strlcat(pathbuf, rec->key, pathlen);
	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		UdbDigestLine *line = safe_alloc(sizeof(*line));
		size_t len = strlen(pathbuf) + 2 + (rec->data_str ? strlen(rec->data_str) : 32);
		line->line = safe_alloc(len);
		if (rec->data_str)
			snprintf(line->line, len, "%s %s", pathbuf, rec->data_str);
		else
			snprintf(line->line, len, "%s *%lu", pathbuf, rec->data_num);
		line->next = *lines;
		*lines = line;
	}
	for (child = rec->child; child; child = child->sibling)
		udb_digest_collect(child, depth + 1, pathbuf, pathlen, lines);
	pathbuf[old_len] = '\0';
}

static int udb_digest_line_cmp(const void *a, const void *b)
{
	const UdbDigestLine *const *left = a;
	const UdbDigestLine *const *right = b;
	return strcmp((*left)->line, (*right)->line);
}

/* The digest covers sorted logical records, never save-time file headers/order. */
static unsigned long udb_compute_tree_checksum(UdbRecord *tree)
{
	UdbDigestLine *lines = NULL;
	UdbDigestLine *line;
	UdbDigestLine **sorted;
	unsigned long crc = 0xFFFFFFFFUL;
	unsigned int count = 0;
	unsigned int i = 0;
	char pathbuf[4096] = "";

	if (!tree)
		return 0;
	for (UdbRecord *rec = tree->child; rec; rec = rec->sibling)
		udb_digest_collect(rec, 0, pathbuf, sizeof(pathbuf), &lines);
	for (line = lines; line; line = line->next)
		count++;
	sorted = count ? safe_alloc(sizeof(*sorted) * count) : NULL;
	for (line = lines; line; line = line->next)
		sorted[i++] = line;
	if (count)
		qsort(sorted, count, sizeof(*sorted), udb_digest_line_cmp);
	for (i = 0; i < count; i++)
	{
		crc = udb_crc32_step(crc, sorted[i]->line, strlen(sorted[i]->line));
		crc = udb_crc32_step(crc, "\n", 1);
	}
	for (line = lines; line;)
	{
		UdbDigestLine *next = line->next;
		safe_free(line->line);
		safe_free(line);
		line = next;
	}
	safe_free(sorted);
	return crc ^ 0xFFFFFFFFUL;
}

static UdbRecord *udb_stage_find(UdbRecord *parent, const char *key)
{
	UdbRecord *rec;
	for (rec = parent ? parent->child : NULL; rec; rec = rec->sibling)
		if (!strcasecmp(rec->key, key))
			return rec;
	return NULL;
}

static UdbRecord *udb_stage_insert(UdbRecord *parent, const char *key,
                                   UdbSyncSession *session)
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
	char line[4096];
	char *value, *part, *next;
	UdbRecord *parent;

	if (!block || !session || !session->tree || !input || !*input || strlen(input) >= sizeof(line))
		return 0;
	strlcpy(line, input, sizeof(line));
	value = strchr(line, ' ');
	if (value)
		*value++ = '\0';
	if (!*line)
		return 0;
	if (!udb_password_record_valid(block, line, value))
		return 0;
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
				return 0;
		}
		if (!*part)
			return 0;
		parent = udb_stage_insert(parent, part, session);
		part = next;
	}
	if (value && *value == '*')
	{
		char *end;
		unsigned long n = strtoul(value + 1, &end, 10);
		if (end == value + 1 || *end)
			return 0;
		safe_free(parent->data_str);
		parent->data_num = n;
	} else if (value)
	{
		safe_free(parent->data_str);
		safe_strdup(parent->data_str, value);
		parent->data_num = 0;
	} else
	{
		return 0;
	}
	return 1;
}

static int udb_stage_persist_block(UdbBlock *block, UdbSyncSession *session)
{
	char tmp_path[512];
	char pathbuf[4096] = "";
	FILE *fp;
	UdbRecord *rec;

	if (!block || !session || !session->tree || !block->filepath)
		return 0;
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", block->filepath);
	fp = fopen(tmp_path, "w");
	if (!fp)
		return 0;
	fprintf(fp, "; UDB Block %c - Version %d\n", block->letter, block->version);
	fprintf(fp, "; Saved: %ld\n", (long)time(NULL));
	fprintf(fp, "; Records: %u\n", session->record_count);
	for (rec = session->tree->child; rec; rec = rec->sibling)
		udb_serialize_tree(rec, 0, fp, pathbuf, sizeof(pathbuf));
	if (fclose(fp) != 0 || rename(tmp_path, block->filepath) != 0)
		return 0;
	return 1;
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

static int udb_block_commit_stage(UdbBlock *block, UdbSyncSession *session,
                                  unsigned long checksum)
{
	UdbRecord *rec;
	struct stat st;

	if (!block || !session || block->session != session)
		return 0;
	/* Persistence succeeded before this point; only now may active state move. */
	udb_block_reset(block);
	udb_record_free_tree(block->tree);
	block->tree = session->tree;
	session->tree = NULL;
	block->record_count = session->record_count;
	udb_ctx->total_records += block->record_count;
	for (rec = block->tree->child; rec; rec = rec->sibling)
		udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), rec->key);
	udb_block_set_context_root(block);
	block->checksum = checksum;
	block->modified_at = time(NULL);
	if (stat(block->filepath, &st) == 0)
		block->filesize = st.st_size;
	udb_sync_session_free(block);

	for (rec = block->tree->child; rec; rec = rec->sibling)
	{
		UdbRecord *child;
		udb_apply_special_record(block, rec, 1);
		for (child = rec->child; child; child = child->sibling)
			udb_apply_special_record(block, child, 1);
	}
	return 1;
}

/* ========================================================================
 * File I/O Operations
 * ======================================================================== */
static UdbRecord *udb_file_parse_line(UdbBlock *block, char *line)
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
			data_num = strtoul(value + 1, NULL, 10);
		} else
		{
			data_str = value;
		}
	}
	if (!udb_password_record_valid(block, line, data_str))
		return NULL;

	char *p = line;
	UdbRecord *parent = block->tree;
	UdbRecord *leaf_rec = NULL;
	while (p && *p)
	{
		char *next = strstr(p, "::");
		if (next)
		{
			*next = '\0';
			next += 2;
		}

		UdbRecord *rec = udb_record_find(p, parent);
		if (!rec)
		{
			rec = udb_record_create(parent);
			if (p && *p)
			{
				if (parent == block->tree)
				{
					safe_strdup(rec->key, p);
					rec->is_dynamic_key = 1;
				} else
				{
					const char *shared = udb_get_shared_subkey(p);
					if (shared)
					{
						rec->key = (char *)shared;
						rec->is_dynamic_key = 0;
					} else
					{
						safe_strdup(rec->key, p);
						rec->is_dynamic_key = 1;
					}
				}
			}
			if (parent == block->tree)
			{
				udb_hash_insert_record(rec, udb_block_letter_to_index(block->letter), p);
			}
			block->record_count++;
			udb_ctx->total_records++;
		}

		if (!next)
		{
			if (data_str && *data_str == '*')
			{
				rec->data_num = atoi(data_str + 1);
				if (rec->data_str)
				{
					safe_free(rec->data_str);
					rec->data_str = NULL;
				}
			} else if (data_str)
			{
				safe_strdup(rec->data_str, data_str);
				rec->data_num = 0;
			} else
			{
				if (rec->data_str)
				{
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


static int udb_file_load_block(UdbBlock *block)
{
	if (!block || !block->filepath)
		return 0;
	FILE *fp = fopen(block->filepath, "r");
	if (!fp)
		return 0;

	char line[4096];
	while (fgets(line, sizeof(line), fp))
	{
		char *p = strchr(line, '\n');
		if (p)
			*p = '\0';
		p = strchr(line, '\r');
		if (p)
			*p = '\0';

		if (line[0] == ';' || line[0] == '\0')
			continue;

		udb_file_parse_line(block, line);
	}
	fclose(fp);

	UdbRecord *curr = block->tree->child;
	while (curr)
	{
		UdbRecord *sub = curr->child;
		while (sub)
		{
			udb_apply_special_record(block, sub, 1);
			sub = sub->sibling;
		}
		udb_apply_special_record(block, curr, 1);
		curr = curr->sibling;
	}

	block->checksum = udb_compute_block_checksum(block);

	struct stat st;
	if (stat(block->filepath, &st) == 0)
	{
		block->filesize = st.st_size;
		block->modified_at = st.st_mtime;
	}
	char logbuf[512];
	snprintf(logbuf, sizeof(logbuf), "[UDB] Loaded block %c from %s (%u records)", block->letter, block->filepath, block->record_count);
	unreal_log(ULOG_INFO, "udb", "UDB_FILE_LOADED", NULL, "$msg", log_data_string("msg", logbuf));

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
	const char *buf = "diagnostic detail redacted";

	(void)fmt;

	Client *client;
	list_for_each_entry(client, &client_list, client_node)
	{
		if (IsServer(client) && client != source)
		{
			if (udb_ctx && udb_ctx->links)
			{
				UdbRecord *srv_rec = udb_record_find(client->name, udb_ctx->links);
				if (srv_rec)
				{
					UdbRecord *opt_rec = udb_record_find(LKEY_OPTIONS, srv_rec);
					if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG))
					{
						sendto_one(client, NULL, ":%s NOTICE %s :[UDB Debug] %s", me.id, client->id, buf);
					}
				}
			}
		}
	}

	// Also send to local opers if our own server has the debug option
	if (udb_ctx && udb_ctx->links)
	{
		UdbRecord *me_rec = udb_record_find(me.name, udb_ctx->links);
		if (me_rec)
		{
			UdbRecord *opt_rec = udb_record_find(LKEY_OPTIONS, me_rec);
			if (opt_rec && (opt_rec->data_num & UDB_LNKOPT_DEBUG))
			{
				unreal_log(ULOG_INFO, "udb", "UDB_DEBUG_OPER", source,
				           "[UDB Debug] $msg", log_data_string("msg", buf));
			}
		}
	}

	unreal_log(ULOG_DEBUG, "udb", "UDB_DEBUG", source, "[UDB Debug] $msg", log_data_string("msg", buf));
}

/* End of udb_core.c.inc */

/* Runtime effects: special-record dispatch and per-block routing */
/* Inlined: udb_effects.c.inc */
/*
 * UDB Runtime Effects for UnrealIRCd 6
 * Routes special records to their concrete nick, channel, IP, setting, link,
 * and line effect implementations.
 */

static int udb_apply_special_record(UdbBlock *block, UdbRecord *rec, int is_new)
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
	} else if (block->letter == 'C')
	{
		UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
		Channel *channel = find_channel(chan_rec->key);
		if (channel)
		{
			udb_channel_apply_record(channel, chan_rec, rec->key, is_new);
		}
	} else if (block->letter == 'I')
	{
		UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
		udb_ip_apply_record(ip_rec->key, ip_rec, rec->key, is_new);
	} else if (block->letter == 'S')
	{
		if (!udb_settings_apply_record(rec))
			udb_log(ULOG_WARNING, "UDB_SETTING_INVALID", NULL,
			        "Ignoring invalid or unsupported S::$setting", log_data_string("setting", rec->key));
	} else if (block->letter == 'L')
	{
		udb_link_apply_record(rec);
	} else if (block->letter == 'K')
	{
		udb_line_apply_record(rec, is_new);
	}
	return 1;
}

static void udb_remove_special_record(UdbBlock *block, UdbRecord *rec)
{
	if (!rec)
		return;
	if (block->letter == 'N')
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
				} else if (!strcmp(rec->key, NKEY_OPER))
				{
					udb_nick_revoke_oper(client);
				} else if (!strcmp(rec->key, NKEY_SWHOIS))
				{
					swhois_delete(client, "udb", "*", &me, NULL);
				} else if (!strcmp(rec->key, NKEY_MODES))
				{
					long old_umodes = client->umodes & ALL_UMODES;
					UdbRecord *mode_rec = udb_record_find(NKEY_MODES, nick_rec);
					if (mode_rec && mode_rec->data_str)
						client->umodes &= ~(set_usermode(mode_rec->data_str) & ~UMODE_OPER);
					send_umode_out(client, 1, old_umodes);
				} else if (!strcmp(rec->key, NKEY_SNOMASKS))
				{
					set_snomask(client, NULL);
				} else if (!strcmp(rec->key, NKEY_SUSPENDED))
				{
					long old_umodes = client->umodes & ALL_UMODES;
					client->umodes &= ~set_usermode("S");
					send_umode_out(client, 1, old_umodes);
				} else if (!strcmp(rec->key, NKEY_PASS))
				{
					udb_nick_strip(client, nick_rec);
				}
			}
		} else
		{
			Client *client = find_user(rec->key, NULL);
			if (client && MyUser(client))
			{
				udb_nick_strip(client, rec);
			}
		}
	} else if (block->letter == 'C')
	{
		UdbRecord *chan_rec = rec->parent == block->tree ? rec : rec->parent;
		Channel *channel = find_channel(chan_rec->key);
		if (channel)
		{
			udb_channel_remove_record(channel, chan_rec, rec->key);
		}
	} else if (block->letter == 'I')
	{
		UdbRecord *ip_rec = rec->parent == block->tree ? rec : rec->parent;
		if (rec->parent == block->tree)
		{
			/* Deleting I::<key> must remove effects owned by every child. */
			UdbRecord *child;
			for (child = ip_rec->child; child; child = child->sibling)
				udb_ip_remove_record(ip_rec->key, ip_rec, child->key);
		} else
		{
			udb_ip_remove_record(ip_rec->key, ip_rec, rec->key);
		}
	} else if (block->letter == 'S')
	{
		udb_settings_remove_record(rec);
	} else if (block->letter == 'L')
	{
		udb_link_remove_record(rec);
	} else if (block->letter == 'K')
	{
		udb_line_remove_record(rec);
	}
}

/* End of udb_effects.c.inc */

/* S2S protocol handler: DB command, server sync */
/* Inlined: udb_protocol.c.inc */
/* UDB - Unreal Database System for UnrealIRCd 6
 * Protocol implementation (S2S DB command and sync)
 */

static unsigned long udb_sync_txid = 0;

typedef struct UdbHelloPeer UdbHelloPeer;

struct UdbHelloPeer {
	Client *peer;
	time_t deadline;
	int state;
	UdbHelloPeer *next;
};

#define UDB_HEL_WAITING     1
#define UDB_HEL_CONFIRMED   2
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

	return server && IsServer(server) && MyConnect(server) && peer &&
	       peer->state == UDB_HEL_CONFIRMED;
}

static int udb_has_staged_sync(Client *server)
{
	return udb_has_hello(server);
}

static void udb_hello_start(Client *server)
{
	UdbHelloPeer *peer;

	if (!server || !IsServer(server) || !MyConnect(server))
		return;
	peer = udb_hello_peer(server, 1);
	if (peer->state)
		return;
	peer->state = UDB_HEL_WAITING;
	peer->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	sendto_one(server, NULL, ":%s DB %s HEL 4", me.id, server->id);
}

static void udb_sendto_confirmed_servers(Client *except, const char *fmt, ...)
{
	Client *server;
	char line[4096];
	va_list args;

	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	list_for_each_entry(server, &client_list, client_node) if (server != except && IsServer(server) && MyConnect(server) && udb_has_hello(server))
	    sendto_one(server, NULL, "%s", line);
}

static void udb_sync_abort(UdbBlock *block, const char *reason)
{
	if (!block || !block->session)
		return;
	udb_log(ULOG_WARNING, "UDB_SYNC_ABORT", block->session->peer,
	        "Aborted staged sync of block $block: $reason",
	        log_data_string("block", (char[]){block->letter, '\0'}),
	        log_data_string("reason", reason));
	udb_sync_session_free(block);
}

static int udb_sync_begin(UdbBlock *block, Client *peer, const char *txid)
{
	UdbSyncSession *session;

	if (!block || !peer || !txid || !*txid || strlen(txid) >= sizeof(session->txid))
		return 0;
	if (block->session)
		return 0;
	session = safe_alloc(sizeof(*session));
	session->peer = peer;
	strlcpy(session->txid, txid, sizeof(session->txid));
	session->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	session->tree = udb_record_create(NULL);
	session->tree->block_idx = (unsigned char)udb_block_letter_to_index(block->letter);
	safe_strdup(session->tree->key, "UDB");
	session->tree->is_dynamic_key = 1;
	block->session = session;
	block->syncing_from = peer;
	return 1;
}

static void udb_sync_send_tree(Client *server, UdbRecord *rec, int depth,
                               char *pathbuf, int pathlen, char letter, const char *txid)
{
	size_t old_len;
	UdbRecord *child;

	if (!rec || !rec->key)
		return;
	old_len = strlen(pathbuf);
	if (depth > 0)
		strlcat(pathbuf, "::", pathlen);
	strlcat(pathbuf, rec->key, pathlen);
	if (rec->data_str || rec->data_num > 0 || !rec->child)
	{
		if (rec->data_str)
			sendto_one(server, NULL, ":%s DB %s PUT %c %s %s :%s", me.id, server->id,
			           letter, txid, pathbuf, rec->data_str);
		else
			sendto_one(server, NULL, ":%s DB %s PUT %c %s %s *%lu", me.id, server->id,
			           letter, txid, pathbuf, rec->data_num);
	}
	for (child = rec->child; child; child = child->sibling)
		udb_sync_send_tree(server, child, depth + 1, pathbuf, pathlen, letter, txid);
	pathbuf[old_len] = '\0';
}

static void udb_sync_send_stage(Client *server, UdbBlock *block)
{
	char txid[32];
	char pathbuf[4096] = "";
	UdbRecord *rec;

	if (!udb_has_hello(server))
		return;
	snprintf(txid, sizeof(txid), "%08lx", ++udb_sync_txid);
	sendto_one(server, NULL, ":%s DB %s BEGIN %c %s %08lX", me.id, server->id,
	           block->letter, txid, block->checksum);
	for (rec = block->tree->child; rec; rec = rec->sibling)
		udb_sync_send_tree(server, rec, 0, pathbuf, sizeof(pathbuf), block->letter, txid);
	sendto_one(server, NULL, ":%s DB %s END %c %s %08lX", me.id, server->id,
	           block->letter, txid, block->checksum);
}

EVENT(udb_sync_timeout_event)
{
	UdbBlock *block;
	UdbHelloPeer *peer;
	time_t now = time(NULL);

	for (block = udb_ctx ? udb_ctx->block_list : NULL; block; block = block->next)
		if (block->session && block->session->deadline <= now)
			udb_sync_abort(block, "timeout");

	for (peer = udb_hello_peers; peer; peer = peer->next)
	{
		if (peer->state == UDB_HEL_WAITING && peer->deadline <= now)
		{
			peer->state = UDB_HEL_UNSUPPORTED;
			udb_log(ULOG_INFO, "UDB_HEL_TIMEOUT", peer->peer,
			        "No UDB HEL 4 acknowledgement from directly linked server; capability disabled for this link");
		}
	}
}

static const char *udb_selected_propagator(void)
{
	const char *selected = NULL;
	UdbRecord *link;
	int sources = 0;

	if (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator)
	{
		selected = udb_cfg->propagator;
		sources++;
	}
	if (udb_ctx && udb_ctx->links)
	{
		for (link = udb_ctx->links->child; link; link = link->sibling)
		{
			UdbRecord *options = udb_record_find(LKEY_OPTIONS, link);
			if (options && !options->data_str &&
			    !(options->data_num & ~(UDB_LNKOPT_DEBUG | UDB_LNKOPT_PROPAGATOR)) &&
			    (options->data_num & UDB_LNKOPT_PROPAGATOR))
			{
				selected = link->key;
				sources++;
			}
		}
	}
	if (sources != 1)
	{
		udb_ctx->propagator = NULL;
		udb_send_to_debugs(NULL, "propagator selection rejected");
		udb_log(ULOG_WARNING, "UDB_PROPAGATOR_SELECTION", NULL,
		        "Rejecting UDB writes: exactly one propagator source is required");
		return NULL;
	}
	return selected;
}

static int udb_is_propagator(Client *server)
{
	const char *selected;

	if (!server || !IsServer(server))
		return 0;
	selected = udb_selected_propagator();
	if (selected && !strcasecmp(server->name, selected))
	{
		udb_ctx->propagator = server;
		return 1;
	}
	return 0;
}

/* Paths are passed to the file parser without the block prefix. */
static UdbBlock *udb_protocol_path_block(const char *path)
{
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
	while ((separator = strstr(component, "::")))
	{
		if (separator == component || !separator[2])
			return NULL;
		component = separator + 2;
	}

	return block;
}

static void udb_protocol_params_error(Client *client, const char *subcmd)
{
	sendto_one(client, NULL, ":%s DB %s ERR %s %d 0", me.id, client->id,
	           subcmd ? subcmd : "0", UDB_ERR_PARAMS);
}

static void udb_sync_to_server(Client *server)
{
	UdbBlock *block = udb_ctx->block_list;
	if (!udb_has_hello(server))
		return;
	while (block)
	{
		sendto_one(server, NULL, ":%s DB %s INF %c %lX %lu",
		           me.id, server->id, block->letter, block->checksum, (unsigned long)block->modified_at);
		block = block->next;
	}
}

static int udb_hook_server_sync(Client *client)
{
	if (!client || !IsServer(client) || !MyConnect(client))
		return 0;
	udb_hello_start(client);
	return 0;
}

static int udb_hook_server_quit(Client *client, MessageTag *mtags)
{
	UdbBlock *block;
	UdbHelloPeer **peer;

	if (!udb_ctx || !client)
		return 0;

	if (udb_ctx->propagator == client)
		udb_ctx->propagator = NULL;
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
		if (block->session && block->session->peer == client)
			udb_sync_abort(block, "peer quit");
		else if (block->syncing_from == client)
			block->syncing_from = NULL;
	}
	return 0;
}

CMD_FUNC(cmd_db)
{
	/* Process DB protocol messages sent via server-to-server connection */

	if (parc < 4)
	{
		sendto_one(client, NULL, ":%s DB %s ERR 0 %i 0", me.id, client->id, UDB_ERR_PARAMS);
		return;
	}

	const char *target = parv[1];
	const char *subcmd = parv[2];
	char logbuf[512];

	if (!target || !*target || !subcmd || !*subcmd)
	{
		udb_protocol_params_error(client, subcmd);
		return;
	}

	/* HEL is the sole DB frame accepted before UDB capability confirmation. */
	if (!strcasecmp(subcmd, "HEL"))
	{
		UdbHelloPeer *peer;

		if (!IsServer(client) || !MyConnect(client) ||
		    (strcmp(target, me.id) && strcmp(target, me.name)) ||
		    parc < 4 || strcmp(parv[3], "4"))
			return;
		snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
		unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));
		peer = udb_hello_peer(client, 1);
		if (parc == 5 && !strcasecmp(parv[4], "ACK"))
		{
			if (peer->state != UDB_HEL_WAITING)
				return;
			peer->state = UDB_HEL_CONFIRMED;
			udb_log(ULOG_INFO, "UDB_HEL_CONFIRMED", client,
			        "UDB HEL 4 capability confirmed for directly linked server");
			udb_sync_to_server(client);
			return;
		}
		if (parc != 4)
			return;
		/* Each side sends its own request, so only an ACK confirms outbound data. */
		if (!peer->state)
			udb_hello_start(client);
		sendto_one(client, NULL, ":%s DB %s HEL 4 ACK", me.id, client->id);
		return;
	}

	if (!udb_has_hello(client))
	{
		sendto_one(client, NULL, ":%s DB %s ERR %s %d 0", me.id, client->id,
		           subcmd, UDB_ERR_FORBIDDEN);
		return;
	}

	snprintf(logbuf, sizeof(logbuf), "[UDB] S2S DB received: parc=%d target=%s subcmd=%s", parc, target, subcmd);
	unreal_log(ULOG_INFO, "udb", "UDB_CMD_DB", client, "$msg", log_data_string("msg", logbuf));

	int is_broadcast = !strcmp(target, "*");
	int is_for_me = is_broadcast || !strcmp(target, me.id) || !strcmp(target, me.name);

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
				block = udb_block_by_letter(*parv[3]);
				if (is_for_me)
				{
					if (block && block->session && block->session->peer == client)
					{
						udb_sync_abort(block, "duplicate BEGIN");
						sendto_one(client, NULL, ":%s DB %s ERR BEGIN %d %c", me.id, client->id,
						           UDB_ERR_SYNC_ACTIVE, *parv[3]);
						return;
					}
					if (!block || !udb_sync_begin(block, client, parv[4]))
					{
						sendto_one(client, NULL, ":%s DB %s ERR BEGIN %d %c", me.id, client->id,
						           UDB_ERR_SYNC_ACTIVE, parv[3] ? *parv[3] : '0');
						return;
					}
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s BEGIN %s %s %s", client->id,
				                             target, parv[3], parv[4], parv[5]);
			}
			break;

		case 'P':
			if (!strcasecmp(subcmd, "PUT"))
			{
				UdbBlock *block;
				UdbSyncSession *session;
				char line[4096];
				if (parc < 7 || !udb_has_staged_sync(client))
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				block = udb_block_by_letter(*parv[3]);
				if (is_for_me)
				{
					session = block ? block->session : NULL;
					if (!session || session->peer != client || strcmp(session->txid, parv[4]))
					{
						if (session && session->peer == client)
							udb_sync_abort(block, "invalid PUT sequence");
						sendto_one(client, NULL, ":%s DB %s ERR PUT %d %c", me.id, client->id,
						           UDB_ERR_NO_SYNC, parv[3] ? *parv[3] : '0');
						return;
					}
					if (snprintf(line, sizeof(line), "%s %s", parv[5], parv[6]) >= (int)sizeof(line) ||
					    !udb_stage_parse_line(block, session, line))
					{
						udb_sync_abort(block, "invalid PUT payload");
						sendto_one(client, NULL, ":%s DB %s ERR PUT %d %c", me.id, client->id,
						           UDB_ERR_PARAMS, *parv[3]);
						return;
					}
					session->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s PUT %s %s %s :%s", client->id,
				                             target, parv[3], parv[4], parv[5], parv[6]);
			}
			break;

		case 'E':
			if (!strcasecmp(subcmd, "END"))
			{
				UdbBlock *block;
				UdbSyncSession *session;
				unsigned long digest;
				if (parc < 6 || !udb_has_staged_sync(client))
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				block = udb_block_by_letter(*parv[3]);
				if (is_for_me)
				{
					session = block ? block->session : NULL;
					if (!session || session->peer != client || strcmp(session->txid, parv[4]))
					{
						if (session && session->peer == client)
							udb_sync_abort(block, "invalid END sequence");
						sendto_one(client, NULL, ":%s DB %s ERR END %d %c", me.id, client->id,
						           UDB_ERR_NO_SYNC, parv[3] ? *parv[3] : '0');
						return;
					}
					digest = udb_compute_tree_checksum(session->tree);
					if (digest != strtoul(parv[5], NULL, 16) ||
					    !udb_stage_persist_block(block, session) ||
					    !udb_block_commit_stage(block, session, digest))
					{
						udb_sync_abort(block, "digest or persistence failure");
						sendto_one(client, NULL, ":%s DB %s ERR END %d %c", me.id, client->id,
						           UDB_ERR_FATAL, *parv[3]);
						return;
					}
					sendto_one(client, NULL, ":%s DB %s ACK %c %s %08lX", me.id, client->id,
					           *parv[3], parv[4], digest);
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s END %s %s %s", client->id,
				                             target, parv[3], parv[4], parv[5]);
			} else if (!strcasecmp(subcmd, "ERR"))
			{
				if (parc < 5)
					return;
				if (is_for_me)
				{
					int errcode = atoi(parv[4]);
					udb_log(ULOG_INFO, "UDB_EVENT", client, "Error from $client: cmd=$cmd err=$errcode",
					        log_data_client("client", client), log_data_string("cmd", parv[3]), log_data_integer("errcode", errcode));
					if (!is_broadcast)
						return;
				}
				if (parc >= 6)
				{
					udb_sendto_confirmed_servers(client, ":%s DB %s ERR %s %s %s", client->id, target, parv[3], parv[4], parv[5]);
				} else
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
					udb_log(ULOG_INFO, "UDB_SYNC_ACK", client,
					        "Staged sync acknowledged for block $block",
					        log_data_string("block", parv[3]));
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s ACK %s %s %s", client->id,
				                             target, parv[3], parv[4], parv[5]);
			}
			break;

		case 'I':
			if (!strcasecmp(subcmd, "INF"))
			{
				if (parc < 6)
					return;
				char letter = *parv[3];
				UdbBlock *block = udb_block_by_letter(letter);

				if (is_for_me)
				{
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR INF %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					unsigned long crc32 = strtoul(parv[4], NULL, 16);
					time_t remote_ts = atol(parv[5]);

					if (crc32 != block->checksum)
					{
						if (remote_ts > block->modified_at)
						{
							sendto_one(client, NULL, ":%s DB %s RES %c", me.id, client->id, letter);
						} else if (remote_ts == block->modified_at)
						{
							sendto_one(client, NULL, ":%s DB %s RES %c", me.id, client->id, letter);
						}
					}
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s INF %c %s %s", client->id, target, letter, parv[4], parv[5]);
			} else if (!strcasecmp(subcmd, "INS"))
			{
				if (parc < 5)
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}
				const char *path = parv[3];
				const char *data = parv[4];
				UdbBlock *block = udb_protocol_path_block(path);
				char letter = path && *path ? path[0] : '0';

				if (!block)
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}

				if (is_for_me)
				{
					if (block->session)
					{
						sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from && block->syncing_from != client)
					{
						sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from != client && !udb_is_propagator(client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR INS %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}

					int len = strlen(path) + strlen(data) + 2;
					char *line = safe_alloc(len);
					snprintf(line, len, "%s %s", path + 3, data);
					UdbRecord *rec = udb_file_parse_line(block, line);
					safe_free(line);

					if (rec)
					{
						udb_apply_special_record(block, rec, 1);
					}
					if (!block->syncing_from)
						udb_file_save_block(block);

					char logbuf[512];
					snprintf(logbuf, sizeof(logbuf), "[UDB] Inserted record via S2S: %s -> %s", path, data);
					unreal_log(ULOG_INFO, "udb", "UDB_INS_RECEIVED", client, "$msg", log_data_string("msg", logbuf));

					if (udb_ctx->propagator && block->syncing_from == client)
					{
						udb_sendto_confirmed_servers(client, ":%s DB %s INS %s %s", udb_ctx->propagator->id, target, path, data);
						return;
					}

					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s INS %s %s", client->id, target, path, data);
			}
			break;

		case 'R':
			if (!strcasecmp(subcmd, "RES"))
			{
				if (parc < 4)
					return;
				char letter = *parv[3];
				UdbBlock *block = udb_block_by_letter(letter);

				if (is_for_me)
				{
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					if (block->syncing_from && block->syncing_from != client)
					{
						sendto_one(client, NULL, ":%s DB %s ERR RES %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}

					if (udb_has_staged_sync(client))
					{
						udb_sync_send_stage(client, block);
					} else
					{
						FILE *fp = fopen(block->filepath, "r");
						if (fp)
						{
							char line[1024];
							while (fgets(line, sizeof(line), fp))
							{
								size_t len = strlen(line);
								while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
								{
									line[--len] = '\0';
								}
								if (len > 0)
								{
									if (strchr(line, ' '))
										sendto_one(client, NULL, ":%s DB * INS %c::%s", me.id, letter, line);
									else
										sendto_one(client, NULL, ":%s DB * DEL %c::%s", me.id, letter, line);
								}
							}
							fclose(fp);
						}
						sendto_one(client, NULL, ":%s DB %s FDR %c", me.id, client->id, letter);
					}
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
				const char *path = parv[3];
				UdbBlock *block = udb_protocol_path_block(path);
				char letter = path && *path ? path[0] : '0';

				if (!block)
				{
					udb_protocol_params_error(client, subcmd);
					return;
				}

				if (is_for_me)
				{
					if (block->session)
					{
						sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from)
					{
						if (block->syncing_from != client)
						{
							sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
							return;
						}
					} else if (!udb_is_propagator(client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR DEL %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}

					UdbRecord *rec = udb_record_find_path(block, path + 3);
					if (rec)
					{
						udb_record_delete(block, rec, 1);
					}

					if (udb_ctx->propagator && block->syncing_from == client)
					{
						udb_sendto_confirmed_servers(client, ":%s DB %s DEL %s", udb_ctx->propagator->id, target, path);
						return;
					}
					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s DEL %s", client->id, target, path);
			} else if (!strcasecmp(subcmd, "DRP"))
			{
				if (parc < 4)
					return;
				char letter = *parv[3];

				if (is_for_me)
				{
					UdbBlock *block = udb_block_by_letter(letter);
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					if (block->session)
					{
						sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from && block->syncing_from != client)
					{
						sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (!udb_is_propagator(client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR DRP %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}

					udb_block_reset(block);
					block->checksum = 0;
					block->filesize = 0;
					if (!block->syncing_from)
						udb_file_save_block(block);

					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s DRP %c", client->id, target, *parv[3]);
			}
			break;

		case 'F':
			if (!strcasecmp(subcmd, "FDR"))
			{
				if (parc < 4)
					return;
				char letter = *parv[3];

				if (is_for_me)
				{
					UdbBlock *block = udb_block_by_letter(letter);
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					if (block->session)
					{
						if (block->session->peer == client)
							udb_sync_abort(block, "legacy FDR during staged sync");
						sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (block->syncing_from != client)
					{
						sendto_one(client, NULL, ":%s DB %s ERR FDR %d %c", me.id, client->id, UDB_ERR_NO_SYNC, letter);
						return;
					}

					udb_file_save_block(block);
					block->syncing_from = NULL;

					if (!is_broadcast)
						return;
				}
				udb_sendto_confirmed_servers(client, ":%s DB %s FDR %c", client->id, target, *parv[3]);
			}
			break;

		case 'O':
			if (!strcasecmp(subcmd, "OPT"))
			{
				if (parc < 4)
					return;
				char letter = *parv[3];

				if (is_for_me)
				{
					UdbBlock *block = udb_block_by_letter(letter);
					if (!block)
					{
						sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_NO_BLOCK, letter);
						return;
					}
					if (block->session)
					{
						sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_SYNC_ACTIVE, letter);
						return;
					}
					if (!udb_is_propagator(client))
					{
						sendto_one(client, NULL, ":%s DB %s ERR OPT %d %c", me.id, client->id, UDB_ERR_FORBIDDEN, letter);
						return;
					}
					if (parc >= 5)
					{
						block->modified_at = atol(parv[4]);
					}
					udb_file_save_block(block);

					if (!is_broadcast)
						return;
				}
				if (parc >= 5)
					udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c %s", client->id, target, *parv[3], parv[4]);
				else
					udb_sendto_confirmed_servers(client, ":%s DB %s OPT %c", client->id, target, *parv[3]);
			}
			break;
	}
}

static int udb_protocol_init(ModuleInfo *modinfo)
{
	CommandAdd(modinfo->handle, "DB", cmd_db, MAXPARA, CMD_SERVER);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, udb_hook_server_sync);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, udb_hook_server_quit);
	EventAdd(modinfo->handle, "udb_sync_timeout", udb_sync_timeout_event, NULL, 1000, 0);

	return 0;
}

/* End of udb_protocol.c.inc */

/* Nick management: registration, identification, ghost, vhost, oper */
/* Inlined: udb_nicks.c.inc */
#include <openssl/evp.h>

static void udb_nick_set_vhost(Client *client, UdbRecord *vhost_rec)
{
	if (!client || !client->user || !vhost_rec || !vhost_rec->data_str)
		return;

	/* If the vhost is already active and set to this exact value, nothing to do */
	if (client->user->virthost && !strcmp(client->user->virthost, vhost_rec->data_str) && IsHidden(client) && IsSetHost(client))
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
		sendnotice(client, "*** Your vhost is now %s", client->user->virthost);
	}

	userhost_changed(client);
}

static void udb_nick_remove_vhost(Client *client)
{
	if (!client || !client->user)
		return;

	userhost_save_current(client);

	if (*client->user->cloakedhost)
	{
		safe_strdup(client->user->virthost, client->user->cloakedhost);
	} else
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
		sendnotice(client, "*** Your vhost has been removed");
	}

	userhost_changed(client);
}

static void udb_nick_grant_oper(Client *client, UdbRecord *nick_rec, UdbRecord *oper_rec)
{
	if (!oper_rec)
		return;

	unsigned long level = oper_rec->data_num;
	const char *operclass = NULL;

	if (level & UDB_OPER_ROOT)
	{
		operclass = "netadmin";
	} else if (level & UDB_OPER_ADMIN)
	{
		operclass = "admin";
	} else if (level & UDB_OPER_HELPER)
	{
		operclass = "locop";
	}

	if (operclass)
	{
		if (IsOper(client))
		{
			const char *curr_class = get_operclass(client);
			if (curr_class && !strcmp(curr_class, operclass))
				return;
			udb_nick_revoke_oper(client);
		}
		make_oper(client, "UDB", operclass, NULL, UMODE_OPER, NULL, NULL, NULL);
	}
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
	if (!snomask_rec || !snomask_rec->data_str)
		return;
	set_snomask(client, snomask_rec->data_str);
}

static void udb_nick_force_rename(Client *client, const char *nick_in_db)
{
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

static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync)
{
	if (!client || !nick_rec)
		return;

	UdbRecord *forbid = udb_record_find(NKEY_FORBID, nick_rec);
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
			UdbRecord *pass_rec = udb_record_find(NKEY_PASS, nick_rec);
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

	UdbRecord *susp = udb_record_find(NKEY_SUSPENDED, nick_rec);
	if (susp)
	{
		client->umodes |= set_usermode("S");
	}

	send_umode_out(client, 1, old_umodes);

	UdbRecord *vhost_rec = udb_record_find(NKEY_VHOST, nick_rec);
	if (vhost_rec)
		udb_nick_set_vhost(client, vhost_rec);

	UdbRecord *oper_rec = udb_record_find(NKEY_OPER, nick_rec);
	if (oper_rec)
		udb_nick_grant_oper(client, nick_rec, oper_rec);

	UdbRecord *modes_rec = udb_record_find(NKEY_MODES, nick_rec);
	if (modes_rec && modes_rec->data_str)
	{
		udb_nick_set_modes(client, nick_rec, modes_rec, modes_rec->data_str);
	}

	UdbRecord *swhois_rec = udb_record_find(NKEY_SWHOIS, nick_rec);
	if (swhois_rec)
		udb_nick_set_swhois(client, nick_rec, swhois_rec);

	UdbRecord *sno_rec = udb_record_find(NKEY_SNOMASKS, nick_rec);
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
		UdbRecord *mode_rec = udb_record_find(NKEY_MODES, nick_rec);
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
		UdbRecord *swhois_rec = udb_record_find(NKEY_SWHOIS, nick_rec);
		if (swhois_rec && swhois_rec->data_str)
		{
			swhois_delete(client, "udb", "*", &me, NULL);
		}
	}
}

static UdbPasswordFailure *udb_password_failure_find(UdbRecord *profile_rec,
                                                     Client *client, int create)
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
		if (entry->since && entry->block_idx == profile_rec->block_idx &&
		    !strcmp(entry->profile, profile_rec->key) && !strcmp(entry->ip, ip))
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

static int udb_password_type(const char *challenge, const char *stored_pass,
                             const char **hash)
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
	pass_rec = udb_record_find(NKEY_PASS, profile_rec);
	if (!pass_rec || BadPtr(pass_rec->data_str))
	{
		udb_password_failure_record(profile_rec, client, 0);
		return 0;
	}
	chall_rec = udb_record_find(NKEY_CHALLENGE, profile_rec);
	if (chall_rec && chall_rec->data_str)
		challenge = chall_rec->data_str;
	else if (udb_ctx && udb_ctx->settings)
	{
		UdbRecord *global = udb_record_find(SKEY_CHALLENGE, udb_ctx->settings);
		if (global && global->data_str)
			challenge = global->data_str;
	}
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
	} else
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
	char *end;
	long prefix;
	int family;
	int bytes;

	if (!slash || (size_t)(slash - cidr) >= sizeof(network_text))
		return 0;
	memcpy(network_text, cidr, slash - cidr);
	network_text[slash - cidr] = '\0';
	prefix = strtol(slash + 1, &end, 10);
	if (end == slash + 1 || *end)
		return 0;
	if (inet_pton(AF_INET, ip, address) == 1)
		family = AF_INET;
	else if (inet_pton(AF_INET6, ip, address) == 1)
		family = AF_INET6;
	else
		return 0;
	if (inet_pton(family, network_text, network) != 1 ||
	    prefix < 0 || prefix > (family == AF_INET ? 32 : 128))
		return 0;
	bytes = prefix / 8;
	if (bytes && memcmp(address, network, bytes))
		return 0;
	if (prefix % 8 && (address[bytes] & (0xff << (8 - (prefix % 8)))) !=
	                      (network[bytes] & (0xff << (8 - (prefix % 8)))))
		return 0;
	return 1;
}

static int udb_nick_access_allowed(Client *client, UdbRecord *nick_rec)
{
	UdbRecord *access_rec = udb_record_find(NKEY_ACCESS, nick_rec);
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
		sendnotice(client, "UDB is not fully initialized.");
		return;
	}

	UdbRecord *nick_rec = udb_record_find(target_nick, udb_ctx->nicks);
	if (!nick_rec)
	{
		sendnotice(client, "Nick %s is not registered.", target_nick);
		return;
	}

	if (!udb_check_password(pass, nick_rec, client))
	{
		sendnotice(client, "Invalid password for %s.", target_nick);
		return;
	}
	if (!udb_nick_access_allowed(client, nick_rec))
	{
		sendnotice(client, "Access to %s is not permitted from your IP address.", target_nick);
		return;
	}

	Client *target = find_client(target_nick, NULL);
	if (target)
	{
		if (target == client)
		{
			sendnotice(client, "You cannot ghost yourself.");
			return;
		}
		sendnotice(client, "Ghosting %s...", target_nick);
		exit_client(target, NULL, "GHOST command used");
	} else
	{
		sendnotice(client, "%s is not online.", target_nick);
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
	} else if (pass_bang && (!pass_colon || pass_bang < pass_colon))
	{
		pass = pass_bang;
		force_ghost = 1;
	}

	if (!pass)
		goto passthrough;

	*pass++ = '\0';
	if (client->local)
		safe_strdup(client->local->passwd, pass);

	UdbRecord *rec = (udb_ctx && udb_ctx->nicks) ? udb_record_find(clean_nick, udb_ctx->nicks) : NULL;

	if (rec && udb_check_password(pass, rec, client) && udb_nick_access_allowed(client, rec))
	{
		Client *acptr = find_client(clean_nick, NULL);
		if (acptr && acptr != client)
		{
			if (force_ghost)
			{
				char quit_msg[128];
				snprintf(quit_msg, sizeof(quit_msg), "Ghosted (Nick taken by %s)", client->name);
				exit_client(acptr, NULL, quit_msg);
			} else
			{
				sendnotice(client, "This nickname is currently in use. If you are the owner, you can recover it by typing /NICK %s!Password", clean_nick);
			}
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

	UdbRecord *nick_rec = udb_record_find(newnick, udb_ctx->nicks);
	if (nick_rec)
	{
		UdbRecord *forbid = udb_record_find(NKEY_FORBID, nick_rec);
		if (forbid)
		{
			*reject_reason = "This nick is forbidden.";
			return HOOK_DENY;
		}

		/* If client is already this nick and identified with +r, allow without re-entering password */
		if (!strcasecmp(client->name, newnick) && has_user_mode(client, 'r'))
			return udb_nick_access_allowed(client, nick_rec) ? HOOK_CONTINUE : HOOK_DENY;

		const char *pass = client->local ? client->local->passwd : NULL;
		if (pass && udb_check_password(pass, nick_rec, client) &&
		    udb_nick_access_allowed(client, nick_rec))
		{
			return HOOK_CONTINUE;
		}

		static char reject_buf[256];
		snprintf(reject_buf, sizeof(reject_buf), "This nick is registered and requires a password and an authorized IP. Use /NICK %s:Password", newnick);
		*reject_reason = reject_buf;
		return HOOK_DENY;
	}
	return HOOK_CONTINUE;
}

static int udb_hook_nick_change(Client *client, MessageTag *mtags, const char *newnick)
{
	if (!udb_ctx || !udb_ctx->nicks)
		return 0;
	if (!MyConnect(client))
		return 0;

	UdbRecord *old_rec = udb_record_find(client->name, udb_ctx->nicks);
	UdbRecord *new_rec = udb_record_find(newnick, udb_ctx->nicks);

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

	UdbRecord *new_rec = udb_record_find(client->name, udb_ctx->nicks);
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

	UdbRecord *nick_rec = udb_record_find(client->name, udb_ctx->nicks);
	if (nick_rec)
	{
		udb_nick_apply(client, nick_rec, 0);
	}
	return 0;
}

int udb_nicks_init(ModuleInfo *modinfo)
{
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
/* UDB Channels Module for UnrealIRCd 6 */

typedef struct UdbPendingChannelAuth UdbPendingChannelAuth;
typedef struct UdbChannelModeState UdbChannelModeState;
typedef struct UdbInviteGrant UdbInviteGrant;
typedef struct UdbBanOwner UdbBanOwner;
typedef struct UdbBanSnapshot UdbBanSnapshot;

struct UdbPendingChannelAuth {
	UdbPendingChannelAuth *next;
	char channel[CHANNELLEN + 1];
};

struct UdbChannelModeState {
	char *value;
};

struct UdbInviteGrant {
	UdbInviteGrant *next;
	char channel[CHANNELLEN + 1];
	time_t expires;
};

struct UdbBanOwner {
	UdbBanOwner *next;
	char *ban;
	char *owner;
};

struct UdbBanSnapshot {
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
static int udb_hook_pre_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode);
static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic);
CMD_OVERRIDE_FUNC(udb_override_invite);
CMD_OVERRIDE_FUNC(udb_override_mode);

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
	set_channel_mode(channel, NULL, inverse, parabuf ? parabuf : "");
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
	const char *value = fallback_value;

	if (!udb_channel_modes_md)
	{
		udb_channel_reverse_modes(channel, fallback_value);
		return;
	}
	state = moddata_channel(channel, udb_channel_modes_md).ptr;
	if (state && state->value)
		value = state->value;
	udb_channel_reverse_modes(channel, value);
	if (state)
	{
		safe_free(state->value);
		safe_free(state);
		moddata_channel(channel, udb_channel_modes_md).ptr = NULL;
	}
}

static void udb_channel_set_persistent(Channel *channel, int enabled)
{
	/* +P is supplied by an optional native module; do not emulate it. */
	if (!find_channel_mode_handler('P'))
		return;
	if (enabled && !has_channel_mode(channel, 'P'))
		set_channel_mode(channel, NULL, "+P", "");
	else if (!enabled && has_channel_mode(channel, 'P'))
		set_channel_mode(channel, NULL, "-P", "");
}

static void udb_channel_invite_grant_set(Client *client, Channel *channel)
{
	UdbInviteGrant *grant;

	if (!MyUser(client) || !udb_channel_invite_grant_md)
		return;
	for (grant = moddata_local_client(client, udb_channel_invite_grant_md).ptr;
	     grant; grant = grant->next)
	{
		if (!strcasecmp(grant->channel, channel->name))
		{
			grant->expires = TStime() + UDB_INVITE_GRANT_TTL;
			return;
		}
	}
	grant = safe_alloc(sizeof(*grant));
	strlcpy(grant->channel, channel->name, sizeof(grant->channel));
	grant->expires = TStime() + UDB_INVITE_GRANT_TTL;
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
	for (owner = moddata_channel(channel, udb_channel_ban_owners_md).ptr;
	     owner; owner = owner->next)
		if (!mycmp(owner->ban, ban))
			return owner;
	return NULL;
}

static void udb_channel_ban_owners_prune(Channel *channel)
{
	UdbBanOwner **owner;

	if (!udb_channel_ban_owners_md)
		return;
	for (owner = (UdbBanOwner **)&moddata_channel(channel, udb_channel_ban_owners_md).ptr;
	     *owner;)
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
		if (udb_channel_ban_was_present(snapshot, ban->banstr) ||
		    udb_channel_ban_owner_find(channel, ban->banstr))
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
		} else if (check_channel_access_member(member, "q"))
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
	if (!sub_rec || !sub_rec->data_str)
		return;

	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, chan_rec);
	} else if (!strcmp(subkey, CKEY_MODES))
	{
		udb_channel_apply_modes(channel, sub_rec->data_str);
	} else if (!strcmp(subkey, CKEY_PERSISTENT))
	{
		udb_channel_set_persistent(channel, 1);
	} else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE))
	{
		udb_channel_revoke_udb_admins(channel);
	} else if (!strcmp(subkey, CKEY_TOPIC))
	{
		if (!channel->topic || strcmp(channel->topic, sub_rec->data_str))
		{
			safe_strdup(channel->topic, sub_rec->data_str);
			channel->topic_time = TStime();
			safe_strdup(channel->topic_nick, udb_get_bot_nick(SKEY_CHANSERV, 0));
			if (channel->users > 0)
			{
				sendto_channel(channel, &me, NULL, 0, 0, SEND_LOCAL, NULL,
				               ":%s TOPIC %s :%s", me.name, channel->name, channel->topic);
			}
		}
	}
}

static void udb_channel_remove_record(Channel *channel, UdbRecord *chan_rec, const char *subkey)
{
	if (!strcmp(subkey, CKEY_FOUNDER))
	{
		udb_channel_reconcile_founder(channel, NULL);
	} else if (!strcmp(subkey, CKEY_MODES))
	{
		UdbRecord *mode_rec = udb_record_find(CKEY_MODES, chan_rec);
		udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
	} else if (!strcmp(subkey, CKEY_PERSISTENT))
	{
		udb_channel_set_persistent(channel, 0);
	} else if (!strcmp(subkey, CKEY_PASS) || !strcmp(subkey, CKEY_CHALLENGE))
	{
		udb_channel_revoke_udb_admins(channel);
	} else if (!strcmp(subkey, CKEY_TOPIC))
	{
		udb_channel_clear_topic(channel);
	} else if (!strcasecmp(subkey, chan_rec->key))
	{
		UdbRecord *mode_rec = udb_record_find(CKEY_MODES, chan_rec);
		udb_channel_reconcile_founder(channel, NULL);
		udb_channel_revoke_udb_admins(channel);
		udb_channel_remove_modes(channel, mode_rec ? mode_rec->data_str : NULL);
		udb_channel_set_persistent(channel, 0);
		if (udb_record_find(CKEY_TOPIC, chan_rec))
			udb_channel_clear_topic(channel);
	}
}

static int udb_hook_pre_local_join(Client *client, Channel *channel, const char *key)
{
	UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
	UdbRecord *pass_rec;
	if (!chan_rec)
		return HOOK_CONTINUE;

	UdbRecord *forbid_rec = udb_record_find(CKEY_FORBID, chan_rec);
	if (forbid_rec)
	{
		return HOOK_CONTINUE; /* Let can_join handle the reject with proper numeric */
	}

	if (udb_channel_is_identified_founder(client, chan_rec))
		return HOOK_ALLOW; /* Bypass bans/keys/invite */

	/* CAN_JOIN already verified this credential. Record it only after all
     * regular join checks have succeeded, immediately before membership. */
	pass_rec = udb_record_find(CKEY_PASS, chan_rec);
	if (pass_rec && pass_rec->data_str && *pass_rec->data_str && key &&
	    udb_check_password(key, chan_rec, client))
	{
		/* A supplied password remains an admin authentication, even if invited. */
		udb_channel_invite_grant_take(client, channel, 1);
		udb_channel_pending_auth_set(client, channel);
	} else
	{
		udb_channel_invite_grant_take(client, channel, 1);
	}
	return HOOK_CONTINUE;
}

static int udb_hook_can_join(Client *client, Channel *channel, const char *key, char **errmsg)
{
	static char errbuf[512];
	UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
	if (!chan_rec)
		return 0;

	UdbRecord *forbid_rec = udb_record_find(CKEY_FORBID, chan_rec);
	if (forbid_rec)
	{
		snprintf(errbuf, sizeof(errbuf), "%%s :%s", forbid_rec->data_str ? forbid_rec->data_str : "Channel is forbidden");
		*errmsg = errbuf;
		return ERR_FORBIDDENCHANNEL;
	}

	int is_founder = udb_channel_is_identified_founder(client, chan_rec);
	int has_invite_grant = udb_channel_invite_grant_take(client, channel, 0);

	UdbRecord *pass_rec = udb_record_find(CKEY_PASS, chan_rec);
	if (pass_rec && pass_rec->data_str && *pass_rec->data_str && !is_founder && !has_invite_grant)
	{
		if (!key || !udb_check_password(key, chan_rec, client))
		{
			*errmsg = STR_ERR_BADCHANNELKEY;
			return ERR_BADCHANNELKEY;
		}
	}

	UdbRecord *access_rec = udb_record_find(CKEY_ACCESS, chan_rec);
	if (access_rec && !is_founder)
	{
		UdbRecord *acc_entry = udb_record_find(client->name, access_rec);
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
	UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
	if (!chan_rec)
		return;

	int is_founder = udb_channel_is_identified_founder(client, chan_rec);

	if (channel->users == 1)
	{
		UdbRecord *susp_rec = udb_record_find(CKEY_SUSPENDED, chan_rec);

		/* A registered channel assigns founder authority exclusively as +q. */
		if (!IsServer(client) && !IsULine(client))
		{
			set_channel_mode(channel, mtags, "-o", client->name);
		}

		if (!susp_rec)
		{
			set_channel_mode(channel, mtags, "+r", "");
		}

		udb_channel_apply_record(channel, chan_rec, CKEY_MODES, 0);
		udb_channel_apply_record(channel, chan_rec, CKEY_TOPIC, 0);
		if (udb_record_find(CKEY_PERSISTENT, chan_rec))
			udb_channel_set_persistent(channel, 1);
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
	return modes && (strchr(modes, '+') || strchr(modes, '-'));
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

static int udb_channel_blocks_ban_removal(Client *client, Channel *channel,
                                          int parc, const char *parv[])
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
		handler = find_channel_mode_handler(*modes);
		takes_parameter = handler && handler->paracount &&
		                  (what == MODE_ADD || handler->unset_with_param);
		if (!takes_parameter || param >= parc)
			continue;
		if (*modes == 'b' && what == MODE_DEL)
		{
			const char *ban = clean_ban_mask(parv[param], MODE_DEL, EXBTYPE_BAN,
			                                 client, channel, 0);
			UdbBanOwner *owner = udb_channel_ban_owner_find(channel, ban);
			if (owner && strcasecmp(owner->owner, client->name))
			{
				sendnotice(client, "You may not remove the UDB-protected ban %s", ban);
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
	chan_rec = channel ? udb_record_find(channel->name, udb_ctx->channels) : NULL;
	pass_rec = chan_rec ? udb_record_find(CKEY_PASS, chan_rec) : NULL;
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
		sendnotice(client, "Password INVITE requires a local target");
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
	UdbRecord *chan_rec, *options_rec;
	UdbBanSnapshot *snapshot = NULL;
	int is_founder;
	int protect_bans;

	if (!MyUser(client) || parc < 3 || !IsChannelName(parv[1]))
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	channel = find_channel(parv[1]);
	chan_rec = (channel && udb_ctx && udb_ctx->channels) ? udb_record_find(channel->name, udb_ctx->channels) : NULL;
	options_rec = chan_rec ? udb_record_find(CKEY_OPTIONS, chan_rec) : NULL;
	if (!chan_rec)
	{
		CALL_NEXT_COMMAND_OVERRIDE();
		return;
	}
	strlcpy(channel_name, channel->name, sizeof(channel_name));
	is_founder = udb_channel_is_identified_founder(client, chan_rec);
	if (options_rec && (options_rec->data_num & UDB_CHOPT_LOCK_MODES) &&
	    udb_channel_mode_has_change(parv[2]) && !is_founder)
	{
		sendnotice(client, "You do not have permission to change modes in %s (locked by UDB)", channel->name);
		return;
	}
	protect_bans = options_rec && (options_rec->data_num & UDB_CHOPT_PROTECT_BANS);
	udb_channel_ban_owners_prune(channel);
	if (protect_bans && !is_founder && !IsOper(client) &&
	    udb_channel_blocks_ban_removal(client, channel, parc, parv))
		return;
	/* Retain local ban ownership even before the protection option is enabled. */
	if (udb_channel_mode_has_ban_add(parv[2]))
		snapshot = udb_channel_ban_snapshot(channel);
	CALL_NEXT_COMMAND_OVERRIDE();
	channel = find_channel(channel_name);
	if (snapshot)
	{
		if (channel)
			udb_channel_track_new_bans(channel, client, snapshot);
		udb_channel_ban_snapshot_free(snapshot);
	}
	if (channel)
		udb_channel_ban_owners_prune(channel);
}

static const char *udb_hook_pre_topic(Client *client, Channel *channel, const char *topic)
{
	if (IsServer(client))
		return topic;

	UdbRecord *chan_rec = udb_record_find(channel->name, udb_ctx->channels);
	if (!chan_rec)
		return topic;

	UdbRecord *topic_rec = udb_record_find(CKEY_TOPIC, chan_rec);
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
	return 0;
}

/* End of udb_channels.c.inc */

/* IP management: clones, nolines, host overrides */
/* Inlined: udb_ips.c.inc */
/* udb_ips.inc.c
 * Implements IP and host tracking and restrictions for UDB.
 */

typedef struct UdbIpHostState UdbIpHostState;
struct UdbIpHostState {
	char key[256];
	char realhost[HOSTLEN + 1];
	char cloakedhost[HOSTLEN + 1];
	char *virthost;
	long host_umodes;
	int derived_vhost;
};

static ModDataInfo *udb_ip_host_md = NULL;

static void udb_ip_restore_host(Client *client, const char *ip_key);

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
	return tkl && tkl->set_by && tkl->ptr.banexception &&
	       tkl->ptr.banexception->reason &&
	       !strcmp(tkl->set_by, "UDB") &&
	       !strcmp(tkl->ptr.banexception->reason, "UDB Nolines Exemption");
}

static int udb_ip_is_throttle_exempt(UdbRecord *ip_rec)
{
	UdbRecord *nolines = udb_record_find(IKEY_NOLINES, ip_rec);

	/* 'c' is UnrealIRCd's connect-flood exception type. */
	return nolines && nolines->data_str &&
	       (strchr(nolines->data_str, 'c') || strchr(nolines->data_str, 'C'));
}

static int udb_ip_client_matches(Client *client, const char *ip_key)
{
	UdbIpHostState *state;

	if (!client || !MyConnect(client) || !client->user)
		return 0;
	state = udb_ip_host_md ? moddata_local_client(client, udb_ip_host_md).ptr : NULL;
	return (client->ip && !strcasecmp(client->ip, ip_key)) ||
	       (state && !strcasecmp(state->key, ip_key)) ||
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
}

static void udb_ip_restore_host(Client *client, const char *ip_key)
{
	UdbIpHostState *state = moddata_local_client(client, udb_ip_host_md).ptr;

	if (!state || strcasecmp(state->key, ip_key))
		return;
	strlcpy(client->user->realhost, state->realhost, sizeof(client->user->realhost));
	strlcpy(client->user->cloakedhost, state->cloakedhost, sizeof(client->user->cloakedhost));
	safe_strdup(client->user->virthost, state->virthost);
	client->umodes = (client->umodes & ~(UMODE_HIDE | UMODE_SETHOST)) |
	                 state->host_umodes;
	udb_ip_host_state_free(&moddata_local_client(client, udb_ip_host_md));
	moddata_local_client(client, udb_ip_host_md).ptr = NULL;
}

static const char *udb_ip_explicit_vhost(Client *client)
{
	UdbRecord *nick_rec;
	UdbRecord *vhost_rec;

	if (!client || !client->user || !udb_ctx || !udb_ctx->nicks)
		return NULL;
	nick_rec = udb_record_find(client->name, udb_ctx->nicks);
	if (!nick_rec && strcmp(client->user->account, "*"))
		nick_rec = udb_record_find(client->user->account, udb_ctx->nicks);
	vhost_rec = nick_rec ? udb_record_find(NKEY_VHOST, nick_rec) : NULL;
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
	if (!HMAC(EVP_sha256(), key, sizeof(key), (unsigned char *)input, strlen(input),
	          digest, &digestlen) ||
	    digestlen < 16)
		return 0;
	for (i = 0; i < 16; i++)
		snprintf(host + (i * 2), hostlen - (i * 2), "%02x", digest[i]);
	strlcat(host, udb_ctx->suffix, hostlen);
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
			/* A nick vhost supersedes a derived vhost without restoring over it. */
			if (state && state->derived_vhost)
			{
				udb_ip_host_state_free(&moddata_local_client(client, udb_ip_host_md));
				moddata_local_client(client, udb_ip_host_md).ptr = NULL;
			}
			continue;
		}
		if (!udb_ip_derive_vhost(client, host, sizeof(host)))
		{
			if (state && state->derived_vhost)
			{
				userhost_save_current(client);
				udb_ip_restore_host(client, state->key);
				userhost_changed(client);
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
		sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, host);
		userhost_changed(client);
	}
}

static void udb_ip_reconcile_host(const char *ip_key, const char *host)
{
	Client *client;

	list_for_each_entry(client, &lclient_list, lclient_node)
	{
		if (!udb_ip_client_matches(client, ip_key))
			continue;
		if (host)
			udb_ip_apply_host(client, ip_key, host);
		else
			udb_ip_restore_host(client, ip_key);
	}
}

static void udb_ip_apply_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey, int is_new)
{
	(void)is_new;
	if (!strcmp(subkey, IKEY_NOLINES))
	{
		UdbRecord *nolines = udb_record_find(IKEY_NOLINES, ip_rec);
		TKL *tkl = find_tkl_banexception(TKL_EXCEPTION, "*", ip_key, 0);

		if (udb_ip_tkl_is_owned(tkl))
		{
			tkl_del_line(tkl);
			tkl = NULL;
		}
		if (!tkl && nolines && nolines->data_str && *nolines->data_str)
		{
			tkl_add_banexception(TKL_EXCEPTION, "*", ip_key, NULL,
			                     "UDB Nolines Exemption", "UDB", 0, TStime(), 0,
			                     nolines->data_str, 0);
		}
	} else if (!strcmp(subkey, IKEY_HOST))
	{
		UdbRecord *host = udb_record_find(IKEY_HOST, ip_rec);
		if (host && host->data_str && *host->data_str)
			udb_ip_reconcile_host(ip_key, host->data_str);
	}
}

static void udb_ip_remove_record(const char *ip_key, UdbRecord *ip_rec, const char *subkey)
{
	if (!strcmp(subkey, IKEY_NOLINES))
	{
		TKL *tkl;
		if ((tkl = find_tkl_banexception(TKL_EXCEPTION, "*", ip_key, 0)) &&
		    udb_ip_tkl_is_owned(tkl))
		{
			tkl_del_line(tkl);
		}
	} else if (!strcmp(subkey, IKEY_HOST))
	{
		udb_ip_reconcile_host(ip_key, NULL);
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

	if (ip_rec)
	{
		/* Apply Clone Limit */
		sub_rec = udb_record_find(IKEY_CLONES, ip_rec);
		if (sub_rec && sub_rec->data_num > 0)
			limit = (int)sub_rec->data_num;

		/* Apply Host Override */
		sub_rec = udb_record_find(IKEY_HOST, ip_rec);
		if (sub_rec && sub_rec->data_str && *sub_rec->data_str && client->user)
			udb_ip_apply_host(client, ip_rec->key, sub_rec->data_str);

		if (udb_ip_is_throttle_exempt(ip_rec))
			return HOOK_CONTINUE;
	}
	udb_ip_refresh_derived_hosts();

	/* Fallback to global clones if no specific IP limit */
	if (limit == 0 && udb_ctx && udb_ctx->settings)
	{
		UdbRecord *g_clones = udb_record_find(SKEY_CLONES, udb_ctx->settings);
		if (g_clones && g_clones->data_num > 0)
			limit = (int)g_clones->data_num;
	}

	if (limit <= 0)
		return 0;

	int clone_count = 0;
	Client *c;
	list_for_each_entry(c, &lclient_list, lclient_node)
	{
		if (c->ip && !strcmp(c->ip, client->ip))
			clone_count++;
	}

	if (clone_count < limit)
		return 0;

	const char *quit_msg = (udb_ctx && udb_ctx->quit_clones) ? udb_ctx->quit_clones : "Too many connections from your IP";

	udb_log(ULOG_INFO, "UDB_CLONES", client,
	        "Rejecting $client.ip (Exceeds UDB clone limit of $limit)",
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
/* udb_lines.inc.c
 * Implements K-line, Z-line, Shun, Q-line, and Spamfilter support for UDB.
 */

static UdbRecord *udb_line_owner(UdbRecord *rec)
{
	while (rec && rec->parent && rec->parent->parent &&
	       rec->parent->parent->parent)
		rec = rec->parent;

	if (!rec || !rec->parent || !rec->parent->parent ||
	    rec->parent->parent->parent)
		return NULL;
	return rec;
}

static void udb_line_split_mask(const char *mask, char *user, size_t usersz,
                                char *host, size_t hostsz)
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
	} else
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

	if (strncmp(stored, UDB_SPAMFILTER_B64_PREFIX,
	            strlen(UDB_SPAMFILTER_B64_PREFIX)))
	{
		if (!*stored || strlen(stored) >= patternsz)
			return 0;
		strlcpy(pattern, stored, patternsz);
		return 1;
	}

	encoded = stored + strlen(UDB_SPAMFILTER_B64_PREFIX);
	encoded_len = strlen(encoded);
	if (!encoded_len || encoded_len % 4 ||
	    encoded_len > ((patternsz - 1 + 2) / 3) * 4)
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
	if (!decoded_len || decoded_len >= patternsz)
		return 0;
	n = b64_decode(encoded, (unsigned char *)pattern, patternsz - 1);
	if (n < 0 || (size_t)n != decoded_len || memchr(pattern, '\0', decoded_len))
		return 0;
	pattern[decoded_len] = '\0';
	if (b64_encode((unsigned char *)pattern, decoded_len, canonical,
	               sizeof(canonical)) != (int)encoded_len ||
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
		return TKLIsSpamfilter(tkl) && (tkl->type & TKL_GLOBAL) &&
		       tkl->ptr.spamfilter && tkl->ptr.spamfilter->match &&
		       !strcmp(tkl->ptr.spamfilter->match->str, pattern);
	if (type == 'Q')
		return TKLIsNameBan(tkl) && (tkl->type & TKL_GLOBAL) &&
		       tkl->ptr.nameban && !strcasecmp(tkl->ptr.nameban->name, pattern);

	udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));
	return TKLIsServerBan(tkl) && (tkl->type & TKL_GLOBAL) &&
	       ((type == 'G' && (tkl->type & TKL_KILL)) ||
	        (type == 'Z' && (tkl->type & TKL_ZAP)) ||
	        (type == 'S' && (tkl->type & TKL_SHUN))) &&
	       tkl->ptr.serverban &&
	       !strcasecmp(tkl->ptr.serverban->usermask, user) &&
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
		udb_log(ULOG_ERROR, "UDB_SPAMF_PATTERN", NULL,
		        "Invalid spamfilter pattern: $pattern",
		        log_data_string("pattern", line_rec->key), NULL);
		return;
	}
	if (type != 'F')
		strlcpy(pattern, line_rec->key, sizeof(pattern));

	udb_line_remove_owned(type, pattern);

	const char *reason = NULL;
	raz = udb_record_find(KKEY_REASON, line_rec);
	if (raz && raz->data_str)
	{
		reason = raz->data_str;
	} else if (line_rec->data_str)
	{
		reason = line_rec->data_str;
	}

	if (!reason)
		return;

	dur = udb_record_find(KKEY_DURATION, line_rec);
	expires = dur && dur->data_num ? TStime() + (time_t)dur->data_num : 0;

	if (type == 'F')
	{
		UdbRecord *tip = udb_record_find(KKEY_TYPE, line_rec);
		UdbRecord *acc = udb_record_find(KKEY_ACTION, line_rec);

		if (tip && acc && tip->data_str && acc->data_str)
		{
			int target = spamfilter_getconftargets(tip->data_str);
			BanActionValue act_val = banact_stringtoval(acc->data_str);
			BanAction *action = banact_value_to_struct(act_val);

			const char *err = NULL;
			Match *match = target > 0 && action ? unreal_create_match(MATCH_PCRE_REGEX, pattern, &err) : NULL;
			if (match)
			{
				tkl_add_spamfilter(TKL_SPAMF | TKL_GLOBAL, pattern, target, action, match,
				                   pattern, NULL, "UDB", expires, TStime(),
				                   dur ? (time_t)dur->data_num : 0, reason, 0, 0, 0);
			} else
			{
				udb_log(ULOG_ERROR, "UDB_SPAMF_ERROR", NULL, "Failed to compile spamfilter regex: $regex ($err)",
				        log_data_string("regex", pattern),
				        log_data_string("err", err ? err : "unknown error"), NULL);
			}
		}
	} else
	{
		char user[128];
		char host[128];
		udb_line_split_mask(pattern, user, sizeof(user), host, sizeof(host));

		if (type == 'G')
		{
			tkl_add_serverban(TKL_KILL | TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", expires, TStime(), 0, 0);
		} else if (type == 'Z')
		{
			tkl_add_serverban(TKL_ZAP | TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", expires, TStime(), 0, 0);
		} else if (type == 'S')
		{
			tkl_add_serverban(TKL_SHUN | TKL_GLOBAL, user, host, NULL,
			                  reason, "UDB", expires, TStime(), 0, 0);
		} else if (type == 'Q')
		{
			tkl_add_nameban(TKL_NAME | TKL_GLOBAL, pattern, 0, reason, "UDB",
			                expires, TStime(), 0);
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
	} else
	{
		strlcpy(pattern, line_rec->key, sizeof(pattern));
	}
	udb_line_remove_owned(type, pattern);
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

static int udb_query_is_secret(const UdbRecord *rec)
{
	return rec && rec->key && (!strcmp(rec->key, NKEY_PASS) || !strcmp(rec->key, NKEY_CHALLENGE) || !strcmp(rec->key, SKEY_CRYPT_KEY));
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
		sendto_one(client, NULL, ":%s 339 %s :Insufficient parameters. Syntax: /DBQ [server] <block>[::path]",
		           me.name, client->name);
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
	} else
	{
		safe_strdup(query_str, parv[1]);
	}

	block = udb_block_by_letter(query_str[0]);
	if (!block)
	{
		sendto_one(client, NULL, ":%s 339 %s :Block %c does not exist.",
		           me.name, client->name, query_str[0]);
		safe_free(query_str);
		return;
	}

	/* Query for block summary only (e.g. "/DBQ N") */
	if (query_str[1] == '\0')
	{
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
	if (cur[0] != ':' || cur[1] != ':' || cur[2] == '\0')
	{
		sendto_one(client, NULL, ":%s 339 %s :Invalid block format.",
		           me.name, client->name);
		safe_free(query_str);
		return;
	}
	cur += 2;

	rec = block->tree;
	while ((ds = strstr(cur, "::")))
	{
		*ds = '\0';
		rec = udb_record_find(cur, rec);
		if (!rec)
			goto notfound;
		cur = ds + 2;
	}
	rec = udb_record_find(cur, rec);

	if (!rec)
	{
	notfound:
		sendto_one(client, NULL, ":%s 339 %s :Block not found: %s",
		           me.name, client->name, query_str);
		safe_free(query_str);
		return;
	}

	/* Display the found record */
	if (rec->data_str)
	{
		sendto_one(client, NULL, ":%s 339 %s :DBQ %s %s",
		           me.name, client->name, query_str,
		           udb_query_is_secret(rec) ? "<redacted>" : rec->data_str);
	} else if (rec->data_num)
	{
		sendto_one(client, NULL, ":%s 339 %s :DBQ %s %lu",
		           me.name, client->name, query_str, rec->data_num);
	} else
	{
		UdbRecord *child;
		for (child = rec->child; child; child = child->sibling)
		{
			if (child->data_str)
				sendto_one(client, NULL, ":%s 339 %s :DBQ %s::%s %s",
				           me.name, client->name, query_str, child->key,
				           udb_query_is_secret(child) ? "<redacted>" : child->data_str);
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
	CommandAdd(modinfo->handle, "DBQ", cmd_dbq, MAXPARA, CMD_USER | CMD_SERVER);
}

/* End of udb_query.c.inc */

/* Engine, block, configuration, and module lifecycle coordination */
/* Inlined: udb_lifecycle.c.inc */
/* UDB module and database lifecycle coordination. */

static UdbBlock *udb_block_create(char letter, const char *name)
{
	UdbBlock *b = safe_alloc(sizeof(UdbBlock));
	char path[512];

	b->letter = letter;
	b->version = 1;
	b->tree = udb_record_create(NULL);
	b->tree->block_idx = (unsigned char)udb_block_letter_to_index(letter);
	safe_strdup(b->tree->key, name);
	b->tree->data_num = 1;

	snprintf(path, sizeof(path), "udb_%c.db", letter);
	safe_strdup(b->filepath, path);
	convert_to_absolute_path(&b->filepath, PERMDATADIR);

	udb_ctx->blocks[(unsigned char)letter] = b;
	b->next = udb_ctx->block_list;
	udb_ctx->block_list = b;
	udb_ctx->block_count++;
	return b;
}

static void udb_block_set_context_root(UdbBlock *block)
{
	if (!udb_ctx || !block)
		return;
	switch (block->letter)
	{
		case 'N':
			udb_ctx->nicks = block->tree;
			break;
		case 'C':
			udb_ctx->channels = block->tree;
			break;
		case 'I':
			udb_ctx->ips = block->tree;
			break;
		case 'S':
			udb_ctx->settings = block->tree;
			break;
		case 'L':
			udb_ctx->links = block->tree;
			break;
		case 'K':
			udb_ctx->lines = block->tree;
			break;
	}
}

static void udb_block_reset(UdbBlock *block)
{
	char *name = NULL;
	int block_idx;

	if (!block)
		return;
	if (block->letter == 'I' && block->tree)
	{
		UdbRecord *rec;
		for (rec = block->tree->child; rec; rec = rec->sibling)
		{
			UdbRecord *child;
			for (child = rec->child; child; child = child->sibling)
				udb_ip_remove_record(rec->key, rec, child->key);
		}
	}
	if ((block->letter == 'S' || block->letter == 'L') && block->tree)
	{
		UdbRecord *rec;
		for (rec = block->tree->child; rec; rec = rec->sibling)
			udb_remove_special_record(block, rec);
	}

	if (block->tree && block->tree->key)
		safe_strdup(name, block->tree->key);
	else
		safe_strdup(name, "UDB");
	block_idx = udb_block_letter_to_index(block->letter);
	if (block->tree)
	{
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

static int udb_block_load(UdbBlock *block)
{
	return udb_file_load_block(block);
}

static void udb_block_unload(UdbBlock *block)
{
	if (block->tree)
	{
		udb_record_free_tree(block->tree);
		block->tree = NULL;
	}
}

static void udb_blocks_load_all(void)
{
	UdbBlock *b;
	for (b = udb_ctx->block_list; b; b = b->next)
		udb_block_load(b);
}

static void udb_blocks_save_all(void)
{
	UdbBlock *b;
	for (b = udb_ctx->block_list; b; b = b->next)
		udb_file_save_block(b);
}

static UdbBlock *udb_block_by_letter(char letter)
{
	return udb_ctx ? udb_ctx->blocks[(unsigned char)letter] : NULL;
}

static int udb_engine_init(void)
{
	struct stat st = {0};
	const char *dir;

	udb_ctx = safe_alloc(sizeof(UdbContext));
	udb_hash_init();
	/* Keep the existing directory check and block creation sequence. */
	dir = udb_cfg && udb_cfg->db_directory ? udb_cfg->db_directory : "data/udb";
	if (stat(dir, &st) == -1)
		mkdir(dir, 0700);

	udb_block_create('N', "Nicks");
	udb_block_create('C', "Channels");
	udb_block_create('I', "IPs");
	udb_block_create('S', "Settings");
	udb_block_create('L', "Links");
	udb_block_create('K', "Lines");
	udb_block_set_context_root(udb_ctx->blocks['N']);
	udb_block_set_context_root(udb_ctx->blocks['C']);
	udb_block_set_context_root(udb_ctx->blocks['I']);
	udb_block_set_context_root(udb_ctx->blocks['S']);
	udb_block_set_context_root(udb_ctx->blocks['L']);
	udb_block_set_context_root(udb_ctx->blocks['K']);
	udb_blocks_load_all();
	return 1;
}

static void udb_engine_shutdown(void)
{
	UdbBlock *b;

	if (!udb_ctx)
		return;
	udb_blocks_save_all();
	udb_ips_shutdown();
	for (b = udb_ctx->block_list; b;)
	{
		UdbBlock *next = b->next;
		udb_sync_session_free(b);
		udb_block_unload(b);
		safe_free(b->filepath);
		safe_free(b);
		b = next;
	}
	udb_hash_destroy();
	udb_config_free();
	safe_free(udb_ctx);
	udb_ctx = NULL;
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
	if (udb_engine_init() == 0)
	{
		config_error("[UDB] Failed to initialize database engine");
		return MOD_FAILED;
	}
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
	udb_blocks_load_all();
	udb_nicks_load(modinfo);
	udb_channels_load(modinfo);
	unreal_log(ULOG_INFO, "udb", "UDB_LOADED", NULL,
	           "[UDB] Unreal Database System v" UDB_VERSION " loaded successfully");
	return MOD_SUCCESS;
}

static int udb_module_unload(void)
{
	unreal_log(ULOG_INFO, "udb", "UDB_UNLOADING", NULL,
	           "[UDB] Saving databases and shutting down...");
	udb_blocks_save_all();
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