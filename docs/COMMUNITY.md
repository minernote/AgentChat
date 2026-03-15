# AgentChat Community Guide

## Community Forums

We use Community Forums for questions, ideas, and design conversations.
Issues are for bugs and feature requests only — keep them focused.

### How to enable Discussions (repo admins)

1. Go to https://github.com/minernote/AgentChat/settings
2. Scroll to **Features** section
3. Check **Discussions**
4. Click **Set up discussions** — use the default template

### Discussion categories we use

| Category | Purpose |
|---|---|
| 💬 General | Anything about AgentChat |
| 💡 Ideas | Feature proposals before opening an issue |
| 🙏 Q\&A | How-to questions, help with setup |
| 🔒 Security | Non-critical security questions (critical → private advisory) |
| 📣 Show and Tell | Share what you built with AgentChat |

---

## Contributing

Full guide: [docs/CONTRIBUTING.md](CONTRIBUTING.md)

### Quick start (3 steps)

```bash
# 1. Fork and clone
git clone https://github.com/YOUR_USERNAME/AgentChat.git
cd AgentChat

# 2. Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure

# 3. Make your change, then open a PR
git checkout -b fix/your-fix-name
# ... make changes ...
git push origin fix/your-fix-name
```

---

## Good First Issues

Looking for your first contribution? Filter by the `good-first-issue` label:
https://github.com/minernote/AgentChat/issues?q=label%3Agood-first-issue

### What makes a good `good-first-issue`

When creating issues for new contributors, label them `good-first-issue` if:
- The fix is self-contained (touches 1-3 files)
- The expected behavior is clearly described
- A test case can be written
- No deep knowledge of the codebase is required

### Current good first issues (examples)

- Add `--version` flag to `agentchat_client` CLI
- Write a Go example in `examples/go/`
- Add missing docstrings to Python SDK
- Improve error messages when server is unreachable
- Add `docker-compose.yml` for easy local dev setup

---

## Community Norms

- Be respectful — we follow the [Contributor Covenant](https://www.contributor-covenant.org/)
- Search before asking — your question may already be answered
- Include context — OS, version, error logs when reporting bugs
- One issue per issue — don't bundle multiple bugs together

---

## Roadmap Participation

Want to influence what we build next? 
- Comment on [docs/ROADMAP.md](ROADMAP.md) items via Discussions
- Vote on issues with 👍
- Open a Discussion in the 💡 Ideas category

We review community feedback before each quarterly planning cycle.
