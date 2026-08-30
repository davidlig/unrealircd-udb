---
name: udb-sync-protocol
description: UDB S2S state-machine invariants for HEL 4, staged sync, bootstrap, propagators, reconciliation, readiness, failover, and six-block convergence.
---

# UDB Synchronization Protocol

Treat `N`, `C`, `I`, `S`, `L`, `K` as one distributed database for readiness/convergence.

Before a non-trivial change, write down only the affected transition:
1. current state and trigger/frame;
2. authorized source and prerequisites (`HEL 4`, direct-peer/authority policy);
3. allowed next state and active-vs-staged mutation boundary;
4. duplicate, replay, unsolicited, malformed, oversized, and out-of-order behavior;
5. disconnect/module-lifecycle invalidation;
6. affected readiness/reconciliation fields;
7. one success test and relevant rejection tests.

Required properties:
- staged data cannot alter active state before full validation/commit;
- bootstrap without explicit propagator policy does not mix initial snapshots from multiple peers;
- failover/failback and authority selection are deterministic;
- no stale disconnected `Client *` remains referenced;
- no frame advances state unless it is expected and authorized at action time;
- recovery to healthy requires complete required convergence, not one completed block;
- network-visible mutations converge deterministically.

If the change cannot be expressed as a small set of explicit transitions, inspect more state before editing rather than guessing.
