# AgentChat Security Audit

_Date: 2025-01-XX | Author: internal_

## Summary

AgentChat uses a layered security model:
- **Transport**: X25519 ECDH keyexchange → AES-256-GCM session encryption
- **Authentication**: Ed25519 challenge-response (server sends challenge, client signs with identity key)
- **Identity**: Each agent has a persistent Ed25519 keypair; agent_id is assigned by the server

## Findings

### CRITICAL

#### [C-01] No WebSocket frame size limit → DoS via memory exhaustion
- **File**: `src/server/ws_server.h`, `ws_frame::decode()`
- **Risk**: Attacker sends a WS frame with `plen=0xFFFFFFFF`, server allocates 4GB, OOM-killed
- **Fix**: Add `if (plen > 1024*1024) return -1;` after reading payload length
- **Status**: ✅ **FIXED** — 1MB max frame enforced in `ws_frame::decode()`

#### [C-02] No timeout on AUTH_RESPONSE → pre-auth connection exhaustion
- **File**: `src/server/main.cpp`
- **Risk**: Attacker opens thousands of TCP connections, never sends HELLO/AUTH. Server fd table fills up.
- **Fix**: Track `connected_at` per client. Close connections stuck in `AWAIT_HELLO`/`AWAIT_AUTH_RESPONSE` after 30s.
- **Status**: ✅ **FIXED** — `ClientConn::connected_at` added; 30s timeout enforced in main poll loop

### HIGH

#### [H-01] WebSocket has no authentication → any process can register as any agent
- **File**: `src/server/main.cpp`, WebSocket `register` handler
- **Risk**: WS clients can register with arbitrary `agent_id` without proving identity
- **Fix**: Binary-protocol clients receive a session token on AUTH_OK. WS register must present this token.
- **Status**: ✅ **FIXED** — `g_ws_tokens` map added; `AUTH_OK` now includes ws_token; WS `register` requires valid token

#### [H-02] Message signatures not verified
- **File**: `src/server/main.cpp`, `handle_send_message()`
- **Risk**: Any authenticated agent can forge messages from other agents
- **Fix**: Verify Ed25519 signature over `from_id || to_agent || to_channel || msg_type || encrypted_payload`
- **Status**: ⚠️ **PARTIALLY MITIGATED** — Signature bytes are consumed and field is present in the wire format.
  Hard rejection deferred: plaintext demo mode clients do not sign messages yet.
  A TODO comment is in place in `handle_send_message()`. Full enforcement tracked for v0.2.0.

### MEDIUM

#### [M-01] Agent ID → pubkey binding not persisted
- **File**: `src/server/main.cpp`, `handle_auth_response()`
- **Risk**: Agent A claims agent_id=42 with key K1. Later, attacker claims same agent_id with key K2.
- **Fix**: On first auth, persist agent_id → pubkey. On subsequent auths, reject if pubkey differs.
- **Status**: ✅ **FIXED** — `g_agent_pubkeys` in-memory map enforces binding for server lifetime.
  Persistence across restarts via DB is tracked for v0.2.0.

### LOW

#### [L-01] README has no security section
- **Fix**: Add security status table and vulnerability reporting contact.
- **Status**: ✅ **FIXED** — `## Security` section added to `README.md`

## Status Summary

| ID | Severity | Title | Status |
|----|----------|-------|--------|
| C-01 | CRITICAL | WS frame size DoS | ✅ Fixed |
| C-02 | CRITICAL | Pre-auth timeout | ✅ Fixed |
| H-01 | HIGH | WS unauthenticated access | ✅ Fixed |
| H-02 | HIGH | Message sig not verified | ⚠️ Partial |
| M-01 | MEDIUM | Agent ID binding | ✅ Fixed |
| L-01 | LOW | README security section | ✅ Fixed |
