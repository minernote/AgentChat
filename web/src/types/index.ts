// AgentChat TypeScript types

export type MessageType = 'text' | 'image' | 'file' | 'system';
export type DeliveryStatus = 'queued' | 'sent' | 'delivered' | 'read' | 'failed';

export interface Message {
  id: number;
  from: number;        // sender agent_id
  to?: number;         // recipient agent_id (DM)
  channel?: string;    // channel name (group)
  text: string;
  type: MessageType;
  timestamp: number;   // ms since epoch
  replyTo?: number;
  status?: DeliveryStatus; // delivery/read status
}

export interface Agent {
  id: number;
  name: string;
  online: boolean;
}

export interface Channel {
  id: string;
  name: string;
  members: number[];
}

export type ChatTarget =
  | { kind: 'agent'; id: number; name: string }
  | { kind: 'channel'; id: string; name: string };

export interface ConnectionConfig {
  host: string;
  port: number;
  agentId: number;
  name: string;
  capabilities?: string[];
  isNewRegistration?: boolean;
}
