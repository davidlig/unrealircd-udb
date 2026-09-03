/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Operclass Inventory (OCL) & Global Operclass View (OCLG)
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 *
 * OCL is the distributed source of truth about the operclasses each
 * participant IRCd currently has loaded.  OCLG is a derived projection (the
 * intersection of every participant inventory with matching effective
 * digests) published to explicit consumer subscribers such as Services.
 * Neither is ever persisted to the udb_*.db blocks.
 */

/* conf.c owns the active operclass list; it is not exported through h.h. */
extern ConfigItem_operclass *conf_operclass;

static UdbOclOrigin *udb_ocl_origins = NULL;
static UdbOclInventory *udb_ocl_local = NULL;
static char udb_ocl_epoch[UDB_OCL_EPOCH_LEN + 1] = "";
static unsigned long udb_ocl_local_generation = 0;
static UdbOclInventory *udb_oclg_view = NULL;
static int udb_oclg_ready = 0;
static unsigned long udb_oclg_generation = 0;

static void udb_ocl_ensure_epoch(void);
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

static ConfigItem_operclass *udb_ocl_operclass_list(void)
{
	return conf_operclass;
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
	list_for_each_entry(server, &global_server_list, client_node)
		if (udb_ocl_is_participant(server) && !IsMe(server))
			udb_ocl_member_add(server->id);
	udb_ocl_membership_prune_invisible();
	udb_ocl_purge_nonmembers();
}

/* ---- Canonical serialization and digests ---- */

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

/* ---- Local inventory construction ---- */

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
	if (node->conf && node->conf->classStruct && node->conf->classStruct->ISA && *node->conf->classStruct->ISA)
	{
		int pidx = udb_ocl_node_find(nodes, count, node->conf->classStruct->ISA);

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
	if (!node->conf || !node->conf->classStruct || !udb_ocl_own_digest(node->conf->classStruct, own))
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

	for (oc = udb_ocl_operclass_list(); oc; oc = oc->next)
	{
		if (!oc->classStruct || !oc->classStruct->name || !*oc->classStruct->name)
			continue;
		if (count >= UDB_OCL_MAX_CLASSES)
		{
			limited = 1;
			break;
		}
		if (udb_ocl_node_find(nodes, count, oc->classStruct->name) >= 0)
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
		strlcpy(nodes[count].name, oc->classStruct->name, sizeof(nodes[count].name));
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

/* ---- Local publish, send and replay ---- */

static void udb_ocl_send_inventory_to(Client *to, const char *origin_sid, UdbOclInventory *inv)
{
	unsigned int i;

	if (!to || !inv)
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
	unsigned int i;

	if (!inv)
		return;
	udb_sendto_confirmed_servers(except, ":%s DB * OCL BEGIN %s %s %lu %u %s", me.id, origin_sid, inv->epoch,
								 inv->generation, inv->count, inv->inventory_digest);
	for (i = 0; i < inv->count; i++)
		udb_sendto_confirmed_servers(except, ":%s DB * OCL ITEM %s %s %lu %s %s", me.id, origin_sid, inv->epoch,
									 inv->generation, inv->entries[i].name, inv->entries[i].digest);
	udb_sendto_confirmed_servers(except, ":%s DB * OCL END %s %s %lu", me.id, origin_sid, inv->epoch, inv->generation);
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

/* ---- OCL receive path ---- */

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
		!udb_ocl_member_exists(origin_sid) ||
		origin_client->direction != direct_peer)
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
			if (origin->current && !strcmp(origin->current->epoch, epoch) &&
				origin->current->generation == generation)
				return; /* already committed */
			if (origin->staging && !strcmp(origin->staging->epoch, epoch) &&
				origin->staging->generation == generation)
				return; /* already staging */
			/* The previous attempt was aborted; rebuild the same high-water snapshot. */
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
	if (!peer || peer->ocl_replayed || peer->state != UDB_HEL_CONFIRMED || !peer->ocl_capable)
		return;
	peer->ocl_replayed = 1;
	if (udb_ocl_local)
		udb_ocl_send_inventory_to(server, me.id, udb_ocl_local);
	for (origin = udb_ocl_origins; origin; origin = origin->next)
	{
		if (origin->member && origin->current)
			udb_ocl_send_inventory_to(server, origin->sid, origin->current);
	}
	if (peer->oclg_subscribed)
		udb_oclg_push(server);
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

/* ---- Instance epoch: generation is monotonic only within one epoch, so a
 * module reload with the same SID cannot make an old receiver reject a fresh
 * generation-1 snapshot as stale. ---- */
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

/* ---- Registry completeness, OCLG projection ---- */

static UdbOclEntry *udb_ocl_find_entry(UdbOclInventory *inv, const char *name)
{
	unsigned int lo = 0, hi = inv ? inv->count : 0;

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
	if (udb_oclg_ready == old_ready && old_view && old_view->count == view->count &&
		!strcmp(old_view->inventory_digest, hex))
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
