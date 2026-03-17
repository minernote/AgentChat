import { useState, useEffect, useRef, useCallback } from 'react';
import type { Agent, Channel, Message, SessionInfo } from '../types';
import {
  mkRegister,
  mkMessage,
  mkChannelMessage,
  mkListAgents,
  mkPing,
  mkTyping,
  mkSealedSend,
  parseFrame,
  type ServerMessage,
  type ServerAgentList,
  type ServerAgentStatus,
  type ServerChannelEvent,
  type ServerSealedSend,
} from '../utils/protocol';
import { useMessages } from './useMessages';

export type ConnectionState = 'disconnected' | 'connecting' | 'connected' | 'error';

export interface UseAgentChatReturn {
  state: ConnectionState;
  error: string | null;
  agents: Agent[];
  channels: Channel[];
  messages: Message[];
  sessions: SessionInfo[];
  sendText: (text: string, to: number) => void;
  sendChannelText: (text: string, channel: string) => void;
  sendSealedMessage: (to: number, ciphertext: string) => void;
  deleteMessage: (msgId: number, to?: number, channel?: string) => void;
  listSessions: () => void;
  kickSession: (fd: number) => void;
  disconnect: () => void;
  dmMessages: (peerId: number) => Message[];
  channelMessages: (channel: string) => Message[];
}

function mkDeleteMessage(messageId: number, to?: number, channel?: string): string {
  return JSON.stringify({ type: 'delete_message', message_id: messageId, to: to ?? 0, channel: channel ?? '' });
}

function mkListSessions(): string {
  return JSON.stringify({ type: 'list_sessions' });
}

function mkKickSession(fd: number): string {
  return JSON.stringify({ type: 'kick_session', fd });
}

