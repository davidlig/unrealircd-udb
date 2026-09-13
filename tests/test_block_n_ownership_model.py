#!/usr/bin/env python3
"""Independent state model for Block N runtime ownership.

The model mirrors the implementation contract without sharing its code:

  authentication -> identity -> desired(profile) reconciled against
  owned(runtime effects) and unknown external state.

Invariants checked after every event:

  * a passless or suspended profile can never hold identity;
  * without identity there are no UDB-owned effects;
  * state UDB does not own is never removed or overwritten on revoke;
  * a failed authentication, a forced rename or leaving a nick grants nothing;
  * a deleted or suspended profile never restores identity, while a pass or
    access value change that still permits the holder keeps it.

The scripted scenarios here correspond to the runtime scenarios in
tests/runtime_nick_auth.py (sections W, X, Y and AA/AB): the model states what
must happen and the runtime suite proves the binary does it.
"""

import random
import sys


def require(condition, message):
    if not condition:
        raise AssertionError(message)


_UNSET = object()


def apply_snomask_expression(current, expr):
    if expr is None:
        return None
    current_set = set(current) if current else set()
    mode = '+'
    for ch in expr:
        if ch in ('+', '-'):
            mode = ch
        elif ch.isalpha():
            if mode == '+':
                current_set.add(ch)
            else:
                current_set.discard(ch)
    if not current_set:
        return None
    return "".join(sorted(current_set))


