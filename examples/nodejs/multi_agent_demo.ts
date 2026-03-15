/**
 * AgentChat Multi-Agent Demo — TypeScript/Node.js
 * =================================================
 * Demonstrates:
 *   - Multiple agents connecting to the same server
 *   - Agent-to-agent direct message routing
 *   - Creating a channel and broadcasting to it
 *
 * Usage:
 *   npx ts-node multi_agent_demo.ts --server localhost:8765
 *
 * Prerequisites:
 *   npm install ws
 *   npm install -D @types/ws ts-node typescript
 */

import WebSocket from 'ws';
import { parseArgs } from 'util';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface AgentConfig {
  id: number;
  name: string;
  role: 'orchestrator' | 'worker';
}

interface OutboundMsg {
  type: string;
  [key: string]: unknown;
}

interface InboundEnvelope {
  type: string;
  from?: number;
  id?: number;
  text?: string;
  channel_id?: number;
  message?: string;
  [key: string]: unknown;
}

// ---------------------------------------------------------------------------
// AgentConnection class
// ---------------------------------------------------------------------------

class AgentConnection {
  public ws!: WebSocket;
  private uri: string;
  private config: AgentConfig;
  private onMessage: (env: InboundEnvelope) => void;
  private resolveOpen!: () => void;
  public ready: Promise<void>;

  constructor(
    server: string,
    config: AgentConfig,
    onMessage: (env: InboundEnvelope) => void,
  ) {
    this.uri = `ws://${server}`;
    this.config = config;
    this.onMessage = onMessage;
    this.ready = new Promise((res) => { this.resolveOpen = res; });
    this._connect();
  }

  private _connect(): void {
    this.ws = new WebSocket(this.uri);

    this.ws.on('open', () => {
      this.send({ type: 'register', agent_id: this.config.id, name: this.config.name });
      console.log(`[${this.config.name}] connected and registered`);
      this.resolveOpen();
    });

    this.ws.on('message', (data: WebSocket.RawData) => {
      try {
        const env = JSON.parse(data.toString()) as InboundEnvelope;
        this.onMessage(env);
      } catch {
        // ignore malformed frames
      }
    });

    this.ws.on('error', (err) => console.error(`[${this.config.name}] error:`, err.message));
    this.ws.on('close', () => console.log(`[${this.config.name}] disconnected`));
  }

  send(payload: OutboundMsg): void {
    if (this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(payload));
    }
  }

  sendMessage(to: number, text: string, replyTo?: number): void {
    const msg: OutboundMsg = { type: 'message', to, text };
    if (replyTo !== undefined) msg.reply_to = replyTo;
    this.send(msg);
  }

  createChannel(name: string): void {
    this.send({ type: 'create_channel', name });
  }

  joinChannel(channelId: number): void {
    this.send({ type: 'join_channel', channel_id: channelId });
  }

  broadcastToChannel(channelId: number, text: string): void {
    this.send({ type: 'channel_message', channel_id: channelId, text });
  }

  close(): void {
    this.ws.close();
  }
}

// ---------------------------------------------------------------------------
// Demo scenario
// ---------------------------------------------------------------------------

function sleep(ms: number): Promise<void> {
  return new Promise((res) => setTimeout(res, ms));
}

async function runDemo(server: string): Promise<void> {
  const ORCHESTRATOR_ID = 1;
  const WORKER_A_ID     = 2;
  const WORKER_B_ID     = 3;

  // Shared state for channel created by orchestrator
  let broadcastChannelId: number | null = null;

  // ---- Worker A ----
  const workerA = new AgentConnection(
    server,
    { id: WORKER_A_ID, name: 'worker-a', role: 'worker' },
    (env) => {
      if (env.type === 'message') {
        console.log(`[worker-a] received from agent ${env.from}: "${env.text}"`);
        // Echo back a processed result
        workerA.sendMessage(env.from!, `[worker-a result] processed: ${env.text}`, env.id);
      } else if (env.type === 'channel_message') {
        console.log(`[worker-a] broadcast on channel ${env.channel_id}: "${env.text}"`);
      } else if (env.type === 'join_channel_ack') {
        console.log(`[worker-a] joined channel ${env.channel_id}`);
      }
    },
  );

  // ---- Worker B ----
  const workerB = new AgentConnection(
    server,
    { id: WORKER_B_ID, name: 'worker-b', role: 'worker' },
    (env) => {
      if (env.type === 'message') {
        console.log(`[worker-b] received from agent ${env.from}: "${env.text}"`);
        workerB.sendMessage(env.from!, `[worker-b result] processed: ${env.text}`, env.id);
      } else if (env.type === 'channel_message') {
        console.log(`[worker-b] broadcast on channel ${env.channel_id}: "${env.text}"`);
      } else if (env.type === 'join_channel_ack') {
        console.log(`[worker-b] joined channel ${env.channel_id}`);
      }
    },
  );

  // ---- Orchestrator ----
  const orchestrator = new AgentConnection(
    server,
    { id: ORCHESTRATOR_ID, name: 'orchestrator', role: 'orchestrator' },
    (env) => {
      if (env.type === 'message') {
        console.log(`[orchestrator] reply from agent ${env.from}: "${env.text}"`);
      } else if (env.type === 'create_channel_ack') {
        broadcastChannelId = env.channel_id as number;
        console.log(`[orchestrator] channel created: id=${broadcastChannelId}`);
      } else if (env.type === 'error') {
        console.error(`[orchestrator] server error: ${env.message}`);
      }
    },
  );

  // Wait for all agents to connect
  await Promise.all([orchestrator.ready, workerA.ready, workerB.ready]);
  console.log('\n--- All agents connected ---\n');
  await sleep(200);

  // Step 1: Orchestrator sends direct messages to workers
  console.log('--- Step 1: Agent-to-agent direct messages ---');
  orchestrator.sendMessage(WORKER_A_ID, 'Hello worker-a, please process task-1');
  orchestrator.sendMessage(WORKER_B_ID, 'Hello worker-b, please process task-2');
  await sleep(500);

  // Step 2: Create a broadcast channel
  console.log('\n--- Step 2: Create broadcast channel ---');
  orchestrator.createChannel('demo-broadcast');
  await sleep(500);

  if (broadcastChannelId !== null) {
    // Step 3: Workers join the channel
    console.log('\n--- Step 3: Workers join channel ---');
    workerA.joinChannel(broadcastChannelId);
    workerB.joinChannel(broadcastChannelId);
    await sleep(300);

    // Step 4: Orchestrator broadcasts to the channel
    console.log('\n--- Step 4: Broadcast to channel ---');
    orchestrator.broadcastToChannel(broadcastChannelId, 'Broadcast: all workers stand by for sync');
    await sleep(500);
  } else {
    console.warn('Channel creation not acknowledged — skipping broadcast steps.');
    console.warn('(Ensure the server supports create_channel messages)');
  }

  console.log('\n--- Demo complete ---');
  await sleep(200);

  orchestrator.close();
  workerA.close();
  workerB.close();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

const { values } = parseArgs({
  options: { server: { type: 'string', default: 'localhost:8765' } },
  strict: false,
});

runDemo(values['server'] as string).catch((err) => {
  console.error('Demo failed:', err);
  process.exit(1);
});
