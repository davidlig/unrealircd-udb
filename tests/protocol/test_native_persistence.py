"""Exclusive native HEL 4 contracts on disposable real daemon nodes."""
import time
import socket
import pytest
from udb_test_support.peer import Peer
from udb_test_support.native_reference import logical_digest, wire_value
from udb_test_support.runtime import Wire

pytestmark = pytest.mark.protocol


def server_query(peer, query):
    start = len(peer.lines)
    peer.send(f'DBQ {query}')
    peer.barrier()
    return peer.lines[start:]


def observe_ready_inventory(node, *, name, sid, source_sid, source_epoch):
    # An authorized downstream receives INF only after durable promotion,
    # ordered effects and replay complete. Observe that real transition before
    # attempting NICK; a fixed sleep cannot establish admission readiness.
    with Peer(node, name=name, sid=sid, negotiate=False) as observer:
        observer.negotiate(policy=node.name)
        first = observer.wait(lambda line: ' INF ' in line, 'READY-only downstream inventory')
        round_id = first.split()[4]
        for letter in 'NCISLK':
            line = observer.wait(lambda line: f' INF {round_id} {letter} ' in line,
                                 'complete READY inventory')
            fields = line.split()
            assert fields[10:] == [source_sid, source_epoch], line
    # Closing this non-authority peer must not restart upstream reconciliation.


@pytest.mark.parametrize('cap,user_first', [(False, False), (True, False), (True, True)])
def test_bootstrap_initial_nick_reports_temporary_unavailability(node_factory, cap, user_first):
    node = node_factory(ready=False, expected_state=None,
                        peers=['peer.test', 'sink.test'], propagator='peer.test')
    with Peer(node) as peer:
        assert any('Database readiness: BOOTSTRAPPING' in line for line in server_query(peer, 'STATUS'))
        with socket.create_connection(('127.0.0.1', node.client_port), timeout=5) as sock:
            wire = Wire(sock)
            if cap:
                wire.send('CAP LS 302')
                wire.wait(lambda line: ' CAP ' in line and ' LS ' in line and ' LS * ' not in line,
                          'complete CAP inventory')
                wire.send('CAP REQ :multi-prefix')
                wire.wait(lambda line: ' CAP ' in line and ' ACK ' in line, 'CAP acknowledgement')
            if user_first:
                wire.send('USER blocked 0 * :bootstrap rejection')
            wire.send('NICK blocked')
            error = wire.wait(lambda line: line.startswith('ERROR :'), 'explicit bootstrap rejection', timeout=3)
            assert 'UDB synchronization unavailable' in error, error
            assert 'temporarily not accepting new connections' in error, error
            assert not any(' 001 ' in line or 'Registration Timeout' in line for line in wire.lines)
            assert sock.recv(1) == b''
        assert 'UDB_NOT_READY_DENY_CLIENT' in node.log_text()
        assert any('Database readiness: BOOTSTRAPPING' in line for line in server_query(peer, 'STATUS'))
        # The rejection does not change authority or bypass six-block bootstrap.
        peer.synchronize()
        observe_ready_inventory(node, name='sink.test', sid='0B1',
                                source_sid=peer.sid, source_epoch=peer.epoch)
        client = node.client('afterbootstrap')
        assert any(' 001 ' in line for line in client.lines), client.lines