class BlockNModel:
    def __init__(self, profile):
        self.profile = dict(profile)
        self.identity = False
        self.owned_modes = set()
        self.applied_vhost = None
        self.vhost_owned = False
        self.previous_snomask = None
        self.applied_snomask = None
        self.snomask_owned = False
        self.oper_owned = False
        self.ext_modes = set()
        self.ext_vhost = None
        self.ext_snomask = None
        self.ext_oper = False
        self.ext_operclass = None
        self.runtime_modes = set()
        self.runtime_vhost = None
        self.runtime_snomask = None
        self.runtime_oper = False
        self.runtime_operclass = None
        # Known limitation: an external claim to the exact value UDB already
        # applied is indistinguishable from UDB's own state and is dropped on
        # revoke (same as an invisible claim on an already-set mode bit).
        self.vhost_ambiguous = False
        self.snomask_ambiguous = False

    # -- runtime projections ------------------------------------------------
    def has_pass(self):
        return bool(self.profile.get("pass"))

    def is_suspended(self):
        return bool(self.profile.get("suspend"))

    # -- UDB effect primitives ---------------------------------------------
    def revoke_vhost(self):
        if not self.vhost_owned:
            return
        if self.runtime_vhost == self.applied_vhost:
            self.runtime_vhost = self.ext_vhost
        self.vhost_owned = False
        self.applied_vhost = None

    def revoke_snomasks(self):
        if not self.snomask_owned:
            return
        if self.runtime_snomask == self.applied_snomask:
            self.runtime_snomask = self.previous_snomask
        self.snomask_owned = False
        self.previous_snomask = None
        self.applied_snomask = None

    def revoke_oper(self):
        if not self.oper_owned:
            return
        if not self.runtime_oper:
            self.oper_owned = False
            self.runtime_operclass = None
            return
        self.runtime_oper = False
        self.runtime_operclass = None
        self.oper_owned = False

    def revoke_effects(self):
        self.revoke_oper()
        self.runtime_modes -= self.owned_modes
        self.owned_modes = set()
        self.revoke_vhost()
        self.revoke_snomasks()

    def revoke_identity(self):
        self.identity = False

    def apply_effects(self):
        desired_modes = set(self.profile.get("modes", ()))
        removed = self.owned_modes - desired_modes
        self.runtime_modes -= removed
        added = desired_modes - self.runtime_modes
        self.runtime_modes |= desired_modes
        self.owned_modes = (self.owned_modes & desired_modes) | added

        desired_vhost = self.profile.get("vhost")
        if not desired_vhost:
            self.revoke_vhost()
        elif self.vhost_owned and self.applied_vhost == desired_vhost:
            if self.runtime_vhost != desired_vhost:
                self.runtime_vhost = desired_vhost
        elif not self.vhost_owned and self.runtime_vhost == desired_vhost:
            pass
        else:
            self.revoke_vhost()
            self.runtime_vhost = desired_vhost
            self.applied_vhost = desired_vhost
            self.vhost_owned = True

        desired_snomask = self.profile.get("snomask")
        if not desired_snomask:
            self.revoke_snomasks()
        else:
            if self.snomask_owned:
                self.revoke_snomasks()
            self.previous_snomask = self.runtime_snomask
            self.runtime_snomask = apply_snomask_expression(self.runtime_snomask, desired_snomask)
            self.applied_snomask = self.runtime_snomask
            self.snomask_owned = True

        desired_oper = self.profile.get("oper")
        if not desired_oper:
            self.revoke_oper()
        elif self.runtime_oper and not self.oper_owned:
            # External oper: UDB never touches it, never replaces operclass, never claims ownership
            pass
        elif self.oper_owned:
            self.runtime_operclass = desired_oper
        else:
            self.runtime_oper = True
            self.runtime_operclass = desired_oper
            self.oper_owned = True

    # -- events -------------------------------------------------------------
    def authenticate(self, ok=True):
        if not ok or not self.has_pass() or self.is_suspended():
            return False
        self.identity = True
        self.apply_effects()
        return True

    def suspend(self):
        self.revoke_effects()
        self.revoke_identity()

    def unsuspend(self):
        # DEL suspend renames the holder: identity never returns.
        self.revoke_effects()
        self.revoke_identity()
        self.profile["suspend"] = False

    def policy_change(self, pass_value=None, access=None, access_ok=True):
        if pass_value is not None:
            self.profile["pass"] = pass_value
        if access is not None:
            self.profile["access"] = access
        # Credential/access values are re-checked live: only losing pass, being
        # suspended, or an access list that no longer permits revokes identity.
        if not self.has_pass() or self.is_suspended() or not access_ok:
            self.revoke_effects()
            self.revoke_identity()
        elif self.identity:
            self.apply_effects()

    def delete_profile(self):
        self.revoke_effects()
        self.revoke_identity()
        self.profile = {"exists": False}

    def forced_rename(self):
        self.revoke_effects()
        self.revoke_identity()

    def leave_nick(self):
        self.revoke_effects()
        self.revoke_identity()

    def effect_update(self, modes=_UNSET, vhost=_UNSET, snomask=_UNSET, oper=_UNSET):
        if modes is not _UNSET:
            self.profile["modes"] = tuple(modes) if modes else ()
        if vhost is not _UNSET:
            self.profile["vhost"] = vhost
        if snomask is not _UNSET:
            self.profile["snomask"] = snomask
        if oper is not _UNSET:
            self.profile["oper"] = oper
        if self.identity:
            self.apply_effects()
        else:
            self.revoke_effects()

    def external_vhost(self, value):
        self.ext_vhost = value
        self.runtime_vhost = value
        self.vhost_ambiguous = self.vhost_owned and value == self.applied_vhost

    def external_mode(self, mode):
        self.ext_modes.add(mode)
        self.runtime_modes.add(mode)

    def external_unsmode(self, mode):
        self.ext_modes.discard(mode)
        self.runtime_modes.discard(mode)

    def external_snomask(self, value):
        self.ext_snomask = value
        self.runtime_snomask = value
        self.snomask_ambiguous = self.snomask_owned and value == self.applied_snomask

    def external_oper(self, operclass="netadmin", snomask="BOSbcdfkoqsx"):
        self.ext_oper = True
        self.ext_operclass = operclass
        self.runtime_oper = True
        self.runtime_operclass = operclass
        self.oper_owned = False
        if snomask is not None:
            self.ext_snomask = snomask
            self.runtime_snomask = snomask

    def external_deoper(self):
        self.ext_oper = False
        self.ext_operclass = None
        if not self.oper_owned:
            self.runtime_oper = False
            self.runtime_operclass = None

    def snapshot(self, new_profile, access_ok=True):
        keep = (self.identity and new_profile.get("pass") and
                not new_profile.get("suspend") and access_ok)
        self.profile = dict(new_profile)
        if keep:
            self.apply_effects()
        else:
            self.revoke_effects()
            self.revoke_identity()

    # -- invariants ---------------------------------------------------------
    def check(self, context):
        require(not (not self.has_pass() or self.is_suspended()) or not self.identity,
                f"{context}: passless/suspended profile kept identity")
        require(self.identity or (not self.owned_modes and not self.vhost_owned and not self.snomask_owned and not self.oper_owned),
                f"{context}: effects owned without identity")
        require(self.vhost_owned or self.vhost_ambiguous or self.runtime_vhost == self.ext_vhost,
                f"{context}: external vhost changed while unowned")
        require(self.snomask_owned or self.snomask_ambiguous or self.runtime_snomask == self.ext_snomask,
                f"{context}: external snomask changed while unowned")
        require(not self.owned_modes or self.identity,
                f"{context}: owned mode bookkeeping diverged without identity")
        require(not self.oper_owned or self.identity,
                f"{context}: oper owned without identity")
        require(not self.oper_owned or self.runtime_oper,
                f"{context}: oper owned but runtime not oper")
        require(not (self.ext_oper and self.oper_owned),
                f"{context}: external oper was hijacked by UDB")
        if self.ext_oper:
            require(self.runtime_oper,
                    f"{context}: external oper was revoked by UDB")
            require(self.runtime_operclass == self.ext_operclass,
                    f"{context}: external operclass was altered by UDB")


