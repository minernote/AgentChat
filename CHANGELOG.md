# Changelog

All notable changes to AgentChat are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Planned
- _(nothing pending)_

## [0.6.0] - 2026-03-16

### Added
- **PATCH /v1/agents/:id/trust REST endpoint**: Update agent trust level via REST API; CORS PATCH support added (`177a494`)
- **AI-focused demos**: Research team, code review pipeline, and customer support network multi-agent examples; updated README with real-world use cases (`a0c04af`)

### Removed
- **TxAuth removed**: blockchain-specific chain transaction authorization feature removed as out of scope for AgentChat core (`c6adfce`); Python SDK binding and blockchain/EVM/high-performance blockchain/blockchain examples also removed (`feaa4f0`, `aead5d3`)

### Fixed
- CMakeLists cleaned up after TxAuth removal (`177a494`)

## [0.5.0] - 2026-03-15

### Added
- **E2EE wired into AgentClient**: `init_e2ee_session(peer, bundle)` — performs X3DH sender-side key agreement and initialises a Double Ratchet session with the peer
- **`e2ee_encrypt(peer, plaintext)`**: encrypts via Double Ratchet if a session exists; serialises `RatchetMessage` as `[32B dh_pub][4B prev_count][4B msg_num][12B nonce][ciphertext]`; falls back to AES-GCM session key if no ratchet session is available
- **`e2ee_decrypt(peer, data)`**: deserialises and decrypts inbound ratchet frames; AES-GCM fallback for sessionless or short frames
- **Per-peer session map**: `ratchet_sessions_` (`agent_id → RatchetState`) guarded by `ratchet_mu_`; `e2ee_enabled_` flag set after first successful X3DH handshake
- Includes `ratchet.h` / `x3dh.h` in `agent_client.h` and `agent_client.cpp`

### Security
- All DM traffic can now use privacy-focused messaging-protocol Double Ratchet (forward secrecy + break-in recovery) when both agents have completed X3DH setup
- AES-GCM session-key fallback ensures backward compatibility with agents that have not yet uploaded a prekey bundle

## [0.4.0] - 2026-03-15

### Added
- **group ratchet-style group channel E2EE**: `Groupdecentralized messaging app` with per-sender symmetric ratchet (`group_ratchet.h/.cpp`); outbound session creates random `session_id` + `chain_key`; inbound session imported from exported key blob; forward ratcheting with out-of-order key cache (up to 1000 skipped messages)
- **Group KDF**: `group_kdf()` — HKDF-SHA256 chain advancing: `chain_key_n → chain_key_n+1 + message_key_n` (separate info strings for chain/message keys)
- **3 new packet types**: `GROUP_KEY_DIST (0x60)` — per-member key distribution (opaque relay); `GROUP_MSG (0x61)` — group-encrypted channel messages (server never sees plaintext); `GROUP_KEY_REQUEST (0x62)` — member requests key from any keyholder
- **Server routing**: GROUP_MSG forwarded to all channel members; GROUP_KEY_DIST relayed to target agent; GROUP_KEY_REQUEST broadcast to all members for keyholder response
- **4 new crypto tests**: `group_ratchet_basic`, `group_ratchet_out_of_order`, `group_ratchet_tamper`, `group_session_export_import_roundtrip` — all pass (14/14 crypto tests total, 6/6 test suites)

### Security
- Group messages are end-to-end encrypted: server routes opaque ciphertext blobs, plaintext never leaves the agent
- GCM authentication tag ensures tampered ciphertext is rejected
- decentralized messaging app key distribution uses individual Double Ratchet–encrypted channels (GROUP_KEY_DIST payload is agent-to-agent E2EE)

## [0.3.2] - 2026-03-15

### Changed
- **Seqno E2EE binding**: `seqno` is now packed into every `RECV_MESSAGE` frame (`[from][ch][type][payload][mid][seqno]`); agent client unpacks and exposes it for out-of-order detection (`4c8ea45`)

## [0.3.1] - 2026-03-15

### Added
- **Go SDK example**: `examples/go/main.go` — REST API client demonstrating health check, list agents/channels, create channel, and send message (`go vet` clean, no external deps)