def test_native_sender_waits_for_valid_durable_ack_between_blocks(node_factory):
    node = node_factory(ready=False, expected_state=None,
                        peers=['peer.test', 'sink.test'], propagator='peer.test')
    blocks = {'N': {f'nick{i}': None for i in range(2000)},
              'C': {f'#channel{i}': None for i in range(3000)}}
    with Peer(node) as root:
        seed_round = root.inventory(blocks)
        # Seed conservatively rather than using the fixture's unlimited burst.
        # This setup is not the pacing under test: all outbound rows below are
        # streamed by the real production sender, with its own bounded turns.
        for letter in 'NCISLK':
            if not any(f' RES {seed_round} {letter}' in line for line in root.lines):
                continue
            records = blocks.get(letter, {})
            digest = logical_digest(records, letter)
            txid = f'seed-{letter}'
            root.send(f'DB {node.sid} BEGIN {seed_round} {letter} {txid} {digest} 0')
            for index, (path, value) in enumerate(records.items(), 1):
                root.send(f'DB {node.sid} PUT {seed_round} {letter} {txid} {letter}::{path} :{wire_value(value)}')
                if index % 16 == 0:
                    time.sleep(0.02)
            root.send(f'DB {node.sid} END {seed_round} {letter} {txid} {digest} 0')
            root.wait(lambda line: f' ACK {seed_round} {letter} {txid} ' in line,
                      'paced seed durable ACK', timeout=15)
        time.sleep(0.3)
        assert any('Database readiness: READY' in line for line in server_query(root, 'STATUS'))
        with Peer(node, name='sink.test', sid='0B1', negotiate=False) as sink:
            sink.negotiate(policy=node.name)
            offer = sink.wait(lambda line: ' INF ' in line and line.split()[5] == 'N', 'native sender inventory')
            round_id = offer.split()[4]
            start = len(sink.lines)
            # Request all six blocks up front, as a real receiver does. The
            # production sender must not spend the next writer credit at END.
            for letter in 'NCISLK':
                sink.send(f'DB {node.sid} RES {round_id} {letter}')
            for letter in 'NCISLK':
                end = sink.wait(lambda line: f' END {round_id} {letter} ' in line,
                                'native sender END', start=start, timeout=15)
                rows = [line for line in sink.lines[start:] if f' PUT {round_id} {letter} ' in line]
                assert len(rows) == len(blocks.get(letter, {}))
                if letter in blocks:
                    assert end.split()[7] == logical_digest(blocks[letter], letter)
                if letter == 'N':
                    fields = end.split()[4:]
                    invalid = [fields[:2] + ['wrong-txid'] + fields[3:],
                               fields[:3] + ['0' * 64] + fields[4:],
                               fields[:-1] + [str(int(fields[-1]) + 1)],
                               [str(int(fields[0]) + 1)] + fields[1:]]
                    for ack in invalid:
                        sink.send(f'DB {node.sid} ACK ' + ' '.join(ack))
                    # Give multiple bounded sender turns time to run, then
                    # drain their output through an IRC command barrier.
                    time.sleep(0.1)
                    sink.barrier()
                    assert not any(f' BEGIN {round_id} C ' in line for line in sink.lines[start:])
                ack = f'DB {node.sid} ACK ' + ' '.join(end.split()[4:])
                sink.send(ack)
                if letter != 'K':
                    # Duplicate credit cannot acknowledge the next block.
                    sink.send(ack)
            sink.barrier()
            begins = [line.split()[5] for line in sink.lines[start:] if f' BEGIN {round_id} ' in line]
            assert begins == list('NCISLK')


def test_snapshot_password_addition_renames_online_guest_without_disconnect(node_factory):
    import hashlib
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'], propagator='peer.test')
    with Peer(node) as peer:
        peer.synchronize({'N': {'bob': None}})
        time.sleep(0.3)
        assert any('Database readiness: READY' in line for line in server_query(peer, 'STATUS'))
        client = node.client('bob')
        start = len(client.lines)
        blocks = {'N': {'bob': None, 'bob::pass': 'sha256:' + hashlib.sha256(b'password').hexdigest(),
                        'bob::vhost': 'protected.test'},
                  'C': {'#pending': None, '#pending::topic': 'committed topic'}}
        round_id = peer.inventory(blocks)
        peer.transfer_round(round_id, {'N': blocks['N']}, only=True)
        assert any('Database readiness: BOOTSTRAPPING' in line for line in server_query(peer, 'STATUS'))
        assert not any(' NICK :Guest' in line for line in client.request('PING :still-bob'))
        peer.transfer_round(round_id, {'C': blocks['C']}, only=True)
        renamed = client.wait(lambda line: ' NICK :Guest' in line, 'publication Guest rename', start=start)
        guest = renamed.rsplit(':', 1)[1]
        replies = client.request(f'WHOIS {guest}')
        assert any(' 311 ' in line for line in replies), replies
        assert not any(' 307 ' in line or 'protected.test' in line for line in replies), replies
        assert not any('UDB nickname policy data unavailable' in line or 'ERROR :' in line
                       for line in client.lines[start:])
        time.sleep(0.3)
        assert any('Database readiness: READY' in line for line in server_query(peer, 'STATUS'))
        replies = client.request('NICK bob:password')
        assert any('You are now identified for nickname bob' in line for line in replies), replies
        assert any(' 307 ' in line for line in client.request('WHOIS bob'))


