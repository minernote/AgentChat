# Contributing to AgentChat

All contributions are welcome — from fixing a typo to implementing federation.
AgentChat is MIT-licensed and built in the open. Every merged PR makes the
project better for every AI agent that runs on top of it.

---

## Why contribute?

AgentChat is building the open messaging layer for AI agents. If you've ever wanted to:

- Give AI agents a reliable, encrypted way to talk to each other
- Help shape a protocol before it becomes a standard
- Sharpen your C++17, crypto, or distributed-systems skills on a real project
- Be credited in release notes and listed in `CONTRIBUTORS.md`

…then this is a great place to be. The codebase is young, the surface area is large, and good ideas land fast.

---

## Code of Conduct

Be respectful. Be constructive. Critique code, not people. Welcome newcomers.

---

## Development Setup

### Prerequisites

- C++17 compiler: GCC 11+ or Clang 13+
- CMake 3.20+
- Python 3.10+
- Node.js 18+
- Git

### Clone and Build

```bash
git clone https://github.com/minernote/AgentChat.git
cd AgentChat
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### Run Tests

```bash
# C++ tests
cd build && ctest --verbose

# Python SDK
cd bindings/python && pip install -e ".[dev]" && pytest tests/

# Node.js SDK
cd bindings/nodejs && npm install && npm test
```

### Quick Start via Docker

```bash
docker compose up -d
# Server at localhost:8765, Web UI at localhost:3000
```

---

## Project Structure

```
AgentChat/
  src/
    core/
      crypto/      # X25519, AES-256-GCM, Ed25519 (src/core/crypto/)
      protocol/    # Binary framing and packet types (src/core/protocol/)
      network/     # TCP transport (src/core/network/)
      storage/     # SQLite message store (src/core/storage/)
    agent/         # AgentClient — the main SDK entry point
    server/        # agentchat_server binary
  bindings/
    python/        # Python SDK (PyPI: agentchat)
    nodejs/        # Node.js SDK (npm: @agentchat/sdk)
  include/         # Public C headers (stable ABI)
  tests/           # Integration and unit tests
  docs/            # Documentation
  examples/        # Example agents and integrations
  web/             # React web client
  landing/         # Landing page
```

---

## Code Style

### C++
- C++17 standard, snake_case, 4-space indent, 100-char line limit
- Run `make format` (clang-format) before committing
- Doxygen comments on all public APIs
- No raw pointers — use `std::unique_ptr` / `std::shared_ptr`
- No exceptions in core — use `std::expected` or error codes

### Python SDK (`bindings/python/`)
- PEP 8, type hints everywhere, Google-style docstrings
- Run `ruff check .` and `mypy .` before submitting

### Node.js SDK (`bindings/nodejs/`)
- TypeScript strict mode, ESLint + Prettier
- Run `npm run lint` before submitting

### Commit Messages
- Conventional Commits: `type(scope): description`
- Types: `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `chore`
- Example: `feat(crypto): add Ed25519 batch verification`
- One logical change per commit; tests required for new functionality

---

## How to Contribute

### Reporting Bugs
1. Search [existing issues](https://github.com/minernote/AgentChat/issues) first
2. Open a new issue with: version, OS/arch, reproduction steps, expected vs actual, logs

### Suggesting Features
1. Open a [GitHub Discussion](https://github.com/minernote/AgentChat/discussions)
2. Describe the use case, not just the feature
3. A maintainer will convert it to a tracked issue if there's consensus

### Submitting Code
1. Fork [minernote/AgentChat](https://github.com/minernote/AgentChat)
2. `git checkout -b feat/your-feature-name`
3. Make changes, add tests, run linters
4. Push and open a Pull Request

---

## PR Guidelines

### Checklist Before Opening
- [ ] Tests pass locally
- [ ] Linters pass
- [ ] New code has test coverage
- [ ] Public APIs have doc comments
- [ ] `CHANGELOG.md` updated for user-facing changes
- [ ] No unrelated changes mixed in

### PR Description
Answer these three questions:
1. What does this PR do?
2. Why? (link to issue if applicable)
3. How was it tested?

### Review Process
- 1 maintainer approval required for all PRs
- 2 approvals required for security-sensitive changes (crypto, auth, transport)
- Squash commits before merge
- PRs stale for 30 days are closed (reopenable)

---

## Good First Issues

Not sure where to start? These are well-scoped and don't require deep context:

### Documentation
- Improve the quick-start guide with more real-world examples (`docs/AGENT_API.md`)
- Add AI framework integration examples (`examples/`)
- Fix typos and clarify confusing passages anywhere in `docs/`
- Translate README to Chinese, Spanish, or Portuguese

### Python SDK (`bindings/python/`)
- Add retry logic for connection drops (`bindings/python/agentchat/client.py`)
- Improve error messages to be more actionable
- Add async context manager support for channels
- More unit tests for edge cases

### Node.js SDK (`bindings/nodejs/`)
- TypeScript generic types for message payloads (`bindings/nodejs/index.ts`)
- Improve reconnection logic
- Add event emitter usage examples

### C++ Core
- Missing unit tests for the discovery module (`tests/`)
- Better error messages in the transport layer (`src/core/network/`)
- Profile and optimize message serialization (`src/core/protocol/`)
- Add Windows CI pipeline (`.github/workflows/`)

### DevOps
- Improve Docker Compose for local development (`docker-compose.yml`)
- Add ARM64 build to CI (`.github/workflows/ci.yml`)
- Improve CMake presets for common platforms (`CMakeLists.txt`)

Filter by the [`good-first-issue`](https://github.com/minernote/AgentChat/issues?q=label%3Agood-first-issue) label on GitHub.

---

## Community

Come say hi, ask questions, share what you're building, or just hang out:

- **Community:** See README for community links
- **GitHub Discussions:** [github.com/minernote/AgentChat/discussions](https://github.com/minernote/AgentChat/discussions)
- **Issues:** [github.com/minernote/AgentChat/issues](https://github.com/minernote/AgentChat/issues)

We're friendly. No question is too basic.

---

## Security Issues

Do **not** open public GitHub issues for security vulnerabilities.

**Report:** Open a [private GitHub Security Advisory](https://github.com/minernote/AgentChat/security/advisories/new) — only visible to you and the reporter

Include: description, reproduction steps, potential impact, suggested fix.

We acknowledge within 48 hours and patch critical issues within 7 days.
Please allow 90 days before public disclosure.

---

## Recognition

All contributors are listed in `CONTRIBUTORS.md`.
Significant contributions are called out in release notes and `CHANGELOG.md`.

Thank you for helping build the open messaging layer for AI agents.

---

*AgentChat — MIT License — [github.com/minernote/AgentChat](https://github.com/minernote/AgentChat)*
