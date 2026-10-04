"""Identity and effect ownership observed over IRC, never through UDB internals."""

import hashlib
import time

import pytest

from udb_test_support.peer import Peer

pytestmark = pytest.mark.integration


def profile(password="secret", **effects):
    return {"alice::pass": "sha256:" + hashlib.sha256(password.encode()).hexdigest(),
            "alice::vhost": "alice.test", **{f"alice::{key}": value for key, value in effects.items()}}


def modes(client, nick):
    replies = client.request(f"MODE {nick}", terminator=lambda line: " 221 " in line)
    line = next(line for line in replies if " 221 " in line)
    return set(line.rsplit(" ", 1)[-1].lstrip(":+"))


def host(client, nick):
    replies = client.request(f"WHOIS {nick}", terminator=lambda line: " 318 " in line)
    line = next(line for line in replies if " 311 " in line)
    return line.split()[5]


def authenticate(client, *, credential="secret", nick="alice"):
    replies = client.request(f"NICK {nick}:{credential}")
    assert any(line.endswith(f" NICK :{nick}") for line in replies), replies
    assert any("You are now identified for nickname" in line for line in replies), replies
    assert "r" in modes(client, nick)
    assert host(client, nick) == "alice.test"


def setup_profile(node_factory, records, **node_options):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test", **node_options)
    peer = Peer(node)
    try:
        assert " ACK " in peer.transfer(records)
    except BaseException:
        peer.close()
        raise
    return node, peer


def setup_profile_with_runtime_state(node_factory, records):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        runtime_state_fixture=True)
    peer = Peer(node)
    try:
        assert " ACK " in peer.transfer(records)
    except BaseException:
        peer.close()
        raise
    return node, peer


def read_snomask(client, peer, nick):
    start = len(client.lines)
    peer.send(f"UDBTEST GETSNOMASK {nick}")
    line = client.wait(lambda value: "UDBTEST SNOMASK=" in value,
                       f"snomask state for {nick}", start=start)
    return line.split("UDBTEST SNOMASK=", 1)[1].strip()


def assert_snomask(client, peer, nick, expected):
    actual = read_snomask(client, peer, nick)
    normalize = lambda value: "".join(sorted(value.replace("+", "").replace("-", "")))
    assert normalize(actual) == normalize(expected), (actual, expected)


def read_oper_state(client, peer, nick):
    start = len(client.lines)
    peer.send(f"UDBTEST GETOPER {nick}")
    line = client.wait(lambda value: "UDBTEST OPER=" in value,
                       f"oper state for {nick}", start=start)
    fields = {}
    for item in line.split("UDBTEST ", 1)[1].split():
        if "=" in item:
            key, value = item.split("=", 1)
            fields[key] = value
    return {
        "oper": fields.get("OPER") == "1",
        "operclass": fields.get("OPERCLASS"),
        "opercount": int(fields.get("OPERCOUNT", "0")),
        "operlist": int(fields.get("OPERLIST", "0")),
    }


@pytest.mark.parametrize("password", ["wrong", "", "secret:extra"])
def test_bad_credential_cannot_adopt_protected_profile_or_apply_effects(node_factory, password):
    node, peer = setup_profile(node_factory, profile())
    with peer:
        client = node.client("visitor")
        before_host = host(client, "visitor")
        replies = client.request(f"NICK alice:{password}")
        assert any(any(f" {code} " in line for code in (432, 433, 437)) for line in replies), replies
        assert any("password" in line.lower() for line in replies), replies
        assert "r" not in modes(client, "visitor")
        assert host(client, "visitor") == before_host
        assert not any("You are now identified" in line for line in replies)


def test_authentication_case_change_and_leaving_profile_respect_effect_ownership(node_factory):
    node, peer = setup_profile(node_factory, profile())
    with peer:
        client = node.client("visitor")
        client.request("MODE visitor +i")
        before_host = host(client, "visitor")
        authenticate(client)
        replies = client.request("NICK ALICE")
        assert any(line.endswith(" NICK :ALICE") for line in replies)
        assert not any("You are now identified" in line for line in replies)
        assert "r" in modes(client, "ALICE")
        assert host(client, "ALICE") == "alice.test"
        client.request("NICK departed")
        assert "r" not in modes(client, "departed")
        assert "i" in modes(client, "departed")
        assert host(client, "departed") == before_host


def test_case_only_svsnick_preserves_identity_without_success_notice(node_factory):
    node, peer = setup_profile(node_factory, profile(), ulines=["peer.test"])
    with peer:
        client = node.client("visitor")
        authenticate(client)

        for previous, updated in (("alice", "ALICE"), ("ALICE", "alice")):
            start = len(client.lines)
            peer.send(f"SVSNICK {previous} {updated} {int(time.time())}")
            client.wait(lambda line: line.endswith(f" NICK :{updated}"),
                        f"case-only SVSNICK {previous} to {updated}", start=start)
            assert "r" in modes(client, updated)
            assert host(client, updated) == "alice.test"
            assert not any("You are now identified for nickname" in line
                           for line in client.lines[start:])


def test_profile_cannot_claim_a_mode_that_was_already_set_externally(node_factory):
    node, peer = setup_profile(node_factory, profile(modes="+i"))
    with peer:
        client = node.client("visitor")
        client.request("MODE visitor +i")
        before_host = host(client, "visitor")

        authenticate(client)
        assert "i" in modes(client, "alice")

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "ownership check")
        client.wait(lambda line: "This nickname is suspended. Reason: ownership check" in line,
                    "suspend notice", start=start)

        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert "i" in current_modes
        assert host(client, "alice") == before_host


def test_hot_mode_and_vhost_updates_are_applied_and_removed(node_factory):
    node, peer = setup_profile(node_factory, profile())
    with peer:
        client = node.client("visitor")
        observer = node.client("observer")
        assert any(" 381 " in line for line in observer.request("OPER testoper operpass"))
        before_host = host(client, "visitor")
        authenticate(client)

        start = len(client.lines)
        peer.mutation(1, "N::alice::vhost", "updated.test")
        assert host(client, "alice") == "updated.test"
        assert "r" in modes(client, "alice")
        assert not any("You are now identified for nickname" in line for line in client.lines[start:])
        client.request("MODE alice -i")
        assert "i" not in modes(client, "alice")
        start = len(client.lines)
        peer.mutation(2, "N::alice::modes", "+i")
        assert "+i" in "\n".join(observer.request("DBQ N::alice::modes"))
        assert "i" in modes(client, "alice")
        assert "r" in modes(client, "alice")
        assert not any("You are now identified for nickname" in line for line in client.lines[start:])

        peer.mutation(3, "N::alice::vhost")
        assert host(client, "alice") == before_host
        peer.mutation(4, "N::alice::modes")
        assert "Block not found" in "\n".join(observer.request("DBQ N::alice::modes"))
        assert "i" not in modes(client, "alice")
        assert "r" in modes(client, "alice")