def scenario_mode_ownership():
    model = BlockNModel({"pass": "p", "access": "a", "modes": ("S",)})
    model.authenticate()
    require(model.runtime_modes == {"S"} and model.owned_modes == {"S"},
            "mode A: UDB did not own an added mode")
    model.suspend()
    require(model.runtime_modes == set(), "mode A: suspend kept a UDB-added mode")
    model.check("mode A")

    model = BlockNModel({"pass": "p", "access": "a", "modes": ("S",)})
    model.external_mode("S")
    model.authenticate()
    model.suspend()
    require("S" in model.runtime_modes, "mode B: suspend removed an external mode")
    model.check("mode B")

    model = BlockNModel({"pass": "p", "access": "a", "modes": ("S",)})
    model.authenticate()
    model.external_unsmode("S")
    model.suspend()
    require("S" not in model.runtime_modes, "mode C: external removal mishandled")
    model.check("mode C")

    model = BlockNModel({"pass": "p", "access": "a", "modes": ("S",)})
    model.authenticate()
    model.effect_update(modes=())
    require("S" not in model.runtime_modes, "mode D: hot removal kept the owned mode")
    model.check("mode D")

    model = BlockNModel({"pass": None, "access": "a", "modes": ("S",)})
    model.external_mode("S")
    model.suspend()
    require("S" in model.runtime_modes, "mode E: passless profile touched external state")
    model.check("mode E")


def scenario_vhost_ownership():
    model = BlockNModel({"pass": "p", "access": "a", "vhost": "a.test"})
    model.authenticate()
    require(model.runtime_vhost == "a.test" and model.vhost_owned, "vhost A: not applied")
    model.suspend()
    require(model.runtime_vhost is None, "vhost A: revoke kept the UDB vhost")
    model.check("vhost A")

    model = BlockNModel({"pass": "p", "access": "a", "vhost": "a.test"})
    model.external_vhost("a.test")
    model.authenticate()
    model.suspend()
    require(model.runtime_vhost == "a.test", "vhost B: external matching value removed")
    model.check("vhost B")

    model = BlockNModel({"pass": "p", "access": "a", "vhost": "a.test"})
    model.authenticate()
    model.external_vhost("b.test")
    model.suspend()
    require(model.runtime_vhost == "b.test", "vhost C: external override removed")
    model.check("vhost C")

    model = BlockNModel({"pass": "p", "access": "a", "vhost": "a.test"})
    model.authenticate()
    model.effect_update(vhost="b.test")
    require(model.runtime_vhost == "b.test" and model.vhost_owned, "vhost D: hot update failed")
    model.suspend()
    require(model.runtime_vhost is None, "vhost D: revoke kept the new owned value")
    model.check("vhost D")

    model = BlockNModel({"pass": "p", "access": "a"})
    model.external_vhost("b.test")
    model.effect_update(vhost="c.test")
    model.authenticate()
    require(model.runtime_vhost == "c.test" and model.vhost_owned,
            "vhost E: explicit profile vhost not applied and owned")
    model.check("vhost E")


