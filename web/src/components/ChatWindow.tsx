import { useEffect, useRef } from 'react';
import type { Message, ChatTarget } from '../types';
import styles from './ChatWindow.module.css';
import { Identicon } from './Identicon';

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
  const time = d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false });
  if (isToday) return time;
  if (isYesterday) return `YESTERDAY ${time}`;
  return `${d.toLocaleDateString([], { month: 'short', day: 'numeric' })} ${time}`;
}

function StatusIcon({ status }: { status?: string }) {
  switch (status) {
    case 'queued':    return <span className={styles.statusQueued}    title="Queued">○</span>;
    case 'sent':      return <span className={styles.statusSent}      title="Sent">◉</span>;
    case 'delivered': return <span className={styles.statusDelivered} title="Delivered">◉◉</span>;
    case 'read':      return <span className={styles.statusRead}      title="Read">◉◉</span>;
    case 'failed':    return <span className={styles.statusFailed}    title="Failed">⚠</span>;
    default:          return <span className={styles.statusSent}      title="Sent">◉</span>;
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
        <span className={styles.emptyIcon}>⬡</span>
        <span className={styles.emptyText}>select agent or channel</span>
      </div>
    );
  }

  return (
    <div className={styles.window}>
      <div className={styles.topbar}>
        <div className={styles.topbarLeft}>
          {target.kind === 'channel' ? (
            <span className={styles.topbarTitle}># {target.name}</span>
          ) : (
            <span className={styles.topbarTitle}>
              {target.name}{' '}
              <span className={styles.topbarId}>#{target.id}</span>
            </span>
          )}
          <span className={`${styles.topbarBadge} ${styles.badgeE2ee}`}>🔒 E2EE</span>
        </div>
      </div>

      <div className={styles.messages}>
        {messages.length === 0 && (
          <div className={styles.noMessages}>— NO MESSAGES YET —</div>
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
                  {String(msg.from).slice(-2)}
                </div>
              )}
              <div className={styles.bubble__wrap}>
                {!isOwn && (
                  <div className={styles.senderLabel}>AGT#{msg.from}</div>
                )}
                <div className={`${styles.bubble} ${isOwn ? styles.ownBubble : styles.theirBubble}`}>
                  <span className={styles.msgText}>{msg.text}</span>
                  <span className={styles.msgMeta}>
                    <span className={styles.encBadge}>🔒</span>
                    <span className={styles.timestamp}>{formatTime(msg.timestamp)}</span>
                    {isOwn && <StatusIcon status={msg.status} />}
                  </span>
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
             </span>
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
