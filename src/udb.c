/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Main Module Entry Point & Coordinator
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

#include "udb_internal.h"

ModuleHeader MOD_HEADER = {"third/udb", UDB_VERSION,
						   "UDB 4 - Unreal Database System (nick/channel/IP registration & sync)",
						   "David Abuín Fontán ('davidlig')", "unrealircd-6"};

/* Implementation units share one translation unit. Shared declarations in
 * udb_internal.h allow static helpers to cross subsystem boundaries. */

/* Record store: tree, hash, path, and file persistence primitives */
#include "udb_store.c.inc"

/* Configuration: daemon block parsing and UDB settings state */
#include "udb_config.c.inc"

/* Core database engine: records, checksums, sync staging, and file I/O */
#include "udb_core.c.inc"

/* Dynamic connected service clients and service-originated notices */
#include "udb_services.c.inc"

/* Runtime effects: special-record dispatch only */
#include "udb_effects.c.inc"

/* Staged synchronization sessions: HEL capability and transfer state */
#include "udb_sync.c.inc"

/* Operclass registry: local inventory, OCL propagation and OCLG view */
#include "udb_operclasses.c.inc"

/* Authorized real-time mutations: validation, effects, persistence, forwarding */
#include "udb_mutation.c.inc"

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

MOD_TEST()
{
	return udb_module_test(modinfo);
}

MOD_INIT()
{
	return udb_module_init(modinfo);
}

MOD_LOAD()
{
	return udb_module_load(modinfo);
}

MOD_UNLOAD()
{
	return udb_module_unload(modinfo);
}
