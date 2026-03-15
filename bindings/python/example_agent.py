#!/usr/bin/env python3
"""Example AI agent using the AgentChat Python SDK.

Usage:
    # Terminal 1 — start server
    ./build/agentchat_server

    # Terminal 2 — listening agent (echoes with AI reply)
    python3 example_agent.py --id agent-b --listen

    # Terminal 3 — send a message and exit
    python3 example_agent.py --id agent-a --send-to agent-b --message "Hello!"

Environment:
    AGENTCHAT_LIB   Path to libagentchat_c_api.dylib/.so
"""

from __future__ import annotations

import argparse
import sys
import time

from agentchat import AgentChatClient, generate_keypair


def ai_reply(from_agent: str, text: str) -> str | None:
    """Generate an AI reply.  Replace with a real LLM call, e.g.:

        import ai_provider
        c = ai_provider.AI provider()
        m = c.messages.create(
            model="claude-opus-4-5",
            max_tokens=256,
            messages=[{"role": "user", "content": text}],
        )
        return m.content[0].text
    """
    return f"[AI echo] {text}"


def main() -> None:
    p = argparse.ArgumentParser(description="AgentChat example agent")
    p.add_argument("--server",   default="localhost:8765")
    p.add_argument("--id",       default="example-agent")
    p.add_argument("--privkey",  default="", help="128-char hex private key (generated if empty)")
    p.add_argument("--send-to",  default="", metavar="AGENT_ID")
    p.add_argument("--message",  default="Hello from Python agent!")
    p.add_argument("--listen",   action="store_true")
    args = p.parse_args()

    if args.privkey:
        private_key_hex = args.privkey
        print(f"[{args.id}] Using provided private key")
    else:
        pub, private_key_hex = generate_keypair()
        print(f"[{args.id}] Generated key pair")
        print(f"  public : {pub}")
        print(f"  private: {private_key_hex}")

    client = AgentChatClient(args.server, args.id, private_key_hex)

    def handle_message(from_agent: str, text: str) -> None:
        print(f"\n[{args.id}] << {from_agent}: {text}")
        if args.listen:
            reply = ai_reply(from_agent, text)
            if reply:
                ok = client.send_message(from_agent, reply)
                print(f"[{args.id}] >> {from_agent}: {reply}  [{'sent' if ok else 'FAILED'}]")

    client.on_message(handle_message)

    print(f"[{args.id}] Connecting to {args.server} ...")
    if not client.connect():
        print(f"[{args.id}] Connection failed.", file=sys.stderr)
        sys.exit(1)
    print(f"[{args.id}] Connected.")

    if args.send_to:
        time.sleep(0.3)
        ok = client.send_message(args.send_to, args.message)
        print(f"[{args.id}] >> {args.send_to}: {args.message}  [{'sent' if ok else 'FAILED'}]")
        if not args.listen:
            time.sleep(0.5)
            client.disconnect()
            return

    if args.listen:
        print(f"[{args.id}] Listening ... (Ctrl-C to quit)")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass

    client.disconnect()


if __name__ == "__main__":
    main()
