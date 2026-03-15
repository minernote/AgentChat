# AgentChat Examples

Real-world AI agent scenarios using AgentChat's encrypted messaging protocol.

All examples demonstrate **agent-to-agent E2EE communication** — the AgentChat server
never sees plaintext message content.

## Python Examples

### 1. AI Research Team (`ai_research_team.py`)
Three specialised AI agents collaborate on a research task:
- **ResearchAgent** — Collects raw data and facts
- **AnalystAgent** — Identifies patterns and insights
- **WriterAgent** — Synthesises an executive report

```bash
python3 examples/python/ai_research_team.py
```

### 2. AI Code Review Pipeline (`ai_code_review.py`)
Parallel AI code review with specialised agents:
- **SecurityAgent** — Finds vulnerabilities (SQL injection, auth issues)
- **PerfAgent** — Identifies performance bottlenecks
- **CoordinatorAgent** — Aggregates findings, produces final report

```bash
python3 examples/python/ai_code_review.py
```

### 3. AI Customer Support Network (`ai_customer_support.py`)
Encrypted customer support routing — PII stays E2EE:
- **TriageAgent** — Classifies and routes incoming queries
- **BillingAgent** — Handles payment issues
- **TechAgent** — Handles technical problems
- **EscalationAgent** — Handles enterprise/complex cases

```bash
python3 examples/python/ai_customer_support.py
```

### 4. AI orchestration framework Integration (`ai_framework_demo.py`)
Two AI orchestration framework agents communicating via AgentChat:
- Supports real AI provider LLM mode and mock mode
- Shows how to integrate AgentChat into existing AI orchestration framework pipelines

```bash
# Mock mode (no API key needed)
python3 examples/python/ai_framework_demo.py

# Real LLM mode
OPENAI_API_KEY=sk-... python3 examples/python/ai_framework_demo.py
```

### 5. Trading privacy-focused messaging Agent (`trading_signal_agent.py`)
privacy-focused messaging generator + execution agent with encrypted coordination.

## Node.js Examples

### Basic Agent (`basic_agent.ts`)
Minimal agent connecting to AgentChat server.

### Multi-Agent Demo (`multi_agent_demo.ts`)
Two agents sending messages back and forth.

## Go Example

### REST API Client (`go/main.go`)
Demonstrates AgentChat REST API from Go:
- Health check, list agents/channels
- Create channel, send message

## Prerequisites

```bash
# Start the AgentChat server
agentchat_server --port 8765

# Python SDK
cd bindings/python && pip install -e .

# Node.js SDK
cd bindings/nodejs && npm install
```

## Key Properties of All Demos

- **Zero plaintext on server** — All inter-agent messages are E2EE encrypted
- **Ed25519 signatures** — Every message is cryptographically signed
- **No central coordinator required** — Agents communicate peer-to-peer
- **Language agnostic** — Python, Node.js, Go, or any language via REST API
