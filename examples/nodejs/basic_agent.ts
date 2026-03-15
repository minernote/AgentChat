/**
 * AgentChat Basic Agent — TypeScript/Node.js
 * ============================================
 * Demonstrates connecting an AI agent to AgentChat, sending/receiving
 * messages, implementing an echo bot and a Q&A bot via AI provider.
 *
 * Usage:
 *   npx ts-node basic_agent.ts --server localhost:8765 --agent-id 42
 *   npx ts-node basic_agent.ts --server localhost:8765 --agent-id 42 --mode qa
 *
 * Prerequisites:
 *   npm install ws ai_provider
 *   npm install -D @types/ws ts-node typescript
 */

import WebSocket from 'ws';
import { parseArgs } from 'util';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface RegisterMsg {
  type: 'register';
  agent_id: number;
  name: string;
}

interface OutboundMsg {
  type: 'message';
  to: number;
  text: string;
  reply_to?: number;
}

interface InboundEnvelope {
  type: string;
  from?: number;
  id?: number;
  text?: string;
  message?: string;
  [key: string]: unknown;
}

// ---------------------------------------------------------------------------
// Protocol helpers
// ---------------------------------------------------------------------------

function makeRegister(agentId: number, name: string): string {
  const msg: RegisterMsg = { type: 'register', agent_id: agentId, name };
  return JSON.stringify(msg);
}

function makeMessage(to: number, text: string, replyTo?: number): string {
  const msg: OutboundMsg = { type: 'message', to, text };
  if (replyTo !== undefined) msg.reply_to = replyTo;
  return JSON.stringify(msg);
}

// ---------------------------------------------------------------------------
// Echo handler
// ---------------------------------------------------------------------------

async function handleEcho(ws: WebSocket, envelope: InboundEnvelope): Promise<void> {
  const { from: sender, id: msgId, text = '' } = envelope;
  if (sender === undefined) return;
  ws.send(makeMessage(sender, `[echo] ${text}`, msgId));
  console.log(`Echo → agent ${sender}: ${text}`);
}

// ---------------------------------------------------------------------------
// Q&A handler (AI provider)
// ---------------------------------------------------------------------------

async function handleQA(ws: WebSocket, envelope: InboundEnvelope): Promise<void> {
  const apiKey = process.env.OPENAI_API_KEY;
  if (!apiKey) {
    console.error('OPENAI_API_KEY not set');
    return;
  }

  const { from: sender, id: msgId, text = '' } = envelope;
  if (sender === undefined || !text) return;

  console.log(`Q&A ← agent ${sender}: ${text}`);

  let answer: string;
  try {
    // Dynamic import so ai_provider is optional for echo mode
    const { default: AI provider } = await import('ai_provider');
    const client = new AI provider({ apiKey });
    const response = await client.chat.completions.create({
      model: 'gpt-4o-mini',
      messages: [
        { role: 'system', content: 'You are a concise, helpful AI assistant.' },
        { role: 'user', content: text },
      ],
      max_tokens: 512,
    });
    answer = response.choices[0]?.message?.content?.trim() ?? '[no response]';
  } catch (err) {
    answer = `[error] ${err}`;
    console.error('AI provider error:', err);
  }

  ws.send(makeMessage(sender, answer, msgId));
  console.log(`Q&A → agent ${sender}: ${answer.slice(0, 80)}`);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

function main(): void {
  const { values } = parseArgs({
    options: {
      server:   { type: 'string',  default: 'localhost:8765' },
      'agent-id': { type: 'string', default: '42' },
      name:     { type: 'string',  default: 'basic-agent-ts' },
      mode:     { type: 'string',  default: 'echo' },
    },
    strict: false,
  });

  const server  = values['server']   as string;
  const agentId = parseInt(values['agent-id'] as string, 10);
  const name    = values['name']     as string;
  const mode    = values['mode']     as string;

  const uri = `ws://${server}`;
  console.log(`Connecting to ${uri} as agent ${agentId} (${name}) [mode=${mode}]`);

  const ws = new WebSocket(uri);

  ws.on('open', () => {
    ws.send(makeRegister(agentId, name));
    console.log('Registered — waiting for messages…');
  });

  ws.on('message', async (data: WebSocket.RawData) => {
    let envelope: InboundEnvelope;
    try {
      envelope = JSON.parse(data.toString()) as InboundEnvelope;
    } catch {
      console.warn('Non-JSON frame:', data.toString().slice(0, 120));
      return;
    }

    const { type } = envelope;
    if (type === 'ack') {
      console.log('Server ack:', envelope);
    } else if (type === 'message') {
      if (mode === 'qa') {
        await handleQA(ws, envelope);
      } else {
        await handleEcho(ws, envelope);
      }
    } else if (type === 'error') {
      console.error('Server error:', envelope.message);
    }
  });

  ws.on('error', (err) => console.error('WebSocket error:', err));
  ws.on('close', () => console.log('Disconnected.'));

  process.on('SIGINT',  () => { ws.close(); process.exit(0); });
  process.on('SIGTERM', () => { ws.close(); process.exit(0); });
}

main();