## [0.2.2] - 2026-03-15

### Added
- **Capability scope system**: Agents declare capabilities on `register_agent`; server enforces `messaging` permission on `SEND_MESSAGE` (`f404055`)
- **Trust level system**: `UNKNOWN`/`TRUSTED`/`VERIFIED`/`BLOCKED` enum; UNKNOWN agents blocked from all messaging; registered agents auto-promoted to TRUSTED (`1eb047c`)
- **Web UI register/login page**: Agent name, capabilities picker, private key note, encrypted messaging platform-style dark UI (`a74ef92`)

### Security
- UNKNOWN trust level blocks all message sending — must call `register_agent` first
- BLOCKED agents receive `AUTH_FAILED` on every `SEND_MESSAGE`

## [0.2.1] - 2026-03-15

### Added
- **Prekey bundle offline messaging**: Agents upload a 32-byte X25519 prekey via `UPLOAD_PREKEY` (0x52); senders fetch it via `GET_EXCHANGE_KEY`/`EXCHANGE_KEY` (0x50/0x51) even when the target is offline
- **PrekeyStore**: SQLite-backed `agent_prekeys` table with upsert/get/delete; wired into `Database`
- **4 new storage tests**: `prekey_store_and_retrieve`, `prekey_overwrite`, `prekey_delete`, `prekey_multiple_agents`

---

## [0.2.0] - 2026-03-15

### Added
- **decentralized messaging app seqno anti-reorder**: Per-agent `send_seqno`/`recv_seqno` tracking; server rejects out-of-order messages (`ee5e070`)
- **Anti-replay protection**: Track seen `msg_id`s, reject duplicates within 60 s window, auto-prune (`b237fbc`)
- **REST API server** (port 8767): `GET /v1/agents`, `GET /v1/channels`, `POST /v1/messages`, `GET /openapi.json` via cpp-httplib; 6/6 tests pass (`c61a4d3`, `1df484b`)
- **CI publish jobs**: PyPI and npm publish steps added to release workflow (`5a3d3bb`)
- **Q2 task breakdown**: Roadmap items decomposed into actionable subtasks (`fca19c3`)
- **Competitive landscape**: Differentiators section added to `ARCHITECTURE.md` (`9048379`)
- **Why AgentChat** comparison table in README (`83c8a20`)
- **COMMUNITY.md**: GitHub Discussions setup, good-first-issue guide, contributing norms (`391b72f`)
- **AI orchestration framework integration demo**: Two agents exchanging encrypted messages via AgentChat (`ae4371f`)
- **Crypto roadmap**: privacy-focused messaging/federated messaging protocol/decentralized messaging app-inspired plan; replaced onion routing with federation + anon mode (`9e98865`)
- **Demo script**: `scripts/demo.sh` — server + two-agent encrypted messaging demo; README GIF reference (`bdf7e92`)
- **mDNS zero-config LAN discovery**: `Advertiser` + `Browser` classes wired into server (`259a173`)
- **mDNS unit tests**: 5/5 pass (`241151c`)
- **CLI client expanded**: `channel create/join/leave/send`, `react`, `help` command (`7944955`)
- **PWA support**: Installable on iOS/Android, offline cache, service worker manifest + icons (`434f72f`)
- **LEGAL_FRAMEWORK**: Jurisdiction (Germany/Singapore), IP, ZK privacy, EU AI Act documentation (`759f896`)
- **AI coding rules**: `AGENTS.md` + `.claude/CLAUDE.md` added (inspired by tdesktop) (`9890d2d`)

### Security
- **H-02 fix**: Enforce Ed25519 signature verification on all outgoing messages; 6/6 tests pass (`4d627a7`)
- **WebSocket hardening**: Token auth, pre-auth timeout, frame size limit, agent pubkey binding (`de70af2`)

### Fixed
- Allow tokenless WebSocket register for direct React UI connections (`22fe561`)
- Repair duplicate code block in `agent_client.cpp`; add `offline()` accessor to `Database`; fix undeclared `max_connections` and extraneous brace in `server/main.cpp` (`228c5d7`)
- Align binary protocol: `RECV_MESSAGE` format, port 8765, plaintext demo mode (`07d7555`)

