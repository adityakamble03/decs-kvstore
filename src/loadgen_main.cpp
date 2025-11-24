#include "cpp-httplib/httplib.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <mutex>
#include <iomanip>
#include <cstdlib>
#include <string>
#include <sstream>

// ============================================================================
// Configuration
// ============================================================================
struct LoadGenConfig {
  std::string server_host = "127.0.0.1";
  int server_port = 8080;
  int num_threads = 10;
  int duration_seconds = 60;
  std::string workload_type = "get_popular"; // get_all, put_all, get_popular, get_put
  int popular_keys = 100; // for get_popular workload
  double read_ratio = 0.8; // for get_put workload (80% reads, 20% writes)
};

// ============================================================================
// Statistics tracking
// ============================================================================
struct Stats {
  std::atomic<uint64_t> total_requests{0};
  std::atomic<uint64_t> successful_requests{0};
  std::atomic<uint64_t> failed_requests{0};
  std::atomic<uint64_t> total_response_time_us{0};
  
  void record_success(uint64_t response_time_us) {
    total_requests++;
    successful_requests++;
    total_response_time_us += response_time_us;
  }
  
  void record_failure() {
    total_requests++;
    failed_requests++;
  }
  
  void print_summary(int duration_seconds) {
    uint64_t total = total_requests.load();
    uint64_t success = successful_requests.load();
    uint64_t failed = failed_requests.load();
    uint64_t total_time = total_response_time_us.load();
    
    double throughput = static_cast<double>(success) / duration_seconds;
    double avg_response_time_ms = success > 0 ? 
      static_cast<double>(total_time) / success / 1000.0 : 0.0;
    
    std::cout << "\n========================================\n";
    std::cout << "LOAD TEST RESULTS\n";
    std::cout << "========================================\n";
    std::cout << "Duration:              " << duration_seconds << " seconds\n";
    std::cout << "Total requests:        " << total << "\n";
    std::cout << "Successful requests:   " << success << "\n";
    std::cout << "Failed requests:       " << failed << "\n";
    std::cout << "Success rate:          " << std::fixed << std::setprecision(2) 
              << (total > 0 ? (success * 100.0 / total) : 0.0) << "%\n";
    std::cout << "========================================\n";
    std::cout << "Average Throughput:    " << std::fixed << std::setprecision(2) 
              << throughput << " req/s\n";
    std::cout << "Average Response Time: " << std::fixed << std::setprecision(2) 
              << avg_response_time_ms << " ms\n";
    std::cout << "========================================\n";
  }
};

// ============================================================================
// Workload generators
// ============================================================================
class WorkloadGenerator {
public:
  virtual ~WorkloadGenerator() = default;
  virtual void execute(httplib::Client& client, Stats& stats, int thread_id) = 0;
};

// PUT ALL: Only create/delete requests (disk-bound at DB)
class PutAllWorkload : public WorkloadGenerator {
private:
  std::atomic<uint64_t> counter_{0};
  
public:
  void execute(httplib::Client& client, Stats& stats, int thread_id) override {
    std::random_device rd;
    std::mt19937 gen(rd() + thread_id);
    std::uniform_int_distribution<> op_dist(0, 1);
    
    uint64_t key_num = counter_++;
    std::string key = "key_" + std::to_string(thread_id) + "_" + std::to_string(key_num);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
      if (op_dist(gen) == 0) {
        // CREATE
        std::string body = "{\"key\":\"" + key + "\",\"value\":\"value_" + std::to_string(key_num) + "\"}";
        auto res = client.Post("/create", body, "application/json");
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        if (res && res->status == 200) {
          stats.record_success(duration);
        } else {
          stats.record_failure();
        }
      } else {
        // DELETE
        std::string path = "/delete?key=" + key;
        auto res = client.Delete(path.c_str());
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        if (res && (res->status == 200 || res->status == 404)) {
          stats.record_success(duration);
        } else {
          stats.record_failure();
        }
      }
    } catch (...) {
      stats.record_failure();
    }
  }
};

// GET ALL: Only read requests with unique keys (cache misses, disk-bound)
class GetAllWorkload : public WorkloadGenerator {
private:
  std::atomic<uint64_t> counter_{0};
  
public:
  void execute(httplib::Client& client, Stats& stats, int thread_id) override {
    uint64_t key_num = counter_++;
    std::string key = "unique_key_" + std::to_string(thread_id) + "_" + std::to_string(key_num);
    std::string path = "/read?key=" + key;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
      auto res = client.Get(path.c_str());
      
      auto end = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      
      if (res && (res->status == 200 || res->status == 404)) {
        stats.record_success(duration);
      } else {
        stats.record_failure();
      }
    } catch (...) {
      stats.record_failure();
    }
  }
};

