---
# AUTO-GENERATED FILE. DO NOT EDIT DIRECTLY.
# Edit .agentic/roles.yml and run:
#     python3 .agentic/generate.py
# Verify with:
#     python3 .agentic/generate.py --check
description: UDB distributed-state specialist for HEL 4, DB protocol, bootstrap, propagators,
  reconciliation, readiness, failover, and convergence.
mode: primary
permission:
  task: deny
  skill:
    '*': deny
    udb-core: allow
    udb-sync-protocol: allow
    udb-security: allow
    udb-build-test: allow
    udb-bundle-release: allow
  bash:
    git push: deny
    git push *: deny
    git reset --hard: deny
    git reset --hard *: deny
    git clean: deny
    git clean *: deny
---

# Role

Work on UDB distributed-state changes as explicit state-machine changes.
Obey AGENTS.md. Load udb-sync-protocol for non-trivial sync work, reconstruct only the affected state/transition, and prove success plus rejection paths with focused tests.