def test_native_fresh_files_and_local_readiness(node_factory):
    node = node_factory(settings='cache-size 268435456;')
    files = node.data_dir / 'udb'
    assert {f.name for f in files.glob('udb_*.db')} == {
        'udb_N.db', 'udb_C.db', 'udb_I.db', 'udb_S.db', 'udb_L.db', 'udb_K.db',
        'udb_wal.db', 'udb_meta0.db', 'udb_meta1.db'}
    assert not (node.data_dir / '.udb_state').exists()
    # node_factory has already verified READY through UDB STATUS.
    # Close the node before inspecting files held exclusively on Windows.
    node.close()
    with (files / 'udb_N.db').open('rb') as stream:
        assert stream.read(6) == b'UDBPG4'


def test_six_root_import_origin_and_durable_live_mutation(node_factory):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'], propagator='peer.test')
    origin = '2222222222222222'  # Deliberately different from relay HEL epoch.
    records = {'alice': None, 'alice::vhost': 'first.test'}
    with Peer(node) as peer:
        peer.synchronize({'N': records, 'I': {'127.0.0.2': None, '127.0.0.2::clones': 0}}, source_epoch=origin)
        # A socket PING is not a writer durability barrier. Wait once for the
        # completion event and assert the actual runtime state/data afterwards.
        time.sleep(0.3)
        replies = server_query(peer, 'STATUS')
        assert any('Database readiness: READY' in line for line in replies), replies
        replies = server_query(peer, 'I::127.0.0.2::clones')
        assert any('DBQ I::127.0.0.2::clones 0' in line for line in replies), replies
        peer.mutation(1, 'N::alice::vhost', 'second.test', epoch=origin)
        time.sleep(0.2)
        replies = server_query(peer, 'N::alice::vhost')
        assert any('DBQ N::alice::vhost second.test' in line for line in replies), replies
        # Old untagged values and a neighbor HEL epoch cannot replace the root.
        peer.send(f'DB * INS {peer.epoch} 2 N::alice::vhost :untagged')
        peer.barrier()
        assert not any('DBQ N::alice::vhost untagged' in line for line in server_query(peer, 'N::alice::vhost'))
    node.close()
    restarted = node_factory(path=node.path, ready=False, expected_state=None, peers=['peer.test'], propagator='peer.test')
    client = restarted.client('inspect')
    client.request('OPER testoper operpass')
    replies = client.request('DBQ N::alice::vhost')
    assert any('DBQ N::alice::vhost second.test' in line for line in replies), replies


def test_old_inf_without_origin_cannot_start_snapshot(node_factory):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'], propagator='peer.test')
    with Peer(node) as peer:
        start = len(peer.lines)
        peer.send(f'DB {node.sid} INF 8 N {logical_digest({}, "N")} 0 0 0')
        peer.barrier()
        assert not any(' RES 8 N' in line for line in peer.lines[start:])
        assert any('Database readiness: BOOTSTRAPPING' in line for line in server_query(peer, 'STATUS'))


def test_nonempty_six_blocks_publish_native_effects_together(node_factory):
    import hashlib
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'], propagator='peer.test')
    blocks = {
        'N': {'bob': None, 'bob::pass': 'sha256:' + hashlib.sha256(b'password').hexdigest(), 'bob::vhost': 'nick.test'},
        'C': {'#native': None, '#native::topic': 'native topic', '#native::options': 8},
        'I': {'127.0.0.1': None, '127.0.0.1::clones': 20, '127.0.0.1::host': 'policy.test'},
        'S': {'suffix': '.native.test'},
        'L': {node.name: None, node.name + '::options': 0},
        'K': {'G::*@banned.test::reason': 'native ban'},
    }
    with Peer(node) as peer:
        peer.synchronize(blocks)
        time.sleep(0.3)
        assert any('Database readiness: READY' in line for line in server_query(peer, 'STATUS'))
        for letter, records in blocks.items():
            replies = server_query(peer, letter)
            assert any(logical_digest(records, letter) in line for line in replies), replies
        client = node.client('observer')
        client.request('OPER testoper operpass')
        assert any('native topic' in line for line in client.request('TOPIC #native'))
        assert any('banned.test' in line for line in client.request('STATS G'))
        replies = client.request('NICK bob:password')
        assert any('You are now identified for nickname bob' in line for line in replies), replies
        replies = client.request('WHOIS bob')
        assert any(' 311 ' in line and 'nick.test' in line for line in replies), replies
        # Point-scoped native publication updates owned effects from its receipt.
        start = len(client.lines)
        peer.mutation(1, 'N::bob::vhost', 'updated.test')
        client.wait(lambda line: 'updated.test' in line, 'durable vhost effect', start=start, timeout=5)
        assert any('updated.test' in line for line in server_query(peer, 'N::bob::vhost'))
        replies = client.request('WHOIS bob')
        assert any(' 311 ' in line and 'updated.test' in line for line in replies), replies
        start = len(client.lines)
        peer.mutation(2, 'N::bob::vhost', delete=True)
        # PONG confirms frame processing, not durable publication of its effects.
        client.wait(lambda line: 'Your base cloak has been restored' in line,
                    'durable vhost revocation', start=start, timeout=5)
        replies = client.request('WHOIS bob')
        assert any(' 311 ' in line and 'Clk-' in line for line in replies), replies
        assert any(' 307 ' in line for line in replies), replies


