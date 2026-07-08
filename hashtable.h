#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <cstddef>
#include <cstdint>
#include <vector>

class SimpleHashTable {
private:
  size_t capacity;

  // head[hash_idx] points to the start of the row chain for that hash
  std::vector<int64_t> head;

  // next[row_idx] points to the next row index in the chain
  std::vector<int64_t> next;

  // stores the actual keys for validation during probing
  std::vector<int64_t> keys;

  size_t Hash(int64_t key) const { return static_cast<size_t>(key) % capacity; }

public:
  SimpleHashTable(size_t cap = 2048)
      : capacity(cap), head(cap, -1), next(cap, -1), keys(cap, 0) {}

  // insert a key and chain it to any previous matching keys
  void Insert(int64_t key, size_t row_idx) {
    size_t h = Hash(key);

    // ensure our tracking vectors are wide enough for the build side row index
    if (row_idx >= next.size()) {
      next.resize(row_idx + 1, -1);
      keys.resize(row_idx + 1, 0);
    }

    // chain the current head to our next pointer, then make this row the new
    // head
    next[row_idx] = head[h];
    head[h] = static_cast<int64_t>(row_idx);
    keys[row_idx] = key;
  }

  // get the head row index for a given key to begin the probe chain walk
  int64_t GetHead(int64_t key) const {
    size_t h = Hash(key);
    return head[h];
  }

  // advance to the next row index in the chain matching the key
  int64_t GetNext(size_t current_row_idx) const {
    if (current_row_idx >= next.size())
      return -1;
    return next[current_row_idx];
  }

  // helper to verify if the row index actually matches our key
  bool KeyMatches(size_t row_idx, int64_t key) const {
    if (row_idx >= keys.size())
      return false;
    return keys[row_idx] == key;
  }
};

#endif // HASHTABLE_H
