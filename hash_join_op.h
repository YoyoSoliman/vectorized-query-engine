#ifndef HASH_JOIN_OP_H
#define HASH_JOIN_OP_H

#include "hashtable.h"
#include "operators.h"
#include <memory>

class HashJoinOperator : public PhysicalOperator {
private:
  std::unique_ptr<PhysicalOperator> build_child;
  std::unique_ptr<PhysicalOperator> probe_child;
  size_t build_col_idx;
  size_t probe_col_idx;

  SimpleHashTable hash_table;
  ColumnarBatch build_batch;
  bool hash_table_built = false;

public:
  HashJoinOperator(std::unique_ptr<PhysicalOperator> build_input,
                   std::unique_ptr<PhysicalOperator> probe_input,
                   size_t build_col, size_t probe_col)
      : build_child(std::move(build_input)),
        probe_child(std::move(probe_input)), build_col_idx(build_col),
        probe_col_idx(probe_col) {}

  bool Next(ColumnarBatch &batch) override {
    if (!hash_table_built) {
      if (build_child->Next(build_batch)) {
        const int64_t *build_keys = static_cast<const int64_t *>(
            build_batch.columns[build_col_idx].data);
        for (size_t i = 0; i < build_batch.size; i++) {
          hash_table.Insert(build_keys[i], i);
        }
      }
      hash_table_built = true;
    }

    ColumnarBatch probe_batch;
    if (!probe_child->Next(probe_batch))
      return false;

    const int64_t *probe_keys =
        static_cast<const int64_t *>(probe_batch.columns[probe_col_idx].data);

    batch.columns.clear();
    batch.columns.push_back(Vector(TypeId::INT64));
    batch.columns.push_back(Vector(TypeId::INT64));

    int64_t *out_col0 = static_cast<int64_t *>(batch.columns[0].data);
    int64_t *out_col1 = static_cast<int64_t *>(batch.columns[1].data);
    size_t match_count = 0;

    for (size_t i = 0; i < probe_batch.size; i++) {
      int64_t match_row = hash_table.GetHead(probe_keys[i]);
      while (match_row != -1) {
        if (hash_table.KeyMatches(match_row, probe_keys[i])) {
          const int64_t *build_keys = static_cast<const int64_t *>(
              build_batch.columns[build_col_idx].data);
          out_col0[match_count] = build_keys[match_row];
          out_col1[match_count] = probe_keys[i];
          match_count++;
        }
        match_row = hash_table.GetNext(match_row);
      }
    }

    batch.size = match_count;
    batch.use_selection_vector = false;

    return batch.size > 0;
  }
};

#endif // HASH_JOIN_OP_H
