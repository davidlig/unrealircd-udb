---
name: udb-security
description: UDB-specific C, parser, authorization, protocol, persistence, privilege, bounds, and lifetime checks. Use for security-sensitive or memory-safety changes.
---

# UDB Security

Treat network input, persisted data, configuration, credentials, masks, and sync payloads as untrusted.

Check only areas touched by the change, with emphasis on:
- NULL handling, allocation ownership, failure cleanup, unload/reload and callback/event lifetime;
- stale `Client *`, use-after-free, double-free, partial initialization;
- overflow/underflow, signedness, size calculations, bounds, NUL termination, silent truncation;
- valid protocol state and authorization at mutation time;
- malformed/duplicate/replayed/unsolicited/out-of-state input rejection;
- no partial commit after rejected staged data;
- credential/CIDR/access/rate-limit/privilege behavior where affected;
- persistence write/fsync/close/rename failures, symlink protection where supported, and preservation of valid active data.

Do not add defensive complexity unrelated to the changed path. Do not weaken validation to satisfy a test.