def test_deleting_profile_revokes_identity_and_owned_effects(node_factory):
    node, peer = setup_profile(node_factory, profile(modes="+i"))
    with peer:
        client = node.client("visitor")
        client.request("MODE visitor -i")
        before_host = host(client, "visitor")
        authenticate(client)
        assert "i" in modes(client, "alice")

        peer.mutation(1, "N::alice")

        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert "i" not in current_modes
        assert host(client, "alice") == before_host


def test_snapshot_profile_removal_revokes_identity_without_renaming(node_factory):
    node, peer = setup_profile(node_factory, profile(modes="+i"))
    with peer:
        client = node.client("visitor")
        client.request("MODE visitor -i")
        before_host = host(client, "visitor")
        authenticate(client)
        assert "i" in modes(client, "alice")

        assert " ACK " in peer.transfer({}, txid="remove-nick-profile")

        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert "i" not in current_modes
        assert host(client, "alice") == before_host
        assert not any(" NICK :Guest" in line for line in client.lines)


def test_external_vhost_survives_suspension_when_profile_has_no_vhost(node_factory):
    records = {"alice::pass": profile()["alice::pass"], "alice::access": "127.0.0.0/8"}
    node, peer = setup_profile(node_factory, records, ulines=["peer.test"])
    with peer:
        client = node.client("visitor")
        peer.send("CHGHOST visitor external-no-profile-vhost.test")
        client.wait(lambda line: " 396 " in line and "external-no-profile-vhost.test" in line,
                    "external vhost before identity without profile vhost")
        replies = client.request("NICK alice:secret")
        assert any(line.endswith(" NICK :alice") for line in replies), replies
        assert "r" in modes(client, "alice")
        assert host(client, "alice") == "external-no-profile-vhost.test"

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "preserve vhost without profile effect")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve vhost without profile effect" in line,
                    "suspension without N::vhost", start=start)
        assert "r" not in modes(client, "alice")
        assert host(client, "alice") == "external-no-profile-vhost.test"
        assert not any(" NICK :Guest" in line for line in client.lines[start:])


