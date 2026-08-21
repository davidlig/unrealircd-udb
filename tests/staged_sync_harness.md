# Staged S2S Sync Harness

Run these against two linked UDB V4 servers with different `N` blocks.

1. Trigger `RES N` and confirm `BEGIN`, zero or more `PUT`, `END`, and `ACK` in that order.
2. During `PUT`, issue a propagator `INS N::queued::pass value`; expect `UDB_ERR_SYNC_ACTIVE` and confirm the active `N` file is unchanged.
3. Disconnect the sender before `END`; confirm the receiver retains its prior active tree and file.
4. Send `END` with a mismatched digest; confirm no active-tree or file change.
5. Pause more than 60 seconds between `BEGIN` and `PUT`; confirm the next `PUT` receives `UDB_ERR_NO_SYNC`.
6. Save identical records in different sibling orders and with different comment timestamps; confirm their advertised `INF` digests match.
7. From a HEL-confirmed direct peer that is not the selected propagator, send `BEGIN`, `PUT`, `END`, and `RES`; confirm each returns `UDB_ERR_FORBIDDEN`, no staged session or export is created, and the active block file is unchanged.
