import { useState, useCallback } from 'react';
import { ConnectForm } from './components/ConnectForm';
import { Sidebar } from './components/Sidebar';
import { ChatWindow } from './components/ChatWindow';
import { MessageInput } from './components/MessageInput';
import { ActivityTicker } from './components/ActivityTicker';
import { useAgentChat } from './hooks/useAgentChat';
import type { ConnectionConfig, ChatTarget } from './types';
import styles from './styles/app.module.css';
import './styles/global.css';

export default function App() {
  const [config, setConfig] = useState<ConnectionConfig | null>(null);
  const [activeTarget, setActiveTarget] = useState<ChatTarget | null>(null);

  const { state, error, agents, channels, sendText, sendChannelText, disconnect, dmMessages, channelMessages } =
    useAgentChat(
      config?.host ?? '',
      config?.port ?? 0,
      config?.agentId ?? 0,
      config?.name ?? '',
      config !== null,
    );

  const handleConnect = useCallback((cfg: ConnectionConfig) => {
    setConfig(cfg);
    setActiveTarget(null);
  }, []);

  const handleDisconnect = useCallback(() => {
    disconnect();
    setConfig(null);
    setActiveTarget(null);
  }, [disconnect]);

  const handleSend = useCallback((text: string) => {
    if (!activeTarget) return;
    if (activeTarget.kind === 'agent') {
      sendText(text, activeTarget.id);
    } else {
      sendChannelText(text, activeTarget.id);
    }
  }, [activeTarget, sendText, sendChannelText]);

  const visibleMessages = activeTarget
    ? activeTarget.kind === 'agent'
      ? dmMessages(activeTarget.id)
      : channelMessages(activeTarget.id)
    : [];

  if (!config || state === 'disconnected') {
    return <ConnectForm onConnect={handleConnect} />;
  }

  return (
    <div className={styles.app}>
      <header className={styles.topnav}>
        <div className={styles.logo}>AC</div>
        <h1 className={styles.appName}>AgentChat</h1>
        <div className={styles.navBadges}>
          <span className={styles.navBadge}>🔒 E2EE Active</span>
          <span className={styles.navBadge}>{agents.filter(a => a.online).length} Agents Online</span>
          <button onClick={handleDisconnect} className={styles.navBadge} style={{cursor:'pointer', border:'none', background:'#fee2e2', color:'#ef4444'}}>
            Disconnect
          </button>
        </div>
      </header>

      <ActivityTicker messages={[]} />

      <main className={styles.body}>
        <Sidebar
          agents={agents}
          channels={channels}
          activeTarget={activeTarget}
          onSelectTarget={setActiveTarget}
          myId={config.agentId}
          onDisconnect={handleDisconnect}
        />
        
        <section className={styles.main}>
          <ChatWindow
            myId={config.agentId}
            target={activeTarget}
            messages={visibleMessages}
          />
          {activeTarget && (
            <MessageInput onSend={handleSend} placeholder={`Message ${activeTarget.name}...`} />
          )}
        </section>
      </main>

      {error && (
        <div style={{position:'fixed', bottom:20, right:20, background:'#ef4444', color:'#fff', padding:'10px 20px', borderRadius:8, boxShadow:'0 4px 12px rgba(0,0,0,0.2)', zIndex:100}}>
          Error: {error}
        </div>
      )}
    </div>
  );
}
