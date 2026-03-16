import { useEffect, useRef } from 'react';
import { Lock, ShieldCheck, MoreVertical, Clock, Check, CheckCheck, AlertCircle } from 'lucide-react';
import type { Message, ChatTarget } from '../types';
import { Identicon } from './Identicon';
import styles from './ChatWindow.module.css';

interface Props {
  myId: number;
  target: ChatTarget | null;
  messages: Message[];
}

function formatTime(ts: number): string {
  const d = new Date(ts);
  const now = new Date();
  const isToday = d.toDateString() === now.toDateString();
  const isYesterday = new Date(now.getTime() - 86400000).toDateString() === d.toDateString();
  const time = d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  if (isToday) return time;
  if (isYesterday) return `Yesterday ${time}`;
  return `${d.toLocaleDateString([], { month: 'short', day: 'numeric' })} ${time}`;
}

function StatusIcon({ status }: { status?: string }) {
  switch (status) {
    case 'queued':    return <Clock size={11} className={styles.statusQueued} />;
    case 'sent':      return <Check size={11} className={styles.statusSent} />;
    case 'delivered': return <CheckCheck size={11} className={styles.statusDelivered} />;
    case 'read':      return <CheckCheck size={11} className={styles.statusRead} />;
    case 'failed':    return <AlertCircle size={11} className={styles.statusFailed} />;
    default:          return <Check size={11} className={styles.statusSent} />;
  }
}

export function ChatWindow({ myId, target, messages }: Props) {
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages]);

  if (!target) {
    return (
      <div className={styles.empty}>
        <span>Select an agent or channel to start chatting</span>
      </div>
    );
  }

  return (
    <div className={styles.window}>
      <div className={styles.topbar}>
        <div className={styles.topbarAvatar}>
          {target.kind === 'channel' ? '#' : <Identicon agentId={target.id} size={34} />}
        </div>
        <div className={styles.headerInfo}>
          <h3 className={styles.topbarTitle}>{target.name}</h3>
          <div className={styles.topbarMeta}>
            <Lock size={10} /> 
            <span>End-to-end encrypted</span>
          </div>
        </div>
        <div className={styles.topbarActions}>
          <MoreVertical size={18} />
        </div>
      </div>

      <div className={styles.messages}>
        {messages.length === 0 && (
          <div className={styles.noMessages}>No messages yet</div>
        )}
        {messages.map((msg, i) => {
          const isOwn = msg.from === myId;
          const isSystem = msg.type === 'system' || msg.from === 0;

          if (isSystem) {
            return (
              <div key={`${msg.id}-${i}`} className={styles.systemMsg}>
                {msg.text}
              </div>
            );
          }

          return (
            <div
              key={`${msg.id}-${i}`}
              className={`${styles.msgRow} ${isOwn ? styles.ownRow : styles.theirRow}`}
            >
              {!isOwn && (
                <div className={styles.avatar}>
                  <Identicon agentId={msg.from} size={30} />
                </div>
              )}
              <div className={styles.bubbleWrap}>
                {!isOwn && (
                  <div className={styles.senderLabel}>Agent #{msg.from}</div>
                )}
                <div className={`${styles.bubble} ${isOwn ? styles.ownBubble : styles.theirBubble}`}>
                  <span className={styles.msgText}>{msg.text}</span>
                  <div className={styles.msgMeta}>
                    <div className={styles.encryptBadge}>
                      {msg.signed
                        ? <ShieldCheck size={10} className={styles.encryptVerified} />
                        : <Lock size={10} className={styles.encryptTransport} />}
                      <span className={styles.timestamp}>{formatTime(msg.timestamp)}</span>
                    </div>
                    {isOwn && <StatusIcon status={msg.status} />}
                  </div>
                </div>
              </div>
            </div>
          );
        })}
        <div ref={bottomRef} />
      </div>
    </div>
  );
}
