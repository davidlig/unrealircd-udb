#!/usr/bin/env python3
"""Smoke test for UDB nick and channel runtime reconciliation.

Run against an isolated UnrealIRCd instance preloaded with Argon2id or bcrypt
hashes for the passwords below, for example:
N::alice::pass argon2id:$argon2id$...
N::alice::access 127.0.0.0/8
N::alice::vhost alice.test
C::#vault::founder alice
C::#vault::pass bcrypt:$2y$...

The tested plaintexts remain ``secret`` and ``chansecret``. Verify separately
that plaintext, ``md5:``, ``sha256:``, and ``crypt:`` records are rejected; a
flooded profile/IP pair must remain rejected until the configured flood window
expires, and a valid password from outside ``N::alice::access`` must not permit
NICK or GHOST.
"""

import os
import socket
import sys
import time


HOST = os.environ.get("UDB_TEST_HOST", "127.0.0.1")
PORT = int(os.environ.get("UDB_TEST_PORT", "16667"))


class IrcClient:
    def __init__(self, nick):
        self.nick = nick
        self.sock = socket.create_connection((HOST, PORT), timeout=3)
        self.sock.settimeout(0.2)
        self.lines = []
        self.send(f"NICK {nick}")
        self.send(f"USER {nick} 0 * :{nick}")
        self.wait_for(" 001 ")

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode())

    def drain(self, duration=0.5):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            for line in data.decode(errors="replace").split("\r\n"):
                if not line:
                    continue
                if line.startswith("PING "):
                    self.send("PONG " + line.split(" ", 1)[1])
                else:
                    self.lines.append(line)

    def wait_for(self, needle, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.drain(0.1)
            if any(needle in line for line in self.lines):
                return
        raise AssertionError(f"{self.nick}: no se recibió {needle!r}; líneas={self.lines!r}")

    def command(self, command, duration=0.6):
        start = len(self.lines)
        self.send(command)
        self.drain(duration)
        return self.lines[start:]

    def close(self):
        try:
            self.send("QUIT :test complete")
        finally:
            self.sock.close()


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    clients = []
    try:
        alice = IrcClient("alice:secret")
        clients.append(alice)
        require(any(" MODE alice :+r" in line or " MODE alice +r" in line for line in alice.lines),
                f"alice no recibió +r: {alice.lines!r}")

        whois = alice.command("WHOIS alice")
        require(any("alice.test" in line for line in whois),
                f"WHOIS de alice no contiene vhost UDB: {whois!r}")

        alice.command("CAP REQ :multi-prefix")
        alice.command("JOIN #vault")
        names = alice.command("NAMES #vault")
        require(any("~alice" in line for line in names),
                f"el fundador no recibió +q: {names!r}")
        require(not any("@alice" in line for line in names),
                f"el fundador recibió +o además de +q: {names!r}")

        bob = IrcClient("bob")
        clients.append(bob)
        rejected = bob.command("JOIN #vault wrong")
        require(any(" 475 " in line for line in rejected),
                f"contraseña inválida no fue rechazada: {rejected!r}")

        bob.command("JOIN #vault chansecret")
        names = bob.command("NAMES #vault")
        require(any("&bob" in line for line in names),
                f"autenticación de canal no concedió +a: {names!r}")

        bob.command("PART #vault")
        alice.command("INVITE bob #vault chansecret")
        bob.command("JOIN #vault")
        names = bob.command("NAMES #vault")
        require(not any("&bob" in line for line in names),
                f"INVITE con contraseña concedió +a: {names!r}")
        bob.command("PART #vault")
        rejected = bob.command("JOIN #vault")
        require(any(" 475 " in line for line in rejected),
                f"permiso INVITE no fue de un solo uso: {rejected!r}")

        alice.command("NICK alice2")
        mode_reply = alice.command("MODE alice2")
        user_modes = [line.rsplit(" ", 1)[-1] for line in mode_reply if " 221 " in line]
        require(user_modes and all("r" not in modes for modes in user_modes),
                f"+r persistió tras salir del nick: {mode_reply!r}")
        whois = alice.command("WHOIS alice2")
        require(not any("alice.test" in line for line in whois),
                f"el vhost UDB persistió tras salir del nick: {whois!r}")

        print("PASS: +r/vhost, fundador solo +q, password JOIN +a, INVITE de un uso y limpieza de nick")
    finally:
        for client in clients:
            client.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
