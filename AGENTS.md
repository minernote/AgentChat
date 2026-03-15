# AGENTS.md — AgentChat AI Development Guide

This file governs how AI agents (Codex, Claude Code, Gemini, etc.) contribute to AgentChat.

## Before You Start

1. Read `docs/ARCHITECTURE.md` — understand the system before touching code
2. Read `docs/ROADMAP.md` — find the next uncompleted task
3. Read `docs/SECURITY_AUDIT.md` — never regress security
4. Run `make -C build && ctest --output-on-failure` — confirm baseline passes

## Task Selection

Pick tasks in this order:
1. Any open security issues in `docs/SECURITY_AUDIT.md` marked ⚠️ PARTIAL or ❌ OPEN
2. Next unchecked item in `docs/ROADMAP.md` Q1 section
3. Failing tests in `tests/`
4. Documentation gaps

Do ONE task per session. Do not attempt multiple unrelated changes.

## Implementation Rules

### C++ Core (`src/`)
- All crypto through `src/core/crypto/` only
- New protocol messages need: packet type enum + serialization + Ed25519 signature field
- Server changes must preserve backward compatibility with existing clients
- Add unit test in `tests/` for every new function

### SDKs (`bindings/`)
- Python SDK mirrors the C API exactly — no extra abstractions
- Node.js SDK uses TypeScript strict mode
- Both SDKs must have matching examples in `examples/`

### Web UI (`web/`)
- React 19 + Vite + TypeScript strict
- No `any` types
- Mobile-first (PWA already configured)
- WebSocket connects to port 8766

## Verification (Mandatory Before Commit)

```bash
# C++ changes
cd build && make -j$(nproc) && ctest --output-on-failure

# Web changes  
cd web && npm run build

# Python SDK changes
cd bindings/python && python3 -m pytest tests/ 2>/dev/null || python3 -m build

# Node.js SDK changes
cd bindings/nodejs && npm run build
```

If any check fails — fix it before committing.

## Commit Format

```
type(scope): description

Types: feat, fix, docs, test, chore, security, perf
Scopes: core, server, sdk-python, sdk-node, web, docs, ci

Examples:
security(server): enforce Ed25519 signature verification in handle_send_message
feat(web): add agent roster sidebar with trust level badges  
docs(architecture): add capability scope system design
```

## Security Non-Negotiables

- Never log message content (plaintext or ciphertext)
- Never weaken crypto primitives
- Never disable signature verification
- Never expose private keys
- Always enforce frame size limits on new transports
- Always apply rate limiting on new API endpoints

## Reporting

After completing a task, summarize:
- What was done (1 sentence)
- Files changed
- Tests added/updated
- Build status ✅/❌
- Next recommended task