def test_hot_suspend_revokes_identity_without_renaming_and_unsuspend_requires_reauth(node_factory):
    node, peer = setup_profile(node_factory, profile())
    with peer:
        client = node.client("visitor")
        before_host = host(client, "visitor")
        authenticate(client)
        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "manual review")
        client.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "hot suspension notice", start=start)
        assert "r" not in modes(client, "alice")
        assert host(client, "alice") == before_host
        assert not any(" NICK :" in line for line in client.lines[start:])
        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend")
        renamed = client.wait(lambda line: " NICK :Guest" in line,
                              "unsuspension requires reauthentication", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        transition = client.lines[start:]
        assert any(line.endswith(":The nickname alice is no longer suspended.")
                   for line in transition), transition
        assert any(line.endswith(":You have been renamed. If you are the owner, please identify: /NICK alice:Password")
                   for line in transition), transition
        assert not any("has been registered or synced in the UDB database" in line
                       for line in transition), transition
        assert "r" not in modes(client, guest)
        assert host(client, guest) == before_host
        authenticate(client)


def test_passless_profile_never_creates_authenticated_identity(node_factory):
    node, peer = setup_profile(node_factory, {"alice::vhost": "alice.test", "alice::modes": "+i"})
    with peer:
        client = node.client("visitor")
        client.request("MODE visitor -i")
        assert "i" not in modes(client, "visitor")
        before_host = host(client, "visitor")
        replies = client.request("NICK alice")
        assert any(line.endswith(" NICK :alice") for line in replies)
        assert "r" not in modes(client, "alice")
        assert "i" not in modes(client, "alice")
        assert host(client, "alice") == before_host
        assert not any("You are now identified" in line for line in replies)


def test_passless_to_protected_and_hot_credential_removal_require_authentication(node_factory):
    node, peer = setup_profile(node_factory, {
        "alice::access": "127.0.0.0/8",
        "alice::vhost": "alice.test",
    })
    with peer:
        client = node.client("alice")
        before_host = host(client, "alice")
        assert "r" not in modes(client, "alice")
        assert host(client, "alice") == before_host

        start = len(client.lines)
        peer.mutation(1, "N::alice::pass", profile()["alice::pass"])
        renamed = client.wait(lambda line: " NICK :Guest" in line,
                              "pass insertion renames unauthenticated holder", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        assert "r" not in modes(client, guest)
        assert host(client, guest) == before_host

        authenticate(client)
        start = len(client.lines)
        peer.mutation(2, "N::alice::pass")
        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert host(client, "alice") == before_host
        assert not any(" NICK :Guest" in line for line in client.lines[start:])


def test_server_forced_nick_change_cannot_authenticate_protected_or_suspended_profiles(node_factory):
    records = {
        "forced::pass": profile()["alice::pass"],
        "forced::access": "127.0.0.0/8",
        "forced::vhost": "forced.test",
        "held::pass": profile()["alice::pass"],
        "held::access": "127.0.0.0/8",
        "held::vhost": "held.test",
        "held::suspend": "manual review",
    }
    node, peer = setup_profile(node_factory, records, ulines=["peer.test"])
    with peer:
        for target, expected_vhost in (("forced", "forced.test"), ("held", "held.test")):
            client = node.client(f"{target}-holder")
            before_host = host(client, client.nickname)
            start = len(client.lines)
            peer.send(f"SVSNICK {client.nickname} {target} {int(time.time())}")
            renamed = client.wait(lambda line: " NICK :Guest" in line,
                                  f"unauthenticated SVSNICK away from {target}", start=start)
            guest = renamed.rsplit(" :", 1)[-1]
            assert guest != target
            assert "r" not in modes(client, guest)
            assert host(client, guest) == before_host
            assert host(client, guest) != expected_vhost

        start = len(client.lines)
        peer.mutation(1, "N::held::suspend")
        assert "r" not in modes(client, guest)
        assert not any(" MODE " in line and "+r" in line for line in client.lines[start:])


def test_nick_credentials_are_one_shot_and_do_not_follow_to_another_profile(node_factory):
    shared = "sha256:" + hashlib.sha256(b"shared-secret").hexdigest()
    records = {
        "source::pass": shared,
        "source::access": "127.0.0.0/8",
        "destination::pass": shared,
        "destination::access": "127.0.0.0/8",
    }
    node, peer = setup_profile(node_factory, records)
    with peer:
        client = node.client("one-shot-client")
        identified = client.request("NICK source:shared-secret")
        assert any(line.endswith(" NICK :source") for line in identified), identified
        assert "r" in modes(client, "source")

        client.request("NICK free-after-source")
        attempted = client.request("NICK destination")
        assert any("requires a password" in line.lower() for line in attempted), attempted
        assert not any("Invalid password for destination" in line for line in attempted), attempted
        assert "r" not in modes(client, "free-after-source")


def test_external_account_does_not_adopt_newly_protected_nick_without_auth(node_factory):
    records = {
        "extacct::access": "127.0.0.0/8",
        "extacct::vhost": "extacct.udb",
    }
    node, peer = setup_profile(node_factory, records, ulines=["peer.test"])
    with peer:
        client = node.client("extacct")
        peer.send("SVSLOGIN * extacct extacct")
        peer.send("SVS2MODE extacct +r")
        peer.barrier()
        assert "r" in modes(client, "extacct")
        external_login = client.request("WHOIS extacct", terminator=lambda line: " 318 " in line)
        assert any(" 330 " in line and "extacct" in line for line in external_login), external_login

        start = len(client.lines)
        peer.mutation(1, "N::extacct::pass",
                      "sha256:" + hashlib.sha256(b"extacctsecret").hexdigest())
        renamed = client.wait(lambda line: " NICK :Guest" in line,
                              "newly protected profile does not auto-authenticate external account", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        assert host(client, guest) != "extacct.udb"

        replies = client.request("NICK extacct:extacctsecret")
        assert any(line.endswith(" NICK :extacct") for line in replies), replies
        assert any("You are now identified for nickname extacct" in line for line in replies), replies
        assert "r" in modes(client, "extacct")
        assert host(client, "extacct") == "extacct.udb"


def test_external_account_cannot_authenticate_newly_protected_snapshot(node_factory):
    records = {
        "extacct::access": "127.0.0.0/8",
        "extacct::vhost": "extacct.udb",
    }
    node, peer = setup_profile(node_factory, records, ulines=["peer.test"])
    with peer:
        client = node.client("extacct")
        peer.send("SVSLOGIN * extacct extacct")
        peer.send("SVS2MODE extacct +r")
        peer.barrier()
        assert "r" in modes(client, "extacct")

        protected = {
            **records,
            "extacct::pass": "sha256:" + hashlib.sha256(b"extacctsecret").hexdigest(),
        }
        start = len(client.lines)
        assert " ACK " in peer.transfer(protected, txid="acct-pass-snap")
        renamed = client.wait(lambda line: " NICK :Guest" in line,
                              "newly protected snapshot does not authenticate external account", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        assert host(client, guest) != "extacct.udb"
        assert not any("You are now identified for nickname" in line for line in client.lines[start:])


def test_external_account_is_cleared_on_revoke_and_restored_only_after_reauth(node_factory):
    node, peer = setup_profile(node_factory, profile())
    with peer:
        client = node.client("visitor")
        authenticate(client)
        peer.send("SVSLOGIN * alice other")
        peer.send("SVS2MODE alice -r")
        peer.barrier()
        external = client.request("WHOIS alice", terminator=lambda line: " 318 " in line)
        assert any(" 330 " in line and "other" in line for line in external), external

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "revoke external account projection")
        client.wait(lambda line: "This nickname is suspended. Reason: revoke external account projection" in line,
                    "suspension revokes external account", start=start)
        assert "r" not in modes(client, "alice")
        suspended = client.request("WHOIS alice", terminator=lambda line: " 318 " in line)
        assert not any(" 330 " in line and "other" in line for line in suspended), suspended

        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend")
        renamed = client.wait(lambda line: " NICK :Guest" in line,
                              "unsuspension requires explicit authentication", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        assert "r" not in modes(client, guest)
        replies = client.request("NICK alice:secret")
        assert any(line.endswith(" NICK :alice") for line in replies), replies
        assert "r" in modes(client, "alice")
        restored = client.request("WHOIS alice", terminator=lambda line: " 318 " in line)
        assert any(" 330 " in line and "alice" in line for line in restored), restored


def test_failed_occupied_suspended_nick_attempt_cannot_authenticate_later_svsnick(node_factory):
    records = {}
    for nick in ("sneak", "victimproof"):
        records[f"{nick}::pass"] = "sha256:" + hashlib.sha256(f"{nick}-secret".encode()).hexdigest()
        records[f"{nick}::access"] = "127.0.0.0/8"
        records[f"{nick}::vhost"] = f"{nick}.test"
        records[f"{nick}::suspend"] = "manual review"
    node, peer = setup_profile(node_factory, records, ulines=["peer.test"])
    with peer:
        holder = node.client("sneak-holder")
        holder_replies = holder.request("NICK sneak:sneak-secret")
        assert any(line.endswith(" NICK :sneak") for line in holder_replies), holder_replies
        holder.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "occupied suspended nickname holder")

        victim = node.client("victim-holder")
        victim_replies = victim.request("NICK victimproof:victimproof-secret")
        assert any(line.endswith(" NICK :victimproof") for line in victim_replies), victim_replies
        victim.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "victim suspended profile holder")
        rejected = victim.request("NICK sneak:sneak-secret")
        assert any(" 433 " in line for line in rejected), rejected

        current = "victim-holder"
        for line in victim.lines:
            prefix = line.split(" ", 1)[0].lstrip(":").split("!", 1)[0]
            if prefix == current and " NICK :" in line:
                current = line.rsplit(" NICK :", 1)[1].strip()

        peer_start = len(peer.lines)
        holder.send("QUIT :release protected nickname for forced rename")
        peer.wait(lambda line: " QUIT " in line,
                  "released holder QUIT propagated to the server peer", start=peer_start)
        holder.close()
        start = len(victim.lines)
        peer.send(f"SVSNICK {current} sneak {int(time.time())}")
        try:
            renamed = victim.wait(lambda line: " NICK :Guest" in line,
                                  "failed-attempt target forced rename", start=start)
        except BaseException as error:
            raise AssertionError(f"{error}\n{node.log_text()}") from error
        guest = renamed.rsplit(" :", 1)[-1]
        assert "r" not in modes(victim, guest)
        assert host(victim, guest) != "sneak.test"

        start = len(victim.lines)
        peer.mutation(1, "N::sneak::suspend")
        assert "r" not in modes(victim, guest)
        assert not any(" MODE " in line and "+r" in line for line in victim.lines[start:])


def test_password_and_still_permissive_access_refresh_keep_active_identity(node_factory):
    node, peer = setup_profile(node_factory, profile(access="127.0.0.0/8"))
    with peer:
        client = node.client("visitor")
        authenticate(client)

        changes = [
            ("N::alice::pass", "sha256:" + hashlib.sha256(b"rotated").hexdigest()),
            ("N::alice::access", "127.0.0.0/9"),
            ("N::alice::access", None),
        ]
        for sequence, (path, value) in enumerate(changes, 1):
            start = len(client.lines)
            peer.mutation(sequence, path, value)
            assert "r" in modes(client, "alice")
            assert host(client, "alice") == "alice.test"
            assert not any(" NICK :Guest" in line for line in client.lines[start:])


def test_suspended_adoption_never_materializes_profile_and_passless_unsuspend_keeps_nick(node_factory):
    records = profile(modes="+i", access="127.0.0.0/8", suspend="manual review")
    records.update({"open::access": "127.0.0.0/8", "open::suspend": "manual review"})
    node, peer = setup_profile(node_factory, records)
    with peer:
        client = node.client("visitor")
        client.request("MODE visitor +i")
        before_host = host(client, "visitor")
        replies = client.request("NICK alice:secret")
        assert any(line.endswith(" NICK :alice") for line in replies), replies
        client.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "suspended profile notice")
        assert not any("You are now identified for nickname" in line for line in replies), replies
        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert "i" in current_modes
        assert host(client, "alice") == before_host

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend")
        renamed = client.wait(lambda line: " NICK :Guest" in line,
                              "protected unsuspend requires reauthentication", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        transition = client.lines[start:]
        assert any(line.endswith(":The nickname alice is no longer suspended.")
                   for line in transition), transition
        assert any(line.endswith(":You have been renamed. If you are the owner, please identify: /NICK alice:Password")
                   for line in transition), transition
        assert not any("has been registered or synced in the UDB database" in line
                       for line in transition), transition
        assert "r" not in modes(client, guest)
        assert "i" in modes(client, guest)
        assert host(client, guest) == before_host

        passless = node.client("open-client")
        passless.request("NICK open")
        passless.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                      "passless suspended profile notice")
        assert "r" not in modes(passless, "open")
        start = len(passless.lines)
        peer.mutation(2, "N::open::suspend")
        assert "r" not in modes(passless, "open")
        assert not any(" NICK :Guest" in line or "You have been renamed" in line
                       for line in passless.lines[start:])


def test_snapshot_refresh_preserves_identity_until_suspend_policy_changes(node_factory):
    initial = profile(access="127.0.0.0/8")
    node, peer = setup_profile(node_factory, initial, ulines=["peer.test"])
    with peer:
        client = node.client("visitor")
        before_host = host(client, "visitor")
        authenticate(client)

        refreshed = profile("rotated", access="127.0.0.0/8", vhost="updated.test")
        assert " ACK " in peer.transfer(refreshed, txid="policy-refresh")
        assert "r" in modes(client, "alice")
        assert host(client, "alice") == "updated.test"

        without_vhost = {
            "alice::pass": profile("rotated")["alice::pass"],
            "alice::access": "127.0.0.0/8",
        }
        assert " ACK " in peer.transfer(without_vhost, txid="policy-vhost-delete")
        assert "r" in modes(client, "alice")
        assert host(client, "alice") == before_host
        assert " ACK " in peer.transfer(refreshed, txid="policy-vhost-restore")
        assert "r" in modes(client, "alice")
        assert host(client, "alice") == "updated.test"

        start = len(client.lines)
        peer.send("CHGHOST alice external-snapshot.test")
        client.wait(lambda line: " 396 " in line and "external-snapshot.test" in line,
                    "external vhost override before snapshot suspend", start=start)
        peer.send("SVS2MODE alice +S")
        client.wait(lambda line: " MODE alice " in line and "+S" in line,
                    "external +S before snapshot suspend", start=start)

        suspended = {**refreshed, "alice::suspend": "manual review"}
        start = len(client.lines)
        assert " ACK " in peer.transfer(suspended, txid="policy-suspend")
        client.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "snapshot suspension notice", start=start)
        suspended_modes = modes(client, "alice")
        assert "r" not in suspended_modes
        assert "S" in suspended_modes
        assert host(client, "alice") == "external-snapshot.test"
        assert not any(" NICK :Guest" in line for line in client.lines[start:])

        suspended_again = {**suspended, "alice::suspend": "continued review"}
        start = len(client.lines)
        assert " ACK " in peer.transfer(suspended_again, txid="policy-suspend-again")
        suspended_modes = modes(client, "alice")
        assert "r" not in suspended_modes
        assert "S" in suspended_modes
        assert host(client, "alice") == "external-snapshot.test"
        assert not any(" NICK :Guest" in line for line in client.lines[start:])

        start = len(client.lines)
        assert " ACK " in peer.transfer(refreshed, txid="policy-unsuspend")
        renamed = client.wait(lambda line: " NICK :Guest" in line,
                              "snapshot unsuspend requires fresh authentication", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        assert "r" not in modes(client, guest)
        assert host(client, guest) == "external-snapshot.test"


def test_suspended_adoption_preserves_ulined_external_mode_and_vhost(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        ulines=["peer.test"])
    with Peer(node) as peer:
        records = profile(modes="+S", suspend="manual review")
        assert " ACK " in peer.transfer(records, txid="suspended-profile")
        client = node.client("visitor")
        start = len(client.lines)
        peer.send("SVS2MODE visitor +S")
        client.wait(lambda line: " MODE visitor " in line and "+S" in line,
                    "external +S assignment", start=start)
        peer.send("CHGHOST visitor external.test")
        client.wait(lambda line: " 396 " in line and "external.test" in line,
                    "external vhost assignment")
        before_host = host(client, "visitor")
        assert before_host == "external.test"

        replies = client.request("NICK alice:secret")
        assert any(line.endswith(" NICK :alice") for line in replies), replies
        client.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "suspended adoption notice")
        assert not any("You are now identified for nickname" in line for line in replies), replies
        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert "S" in current_modes
        assert host(client, "alice") == before_host

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend")
        renamed = client.wait(lambda line: " NICK :Guest" in line,
                              "unsuspend rename after dormant profile", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        transition = client.lines[start:]
        assert any(line.endswith(":The nickname alice is no longer suspended.")
                   for line in transition), transition
        assert any(line.endswith(":You have been renamed. If you are the owner, please identify: /NICK alice:Password")
                   for line in transition), transition
        assert not any("has been registered or synced in the UDB database" in line
                       for line in transition), transition
        current_modes = modes(client, guest)
        assert "r" not in current_modes
        assert "S" in current_modes
        assert host(client, guest) == before_host


def test_passless_profile_keeps_ulined_external_effects_and_applies_none(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        ulines=["peer.test"])
    with Peer(node) as peer:
        records = {
            "alice::access": "127.0.0.0/8",
            "alice::modes": "+S",
            "alice::vhost": "dormant.test",
            "alice::suspend": "manual review",
        }
        assert " ACK " in peer.transfer(records, txid="passless-profile")
        client = node.client("visitor")
        peer.send("SVS2MODE visitor +S")
        client.wait(lambda line: " MODE visitor " in line and "+S" in line,
                    "external +S assignment")
        peer.send("CHGHOST visitor external.test")
        client.wait(lambda line: " 396 " in line and "external.test" in line,
                    "external vhost assignment")

        replies = client.request("NICK alice")
        assert any(line.endswith(" NICK :alice") for line in replies), replies
        client.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "passless suspended-profile notice")
        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert "S" in current_modes
        assert host(client, "alice") == "external.test"
        assert not any("You are now identified" in line for line in replies)

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend")
        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert "S" in current_modes
        assert host(client, "alice") == "external.test"
        assert not any(" NICK :Guest" in line or "You have been renamed" in line
                       for line in client.lines[start:])


