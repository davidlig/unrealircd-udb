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

struct UdbHelloPeer {
	Client *peer;
	time_t deadline;
	int state;
	int authorizes_us;
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

static int udb_peer_authorizes_us(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 0);

	return udb_has_hello(server) && peer->authorizes_us;
}

static void udb_sync_hello_start(Client *server)
{
	UdbHelloPeer *peer;
	const char *propagator;

	if (!server || !IsServer(server) || !MyConnect(server))
		return;
	peer = udb_hello_peer(server, 1);
	if (peer->state)
		return;
	peer->state = UDB_HEL_WAITING;
	peer->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	propagator = udb_selected_propagator(udb_ctx);
	sendto_one(server, NULL, ":%s DB %s HEL 4 %s", me.id, server->id,
	           propagator ? propagator : "?");
}

static int udb_sync_hello_ack(Client *server)
{
	UdbHelloPeer *peer = udb_hello_peer(server, 1);

	if (peer->state != UDB_HEL_WAITING)
		return 0;
	peer->state = UDB_HEL_CONFIRMED;
	udb_log(ULOG_INFO, "UDB_HEL_CONFIRMED", server,
	        "UDB HEL 4 capability confirmed for directly linked server");
	udb_sync_to_server(server);
	return 1;
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
	{
		if (block->session->peer == peer)
			udb_sync_abort(block, "duplicate BEGIN");
		return 0;
	}
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

static int udb_sync_put(UdbBlock *block, Client *peer, const char *txid,
                        const char *path, const char *data)
{
	UdbSyncSession *session = block ? block->session : NULL;
	char line[4096];

	if (!session || session->peer != peer || strcmp(session->txid, txid))
	{
		if (session && session->peer == peer)
			udb_sync_abort(block, "invalid PUT sequence");
		return UDB_ERR_NO_SYNC;
	}
	if (snprintf(line, sizeof(line), "%s %s", path, data) >= (int)sizeof(line) ||
	    !udb_stage_parse_line(block, session, line))
	{
		udb_sync_abort(block, "invalid PUT payload");
		return UDB_ERR_PARAMS;
	}
	session->deadline = time(NULL) + UDB_SYNC_TIMEOUT;
	return 0;
}

static int udb_checksum_parse(const char *input, unsigned long *checksum)
{
	char *end;
	const char *p;

	if (!input || !*input)
		return 0;
	for (p = input; *p; p++)
		if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
		      (*p >= 'A' && *p <= 'F')))
			return 0;
	errno = 0;
	*checksum = strtoul(input, &end, 16);
	return errno != ERANGE && *end == '\0';
}

static int udb_sync_end(UdbContext *ctx, UdbBlock *block, Client *peer,
                        const char *txid, const char *checksum, unsigned long *digest)
{
	UdbSyncSession *session = block ? block->session : NULL;
	unsigned long received_digest;

	if (!session || session->peer != peer || strcmp(session->txid, txid))
	{
		if (session && session->peer == peer)
			udb_sync_abort(block, "invalid END sequence");
		return UDB_ERR_NO_SYNC;
	}
	*digest = udb_compute_tree_checksum(session->tree);
	if (!udb_checksum_parse(checksum, &received_digest) ||
	    *digest != received_digest ||
	    !udb_file_write_snapshot(block, session->tree, session->record_count) ||
	    !udb_block_commit_stage(ctx, block, session, *digest))
	{
		udb_sync_abort(block, "digest or persistence failure");
		return UDB_ERR_FATAL;
	}
	return 0;
}

static void udb_sync_ack(Client *peer, const char *block)
{
	udb_log(ULOG_INFO, "UDB_SYNC_ACK", peer,
	        "Staged sync acknowledged for block $block", log_data_string("block", block));
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

	/* HEL confirms protocol support; snapshots require the selected data source. */
	if (!udb_peer_authorizes_us(server))
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
	UdbHelloPeer *peer, *next;
	time_t now = time(NULL);

	for (block = udb_ctx ? udb_ctx->block_list : NULL; block; block = block->next)
		if (block->session && block->session->deadline <= now)
			udb_sync_abort(block, "timeout");
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
