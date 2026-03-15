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

  const allMessages = activeTarget
    ? activeTarget.kind === 'agent'
      ? dmMessages(activeTarget.id)
      : channelMessages(activeTarget.id)
    : [];

  // Collect recent messages across all targets for the ticker
  const recentForTicker = (() => {
    const all: ReturnType<typeof dmMessages> = [];
    for (const a of agents) {
      all.push(...dmMessages(a.id));
    }
    for (const ch of channels) {
      all.push(...channelMessages(ch.id));
    }
    return all.sort((a, b) => b.timestamp - a.timestamp).slice(0, 12);
  })();

  if (!config || state === 'disconnected') {
    return <ConnectForm onConnect={handleConnect} />;
  }

  return (
    <div className={styles.layout}>
      <Sidebar
        myId={config.agentId}
        agents={agents}
        channels={channels}
        activeTarget={activeTarget}
        onSelectTarget={setActiveTarget}
        onDisconnect={handleDisconnect}
      />
      <div className={styles.chatPane}>
        <ActivityTicker agents={agents} recentMessages={recentForTicker} />
        {state === 'connecting' && (
          <div className={styles.statusBar}>
            ◈ CONNECTING TO {config.host}:{config.port}…
          </div>
        )}
        {state === 'error' && (
          <div className={`${styles.statusBar} ${styles.error}`}>
            ⚠ ERROR: {error}
          </div>
        )}
        <ChatWindow
          myId={config.agentId}
          target={activeTarget}
          messages={allMessages}
        />
        <MessageInput
          onSend={handleSend}
          disabled={state !== 'connected' || !activeTarget}
          placeholder={activeTarget ? `encrypt & send to ${activeTarget.name}…` : 'select a target first…'}
        />
      </div>
    </div>
  );
}
