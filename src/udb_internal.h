/* UDB internal module interface.
 *
 * This header is intentionally included only by the bundled implementation
 * unit. It centralizes daemon-dependent state and cross-subsystem interfaces.
 */

#ifndef UDB_INTERNAL_H
#define UDB_INTERNAL_H

#include "udb.h"

#include "unrealircd.h"
#include <errno.h>
#include <openssl/hmac.h>

#define UDB_DEFAULT_DB_DIRECTORY   PERMDATADIR
#define UDB_BLOCK_PATH_MAX         512
#define UDB_RECORD_PATH_MAX        4096
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
static void udb_config_free(UdbContext *ctx);
static int udb_database_directory_valid(const char *value);
static char *udb_block_filepath(char letter);
static int udb_module_test(ModuleInfo *modinfo);
static int udb_module_init(ModuleInfo *modinfo);
static int udb_module_load(ModuleInfo *modinfo);
static int udb_module_unload(void);
static int udb_engine_init(void);
static void udb_engine_shutdown(void);
static UdbBlock *udb_block_create(UdbContext *ctx, char letter, const char *name);
static void udb_block_set_context_root(UdbContext *ctx, UdbBlock *block);
static int udb_block_load(UdbContext *ctx, UdbBlock *block);
static void udb_block_unload(UdbContext *ctx, UdbBlock *block);
static void udb_block_reset(UdbContext *ctx, UdbBlock *block);
static void udb_blocks_load_all(UdbContext *ctx);
static void udb_blocks_save_all(UdbContext *ctx);
static UdbBlock *udb_block_by_letter(UdbContext *ctx, char letter);
static UdbRecord *udb_record_find(UdbContext *ctx, const char *key, UdbRecord *parent);
static UdbRecord *udb_record_create(UdbRecord *parent);
static UdbRecord *udb_record_insert(UdbContext *ctx, UdbBlock *block, UdbRecord *parent,
                                    const char *key, const char *data_str,
                                    unsigned long data_num, int persist);
static UdbRecord *udb_record_find_path(UdbContext *ctx, UdbBlock *block, const char *path);
static UdbRecord *udb_record_delete(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int persist);
static void udb_record_free_tree(UdbRecord *rec);
static UdbRecord *udb_record_clone_tree(UdbRecord *rec, UdbRecord *needle,
                                        UdbRecord **needle_clone);
static unsigned int udb_record_count_tree(UdbRecord *rec);
static UdbRecord *udb_record_insert_path(UdbRecord *tree, const char *path,
                                         const char *data);
static void udb_record_delete_tree(UdbRecord *rec);
static void udb_hash_init(UdbContext *ctx);
static void udb_hash_destroy(UdbContext *ctx);
static void udb_hash_insert_record(UdbContext *ctx, UdbRecord *rec, int block_idx, const char *key);
static int udb_hash_remove_record(UdbContext *ctx, UdbRecord *rec, int block_idx, const char *key);
static UdbRecord *udb_hash_find(UdbContext *ctx, int block_idx, const char *key);
static int udb_file_write_snapshot(UdbBlock *block, UdbRecord *tree,
                                   unsigned int record_count);
static int udb_file_save_block(UdbContext *ctx, UdbBlock *block);
static void udb_block_replace_tree(UdbContext *ctx, UdbBlock *block,
                                   UdbRecord *tree, unsigned int record_count);
static int udb_file_load_block(UdbContext *ctx, UdbBlock *block);
static UdbRecord *udb_file_parse_line(UdbContext *ctx, UdbBlock *block, char *line);
static void udb_serialize_tree(UdbRecord *rec, int depth, FILE *fp, char *pathbuf,
                               int pathlen);
static unsigned long udb_crc32(const char *data, size_t len);
static unsigned long udb_compute_block_checksum(UdbBlock *block);
static unsigned long udb_compute_tree_checksum(UdbRecord *tree);
static int udb_stage_parse_line(UdbBlock *block, UdbSyncSession *session,
                                const char *line);
