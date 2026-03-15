/**
 * AgentChat Node.js SDK — Integration Tests
 * Tests the SDK API surface; loopback test requires server binary.
 *
 * Usage:
 *   node tests/test_nodejs_sdk.mjs
 */

import * as net from 'net';
import { fileURLToPath } from 'url';
import { dirname, resolve } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const sdkPath   = resolve(__dirname, '../bindings/nodejs/dist/index.js');

let sdk;
try {
  sdk = await import(sdkPath);
} catch (e) {
  console.error(`Cannot load SDK from ${sdkPath}: ${e.message}`);
  console.error('Run: cd bindings/nodejs && npm run build');
  process.exit(1);
}

const { AgentChatClient, generateKeyPair } = sdk;

let pass = 0, fail = 0;

async function run(name, fn) {
  process.stdout.write(`  ${name} ... `);
  try {
    await fn();
    console.log('PASS'); pass++;
  } catch (e) {
    console.log(`FAIL: ${e.message}`); fail++;
  }
}

// ── generateKeyPair ──────────────────────────────────────────────────────────

await run('generateKeyPair_returns_object', () => {
  const kp = generateKeyPair();
  if (!kp || typeof kp !== 'object') throw new Error('expected object');
  if (typeof kp.publicHex  !== 'string') throw new Error('publicHex must be string');
  if (typeof kp.privateHex !== 'string') throw new Error('privateHex must be string');
});

await run('generateKeyPair_hex_valid', () => {
  const { publicHex, privateHex } = generateKeyPair();
  if (!/^[0-9a-f]+$/i.test(publicHex))  throw new Error('publicHex not valid hex');
  if (!/^[0-9a-f]+$/i.test(privateHex)) throw new Error('privateHex not valid hex');
});

await run('generateKeyPair_unique', () => {
  const keys = Array.from({ length: 5 }, () => generateKeyPair().privateHex);
  if (new Set(keys).size !== 5) throw new Error('keypairs must be unique');
});

await run('generateKeyPair_32bytes', () => {
  const { publicHex, privateHex } = generateKeyPair();
  if (publicHex.length  !== 64) throw new Error(`publicHex length ${publicHex.length}, want 64`);
  if (privateHex.length !== 64) throw new Error(`privateHex length ${privateHex.length}, want 64`);
});

// ── Client construction ──────────────────────────────────────────────────────

await run('client_construction', () => {
  const { privateHex } = generateKeyPair();
  const c = new AgentChatClient({
    host: '127.0.0.1', port: 19999, agentId: 42n,
    identityPrivHex: privateHex, exchangePrivHex: privateHex,
  });
  if (!c) throw new Error('client is null');
  c.disconnect();
});

await run('client_construction_minimal', () => {
  const c = new AgentChatClient({ host: '127.0.0.1', port: 19999, agentId: 1n });
  if (!c) throw new Error('client is null');
  c.disconnect();
});

await run('client_on_message_registration', () => {
  const c = new AgentChatClient({ host: '127.0.0.1', port: 19999, agentId: 1n });
  c.onMessage((fromId, payload) => {});
  c.disconnect();
});

await run('client_connect_refused', async () => {
  const c = new AgentChatClient({ host: '127.0.0.1', port: 19997, agentId: 99n });
  try {
    await c.connect();
    c.disconnect();
    throw new Error('expected connection to fail');
  } catch (e) {
    if (e.message === 'expected connection to fail') throw e;
    // connection refused = expected
  }
});

// ── Protocol framing ─────────────────────────────────────────────────────────

