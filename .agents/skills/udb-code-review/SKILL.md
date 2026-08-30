---
name: udb-code-review
description: Reviews a UDB diff for concrete correctness, security, protocol, convergence, lifetime, persistence, and regression defects. Use for commit/PR review.
---

# UDB Code Review

1. Start from the supplied diff/patch.
2. Enumerate changed functions and affected invariants.
3. Read only nearby code needed to establish reachability/ownership/state.
4. Inspect directly relevant tests and generated/doc consistency.
5. Report findings by severity; style comes last.

A finding is valid only when it states:
- exact location;
- reachable trigger;
- incorrect behavior/impact;
- why existing checks/tests do not prevent it;
- concise remediation direction.

Prioritize memory corruption/UAF, auth bypass, remote crash/parser safety, cross-node corruption/desync, partial active-state mutation, privilege escalation, then state/readiness/persistence/lifecycle regressions.

If no material issue is proved, say so and state only concrete residual untested risk. Do not manufacture findings or broaden into unrelated audit work.
