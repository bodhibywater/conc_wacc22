#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity)
      : buckets_(std::max(size_t{1}, initial_capacity)) {
    locks_.reserve(buckets_.size());
    for (size_t i = 0; i < buckets_.size(); ++i) {
      locks_.push_back(std::make_unique<std::mutex>());
    }
  }

  bool Add(T elem) final {
    const size_t hash = Hash(elem);
    size_t observed_capacity = 0;
    {
      const std::scoped_lock<std::mutex> lock(*locks_[hash % locks_.size()]);
      auto& bucket = buckets_[hash % buckets_.size()];
      if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
        return false;
      }
      bucket.push_back(std::move(elem));
      size_.fetch_add(1, std::memory_order_relaxed);
      observed_capacity = buckets_.size();
    }

    if (size_.load(std::memory_order_relaxed) >
        kMaxAverageBucketLength * observed_capacity) {
      Resize(observed_capacity);
    }
    return true;
  }

  bool Remove(T elem) final {
    const size_t hash = Hash(elem);
    const std::scoped_lock<std::mutex> lock(*locks_[hash % locks_.size()]);
    auto& bucket = buckets_[hash % buckets_.size()];
    const auto it = std::find(bucket.begin(), bucket.end(), elem);
    if (it == bucket.end()) {
      return false;
    }
    bucket.erase(it);
    size_.fetch_sub(1, std::memory_order_relaxed);
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    const size_t hash = Hash(elem);
    const std::scoped_lock<std::mutex> lock(*locks_[hash % locks_.size()]);
    const auto& bucket = buckets_[hash % buckets_.size()];
    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  [[nodiscard]] size_t Size() const final {
    return size_.load(std::memory_order_relaxed);
  }

 private:
  static constexpr size_t kMaxAverageBucketLength = 4;

  [[nodiscard]] static size_t Hash(const T& elem) {
    return std::hash<T>{}(elem);
  }

  void Resize(size_t observed_capacity) {
    std::vector<std::unique_lock<std::mutex>> guards;
    guards.reserve(locks_.size());
    for (const auto& stripe : locks_) {
      guards.emplace_back(*stripe);
    }
    if (observed_capacity != buckets_.size() ||
        size_.load(std::memory_order_relaxed) <=
            kMaxAverageBucketLength * buckets_.size()) {
      return;
    }

    std::vector<std::vector<T>> new_buckets(buckets_.size() * 2);
    for (const auto& bucket : buckets_) {
      for (const auto& elem : bucket) {
        new_buckets[Hash(elem) % new_buckets.size()].push_back(elem);
      }
    }
    buckets_.swap(new_buckets);
  }

  std::vector<std::vector<T>> buckets_;
  std::vector<std::unique_ptr<std::mutex>> locks_;
  std::atomic<size_t> size_{0};
};

#endif  // HASH_SET_STRIPED_H