def scenario_snomask_ownership():
    model = BlockNModel({"pass": "p", "access": "a", "snomask": "c"})
    model.authenticate()
    require(model.runtime_snomask == "c" and model.snomask_owned, "snomask A: not applied")
    model.suspend()
    require(model.runtime_snomask is None, "snomask A: revoke kept UDB value")
    model.check("snomask A")

    # Relative expressions: +c, -c, +c-k
    model = BlockNModel({"pass": "p", "access": "a", "snomask": "+c-k"})
    model.external_snomask("k")
    model.authenticate()
    require(model.runtime_snomask == "c" and model.snomask_owned, "snomask B1: +c-k did not produce c from k")
    model.suspend()
    require(model.runtime_snomask == "k", "snomask B1: external value k not restored")
    model.check("snomask B1")

    model = BlockNModel({"pass": "p", "access": "a", "snomask": "-k"})
    model.external_snomask("ck")
    model.authenticate()
    require(model.runtime_snomask == "c" and model.snomask_owned, "snomask B2: -k did not produce c from ck")
    model.suspend()
    require(model.runtime_snomask == "ck", "snomask B2: external value ck not restored")
    model.check("snomask B2")

    model = BlockNModel({"pass": "p", "access": "a", "snomask": "+c"})
    model.authenticate()
    require(model.runtime_snomask == "c" and model.snomask_owned, "snomask B3: +c did not produce c from empty")
    model.suspend()
    require(model.runtime_snomask is None, "snomask B3: revoke kept UDB value")
    model.check("snomask B3")

    model = BlockNModel({"pass": "p", "access": "a", "snomask": "-c+k"})
    model.external_snomask("c")
    model.authenticate()
    require(model.runtime_snomask == "k" and model.snomask_owned, "snomask B4: -c+k did not produce k from c")
    model.suspend()
    require(model.runtime_snomask == "c", "snomask B4: external value c not restored")
    model.check("snomask B4")

    model = BlockNModel({"pass": "p", "access": "a", "snomask": "+ck"})
    model.authenticate()
    require(model.runtime_snomask == "ck" and model.snomask_owned, "snomask B5: +ck did not produce ck")
    model.suspend()
    require(model.runtime_snomask is None, "snomask B5: revoke kept ck")
    model.check("snomask B5")

    model = BlockNModel({"pass": "p", "access": "a", "snomask": "c"})
    model.external_snomask("c")
    model.authenticate()
    model.suspend()
    require(model.runtime_snomask == "c", "snomask C: external matching value removed")
    model.check("snomask C")

    model = BlockNModel({"pass": "p", "access": "a", "snomask": "+c"})
    model.authenticate()
    model.external_snomask("k")
    model.suspend()
    require(model.runtime_snomask == "k", "snomask D: external override overwritten")
    model.check("snomask D")

    model = BlockNModel({"pass": "p", "access": "a"})
    model.external_snomask("k")
    model.authenticate()
    model.suspend()
    require(model.runtime_snomask == "k", "snomask E: absent profile removed external value")
    model.check("snomask E")

    model = BlockNModel({"pass": "p", "access": "a", "snomask": "+c"})
    model.external_snomask("k")
    model.authenticate()
    require(model.runtime_snomask == "ck", "snomask F: +c on k did not produce ck")
    model.effect_update(snomask="+j")
    require(model.runtime_snomask == "jk", "snomask F: hot update to +j did not produce jk")
    model.suspend()
    require(model.runtime_snomask == "k", "snomask F: revoke after hot update did not restore k")
    model.check("snomask F")


