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

/* Core database engine: records, checksums, sync staging, and file I/O */
#include "udb_core.c.inc"

/* Runtime effects: special-record dispatch only */
#include "udb_effects.c.inc"

/* Staged synchronization sessions: HEL capability and transfer state */
#include "udb_sync.c.inc"

/* S2S protocol handler: DB command parsing, routing, and server sync */
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

/* Engine, block, configuration, and module lifecycle coordination */
#include "udb_lifecycle.c.inc"

/* ========================================================================
 * Configuration Test (MOD_TEST)
 *
 * Validates the udb { } configuration block at config load time.
 * ======================================================================== */

MOD_TEST()
{
	return udb_module_test(modinfo);
}

/* ========================================================================
 * Module Initialization (MOD_INIT)
 *
 * Registers all commands, hooks, ModData, and initializes the DB engine.
 * ======================================================================== */

MOD_INIT()
{
	return udb_module_init(modinfo);
}

/* ========================================================================
 * Module Load (MOD_LOAD)
 *
 * Called after all modules are initialized. Load database files.
 * ======================================================================== */

MOD_LOAD()
{
	return udb_module_load(modinfo);
}

/* ========================================================================
 * Module Unload (MOD_UNLOAD)
 *
 * Save all data and free resources.
 * ======================================================================== */

MOD_UNLOAD()
{
	return udb_module_unload();
}
