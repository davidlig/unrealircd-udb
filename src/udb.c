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

/* Exclusive native binary persistence and bounded immutable queries. */
#include "udb_storage_cache.c.inc"
#include "udb_storage_catalog.c.inc"
#include "udb_storage_codec.c.inc"
#include "udb_storage_engine.c.inc"
#include "udb_storage_fragments.c.inc"
#include "udb_storage_hash.c.inc"
#include "udb_storage_io.c.inc"
#include "udb_storage_jobs.c.inc"
#include "udb_storage_journal.c.inc"
#include "udb_storage_nodes.c.inc"
#include "udb_storage_orphans.c.inc"
#include "udb_storage_point.c.inc"
#include "udb_storage_prefix.c.inc"
#include "udb_storage_queries.c.inc"
#include "udb_storage_raw.c.inc"
#include "udb_storage_read.c.inc"
#include "udb_storage_rebuild.c.inc"
#include "udb_storage_reclaim.c.inc"
#include "udb_storage_runs.c.inc"
#include "udb_storage_sort.c.inc"
#include "udb_storage_stage_jobs.c.inc"
#include "udb_storage_stages.c.inc"
#include "udb_storage_state.c.inc"
#include "udb_storage_validate.c.inc"

/* Canonical wire path encoding and secret tuple classification */
#include "udb_store.c.inc"

/* Configuration: daemon block parsing and UDB settings state */
#include "udb_config.c.inc"

/* Pure business schema, bounded parsing, and foreground session lifetime */
#include "udb_core.c.inc"
#include "udb_native_read.c.inc"
#include "udb_native_schema.c.inc"

/* Dynamic connected service clients and service-originated notices */
#include "udb_services.c.inc"

/* Staged synchronization sessions: HEL capability and transfer state */
#include "udb_sync.c.inc"

/* Pinned native snapshot senders and typed wire values. */
#include "udb_native_effects.c.inc"
#include "udb_native_receive.c.inc"
#include "udb_native_sync.c.inc"
#include "udb_native_wire.c.inc"

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
