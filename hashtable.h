#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <cstddef>
#include <cstdint>
#include <vector>

class SimpleHashTable {
private:
  size_t capacity;
  std::vector<int64_t> head;
  std::vector<int64_t> next;
  std::vector<int64_t> keys;

  size_t Hash(int64_t key) const { return static_cast<size_t>(key) % capacity; }

public:
  SimpleHashTable(size_t cap = 2048)
      : capacity(cap), head(cap, -1), next(cap, -1), keys(cap, 0) {}

  void Insert(int64_t key, size_t row_idx) {
    size_t h = Hash(key);
    if (row_idx >= next.size()) {
      next.resize(row_idx + 1, -1);
      keys.resize(row_idx + 1, 0);
    }
    next[row_idx] = head[h];
    head[h] = static_cast<int64_t>(row_idx);
    keys[row_idx] = key;
  }

  int64_t GetHead(int64_t key) const { return head[Hash(key)]; }
  int64_t GetNext(size_t current_row_idx) const {
    return next[current_row_idx];
  }
  bool KeyMatches(size_t row_idx, int64_t key) const {
    return keys[row_idx] == key;
  }
};

#endif // HASHTABLE_H