// GET POPULAR: Small set of keys repeatedly accessed (cache hits, CPU/memory-bound)
class GetPopularWorkload : public WorkloadGenerator {
private:
  int popular_keys_;
  
public:
  explicit GetPopularWorkload(int popular_keys) : popular_keys_(popular_keys) {}
  
  void execute(httplib::Client& client, Stats& stats, int thread_id) override {
    std::random_device rd;
    std::mt19937 gen(rd() + thread_id);
    std::uniform_int_distribution<> key_dist(0, popular_keys_ - 1);
    
    int key_num = key_dist(gen);
    std::string key = "popular_key_" + std::to_string(key_num);
    std::string path = "/read?key=" + key;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
      auto res = client.Get(path.c_str());
      
      auto end = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      
      if (res && (res->status == 200 || res->status == 404)) {
        stats.record_success(duration);
      } else {
        stats.record_failure();
      }
    } catch (...) {
      stats.record_failure();
    }
  }
};

// GET+PUT: Mixed workload (some cache hits, some DB access)
class GetPutWorkload : public WorkloadGenerator {
private:
  double read_ratio_;
  std::atomic<uint64_t> counter_{0};
  
public:
  explicit GetPutWorkload(double read_ratio) : read_ratio_(read_ratio) {}
  
  void execute(httplib::Client& client, Stats& stats, int thread_id) override {
    std::random_device rd;
    std::mt19937 gen(rd() + thread_id);
    std::uniform_real_distribution<> op_dist(0.0, 1.0);
    std::uniform_int_distribution<> key_dist(0, 9999);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
      if (op_dist(gen) < read_ratio_) {
        // READ
        int key_num = key_dist(gen);
        std::string key = "mixed_key_" + std::to_string(key_num);
        std::string path = "/read?key=" + key;
        
        auto res = client.Get(path.c_str());
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        if (res && (res->status == 200 || res->status == 404)) {
          stats.record_success(duration);
        } else {
          stats.record_failure();
        }
      } else {
        // CREATE
        uint64_t key_num = counter_++;
        std::string key = "mixed_key_" + std::to_string(key_num % 10000);
        std::string body = "{\"key\":\"" + key + "\",\"value\":\"value_" + std::to_string(key_num) + "\"}";
        
        auto res = client.Post("/create", body, "application/json");
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        if (res && res->status == 200) {
          stats.record_success(duration);
        } else {
          stats.record_failure();
        }
      }
    } catch (...) {
      stats.record_failure();
    }
  }
};

// ============================================================================
// Worker thread function
// ============================================================================
void worker_thread(int thread_id, const LoadGenConfig& config, 
                   WorkloadGenerator* workload, Stats& stats,
                   std::atomic<bool>& should_stop) {
  // Create HTTP client for this thread
  httplib::Client client(config.server_host, config.server_port);
  client.set_connection_timeout(5, 0);
  client.set_read_timeout(10, 0);
  client.set_write_timeout(10, 0);
  
  std::cout << "Thread " << thread_id << " started\n";
  
  // Closed-loop: send request, wait for response, repeat
  while (!should_stop.load()) {
    workload->execute(client, stats, thread_id);
  }
  
  std::cout << "Thread " << thread_id << " stopped\n";
}

