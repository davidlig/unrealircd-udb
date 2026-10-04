"""Current runtime application and expiry for Block K server bans."""

from base64 import b64encode
import time

import pytest

pytestmark = pytest.mark.integration


@pytest.mark.parametrize(
    ("line_type", "pattern", "stats_command"),
    [
        ("G", "blockedg@*", "G"),
        ("Z", "0.0.0.0/0", "G"),
        ("S", "blockedshun@*", "s"),
        ("F", f"b64%3A{b64encode(b'runtime-marker').decode('ascii')}", "f"),
    ],
)
def test_expiring_server_ban_materializes_and_expires_from_tkl_state(node_factory, line_type, pattern, stats_command):
    expires = int(time.time()) + 8

    fields = [("expires", f"*{expires}")]
    if line_type == "F":
        fields.extend(
            [
                ("match-type", "simple"),
                ("targets", "p"),
                ("action", "gline"),
                ("reason", "current F-line test"),
            ]
        )
    else:
        fields.append(("reason", f"current {line_type}-line test"))

    def seed_k_snapshot(data_dir):
        block = data_dir / "udb_K.db"
        entries = "".join(f"{line_type}::{pattern}::{key} {value}\n" for key, value in fields)
        block.write_text(
            "; UDB Block K - Version 1\n"
            "; Generation: 1\n"
            + entries,
            encoding="ascii",
        )

    node = node_factory(ready=True, prepare=seed_k_snapshot, tkl_monitor_exception=line_type == "Z")
    block = node.data_dir / "udb_K.db"
    prefix = f"{line_type}::{pattern}::"
    assert prefix.encode("ascii") in block.read_bytes()
    assert int(time.time()) < expires, "the isolated daemon took too long to start the expiry fixture"

    oper = node.client("lineoper")
    oper.send("OPER testoper operpass")
    oper.wait(lambda line: " 381 lineoper " in line, "operator access to live TKL state")
    stats = oper.request(f"STATS {stats_command}")
    if line_type == "F":
        found_active = any(
            " 229 " in line and " F simple p gline " in line and "UDB:managed" in line and ":runtime-marker" in line
            for line in stats
        )
        expected_row = "spamfilter F simple p gline ... runtime-marker"
    else:
        tkl_type = line_type.lower() if line_type == "S" else line_type
        tkl_mask = pattern if line_type in ("G", "S") else f"*@{pattern}"
        expected_row = f" {tkl_type} {tkl_mask} "
        found_active = any(" 223 " in line and expected_row in line for line in stats)
    assert found_active, (
        f"persisted {line_type}-line did not materialize in current TKL state:\n" + "\n".join(stats)
    )

    deadline = time.monotonic() + 18
    while time.monotonic() < deadline:
        if prefix.encode("ascii") not in block.read_bytes():
            break
        if node.process.process.poll() is not None:
            pytest.fail(f"isolated daemon exited before {line_type}-line expiry:\n{node.log_text()}")
        time.sleep(0.1)
    else:
        pytest.fail(f"authoritative expiry did not delete {line_type}-line:\n{block.read_text(encoding='ascii')}")

    stats_after_expiry = oper.request(f"STATS {stats_command}")
    if line_type == "F":
        remains_active = any(" 229 " in line and " F simple p gline " in line and "UDB:managed" in line and ":runtime-marker" in line for line in stats_after_expiry)
    else:
        remains_active = any(" 223 " in line and expected_row in line for line in stats_after_expiry)
    assert not remains_active, (
        f"expired {line_type}-line remains in current TKL state:\n" + "\n".join(stats_after_expiry)
    )
