"""Shared .udb_state seeding helpers for UDB runtime tests.

State files must use the full versioned format. Minimal (STATE=/LAST_SYNC=
only) files and ORIGIN=LEGACY files are rejected by the module.

A READY seeding must pair the state file with six block files whose
"; Generation: " headers match the seeded GENERATION.
"""

import time

UDB_STATE_FORMAT = 1
DEFAULT_SEED_GENERATION = 1


def wait_for_state(state_path, needle, timeout=10.0, poll=0.2):
    """Poll a .udb_state file until it contains needle (e.g. STATE=READY).

    Persisting READY involves six snapshot writes plus fsyncs, which can take
    longer than a fixed sleep on slow or sanitized (ASan) environments; tests
    must poll instead of racing a fixed delay.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if needle in state_path.read_text(encoding="ascii", errors="replace"):
                return True
        except OSError:
            pass
        time.sleep(poll)
    return False


def read_text_lenient(path):
    """Read a file that may transiently not exist during snapshot rotation.

    udb_blocks_save_all() renames each current snapshot to .udb_previous
    before installing the new set, so a concurrent reader can observe ENOENT
    for a block file mid-transition; polling callers must treat that as "not
    there yet" instead of failing.
    """
    try:
        return path.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def block_header(letter, generation=DEFAULT_SEED_GENERATION):
    """Leading comment lines required in a seeded block snapshot file."""
    return f"; UDB Block {letter} - Version 1\n; Generation: {generation}\n"


def seed_block(path, letter, body="", generation=DEFAULT_SEED_GENERATION):
    """Write a block snapshot file with the required generation header."""
    path.write_text(block_header(letter, generation) + body, encoding="ascii")


def seed_ready_state(data_dir, generation=DEFAULT_SEED_GENERATION, last_sync=1787720000):
    """Write a versioned READY .udb_state (block files must share generation)."""
    (data_dir / ".udb_state").write_text(
        f"FORMAT={UDB_STATE_FORMAT}\nSTATE=READY\nORIGIN=FRESH\nGENERATION={generation}\nLAST_SYNC={last_sync}\n",
        encoding="ascii",
    )


def seed_bootstrapping_state(data_dir, generation=0):
    """Write a versioned BOOTSTRAPPING .udb_state."""
    (data_dir / ".udb_state").write_text(
        f"FORMAT={UDB_STATE_FORMAT}\nSTATE=BOOTSTRAPPING\nORIGIN=FRESH\nGENERATION={generation}\nLAST_SYNC=0\n",
        encoding="ascii",
    )
