import { useState } from 'react';
import type { KeyboardEvent, FormEvent } from 'react';
import styles from './MessageInput.module.css';

interface Props {
  onSend: (text: string) => void;
  disabled?: boolean;
  placeholder?: string;
}

export function MessageInput({ onSend, disabled, placeholder }: Props) {
  const [text, setText] = useState('');

  function handleSubmit(e: FormEvent) {
    e.preventDefault();
    const trimmed = text.trim();
    if (!trimmed || disabled) return;
    onSend(trimmed);
    setText('');
  }

  function handleKeyDown(e: KeyboardEvent<HTMLTextAreaElement>) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      const trimmed = text.trim();
      if (!trimmed || disabled) return;
      onSend(trimmed);
      setText('');
    }
  }

  return (
    <form className={styles.bar} onSubmit={handleSubmit}>
      <div className={styles.inputWrap}>
        <span className={styles.encIndicator}>🔒</span>
        <textarea
          className={styles.input}
          value={text}
          onChange={e => setText(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder={placeholder ?? 'encrypted message…'}
          disabled={disabled}
          rows={1}
        />
      </div>
      <button
        className={styles.sendBtn}
        type="submit"
        disabled={disabled || !text.trim()}
        title="Send encrypted"
      >
        <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
          <path d="M2 21l21-9L2 3v7l15 2-15 2v7z" />
        </svg>
      </button>
    </form>
  );
}