def test_passless_access_denial_renames_without_stripping_external_mode(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        ulines=["peer.test"])
    with Peer(node) as peer:
        records = {
            "denyinert::access": "127.0.0.0/8",
            "denyinert::modes": "+S",
        }
        assert " ACK " in peer.transfer(records, txid="passless-access-profile")
        client = node.client("denyinert-external")
        start = len(client.lines)
        peer.send("SVS2MODE denyinert-external +S")
        client.wait(lambda line: " MODE denyinert-external " in line and "+S" in line,
                    "external +S before passless adoption", start=start)
        replies = client.request("NICK denyinert")
        assert any(line.endswith(" NICK :denyinert") for line in replies), replies
        assert "r" not in modes(client, "denyinert")
        assert "S" in modes(client, "denyinert")

        start = len(client.lines)
        peer.mutation(1, "N::denyinert::access", "192.0.2.0/24")
        renamed = client.wait(lambda line: " NICK :Guest" in line,
                              "passless access denial rename", start=start)
        guest = renamed.rsplit(" :", 1)[-1]
        assert "r" not in modes(client, guest)
        assert "S" in modes(client, guest)


def test_passless_snapshots_preserve_external_mode_and_vhost_for_replaced_and_removed_profiles(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        ulines=["peer.test"])
    with Peer(node) as peer:
        initial = {
            "snapinert::access": "127.0.0.0/8",
            "snapinert::vhost": "snapinert.test",
            "snapgone::access": "127.0.0.0/8",
            "snapgone::vhost": "snapgone.test",
        }
        assert " ACK " in peer.transfer(initial, txid="passless-snapshot-initial")
        clients = {}
        for nick in ("snapinert", "snapgone"):
            client = node.client(f"{nick}-external")
            clients[nick] = client
            peer.send(f"SVS2MODE {nick}-external +S")
            client.wait(lambda line, nick=nick: f" MODE {nick}-external " in line and "+S" in line,
                        f"external +S for {nick}")
            peer.send(f"CHGHOST {nick}-external external-{nick}.test")
            client.wait(lambda line, nick=nick: " 396 " in line and f"external-{nick}.test" in line,
                        f"external vhost for {nick}")
            replies = client.request(f"NICK {nick}")
            assert any(line.endswith(f" NICK :{nick}") for line in replies), replies
            assert "r" not in modes(client, nick)

        replacement = {
            "snapinert::access": "127.0.0.0/8",
            "snapinert::vhost": "snapinert-replaced.test",
        }
        assert " ACK " in peer.transfer(replacement, txid="passless-snapshot-replace")
        for nick, client in clients.items():
            assert "S" in modes(client, nick)
            assert host(client, nick) == f"external-{nick}.test"