def scenario_oper_ownership():
    # 1. External oper with different class: UDB must never touch it
    model = BlockNModel({"pass": "p", "access": "a", "oper": "netadmin"})
    model.external_oper("locop")
    model.authenticate()
    require(model.runtime_oper and model.runtime_operclass == "locop" and not model.oper_owned,
            "oper A: UDB altered external operclass or claimed ownership")
    model.suspend()
    require(model.runtime_oper and model.runtime_operclass == "locop" and not model.oper_owned,
            "oper A: suspend de-opered external oper")
    model.delete_profile()
    require(model.runtime_oper and model.runtime_operclass == "locop",
            "oper A: delete profile de-opered external oper")
    model.check("oper A")

    # 2. External oper with same class: UDB must never claim ownership
    model = BlockNModel({"pass": "p", "access": "a", "oper": "locop"})
    model.external_oper("locop")
    model.authenticate()
    require(model.runtime_oper and model.runtime_operclass == "locop" and not model.oper_owned,
            "oper B: UDB claimed ownership of matching external oper")
    model.suspend()
    require(model.runtime_oper and model.runtime_operclass == "locop",
            "oper B: suspend de-opered matching external oper")
    model.check("oper B")

    # 3. Normal user: UDB grants oper, suspend revokes it
    model = BlockNModel({"pass": "p", "access": "a", "oper": "netadmin"})
    model.authenticate()
    require(model.runtime_oper and model.runtime_operclass == "netadmin" and model.oper_owned,
            "oper C: UDB did not grant oper to non-oper")
    model.suspend()
    require(not model.runtime_oper and model.runtime_operclass is None and not model.oper_owned,
            "oper C: suspend did not revoke UDB-granted oper")
    model.check("oper C")

    # 4. Hot update of operclass on UDB-owned oper
    model = BlockNModel({"pass": "p", "access": "a", "oper": "netadmin"})
    model.authenticate()
    model.effect_update(oper="locop")
    require(model.runtime_oper and model.runtime_operclass == "locop" and model.oper_owned,
            "oper D: hot update failed to change operclass")
    model.suspend()
    require(not model.runtime_oper and not model.oper_owned,
            "oper D: suspend did not revoke hot-updated oper")
    model.check("oper D")

    # 5. Hot removal of oper on UDB-owned oper
    model = BlockNModel({"pass": "p", "access": "a", "oper": "netadmin"})
    model.authenticate()
    model.effect_update(oper=None)
    require(not model.runtime_oper and not model.oper_owned and model.identity,
            "oper E: hot removal did not revoke oper while keeping identity")
    model.check("oper E")

    # 6. Passless profile with oper
    model = BlockNModel({"pass": None, "access": "a", "oper": "netadmin"})
    model.authenticate()
    require(not model.runtime_oper and not model.identity,
            "oper F: passless profile granted oper")
    model.check("oper F")

    # 7. Suspended profile with oper
    model = BlockNModel({"pass": "p", "access": "a", "oper": "netadmin", "suspend": True})
    model.authenticate()
    require(not model.runtime_oper and not model.identity,
            "oper G: suspended profile granted oper")
    model.check("oper G")


def scenario_snapshots():
    model = BlockNModel({"pass": "p", "access": "a", "vhost": "a.test", "modes": ("S",), "oper": "locop"})
    model.authenticate()
    model.snapshot({"pass": "p", "access": "a", "vhost": "b.test", "modes": ("S",), "oper": "locop"})
    require(model.identity, "snapshot equivalent: identity lost")
    require(model.runtime_vhost == "b.test", "snapshot equivalent: effects not reconciled")
    require("S" in model.runtime_modes, "snapshot equivalent: owned mode churned")
    require(model.runtime_oper and model.runtime_operclass == "locop" and model.oper_owned,
            "snapshot equivalent: oper lost")
    model.check("snapshot equivalent")

    model.external_vhost("external.test")
    model.snapshot({"exists": False})
    require(not model.identity, "snapshot delete: identity kept")
    require(model.runtime_vhost == "external.test", "snapshot delete: external vhost removed")
    require("S" not in model.runtime_modes, "snapshot delete: owned mode kept")
    require(not model.runtime_oper, "snapshot delete: UDB oper kept")
    model.check("snapshot delete")

    model = BlockNModel({"pass": "p", "access": "a"})
    model.authenticate()
    model.snapshot({"pass": "p", "access": "b"})
    require(model.identity, "snapshot access change: identity revoked while still permitted")
    model.check("snapshot access change")

    model = BlockNModel({"pass": "p", "access": "a"})
    model.authenticate()
    model.snapshot({"pass": "q", "access": "b"})
    require(model.identity, "snapshot pass change: identity revoked")
    model.check("snapshot pass change")

    model = BlockNModel({"pass": "p", "access": "a"})
    model.authenticate()
    model.snapshot({"pass": "p", "access": "b"}, access_ok=False)
    require(not model.identity, "snapshot access denial: identity kept")
    model.check("snapshot access denial")

    model = BlockNModel({"pass": "p", "access": "a"})
    model.authenticate()
    model.snapshot({"pass": "p", "access": "a", "suspend": True})
    require(not model.identity, "snapshot suspend: identity kept")
    model.check("snapshot suspend")


