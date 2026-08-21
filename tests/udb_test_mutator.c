/* Test-only UDB protocol mutator for the isolated UDB harnesses. */
#include "unrealircd.h"
#include <unistd.h>

ModuleHeader MOD_HEADER = {
    "third/udb_test_mutator",
    "1.0",
    "UDB test-only authorized mutation fixture",
    "UnrealIRCd UDB tests",
    "unrealircd-6"};

#define MUTATOR_PATH      "N::udb-test-mutator"
#define MUTATOR_DIRECTORY "UDB_TEST_MUTATOR_DIRECTORY"
#define SETTLEMENT_DELAY  3
#define MUTATOR_GO        "udb-test-mutator-go"
#define MUTATOR_INS_GO    "udb-test-mutator-ins-go"
#define MUTATOR_DEL_GO    "udb-test-mutator-del-go"
#define MUTATOR_DRP_GO    "udb-test-mutator-drp-go"
#define MUTATOR_OPT_GO    "udb-test-mutator-opt-go"

static Client *mutator_peer;
static const char *mutator_value;
static time_t mutator_deadline;
static int mutator_state;
static int mutator_staged_authorization_test;

static int mutator_trigger_exists(const char *name)
{
	const char *directory = getenv(MUTATOR_DIRECTORY);
	char path[512];

	if (!directory || !*directory ||
	    snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path))
		return 0;
	return access(path, F_OK) == 0;
}

static int mutator_server_synced(Client *client)
{
	if (!strcasecmp(me.name, "udb-a.test") && !strcasecmp(client->name, "udb-b.test"))
		mutator_value = "authorized-insert";
	else if (!strcasecmp(me.name, "udb-b.test") && !strcasecmp(client->name, "udb-c.test"))
		mutator_value = "authorized-insert-b-c";
	else if (!strcasecmp(me.name, "udb-c.test") && !strcasecmp(client->name, "udb-b.test"))
		mutator_staged_authorization_test = 1;
	else
		return 0;

	mutator_peer = client;
	mutator_deadline = TStime() + SETTLEMENT_DELAY;
	mutator_state = 0;
	unreal_log(ULOG_INFO, "udb-test-mutator", "UDB_TEST_MUTATOR", client,
	           "[UDB_TEST_MUTATOR] peer synced; waiting for UDB HEL/snapshot settlement", NULL);
	return 0;
}

static int mutator_server_quit(Client *client, MessageTag *mtags)
{
	if (client == mutator_peer)
	{
		mutator_peer = NULL;
		mutator_state = -1;
	}
	return 0;
}

EVENT(udb_test_mutator_event)
{
	if (mutator_staged_authorization_test)
	{
		if (mutator_state || !mutator_peer || !mutator_trigger_exists("udb-test-mutator-staged-authorization-go"))
			return;
		if (!IsServer(mutator_peer) || !MyConnect(mutator_peer))
		{
			mutator_state = -1;
			return;
		}
		/* Re-declare a different selected source so B treats C as non-propagating. */
		sendto_one(mutator_peer, NULL, ":%s DB %s HEL 4 udb-a.test", me.id, mutator_peer->id);
		sendto_one(mutator_peer, NULL, ":%s DB %s BEGIN N attack 00000000", me.id, mutator_peer->id);
		sendto_one(mutator_peer, NULL, ":%s DB %s PUT N attack attack :unauthorized", me.id, mutator_peer->id);
		sendto_one(mutator_peer, NULL, ":%s DB %s END N attack 00000000", me.id, mutator_peer->id);
		sendto_one(mutator_peer, NULL, ":%s DB %s RES N", me.id, mutator_peer->id);
		mutator_state = 1;
		unreal_log(ULOG_INFO, "udb-test-mutator", "UDB_TEST_MUTATOR", mutator_peer,
		           "[UDB_TEST_MUTATOR] emitted unauthorized staged-sync and RES frames", NULL);
		return;
	}
	if (mutator_state < 0 || !mutator_peer || TStime() < mutator_deadline)
		return;
	if (mutator_state == 0 && mutator_trigger_exists(MUTATOR_DRP_GO))
	{
		if (!IsServer(mutator_peer) || !MyConnect(mutator_peer))
		{
			mutator_state = -1;
			return;
		}
		sendto_one(mutator_peer, NULL, ":%s DB %s DRP N", me.id, mutator_peer->id);
		mutator_state = 2;
		unreal_log(ULOG_INFO, "udb-test-mutator", "UDB_TEST_MUTATOR", mutator_peer,
		           "[UDB_TEST_MUTATOR] emitted authorized DRP", NULL);
		return;
	}
	if (mutator_state == 0 && mutator_trigger_exists(MUTATOR_OPT_GO))
	{
		if (!IsServer(mutator_peer) || !MyConnect(mutator_peer))
		{
			mutator_state = -1;
			unreal_log(ULOG_WARNING, "udb-test-mutator", "UDB_TEST_MUTATOR", NULL,
			           "[UDB_TEST_MUTATOR] peer disappeared before mutation", NULL);
			return;
		}
		sendto_one(mutator_peer, NULL, ":%s DB %s OPT N %lld", me.id, mutator_peer->id,
		           (long long)TStime());
		mutator_state = 2;
		unreal_log(ULOG_INFO, "udb-test-mutator", "UDB_TEST_MUTATOR", mutator_peer,
		           "[UDB_TEST_MUTATOR] emitted authorized OPT", NULL);
		return;
	}
	if (mutator_state == 1 &&
	    (mutator_trigger_exists(MUTATOR_GO) || mutator_trigger_exists(MUTATOR_DEL_GO)))
	{
		if (!IsServer(mutator_peer) || !MyConnect(mutator_peer))
		{
			mutator_state = -1;
			return;
		}
		sendto_one(mutator_peer, NULL, ":%s DB %s DEL " MUTATOR_PATH, me.id, mutator_peer->id);
		mutator_state = 2;
		unreal_log(ULOG_INFO, "udb-test-mutator", "UDB_TEST_MUTATOR", mutator_peer,
		           "[UDB_TEST_MUTATOR] emitted authorized DEL", NULL);
		return;
	}
	if (mutator_state != 0 ||
	    !(mutator_trigger_exists(MUTATOR_GO) || mutator_trigger_exists(MUTATOR_INS_GO)))
		return;
	if (!IsServer(mutator_peer) || !MyConnect(mutator_peer))
	{
		mutator_state = -1;
		unreal_log(ULOG_WARNING, "udb-test-mutator", "UDB_TEST_MUTATOR", NULL,
		           "[UDB_TEST_MUTATOR] peer disappeared before mutation", NULL);
		return;
	}

	sendto_one(mutator_peer, NULL, ":%s DB %s INS " MUTATOR_PATH " %s",
	           me.id, mutator_peer->id, mutator_value);
	mutator_state = 1;
	mutator_deadline = TStime() + SETTLEMENT_DELAY;
	unreal_log(ULOG_INFO, "udb-test-mutator", "UDB_TEST_MUTATOR", mutator_peer,
	           "[UDB_TEST_MUTATOR] emitted authorized INS", NULL);
}

MOD_INIT()
{
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNCED, 0, mutator_server_synced);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, mutator_server_quit);
	EventAdd(modinfo->handle, "udb_test_mutator_event", udb_test_mutator_event, NULL, 250, 0);
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
