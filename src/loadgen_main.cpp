#include "cpp-httplib/httplib.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <fstream>

using clk = std::chrono::steady_clock;

struct Args {
  std::string host = "127.0.0.1";
  int port = 8080;
  std::string workload = "get_popular"; // get_all | put_all | get_popular | mixed
  int threads = 10;
  int seconds = 30;
  int keyspace = 100000; // used for unique keys
  int hot_keys = 128;    // used for get_popular
};

static Args parse(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; i++) {
    std::string s = argv[i];
    auto next = [&](int& /*j*/) { return std::string(argv[++i]); };
    if (s == "--host") a.host = next(i);
    else if (s == "--port") a.port = std::stoi(next(i));
    else if (s == "--workload") a.workload = next(i);
    else if (s == "--threads") a.threads = std::stoi(next(i));
    else if (s == "--duration") a.seconds = std::stoi(next(i));
    else if (s == "--keyspace") a.keyspace = std::stoi(next(i));
    else if (s == "--hot") a.hot_keys = std::stoi(next(i));
  }
  return a;
}

struct Stats {
  std::atomic<uint64_t> succ{0}, fail{0};
  std::atomic<uint64_t> micros{0};
};

static std::string random_value(std::mt19937& rng) {
  static const char al[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::uniform_int_distribution<int> L(10, 50), C(0, (int)(sizeof(al) - 2));
  int len = L(rng);
  std::string s;
  s.reserve(len);
  for (int i = 0; i < len; i++) s.push_back(al[C(rng)]);
  return s;
}

int main(int argc, char** argv) {
  Args a = parse(argc, argv);
  Stats st;
  std::atomic<bool> stop{false};
  auto start_time = clk::now();

  std::cout << "Running workload '" << a.workload << "' with " << a.threads << " threads\n";

  auto worker = [&](int tid) {
    httplib::Client cli(a.host.c_str(), a.port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    std::mt19937 rng((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count() + tid);

    auto do_create = [&](const std::string& k, const std::string& v) {
      std::string body = "{\"key\":\"" + k + "\",\"value\":\"" + v + "\"}";
      return cli.Post("/create", body, "application/json");
    };
    auto do_read = [&](const std::string& k) { return cli.Get(("/read?key=" + k).c_str()); };
    auto do_delete = [&](const std::string& k) { return cli.Delete(("/delete?key=" + k).c_str()); };

    std::uniform_int_distribution<int> keyu(0, a.keyspace - 1);
    std::uniform_int_distribution<int> hotu(0, a.hot_keys - 1);
    std::uniform_int_distribution<int> mix(0, 99);

    while (!stop.load(std::memory_order_relaxed)) {
      std::string key, val;
      int op = 0; // 0 = GET, 1 = PUT, 2 = DELETE

      if (a.workload == "get_all") {
        key = "k_" + std::to_string(keyu(rng));
        op = 0;
      } else if (a.workload == "put_all") {
        key = "k_" + std::to_string(keyu(rng));
        val = random_value(rng);
        op = 1;
      } else if (a.workload == "get_popular") {
        key = "hot_" + std::to_string(hotu(rng));
        op = 0;
      } else { // mixed workload
        int r = mix(rng);
        if (r < 70) op = 0;         // 70% GET
        else if (r < 95) op = 1;    // 25% PUT
        else op = 2;                // 5% DELETE
        key = "k_" + std::to_string(keyu(rng));
        val = random_value(rng);
      }

      auto t1 = clk::now();
      httplib::Result res;

      if (op == 0) res = do_read(key);
      else if (op == 1) res = do_create(key, val);
      else res = do_delete(key);

      auto t2 = clk::now();

      // Check for success
      if (res && res->status >= 200 && res->status < 300)
        st.succ++;
      else
        st.fail++;

      st.micros += std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();

    }
  };

  std::vector<std::thread> threads;
  threads.reserve(a.threads);
  for (int i = 0; i < a.threads; i++) threads.emplace_back(worker, i);

  std::this_thread::sleep_for(std::chrono::seconds(a.seconds));
  stop.store(true);
  for (auto& t : threads) t.join();

  double total_s = std::chrono::duration<double>(clk::now() - start_time).count();
  double throughput = st.succ.load() / total_s;
  double avg_ms = (st.succ > 0) ? (st.micros.load() / 1000.0 / st.succ.load()) : 0.0;

  std::cout << "----------------------------------------\n";
  std::cout << "Throughput: " << throughput << " req/s\n";
  std::cout << "Avg latency: " << avg_ms << " ms\n";
  std::cout << "Success: " << st.succ << " | Fail: " << st.fail << "\n";
  std::cout << "----------------------------------------\n";
  return 0;
}
