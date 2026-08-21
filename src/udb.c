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

/* Record store: tree, hash, path, and file persistence primitives */
#include "udb_store.c.inc"

/* Configuration: daemon block parsing and UDB settings state */
#include "udb_config.c.inc"

/* Core database engine: runtime effects, sync staging, and lifecycle */
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
