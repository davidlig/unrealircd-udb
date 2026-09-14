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