def test_private_partial_round_and_bad_checksum_never_publish(node_factory):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'], propagator='peer.test')
    blocks = {'N': {'alice': None, 'alice::vhost': 'healthy.test'},
              'C': {'#native': None, '#native::topic': 'healthy topic'}}
    with Peer(node) as peer:
        round_id = peer.inventory(blocks)
        # Seal N durably while C is still missing; ACK is not six-root promotion.
        peer.transfer_round(round_id, {'N': blocks['N']}, only=True)
        replies = server_query(peer, 'N::alice::vhost')
        assert not any('healthy.test' in line for line in replies), replies
        assert any('BOOTSTRAPPING' in line for line in server_query(peer, 'STATUS'))
        digest = logical_digest(blocks['C'], 'C')
        peer.send(f'DB {node.sid} BEGIN {round_id} C bad-C {digest} 0')
        peer.send(f'DB {node.sid} PUT {round_id} C bad-C C::#native :!n')
        peer.send(f'DB {node.sid} PUT {round_id} C bad-C C::#native::topic :{wire_value("tampered topic")}')
        peer.send(f'DB {node.sid} END {round_id} C bad-C {digest} 0')
        peer.barrier()
        time.sleep(0.3)
        assert not any('healthy.test' in line for line in server_query(peer, 'N::alice::vhost'))
        assert any('BOOTSTRAPPING' in line for line in server_query(peer, 'STATUS'))
        assert 'UDB_SYNC_ABORT' in node.log_text()
        # A fresh correct round can replace the rejected candidate.
        peer.synchronize(blocks)
        time.sleep(0.3)
        assert any('READY' in line for line in server_query(peer, 'STATUS'))
        assert any('healthy.test' in line for line in server_query(peer, 'N::alice::vhost'))


def test_import_replays_post_watermark_mutations_before_readiness(node_factory):
    node = node_factory(ready=False, expected_state=None, peers=['peer.test'], propagator='peer.test')
    blocks = {'N': {'alice': None, 'alice::vhost': 'snapshot.test'}}
    origin = '2222222222222222'
    with Peer(node) as peer:
        round_id = peer.inventory(blocks, watermark=10, source_epoch=origin)
        peer.mutation(11, 'N::alice::vhost', 'replayed.test', epoch=origin)
        assert not any('replayed.test' in line for line in server_query(peer, 'N::alice::vhost'))
        peer.transfer_round(round_id, blocks, watermark=10)
        time.sleep(0.8)
        status = server_query(peer, 'STATUS')
        assert any('Database readiness: READY' in line for line in status), status
        assert any('replayed.test' in line for line in server_query(peer, 'N::alice::vhost'))
        peer.mutation(12, 'N::alice::vhost', 'next.test', epoch=origin)
        time.sleep(0.3)
        assert any('next.test' in line for line in server_query(peer, 'N::alice::vhost'))


