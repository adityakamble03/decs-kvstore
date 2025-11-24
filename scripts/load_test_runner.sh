#!/bin/bash

# Configuration
LOAD_GEN="/home/aditya/cs744/decs_project/build/loadgen"
OUTPUT_CSV="load_test_results.csv"
SERVER_HOST="127.0.0.1"
SERVER_PORT="8080"
TEST_DURATION=30 # seconds per test
WORKLOAD_TYPE="put_all"  # Options: get_all, put_all, get_popular, get_put

# Check if load generator binary exists
if [ ! -f "$LOAD_GEN" ]; then
    echo "Error: Load generator binary not found at $LOAD_GEN"
    echo "Please build the project first:"
    echo "  ./build.sh"
    exit 1
fi

# Check if server is running
echo "Checking if server is running at $SERVER_HOST:$SERVER_PORT..."
if ! timeout 2 bash -c "cat < /dev/null > /dev/tcp/$SERVER_HOST/$SERVER_PORT" 2>/dev/null; then
    echo "Warning: Cannot connect to server at $SERVER_HOST:$SERVER_PORT"
    echo "Make sure your kvserver is running:"
    echo "  ./build/kvserver"
    echo ""
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Thread counts to test (adjust as needed)
THREAD_COUNTS=(1 5 10 20 30 50)

# Optional: different workload types to test
# Uncomment to test multiple workloads
# WORKLOAD_TYPES=("get_popular" "get_all" "put_all" "get_put")

echo ""
echo "=========================================="
echo "AUTOMATED LOAD TEST SUITE"
echo "=========================================="
echo "Output file: $OUTPUT_CSV"
echo "Test duration: ${TEST_DURATION}s per test"
echo "Thread counts: ${THREAD_COUNTS[@]}"
echo "Workload: $WORKLOAD_TYPE (DISK-BOUND)"
echo "CPU Affinity: Cores 7-15 (9 cores)"
echo "=========================================="
echo ""
echo "NOTE: Load generator pinned to cores 7-15"
echo "      Server should be on cores 0-6!"
echo "      Using disk-bound workload to avoid CPU bottleneck"
echo ""

# Create CSV header only if file doesn't exist
if [ ! -f "$OUTPUT_CSV" ]; then
    echo "workload,threads,duration_sec,total_requests,successful_requests,failed_requests,success_rate_pct,throughput_rps,avg_response_time_ms" > "$OUTPUT_CSV"
    echo "Created new CSV file: $OUTPUT_CSV"
else
    echo "Appending to existing CSV file: $OUTPUT_CSV"
fi

# Function to parse output and extract metrics
parse_output() {
    local output="$1"
    local workload="$2"
    local threads="$3"
    
    # Extract metrics using grep and awk
    local duration=$(echo "$output" | grep "Duration:" | awk '{print $2}')
    local total=$(echo "$output" | grep "Total requests:" | awk '{print $3}')
    local successful=$(echo "$output" | grep "Successful requests:" | awk '{print $3}')
    local failed=$(echo "$output" | grep "Failed requests:" | awk '{print $3}')
    local success_rate=$(echo "$output" | grep "Success rate:" | awk '{print $3}' | tr -d '%')
    local throughput=$(echo "$output" | grep "Average Throughput:" | awk '{print $3}')
    local response_time=$(echo "$output" | grep "Average Response Time:" | awk '{print $4}')
    
    # Write to CSV - ALL ON ONE LINE
    echo "$workload,$threads,$duration,$total,$successful,$failed,$success_rate,$throughput,$response_time" > "$OUTPUT_CSV"
    
    echo "  Throughput: $throughput req/s | Avg Response: $response_time ms"
}

