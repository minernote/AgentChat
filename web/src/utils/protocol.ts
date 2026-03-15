/**
 * AgentChat JSON-over-WebSocket protocol helpers.
 *
 * enterprise messaging format (from basic_agent.ts / server):
 *   All frames are UTF-8 JSON objects with a top-level `type` string.
 *
 * Client → Server:
 *   { type: 'register',  agent_id: number, name: string }
 *   { type: 'message',   to: number, text: string, reply_to?: number }
 *   { type: 'list_agents' }
 *   { type: 'join_channel',  channel: string }
 *   { type: 'leave_channel', channel: string }
 *   { type: 'channel_message', channel: string, text: string }
 *   { type: 'ping' }
 *
 * Server → Client:
 *   { type: 'ack',          id: number }
 *   { type: 'message',      id: number, from: number, to: number, text: string }
 *   { type: 'agent_list',   agents: Agent[] }
 *   { type: 'channel_event', event: string, channel: string, agent_id: number }
 *   { type: 'error',        message: string }
 *   { type: 'pong' }
 */


// ── Outbound frame builders ─────────────────────────────────────────────���─

export function mkRegister(agentId: number, name: string): string {
  return JSON.stringify({ type: 'register', agent_id: agentId, name });
}

export function mkMessage(
  to: number,
  text: string,
  replyTo?: number,
): string {
  const obj: Record<string, unknown> = { type: 'message', to, text };
  if (replyTo !== undefined) obj.reply_to = replyTo;
  return JSON.stringify(obj);
}

export function mkChannelMessage(channel: string, text: string): string {
  return JSON.stringify({ type: 'channel_message', channel, text });
}

export function mkListAgents(): string {
  return JSON.stringify({ type: 'list_agents' });
}

export function mkJoinChannel(channel: string): string {
  return JSON.stringify({ type: 'join_channel', channel });
}

export function mkLeaveChannel(channel: string): string {
  return JSON.stringify({ type: 'leave_channel', channel });
}

export function mkPing(): string {
  return JSON.stringify({ type: 'ping' });
}

// ── Inbound frame shapes ───────────────────────────────────────���──────────

export interface ServerAck {
  type: 'ack';
  id: number;
}

export interface ServerMessage {
  type: 'message';
  id: number;
  from: number;
  to?: number;
  channel?: string;
  text: string;
  reply_to?: number;
}

export interface ServerAgentList {
  type: 'agent_list';
  agents: Array<{ id: number; name: string; online?: boolean }>;
}

export interface ServerChannelEvent {
  type: 'channel_event';
  event: 'join' | 'leave' | 'update' | string;
  channel: string;
  agent_id: number;
  members?: number[];
}

export interface ServerError {
  type: 'error';
  message: string;
}

export interface ServerPong {
  type: 'pong';
}

export type ServerFrame =
  | ServerAck
  | ServerMessage
  | ServerAgentList
  | ServerChannelEvent
  | ServerError
  | ServerPong
  | { type: string; [k: string]: unknown };

export function parseFrame(raw: string): ServerFrame | null {
  try {
    return JSON.parse(raw) as ServerFrame;
  } catch {
    return null;
  }
}
