import { useState } from 'react';
import type { FormEvent } from 'react';
import { Send, Paperclip } from 'lucide-react';
import styles from './MessageInput.module.css';

interface Props {
  onSend: (text: string) => void;
  placeholder?: string;
}

export function MessageInput({ onSend, placeholder }: Props) {
  const [text, setText] = useState('');

  function handleSubmit(e: FormEvent) {
    e.preventDefault();
    if (!text.trim()) return;
    onSend(text);
    setText('');
  }

  return (
    <div className={styles.container}>
      <button className={styles.attachBtn} title="Attach file">
        <Paperclip size={20} />
      </button>
      <form className={styles.form} onSubmit={handleSubmit}>
        <input
          className={styles.input}
          value={text}
          onChange={e => setText(e.target.value)}
          placeholder={placeholder || "Write a message..."}
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
