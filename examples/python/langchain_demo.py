#!/usr/bin/env python3
"""
ai_framework_demo.py — Two AI orchestration framework agents communicating via AgentChat encrypted messaging.

Setup:
    pip install ai_framework ai_framework-ai_provider agentchat-sdk

Usage:
    # Start AgentChat server first:
    #   ./build/agentchat_server --port 8080
    python3 ai_framework_demo.py
"""

import asyncio
import os
import sys
import threading
import time
from typing import Any, Optional

try:
    from ai_framework.agents import AgentExecutor, create_tool_calling_agent
    from ai_framework.tools import tool
    from ai_framework_core.prompts import ChatPromptTemplate, MessagesPlaceholder
    from ai_framework_core.messages import HumanMessage, AIMessage
    from ai_framework_ai_provider import ChatAI provider
except ImportError:
    print("Missing deps. Run: pip install ai_framework ai_framework-ai_provider ai_framework-core")
    sys.exit(1)

try:
    import agentchat
except ImportError:
    print("Missing agentchat SDK. Run: pip install agentchat-sdk")
    print("Or from source: cd bindings/python && pip install -e .")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SERVER_URL = os.environ.get("AGENTCHAT_SERVER", "http://localhost:8080")
OPENAI_API_KEY = os.environ.get("OPENAI_API_KEY", "")

AGENT_A_NAME = "ResearchAgent"
AGENT_B_NAME = "SummaryAgent"

# Shared inbox for demo (thread-safe)
_inbox: dict[str, list[str]] = {AGENT_A_NAME: [], AGENT_B_NAME: []}
_inbox_lock = threading.Lock()


# ---------------------------------------------------------------------------
# AgentChat helpers
# ---------------------------------------------------------------------------

class AgentChatClient:
    """Thin wrapper around the AgentChat SDK for a single agent identity."""

    def __init__(self, name: str):
        self.name = name
        self.client = agentchat.Client(server_url=SERVER_URL)
        # Generate or load keypair
        self.keypair = agentchat.generate_keypair()
        self.agent_id = self.client.register(
            name=name,
            public_key=self.keypair.public_key,
        )
        print(f"[{name}] registered — id={self.agent_id[:16]}...")

    def send(self, recipient_id: str, message: str) -> str:
        """Encrypt and send a message; returns message_id."""
        msg_id = self.client.send(
            sender_keypair=self.keypair,
            recipient_id=recipient_id,
            message=message,
        )
        return msg_id

    def receive(self) -> list[str]:
        """Poll for new messages; returns list of plaintext strings."""
        messages = self.client.receive(
            agent_id=self.agent_id,
            keypair=self.keypair,
        )
        return [m.plaintext for m in messages]


# ---------------------------------------------------------------------------
# AI orchestration framework Tools
# ---------------------------------------------------------------------------

def make_send_tool(client: AgentChatClient, recipient_id_ref: list):
    """Factory: returns a AI orchestration framework tool that sends via AgentChat."""

    @tool
    def send_encrypted_message(message: str) -> str:
        """Send an encrypted message to the other agent via AgentChat.
        Use this to pass information or ask questions to your counterpart."""
        recipient_id = recipient_id_ref[0]
        if not recipient_id:
            return "Error: recipient not yet known."
        msg_id = client.send(recipient_id=recipient_id, message=message)
        print(f"[{client.name}] → sent (id={msg_id[:12]}...): {message[:80]}")
        return f"Message sent (id={msg_id[:12]})"

    return send_encrypted_message


def make_receive_tool(client: AgentChatClient):
    """Factory: returns a AI orchestration framework tool that reads from AgentChat inbox."""

    @tool
    def read_encrypted_messages() -> str:
        """Read incoming encrypted messages from the other agent.
        Call this to check if you have received any new messages."""
        msgs = client.receive()
        if not msgs:
            # Also check shared demo inbox
            with _inbox_lock:
                msgs = _inbox.get(client.name, [])
                _inbox[client.name] = []
        if not msgs:
            return "No new messages."
        result = "\n".join(f"- {m}" for m in msgs)
        print(f"[{client.name}] ← received {len(msgs)} message(s)")
        return result

    return read_encrypted_messages


# ---------------------------------------------------------------------------
# Agent builder
# ---------------------------------------------------------------------------

def build_ai_framework_agent(
    name: str,
    system_prompt: str,
    ac_client: AgentChatClient,
    recipient_id_ref: list,
    llm: ChatAI provider,
) -> AgentExecutor:
    send_tool = make_send_tool(ac_client, recipient_id_ref)
    recv_tool = make_receive_tool(ac_client)
    tools = [send_tool, recv_tool]

    prompt = ChatPromptTemplate.from_messages([
        ("system", system_prompt),
        MessagesPlaceholder("chat_history", optional=True),
        ("human", "{input}"),
        MessagesPlaceholder("agent_scratchpad"),
    ])

    agent = create_tool_calling_agent(llm=llm, tools=tools, prompt=prompt)
    executor = AgentExecutor(
        agent=agent,
        tools=tools,
        verbose=True,
        max_iterations=6,
        handle_parsing_errors=True,
    )
    return executor


