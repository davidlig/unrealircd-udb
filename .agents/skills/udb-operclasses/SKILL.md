---
name: udb-operclasses
description: UDB runtime OCL/OCLG invariants for operclass fingerprints, epochs, participant membership, staging, replay, completeness, and global view.
---

# UDB Operclass Registry (OCL/OCLG)

Use for changes touching `udb_operclasses.c.inc`, HEL OCL/OCLG capabilities, `/UDB OPERCLASS*`, operclass rehash, membership or replay.

## Boundary

OCL/OCLG is runtime-only. Never persist it in `udb_[NCISLK].db` or include it in six-block READY generations.

## Inventory invariants

- Participants are visible IRC servers excluding ULined Services; local inventory comes from loaded `conf_operclass` structures.
- Fingerprints are based on canonical runtime structure, not configuration text. Effective digests recursively include the parent.
- Missing parents, cycles, excessive depth or unserializable structures fail closed; invalid classes and affected descendants must not appear as valid global matches.
- Local inventory ordering/digesting must remain deterministic and avoid generation churn when the resulting inventory is unchanged.

## Wire and source authorization

OCL uses broadcast-target frames `BEGIN`, `ITEM`, `END` carrying `originSID`, instance epoch and generation.

- Accept an origin only if it is a current visible participant reached through the direct peer that delivered the frame (`origin->direction`).
- For the direct peer's own origin SID, the OCL epoch must agree with the epoch negotiated in confirmed HEL 4.
- Reject malformed, duplicate, stale-generation, conflicting-descriptor, overflow/count and digest violations without publishing partial state.
- Accepting a newer `BEGIN` withdraws the previous current inventory immediately; a failed/expired replacement leaves that origin missing until a valid snapshot commits.
- Stage commit occurs only after `END` validates item count and aggregate digest. Stage timeout remains bounded.
- Relay/replay only committed inventories to appropriate HEL-confirmed peers; do not relay uncommitted stages.

## OCLG

- Registry completeness requires a current inventory for local server and every current participant.
- OCLG is READY only when the registry is complete; otherwise expose INCOMPLETE and fail closed.
- The global view is the intersection of operclasses whose name and effective digest match on every participant.
- OCLG is sent only to explicit HEL `OCLG` subscribers. UnrealIRCd peers do not consume incoming OCLG as authoritative state.

For tests, prefer `test_ocl_registry.py`, `test_ocl_rehash_replay.py`, and `test_ocl_membership_multihop.py` before broader suites.
