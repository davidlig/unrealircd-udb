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
	UdbPropagatorSelection selected;
	const char *propagator;
	int ok;

	if (!server || !IsServer(server) || !MyConnect(server))
		return 0;
	peer = udb_hello_peer(server, 1);
	if (peer->state)
		return 0;
	peer->state = UDB_HEL_WAITING;
	peer->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	if (udb_select_propagator(udb_ctx, 1, &selected))
		propagator = selected.name;
	else
		propagator = udb_propagator_policy_present(udb_ctx) ? "-" : "?";
	ok = udb_send_db_to_one(server, ":%s DB %s HEL 4 %s", me.id, server->id, propagator);
	if (!ok)
	{
		peer->state = 0;
		peer->deadline = 0;
		return 0;
	}
	return 1;
}

static void udb_sync_hello_ack(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 1);

	if (peer->state != UDB_HEL_CONFIRMED)
	{
		peer->state = UDB_HEL_CONFIRMED;
		udb_log(ULOG_INFO, "UDB_HEL_CONFIRMED", server, "UDB HEL 4 capability confirmed for directly linked server");
		udb_propagator_policy_changed(udb_ctx);
		if (!peer->auth_received || peer->authorizes_us)
			udb_sync_to_server(server);
	}
}

static void udb_sync_status_refresh(void)
{
	UdbContext *ctx = udb_ctx;
	int policy_present = udb_propagator_policy_present(ctx);
	UdbPropagatorSelection selected;
	int has_selected = ctx ? udb_select_propagator(ctx, 1, &selected) : 0;
	time_t now = time(NULL);
	int timeout = (udb_cfg && udb_cfg->stale_timeout > 0) ? udb_cfg->stale_timeout : 300;
	UdbStaleAction action = udb_cfg ? udb_cfg->stale_action : UDB_STALE_ACTION_DENY_NEW_CLIENTS;

	if (!policy_present || has_selected)
	{
		/* We have an eligible propagator or we have no policy (bootstrap / standalone) */
		if (udb_sync_status != UDB_SYNC_OK)
		{
			const char *prop_name = has_selected ? selected.name : "none";
			udb_log(ULOG_INFO, "UDB_SYNC_RECOVERED", NULL,
					"UDB synchronization recovered. Selected propagator: $propagator",
					log_data_string("propagator", prop_name));
			udb_sync_status = UDB_SYNC_OK;
		}
		udb_degraded_since = 0;
		return;
	}

	/* Policy is present, but no propagator is eligible (HEL 4 -) */
	if (udb_degraded_since == 0)
		udb_degraded_since = now;

	time_t elapsed = (now >= udb_degraded_since) ? (now - udb_degraded_since) : 0;

	if (elapsed < timeout)
	{
		if (udb_sync_status != UDB_SYNC_DEGRADED)
		{
			const char *source = (udb_cfg && udb_cfg->propagator && *udb_cfg->propagator) ? "local" : "S";
			const char *policy = udb_propagator_policy(ctx);
			udb_log(ULOG_WARNING, "UDB_SYNC_DEGRADED", NULL,
					"No eligible propagator. Database may become stale. Policy source: $source, Policy: $policy",
					log_data_string("source", source), log_data_string("policy", policy ? policy : "none"));
			udb_sync_status = UDB_SYNC_DEGRADED;
		}
	}
	else
	{
		if (udb_sync_status != UDB_SYNC_STALE)
		{
			const char *action_str = (action == UDB_STALE_ACTION_WARN) ? "warn" : "deny-new-clients";
			udb_log(
				ULOG_ERROR, "UDB_SYNC_STALE", NULL,
				"UDB synchronization is stale ($seconds seconds without eligible propagator). Stale action: $action",
				log_data_integer("seconds", (int)elapsed), log_data_string("action", action_str));
			udb_sync_status = UDB_SYNC_STALE;
		}
	}
}

static int udb_hook_stale_pre_connect(Client *client)
{
	if (!client || !MyConnect(client) || IsServer(client))
		return HOOK_CONTINUE;

	if (udb_sync_status == UDB_SYNC_STALE && udb_cfg && udb_cfg->stale_action == UDB_STALE_ACTION_DENY_NEW_CLIENTS)
	{
		udb_log(ULOG_WARNING, "UDB_STALE_DENY_CLIENT", client,
				"Rejecting new local client connection due to stale UDB synchronization");
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

	if (udb_select_propagator(udb_ctx, 1, &selected))
		propagator = selected.name;
	else
		propagator = udb_propagator_policy_present(udb_ctx) ? "-" : "?";
	list_for_each_entry(server, &server_list, special_node)
	{
		if (IsServer(server) && MyConnect(server) && udb_has_hello(server))
			udb_send_db_to_one(server, ":%s DB %s HEL 4 %s", me.id, server->id, propagator);
	}
}

static void udb_sync_abort(UdbBlock *block, const char *reason)
{
	if (!block || !block->session)
		return;
	udb_log(ULOG_WARNING, "UDB_SYNC_ABORT", block->session->peer, "Aborted staged sync of block $block: $reason",
			log_data_string("block", (char[]){block->letter, '\0'}), log_data_string("reason", reason));
	udb_sync_session_free(block);
}

static void udb_propagator_policy_changed(UdbContext *ctx)
{
	UdbBlock *block;
	UdbPropagatorSelection selected;
	Client *peer = NULL;

	if (!ctx)
		return;
	if (!udb_propagator_policy_present(ctx))
	{
		udb_sync_hello_refresh_all();
		udb_sync_status_refresh();
		return;
	}
	if (udb_select_propagator(ctx, 1, &selected) && !selected.is_local)
		peer = selected.peer;
	for (block = ctx->block_list; block; block = block->next)
	{
		/* Replacing block S removes the old tree's effects while the S session
		 * still owns its replacement tree. Do not abort that session from its
		 * own policy callback; the new policy is applied after commit. */
		if (block->session && block->letter != 'S' && block->session->peer != peer)
			udb_sync_abort(block, "propagator policy changed");
		else if (!block->session && block->syncing_from && block->syncing_from != peer)
			block->syncing_from = NULL;
	}
	udb_sync_hello_refresh_all();
	udb_sync_status_refresh();
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
	udb_sync_status_refresh();
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
	udb_propagator_policy_changed(udb_ctx);
}
