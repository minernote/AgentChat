import { useState, useEffect, useRef, useCallback } from 'react';
import type { Agent, Channel, Message } from '../types';
import {
  mkRegister,
  mkMessage,
  mkChannelMessage,
  mkListAgents,
  mkPing,
  parseFrame,
  type ServerMessage,
  type ServerAgentList,
  type ServerAgentStatus,
  type ServerChannelEvent,
} from '../utils/protocol';
import { useMessages } from './useMessages';

export type ConnectionState = 'disconnected' | 'connecting' | 'connected' | 'error';

export interface UseAgentChatReturn {
  state: ConnectionState;
  error: string | null;
  agents: Agent[];
  channels: Channel[];
  messages: Message[];
  sendText: (text: string, to: number) => void;
  sendChannelText: (text: string, channel: string) => void;
  disconnect: () => void;
  dmMessages: (peerId: number) => Message[];
  channelMessages: (channel: string) => Message[];
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

  const wsRef = useRef<WebSocket | null>(null);
  const pingIntervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const { messages, addMessage, dmMessages: _dmMessages, channelMessages: _channelMessages } =
    useMessages();

  const dmMessages = useCallback(
    (peerId: number) => _dmMessages(agentId, peerId),
    [_dmMessages, agentId],
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

      // Keepalive ping every 25 s
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
          });
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
          // System message for join/leave
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
      // Optimistically add own message
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

  return {
    state,
    error,
    agents,
    channels,
    messages,
    sendText,
    sendChannelText,
    disconnect,
    dmMessages,
    channelMessages: _channelMessages,
  };
}
