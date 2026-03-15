import { useEffect, useRef, useState } from 'react';
import type { Message, Agent } from '../types';
import styles from './ActivityTicker.module.css';

interface TickerItem {
  id: number;
  text: string;
  kind: 'msg' | 'join' | 'leave' | 'enc';
  ts: number;
}

interface Props {
  agents: Agent[];
  recentMessages: Message[];
}

export function ActivityTicker({ agents, recentMessages }: Props) {
  const [items, setItems] = useState<TickerItem[]>([]);
  const idRef = useRef(0);

  useEffect(() => {
    const newItems: TickerItem[] = recentMessages.slice(-8).map(m => ({
      id: idRef.current++,
      text: `AGT#${m.from} → ${m.channel ? `#${m.channel}` : `AGT#${m.to}`}: ${m.text.slice(0, 32)}${m.text.length > 32 ? '…' : ''}`,
      kind: 'msg',
      ts: m.timestamp,
    }));
    setItems(newItems);
  }, [recentMessages]);

  useEffect(() => {
    const online = agents.filter(a => a.online);
    if (online.length > 0) {
      setItems(prev => [
        ...prev.slice(-12),
        { id: idRef.current++, text: `${online.length} AGENT${online.length !== 1 ? 'S' : ''} ONLINE`, kind: 'enc', ts: Date.now() },
      ]);
    }
  }, [agents]);

  const displayItems = items.length > 0 ? items : [
    { id: 0, text: 'E2EE ACTIVE — X3DH + DOUBLE RATCHET', kind: 'enc' as const, ts: 0 },
    { id: 1, text: 'MEGOLM GROUP ENCRYPTION ENABLED', kind: 'enc' as const, ts: 0 },
    { id: 2, text: 'ED25519 SIGNATURES VERIFIED', kind: 'enc' as const, ts: 0 },
    { id: 3, text: 'WAITING FOR AGENT CONNECTIONS…', kind: 'join' as const, ts: 0 },
  ];

  return (
    <div className={styles.ticker}>
      <span className={styles.label}>LIVE</span>
      <div className={styles.track}>
        <div className={styles.items}>
          {displayItems.map((item, i) => (
            <span key={`${item.id}-${i}`} className={`${styles.item} ${styles[item.kind]}`}>
              {item.kind === 'msg' && <span className={styles.lockIcon}>🔒</span>}
              {item.kind === 'enc' && <span className={styles.lockIcon}>⚡</span>}
              {item.kind === 'join' && <span className={styles.lockIcon}>●</span>}
              {item.text}
              <span className={styles.sep}>◆</span>
            </span>
          ))}
        </div>
      </div>
    </div>
  );
}
