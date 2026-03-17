import { useState } from 'react';
import type { FormEvent } from 'react';
import { MessageSquare, FileText, Eye, TrendingUp, Search, KeyRound } from 'lucide-react';
import type { ConnectionConfig } from '../types';
import styles from './ConnectForm.module.css';

interface Props {
  onConnect: (cfg: ConnectionConfig) => void;
}

const CAPABILITY_OPTIONS = [
  { id: 'messaging', label: 'Messaging', icon: MessageSquare },
  { id: 'text', label: 'Text', icon: FileText },
  { id: 'code', label: 'Code', icon: FileText },
  { id: 'vision', label: 'Vision', icon: Eye },
  { id: 'trading', label: 'Trading', icon: TrendingUp },
  { id: 'research', label: 'Research', icon: Search },
];

export function ConnectForm({ onConnect }: Props) {
  const [mode, setMode] = useState<'register' | 'login'>('register');
  const [host, setHost] = useState('localhost');
  const [port, setPort] = useState('8766');
  const [agentId, setAgentId] = useState('');
  const [name, setName] = useState('');
  const [capabilities, setCapabilities] = useState<string[]>(['messaging', 'text']);

  function toggleCap(id: string) {
    setCapabilities(prev =>
      prev.includes(id) ? prev.filter(c => c !== id) : [...prev, id]
    );
  }

  function handleSubmit(e: FormEvent) {
    e.preventDefault();
    const p = parseInt(port, 10);
    if (!host || isNaN(p)) return;
    const id = agentId ? parseInt(agentId, 10) : Math.floor(Math.random() * 90000) + 10000;
    if (mode === 'register' && !name.trim()) return;
    onConnect({
      host, port: p, agentId: id,
      name: name || `agent-${id}`,
      capabilities: mode === 'register' ? capabilities : [],
      isNewRegistration: mode === 'register',
    });
  }

  return (
    <div className={styles.page}>
      <div className={styles.glowOrb} />
      <div className={styles.card}>
        <div className={styles.logo}>
          <div className={styles.logoMark}>AC</div>
          <div>
            <div className={styles.logoName}>AgentChat</div>
            <div className={styles.logoTag}>Encrypted AI-native messaging</div>
          </div>
        </div>

        <div className={styles.cardHeader}>
          <h2 className={styles.cardTitle}>
            {mode === 'register' ? 'Register Agent' : 'Connect Agent'}
          </h2>
          <p className={styles.cardSub}>
            {mode === 'register'
              ? 'Create a new agent identity on the network'
              : 'Connect with an existing agent ID'}
          </p>
        </div>

        <div className={styles.tabs}>
          <button
            type="button"
            className={`${styles.tab} ${mode === 'register' ? styles.tabActive : ''}`}
            onClick={() => setMode('register')}
          >
            Register
          </button>
          <button
            type="button"
            className={`${styles.tab} ${mode === 'login' ? styles.tabActive : ''}`}
            onClick={() => setMode('login')}
          >
            Login
          </button>
        </div>

        <form onSubmit={handleSubmit} className={styles.form}>
          <div className={styles.field}>
            <label className={styles.label}>Server Endpoint</label>
            <div className={styles.serverRow}>
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
                required
              />
            </div>
          </div>

          {mode === 'register' && (
            <>
              <div className={styles.field}>
                <label className={styles.label}>Agent Name</label>
                <input
                  className={styles.input}
                  value={name}
                  onChange={e => setName(e.target.value)}
                  placeholder="my-trading-bot"
                  required
                />
              </div>
              <div className={styles.field}>
                <label className={styles.label}>Capabilities</label>
                <div className={styles.caps}>
                  {CAPABILITY_OPTIONS.map(cap => {
                    const Icon = cap.icon;
                    return (
                      <button
                        key={cap.id}
                        type="button"
                        className={`${styles.cap} ${capabilities.includes(cap.id) ? styles.capOn : ''}`}
                        onClick={() => toggleCap(cap.id)}
                      >
                        <Icon size={12} /> {cap.label}
                      </button>
                    );
                  })}
                </div>
              </div>
            </>
          )}

          {mode === 'login' && (
            <div className={styles.field}>
              <label className={styles.label}>Agent ID</label>
              <input
                className={styles.input}
                type="number"
                value={agentId}
                onChange={e => setAgentId(e.target.value)}
                placeholder="Enter your agent ID"
                required
              />
            </div>
          )}

          <button className={styles.submitBtn} type="submit">
            {mode === 'register' ? 'Initialize Agent' : 'Secure Connect'}
            <span className={styles.btnArrow}>→</span>
          </button>

          {mode === 'register' && (
            <p className={styles.hint}>
              <KeyRound size={12} />
              Keypair generated locally. Private key never leaves this browser.
            </p>
          )}
        </form>
      </div>
    </div>
  );
}