def test_external_oper_is_not_claimed_or_revoked_by_profile(node_factory):
    node, peer = setup_profile(node_factory, profile(oper="locop"))
    with peer:
        client = node.client("visitor")
        assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
        before = client.request("WHOIS visitor", terminator=lambda line: " 318 " in line)
        assert any(" 313 " in line for line in before), before

        authenticate(client)
        assert "o" in modes(client, "alice")
        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "external oper owner")
        client.wait(lambda line: "This nickname is suspended. Reason: external oper owner" in line,
                    "external oper suspension", start=start)

        assert "o" in modes(client, "alice")
        after = client.request("WHOIS alice", terminator=lambda line: " 318 " in line)
        assert any(" 313 " in line for line in after), after


def test_udb_granted_oper_is_revoked_on_suspension(node_factory):
    node, peer = setup_profile(node_factory, profile(oper="locop"))
    with peer:
        client = node.client("visitor")
        authenticate(client)
        assert "o" in modes(client, "alice")
        whois = client.request("WHOIS alice", terminator=lambda line: " 318 " in line)
        assert any(" 313 " in line for line in whois), whois

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "revoke oper")
        client.wait(lambda line: "This nickname is suspended. Reason: revoke oper" in line,
                    "UDB oper suspension", start=start)
        assert "o" not in modes(client, "alice")
        after = client.request("WHOIS alice", terminator=lambda line: " 318 " in line)
        assert not any(" 313 " in line for line in after), after


def test_relative_snomask_effect_restores_external_mask_when_suspended(node_factory):
    node, peer = setup_profile_with_runtime_state(node_factory, profile())
    with peer:
        peer.mutation(1, "N::alice::snomasks", "+c-k")
        client = node.client("visitor")
        start = len(client.lines)
        peer.send("UDBTEST SNOMASK visitor k")
        client.wait(lambda line: "UDBTEST SNOMASK=" in line, "external snomask setup", start=start)
        assert_snomask(client, peer, "visitor", "k")

        authenticate(client)
        assert_snomask(client, peer, "alice", "c")

        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend", "restore external snomask")
        client.wait(lambda line: "This nickname is suspended. Reason: restore external snomask" in line,
                    "suspension notice", start=start)
        assert_snomask(client, peer, "alice", "k")


def test_external_snomask_override_survives_suspension_of_profile_effect(node_factory):
    node, peer = setup_profile_with_runtime_state(node_factory, profile())
    with peer:
        peer.mutation(1, "N::alice::snomasks", "+c")
        client = node.client("visitor")
        start = len(client.lines)
        peer.send("UDBTEST SNOMASK visitor k")
        client.wait(lambda line: "UDBTEST SNOMASK=" in line, "external snomask setup", start=start)

        authenticate(client)
        assert_snomask(client, peer, "alice", "ck")

        start = len(client.lines)
        peer.send("UDBTEST SNOMASK alice d")
        client.wait(lambda line: "UDBTEST SNOMASK=" in line, "external snomask override", start=start)
        assert_snomask(client, peer, "alice", "d")

        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend", "preserve external snomask override")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve external snomask override" in line,
                    "suspension notice", start=start)
        assert_snomask(client, peer, "alice", "d")


def test_udb_owned_snomask_is_removed_when_suspended(node_factory):
    node, peer = setup_profile_with_runtime_state(node_factory, profile())
    with peer:
        peer.mutation(1, "N::alice::snomasks", "c")
        client = node.client("visitor")
        authenticate(client)
        assert_snomask(client, peer, "alice", "c")

        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend", "remove UDB snomask")
        client.wait(lambda line: "This nickname is suspended. Reason: remove UDB snomask" in line,
                    "suspension notice", start=start)
        assert_snomask(client, peer, "alice", "")


def test_matching_external_snomask_is_preserved_when_suspended(node_factory):
    node, peer = setup_profile_with_runtime_state(node_factory, profile())
    with peer:
        peer.mutation(1, "N::alice::snomasks", "c")
        client = node.client("visitor")
        start = len(client.lines)
        peer.send("UDBTEST SNOMASK visitor c")
        client.wait(lambda line: "UDBTEST SNOMASK=" in line, "matching external snomask", start=start)

        authenticate(client)
        assert_snomask(client, peer, "alice", "c")

        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend", "keep matching external snomask")
        client.wait(lambda line: "This nickname is suspended. Reason: keep matching external snomask" in line,
                    "suspension notice", start=start)
        assert_snomask(client, peer, "alice", "c")


def test_external_snomask_replacement_of_profile_value_survives_suspension(node_factory):
    node, peer = setup_profile_with_runtime_state(node_factory, profile())
    with peer:
        peer.mutation(1, "N::alice::snomasks", "c")
        client = node.client("visitor")
        authenticate(client)
        assert_snomask(client, peer, "alice", "c")

        start = len(client.lines)
        peer.send("UDBTEST SNOMASK alice k")
        client.wait(lambda line: "UDBTEST SNOMASK=" in line, "external snomask replacement", start=start)
        assert_snomask(client, peer, "alice", "k")

        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend", "preserve replacement snomask")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve replacement snomask" in line,
                    "suspension notice", start=start)
        assert_snomask(client, peer, "alice", "k")


