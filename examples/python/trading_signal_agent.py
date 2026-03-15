#!/usr/bin/env python3
"""
AgentChat Trading privacy-focused messaging Agent Example
========================================
An AI agent that:
  - Connects to AgentChat server
  - Listens for trading signal requests from other agents
  - Returns simulated trading signals
  - Demonstrates multi-agent collaboration patterns

privacy-focused messaging request format (send as a message to this agent):
    {"symbol": "BTC", "timeframe": "1h"}

Usage:
    python trading_signal_agent.py --server localhost:8765 --agent-id 10
"""

import argparse
import asyncio
import json
import logging
import os
import random
import signal
import time
from typing import Optional

try:
    import websockets
except ImportError:
    print("Missing dependency: pip install websockets")
    raise

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("trading_signal_agent")

# ---------------------------------------------------------------------------
# Simulated market data
# ---------------------------------------------------------------------------

SUPPORTED_SYMBOLS = {"BTC", "ETH", "SOL", "BNB", "DOGE"}

_BASE_PRICES = {
    "BTC": 65_000.0,
    "ETH": 3_200.0,
    "SOL": 145.0,
    "BNB": 580.0,
    "DOGE": 0.12,
}


def _simulate_price(symbol: str) -> float:
    """Return a slightly randomised price for a symbol."""
    base = _BASE_PRICES.get(symbol.upper(), 100.0)
    noise = random.gauss(0, base * 0.01)
    return round(base + noise, 6)


def _compute_rsi(prices: list, period: int = 14) -> float:
    """Simplified RSI on a synthetic price series."""
    if len(prices) < period + 1:
        return 50.0
    gains, losses = [], []
    for i in range(1, len(prices)):
        delta = prices[i] - prices[i - 1]
        (gains if delta >= 0 else losses).append(abs(delta))
    avg_gain = sum(gains[-period:]) / period if gains else 0
    avg_loss = sum(losses[-period:]) / period if losses else 1e-9
    rs = avg_gain / avg_loss
    return round(100 - 100 / (1 + rs), 2)


def generate_signal(symbol: str, timeframe: str) -> dict:
    """Generate a simulated trading signal for the given symbol/timeframe."""
    symbol = symbol.upper()
    if symbol not in SUPPORTED_SYMBOLS:
        return {"error": f"Unsupported symbol '{symbol}'. Supported: {sorted(SUPPORTED_SYMBOLS)}"}

    # Synthetic price history (30 candles)
    seed_price = _BASE_PRICES.get(symbol, 100.0)
    prices = [seed_price]
    for _ in range(29):
        prices.append(prices[-1] * (1 + random.gauss(0, 0.008)))

    current_price = _simulate_price(symbol)
    rsi = _compute_rsi(prices)

    # Simple signal logic based on RSI
    if rsi < 35:
        direction, confidence = "BUY", round(0.60 + random.random() * 0.30, 2)
    elif rsi > 65:
        direction, confidence = "SELL", round(0.60 + random.random() * 0.30, 2)
    else:
        direction, confidence = "HOLD", round(0.40 + random.random() * 0.25, 2)

    sl_pct = 0.02  # 2% stop-loss
    tp_pct = 0.04  # 4% take-profit
    stop_loss = round(current_price * (1 - sl_pct if direction == "BUY" else 1 + sl_pct), 6)
    take_profit = round(current_price * (1 + tp_pct if direction == "BUY" else 1 - tp_pct), 6)

    return {
        "symbol": symbol,
        "timeframe": timeframe,
        "signal": direction,
        "confidence": confidence,
        "price": current_price,
        "rsi": rsi,
        "stop_loss": stop_loss,
        "take_profit": take_profit,
        "timestamp": int(time.time()),
        "note": "Simulated signal — not financial advice.",
    }


# ---------------------------------------------------------------------------
# Protocol helpers
# ---------------------------------------------------------------------------

def make_register(agent_id: int, name: str) -> str:
    return json.dumps({"type": "register", "agent_id": agent_id, "name": name})


def make_message(to: int, text: str, reply_to: Optional[int] = None) -> str:
    payload: dict = {"type": "message", "to": to, "text": text}
    if reply_to is not None:
        payload["reply_to"] = reply_to
    return json.dumps(payload)


# ---------------------------------------------------------------------------
# Message handler
# ---------------------------------------------------------------------------

async def handle_message(ws, envelope: dict) -> None:
    sender = envelope.get("from")
    msg_id = envelope.get("id")
    text = envelope.get("text", "")
    if sender is None:
        return

    log.info("Request from agent %s: %s", sender, text)

    # Try to parse as JSON signal request
    try:
        req = json.loads(text)
        symbol = req.get("symbol", "BTC")
        timeframe = req.get("timeframe", "1h")
    except (json.JSONDecodeError, AttributeError):
        # Treat the whole text as a symbol name
        symbol = text.strip().upper() or "BTC"
        timeframe = "1h"

    signal_data = generate_signal(symbol, timeframe)
    response_text = json.dumps(signal_data, ensure_ascii=False)

    await ws.send(make_message(sender, response_text, reply_to=msg_id))
    log.info("privacy-focused messaging → agent %s: %s %s", sender, signal_data.get("signal"), symbol)


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

async def run(server: str, agent_id: int, name: str) -> None:
    uri = f"ws://{server}"
    log.info("Trading signal agent connecting to %s (id=%d)", uri, agent_id)

    async with websockets.connect(uri) as ws:
        await ws.send(make_register(agent_id, name))
        log.info("Registered. Supported symbols: %s", sorted(SUPPORTED_SYMBOLS))

        async for raw in ws:
            try:
                envelope = json.loads(raw)
            except json.JSONDecodeError:
                continue

            msg_type = envelope.get("type")
            if msg_type == "message":
                await handle_message(ws, envelope)
            elif msg_type == "error":
                log.error("Server error: %s", envelope.get("message"))


def main() -> None:
    parser = argparse.ArgumentParser(description="AgentChat trading signal agent")
    parser.add_argument("--server", default="localhost:8765")
    parser.add_argument("--agent-id", type=int, default=10)
    parser.add_argument("--name", default="trading-signal-agent")
    args = parser.parse_args()

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, loop.stop)
    try:
        loop.run_until_complete(run(args.server, args.agent_id, args.name))
    finally:
        loop.close()
        log.info("Agent stopped.")


if __name__ == "__main__":
    main()
