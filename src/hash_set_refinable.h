#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

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
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(size_t initial_capacity)
      : state_(std::make_shared<TableState>(
            std::max(size_t{1}, initial_capacity))) {}

  bool Add(T elem) final {
    const size_t hash = Hash(elem);
    size_t observed_capacity = 0;
    {
      auto access = AcquireBucket(hash);
      auto& bucket = access.state->buckets[hash % access.state->buckets.size()];
      if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
        return false;
      }
      bucket.push_back(std::move(elem));
      size_.fetch_add(1, std::memory_order_relaxed);
      observed_capacity = access.state->buckets.size();
    }

    if (size_.load(std::memory_order_relaxed) >
        kMaxAverageBucketLength * observed_capacity) {
      Resize();
    }
    return true;
  }

  bool Remove(T elem) final {
    const size_t hash = Hash(elem);
    auto access = AcquireBucket(hash);
    auto& bucket = access.state->buckets[hash % access.state->buckets.size()];
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
    auto access = AcquireBucket(hash);
    const auto& bucket =
        access.state->buckets[hash % access.state->buckets.size()];
    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  [[nodiscard]] size_t Size() const final {
    return size_.load(std::memory_order_relaxed);
  }

 private:
  static constexpr size_t kMaxAverageBucketLength = 4;

  struct TableState {
    explicit TableState(size_t capacity) : buckets(capacity) {
      locks.reserve(capacity);
      for (size_t i = 0; i < capacity; ++i) {
        locks.push_back(std::make_unique<std::mutex>());
      }
    }

    std::vector<std::vector<T>> buckets;
    std::vector<std::unique_ptr<std::mutex>> locks;
  };

  struct BucketAccess {
    std::shared_ptr<TableState> state;
    std::unique_lock<std::mutex> lock;
  };

  [[nodiscard]] static size_t Hash(const T& elem) {
    return std::hash<T>{}(elem);
  }

  [[nodiscard]] BucketAccess AcquireBucket(size_t hash) {
    while (true) {
      auto current = state_.load(std::memory_order_acquire);
      const size_t index = hash % current->buckets.size();
      std::unique_lock<std::mutex> guard(*current->locks[index]);
      if (current == state_.load(std::memory_order_acquire)) {
        return {std::move(current), std::move(guard)};
      }
    }
  }

  void Resize() {
    const std::scoped_lock<std::mutex> resize_lock(resize_mutex_);
    auto old_state = state_.load(std::memory_order_acquire);
    if (size_.load(std::memory_order_relaxed) <=
        kMaxAverageBucketLength * old_state->buckets.size()) {
      return;
    }
   
    std::vector<std::unique_lock<std::mutex>> guards;
    guards.reserve(old_state->locks.size());
    for (const auto& bucket_lock : old_state->locks) {
      guards.emplace_back(*bucket_lock);
    }
    if (size_.load(std::memory_order_relaxed) <=
        kMaxAverageBucketLength * old_state->buckets.size()) {
      return;
    }

    auto next_state =
        std::make_shared<TableState>(old_state->buckets.size() * 2);
    for (const auto& bucket : old_state->buckets) {
      for (const auto& elem : bucket) {
        next_state->buckets[Hash(elem) % next_state->buckets.size()]
            .push_back(elem);
      }
    }
    state_.store(std::move(next_state), std::memory_order_release);
  }

  std::atomic<std::shared_ptr<TableState>> state_;
  std::mutex resize_mutex_;
  std::atomic<size_t> size_{0};
};

#endif  // HASH_SET_REFINABLE_H
