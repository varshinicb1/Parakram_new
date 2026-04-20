#!/bin/bash
set -eo pipefail

cd "$(dirname "$0")"

echo "=== Starting Parakram Production Server (Local + Tunnel) ==="

# Stop any running instances
pkill -f "Parakram-Backend" || true
pkill -f "localtunnel" || true

# Load environment
export $(grep -v '^#' .env | xargs)

# Ensure database exists
if [ ! -f "parakram.db" ]; then
    echo "Initializing new SQLite database..."
    touch parakram.db
fi

# Start the Backend Server
echo "Starting Backend on port 8400..."
./Parakram-Backend-v1.0.0-linux-x64 > server.log 2>&1 &
BACKEND_PID=$!

echo "Backend started with PID $BACKEND_PID"

# Start Localtunnel to expose port 8400 to the public internet securely
echo "Starting secure HTTPS tunnel via localtunnel..."
npx localtunnel --port 8400 --subdomain parakram-prod-alpha-v1 > tunnel.log 2>&1 &
TUNNEL_PID=$!

echo "Tunnel started with PID $TUNNEL_PID"

echo "============================================================"
echo "Server is successfully running in the background."
echo "Public URL: https://parakram-prod-alpha-v1.loca.lt"
echo "Logs: tail -f server.log tunnel.log"
echo "============================================================"


