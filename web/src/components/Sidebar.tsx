import type { Agent, Channel, ChatTarget } from '../types';
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
      <div className={styles.header}>
        <div>
          <span className={styles.myIdLabel}>IDENTITY</span>
          <span className={styles.myId}>AGT#{myId}</span>
        </div>
        <button className={styles.disconnectBtn} onClick={onDisconnect} title="Disconnect">
          EXIT
        </button>
      </div>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>
          AGENTS {onlineCount > 0 && `[${onlineCount}]`}
        </div>
        {peers.length === 0 && <div className={styles.empty}>no agents online</div>}
        {peers.map(agent => (
          <button
            key={agent.id}
            className={[styles.item, isActive({ kind: 'agent', id: agent.id, name: agent.name }) ? styles.active : ''].join(' ')}
            onClick={() => onSelectTarget({ kind: 'agent', id: agent.id, name: agent.name })}
          >
            <span className={`${styles.dot} ${agent.online ? styles.dotOnline : styles.dotOffline}`} />
            <span className={styles.itemName}>{agent.name}</span>
            <span className={styles.itemId}>#{agent.id}</span>
          </button>
        ))}
      </section>

      <section className={styles.section}>
        <div className={styles.sectionTitle}>CHANNELS</div>
        {channels.length === 0 && <div className={styles.empty}>no channels</div>}
        {channels.map(ch => (
          <button
            key={ch.id}
            className={[styles.item, isActive({ kind: 'channel', id: ch.id, name: ch.name }) ? styles.active : ''].join(' ')}
            onClick={() => onSelectTarget({ kind: 'channel', id: ch.id, name: ch.name })}
          >
            <span className={styles.channelHash}>#</span>
            <span className={styles.itemName}>{ch.name}</span>
          </button>
        ))}
      </section>
    </aside>
  );
}
