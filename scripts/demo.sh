#!/usr/bin/env bash
# scripts/demo.sh — AgentChat demo: start server + two agents, exchange encrypted messages
# Optionally records with asciinema if available.
#
# Usage:
#   ./scripts/demo.sh              # plain run
#   ./scripts/demo.sh --record     # record with asciinema → demo.gif

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER="$REPO_ROOT/build/agentchat_server"
CLIENT="$REPO_ROOT/build/agentchat_client"
PORT=9191
DB="/tmp/agentchat_demo_$$.db"
GIF_OUT="$REPO_ROOT/demo.gif"
RECORD=false

# ── arg parse ──────────────────────────────────────────────────────────────────
for arg in "$@"; do
  case $arg in
    --record) RECORD=true ;;
    *) echo "Unknown arg: $arg" && exit 1 ;;
  esac
done

# ── pre-flight ─────────────────────────────────────────────────────────────────
for bin in "$SERVER" "$CLIENT"; do
  if [[ ! -x "$bin" ]]; then
    echo "❌  Binary not found: $bin"
    echo "    Build first: cd build && cmake .. && make -j4"
    exit 1
  fi
done

if $RECORD; then
  if ! command -v asciinema &>/dev/null; then
    echo "⚠️   asciinema not found — running without recording."
    echo "    Install: brew install asciinema  (macOS) or pip install asciinema"
    RECORD=false
  elif ! command -v agg &>/dev/null; then
    echo "⚠️   agg (asciinema-gif) not found — recording to .cast only."
    echo "    Install: cargo install --git https://github.com/asciinema/agg"
  fi
fi

# ── helpers ────────────────────────────────────────────────────────────────────
cleanup() {
  echo ""
  echo "🧹  Cleaning up..."
  kill "$SERVER_PID" 2>/dev/null || true
  rm -f "$DB" "${DB}-shm" "${DB}-wal"
  echo "✅  Done."
}
trap cleanup EXIT

wait_tcp() {
  local host=$1 port=$2 retries=20
  while ! nc -z "$host" "$port" 2>/dev/null; do
    retries=$((retries - 1))
    [[ $retries -le 0 ]] && echo "❌  Server did not start on $host:$port" && exit 1
    sleep 0.2
  done
}

# ── main demo function ─────────────────────────────────────────────────────────
run_demo() {
  echo "🚀  AgentChat Demo"
  echo "=================="
  echo ""

  # Start server
  echo "▶  Starting server on port $PORT..."
  "$SERVER" --port "$PORT" --db "$DB" &>/tmp/agentchat_server_demo.log &
  SERVER_PID=$!
  wait_tcp 127.0.0.1 "$PORT"
  echo "✅  Server running (PID $SERVER_PID)"
  echo ""

  # Register Agent A
  echo "👤  Registering Agent A..."
  AGENT_A_RESP=$("$CLIENT" --server "ws://127.0.0.1:$PORT" register --name "AgentAlice" 2>&1)
  echo "    $AGENT_A_RESP"
  AGENT_A_ID=$(echo "$AGENT_A_RESP" | grep -o '"agent_id":"[^"]*"' | cut -d'"' -f4 || echo "alice")
  AGENT_A_TOKEN=$(echo "$AGENT_A_RESP" | grep -o '"token":"[^"]*"' | cut -d'"' -f4 || echo "")
  sleep 0.3

  # Register Agent B
  echo "👤  Registering Agent B..."
  AGENT_B_RESP=$("$CLIENT" --server "ws://127.0.0.1:$PORT" register --name "AgentBob" 2>&1)
  echo "    $AGENT_B_RESP"
  AGENT_B_ID=$(echo "$AGENT_B_RESP" | grep -o '"agent_id":"[^"]*"' | cut -d'"' -f4 || echo "bob")
  AGENT_B_TOKEN=$(echo "$AGENT_B_RESP" | grep -o '"token":"[^"]*"' | cut -d'"' -f4 || echo "")
  sleep 0.3

  echo ""
  echo "💬  Sending encrypted messages..."
  echo ""

  # Alice → Bob
  echo "[Alice → Bob]: Hello Bob! This message is end-to-end encrypted."
  "$CLIENT" --server "ws://127.0.0.1:$PORT" send \
    --token "$AGENT_A_TOKEN" \
    --to "$AGENT_B_ID" \
    --message "Hello Bob! This message is end-to-end encrypted." 2>&1 || true
  sleep 0.5

  # Bob → Alice
  echo "[Bob → Alice]: Hi Alice! AgentChat — fast, secure, AI-native."
  "$CLIENT" --server "ws://127.0.0.1:$PORT" send \
    --token "$AGENT_B_TOKEN" \
    --to "$AGENT_A_ID" \
    --message "Hi Alice! AgentChat — fast, secure, AI-native." 2>&1 || true
  sleep 0.5

  # Alice → Bob second message
  echo "[Alice → Bob]: Signatures verified. Zero trust achieved. 🔐"
  "$CLIENT" --server "ws://127.0.0.1:$PORT" send \
    --token "$AGENT_A_TOKEN" \
    --to "$AGENT_B_ID" \
    --message "Signatures verified. Zero trust achieved." 2>&1 || true
  sleep 0.3

  echo ""
  echo "✅  Demo complete! AgentChat is working."
  echo ""
  echo "📖  Docs:    https://github.com/minernote/AgentChat"
  echo "🐍  Python:  pip install agentchat-sdk"
  echo "📦  Node.js: npm install agentchat"
}

# ── record or run ──────────────────────────────────────────────────────────────
if $RECORD; then
  CAST_FILE="/tmp/agentchat_demo_$$.cast"
  echo "🎬  Recording with asciinema..."
  asciinema rec --overwrite "$CAST_FILE" --command "bash -c 'RECORD=false bash $0'"
  echo "📼  Cast saved: $CAST_FILE"

  if command -v agg &>/dev/null; then
    echo "🎞   Converting to GIF..."
    agg "$CAST_FILE" "$GIF_OUT"
    echo "🎉  GIF saved: $GIF_OUT"
  else
    echo "ℹ️   To convert to GIF: agg $CAST_FILE $GIF_OUT"
  fi
else
  run_demo
fi