// ============================================================================
// Warmup phase: populate data for workloads
// ============================================================================
void warmup(const LoadGenConfig& config) {
  std::cout << "Starting warmup phase...\n";
  
  if (config.workload_type == "get_popular") {
    httplib::Client client(config.server_host, config.server_port);
    client.set_connection_timeout(5, 0);
    
    // Populate popular keys
    for (int i = 0; i < config.popular_keys; i++) {
      std::string key = "popular_key_" + std::to_string(i);
      std::string body = "{\"key\":\"" + key + "\",\"value\":\"popular_value_" + std::to_string(i) + "\"}";
      auto res = client.Post("/create", body, "application/json");
      if (!res || res->status != 200) {
        std::cerr << "Warning: Failed to create popular key " << i << "\n";
      }
    }
    std::cout << "Populated " << config.popular_keys << " popular keys\n";
  } else if (config.workload_type == "get_put") {
    httplib::Client client(config.server_host, config.server_port);
    client.set_connection_timeout(5, 0);
    
    // Populate some initial keys for mixed workload
    for (int i = 0; i < 5000; i++) {
      std::string key = "mixed_key_" + std::to_string(i);
      std::string body = "{\"key\":\"" + key + "\",\"value\":\"mixed_value_" + std::to_string(i) + "\"}";
      client.Post("/create", body, "application/json");
    }
    std::cout << "Populated 5000 initial keys for mixed workload\n";
  }
  
  std::cout << "Warmup complete\n\n";
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
  LoadGenConfig config;
  
  // Parse command-line arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      config.server_host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      config.server_port = std::atoi(argv[++i]);
    } else if (arg == "--threads" && i + 1 < argc) {
      config.num_threads = std::atoi(argv[++i]);
    } else if (arg == "--duration" && i + 1 < argc) {
      config.duration_seconds = std::atoi(argv[++i]);
    } else if (arg == "--workload" && i + 1 < argc) {
      config.workload_type = argv[++i];
    } else if (arg == "--popular-keys" && i + 1 < argc) {
      config.popular_keys = std::atoi(argv[++i]);
    } else if (arg == "--read-ratio" && i + 1 < argc) {
      config.read_ratio = std::atof(argv[++i]);
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n";
      std::cout << "Options:\n";
      std::cout << "  --host <host>           Server host (default: 127.0.0.1)\n";
      std::cout << "  --port <port>           Server port (default: 8080)\n";
      std::cout << "  --threads <n>           Number of concurrent threads (default: 10)\n";
      std::cout << "  --duration <seconds>    Test duration in seconds (default: 60)\n";
      std::cout << "  --workload <type>       Workload type: put_all, get_all, get_popular, get_put (default: get_popular)\n";
      std::cout << "  --popular-keys <n>      Number of popular keys for get_popular (default: 100)\n";
      std::cout << "  --read-ratio <ratio>    Read ratio for get_put workload (default: 0.8)\n";
      std::cout << "  --help                  Show this help message\n";
      return 0;
    }
  }
  
  // Print configuration
  std::cout << "========================================\n";
  std::cout << "LOAD GENERATOR CONFIGURATION\n";
  std::cout << "========================================\n";
  std::cout << "Server:         " << config.server_host << ":" << config.server_port << "\n";
  std::cout << "Threads:        " << config.num_threads << "\n";
  std::cout << "Duration:       " << config.duration_seconds << " seconds\n";
  std::cout << "Workload:       " << config.workload_type << "\n";
  if (config.workload_type == "get_popular") {
    std::cout << "Popular keys:   " << config.popular_keys << "\n";
  }
  if (config.workload_type == "get_put") {
    std::cout << "Read ratio:     " << (config.read_ratio * 100) << "%\n";
  }
  std::cout << "========================================\n\n";
  
  // Create appropriate workload generator
  WorkloadGenerator* workload = nullptr;
  if (config.workload_type == "put_all") {
    workload = new PutAllWorkload();
  } else if (config.workload_type == "get_all") {
    workload = new GetAllWorkload();
  } else if (config.workload_type == "get_popular") {
    workload = new GetPopularWorkload(config.popular_keys);
  } else if (config.workload_type == "get_put") {
    workload = new GetPutWorkload(config.read_ratio);
  } else {
    std::cerr << "Unknown workload type: " << config.workload_type << "\n";
    return 1;
  }
  
  // Warmup phase
  // warmup(config);
  
  // Statistics
  Stats stats;
  
  // Control flag for threads
  std::atomic<bool> should_stop{false};
  
  // Launch worker threads
  std::vector<std::thread> threads;
  auto test_start = std::chrono::steady_clock::now();
  
  for (int i = 0; i < config.num_threads; i++) {
    threads.emplace_back(worker_thread, i, std::ref(config), 
                        workload, std::ref(stats), std::ref(should_stop));
  }
  
  std::cout << "Load test running";
  std::cout.flush();
  
  // Run for specified duration
  for (int i = 0; i < config.duration_seconds; i++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (i % 10 == 0) {
      std::cout << ".";
      std::cout.flush();
    }
  }
  std::cout << "\n";
  
  // Stop all threads
  should_stop.store(true);
  
  // Wait for all threads to finish
  for (auto& t : threads) {
    t.join();
  }
  
  auto test_end = std::chrono::steady_clock::now();
  int actual_duration = std::chrono::duration_cast<std::chrono::seconds>(test_end - test_start).count();
  
  // Print results
  stats.print_summary(actual_duration);
  
  delete workload;
  return 0;
}