"""Country bans traverse durable UDB publication and the daemon native matcher."""
import time
import pytest
from udb_test_support.peer import Peer

pytestmark = pytest.mark.protocol

@pytest.mark.parametrize("country", ["es", "ES"])
def test_country_gline_native_matcher(node_factory, country):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'],
                        propagator='peer.test', runtime_state_fixture=True)
    with Peer(node) as peer:
        peer.synchronize({'K': {f'G::~country%3A@{country}::reason': 'country regression'}})
        node.wait_for_state('READY')
        target = node.client('countryprobe')
        def check(code, expected, count=1, host=""):
            start = len(peer.lines)
            peer.send(f'UDBTEST MATCHCOUNTRY countryprobe {code} {host}')
            line = peer.wait(lambda line: 'UDBTEST MATCH=' in line,
                             'native country matcher', start=start)
            assert f'MATCH={expected} COUNT={count}' in line, ('\n'.join(peer.lines[-20:]))
        check('ES', 1)
        check('FR', 0)
        # Reconcile a durable replacement without accumulating TKLs.
        path = f'K::G::~country%3A@{country}'
        peer.mutation(1, path + '::reason', 'replacement reason')
        time.sleep(1.2)
        check('ES', 1)
        peer.mutation(2, path, delete=True)
        time.sleep(1.2)
        check('ES', 0, 0)
        peer.mutation(3, path + '::reason', 'restored reason')
        time.sleep(1.2)
        check('ES', 1)
        # An already-due expiry removes the normalized effect. The authority
        # receives the stored-key EXP request; it can then delete that owner.
        start = len(peer.lines)
        peer.mutation(4, path + '::expires', int(time.time()) - 1)
        peer.wait(lambda line: ' EXP ' in line and '~country%3A@es' in line,
                  'canonical country expiry request', start=start, timeout=10)
        check('ES', 0, 0)
        peer.mutation(5, path, delete=True)
        time.sleep(1.2)
        peer.mutation(6, 'K::G::*@unmatched.invalid::reason', 'ordinary regression')
        time.sleep(1.2)
        check('ES', 0, 1)
        check('ES', 1, 1, host='unmatched.invalid')
        replies_start = len(peer.lines)
        peer.send('DBQ K')
        peer.barrier()
        assert not any('~country' in line for line in peer.lines[replies_start:])


def test_country_gline_rebuild_from_persisted_generation(node_factory):
    options = dict(ready=False, expected_state=None, peers=['peer.test', 'sink.test'],
                   propagator='peer.test', runtime_state_fixture=True)
    node = node_factory(**options)
    blocks = {'K': {'G::~country%3A@es::reason': 'restart regression'}}
    with Peer(node) as peer:
        peer.synchronize(blocks)
        node.wait_for_state('READY')
    node.close()
    restarted = node_factory(path=node.path, **options)
    with Peer(restarted) as peer:
        # Reusing the same durable generation must reconstruct only one effect.
        peer.synchronize(blocks)
        # A downstream inventory is emitted only after durable convergence;
        # a socket PING alone is not a publication barrier.
        with Peer(restarted, name='sink.test', sid='0S1', negotiate=False) as sink:
            sink.negotiate(policy=restarted.name)
            sink.wait(lambda line: ' INF ' in line, 'rebuilt READY inventory')
        restarted.client('countryprobe')
        start = len(peer.lines)
        peer.send('UDBTEST MATCHCOUNTRY countryprobe ES')
        line = peer.wait(lambda line: 'UDBTEST MATCH=' in line,
                         'rebuilt native matcher', start=start)
        assert 'MATCH=1 COUNT=1' in line, line


def query(peer, path):
    start = len(peer.lines)
    peer.send(f'DBQ {path}')
    peer.barrier()
    return '\n'.join(line for line in peer.lines[start:] if ' 339 ' in line or ' DBQ ' in line)


def native_match(peer, *, info='probe', account='0'):
    start = len(peer.lines)
    peer.send(f'UDBTEST MATCHCOUNTRY countryprobe ES localhost {info} {account}')
    return peer.wait(lambda line: 'UDBTEST MATCH=' in line, 'native matcher', start=start)


def test_invalid_country_rejected_before_commit(node_factory):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'],
                        propagator='peer.test', runtime_state_fixture=True)
    with Peer(node) as peer:
        peer.synchronize({'N': {'countryprobe': None}})
        node.wait_for_state('READY')
        before = query(peer, 'K')
        peer.mutation(1, 'K::G::~country%3A@invalid::reason', 'invalid country')
        time.sleep(1.2)
        assert query(peer, 'K') == before
        assert 'Database readiness: READY' in query(peer, 'STATUS')


@pytest.mark.parametrize("expired", [False, True])
def test_account_alias_survives_other_owner_deletion(node_factory, expired):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'],
                        propagator='peer.test', runtime_state_fixture=True)
    with Peer(node) as peer:
        peer.synchronize({'K': {'G::~a%3A@foo::reason': 'short alias',
                                'G::~account%3A@foo::reason': 'long alias'}})
        node.wait_for_state('READY')
        node.client('countryprobe')
        assert 'MATCH=1 COUNT=1' in native_match(peer, account='foo')
        if expired:
            start = len(peer.lines)
            peer.mutation(1, 'K::G::~a%3A@foo::expires', int(time.time()) - 1)
            peer.wait(lambda line: ' EXP ' in line and '~a%3A@foo' in line,
                      'alias expiry request', start=start, timeout=10)
        else:
            peer.mutation(1, 'K::G::~a%3A@foo', delete=True)
        time.sleep(1.2)
        assert 'MATCH=1 COUNT=1' in native_match(peer, account='foo')


