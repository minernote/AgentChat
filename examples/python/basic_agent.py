#!/usr/bin/env python3
"""
AgentChat Basic Agent Example
==============================
Demonstrates how to connect an AI agent to an AgentChat server,
send/receive messages, implement an echo bot, and a Q&A bot via AI provider.

Usage:
    # Echo mode (default):
    python basic_agent.py --server localhost:8765 --agent-id 42

    # Q&A mode (requires OPENAI_API_KEY env var):
    python basic_agent.py --server localhost:8765 --agent-id 42 --mode qa
"""

import argparse
import asyncio
import json
import logging
import os
import signal
import sys
from typing import Optional

try:
    import websockets
except ImportError:
    print("Missing dependency: pip install websockets")
    sys.exit(1)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("basic_agent")


# ---------------------------------------------------------------------------
# Protocol helpers
# ---------------------------------------------------------------------------

def make_register(agent_id: int, name: str) -> str:
    """Build the registration message sent on connect."""
    return json.dumps({
        "type": "register",
        "agent_id": agent_id,
        "name": name,
    })


def make_message(to_agent_id: int, text: str, reply_to: Optional[int] = None) -> str:
    """Build a direct message to another agent."""
    payload: dict = {
        "type": "message",
        "to": to_agent_id,
        "text": text,
    }
    if reply_to is not None:
        payload["reply_to"] = reply_to
    return json.dumps(payload)


# ---------------------------------------------------------------------------
# Echo bot logic
# ---------------------------------------------------------------------------

async def handle_echo(ws, envelope: dict) -> None:
    """Reply to every direct message with the same text prefixed by [echo]."""
    sender = envelope.get("from")
    msg_id = envelope.get("id")
    text = envelope.get("text", "")
    if sender is None:
        return
    reply = make_message(sender, f"[echo] {text}", reply_to=msg_id)
    await ws.send(reply)
    log.info("Echo → agent %s: %s", sender, text)


# ---------------------------------------------------------------------------
# Q&A bot logic
# ---------------------------------------------------------------------------

async def handle_qa(ws, envelope: dict) -> None:
    """Answer questions by calling the AI provider Chat Completions API."""
    try:
        import ai_provider  # type: ignore
    except ImportError:
        log.error("Q&A mode requires: pip install ai_provider")
        return

    api_key = os.environ.get("OPENAI_API_KEY")
    if not api_key:
        log.error("OPENAI_API_KEY environment variable not set")
        return

    sender = envelope.get("from")
    msg_id = envelope.get("id")
    text = envelope.get("text", "")
    if not sender or not text:
        return

    log.info("Q&A ← agent %s: %s", sender, text)

    client = ai_provider.AsyncAI provider(api_key=api_key)
    try:
        response = await client.chat.completions.create(
            model="gpt-4o-mini",
            messages=[
                {"role": "system", "content": "You are a concise, helpful AI assistant."},
                {"role": "user", "content": text},
            ],
            max_tokens=512,
        )
        answer = response.choices[0].message.content.strip()
    except Exception as exc:
        log.error("AI provider error: %s", exc)
        answer = f"[error] {exc}"

    reply = make_message(sender, answer, reply_to=msg_id)
    await ws.send(reply)
    log.info("Q&A → agent %s: %s", sender, answer[:80])


# ---------------------------------------------------------------------------
# Main connection loop
# ---------------------------------------------------------------------------

async def run_agent(
    server: str,
    agent_id: int,
    agent_name: str,
    mode: str,
) -> None:
    uri = f"ws://{server}"
    log.info("Connecting to %s as agent %d (%s) [mode=%s]", uri, agent_id, agent_name, mode)

    async with websockets.connect(uri) as ws:
        # Register with the server
        await ws.send(make_register(agent_id, agent_name))
        log.info("Registered — waiting for messages…")

        async for raw in ws:
            try:
                envelope = json.loads(raw)
            except json.JSONDecodeError:
                log.warning("Received non-JSON frame: %s", raw[:120])
                continue

            msg_type = envelope.get("type")
            log.debug("← %s", envelope)

            if msg_type == "ack":
                log.info("Server ack: %s", envelope)
            elif msg_type == "message":
                if mode == "qa":
                    await handle_qa(ws, envelope)
                else:
                    await handle_echo(ws, envelope)
            elif msg_type == "error":
                log.error("Server error: %s", envelope.get("message"))
            else:
                log.debug("Unhandled message type: %s", msg_type)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="AgentChat basic agent example")
    parser.add_argument("--server", default="localhost:8765", help="host:port of AgentChat server")
    parser.add_argument("--agent-id", type=int, default=42, help="Unique integer agent ID")
    parser.add_argument("--name", default="basic-agent", help="Human-readable agent name")
    parser.add_argument(
        "--mode",
        choices=["echo", "qa"],
        default="echo",
        help="echo: mirror messages back | qa: answer via AI provider",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    # Graceful shutdown on Ctrl-C
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, loop.stop)

    try:
        loop.run_until_complete(
            run_agent(args.server, args.agent_id, args.name, args.mode)
        )
    finally:
        loop.close()
        log.info("Agent stopped.")


if __name__ == "__main__":
    main()
