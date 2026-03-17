import { useState } from 'react';
import type { FormEvent } from 'react';
import { Send, Paperclip, Timer } from 'lucide-react';
import styles from './MessageInput.module.css';

interface Props {
  onSend: (text: string, ttl?: number) => void;
  placeholder?: string;
}

const TTL_OPTIONS = [
  { label: 'Off', value: 0 },
  { label: '5s', value: 5 },
  { label: '30s', value: 30 },
  { label: '1m', value: 60 },
  { label: '1h', value: 3600 },
  { label: '1d', value: 86400 },
];

export function MessageInput({ onSend, placeholder }: Props) {
  const [text, setText] = useState('');
  const [ttl, setTtl] = useState(0);
  const [showTtl, setShowTtl] = useState(false);

  function handleSubmit(e: FormEvent) {
    e.preventDefault();
    if (!text.trim()) return;
    onSend(text, ttl > 0 ? ttl : undefined);
    setText('');
  }

  return (
    <div className={styles.container}>
      <button className={styles.attachBtn} title="Attach file" type="button">
        <Paperclip size={20} />
      </button>

      {/* TTL picker */}
      <div className={styles.ttlWrap}>
        <button
          type="button"
          className={`${styles.attachBtn} ${ttl > 0 ? styles.ttlActive : ''}`}
          title="Auto-delete timer"
          onClick={() => setShowTtl(p => !p)}
        >
          <Timer size={18} />
          {ttl > 0 && <span className={styles.ttlBadge}>{TTL_OPTIONS.find(o => o.value===ttl)?.label}</span>}
        </button>
        {showTtl && (
          <div className={styles.ttlMenu}>
            <div className={styles.ttlTitle}>Auto-delete timer</div>
            {TTL_OPTIONS.map(opt => (
              <button
                key={opt.value}
                type="button"
                className={`${styles.ttlOpt} ${ttl===opt.value ? styles.ttlOptActive : ''}`}
                onClick={() => { setTtl(opt.value); setShowTtl(false); }}
              >
                {opt.value === 0 ? '🚫 Off' : `⏱ ${opt.label}`}
              </button>
            ))}
          </div>
        )}
      </div>

      <form className={`${styles.form} ${ttl>0 ? styles.formTtl : ''}`} onSubmit={handleSubmit}>
        <input
          className={styles.input}
          value={text}
          onChange={e => setText(e.target.value)}
          placeholder={ttl > 0 ? `${placeholder || 'Write a message...'} (auto-deletes in ${TTL_OPTIONS.find(o=>o.value===ttl)?.label})` : placeholder || 'Write a message...'}
        />
        <button
          className={styles.sendBtn}
          type="submit"
          disabled={!text.trim()}
          title="Send message"
        >
          <Send size={18} />
        </button>
      </form>
    </div>
  );
}