def scenario_sequences():
    model = BlockNModel({"pass": "p", "access": "a", "vhost": "a.test"})
    model.authenticate()
    model.external_vhost("ext.test")
    model.suspend()
    require(model.runtime_vhost == "ext.test", "sequence one: external vhost lost on suspend")
    model.unsuspend()
    require(not model.identity, "sequence one: unsuspend restored identity")
    model.authenticate()
    require(model.runtime_vhost == "a.test" and model.identity, "sequence one: reauth failed")
    model.check("sequence one")

    model = BlockNModel({"pass": "p", "access": "a", "modes": ("S",)})
    model.external_mode("S")
    model.authenticate()
    model.effect_update(modes=("S", "D"))
    model.suspend()
    require("S" in model.runtime_modes and "D" not in model.runtime_modes,
            "sequence two: external and owned bits mishandled")
    model.check("sequence two")

    model = BlockNModel({"pass": "p", "access": "a", "vhost": "a.test"})
    model.authenticate()
    model.external_vhost("ext.test")
    model.snapshot({"exists": False})
    require(model.runtime_vhost == "ext.test" and not model.identity,
            "sequence three: delete snapshot mishandled external override")
    model.check("sequence three")

    model = BlockNModel({"pass": None, "access": "a"})
    model.external_mode("S")
    model.external_vhost("ext.test")
    model.leave_nick()
    require(model.runtime_modes == {"S"} and model.runtime_vhost == "ext.test",
            "sequence four: passless leave touched external state")
    model.check("sequence four")


def random_profile(rng):
    return {
        "pass": "p" if rng.random() < 0.7 else None,
        "access": "a",
        "vhost": rng.choice([None, "a.test", "b.test"]),
        "modes": tuple(rng.sample(["S", "D", "G"], rng.randint(0, 2))),
        "snomask": rng.choice([None, "c", "k", "+c", "-c", "+c-k", "+k-c", "+ck", "-c+k"]),
        "oper": rng.choice([None, "locop", "netadmin"]),
        "suspend": rng.random() < 0.15,
    }


def random_sequences():
    rng = random.Random(0xB10C)
    for trial in range(1500):
        model = BlockNModel(random_profile(rng))
        for _ in range(rng.randint(4, 24)):
            event = rng.randrange(18)
            if event == 0:
                model.authenticate()
            elif event == 1:
                model.authenticate(ok=False)
            elif event == 2:
                model.suspend()
            elif event == 3:
                model.unsuspend()
            elif event == 4:
                model.policy_change(pass_value=rng.choice(["p", "q"]))
            elif event == 5:
                model.policy_change(access=rng.choice(["a", "b"]), access_ok=rng.random() < 0.9)
            elif event == 6:
                model.delete_profile()
            elif event == 7:
                model.forced_rename()
            elif event == 8:
                model.leave_nick()
            elif event == 9:
                model.effect_update(
                    modes=rng.choice([None, (), ("S",), ("S", "D")]),
                    vhost=rng.choice([None, "a.test", "b.test"]),
                    snomask=rng.choice([None, "c", "k", "+c", "-c", "+c-k", "+k-c", "+ck", "-c+k"]),
                    oper=rng.choice([None, "locop", "netadmin"]))
            elif event == 10:
                model.external_vhost(rng.choice(["x.test", "y.test"]))
            elif event == 11:
                model.external_mode(rng.choice(["S", "D", "G"]))
            elif event == 12:
                model.external_unsmode(rng.choice(["S", "D", "G"]))
            elif event == 13:
                model.external_snomask(rng.choice(["c", "k", "ck"]))
            elif event == 14:
                model.external_oper(rng.choice(["locop", "netadmin"]))
            elif event == 15:
                model.external_deoper()
            elif event == 16:
                model.effect_update(oper=rng.choice([None, "locop", "netadmin"]))
            else:
                model.snapshot(random_profile(rng), access_ok=rng.random() < 0.9)
            model.check(f"random trial {trial}")
    print("PASS: 1500 randomized ownership sequences hold all invariants")


def main():
    scenario_mode_ownership()
    scenario_vhost_ownership()
    scenario_snomask_ownership()
    scenario_oper_ownership()
    scenario_snapshots()
    scenario_sequences()
    random_sequences()
    print("PASS: Block N ownership model scenarios and invariants")
    return 0


if __name__ == "__main__":
    sys.exit(main())
