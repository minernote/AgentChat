#!/usr/bin/env python3
"""
AgentChat Python SDK — Integration Tests
Tests the SDK API surface without requiring a live server.
Runs against the installed agentchat package.

Usage:
    cd /Users/hacked/Documents/Projects/AgentChat
    AGENTCHAT_LIB=build/libagentchat_c_api.dylib python3 tests/test_python_sdk.py
"""
from __future__ import annotations

import os
import sys
import struct
import socket
import threading
import time
import traceback

# Ensure the bindings are importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'bindings', 'python'))

pass_count = 0
fail_count = 0


def run(name, fn):
    global pass_count, fail_count
    print(f'  {name} ... ', end='', flush=True)
    try:
        fn()
        print('PASS')
        pass_count += 1
    except Exception as e:
        print(f'FAIL: {e}')
        traceback.print_exc()
        fail_count += 1


# ── Keypair tests (no server needed) ─────────────────────────────────────────

def test_generate_keypair_returns_hex_strings():
    from agentchat import generate_keypair
    pub, priv = generate_keypair(ed25519=True)
    assert isinstance(pub, str), 'public key must be str'
    assert isinstance(priv, str), 'private key must be str'
    assert len(pub) > 0, 'public key must be non-empty'
    assert len(priv) > 0, 'private key must be non-empty'


def test_generate_keypair_hex_valid():
    from agentchat import generate_keypair
    pub, priv = generate_keypair(ed25519=True)
    # Must be valid hex
    int(pub, 16)
    int(priv, 16)


def test_generate_keypair_unique():
    from agentchat import generate_keypair
    pairs = [generate_keypair() for _ in range(5)]
    privkeys = [p[1] for p in pairs]
    assert len(set(privkeys)) == 5, 'each keypair must be unique'


def test_generate_keypair_x25519():
    from agentchat import generate_keypair
    pub, priv = generate_keypair(ed25519=False)
    assert len(pub) > 0
    assert len(priv) > 0


# ── Client construction tests (no server needed) ──────────────────────────────

def test_client_construction():
    from agentchat import AgentChatClient, generate_keypair
    _, id_priv = generate_keypair(ed25519=True)
    _, ex_priv = generate_keypair(ed25519=False)
    # Should not raise
    client = AgentChatClient('127.0.0.1', 19999, 42,
                             identity_priv_hex=id_priv, exchange_priv_hex=ex_priv)
    assert client is not None
    del client


def test_client_construction_empty_keys():
    from agentchat import AgentChatClient
    # Empty keys should be accepted (server will generate)
    client = AgentChatClient('127.0.0.1', 19999, 1)
    assert client is not None
    del client


def test_client_connect_refused():
    from agentchat import AgentChatClient
    client = AgentChatClient('127.0.0.1', 19998, 99)
    result = client.connect()  # Should fail gracefully (no server)
    assert result == False or result == 0, f'expected False, got {result!r}'
    del client


def test_client_on_message_registration():
    from agentchat import AgentChatClient
    client = AgentChatClient('127.0.0.1', 19999, 1)
    received = []
    client.on_message(lambda from_id, data: received.append((from_id, data)))
    # No error registering callback
    del client


# ── Loopback integration test (requires working server binary) ────────────────

def _find_server_binary():
    here = os.path.dirname(__file__)
    candidates = [
        os.path.join(here, '..', 'build', 'agentchat_server'),
    ]
    for p in candidates:
        p = os.path.normpath(p)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def test_two_clients_exchange_message():
    """Start a real server, connect two clients, send a message, verify receipt."""
    import subprocess
    from agentchat import AgentChatClient, generate_keypair

    server_bin = _find_server_binary()
    if not server_bin:
        print('(skipped — server binary not found)', end=' ')
        return

    # Pick a free port
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]

    # Pick a free ws port too
    with socket.socket() as s2:
        s2.bind(('127.0.0.1', 0))
        ws_port = s2.getsockname()[1]

    # Start server
    proc = subprocess.Popen(
        [server_bin, '--port', str(port), '--ws-port', str(ws_port), '--db', f'/tmp/ac_test_{port}.db'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    time.sleep(0.4)  # let server start

    try:
        _, priv_a = generate_keypair()
        _, priv_b = generate_keypair()

        received = []
        evt = threading.Event()

        client_b = AgentChatClient('127.0.0.1', port, 2,
                                   identity_priv_hex=priv_b,
                                   exchange_priv_hex=priv_b)
        client_b.on_message(lambda fid, data: (received.append(data), evt.set()))
        assert client_b.connect(), 'client_b connect failed'

        client_a = AgentChatClient('127.0.0.1', port, 1,
                                   identity_priv_hex=priv_a,
                                   exchange_priv_hex=priv_a)
        assert client_a.connect(), 'client_a connect failed'

        time.sleep(0.1)
        ok = client_a.send_text(2, 'hello from A')
        assert ok, 'send_text failed'

        arrived = evt.wait(timeout=3.0)
        assert arrived, 'message never arrived at client_b'
        assert b'hello from A' in received[0], f'unexpected payload: {received[0]!r}'

        client_a.disconnect()
        client_b.disconnect()
    finally:
        proc.terminate()
        proc.wait(timeout=3)


# ── Main ──────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    print('AgentChat Python SDK Tests')
    print('=' * 40)

    run('generate_keypair_returns_hex_strings', test_generate_keypair_returns_hex_strings)
    run('generate_keypair_hex_valid',           test_generate_keypair_hex_valid)
    run('generate_keypair_unique',              test_generate_keypair_unique)
    run('generate_keypair_x25519',              test_generate_keypair_x25519)
    run('client_construction',                  test_client_construction)
    run('client_construction_empty_keys',       test_client_construction_empty_keys)
    run('client_connect_refused',               test_client_connect_refused)
    run('client_on_message_registration',       test_client_on_message_registration)
    run('two_clients_exchange_message',         test_two_clients_exchange_message)

    print('=' * 40)
    print(f'Results: {pass_count} passed, {fail_count} failed')
    sys.exit(0 if fail_count == 0 else 1)
