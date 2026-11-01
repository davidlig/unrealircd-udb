/* Test-only external runtime state fixture for the isolated UDB harnesses.
 *
 * Block N ownership tests need an external source for state no stock service
 * command can change (snomasks). Commands are server-only and never part of
 * the production module.
 */
#include "unrealircd.h"

ModuleHeader MOD_HEADER = {
    "third/udb_test_state",
    "1.0",
    "UDB test-only external runtime state fixture",
    "UnrealIRCd UDB tests",
    "unrealircd-6"};

static void udb_test_state_report_snomask(Client *target)
{
	sendto_one(target, NULL, ":%s NOTICE %s :UDBTEST SNOMASK=%s", me.name, target->name,
	           target->user && target->user->snomask ? target->user->snomask : "-");
}

static void udb_test_state_report_oper(Client *target)
{
	const char *oc = get_operclass(target);
	int in_oper_list = 0;
	Client *c;
	list_for_each_entry(c, &oper_list, special_node)
	{
		if (c == target)
			in_oper_list++;
	}
	sendto_one(target, NULL, ":%s NOTICE %s :UDBTEST OPER=%d OPERCLASS=%s OPERCOUNT=%d OPERLIST=%d",
	           me.name, target->name,
	           IsOper(target) ? 1 : 0,
	           oc ? oc : "-",
	           irccounts.operators,
	           in_oper_list);
}

static int udb_test_state_local_oper(Client *client, int add, const char *oper_block, const char *operclass)
{
	if (!add || strcmp(client->name, "ophook"))
		return 0;

	userhost_save_current(client);
	safe_strdup(client->user->virthost, "hook.external.test");
	client->umodes |= UMODE_HIDE | UMODE_SETHOST;
	sendto_server(NULL, 0, 0, NULL, ":%s SETHOST :%s", client->id, client->user->virthost);
	userhost_changed(client);
	return 0;
}

static int udb_test_state_local_join(Client *client, Channel *channel, MessageTag *mtags)
{
	if (!strcmp(client->name, "opkill") && !strcmp(channel->name, "#udb-kill-on-join"))
		exit_client(client, mtags, "UDB test auto-join kill");
	return 0;
}

CMD_FUNC(cmd_udbtest)
{
	Client *target;

	if (parc < 2)
		return;
	/* Supply deterministic GeoIP only while evaluating the real TKL matcher. */
	if (!strcasecmp(parv[1], "MATCHCOUNTRY") && parc >= 4)
	{
		target = find_user(parv[2], NULL);
		if (!target)
			return;
		ModData *md = moddata_client_get_raw(target, "geoip");
		if (!md)
			return;
		void *saved = md->ptr;
		GeoIPResult geo = {0};
		geo.country_code = (char *)parv[3];
		md->ptr = &geo;
		char saved_host[HOSTLEN + 1];
		strlcpy(saved_host, target->user->realhost, sizeof(saved_host));
		if (parc >= 5)
			strlcpy(target->user->realhost, parv[4], sizeof(target->user->realhost));
		char saved_info[sizeof(target->info)], saved_account[sizeof(target->user->account)];
		memcpy(saved_info, target->info, sizeof(saved_info));
		memcpy(saved_account, target->user->account, sizeof(saved_account));
		if (parc >= 6)
			strlcpy(target->info, parv[5], sizeof(target->info));
		if (parc >= 7)
			strlcpy(target->user->account, parv[6], sizeof(target->user->account));
		int count = 0, matched = 0;
		for (TKL *line = tklines[tkl_hash('G')]; line; line = line->next)
		{
			if (!TKLIsServerBan(line) || !line->set_by || strcmp(line->set_by, "UDB:managed"))
				continue;
			char mask[512];
			tkl_uhost(line, mask, sizeof(mask), 0);
			count++;
			matched += !!match_user(mask, target, MATCH_CHECK_REAL);
		}
		memcpy(target->info, saved_info, sizeof(saved_info));
		memcpy(target->user->account, saved_account, sizeof(saved_account));
		md->ptr = saved;
		strlcpy(target->user->realhost, saved_host, sizeof(target->user->realhost));
		sendto_one(client, NULL, ":%s NOTICE %s :UDBTEST MATCH=%d COUNT=%d", me.name, client->name, matched, count);
		return;
	}
	if (!strcasecmp(parv[1], "GETHOST") && parc >= 3)
	{
		target = find_user(parv[2], NULL);
		if (!target || !target->user)
			return;
		sendto_one(target, NULL, ":%s NOTICE %s :UDBTEST REALHOST=%s IP=%s CLOAK=%s VHOST=%s",
			me.name, target->name, target->user->realhost, target->ip ? target->ip : "-",
			target->user->cloakedhost, target->user->virthost ? target->user->virthost : "-");
		return;
	}
	if (!strcasecmp(parv[1], "SNOMASK") && parc >= 4)
	{
		target = find_user(parv[2], NULL);
		if (!target || !IsUser(target) || !target->user)
			return;
		set_snomask(target, NULL);
		if (strcmp(parv[3], "-"))
			set_snomask(target, parv[3]);
		if (target->user->snomask && *target->user->snomask)
			target->umodes |= UMODE_SERVNOTICE;
		else
			target->umodes &= ~UMODE_SERVNOTICE;
		udb_test_state_report_snomask(target);
		return;
	}
	if (!strcasecmp(parv[1], "GETSNOMASK") && parc >= 3)
	{
		target = find_user(parv[2], NULL);
		if (!target || !IsUser(target) || !target->user)
			return;
		udb_test_state_report_snomask(target);
		return;
	}
	if (!strcasecmp(parv[1], "GETOPER") && parc >= 3)
	{
		target = find_user(parv[2], NULL);
		if (!target || !IsUser(target) || !target->user)
			return;
		udb_test_state_report_oper(target);
		return;
	}
}

MOD_INIT()
{
	CommandAdd(modinfo->handle, "UDBTEST", cmd_udbtest, MAXPARA, CMD_SERVER);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_OPER, 100, udb_test_state_local_oper);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 100, udb_test_state_local_join);
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	return MOD_SUCCESS;
}
