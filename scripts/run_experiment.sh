#!/bin/bash
set -e

# Load environment variables from .env
if [ -f .env ]; then
    export $(grep -v '^#' .env | xargs)
else
    echo ".env file not found!"
    exit 1
fi

echo "======================================"
echo "🔧 Experiment Configuration"
echo "Workload: $WORKLOAD"
echo "Threads : $THREADS"
echo "Duration: $DURATION"
echo "Keyspace: $KEYSPACE"
echo "Hot Keys: $HOT_KEYS"
echo "--------------------------------------"
echo "DB Cores     : $DB_CORES"
echo "Server Cores : $SERVER_CORES"
echo "LoadGen Cores: $LOADGEN_CORES"
echo "======================================"
echo ""

# 1️⃣ Pin PostgreSQL
echo "📦 Pinning PostgreSQL to cores $DB_CORES..."
PG_MAIN_PID=$(pgrep -u postgres -f 'postgres -D' | head -n 1)
if [ -n "$PG_MAIN_PID" ]; then
    sudo taskset -cp $DB_CORES $PG_MAIN_PID >/dev/null
else
    echo "⚠️  PostgreSQL main process not found! Is it running?"
    exit 1
fi

# 2️⃣ Start server (background)
echo "🚀 Starting KV server pinned to cores $SERVER_CORES..."
cd build
taskset -c $SERVER_CORES ./kvserver > server.log 2>&1 &
SERVER_PID=$!
sleep 3

# 3️⃣ Run load generator
echo "💥 Running load generator pinned to cores $LOADGEN_CORES..."
LOGFILE="results_$(date +%Y%m%d_%H%M%S).log"
taskset -c $LOADGEN_CORES ./loadgen \
  --workload $WORKLOAD \
  --threads $THREADS \
  --duration $DURATION \
  --keyspace $KEYSPACE \
  --hot $HOT_KEYS | tee "$LOGFILE"

# 4️⃣ Stop server
echo "🛑 Stopping server..."
kill $SERVER_PID

echo ""
echo "✅ Experiment complete. Results logged to $LOGFILE"
echo "======================================"
