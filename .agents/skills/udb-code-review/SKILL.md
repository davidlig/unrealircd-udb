---
name: udb-code-review
description: Reviews UDB commits and diffs for concrete correctness, security, protocol, convergence, memory-lifetime, persistence, and regression issues.
---

# UDB Code Review

## Procedure

1. Inspect the diff or patch supplied in the conversation first.
2. Enumerate changed functions and affected invariants.
3. Read only nearby code needed to understand the behavior.
4. Inspect directly relevant tests.
5. Check generated bundle/documentation consistency.
6. Specify targeted validation for a shell-capable role; do not run commands from the read-only reviewer.
7. Report findings ordered by severity.

## Priority

Highest priority:
- memory corruption/use-after-free;
- authentication/authorization bypass;
- remote crash or unsafe parser behavior;
- cross-node database corruption/desynchronization;
- partial active-state mutation after failed validation;
- privilege escalation.

Then:
- state-machine errors;
- readiness/convergence mistakes;
- persistence/lifecycle bugs;
- realistic malformed-input rejection gaps.

Style-only observations come last.

## Finding quality

Each finding must state:
- exact location;
- trigger;
- incorrect behavior;
- why current checks/tests do not prevent it;
- concise remediation direction.

Do not invent findings. If no material issue is found, say so and state residual untested risk.