def test_native_three_node_hop_snapshots_and_root_lineage(node_factory):
    a = node_factory(name='a.test', sid='0A1', ready=False, expected_state=None,
                     peers=['peer.test', 'b.test'], propagator='peer.test')
    with Peer(a) as root:
        origin = '2222222222222222'
        root.synchronize({'N': {'alice': None, 'alice::vhost': 'root.test'}}, source_epoch=origin)
        time.sleep(0.3)
        b = node_factory(name='b.test', sid='0B1', ready=False, expected_state=None,
                         peers=['a.test', 'c.test', 'observe-b.test'], propagator='a.test',
                         outgoing={'a.test': a.server_port})
        observe_ready_inventory(b, name='observe-b.test', sid='0D1', source_sid=root.sid, source_epoch=origin)
        cb = b.client('inspectb')
        cb.request('OPER testoper operpass')
        status = cb.request('DBQ STATUS')
        assert any('Database readiness: READY' in line for line in status), (status, b.log_text()[-1800:])
        assert any('root.test' in line for line in cb.request('DBQ N::alice::vhost'))
        upstream_rounds = b.log_text().count('Started reconciliation round')
        root.barrier()
        root_start = len(root.lines)
        c = node_factory(name='c.test', sid='0C1', ready=False, expected_state=None,
                         peers=['b.test', 'observe-c.test'], propagator='b.test',
                         outgoing={'b.test': b.server_port})
        observe_ready_inventory(c, name='observe-c.test', sid='0E1', source_sid=root.sid, source_epoch=origin)
        cc = c.client('inspectc')
        cc.request('OPER testoper operpass')
        status = cc.request('DBQ STATUS')
        assert any('Database readiness: READY' in line for line in status), (status, c.log_text()[-1800:])
        assert any('root.test' in line for line in cc.request('DBQ N::alice::vhost'))
        assert b.log_text().count('Started reconciliation round') == upstream_rounds
        root.barrier()
        assert not any(' HEL 4 ' in line for line in root.lines[root_start:])
        # C's snapshot source is B, but its durable origin remains the root peer.
        root.mutation(1, 'N::alice::vhost', 'multihop.test', epoch=origin)
        time.sleep(0.8)
        for client in (cb, cc):
            replies = client.request('DBQ N::alice::vhost')
            assert any('multihop.test' in line for line in replies), replies
        # A root sequence replay cannot overwrite any downstream data.
        root.mutation(1, 'N::alice::vhost', 'replayed-wrong.test', epoch=origin)
        time.sleep(0.3)
        assert any('multihop.test' in line for line in cc.request('DBQ N::alice::vhost'))


def test_local_native_rehash_reopens_store_and_resizes_cache(node_factory):
    node = node_factory(settings='cache-size 134217728;')
    client = node.client('rehashcheck')
    client.request('OPER testoper operpass')
    config = node.config
    text = config.read_text()
    assert 'cache-size 134217728;' in text
    config.write_text(text.replace('cache-size 134217728;', 'cache-size 268435456;'))
    client.request('REHASH')
    time.sleep(0.6)
    replies = client.request('UDB STATUS')
    assert any('Database readiness: READY' in line for line in replies), (replies, node.log_text()[-1500:])
    assert not any('error loading' in line.lower() for line in node.log_text().splitlines())
    assert len(list((node.data_dir / 'udb').glob('udb_*.db'))) == 9


def test_native_ordered_authority_failover_and_failback(node_factory):
    node = node_factory(ready=False, expected_state=None, peers=['primary.test', 'backup.test'],
                        propagator='primary.test')
    primary = Peer(node, name='primary.test', sid='0P1')
    backup = None
    try:
        policy = {'propagator': 'primary.test,backup.test'}
        primary.synchronize({'N': {'alice': None, 'alice::vhost': 'primary.test'}, 'S': policy})
        time.sleep(0.3)
        client = node.client('inspect')
        client.request('OPER testoper operpass')
        # Remove only the local override so the durable ordered S policy applies.
        config = node.config.read_text()
        assert 'propagator "primary.test";' in config
        node.config.write_text(config.replace('propagator "primary.test";', ''))
        client.request('REHASH')
        primary.negotiate()
        primary.synchronize({'N': {'alice': None, 'alice::vhost': 'primary.test'}, 'S': policy})
        time.sleep(0.3)
        backup = Peer(node, name='backup.test', sid='0P2', epoch='3333333333333333')
        primary.close()
        time.sleep(0.3)
        backup.synchronize({'N': {'alice': None, 'alice::vhost': 'backup.test'}, 'S': policy}, watermark=5)
        time.sleep(0.3)
        replies = server_query(backup, 'STATUS')
        assert any('Database readiness: READY' in line for line in replies), replies
        assert any('backup.test' in line for line in server_query(backup, 'N::alice::vhost'))
        primary = Peer(node, name='primary.test', sid='0P1', epoch='4444444444444444')
        primary.synchronize({'N': {'alice': None, 'alice::vhost': 'failback.test'}, 'S': policy}, watermark=2)
        time.sleep(0.3)
        replies = server_query(primary, 'STATUS')
        assert any('Database readiness: READY' in line for line in replies), replies
        assert any('failback.test' in line for line in server_query(primary, 'N::alice::vhost'))
    finally:
        primary.close()
        if backup is not None:
            backup.close()
