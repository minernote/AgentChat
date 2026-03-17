import { useState } from 'react';
import { Settings, LogOut, Hash, Lock, Search, Circle, Monitor, X } from 'lucide-react';
import type { Agent, Channel, ChatTarget, SessionInfo } from '../types';
import { Identicon } from './Identicon';
import styles from './Sidebar.module.css';

interface Props {
  myId: number;
  agents: Agent[];
  channels: Channel[];
  activeTarget: ChatTarget | null;
  onSelectTarget: (t: ChatTarget) => void;
  onDisconnect: () => void;
  sessions: SessionInfo[];
  onKickSession: (fd: number) => void;
}

export function Sidebar({ myId, agents, channels, activeTarget, onSelectTarget, onDisconnect, sessions, onKickSession }: Props) {
  const [showSettings, setShowSettings] = useState(false);
  const [showSessions, setShowSessions] = useState(false);
  const [search, setSearch] = useState('');
  const peers = agents.filter(a => a.id !== myId);
  const onlineCount = peers.filter(a => a.online).length;

  function isActive(t: ChatTarget): boolean {
    if (!activeTarget) return false;
    if (t.kind !== activeTarget.kind) return false;
    if (t.kind === 'agent' && activeTarget.kind === 'agent') return t.id === activeTarget.id;
    if (t.kind === 'channel' && activeTarget.kind === 'channel') return t.id === activeTarget.id;
    return false;
  }

  const filteredPeers = peers.filter(a =>
    !search || a.name.toLowerCase().includes(search.toLowerCase())
  );
  const filteredChannels = channels.filter(ch =>
    !search || ch.name.toLowerCase().includes(search.toLowerCase())
  );

  return (
    <aside className={styles.sidebar}>
      <div className={styles.header}>
        <div className={styles.myProfile}>
          <div className={styles.myAvatar}>
            <Identicon agentId={myId} size={36} />
          </div>
          <div className={styles.myInfo}>
            <span className={styles.myName}>My Agent</span>
            <span className={styles.myId}>#{myId}</span>
          </div>
        </div>
        <button
          className={styles.settingsBtn}
          onClick={() => setShowSettings(!showSettings)}
          title="Settings"
        >
          <Settings size={18} strokeWidth={1.8} />
        </button>
      </div>

      {showSettings && (
        <div className={styles.settingsMenu}>
          <div className={styles.settingsItem}>
            <Lock size={14} />
            <span>E2EE Active</span>
          </div>
          <div className={styles.settingsItem}>
            <Circle size={14} />
            <span>Agent #{myId}</span>
          </div>
          <div className={styles.settingsDivider} />
          <button className={styles.settingsItemBtn} onClick={() => setShowSessions(!showSessions)}>
            <Monitor size={14} />
            <span>Active Sessions ({sessions.length})</span>
          </button>
          {showSessions && sessions.map(s => (
            <div key={s.fd} className={styles.sessionItem}>
              <Monitor size={12} />
              <span className={styles.sessionName}>{s.name}{s.self ? ' (this device)' : ''}</span>
              {!s.self && (
                <button className={styles.kickBtn} onClick={() => onKickSession(s.fd)} title="Log out this device">
                  <X size={12} />
                </button>
              )}
            </div>
          ))}
          <div className={styles.settingsDivider} />
          <button className={styles.logoutBtn} onClick={onDisconnect}>
            <LogOut size={14} />
            <span>Sign out</span>
          </button>
        </div>
      )}

      <div className={styles.search}>
        <Search size={14} className={styles.searchIcon} />
        <input
          className={styles.searchInput}
          placeholder="Search..."
          value={search}
          onChange={e => setSearch(e.target.value)}
        />
      </div>

      <div className={styles.scrollArea}>
        {filteredChannels.length > 0 && (
          <>
            <div className={styles.sectionTitle}>Channels</div>
            {filteredChannels.map(ch => (
              <button
                key={ch.id}
                className={`${styles.chatItem} ${isActive({ kind: 'channel', id: ch.id, name: ch.name }) ? styles.active : ''}`}
                onClick={() => onSelectTarget({ kind: 'channel', id: ch.id, name: ch.name })}
              >
                <div className={styles.channelIcon}>
                  <Hash size={15} strokeWidth={2} />
                </div>
                <div className={styles.chatInfo}>
                  <div className={styles.chatName}>
                    {ch.name}
                    <Lock size={10} className={styles.lockIcon} />
                  </div>
                  <div className={styles.chatPreview}>Encrypted channel</div>
                </div>
              </button>
            ))}
          </>
        )}

        <div className={styles.sectionTitle}>
          Direct Messages
          {onlineCount > 0 && <span className={styles.onlineBadge}>{onlineCount}</span>}
        </div>
        {filteredPeers.length === 0 && (
          <div className={styles.emptyState}>No agents connected</div>
        )}
        {filteredPeers.map(agent => (
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
      </div>
    </aside>
  );
}