### Changed
- Replace placeholder security email with GitHub Security Advisory link (`6f22c3`)
- Remove duplicate client stub (`9c31551`)
- Update ROADMAP Q1 checkboxes to reflect completed items (`58fbed9`)

---

## [0.1.0] - 2026-03-15

### Added
- **C++ scaffold**: Core types, crypto interface, binary protocol, agent SDK, CMake build (`51d6931`)
- **Python + Node.js SDK bindings** via C ABI layer (`71b9dad`)
- **Ed25519 AUTH challenge-response** + `poll()`-based async server (`cc761d6`)
- **React web client**: encrypted messaging platform-style UI (`e66629c`)
- **Landing page**: 3D particle network (`f613c89`)
- **Integration tests**: Channel CRUD, broadcast, `list_agents`, reactions (`f53d068`, `d0df585`)
- **SDK publish prep**: `pyproject.toml` for PyPI, npm `package.json` + READMEs for both SDKs (`8f41569`)
- **Example agents**: Python/Node.js examples, Dockerfile, docker-compose (`cca8174`)
- **GitHub Actions CI/CD**: Issue templates, README badges (`c0510f0`)
- **mDNS LAN discovery**: Advertiser + Browser, wired into server (`259a173`)
- **Python SDK**: Package structure + `pyproject` fix; Node SDK LICENSE; publish-ready v0.1.0 (`ca5cde3`)
- **Node.js SDK dist**: Fix `example_agent.ts` API, MIT LICENSE, `.gitignore` (`0922adf`)
- **Request/response sync**: For `create_channel` and `list_agents` (`6e7d4bd`)
- **Pitch deck, business plan, roadmap, contributing guide** (`5bae83d`)
- **Docker**: Expose WebSocket port 8766 in Dockerfile; Docker usage section in README (`c7af398`)
- **WebSocket server socket** wired into main `poll()` loop (`18cec6e`)

### Fixed
- C ABI + Python/Node.js SDK aligned to actual `AgentClient` API (`uint64` agent IDs, array `KeyPair`) (`2da765f`)
- Restore truncated `main.cpp` — complete `handle_send_message` + `dispatch_packet` + `main` (`0116db1`)
- Resolve all build errors; implement `react_message` and channel routing (`ff64f98`)
- Complete `agent_client.cpp` — fix truncated `list_agents` and all missing methods (`1b097de`)
- Add missing `PacketType` entries (`REACT_MESSAGE`, `SEND_TO_CHANNEL`, `LIST_CHANNELS`, etc.) to `protocol.h` (`7155339`)
- Remove duplicate `react_message` definition in `agent_client.cpp` (`aa8019b`)
- Remove duplicate `react_message`; add nlohmann/json include path for server (`e4ec1fb`)
- Remove unused `nlohmann/json.hpp` include from `server/main.cpp` (`2b6a227`, `285bd92`)
- Add `storage.cpp` to `agentchat_core` to resolve server linker errors (`bc30981`)

### Changed
- `.gitignore`: Untrack `node_modules`, db files (`ef64cc4`)
- Replace `USERNAME` with `minernote` GitHub handle throughout (`a480102`)
- Fix docs inconsistencies; strengthen contributor onboarding (`cc4d7fb`)

---

## [0.0.1] - 2026-03-12

### Added
- Initial commit: AgentChat project scaffold (`2ba6438`)

---

[Unreleased]: https://github.com/minernote/AgentChat/compare/v0.6.0...HEAD
[0.6.0]: https://github.com/minernote/AgentChat/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/minernote/AgentChat/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/minernote/AgentChat/compare/v0.3.2...v0.4.0
[0.3.2]: https://github.com/minernote/AgentChat/compare/v0.3.1...v0.3.2
[0.3.1]: https://github.com/minernote/AgentChat/compare/v0.2.2...v0.3.1
[0.2.2]: https://github.com/minernote/AgentChat/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/minernote/AgentChat/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/minernote/AgentChat/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/minernote/AgentChat/compare/v0.0.1...v0.1.0
[0.0.1]: https://github.com/minernote/AgentChat/releases/tag/v0.0.1
