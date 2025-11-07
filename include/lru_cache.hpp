#pragma once
#include <unordered_map>
#include <list>
#include <mutex>
#include <optional>
#include <string>

class LRUCache {
public:
  explicit LRUCache(size_t capacity) : cap_(capacity) {}

  std::optional<std::string> get(const std::string& key) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return std::nullopt;
    touch(it);
    return it->second.first;
  }

  void put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      it->second.first = value;
      touch(it);
      return;
    }

    if (list_.size() == cap_) {
      auto k = list_.back();
      list_.pop_back();
      map_.erase(k);
    }

    list_.push_front(key);
    map_[key] = {value, list_.begin()};
  }

  void erase(const std::string& key) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return;
    list_.erase(it->second.second);
    map_.erase(it);
  }

  size_t size() const {
    std::lock_guard<std::mutex> g(mu_);
    return map_.size();
  }

private:
  using ListIt = std::list<std::string>::iterator;

  void touch(std::unordered_map<std::string, std::pair<std::string, ListIt>>::iterator it) {
    list_.erase(it->second.second);
    list_.push_front(it->first);
    it->second.second = list_.begin();
  }

  size_t cap_;
  mutable std::mutex mu_;
  std::list<std::string> list_;
  std::unordered_map<std::string, std::pair<std::string, ListIt>> map_;
};
