---
name: udb-security
description: UDB-specific C, parser, authorization, DB/OCL protocol, persistence, privilege, bounds, secret-redaction, and lifetime checks.
---

# UDB Security

Treat network input, persisted data, configuration, credentials, masks, DB sync payloads and OCL advertisements as untrusted.

Check only areas touched by the change, with emphasis on:
- NULL handling, allocation ownership, failure cleanup, unload/reload and callback/event lifetime;
- stale `Client *`, use-after-free, double-free and partial initialization;
- overflow/underflow, signedness, size calculations, bounds, NUL termination and silent truncation;
- valid DB protocol state and selected-authority authorization at mutation/commit time;
- OCL origin membership/direction, HEL epoch binding, generation/replay rules and atomic stage publication;
- malformed/duplicate/replayed/unsolicited/out-of-state input rejection;
- no partial active-state commit after rejected staged data;
- credential/CIDR/access/rate-limit/privilege behavior where affected;
- persistence write/fsync/close/rename failures, symlink protection where supported, and preservation of valid active data;
- diagnostics/logging redaction for `N::*::pass`, `N::*::challenge`, `C::*::pass`, `C::*::challenge`, and `S::encryption_key` when the touched path can expose values.

Do not assume one redaction helper protects every output path; verify the concrete logger/query path being changed. Do not add defensive complexity unrelated to the changed path, and never weaken validation to satisfy a test.
