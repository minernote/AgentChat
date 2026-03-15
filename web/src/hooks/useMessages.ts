import { useState, useCallback } from 'react';
import type { Message } from '../types';

let _msgIdCounter = Date.now();
function nextLocalId() { return ++_msgIdCounter; }

export function useMessages() {
  const [messages, setMessages] = useState<Message[]>([]);

  const addMessage = useCallback((msg: Omit<Message, 'id'> & { id?: number }) => {
    const m: Message = { ...msg, id: msg.id ?? nextLocalId() };
    setMessages(prev => [...prev, m]);
  }, []);

  const clearMessages = useCallback(() => setMessages([]), []);

  /** Messages for a DM conversation with a specific agent */
  const dmMessages = useCallback(
    (myId: number, peerId: number) =>
      messages.filter(
        m =>
          (m.from === myId && m.to === peerId) ||
          (m.from === peerId && m.to === myId),
      ),
    [messages],
  );

  /** Messages for a channel */
  const channelMessages = useCallback(
    (channel: string) => messages.filter(m => m.channel === channel),
    [messages],
  );

  return { messages, addMessage, clearMessages, dmMessages, channelMessages };
}
