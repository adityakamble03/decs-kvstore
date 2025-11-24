#pragma once
#include <unordered_map>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <functional>
#include <memory>

class LRUCache {
public:
  explicit LRUCache(size_t capacity) : cap_(capacity) {
    // Initialize all shards with reserved space
    size_t shard_capacity = (capacity + NUM_SHARDS - 1) / NUM_SHARDS;
    shards_.reserve(NUM_SHARDS);
    for (size_t i = 0; i < NUM_SHARDS; ++i) {
      shards_.push_back(std::make_unique<Shard>(shard_capacity));
    }
  }

  std::optional<std::string> get(const std::string& key) {
    auto& shard = *get_shard(key);
    std::lock_guard<std::mutex> g(shard.mu);
    
    auto it = shard.map.find(key);
    if (it == shard.map.end()) return std::nullopt;
    
    touch(shard, it);
    return it->second.first;
  }

  void put(const std::string& key, const std::string& value) {
    auto& shard = *get_shard(key);
    std::lock_guard<std::mutex> g(shard.mu);
    
    auto it = shard.map.find(key);
    if (it != shard.map.end()) {
      it->second.first = value;
      touch(shard, it);
      return;
    }

    if (shard.list.size() == shard.capacity) {
      auto k = shard.list.back();
      shard.list.pop_back();
      shard.map.erase(k);
    }

    shard.list.push_front(key);
    shard.map[key] = {value, shard.list.begin()};
  }

  void erase(const std::string& key) {
    auto& shard = *get_shard(key);
    std::lock_guard<std::mutex> g(shard.mu);
    
    auto it = shard.map.find(key);
    if (it == shard.map.end()) return;
    
    shard.list.erase(it->second.second);
    shard.map.erase(it);
  }

  size_t size() const {
    size_t total = 0;
    for (const auto& shard : shards_) {
      std::lock_guard<std::mutex> g(shard->mu);
      total += shard->map.size();
    }
    return total;
  }

private:
  // Number of shards - more shards = less contention
  // 16 is good for 4 cores, 32 for 8+ cores
  static constexpr size_t NUM_SHARDS = 16;
  
  using ListIt = std::list<std::string>::iterator;
  
  struct Shard {
    mutable std::mutex mu;
    std::list<std::string> list;
    std::unordered_map<std::string, std::pair<std::string, ListIt>> map;
    size_t capacity;
    
    explicit Shard(size_t cap) : capacity(cap) {}
  };
  
  std::vector<std::unique_ptr<Shard>> shards_;
  size_t cap_;
  
  // Hash function to determine which shard a key belongs to
  Shard* get_shard(const std::string& key) {
    size_t hash = std::hash<std::string>{}(key);
    return shards_[hash % NUM_SHARDS].get();
  }
  
  const Shard* get_shard(const std::string& key) const {
    size_t hash = std::hash<std::string>{}(key);
    return shards_[hash % NUM_SHARDS].get();
  }
  
  void touch(Shard& shard, 
             typename std::unordered_map<std::string, std::pair<std::string, ListIt>>::iterator it) {
    shard.list.erase(it->second.second);
    shard.list.push_front(it->first);
    it->second.second = shard.list.begin();
  }
};