---
name: udb-sync-protocol
description: UDB DB state-machine invariants for HEL 4, staged sync, bootstrap, propagators, reconciliation, readiness, failover, and mutations.
---

# UDB Synchronization Protocol

Treat `N`, `C`, `I`, `S`, `L`, `K` as one persistent distributed database for readiness/convergence. OCL/OCLG is runtime-only and belongs to `udb-operclasses`.

Before a non-trivial DB protocol change, write down only the affected transition:
1. current state and trigger/frame;
2. authorized source and prerequisites (`HEL 4` confirmed with mandatory `OCL`, direct-peer/authority policy);
3. allowed next state and active-vs-staged mutation boundary;
4. duplicate, replay, unsolicited, malformed, oversized and out-of-order behavior;
5. disconnect/module-lifecycle invalidation;
6. affected readiness/reconciliation fields and deadlines;
7. one success test and relevant rejection tests.

Required properties:
- staged snapshot data cannot alter active state before full validation and persistence commit;
- `INF/RES/BEGIN/PUT/END/ACK` reconciliation is tied to the selected authority, round, block and txid;
- bootstrap without explicit propagator policy does not mix initial snapshots from multiple peers;
- failover/failback and authority selection are deterministic and select only eligible direct links;
- snapshot frames remain hop-by-hop and are never broadcast/forwarded;
- authorized live `INS`, `DEL`, `DRP`, `OPT` may relay multihop, but every receiving node still enforces its own selected upstream authority;
- no stale disconnected `Client *` remains referenced;
- no frame advances state unless expected and authorized at action time;
- recovery to healthy requires complete six-block convergence, not one completed block;
- inactivity and absolute deadlines stay independent so activity cannot keep a broken round alive forever.

If the change cannot be expressed as a small set of explicit transitions, inspect more state before editing rather than guessing.
