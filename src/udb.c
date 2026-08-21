/* UDB - Unreal Database System for UnrealIRCd 6
 *
 * A distributed database engine integrated into the IRC daemon, providing
 * persistent nick registration, channel registration, IP management,
 * distributed *lines, and network-wide configuration synchronization.
 *
 * Originally developed by Trocotronic & MaD for UnrealIRCd 3.2.8 (UDB 3.6.1).
 * Migrated to UnrealIRCd 6 module API - 2026.
 *
 * Architecture:
 *   This file is the main module entry point. It #include's all implementation
 *   files (.inc.c) which are compiled into a single shared library (udb.so).
 *   This approach avoids inter-module symbol visibility issues while keeping
 *   the codebase modular and maintainable.
 *
 * License: GNU General Public License v2+
 */

#include "udb_internal.h"

/* ========================================================================
 * Module Header
 * ======================================================================== */

ModuleHeader MOD_HEADER = {
    "third/udb",
    "4.0.0",
    "UDB - Unreal Database System (nick/channel/IP registration & sync)",
    "David Abuín Fontán ('davidlig')",
    "unrealircd-6"};

/* ========================================================================
 * Implementation Files
 *
 * Each file implements a specific subsystem. They share the same compilation
 * unit, so all functions are static and can call each other freely.
 * ======================================================================== */

/* Core database engine: tree, hash, file I/O, record management */
#include "udb_core.c.inc"

/* S2S protocol handler: DB command, server sync */
#include "udb_protocol.c.inc"

/* Nick management: registration, identification, ghost, vhost, oper */
#include "udb_nicks.c.inc"

/* Channel management: registration, founder, modes, topic, access */
#include "udb_channels.c.inc"

/* IP management: clones, nolines, host overrides */
#include "udb_ips.c.inc"

/* Distributed *lines: glines, zlines, shuns, qlines, spamfilters */
#include "udb_lines.c.inc"

/* DBQ query command for users and opers */
#include "udb_query.c.inc"

/* ========================================================================
 * Configuration Test (MOD_TEST)
 *
 * Validates the udb { } configuration block at config load time.
 * ======================================================================== */

static int udb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
static int udb_config_run(ConfigFile *cf, ConfigEntry *ce, int type);
static int udb_config_posttest(int *errs);

MOD_TEST()
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, udb_config_test);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, udb_config_posttest);
	return MOD_SUCCESS;
}

/* ========================================================================
 * Module Initialization (MOD_INIT)
 *
 * Registers all commands, hooks, ModData, and initializes the DB engine.
 * ======================================================================== */

MOD_INIT()
{
	/* Configuration */
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, udb_config_run);

	/* Initialize the database engine */
	if (udb_engine_init() == 0)
	{
		config_error("[UDB] Failed to initialize database engine");
		return MOD_FAILED;
	}

	/* Register subsystem hooks and commands */
	udb_protocol_init(modinfo);
	udb_nicks_init(modinfo);
	udb_channels_init(modinfo);
	udb_ips_init(modinfo);
	udb_lines_init(modinfo);
	udb_query_init(modinfo);

	/* Mark as global: all servers in the network should load this module */
	MARK_AS_GLOBAL_MODULE(modinfo);

	return MOD_SUCCESS;
}

/* ========================================================================
 * Module Load (MOD_LOAD)
 *
 * Called after all modules are initialized. Load database files.
 * ======================================================================== */

MOD_LOAD()
{
	udb_blocks_load_all();
	udb_nicks_load(modinfo);
	udb_channels_load(modinfo);

	unreal_log(ULOG_INFO, "udb", "UDB_LOADED", NULL,
	           "[UDB] Unreal Database System v" UDB_VERSION " loaded successfully");

	return MOD_SUCCESS;
}

/* ========================================================================
 * Module Unload (MOD_UNLOAD)
 *
 * Save all data and free resources.
 * ======================================================================== */

MOD_UNLOAD()
{
	unreal_log(ULOG_INFO, "udb", "UDB_UNLOADING", NULL,
	           "[UDB] Saving databases and shutting down...");

	/* Save all blocks to disk before unloading */
	udb_blocks_save_all();

	/* Free all memory */
	udb_engine_shutdown();

	return MOD_SUCCESS;
}

/* ========================================================================
 * Configuration: udb { } block
 *
 * Example configuration:
 *
 *   udb {
 *       database-directory "data/udb";
 *       propagator "services.mynetwork.org";
 *       max-global-clones 3;
 *       password-flood 5:30;
 *   };
 * ======================================================================== */

static int udb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	ConfigEntry *cep;

	/* We only handle CONFIG_MAIN blocks named "udb" */
	if (type != CONFIG_MAIN)
		return 0;
	if (!ce || !ce->name || strcmp(ce->name, "udb"))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!strcmp(cep->name, "database-directory"))
		{
			if (!cep->value || !*cep->value)
			{
				config_error("%s:%i: udb::database-directory requires a value",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else if (!strcmp(cep->name, "propagator"))
		{
			if (!cep->value || !*cep->value)
			{
				config_error("%s:%i: udb::propagator requires a server name",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else if (!strcmp(cep->name, "max-global-clones"))
		{
			if (!cep->value || atoi(cep->value) < 0)
			{
				config_error("%s:%i: udb::max-global-clones requires a non-negative integer",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else if (!strcmp(cep->name, "password-flood"))
		{
			if (!cep->value || !strchr(cep->value, ':'))
			{
				config_error("%s:%i: udb::password-flood requires format attempts:seconds (e.g. 5:30)",
				             cep->file->filename, cep->line_number);
				errors++;
			}
		} else
		{
			config_error("%s:%i: unknown directive udb::%s",
			             cep->file->filename, cep->line_number, cep->name);
			errors++;
		}
	}

	*errs = errors;
	return errors ? -1 : 1;
}

static int udb_config_posttest(int *errs)
{
	/* Could validate that propagator is a known link, etc. */
	return 0;
}

static int udb_config_run(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if (type != CONFIG_MAIN)
		return 0;
	if (!ce || !ce->name || strcmp(ce->name, "udb"))
		return 0;

	/* Allocate config if needed */
	if (!udb_cfg)
		udb_cfg = safe_alloc(sizeof(UdbConfig));

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!strcmp(cep->name, "database-directory"))
		{
			safe_strdup(udb_cfg->db_directory, cep->value);
		} else if (!strcmp(cep->name, "propagator"))
		{
			safe_strdup(udb_cfg->propagator, cep->value);
		} else if (!strcmp(cep->name, "max-global-clones"))
		{
			udb_cfg->max_global_clones = atoi(cep->value);
		} else if (!strcmp(cep->name, "password-flood"))
		{
			const char *colon = strchr(cep->value, ':');
			if (colon)
			{
				udb_cfg->flood_attempts = atoi(cep->value);
				udb_cfg->flood_period = atoi(colon + 1);
			}
		}
	}

	/* Set defaults if not configured */
	if (!udb_cfg->db_directory)
		safe_strdup(udb_cfg->db_directory, UDB_DB_SUBDIR);
	if (udb_cfg->flood_attempts == 0)
		udb_cfg->flood_attempts = 5;
	if (udb_cfg->flood_period == 0)
		udb_cfg->flood_period = 60;
	udb_cfg->config_flood_attempts = udb_cfg->flood_attempts;
	udb_cfg->config_flood_period = udb_cfg->flood_period;

	return 1;
}
