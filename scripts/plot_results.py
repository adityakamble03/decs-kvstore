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

# Create figure with 2 subplots only
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
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