static int udb_block_commit_stage(UdbContext *ctx, UdbBlock *block, UdbSyncSession *session,
                                  unsigned long checksum);
static void udb_sync_session_free(UdbBlock *block);
static int udb_block_letter_to_index(char letter);

static void udb_sync_to_server(Client *server);
static int udb_remote_wins_equal_timestamp(Client *server);
static int udb_has_hello(Client *server);
static int udb_has_staged_sync(Client *server);
static int udb_peer_authorizes_us(Client *server);
static void udb_sync_hello_start(Client *server);
static int udb_sync_hello_ack(Client *server);
static void udb_sync_abort(UdbBlock *block, const char *reason);
static int udb_sync_begin(UdbBlock *block, Client *peer, const char *txid);
static int udb_sync_put(UdbBlock *block, Client *peer, const char *txid,
                        const char *path, const char *data);
static int udb_sync_end(UdbContext *ctx, UdbBlock *block, Client *peer,
                        const char *txid, const char *checksum, unsigned long *digest);
static void udb_sync_ack(Client *peer, const char *block);
static void udb_sync_send_stage(Client *server, UdbBlock *block);
static void udb_sync_server_quit(Client *client);
static int udb_is_propagator(UdbContext *ctx, Client *server);
static const char *udb_selected_propagator(UdbContext *ctx);
static void udb_sendto_confirmed_servers(Client *except, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static void udb_protocol_params_error(Client *client, const char *subcmd);
static void udb_mutation_ins(UdbContext *ctx, Client *client, const char *target,
                             const char *path, const char *data, int is_for_me,
                             int is_broadcast);
static void udb_mutation_del(UdbContext *ctx, Client *client, const char *target,
                             const char *path, int is_for_me, int is_broadcast);
static void udb_mutation_drp(UdbContext *ctx, Client *client, const char *target,
                             char letter, int is_for_me, int is_broadcast);
static void udb_mutation_opt(UdbContext *ctx, Client *client, const char *target,
                             char letter, const char *modified_at, int is_for_me,
                             int is_broadcast);
static void udb_nick_apply(Client *client, UdbRecord *nick_rec, int is_hot_sync);
static void udb_nick_strip(Client *client, UdbRecord *nick_rec);
static void udb_nick_remove_record(UdbBlock *block, UdbRecord *rec);
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
static void udb_channel_apply_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec,
                                     int is_new);
static void udb_channel_remove_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static int udb_channels_load(ModuleInfo *modinfo);
static void udb_ips_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec,
                                 int is_new);
static void udb_ips_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static void udb_ip_refresh_derived_hosts(void);
static void udb_ips_shutdown(void);
static void udb_config_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static void udb_config_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static void udb_lines_apply_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec,
                                   int is_new);
static void udb_lines_remove_effect(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static const char *udb_get_bot_nick(const char *service_key, int force_default);
static const char *udb_get_bot_mask(const char *service_key, int force_default);
static Client *udb_service_source(const char *service_key);
static void udb_send_service_notice(Client *target, const char *service_key,
                                    FORMAT_STRING(const char *pattern), ...)
    __attribute__((format(printf, 3, 4)));
static int udb_ip_reapply_vhost(Client *client);
/* Runtime dispatcher; concrete per-block effects stay in their own modules. */
static int udb_apply_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec, int is_new);
static void udb_remove_special_record(UdbContext *ctx, UdbBlock *block, UdbRecord *rec);
static void udb_apply_tree_effects(UdbContext *ctx, UdbBlock *block);
static void udb_remove_tree_effects(UdbContext *ctx, UdbBlock *block);
static void udb_send_to_debugs(Client *source, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

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

#define udb_log(level, event_id, client, msg, ...) \
	unreal_log(level, "udb", event_id, client, "[UDB] " msg, ##__VA_ARGS__)
#define udb_strdup(dest, src) safe_strdup(dest, src)

#endif /* UDB_INTERNAL_H */
