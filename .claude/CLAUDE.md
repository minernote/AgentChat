# AgentChat — AI Coding Rules

## Project Overview
AgentChat is an encrypted, AI-native messaging platform written in C++17.
Core library + C ABI + Python/Node.js SDKs + React Web UI.

## Architecture
- `src/core/` — crypto, protocol, network, storage (pure C++, no external deps except OpenSSL + SQLite)
- `src/server/` — AgentChat server (TCP + WebSocket)
- `src/agent/` — AgentClient SDK (C++ client library)
- `include/agentchat/` — public C API (stable ABI)
- `bindings/python/` — Python SDK
- `bindings/nodejs/` — Node.js/TypeScript SDK
- `web/` — React PWA (Vite + TypeScript)
- `tests/` — unit + integration tests
- `docs/` — architecture, API reference, roadmap

## Build System
```bash
# C++ core + server
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure

# Web UI
cd web && npm install && npm run build

# Python SDK
cd bindings/python && python3 -m build

# Node.js SDK
cd bindings/nodejs && npm install && npm run build
```

## Code Style
- C++17, clang-format (Google style base)
- No exceptions in core library — use return codes
- All crypto operations MUST go through `src/core/crypto/` — never call OpenSSL directly elsewhere
- Public C API in `include/agentchat/` must remain stable (no breaking changes without major version bump)
- TypeScript: strict mode, no `any`

## Security Rules (MANDATORY)
- Never log plaintext message content
- Never store private keys anywhere except agent process memory
- All new protocol messages must have Ed25519 signature support
- Frame size limits must be enforced (1MB max)
- Rate limiting must be applied to all new endpoints
- Run `ctest` before every commit — no broken tests

## Commit Convention
```
feat: add capability scope system
fix: enforce Ed25519 signature verification in handle_send_message
docs: update ARCHITECTURE.md with trust level design
test: add integration test for WS token auth
chore: bump OpenSSL to 3.3.0
security: fix H-02 message signature not verified
```

## Current Priorities (Q1 2026)
1. H-02: enforce Ed25519 message signature verification in `src/server/main.cpp` `handle_send_message()`
2. README demo script (`scripts/demo.sh`) with asciinema recording
3. AI orchestration framework integration example (`examples/ai_framework_demo.py`)
4. Agent capability scope system (v0.2.0)
5. Trust level system: VERIFIED / TRUSTED / UNKNOWN / BLOCKED

## What NOT to Do
- Do not add dependencies without updating CMakeLists.txt AND documenting in ARCHITECTURE.md
- Do not bypass the C ABI layer for language bindings
- Do not commit runtime data files (*.log, *.json state files)
- Do not weaken crypto (downgrade algorithms, remove signature checks)
- Do not auto-restart `live-hl` or `live-aster` trading processes
- Do not push broken builds — always run `make && ctest` first