def test_profile_without_snomasks_leaves_external_mask_untouched(node_factory):
    node, peer = setup_profile_with_runtime_state(node_factory, profile())
    with peer:
        client = node.client("visitor")
        start = len(client.lines)
        peer.send("UDBTEST SNOMASK visitor k")
        client.wait(lambda line: "UDBTEST SNOMASK=" in line, "external snomask setup", start=start)
        assert_snomask(client, peer, "visitor", "k")

        authenticate(client)
        assert_snomask(client, peer, "alice", "k")

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "leave external snomask untouched")
        client.wait(lambda line: "This nickname is suspended. Reason: leave external snomask untouched" in line,
                    "suspension notice", start=start)
        assert_snomask(client, peer, "alice", "k")


def test_hot_snomask_expression_replacement_then_suspend_restores_external_mask(node_factory):
    node, peer = setup_profile_with_runtime_state(node_factory, profile())
    with peer:
        peer.mutation(1, "N::alice::snomasks", "+c")
        client = node.client("visitor")
        start = len(client.lines)
        peer.send("UDBTEST SNOMASK visitor ck")
        client.wait(lambda line: "UDBTEST SNOMASK=" in line, "external snomask setup", start=start)

        authenticate(client)
        assert_snomask(client, peer, "alice", "ck")

        peer.mutation(2, "N::alice::snomasks", "-k")
        assert_snomask(client, peer, "alice", "c")

        start = len(client.lines)
        peer.mutation(3, "N::alice::suspend", "restore original external mask")
        client.wait(lambda line: "This nickname is suspended. Reason: restore original external mask" in line,
                    "suspension notice", start=start)
        assert_snomask(client, peer, "alice", "ck")


def test_staged_profile_cannot_apply_vhost_before_valid_end(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test")
    with Peer(node) as peer:
        client = node.client("alice")
        before_host = host(client, "alice")
        round_id, digest = peer.offer(profile())
        peer.send(f"DB {node.sid} BEGIN {round_id} N private {digest}")
        for path, value in profile().items():
            peer.send(f"DB {node.sid} PUT {round_id} N private {path} :{value}")
        peer.barrier()
        assert "r" not in modes(client, "alice")
        assert host(client, "alice") == before_host
        peer.send(f"DB {node.sid} END {round_id} N private {'f' * 64}")
        peer.barrier()
        assert "r" not in modes(client, "alice")
        assert host(client, "alice") == before_host


def test_udb_owned_mode_s_is_removed_on_suspend(node_factory):
    node, peer = setup_profile(node_factory, profile())
    with peer:
        client = node.client("visitor")
        client.request("MODE visitor -S")
        assert "S" not in modes(client, "visitor")
        authenticate(client)
        peer.mutation(1, "N::alice::modes", "+S")
        assert "S" in modes(client, "alice")

        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend", "remove owned mode S")
        client.wait(lambda line: "This nickname is suspended. Reason: remove owned mode S" in line,
                    "suspension notice", start=start)
        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert "S" not in current_modes


def test_externally_owned_mode_s_survives_profile_suspension(node_factory):
    node, peer = setup_profile(node_factory, profile(modes="+S"), ulines=["peer.test"])
    with peer:
        client = node.client("visitor")
        start = len(client.lines)
        peer.send("SVS2MODE visitor +S")
        client.wait(lambda line: " MODE visitor " in line and "+S" in line,
                    "external mode S assignment", start=start)
        authenticate(client)
        assert "S" in modes(client, "alice")

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "preserve external mode S")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve external mode S" in line,
                    "suspension notice", start=start)
        current_modes = modes(client, "alice")
        assert "r" not in current_modes
        assert "S" in current_modes


def test_external_removal_of_owned_mode_s_is_not_undone_on_suspend(node_factory):
    node, peer = setup_profile(node_factory, profile(modes="+S"), ulines=["peer.test"])
    with peer:
        client = node.client("visitor")
        client.request("MODE visitor -S")
        authenticate(client)
        assert "S" in modes(client, "alice")

        start = len(client.lines)
        peer.send("SVS2MODE alice -S")
        client.wait(lambda line: " MODE alice " in line and "-S" in line,
                    "external mode S removal", start=start)
        assert "S" not in modes(client, "alice")

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "keep external mode removal")
        client.wait(lambda line: "This nickname is suspended. Reason: keep external mode removal" in line,
                    "suspension notice", start=start)
        assert "S" not in modes(client, "alice")


def test_hot_mode_s_deletion_keeps_authenticated_identity(node_factory):
    node, peer = setup_profile(node_factory, profile(modes="+S"))
    with peer:
        client = node.client("visitor")
        client.request("MODE visitor -S")
        authenticate(client)
        assert "S" in modes(client, "alice")

        peer.mutation(1, "N::alice::modes")
        current_modes = modes(client, "alice")
        assert "r" in current_modes
        assert "S" not in current_modes


def test_matching_external_vhost_survives_profile_suspension(node_factory):
    node, peer = setup_profile(node_factory, profile(), ulines=["peer.test"])
    with peer:
        client = node.client("visitor")
        peer.send("CHGHOST visitor alice.test")
        client.wait(lambda line: " 396 " in line and "alice.test" in line,
                    "external vhost matching profile")
        assert host(client, "visitor") == "alice.test"

        authenticate(client)
        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "preserve matching external vhost")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve matching external vhost" in line,
                    "suspension notice", start=start)
        assert host(client, "alice") == "alice.test"


def test_external_vhost_override_survives_profile_suspension(node_factory):
    node, peer = setup_profile(node_factory, profile(), ulines=["peer.test"])
    with peer:
        client = node.client("visitor")
        authenticate(client)
        start = len(client.lines)
        peer.send("CHGHOST alice external.test")
        client.wait(lambda line: " 396 " in line and "external.test" in line,
                    "external vhost override", start=start)
        assert host(client, "alice") == "external.test"

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "preserve external vhost override")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve external vhost override" in line,
                    "suspension notice", start=start)
        assert host(client, "alice") == "external.test"


def test_hot_vhost_replacement_remains_owned_until_suspension(node_factory):
    node, peer = setup_profile(node_factory, profile())
    with peer:
        client = node.client("visitor")
        before_host = host(client, "visitor")
        authenticate(client)

        peer.mutation(1, "N::alice::vhost", "updated.test")
        assert host(client, "alice") == "updated.test"

        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend", "revoke updated vhost")
        client.wait(lambda line: "This nickname is suspended. Reason: revoke updated vhost" in line,
                    "suspension notice", start=start)
        assert "r" not in modes(client, "alice")
        assert host(client, "alice") == before_host
        assert not any(" NICK :" in line for line in client.lines[start:])


def test_new_profile_vhost_overrides_prior_external_host_on_authentication(node_factory):
    records = {"alice::pass": profile()["alice::pass"], "alice::access": "127.0.0.0/8"}
    node, peer = setup_profile(node_factory, records, ulines=["peer.test"])
    with peer:
        client = node.client("visitor")
        peer.send("CHGHOST visitor external-before-auth.test")
        client.wait(lambda line: " 396 " in line and "external-before-auth.test" in line,
                    "external vhost before profile insertion")
        peer.mutation(1, "N::alice::vhost", "alice.test")

        authenticate(client)
        assert host(client, "alice") == "alice.test"


