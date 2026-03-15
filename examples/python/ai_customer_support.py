"""
ai_customer_support.py — Encrypted AI customer support network via AgentChat

Scenario:
  A front-facing TriageAgent routes customer queries to specialised AI agents:
  - BillingAgent:     Handles payment and subscription questions
  - TechAgent:        Handles technical troubleshooting
  - EscalationAgent:  Handles complex cases requiring human review

  Why E2EE matters here:
  - Customer data (billing info, PII) stays encrypted between agents
  - No central logging of sensitive conversations
  - Each specialised agent only sees what it needs to
  - Audit trail via Ed25519 message signatures
"""

import time
from typing import Optional

try:
    import agentchat
except ImportError:
    print("Install SDK: cd ../../bindings/python && pip install -e .")
    raise

SERVER = "localhost:8765"

CUSTOMER_QUERIES = [
    {"id": "C001", "type": "billing",
     "message": "My credit card was charged twice for last month. Order #88241."},
    {"id": "C002", "type": "tech",
     "message": "My API key keeps returning 401 errors since yesterday."},
    {"id": "C003", "type": "complex",
     "message": "I need to discuss enterprise pricing and data residency requirements."},
]


class SupportAgent:
    def __init__(self, name: str):
        self.name = name
        self.inbox: list = []
        self.client = agentchat.AgentChatClient(server=SERVER, agent_id=name)

    def start(self):
        self.client.connect()
        self.client.register_agent(self.name, ["text", "support", "messaging"])
        self.client.on_message(lambda m: self.inbox.append(m))

    def wait(self, timeout=5.0) -> Optional[str]:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.inbox: return self.inbox.pop(0).text
            time.sleep(0.1)
        return None

    def stop(self): self.client.disconnect()


def run_support_network():
    print("\n" + "="*60)
    print("AI Customer Support Network via AgentChat E2EE")
    print("Customer PII transmitted only via encrypted channels")
    print("="*60 + "\n")

    triage    = SupportAgent("TriageAgent")
    billing   = SupportAgent("BillingAgent")
    tech      = SupportAgent("TechAgent")
    escalate  = SupportAgent("EscalationAgent")

    for a in [triage, billing, tech, escalate]:
        a.start()
    time.sleep(0.3)

    print("[Agents online] Triage, Billing, Tech, Escalation\n")

    for query in CUSTOMER_QUERIES:
        print(f"--- Customer {query['id']} ({query['type']}) ---")
        print(f"Query: {query['message']}")

        # Triage routes to appropriate agent
        if query["type"] == "billing":
            target = "BillingAgent"
            triage.client.send_message(target,
                f"CUSTOMER:{query['id']}|{query['message']}")
            resp = billing.wait()
            if resp:
                reply = ("[BillingAgent] Duplicate charge confirmed. "
                         "Refund of $29.99 initiated (3-5 business days). "
                         "Case #R-88241 created.")
                print(f"Response: {reply}")
                billing.client.send_message("TriageAgent", reply)

        elif query["type"] == "tech":
            target = "TechAgent"
            triage.client.send_message(target,
                f"CUSTOMER:{query['id']}|{query['message']}")
            resp = tech.wait()
            if resp:
                reply = ("[TechAgent] API key issue detected: rate limit hit. "
                         "Upgraded your plan tier. New key generated. "
                         "Please rotate within 24h.")
                print(f"Response: {reply}")
                tech.client.send_message("TriageAgent", reply)

        elif query["type"] == "complex":
            target = "EscalationAgent"
            triage.client.send_message(target,
                f"ENTERPRISE_INQUIRY|CUSTOMER:{query['id']}|{query['message']}")
            resp = escalate.wait()
            if resp:
                reply = ("[EscalationAgent] Enterprise inquiry logged. "
                         "Account exec will contact within 2 hours. "
                         "Custom SLA + EU data residency available.")
                print(f"Response: {reply}")
                escalate.client.send_message("TriageAgent", reply)

        print()
        time.sleep(0.2)

    print("All 3 customer queries handled via encrypted agent-to-agent routing.")
    print("Customer PII (order numbers, account info) was never in plaintext\n"
          "on the AgentChat server. ✅\n")

    for a in [triage, billing, tech, escalate]:
        a.stop()


if __name__ == "__main__":
    run_support_network()
