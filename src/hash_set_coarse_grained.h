#ifndef HASH_SET_COARSE_GRAINED_H
#define HASH_SET_COARSE_GRAINED_H

#include <algorithm>
#include <cstddef>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetCoarseGrained : public HashSetBase<T> {
 public:
  explicit HashSetCoarseGrained(size_t initial_capacity)
      : buckets_(std::max(size_t{1}, initial_capacity)) {}

  bool Add(T elem) final {
    const std::scoped_lock<std::mutex> lock(mutex_);
    auto& bucket = buckets_[Hash(elem) % buckets_.size()];
    if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
      return false;
    }
    bucket.push_back(std::move(elem));
    ++size_;
    if (size_ > kMaxAverageBucketLength * buckets_.size()) {
      Resize();
    }
    return true;
  }

  bool Remove(T elem) final {
    const std::scoped_lock<std::mutex> lock(mutex_);
    auto& bucket = buckets_[Hash(elem) % buckets_.size()];
    const auto it = std::find(bucket.begin(), bucket.end(), elem);
    if (it == bucket.end()) {
      return false;
    }
    bucket.erase(it);
    --size_;
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    const std::scoped_lock<std::mutex> lock(mutex_);
    const auto& bucket = buckets_[Hash(elem) % buckets_.size()];
    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  [[nodiscard]] size_t Size() const final {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return size_;
  }

 private:
  static constexpr size_t kMaxAverageBucketLength = 4;

  [[nodiscard]] static size_t Hash(const T& elem) {
    return std::hash<T>{}(elem);
  }

  void Resize() {
    std::vector<std::vector<T>> new_buckets(buckets_.size() * 2);
    for (const auto& bucket : buckets_) {
      for (const auto& elem : bucket) {
        new_buckets[Hash(elem) % new_buckets.size()].push_back(elem);
      }
    }
    buckets_.swap(new_buckets);
  }

  mutable std::mutex mutex_;
  std::vector<std::vector<T>> buckets_;
  size_t size_ = 0;
};

#endif  // HASH_SET_COARSE_GRAINED_H
