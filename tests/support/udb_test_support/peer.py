"""Controlled loopback server peer; reference checksums never call production code."""

import hashlib
import socket
import struct
import time

from .runtime import Wire, tree_digest

EMPTY_DIGEST = hashlib.sha256(b"").hexdigest()


def inventory_digest(entries):
    payload = bytearray(b"UDB-OCL-INVENTORY-v1\0")
    payload.extend(struct.pack(">I", len(entries)))
    for name, digest in entries:
        encoded = name.encode("ascii")
        payload.extend(struct.pack(">I", len(encoded)))
        payload.extend(encoded)
        payload.extend(digest.encode("ascii") + b"\0")
    return hashlib.sha256(payload).hexdigest()


class Peer:
    def __init__(self, node, *, name="peer.test", sid="0P1", epoch="1111111111111111", negotiate=True, oclg=False):
        self.node, self.name, self.sid, self.epoch = node, name, sid, epoch
        self.wire = Wire(socket.create_connection(("127.0.0.1", node.server_port), timeout=5))
        self.round = 0
        self.serial = 0
        try:
            self.wire.send("PASS :testlinkpassword")
            self.wire.send(f"PROTOCTL EAUTH={name}")
            self.wire.send(f"PROTOCTL NOQUIT NICKv2 SJOIN SJOIN2 UMODE2 SJ3 BIGLINES SID={sid}")
            self.wire.send(f"SERVER {name} 1 :controlled test peer")
            self.wait(lambda line: "NETINFO" in line or " EOS" in line, "server handshake")
            self.send("EOS")
            if negotiate:
                self.negotiate(oclg=oclg)
        except BaseException:
            self.close()
            raise

    @property
    def lines(self):
        return self.wire.lines

    def send(self, command):
        self.wire.send(command if command.startswith(":") else f":{self.sid} {command}")

    def wait(self, predicate, description, *, start=0, timeout=5):
        return self.wire.wait(predicate, description, start=start, timeout=timeout)

    def barrier(self):
        start = len(self.lines)
        self.send(f"PING {self.name} :{self.node.sid}")
        self.wait(lambda line: line.endswith(f" PONG {self.node.sid} :{self.name}"),
                  "server command barrier", start=start)

    def negotiate(self, *, oclg=False, policy="?"):
        capabilities = "OCL OCLG" if oclg else "OCL"
        self.send(f"DB {self.node.sid} HEL 4 {policy} {self.epoch} {capabilities}")
        self.wait(lambda line: " DB " in line and " HEL 4 " in line, "HEL 4")
        self.send(f"DB {self.node.sid} HEL 4 ACK {policy} {self.epoch} {capabilities}")
        self.barrier()

    def inventory(self, blocks=None, *, watermark=0):
        """One complete six-block inventory, including the mutation watermark."""
        blocks = {} if blocks is None else blocks
        self.round += 1
        for letter in "NCISLK":
            records = blocks.get(letter, {})
            self.send(f"DB {self.node.sid} INF {self.round} {letter} {tree_digest(records)} {len(records)} {int(time.time()) + 1000 if records else 0} {watermark}")
        self.barrier()
        return self.round

    def mutation(self, sequence, path, value=None, *, epoch=None):
        command = "DEL" if value is None else "INS"
        suffix = "" if value is None else f" :{value}"
        self.send(f"DB * {command} {self.epoch if epoch is None else epoch} {sequence} {path}{suffix}")
        self.barrier()

    def offer(self, records, *, letter="N", digest=None, count=None, watermark=None):
        self.round += 1
        checksum = tree_digest(records) if digest is None else digest
        start = len(self.lines)
        suffix = "" if watermark is None else f" {watermark}"
        self.send(f"DB {self.node.sid} INF {self.round} {letter} {checksum} {len(records) if count is None else count} {int(time.time()) + 1000}{suffix}")
        self.wait(lambda line: f" RES {self.round} {letter}" in line, "requested snapshot", start=start)
        return self.round, checksum

    def transfer(self, records, *, letter="N", txid="snapshot", digest=None, end_digest=None, watermark=None):
        round_id, checksum = self.offer(records, letter=letter, digest=digest, watermark=watermark)
        suffix = "" if watermark is None else f" {watermark}"
        self.send(f"DB {self.node.sid} BEGIN {round_id} {letter} {txid} {checksum}{suffix}")
        for path, value in records.items():
            self.send(f"DB {self.node.sid} PUT {round_id} {letter} {txid} {path} :{value}")
        start = len(self.lines)
        self.send(f"DB {self.node.sid} END {round_id} {letter} {txid} {checksum if end_digest is None else end_digest}{suffix}")
        reply = self.wait(lambda line: f" {round_id} {letter} {txid} " in line and (" ACK " in line or " ABORT " in line), "snapshot result", start=start)
        self.barrier()
        return reply

    def ocl(self, entries, *, generation=1, epoch=None, digest=None, count=None):
        epoch = self.epoch if epoch is None else epoch
        digest = inventory_digest(entries) if digest is None else digest
        self.send(f"DB * OCL BEGIN {self.sid} {epoch} {generation} {len(entries) if count is None else count} {digest}")
        for name, fingerprint in entries:
            self.send(f"DB * OCL ITEM {self.sid} {epoch} {generation} {name} {fingerprint}")
        self.send(f"DB * OCL END {self.sid} {epoch} {generation}")
        self.barrier()

    def close(self):
        self.wire.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