# Main test loop
test_counter=1
total_tests=$((${#THREAD_COUNTS[@]}))

for threads in "${THREAD_COUNTS[@]}"; do
    echo "[$test_counter/$total_tests] Running test with $threads threads..."
    echo "  Pinned to CPU cores 7-15"
    echo "  Testing for ${TEST_DURATION}s..."
    
    # Run the load generator pinned to cores 7-15 and capture output
    output=$(taskset -c 7-15 $LOAD_GEN \
        --host "$SERVER_HOST" \
        --port "$SERVER_PORT" \
        --threads "$threads" \
        --duration "$TEST_DURATION" \
        --workload "$WORKLOAD_TYPE" \
        2>&1)
    
    # Check if the test succeeded
    if [ $? -eq 0 ]; then
        parse_output "$output" "$WORKLOAD_TYPE" "$threads"
        echo "  ✓ Test completed successfully"
    else
        echo "  ✗ Test failed!"
        echo "$output"
    fi
    
    echo ""
    
    # NO COOLDOWN - we want sustained load to hit bottlenecks!
    # The server should stay under pressure
    
    ((test_counter++))
done

echo "=========================================="
echo "ALL TESTS COMPLETED"
echo "=========================================="
echo "Results saved to: $OUTPUT_CSV"
echo ""
echo "Expected pattern for DISK bottleneck:"
echo "  - Server CPU: 30-60% (waiting on disk I/O)"
echo "  - Throughput: Plateaus early"
echo "  - Response time: INCREASES significantly"
echo ""
echo "To plot the results:"
echo "  python3 plot_results.py"
echo ""
echo "To verify disk bottleneck (run during test):"
echo "  iostat -x 2          # Should show high %util"
echo "  mpstat -P ALL 2      # Should show high %iowait"
echo ""

# Optional: Generate a simple Python plotting script
cat > plot_results.py << 'EOF'
#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt

# Read the CSV with proper data types
df = pd.read_csv('load_test_results.csv')

# Convert numeric columns to proper types (in case they're strings)
numeric_cols = ['threads', 'duration_sec', 'total_requests', 'successful_requests', 
                'failed_requests', 'success_rate_pct', 'throughput_rps', 'avg_response_time_ms']
for col in numeric_cols:
    df[col] = pd.to_numeric(df[col], errors='coerce')

# Remove any rows with NaN values
df = df.dropna()

print(f"Loaded {len(df)} test results")
print(f"Workloads: {df['workload'].unique()}")

# Group by workload if multiple workloads exist
workloads = df['workload'].unique()

# Create figure with subplots
fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle('Load Test Results', fontsize=16)

# Plot each workload
for workload in workloads:
    df_workload = df[df['workload'] == workload].sort_values('threads')
    
    # Plot 1: Throughput vs Threads
    ax1.plot(df_workload['threads'], df_workload['throughput_rps'], 
             marker='o', linewidth=2, markersize=8, label=workload)
    
    # Plot 2: Response Time vs Threads
    ax2.plot(df_workload['threads'], df_workload['avg_response_time_ms'], 
             marker='o', linewidth=2, markersize=8, label=workload)
    
    # Plot 3: Success Rate vs Threads
    ax3.plot(df_workload['threads'], df_workload['success_rate_pct'], 
             marker='o', linewidth=2, markersize=8, label=workload)
    
    # Plot 4: Total Requests vs Threads
    ax4.plot(df_workload['threads'], df_workload['total_requests'], 
             marker='o', linewidth=2, markersize=8, label=workload)

# Configure axes
ax1.set_xlabel('Number of Threads')
ax1.set_ylabel('Throughput (req/s)')
ax1.set_title('Throughput vs Concurrency')
ax1.grid(True, alpha=0.3)
ax1.legend()

ax2.set_xlabel('Number of Threads')
ax2.set_ylabel('Avg Response Time (ms)')
ax2.set_title('Response Time vs Concurrency')
ax2.grid(True, alpha=0.3)
ax2.legend()

ax3.set_xlabel('Number of Threads')
ax3.set_ylabel('Success Rate (%)')
ax3.set_title('Success Rate vs Concurrency')
ax3.set_ylim([0, 105])
ax3.grid(True, alpha=0.3)
ax3.legend()

ax4.set_xlabel('Number of Threads')
ax4.set_ylabel('Total Requests')
ax4.set_title('Total Requests vs Concurrency')
ax4.grid(True, alpha=0.3)
ax4.legend()

plt.tight_layout()
plt.savefig('load_test_results.png', dpi=300, bbox_inches='tight')
print("\nPlot saved as 'load_test_results.png'")

# Print summary
print("\n" + "="*50)
print("SUMMARY BY WORKLOAD")
print("="*50)
for workload in workloads:
    df_w = df[df['workload'] == workload]
    print(f"\n{workload.upper()}:")
    print(f"  Max Throughput:    {df_w['throughput_rps'].max():.2f} req/s")
    print(f"  Min Response Time: {df_w['avg_response_time_ms'].min():.2f} ms")
    print(f"  Max Response Time: {df_w['avg_response_time_ms'].max():.2f} ms")
    print(f"  Success Rate:      {df_w['success_rate_pct'].mean():.2f}%")

plt.show()
EOF

chmod +x plot_results.py

echo "Python plotting script generated: plot_results.py"
echo "Run it with: python3 plot_results.py"
echo ""