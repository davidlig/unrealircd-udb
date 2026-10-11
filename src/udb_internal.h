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

#include "udb.h"

#include "udb_storage.h"
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
