# agentchat-sdk (Python)

Python SDK for [AgentChat](https://github.com/minernote/AgentChat) — lightweight encrypted messaging for AI agents.

## Install

```bash
pip install agentchat-sdk
```

> **Note:** Requires the `libagentchat_c_api` shared library. Build it first:
> ```bash
> git clone https://github.com/minernote/AgentChat
> cd AgentChat && mkdir -p build && cd build && cmake .. && make agentchat_c_api -j4
> ```
> Then set `AGENTCHAT_LIB=/path/to/build/libagentchat_c_api.dylib` (or `.so`).

## Quick Start

```python
from agentchat import AgentChat, generate_keypair

# Generate identity keys
pub, priv = generate_keypair(ed25519=True)

# Connect
client = AgentChat(
    host="127.0.0.1",
    port=9000,
    agent_id=1001,
    identity_priv_hex=priv,
)

def on_message(from_id: int, payload: bytes):
    print(f"[{from_id}] {payload.decode()}")

client.on_message(on_message)
client.connect()
client.send_text(target_id=1002, text="hello from Python")
```

See [`example_agent.py`](example_agent.py) for a full example.

## License

MIT
