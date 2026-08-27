---
name: udb-security
description: Applies UDB-specific C, authentication, S2S protocol, persistence, privilege, parser, bounds, and lifetime safety checks.
---

# UDB Security

Treat network input, persisted database content, configuration, credentials, masks, and synchronization payloads as untrusted until validated.

## C safety

Check modified code for:
- NULL handling;
- allocation ownership;
- failure-path cleanup;
- module unload/reload cleanup;
- callback/event lifetime;
- stale `Client *`;
- overflow/underflow;
- signed/unsigned conversion;
- buffer-size calculations;
- NUL termination;
- silent truncation;
- use-after-free/double-free;
- partial initialization.

## Protocol safety

Require:
- valid state before transition;
- authorized source at action time;
- strict bounds before copy/decode;
- rejection of malformed, duplicate, unsolicited, replayed, or overflowing frames when applicable;
- no partial commit of rejected staged data.

## Authentication and privilege

Check:
- accepted credential algorithms;
- CIDR/access enforcement where applicable;
- flood/rate-limit behavior;
- unique ULine/service resolution;
- no accidental rank escalation;
- removal/reconciliation of stale privileges;
- distinction between founder privileges and password-derived privileges.

## Persistence

Check:
- snapshot file permissions;
- symlink protection where supported;
- write/fsync/close/rename failure behavior;
- directory fsync semantics;
- preservation of active database on pre-rename failure;
- truthful handling of post-rename durability failure.

Do not weaken validation for malformed data merely to make a test pass.