await run('sendText_frame_structure', async () => {
  // Mock server that completes auth handshake then captures SEND_MESSAGE
  const frames = [];
  const PT_HELLO          = 0x01;
  const PT_HELLO_ACK      = 0x02;
  const PT_AUTH_CHALLENGE = 0x06;
  const PT_AUTH_OK        = 0x04;
  const PT_SEND_MESSAGE   = 0x20;

  function encodeFrame(type, payload) {
    const f = Buffer.allocUnsafe(4 + 1 + payload.length);
    f.writeUInt32BE(1 + payload.length, 0);
    f.writeUInt8(type, 4);
    payload.copy(f, 5);
    return f;
  }

  const server = net.createServer(sock => {
    let buf = Buffer.alloc(0);
    sock.on('data', d => {
      buf = Buffer.concat([buf, d]);
      while (buf.length >= 5) {
        const len  = buf.readUInt32BE(0);
        if (buf.length < 4 + len) break;
        const type = buf.readUInt8(4);
        const body = buf.slice(5, 4 + len);
        buf = buf.slice(4 + len);

        if (type === PT_HELLO) {
          // Send HELLO_ACK (32 zero bytes ephemeral pub) + AUTH_CHALLENGE
          const challenge = Buffer.alloc(32, 0xAB);
          sock._challenge = challenge;
          const helloAck = encodeFrame(PT_HELLO_ACK, Buffer.alloc(32));
          // pack_blob: [4 BE len][data]
          const blobLen = Buffer.allocUnsafe(4); blobLen.writeUInt32BE(32, 0);
          const chPayload = Buffer.concat([blobLen, challenge]);
          const authChallenge = encodeFrame(PT_AUTH_CHALLENGE, chPayload);
          sock.write(Buffer.concat([helloAck, authChallenge]));
        } else if (type === 0x07) { // AUTH_RESPONSE
          // Just send AUTH_OK unconditionally
          sock.write(encodeFrame(PT_AUTH_OK, Buffer.alloc(0)));
        } else if (type === PT_SEND_MESSAGE) {
          frames.push(body);
        }
      }
    });
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port;

  const c = new AgentChatClient({ host: '127.0.0.1', port, agentId: 1n });
  await c.connect();
  await c.sendText(2n, 'hello');
  await new Promise(r => setTimeout(r, 200));
  c.disconnect();
  await new Promise(r => server.close(r));

  if (frames.length === 0) throw new Error('no SEND_MESSAGE frames received');
  const body = frames[0];
  if (body.length < 8 + 5) throw new Error(`frame too short: ${body.length}`);
  const toId    = body.readBigUInt64BE(0);
  const payload = body.slice(8);
  if (toId !== 2n)                        throw new Error(`toAgentId ${toId}, want 2n`);
  if (payload.toString('utf8') !== 'hello') throw new Error(`payload '${payload}', want 'hello'`);
});

// ── Loopback integration (requires server binary) ────────────────────────────

await run('two_clients_exchange_message', async () => {
  const { existsSync } = await import('fs');
  const serverBin = resolve(__dirname, '../build/agentchat_server');
  if (!existsSync(serverBin)) {
    process.stdout.write('(skipped — server binary not found) ');
    return;
  }

  // Pick free port
  const port = await new Promise(res => {
    const tmp = net.createServer();
    tmp.listen(0, '127.0.0.1', () => { const p = tmp.address().port; tmp.close(() => res(p)); });
  });

  const { spawn } = await import('child_process');
  // Pick a free ws port
  const wsPort = await new Promise(res => {
    const tmp = net.createServer();
    tmp.listen(0, '127.0.0.1', () => { const p = tmp.address().port; tmp.close(() => res(p)); });
  });
  const proc = spawn(serverBin, ['--port', String(port), '--ws-port', String(wsPort), '--db', `/tmp/ac_test_${port}.db`], { stdio: 'ignore' });
  await new Promise(r => setTimeout(r, 400));

  try {
    const { privateHex: idPrivA } = generateKeyPair();
    const { privateHex: exPrivA } = generateKeyPair();
    const { privateHex: idPrivB } = generateKeyPair();
    const { privateHex: exPrivB } = generateKeyPair();

    const received = [];
    let resolveArrived;
    const arrived = new Promise(r => { resolveArrived = r; });

    const clientB = new AgentChatClient({
      host: '127.0.0.1', port, agentId: 2n,
      identityPrivHex: idPrivB, exchangePrivHex: exPrivB,
    });
    clientB.onMessage((fromId, payload) => { received.push(payload); resolveArrived(); });
    await clientB.connect();

    const clientA = new AgentChatClient({
      host: '127.0.0.1', port, agentId: 1n,
      identityPrivHex: idPrivA, exchangePrivHex: exPrivA,
    });
    await clientA.connect();
    await new Promise(r => setTimeout(r, 100));
    await clientA.sendText(2n, 'hello from A');

    const timeout = new Promise((_, rej) => setTimeout(() => rej(new Error('timeout waiting for message')), 3000));
    await Promise.race([arrived, timeout]);

    clientA.disconnect();
    clientB.disconnect();

    if (received.length === 0) throw new Error('no messages received by clientB');
    const text = received[0].toString('utf8');
    if (!text.includes('hello from A')) throw new Error(`unexpected payload: '${text}'`);
  } finally {
    proc.kill();
    await new Promise(r => proc.on('close', r));
  }
});

// ── Results ───────────────────────────────────────────────────────────────────

console.log('='.repeat(40));
console.log(`Results: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
