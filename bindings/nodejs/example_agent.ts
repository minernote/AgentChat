/**
 * AgentChat Node.js SDK — Example Agent
 * Demonstrates connect, send, and receive using AgentChatClient.
 */
import { AgentChatClient, generateKeyPair } from './index';

async function main() {
  // Generate key material (informational; server handles crypto for MVP)
  const identityKey = generateKeyPair();
  const exchangeKey = generateKeyPair();
  console.log('Identity public key:', identityKey.publicHex);
  console.log('Exchange public key:', exchangeKey.publicHex);

  const MY_AGENT_ID = 1001n;
  const PEER_AGENT_ID = 1002n;

  const client = new AgentChatClient({
    host: '127.0.0.1',
    port: 9000,
    agentId: MY_AGENT_ID,
    identityPrivHex: identityKey.privateHex,
    exchangePrivHex: exchangeKey.privateHex,
  });

  // Register message handler before connecting
  client.onMessage((fromAgentId: bigint, payload: Buffer) => {
    console.log(`Message from agent ${fromAgentId}: ${payload.toString('utf-8')}`);
  });

  try {
    await client.connect();
    console.log('Connected to AgentChat server');

    // Send a message to peer
    await client.sendText(PEER_AGENT_ID, 'Hello from AgentChat Node.js SDK!');
    console.log(`Sent message to agent ${PEER_AGENT_ID}`);

    // Keep alive for 5 seconds to receive any replies
    await new Promise<void>((resolve) => setTimeout(resolve, 5000));
  } catch (err) {
    console.error('Connection error:', err);
  } finally {
    client.disconnect();
    console.log('Disconnected');
  }
}

main().catch(console.error);
