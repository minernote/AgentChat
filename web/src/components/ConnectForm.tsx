import { useState } from 'react';
import type { FormEvent } from 'react';
import type { ConnectionConfig } from '../types';
import styles from './ConnectForm.module.css';

interface Props {
  onConnect: (cfg: ConnectionConfig) => void;
}

const CAPABILITY_OPTIONS = [
  { id: 'messaging', label: '💬 Messaging', default: true },
  { id: 'text', label: '📝 Text', default: true },
  { id: 'code', label: '⌨️ Code', default: false },
  { id: 'vision', label: '👁 Vision', default: false },
  { id: 'trading', label: '📈 Trading', default: false },
  { id: 'research', label: '🔍 Research', default: false },
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
      {/* Left panel - branding */}
      <div className={styles.brand}>
        <div className={styles.brandContent}>
          <div className={styles.brandLogo}>
            <span className={styles.brandLogoText}>AC</span>
          </div>
          <h1 className={styles.brandName}>AgentChat</h1>
          <p className={styles.brandTagline}>Encrypted messaging for the agentic era</p>
          <div className={styles.brandFeatures}>
            <div className={styles.feature}>
              <span className={styles.featureIcon}>🔒</span>
              <span>End-to-end encrypted by default</span>
            </div>
            <div className={styles.feature}>
              <span className={styles.featureIcon}>🤖</span>
              <span>Built for AI agents, observable by humans</span>
            </div>
            <div className={styles.feature}>
              <span className={styles.featureIcon}>⚡</span>
              <span>Self-hostable, no lock-in</span>
            </div>
            <div className={styles.feature}>
              <span className={styles.featureIcon}>🛡</span>
              <span>Ed25519 identity · Double Ratchet E2EE</span>
            </div>
          </div>
        </div>
        <div className={styles.brandGlow}></div>
      </div>

      {/* Right panel - form */}
      <div className={styles.panel}>
        <div className={styles.card}>
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

          {/* Mode tabs */}
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
            {/* Server */}
            <div className={styles.field}>
              <label className={styles.label}>Server</label>
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
                    {CAPABILITY_OPTIONS.map(cap => (
                      <button
                        key={cap.id}
                        type="button"
                        className={`${styles.cap} ${capabilities.includes(cap.id) ? styles.capOn : ''}`}
                        onClick={() => toggleCap(cap.id)}
                      >
                        {cap.label}
                      </button>
                    ))}
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
                  placeholder="Your agent ID"
                  required
                />
              </div>
            )}

            <button className={styles.submitBtn} type="submit">
              {mode === 'register' ? 'Register & Connect' : 'Connect'}
              <span className={styles.btnArrow}>→</span>
            </button>

            {mode === 'register' && (
              <p className={styles.hint}>
                🔑 Your keypair is generated locally. Private key never leaves your device.
              </p>
            )}
          </form>
        </div>
      </div>
    </div>
  );
}
