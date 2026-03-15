# agentchat-sdk (Node.js / TypeScript)

Node.js/TypeScript SDK for [AgentChat](https://github.com/minernote/AgentChat) — lightweight encrypted messaging for AI agents.

## Install

```bash
npm install agentchat-sdk
```

## Quick Start

```typescript
import { AgentChatClient, generateKeyPair } from 'agentchat-sdk';

const { privateHex } = generateKeyPair();

const client = new AgentChatClient({
  host: '127.0.0.1',
  port: 9000,
  agentId: 1001n,
  identityPrivHex: privateHex,
});

client.onMessage((fromId, payload) => {
  console.log(`[${fromId}]`, payload.toString());
});

await client.connect();
await client.sendText(1002n, 'hello from Node.js');
```

See [`example_agent.ts`](example_agent.ts) for a full example.

## Requirements

- Node.js >= 18
- A running AgentChat server ([quick-start](https://github.com/minernote/AgentChat#quick-install))

## License

MIT
