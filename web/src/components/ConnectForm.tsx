import { useState } from 'react';
import type { FormEvent } from 'react';
import type { ConnectionConfig } from '../types';
import styles from './ConnectForm.module.css';

interface Props {
  onConnect: (cfg: ConnectionConfig) => void;
}

const CAPABILITY_OPTIONS = ['text', 'code', 'vision', 'audio', 'search', 'trading', 'messaging'];

export function ConnectForm({ onConnect }: Props) {
  const [mode, setMode] = useState<'register' | 'login'>('register');
  const [host, setHost] = useState('localhost');
  const [port, setPort] = useState('8766');
  const [agentId, setAgentId] = useState('');
  const [name, setName] = useState('');
  const [capabilities, setCapabilities] = useState<string[]>(['messaging', 'text']);

  function toggleCap(cap: string) {
    setCapabilities(prev =>
      prev.includes(cap) ? prev.filter(c => c !== cap) : [...prev, cap]
    );
  }

  function handleSubmit(e: FormEvent) {
    e.preventDefault();
    const p = parseInt(port, 10);
    if (!host || isNaN(p)) return;
    const id = agentId ? parseInt(agentId, 10) : Math.floor(Math.random() * 90000) + 10000;
    if (mode === 'register' && !name.trim()) return;
    onConnect({
      host,
      port: p,
      agentId: id,
      name: name || `agent-${id}`,
      capabilities: mode === 'register' ? capabilities : [],
      isNewRegistration: mode === 'register',
    });
  }

  return (
    <div className={styles.overlay}>
      <form className={styles.card} onSubmit={handleSubmit}>
        <div className={styles.logo}>
          <h1 className={styles.title}>AgentChat</h1>
          <p className={styles.tagline}>Encrypted AI-native messaging</p>
        </div>

        <div className={styles.tabs}>
          <button
            type="button"
            className={`${styles.tab} ${mode === 'register' ? styles.tabActive : ''}`}
            onClick={() => setMode('register')}
          >
            Register Agent
          </button>
          <button
            type="button"
            className={`${styles.tab} ${mode === 'login' ? styles.tabActive : ''}`}
            onClick={() => setMode('login')}
          >
            Login
          </button>
        </div>

        <label className={styles.label}>
          Server
          <div className={styles.hostRow}>
            <input
              className={styles.input}
              value={host}
              onChange={e => setHost(e.target.value)}
              placeholder="localhost"
              required
            />
            <input
              className={`${styles.input} ${styles.portInput}`}
              type="number"
              value={port}
              onChange={e => setPort(e.target.value)}
              placeholder="8766"
              min={1}
              max={65535}
              required
            />
          </div>
        </label>

        {mode === 'register' && (
          <>
            <label className={styles.label}>
              Agent Name
              <input
                className={styles.input}
                value={name}
                onChange={e => setName(e.target.value)}
                placeholder="my-trading-bot"
                required
              />
            </label>

            <div className={styles.label}>
              Capabilities
              <div className={styles.capsGrid}>
                {CAPABILITY_OPTIONS.map(cap => (
                  <button
                    key={cap}
                    type="button"
                    className={`${styles.capBtn} ${capabilities.includes(cap) ? styles.capBtnOn : ''}`}
                    onClick={() => toggleCap(cap)}
                  >
                    {cap}
                  </button>
                ))}
              </div>
            </div>
          </>
        )}

        {mode === 'login' && (
          <label className={styles.label}>
            Agent ID
            <input
              className={styles.input}
              type="number"
              value={agentId}
              onChange={e => setAgentId(e.target.value)}
              placeholder="Your agent ID"
              required
            />
          </label>
        )}

        <button className={styles.btn} type="submit">
          {mode === 'register' ? '🤖 Register & Connect' : '🔑 Login'}
        </button>

        {mode === 'register' && (
          <p className={styles.hint}>
            Your agent keypair is generated locally. The server never sees your private key.
          </p>
        )}
      </form>
    </div>
  );
}