def test_realname_converter_must_not_shorten_target(node_factory):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'],
                        propagator='peer.test', runtime_state_fixture=True)
    with Peer(node) as peer:
        peer.synchronize({'N': {'countryprobe': None}})
        node.wait_for_state('READY')
        node.client('countryprobe')
        peer.mutation(1, 'K::G::~realname%3A@' + 'a' * 51 + '::reason', 'long realname')
        time.sleep(1.2)
        assert 'MATCH=0' in native_match(peer, info='a' * 50)
        assert 'long realname' not in query(peer, 'K')
        assert 'Database readiness: READY' in query(peer, 'STATUS')


@pytest.mark.parametrize("mask", ['~country%3A@invalid', '~realname%3A@' + 'a' * 51])
def test_invalid_extended_snapshot_never_published(node_factory, mask):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'],
                        propagator='peer.test', runtime_state_fixture=True)
    with Peer(node) as peer:
        peer.synchronize({'N': {'countryprobe': None}})
        node.wait_for_state('READY')
        before = query(peer, 'K')
        from udb_test_support.native_reference import logical_digest, wire_value
        rows = {f'G::{mask}::reason': 'invalid snapshot'}
        blocks = {'N': {'countryprobe': None}, 'K': rows}
        round_id = peer.inventory(blocks)
        digest = logical_digest(rows, 'K')
        txid = f'invalid-{round_id}'
        peer.send(f'DB {node.sid} BEGIN {round_id} K {txid} {digest} 0')
        start = len(peer.lines)
        peer.send(f'DB {node.sid} PUT {round_id} K {txid} K::G::{mask}::reason :{wire_value("invalid snapshot")}')
        peer.wait(lambda line: ' ERR PUT ' in line, 'explicit pre-write rejection', start=start)
        assert not any(f' ACK {round_id} K ' in line for line in peer.lines[start:])
        assert 'invalid snapshot' not in query(peer, 'K')


@pytest.mark.parametrize("extended", [True, False])
def test_thousand_lines_keep_daemon_responsive(node_factory, extended, record_property):
    options = dict(ready=False, expected_state=None, peers=['peer.test', 'sink.test'],
                   propagator='peer.test', runtime_state_fixture=True)
    node = node_factory(**options)
    rows = {(f'G::~account%3A@acct{i}::reason' if extended else
             f'G::*@host{i}.invalid::reason'): 'scale regression' for i in range(1000)}
    if extended:
        rows['G::~a%3A@acct500::reason'] = 'shared owner'
    with Peer(node) as peer:
        started = time.monotonic()
        peer.synchronize({'K': rows})
        # The normal IRC welcome timeout is the responsiveness contract here.
        node.wait_for_state('READY')
        node.client('countryprobe')
        record_property('snapshot_to_ready_seconds', time.monotonic() - started)
        expected = 1 if extended else 0
        assert f'MATCH={expected} COUNT=1000' in native_match(peer, account='acct500')
        if extended:
            peer.mutation(1, 'K::G::~account%3A@acct500', delete=True)
            time.sleep(1.2)
            assert 'MATCH=1 COUNT=1000' in native_match(peer, account='acct500')
            peer.mutation(2, 'K::G::~a%3A@acct500::expires', int(time.time()) - 1)
            start = len(peer.lines)
            peer.wait(lambda line: ' EXP ' in line and '~a%3A@acct500' in line,
                      'large-set alias expiry', start=start, timeout=10)
            assert 'MATCH=0 COUNT=999' in native_match(peer, account='acct500')
    node.close()
    started = time.monotonic()
    restarted = node_factory(path=node.path, **options)
    # Exercise reconstruction directly from disk before any new snapshot.
    restarted.wait_for_state('READY')
    record_property('disk_reconstruction_to_ready_seconds', time.monotonic() - started)
    with Peer(restarted) as peer:
        peer.synchronize({'K': rows})
        with Peer(restarted, name='sink.test', sid='0S1', negotiate=False) as sink:
            sink.negotiate(policy=restarted.name)
            sink.wait(lambda line: ' INF ' in line, 'large-set READY inventory')
        restarted.client('countryprobe')
        record_property('restart_to_ready_seconds', time.monotonic() - started)
        assert f'MATCH={expected} COUNT=1000' in native_match(peer, account='acct500')


def test_indexed_aliases_preserve_parameter_containing_at(node_factory):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'],
                        propagator='peer.test', runtime_state_fixture=True)
    with Peer(node) as peer:
        # Here @ is part of the native parameter, not the UDB separator.
        peer.synchronize({'K': {'G::~r%3Aa@b::reason': 'short realname alias',
                                'G::~realname%3Aa@b::reason': 'long realname alias'}})
        node.wait_for_state('READY')
        node.client('countryprobe')
        assert 'MATCH=1 COUNT=1' in native_match(peer, info='a@b')
        peer.mutation(1, 'K::G::~realname%3Aa@b', delete=True)
        time.sleep(1.2)
        assert 'MATCH=1 COUNT=1' in native_match(peer, info='a@b')
