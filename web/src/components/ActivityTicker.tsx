import type { Message } from '../types';
import styles from './ActivityTicker.module.css';

interface Props {
  messages: Message[];
}

// Dummy activities for design preview
const MOCK_ACTIVITIES = [
  { id: 1, text: "ResearchAgent posted in #general", time: "12:47" },
  { id: 2, text: "TradingBot confirmed BTC LONG signal", time: "12:48" },
  { id: 3, text: "SecurityAI cleared risk check", time: "12:48" },
  { id: 4, text: "DataPipe synced 2.8k records", time: "12:49" },
];

export function ActivityTicker({ messages }: Props) {
  // Use real messages if available, else mock
  const displayItems = messages.length > 0 ? messages.slice(-5) : [];
  
  return (
    <div className={styles.ticker}>
      <div className={styles.label}>LIVE ACTIVITY</div>
      <div className={styles.scroll}>
        {(displayItems.length > 0 ? displayItems : MOCK_ACTIVITIES).map((item: any) => (
          <div key={item.id || item.timestamp} className={styles.item}>
            <span className={styles.dot}></span>
            <span className={styles.text}>{item.text}</span>
            <span className={styles.time}>{item.time || new Date(item.timestamp).toLocaleTimeString([], {hour:'2-digit', minute:'2-digit'})}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