# ---------------------------------------------------------------------------
# Demo orchestration
# ---------------------------------------------------------------------------

def demo_with_real_llm():
    """Full demo using AI provider. Requires OPENAI_API_KEY."""
    llm = ChatAI provider(model="gpt-4o-mini", api_key=OPENAI_API_KEY)

    ac_a = AgentChatClient(AGENT_A_NAME)
    ac_b = AgentChatClient(AGENT_B_NAME)

    # Cross-wire recipient IDs
    ref_a_to_b = [ac_b.agent_id]
    ref_b_to_a = [ac_a.agent_id]

    agent_a = build_ai_framework_agent(
        name=AGENT_A_NAME,
        system_prompt=(
            "You are ResearchAgent. Your job is to research a given topic and "
            "send a detailed finding to SummaryAgent using send_encrypted_message. "
            "After sending, use read_encrypted_messages to check for a summary reply."
        ),
        ac_client=ac_a,
        recipient_id_ref=ref_a_to_b,
        llm=llm,
    )

    agent_b = build_ai_framework_agent(
        name=AGENT_B_NAME,
        system_prompt=(
            "You are SummaryAgent. Use read_encrypted_messages to retrieve research "
            "from ResearchAgent, then summarise it in 3 bullet points and send it back "
            "using send_encrypted_message."
        ),
        ac_client=ac_b,
        recipient_id_ref=ref_b_to_a,
        llm=llm,
    )

    topic = "the role of Ed25519 signatures in securing AI agent communication"
    print(f"\n=== Demo: ResearchAgent researches '{topic}' ===")
    print(f"=== SummaryAgent will summarise and reply back ===")
    print(f"=== All messages are end-to-end encrypted via AgentChat ===\n")

    # Step 1: ResearchAgent gathers info and sends to SummaryAgent
    agent_a.invoke({"input": f"Research this topic and send your findings to SummaryAgent: {topic}"})

    time.sleep(1)  # allow message delivery

    # Step 2: SummaryAgent reads and replies with a summary
    agent_b.invoke({"input": "Check your messages, summarise the research, and send the summary back."})

    time.sleep(1)

    # Step 3: ResearchAgent reads the summary
    agent_a.invoke({"input": "Check your messages and report what SummaryAgent replied."})

    print("\n=== Demo complete. All messages were end-to-end encrypted. ===")


def demo_mock():
    """
    Mock demo — runs without AI provider key or a live AgentChat server.
    Simulates the two-agent encrypted messaging flow using the shared inbox dict.
    """
    print("\n=== AgentChat + AI orchestration framework Mock Demo ===")
    print("(No OPENAI_API_KEY detected — running in mock/simulation mode)\n")

    topic = "the role of Ed25519 signatures in securing AI agent communication"

    # Simulate ResearchAgent composing a message
    research_finding = (
        f"Research findings on '{topic}':\n"
        "Ed25519 is a high-performance elliptic-curve signature scheme based on "
        "Curve25519. It provides 128-bit security, fast key generation and signing "
        "(~100k ops/sec), and deterministic signatures. In AI agent communication, "
        "it ensures message authenticity — each agent signs outgoing messages with "
        "its private key, and recipients verify with the public key. This prevents "
        "impersonation and message tampering across the network."
    )

    print(f"[{AGENT_A_NAME}] Researched topic: {topic}")
    print(f"[{AGENT_A_NAME}] → Sending encrypted findings to {AGENT_B_NAME}...")
    with _inbox_lock:
        _inbox[AGENT_B_NAME].append(research_finding)
    print(f"[{AGENT_A_NAME}] Message delivered (simulated encryption ✓)\n")

    # Simulate SummaryAgent reading and summarising
    with _inbox_lock:
        received = _inbox[AGENT_B_NAME]
        _inbox[AGENT_B_NAME] = []

    print(f"[{AGENT_B_NAME}] ← Received {len(received)} encrypted message(s)")
    summary = (
        "Summary (3 bullet points):\n"
        "• Ed25519 provides 128-bit security with fast, deterministic signing.\n"
        "• Each AI agent signs messages with its private key for authenticity.\n"
        "• Recipients verify signatures with public keys, blocking impersonation."
    )
    print(f"[{AGENT_B_NAME}] → Sending summary back to {AGENT_A_NAME}...\n")
    with _inbox_lock:
        _inbox[AGENT_A_NAME].append(summary)

    # Simulate ResearchAgent reading the summary
    with _inbox_lock:
        final = _inbox[AGENT_A_NAME]
        _inbox[AGENT_A_NAME] = []

    print(f"[{AGENT_A_NAME}] ← Received summary from {AGENT_B_NAME}:")
    for msg in final:
        print(msg)

    print("\n=== Mock demo complete ===")
    print("To run the full AI orchestration framework demo with real LLMs and encryption:")
    print("  1. Start AgentChat server: ./build/agentchat_server --port 8080")
    print("  2. Set OPENAI_API_KEY=sk-...")
    print("  3. Re-run this script")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if OPENAI_API_KEY:
        demo_with_real_llm()
    else:
        demo_mock()