def test_external_operclass_and_accounting_survive_profile_auth_and_suspend(node_factory):
    node, peer = setup_profile_with_runtime_state(node_factory, profile(oper="locop"))
    with peer:
        client = node.client("visitor")
        assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
        before = read_oper_state(client, peer, "visitor")
        assert before["oper"] and before["operlist"] == 1, before
        assert before["operclass"] == "netadmin-with-override", before

        authenticate(client)
        authenticated = read_oper_state(client, peer, "alice")
        assert authenticated["oper"] and authenticated["operlist"] == 1, authenticated
        assert authenticated["operclass"] == before["operclass"], authenticated
        assert authenticated["opercount"] == before["opercount"], authenticated

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "preserve external oper")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve external oper" in line,
                    "suspension notice", start=start)
        suspended = read_oper_state(client, peer, "alice")
        assert suspended["oper"] and suspended["operlist"] == 1, suspended
        assert suspended["operclass"] == before["operclass"], suspended
        assert suspended["opercount"] == before["opercount"], suspended


def test_external_oper_with_same_profile_class_keeps_oper_ownership(node_factory):
    operclass = "netadmin-with-override"
    node, peer = setup_profile_with_runtime_state(node_factory, profile(oper=operclass))
    with peer:
        client = node.client("visitor")
        assert any(" 381 " in line for line in client.request("OPER testoper operpass"))
        before = read_oper_state(client, peer, "visitor")
        assert before["oper"] and before["operclass"] == operclass, before
        assert before["operlist"] == 1, before

        authenticate(client)
        adopted = read_oper_state(client, peer, "alice")
        assert adopted == before, (before, adopted)

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "preserve same-class external oper")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve same-class external oper" in line,
                    "same-class external oper suspension", start=start)
        suspended = read_oper_state(client, peer, "alice")
        assert suspended == before, (before, suspended)


def test_udb_granted_oper_has_one_list_entry_and_revokes_accounting_on_suspend(node_factory):
    node, peer = setup_profile_with_runtime_state(node_factory, profile(oper="locop"))
    with peer:
        client = node.client("visitor")
        before = read_oper_state(client, peer, "visitor")
        assert not before["oper"] and before["operclass"] == "-", before

        authenticate(client)
        granted = read_oper_state(client, peer, "alice")
        assert granted["oper"] and granted["operclass"] == "locop", granted
        assert granted["opercount"] == before["opercount"] + 1, (before, granted)
        assert granted["operlist"] == 1, granted

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "revoke UDB oper")
        client.wait(lambda line: "This nickname is suspended. Reason: revoke UDB oper" in line,
                    "suspension notice", start=start)
        revoked = read_oper_state(client, peer, "alice")
        assert not revoked["oper"] and revoked["operclass"] == "-", revoked
        assert revoked["opercount"] == before["opercount"], (before, revoked)
        assert revoked["operlist"] == 0, revoked


def test_udb_oper_modes_and_snomask_defaults_are_cleaned_up_on_suspension(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        runtime_state_fixture=True, oper_defaults=True)
    with Peer(node) as peer:
        oper_profile = profile(oper="locop")
        oper_profile.pop("alice::vhost")
        assert " ACK " in peer.transfer(oper_profile)
        client = node.client("visitor")
        identified = client.request("NICK alice:secret")
        assert any(line.endswith(" NICK :alice") for line in identified), identified
        assert any("You are now identified for nickname alice" in line for line in identified), identified
        assert "r" in modes(client, "alice")

        granted = read_oper_state(client, peer, "alice")
        assert granted["oper"] and granted["operclass"] == "locop", granted
        granted_modes = modes(client, "alice")
        assert {"x", "w", "s"} <= granted_modes, granted_modes
        assert_snomask(client, peer, "alice", "c")
        assert host(client, "alice").lower() == "udb.locop.oper.test"

        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "clean up oper defaults")
        client.wait(lambda line: "This nickname is suspended. Reason: clean up oper defaults" in line,
                    "suspension after UDB OPER defaults", start=start)
        revoked = read_oper_state(client, peer, "alice")
        assert not revoked["oper"] and revoked["operclass"] == "-", revoked
        remaining_modes = modes(client, "alice")
        assert "s" not in remaining_modes, remaining_modes
        assert_snomask(client, peer, "alice", "")
        assert host(client, "alice").lower() != "udb.locop.oper.test"


def test_oper_vhost_default_precedence_and_external_override(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        ulines=["peer.test"], oper_defaults=True)
    with Peer(node) as peer:
        records = profile(oper="locop", vhost="literal@$nick.test")
        assert " ACK " in peer.transfer(records)
        client = node.client("visitor")
        observer = node.client("observer")
        before = client.request("WHOIS visitor", terminator=lambda line: " 318 " in line)
        before_ident = next(line for line in before if " 311 " in line).split()[4]

        replies = client.request("NICK alice:secret")
        assert any(line.endswith(" NICK :alice") for line in replies), replies
        assert any("You are now identified for nickname alice" in line for line in replies), replies
        peer.send("SVSNOLAG + alice")
        peer.barrier()
        whois = client.request("WHOIS alice", terminator=lambda line: " 318 " in line)
        registered = next(line for line in whois if " 311 " in line).split()
        assert registered[4] == before_ident, registered
        assert registered[5] == "literal@$nick.test", registered
        assert "r" in modes(client, "alice") and "o" in modes(client, "alice")

        peer.mutation(1, "N::alice::vhost")
        assert "r" in modes(client, "alice")
        assert host(client, "alice").lower() == "udb.locop.oper.test"

        start = len(client.lines)
        peer.send("CHGHOST alice external-oper.test")
        client.wait(lambda line: " 396 " in line and "external-oper.test" in line,
                    "external oper vhost override", start=start)
        start = len(client.lines)
        peer.mutation(2, "N::alice::suspend", "preserve external oper host")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve external oper host" in line,
                    "suspend after external oper vhost", start=start)
        assert host(client, "alice") == "external-oper.test"
        assert host(observer, "alice") == "external-oper.test"


def test_stale_oper_vhost_and_mode_t_are_cleared_after_manual_deoper(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        oper_defaults=True)
    with Peer(node) as peer:
        records = {
            "alice::pass": profile()["alice::pass"],
            "alice::access": "127.0.0.0/8",
            "alice::oper": "locop",
        }
        assert " ACK " in peer.transfer(records)
        client = node.client("visitor")
        replies = client.request("NICK alice:secret")
        assert any(line.endswith(" NICK :alice") for line in replies), replies
        assert host(client, "alice").lower() == "udb.locop.oper.test"
        assert "t" in modes(client, "alice")

        client.request("MODE alice -o")
        assert "o" not in modes(client, "alice")
        start = len(client.lines)
        peer.mutation(1, "N::alice::suspend", "restore stale oper vhost")
        client.wait(lambda line: "This nickname is suspended. Reason: restore stale oper vhost" in line,
                    "suspension after manual de-oper", start=start)
        suspended_modes = modes(client, "alice")
        assert "r" not in suspended_modes
        assert "t" not in suspended_modes
        assert host(client, "alice").lower() != "udb.locop.oper.test"


