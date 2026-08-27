---
name: udb-sync-protocol
description: Guides UDB S2S protocol and distributed-state changes involving HEL 4, staged transactions, readiness, propagators, bootstrap, reconciliation, checksums, failover, and six-block convergence.
---

# UDB Synchronization Protocol

## Six-block database

Treat `N`, `C`, `I`, `S`, `L`, and `K` as one distributed database for readiness and convergence decisions.

A node must not become healthy merely because one divergent block completed.

## Required invariants

1. A peer must have confirmed UDB `HEL 4` capability before it is used for synchronization.
2. Sync sources must satisfy the current direct-peer and authority/propagator policy.
3. Bootstrap without an explicit propagator policy must not mix initial snapshots from multiple peers.
4. Staged data stays separate from active state until validation and commit succeed.
5. Malformed, oversized, partial, mismatched, or overflowing input fails closed.
6. Recovery to healthy state requires all required comparisons/completions plus no conflicting active staged state.
7. Failover/failback must be deterministic among eligible directly connected peers.
8. Peer state must not retain stale/disconnected `Client *` references.
9. Duplicate, unsolicited, replayed, or out-of-state frames must not advance state incorrectly.
10. Network-visible mutations must converge deterministically.

## Change procedure

Before editing:
1. identify current state;
2. identify event/frame/callback;
3. define allowed next state;
4. define duplicate/out-of-order/unauthorized behavior;
5. determine disconnect/lifecycle invalidation;
6. determine affected six-block/reconciliation fields;
7. identify tests for positive and rejection paths.

## Review questions

- Can an unsolicited ACK/END advance state?
- Can multiple peers contribute to an exclusive bootstrap?
- Can the node report OK before full required convergence?
- Can a disconnected `Client *` remain referenced?
- Can rejected staged data partially alter active state?
- Can numeric/checksum input wrap or truncate?
- Is authorization checked at the point mutation occurs?
- Is failover deterministic?