export function useAgentChat(
  serverHost: string,
  port: number,
  agentId: number,
  agentName: string,
  enabled: boolean,
): UseAgentChatReturn {
  const [state, setState] = useState<ConnectionState>('disconnected');
  const [error, setError] = useState<string | null>(null);
  const [agents, setAgents] = useState<Agent[]>([]);
  const [channels, setChannels] = useState<Channel[]>([]);
  const [sessions, setSessions] = useState<SessionInfo[]>([]);

  const wsRef = useRef<WebSocket | null>(null);
  const pingIntervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const { messages, addMessage, deleteMessage: _deleteMessage, markRead, dmMessages: _dmMessages, channelMessages: _channelMessages } =
    useMessages();

  const dmMessages = useCallback(
    (peerId: number) => _dmMessages(agentId, peerId),
    [_dmMessages, agentId],
  );

  const channelMessages = useCallback(
    (channel: string) => _channelMessages(channel),
    [_channelMessages],
  );

  const disconnect = useCallback(() => {
    if (pingIntervalRef.current) clearInterval(pingIntervalRef.current);
    wsRef.current?.close();
    wsRef.current = null;
    setState('disconnected');
  }, []);

  useEffect(() => {
    if (!enabled) return;

    const url = `ws://${serverHost}:${port}`;
    setState('connecting');
    setError(null);

    const ws = new WebSocket(url);
    wsRef.current = ws;

    ws.onopen = () => {
      setState('connected');
      ws.send(mkRegister(agentId, agentName));
      ws.send(mkListAgents());
      ws.send(mkListSessions());
      pingIntervalRef.current = setInterval(() => {
        if (ws.readyState === WebSocket.OPEN) ws.send(mkPing());
      }, 25_000);
    };

    ws.onmessage = (ev: MessageEvent<string>) => {
      const frame = parseFrame(ev.data);
      if (!frame) return;

      switch (frame.type) {
        case 'message': {
          const f = frame as ServerMessage;
          addMessage({
            id: f.id,
            from: f.from,
            to: f.to,
            channel: f.channel,
            text: f.text,
            type: 'text',
            timestamp: Date.now(),
            replyTo: f.reply_to,
            signed: f.signed,
          });
          // Auto send read receipt for incoming DMs
          if (f.from && f.from !== agentId && !f.channel && f.id) {
            ws.send(JSON.stringify({ type: 'read_receipt', message_id: f.id, from: f.from }));
          }
          break;
        }
        case 'message_deleted': {
          const f = frame as unknown as { message_id: number; by: number };
          _deleteMessage(f.message_id);
          break;
        }
        case 'read_receipt': {
          const f = frame as unknown as { message_id: number; reader: number };
          markRead(f.message_id);
          break;
        }
        case 'sessions': {
          const f = frame as unknown as { sessions: SessionInfo[] };
          setSessions(f.sessions);
          break;
        }
        case 'kicked': {
          disconnect();
          setError('You were logged out from another device.');
          break;
        }
        case 'agent_status': {
          const f = frame as ServerAgentStatus;
          setAgents(prev => {
            const existing = prev.find(a => a.id === f.agent_id);
            if (existing) {
              return prev.map(a =>
                a.id === f.agent_id ? { ...a, online: f.online, name: f.name || a.name } : a,
              );
            }
            if (f.online) {
              return [...prev, { id: f.agent_id, name: f.name || `agent-${f.agent_id}`, online: true }];
            }
            return prev;
          });
          break;
        }
        case 'agent_list': {
          const f = frame as ServerAgentList;
          setAgents(
            f.agents.map(a => ({
              id: a.id,
              name: a.name,
              online: a.online ?? true,
            })),
          );
          break;
        }
        case 'channel_event': {
          const f = frame as ServerChannelEvent;
          setChannels(prev => {
            const existing = prev.find(c => c.id === f.channel);
            if (!existing) {
              return [...prev, { id: f.channel, name: f.channel, members: f.members ?? [] }];
            }
            if (f.members) {
              return prev.map(c =>
                c.id === f.channel ? { ...c, members: f.members! } : c,
              );
            }
            return prev;
          });
          if (f.event === 'join' || f.event === 'leave') {
            addMessage({
              from: 0,
              channel: f.channel,
              text: `Agent ${f.agent_id} ${f.event}ed #${f.channel}`,
              type: 'system',
              timestamp: Date.now(),
            });
          }
          break;
        }
        case 'sealed_send': {
          const f = frame as ServerSealedSend;
          addMessage({
            from: 0,
            to: f.to_agent_id,
            text: f.content,
            type: 'text',
            timestamp: f.timestamp ?? Date.now(),
            signed: false,
          });
          break;
        }
        case 'error': {
          const msg = (frame as { message?: string }).message ?? 'Unknown error';
          setError(msg);
          break;
        }
        default:
          break;
      }
    };

    ws.onerror = () => {
      setState('error');
      setError('WebSocket connection failed');
    };

    ws.onclose = () => {
      if (pingIntervalRef.current) clearInterval(pingIntervalRef.current);
      setState(prev => (prev === 'error' ? 'error' : 'disconnected'));
    };

    return () => {
      if (pingIntervalRef.current) clearInterval(pingIntervalRef.current);
      ws.close();
    };
  }, [enabled, serverHost, port, agentId, agentName]); // eslint-disable-line react-hooks/exhaustive-deps

  const sendText = useCallback(
    (text: string, to: number) => {
      if (wsRef.current?.readyState !== WebSocket.OPEN) return;
      wsRef.current.send(mkMessage(to, text));
      addMessage({
        from: agentId,
        to,
        text,
        type: 'text',
        timestamp: Date.now(),
      });
    },
    [agentId, addMessage],
  );

  const sendChannelText = useCallback(
    (text: string, channel: string) => {
      if (wsRef.current?.readyState !== WebSocket.OPEN) return;
      wsRef.current.send(mkChannelMessage(channel, text));
      addMessage({
        from: agentId,
        channel,
        text,
        type: 'text',
        timestamp: Date.now(),
      });
    },
    [agentId, addMessage],
  );

  const sendSealedMessage = useCallback(
    (to: number, ciphertext: string) => {
      if (wsRef.current?.readyState !== WebSocket.OPEN) return;
      wsRef.current.send(mkSealedSend(to, ciphertext));
    },
    [],
  );

  const sendTyping = useCallback(
    (to: number) => {
      if (wsRef.current?.readyState !== WebSocket.OPEN) return;
      wsRef.current.send(mkTyping(to));
    },
    [],
  );

  const deleteMessage = useCallback(
    (msgId: number, to?: number, channel?: string) => {
      if (wsRef.current?.readyState !== WebSocket.OPEN) return;
      wsRef.current.send(mkDeleteMessage(msgId, to, channel));
      _deleteMessage(msgId); // optimistic local delete
    },
    [_deleteMessage],
  );

  const listSessions = useCallback(() => {
    if (wsRef.current?.readyState !== WebSocket.OPEN) return;
    wsRef.current.send(mkListSessions());
  }, []);

  const kickSession = useCallback((fd: number) => {
    if (wsRef.current?.readyState !== WebSocket.OPEN) return;
    wsRef.current.send(mkKickSession(fd));
    setSessions(prev => prev.filter(s => s.fd !== fd));
  }, []);

  return {
    state,
    error,
    agents,
    channels,
    messages,
    sessions,
    sendText,
    sendChannelText,
    sendSealedMessage,
    deleteMessage,
    listSessions,
    kickSession,
    disconnect,
    dmMessages,
    channelMessages,
    sendTyping,
  };
}
 dmMessages,
    channelMessages,
    sendTyping,
  };
}