def test_synchronous_local_oper_vhost_remains_external_through_reconciliation_and_deoper(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        runtime_state_fixture=True, oper_defaults=True)
    with Peer(node) as peer:
        profile_records = profile(oper="locop")
        profile_records.pop("alice::vhost")
        records = {path.replace("alice::", "ophook::", 1): value
                   for path, value in profile_records.items()}
        assert " ACK " in peer.transfer(records)
        client = node.client("ophook-client")
        observer = node.client("observer")
        replies = client.request("NICK ophook:secret")
        assert any(line.endswith(" NICK :ophook") for line in replies), replies
        assert "o" in modes(client, "ophook")
        peer.send("SVSNOLAG + ophook")
        peer.barrier()
        assert host(client, "ophook") == "hook.external.test"

        peer.mutation(1, "N::ophook::swhois", "Hook-owned vhost test")
        assert host(client, "ophook") == "hook.external.test"

        start = len(client.lines)
        peer.mutation(2, "N::ophook::suspend", "preserve synchronous oper hook vhost")
        client.wait(lambda line: "This nickname is suspended. Reason: preserve synchronous oper hook vhost" in line,
                    "suspension after LOCAL_OPER hook", start=start)
        assert host(observer, "ophook") == "hook.external.test"


def test_suspend_sequence_preserves_external_vhost_until_reauthentication(node_factory):
    password = "seqonesecret"
    password_record = profile(password)["alice::pass"]
    records = {
        "seqone::pass": password_record,
        "seqone::access": "127.0.0.0/8",
        "seqone::vhost": "seqone-a.test",
    }
    node, peer = setup_profile(node_factory, records, ulines=["peer.test"])
    with peer:
        client = node.client("seqone-client")
        replies = client.request(f"NICK seqone:{password}")
        assert any(line.endswith(" NICK :seqone") for line in replies), replies
        assert "r" in modes(client, "seqone")
        assert host(client, "seqone") == "seqone-a.test"

        start = len(client.lines)
        peer.send("CHGHOST seqone seq-ext.test")
        client.wait(lambda line: " 396 " in line and "seq-ext.test" in line,
                    "external host override", start=start)
        start = len(client.lines)
        peer.mutation(1, "N::seqone::suspend", "manual review")
        client.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "suspension with external host", start=start)
        assert "r" not in modes(client, "seqone")
        assert host(client, "seqone") == "seq-ext.test"

        start = len(client.lines)
        peer.mutation(2, "N::seqone::suspend")
        rename = client.wait(lambda line: " NICK :Guest" in line,
                             "protected unsuspend rename", start=start)
        guest = rename.rsplit(" NICK :", 1)[-1]
        assert "r" not in modes(client, guest)
        replies = client.request(f"NICK seqone:{password}")
        assert any(line.endswith(" NICK :seqone") for line in replies), replies
        assert "r" in modes(client, "seqone")
        assert host(client, "seqone") == "seqone-a.test"


def test_mode_sequence_removes_only_the_newly_owned_mode_on_suspension(node_factory):
    password = "seqmodesecret"
    records = {
        "seqmode::pass": profile(password)["alice::pass"],
        "seqmode::access": "127.0.0.0/8",
        "seqmode::modes": "+S",
    }
    node, peer = setup_profile(node_factory, records, ulines=["peer.test"])
    with peer:
        client = node.client("seqmode-client")
        start = len(client.lines)
        peer.send("SVS2MODE seqmode-client +S")
        client.wait(lambda line: " MODE seqmode-client " in line and "+S" in line,
                    "external +S ownership", start=start)
        replies = client.request(f"NICK seqmode:{password}")
        assert any(line.endswith(" NICK :seqmode") for line in replies), replies
        assert {"r", "S"} <= modes(client, "seqmode")

        start = len(client.lines)
        peer.mutation(1, "N::seqmode::modes", "+SD")
        client.wait(lambda line: " MODE seqmode " in line and "+D" in line,
                    "hot mode update", start=start)
        assert {"r", "S", "D"} <= modes(client, "seqmode")

        start = len(client.lines)
        peer.mutation(2, "N::seqmode::suspend", "manual review")
        client.wait(lambda line: "This nickname is suspended. Reason: manual review" in line,
                    "mode ownership suspension", start=start)
        current = modes(client, "seqmode")
        assert "r" not in current
        assert "S" in current
        assert "D" not in current


def test_snapshot_sequence_updates_profile_then_deletes_without_clobbering_external_host(node_factory):
    password = "snapseqsecret"
    records = {
        "snapseq::pass": profile(password)["alice::pass"],
        "snapseq::access": "127.0.0.0/8",
        "snapseq::vhost": "snapseq-a.test",
        "snapseq::modes": "+S",
    }
    node, peer = setup_profile(node_factory, records, ulines=["peer.test"])
    with peer:
        client = node.client("snapseq-client")
        replies = client.request(f"NICK snapseq:{password}")
        assert any(line.endswith(" NICK :snapseq") for line in replies), replies
        assert "r" in modes(client, "snapseq")
        assert "S" in modes(client, "snapseq")

        updated = {
            "snapseq::pass": records["snapseq::pass"],
            "snapseq::access": records["snapseq::access"],
            "snapseq::vhost": "snapseq-b.test",
            "snapseq::modes": "+S",
        }
        assert " ACK " in peer.transfer(updated, txid="identity-sequence-a")
        assert "r" in modes(client, "snapseq")
        assert host(client, "snapseq") == "snapseq-b.test"

        start = len(client.lines)
        peer.send("CHGHOST snapseq external-seq.test")
        client.wait(lambda line: " 396 " in line and "external-seq.test" in line,
                    "external host before profile deletion", start=start)
        assert " ACK " in peer.transfer({"other::access": "127.0.0.0/8"},
                                        txid="identity-sequence-b")
        client.request("MODE snapseq", terminator=lambda line: " 221 " in line)
        assert not any(" NICK " in line for line in client.lines[start:])
        current = modes(client, "snapseq")
        assert "r" not in current
        assert "S" not in current
        assert host(client, "snapseq") == "external-seq.test"


def test_oper_auto_join_kill_closes_client_during_identity_effect_application(node_factory):
    node = node_factory(ready=True, peers=["peer.test"], propagator="peer.test",
                        runtime_state_fixture=True,
                        oper_auto_join=("#udb-opers", "#udb-kill-on-join"))
    with Peer(node) as peer:
        records = {
            "opkill::pass": profile("opkillsecret")["alice::pass"],
            "opkill::access": "127.0.0.0/8",
            "opkill::oper": "locop",
            "opkill::modes": "+S",
            "opkill::vhost": "never.test",
        }
        assert " ACK " in peer.transfer(records)
        client = node.client("opkill-client")
        client.send("NICK opkill:opkillsecret")
        with pytest.raises(ConnectionError, match="IRC peer closed the connection"):
            client.wait(lambda _line: False, "oper auto-join test hook disconnect", timeout=15)
