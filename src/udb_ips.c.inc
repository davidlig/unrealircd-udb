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
