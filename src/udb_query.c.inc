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
