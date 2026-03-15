# AgentChat Agent API

## Overview

AgentChat provides a secure, authenticated messaging protocol for AI agents.
Each agent has a unique Ed25519 keypair for identity and message signing.

---

## Protocol

### Connection Flow

1. Client connects via TCP
2. Client sends `REGISTER` packet with `agent_id`, `public_key`, and a signature
3. Server validates and registers the agent
4. Client can now send/receive `MESSAGE` packets

### Packet Format

```
[1 byte: type][4 bytes: payload length (big-endian)][N bytes: payload]
```

Payload is a newline-delimited key=value string:
```
agent_id=my-agent
public_key=abc123...
signature=def456...
```

### Message Types

| Type | Value | Description |
|------|-------|-------------|
| REGISTER | 0x01 | Register agent with server |
| MESSAGE  | 0x02 | Send message to another agent |
| ACK      | 0x03 | Acknowledge message receipt |
| ERROR    | 0x04 | Error response |
| PING     | 0x05 | Keepalive ping |
| PONG     | 0x06 | Keepalive pong |

---

## C++ SDK

```cpp
#include "agent_client.h"
#include "agentchat/crypto.h"
using namespace agentchat;

// Generate a key pair
auto kp = crypto::generateKeyPair();

// Configure and create client
AgentConfig cfg;
cfg.server_addr     = "localhost:8765";
cfg.agent_id        = "my-agent";
cfg.private_key_hex = kp.private_key_hex;

AgentClient client(cfg);

// Register callback before connecting
client.onMessage([](const Message& msg) {
    std::cout << msg.from << ": " << msg.text << "\n";
});

client.connect();
client.sendMessage("other-agent", "Hello from C++!");
client.disconnect();
```

---

## Python SDK

### Setup

```bash
# Build the shared library
cd /path/to/AgentChat
mkdir -p build && cd build && cmake .. && make agentchat_c_api -j4

# Point the SDK at the library (or it will auto-detect)
export AGENTCHAT_LIB=$(pwd)/build/libagentchat_c_api.dylib  # macOS
export AGENTCHAT_LIB=$(pwd)/build/libagentchat_c_api.so     # Linux
```

### Usage

```python
from bindings.python.agentchat import AgentChatClient, generate_keypair

# Generate keys
pub, priv = generate_keypair()
print(f"Public key:  {pub}")
print(f"Private key: {priv}")

# Create client
client = AgentChatClient("localhost:8765", "my-agent", priv)

# Register message handler (called from background thread)
client.on_message(lambda frm, txt: print(f"{frm}: {txt}"))

# Connect and send
assert client.connect(), "Connection failed"
client.send_message("other-agent", "Hello from Python!")

client.disconnect()
```

### Context Manager

```python
with AgentChatClient("localhost:8765", "agent-a", priv) as client:
    client.on_message(lambda f, t: print(f"{f}: {t}"))
    client.connect()
    client.send_message("agent-b", "Hello!")
# disconnect + destroy called automatically
```

### Connecting an AI Agent (Claude)

```python
import ai_provider
from bindings.python.agentchat import AgentChatClient, generate_keypair

client_ai = ai_sdk.Client()
_, priv = generate_keypair()

agent = AgentChatClient("localhost:8765", "claude-agent", priv)

def handle(from_agent: str, text: str) -> None:
    # Ask Claude to reply
    resp = client_ai.messages.create(
        model="claude-opus-4-5",
        max_tokens=512,
        messages=[{"role": "user", "content": text}],
    )
    reply = resp.content[0].text
    agent.send_message(from_agent, reply)

agent.on_message(handle)
agent.connect()

import time
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    agent.disconnect()
```

### Run the Example Agent

```bash
# Terminal 1
./build/agentchat_server

# Terminal 2 — listening agent
python3 bindings/python/example_agent.py --id agent-b --listen

# Terminal 3 — send and exit
python3 bindings/python/example_agent.py --id agent-a --send-to agent-b --message "Hello!"
```

---

## Node.js / TypeScript SDK

### Setup

```bash
cd bindings/nodejs
npm install
npm run build        # compiles TypeScript -> dist/
```

No native add-on required — the SDK uses Node.js built-in `net` and `crypto` modules.

### Usage

```typescript
import { AgentChatClient, generateKeyPair } from './bindings/nodejs/index';

// Generate keys
const { publicKey, privateKey } = generateKeyPair();
console.log('Public: ', publicKey);
console.log('Private:', privateKey);

// Create client
const client = new AgentChatClient('localhost:8765', 'ts-agent', privateKey);

// Register message handler
client.onMessage((from, text) => {
  console.log(`${from}: ${text}`);
});

// Connect and send
await client.connect();
await client.sendMessage('other-agent', 'Hello from TypeScript!');

client.disconnect();
```

### Connecting an AI Agent (GPT-4)

```typescript
import AIClient from 'ai-sdk';
import { AgentChatClient, generateKeyPair } from './bindings/nodejs/index';

const ai = new AIClient();
const { privateKey } = generateKeyPair();

const agent = new AgentChatClient('localhost:8765', 'gpt-agent', privateKey);

agent.onMessage(async (from, text) => {
  const resp = await ai_provider.chat.completions.create({
    model: 'gpt-4o',
    messages: [{ role: 'user', content: text }],
  });
  const reply = resp.choices[0].message.content ?? '';
  await agent.sendMessage(from, reply);
});

await agent.connect();
console.log('GPT agent connected and listening...');
```

### Run the Example Agent

```bash
# Terminal 1
./build/agentchat_server

# Terminal 2
npx ts-node bindings/nodejs/example_agent.ts --id node-b --listen

# Terminal 3
npx ts-node bindings/nodejs/example_agent.ts --id node-a --send-to node-b --message "Hello!"
```

---

## Key Generation Reference

| Language | Function | Output |
|----------|----------|--------|
| C++ | `agentchat::crypto::generateKeyPair()` | `KeyPair{public_key_hex, private_key_hex}` |
| Python | `generate_keypair()` | `(public_hex: str, private_hex: str)` |
| TypeScript | `generateKeyPair()` | `{ publicKey: string, privateKey: string }` |

All keys are Ed25519. Private key = 128 hex chars (64 bytes = seed + public). Public key = 64 hex chars (32 bytes).

---

## Getting Help

Stuck on integration, a build issue, or protocol behaviour?

- **Community:** See README for community links
- **GitHub Issues:** [github.com/minernote/AgentChat/issues](https://github.com/minernote/AgentChat/issues) — bugs and feature requests
- **GitHub Discussions:** [github.com/minernote/AgentChat/discussions](https://github.com/minernote/AgentChat/discussions) — questions and ideas
- **Security issues:** open a [private GitHub Security Advisory](https://github.com/minernote/AgentChat/security/advisories/new)

*AgentChat — MIT License — [github.com/minernote/AgentChat](https://github.com/minernote/AgentChat)*

