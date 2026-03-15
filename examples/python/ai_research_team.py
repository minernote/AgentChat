"""
ai_research_team.py — Multi-agent AI research collaboration via AgentChat E2EE

Scenario:
  3 AI agents collaborate on researching a topic:
  - ResearchAgent:   Searches and collects raw information
  - AnalystAgent:    Processes data, identifies patterns
  - WriterAgent:     Synthesizes findings into a report

  All communication between agents is end-to-end encrypted.
  No central coordinator — agents communicate peer-to-peer.

Run:
  # Start AgentChat server first
  agentchat_server --port 8765

  # Then run this demo
  python3 ai_research_team.py
"""

import time
import threading
from typing import Optional

try:
    import agentchat
except ImportError:
    print("Install SDK: cd ../../bindings/python && pip install -e .")
    raise

SERVER = "localhost:8765"


class AIAgent:
    """A simple AI agent that communicates via AgentChat."""

    def __init__(self, name: str, role: str, instructions: str):
        self.name = name
        self.role = role
        self.instructions = instructions
        self.client = agentchat.AgentChatClient(
            server=SERVER,
            agent_id=name,
        )
        self.inbox: list[agentchat.Message] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._poll, daemon=True)

    def start(self):
        self.client.connect()
        self.client.register_agent(self.name, ["text", "reasoning", "messaging"])
        self._thread.start()
        print(f"[{self.name}] Online — Role: {self.role}")

    def stop(self):
        self._stop.set()
        self.client.disconnect()

    def send(self, to: str, message: str):
        print(f"[{self.name} → {to}]: {message[:80]}{'...' if len(message) > 80 else ''}")
        self.client.send_message(to, message)

    def wait_for_message(self, timeout: float = 10.0) -> Optional[str]:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.inbox:
                return self.inbox.pop(0).text
            time.sleep(0.1)
        return None

    def _poll(self):
        self.client.on_message(lambda msg: self.inbox.append(msg))


def run_research_team(topic: str):
    print(f"\n{'='*60}")
    print(f"AI Research Team — Topic: {topic}")
    print(f"All messages encrypted via Double Ratchet E2EE")
    print(f"{'='*60}\n")

    # Create agents
    researcher = AIAgent(
        "ResearchAgent",
        "Data Collector",
        "Search for key facts and data points on the given topic."
    )
    analyst = AIAgent(
        "AnalystAgent",
        "Data Analyst",
        "Analyse collected data, identify patterns and insights."
    )
    writer = AIAgent(
        "WriterAgent",
        "Report Writer",
        "Synthesise findings into a concise executive report."
    )

    # Start all agents
    for agent in [researcher, analyst, writer]:
        agent.start()
    time.sleep(0.5)

    # Step 1: ResearchAgent collects data and sends to AnalystAgent
    research_data = (
        f"Research findings on '{topic}':\n"
        "• Market size: $4.2B (2025), projected $18.7B by 2030 (CAGR 34.8%)\n"
        "• Top players: AI provider, AI provider, Google DeepMind, Meta AI\n"
        "• Key trend: Shift from single-model to multi-agent orchestration\n"
        "• Risk factor: Regulatory uncertainty in EU (AI Act effective Aug 2026)\n"
        "• Opportunity: Enterprise adoption accelerating post-GPT-5 launch"
    )
    researcher.send("AnalystAgent", research_data)

    # Step 2: AnalystAgent processes and sends to WriterAgent
    raw = analyst.wait_for_message(timeout=5)
    if raw:
        analysis = (
            "Analysis complete:\n"
            "📊 BULLISH SIGNALS: 34.8% CAGR, enterprise momentum, multi-agent shift\n"
            "⚠️  RISK: EU AI Act compliance costs may slow EU deployment\n"
            "🎯 KEY INSIGHT: Companies with secure multi-agent infrastructure\n"
            "   (like AgentChat) are positioned to capture enterprise segment\n"
            "💡 RECOMMENDATION: Focus on B2B security-first positioning"
        )
        analyst.send("WriterAgent", analysis)

    # Step 3: WriterAgent synthesises final report
    analysis_result = writer.wait_for_message(timeout=5)
    if analysis_result:
        report = (
            f"=== EXECUTIVE REPORT: {topic} ===\n\n"
            "SUMMARY: The AI agent market is entering exponential growth phase.\n"
            "Multi-agent systems requiring secure, encrypted communication\n"
            "represent the highest-value infrastructure play for 2026-2030.\n\n"
            "RECOMMENDATION: AgentChat's E2EE protocol positions it as the\n"
            "privacy-focused messaging for AI agents — a critical infrastructure layer.\n"
        )
        print(f"\n[WriterAgent] Final Report:\n{report}")
        writer.send("ResearchAgent", report)

    # Confirm ResearchAgent received the report
    final = researcher.wait_for_message(timeout=5)
    if final:
        print("[ResearchAgent] Report received. Research cycle complete. ✅")

    print(f"\n✅ All messages were end-to-end encrypted via AgentChat.")
    print(f"   The server never saw plaintext content.\n")

    for agent in [researcher, analyst, writer]:
        agent.stop()


if __name__ == "__main__":
    run_research_team("AI Agent Infrastructure Market 2026")
