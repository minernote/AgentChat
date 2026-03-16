import { useState } from 'react';
import type { Agent, Channel, ChatTarget } from '../types';
import { Identicon } from './Identicon';
import styles from './Sidebar.module.css';

interface Props {
  myId: number;
  agents: Agent[];
  channels: Channel[];
  activeTarget: ChatTarget | null;
  onSelectTarget: (t: ChatTarget) => void;
  onDisconnect: () => void;
}

export function Sidebar({ myId, agents, channels, activeTarget, onSelectTarget, onDisconnect }: Props) {
  const [showSettings, setShowSettings] = useState(false);
  const peers = agents.filter(a => a.id !== myId);
  const onlineCount = peers.filter(a => a.online).length;

  function isActive(t: ChatTarget): boolean {
    if (!activeTarget) return false;
    if (t.kind !== activeTarget.kind) return false;
    if (t.kind === 'agent' && activeTarget.kind === 'agent') return t.id === activeTarget.id;
    if (t.kind === 'channel' && activeTarget.kind === 'channel') return t.id === activeTarget.id;
    return false;
  }

  return (
    <aside className={styles.sidebar}>
      {/* Header — like Telegram's profile area */}
      <div className={styles.header}>
        <div className={styles.myProfile}>
          <div className={styles.myAvatar}>
            <Identicon agentId={myId} size={36} />
          </div>
          <div className={styles.myInfo}>
            <span className={styles.myName}>My Agent</span>
            <span className={styles.myId}>#{myId} · <span className={styles.onlineDot}>●</span> Online</span>
          </div>
        </div>
        <button
          className={styles.settingsBtn}
          onClick={() => setShowSettings(!showSettings)}
          title="Settings"
        >
          ⚙️
        </button>
      </div>

      {/* Settings dropdown */}
      {showSettings && (
        <div className={styles.settingsMenu}>
          <div className={styles.settingsItem}>
            <span>🔒 E2EE Active</span>
          </div>
          <div className={styles.settingsItem}>
            <span>🤖 Agent #{myId}</span>
          </div>
          <div className={styles.settingsDivider} />
          <button className={styles.logoutBtn} onClick={onDisconnect}>
            🚪 Sign out
          </button>
        </div>
      )}

      {/* Search — like Telegram */}
      <div className={styles.search}>
        <input className={styles.searchInput} placeholder="🔍  Search agents or channels..." />
      </div>

      {/* Channels */}
      {channels.length > 0 && (
        <>
          <div className={styles.sectionTitle}>Channels</div>
          {channels.map(ch => (
            <button
              key={ch.id}
              className={`${styles.chatItem} ${isActive({ kind: 'channel', id: ch.id, name: ch.name }) ? styles.active : ''}`}
              onClick={() => onSelectTarget({ kind: 'channel', id: ch.id, name: ch.name })}
            >
              <div className={styles.channelIcon}>#</div>
              <div className={styles.chatInfo}>
                <div className={styles.chatName}>{ch.name} <span className={styles.lockIcon}>🔒</span></div>
                <div className={styles.chatPreview}>Encrypted channel</div>
              </div>
            </button>
          ))}
        </>
      )}

      {/* Direct Messages */}
      <div className={styles.sectionTitle}>
        Direct Messages
        {onlineCount > 0 && <span className={styles.onlineBadge}>{onlineCount} online</span>}
      </div>
      {peers.length === 0 && (
        <div className={styles.emptyState}>
          No agents connected yet
        </div>
      )}
      {peers.map(agent => (
        <button
          key={agent.id}
          className={`${styles.chatItem} ${isActive({ kind: 'agent', id: agent.id, name: agent.name }) ? styles.active : ''}`}
          onClick={() => onSelectTarget({ kind: 'agent', id: agent.id, name: agent.name })}
        >
          <div className={styles.avatar}>
            <Identicon agentId={agent.id} size={38} />
            <div className={agent.online ? styles.onlineDotBadge : styles.offlineDotBadge} />
          </div>
          <div className={styles.chatInfo}>
            <div className={styles.chatName}>{agent.name}</div>
            <div className={styles.chatPreview}>#{agent.id} · {agent.online ? 'Online' : 'Offline'}</div>
          </div>
        </button>
      ))}
    </aside>
  );
}
