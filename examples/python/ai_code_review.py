"""
ai_code_review.py — Automated AI code review pipeline via AgentChat E2EE

Scenario:
  An AI coding agent writes code, then a separate security review agent
  and a performance review agent independently review it via encrypted
  AgentChat messages. Results are aggregated by a coordinator agent.

  This demo shows how AgentChat enables:
  - Isolated AI agents with specialised expertise
  - Secure, auditable AI-to-AI communication
  - Parallel multi-agent processing
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

# ── Sample code to review ────────────────────────────────────────────────────

SAMPLE_CODE = """
def transfer_funds(user_id: str, amount: float, destination: str) -> bool:
    # TODO: add authentication
    db.execute(f"UPDATE accounts SET balance = balance - {amount} WHERE id = '{user_id}'")
    db.execute(f"UPDATE accounts SET balance = balance + {amount} WHERE id = '{destination}'")
    return True
"""


class ReviewAgent:
    def __init__(self, name: str, specialty: str):
        self.name = name
        self.specialty = specialty
        self.inbox: list = []
        self.client = agentchat.AgentChatClient(server=SERVER, agent_id=name)

    def start(self):
        self.client.connect()
        self.client.register_agent(self.name, ["code-review", "messaging"])
        self.client.on_message(lambda m: self.inbox.append(m))
        print(f"[{self.name}] Online — Specialty: {self.specialty}")

    def wait(self, timeout=8.0) -> Optional[str]:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.inbox: return self.inbox.pop(0).text
            time.sleep(0.1)
        return None

    def stop(self): self.client.disconnect()


def run_code_review_pipeline():
    print("\n" + "="*60)
    print("AI Code Review Pipeline via AgentChat")
    print("All feedback delivered via E2EE encrypted channels")
    print("="*60 + "\n")

    # Agents
    coordinator = ReviewAgent("CoordinatorAgent", "Orchestration")
    security    = ReviewAgent("SecurityAgent",    "Security Vulnerabilities")
    performance = ReviewAgent("PerfAgent",        "Performance & Scalability")
    coder       = ReviewAgent("CoderAgent",       "Code Generation")

    for a in [coordinator, security, performance, coder]:
        a.start()
    time.sleep(0.3)

    print(f"\n[CoderAgent] Submitting code for review...")
    print(f"Code snippet:\n{SAMPLE_CODE}")

    # Coordinator sends code to both reviewers in parallel
    coordinator.client.send_message("SecurityAgent",
        f"REVIEW_REQUEST\n{SAMPLE_CODE}")
    coordinator.client.send_message("PerfAgent",
        f"REVIEW_REQUEST\n{SAMPLE_CODE}")

    # Security review
    sec_req = security.wait()
    if sec_req:
        sec_findings = (
            "🔴 CRITICAL: SQL Injection vulnerability — user_id and destination\n"
            "   are interpolated directly into SQL. Use parameterised queries.\n"
            "🔴 CRITICAL: No authentication check before fund transfer.\n"
            "🟡 MEDIUM: No transaction rollback if second query fails.\n"
            "🟢 PASS: Function returns bool for error handling."
        )
        print(f"\n[SecurityAgent] Findings (encrypted → CoordinatorAgent):")
        print(f"  {sec_findings[:100]}...")
        security.client.send_message("CoordinatorAgent",
            f"SECURITY_REVIEW\n{sec_findings}")

    # Performance review
    perf_req = performance.wait()
    if perf_req:
        perf_findings = (
            "🟡 MEDIUM: Two separate DB writes — should be wrapped in a transaction.\n"
            "🟡 MEDIUM: No connection pooling — each call opens a new connection.\n"
            "🟢 PASS: Simple O(1) operations, no N+1 queries.\n"
            "💡 SUGGESTION: Use async/await for non-blocking I/O."
        )
        print(f"[PerfAgent]  Findings (encrypted → CoordinatorAgent):")
        print(f"  {perf_findings[:100]}...")
        performance.client.send_message("CoordinatorAgent",
            f"PERF_REVIEW\n{perf_findings}")

    # Coordinator aggregates
    reviews = []
    for _ in range(2):
        msg = coordinator.wait(timeout=5)
        if msg: reviews.append(msg)

    if len(reviews) >= 1:
        print("\n" + "="*60)
        print("[CoordinatorAgent] AGGREGATED REVIEW REPORT")
        print("="*60)
        print("STATUS: ❌ CHANGES REQUIRED\n")
        print("BLOCKERS (must fix before merge):")
        print("  1. SQL injection — use parameterised queries")
        print("  2. Add authentication/authorisation check")
        print("  3. Wrap both DB writes in a transaction\n")
        print("All review feedback was transmitted via AgentChat E2EE.")
        print("Private key never left each agent's process. ✅")

    # Send final report to CoderAgent
    coordinator.client.send_message("CoderAgent",
        "CODE_REVIEW_COMPLETE: 2 critical issues, 1 blocker. See report.")

    final = coder.wait(timeout=5)
    if final:
        print("\n[CoderAgent] Review received. Starting fixes...")

    for a in [coordinator, security, performance, coder]:
        a.stop()


if __name__ == "__main__":
    run_code_review_pipeline()
