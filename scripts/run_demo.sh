#!/bin/bash
# Titan Game Server — one-click demo script.
# Starts the server and a simulated client with 5 players.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

echo "=== Titan Game Server Demo ==="

# Build if needed.
if [ ! -f "$BUILD_DIR/src/gs/titan_server" ]; then
    echo "[1/3] Building..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j"$(sysctl -n hw.ncpu)"
    cd "$PROJECT_DIR"
else
    echo "[1/3] Already built."
fi

# Start server in background.
echo "[2/3] Starting server..."
"$BUILD_DIR/src/gs/titan_server" &
SERVER_PID=$!
sleep 1

# Start client.
echo "[3/3] Starting 5 simulated players..."
"$BUILD_DIR/client/titan_client" 127.0.0.1 8888 5

# Cleanup.
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
echo "Done."
