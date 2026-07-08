#ifndef OPERATORS_H
#define OPERATORS_H

#include "hashtable.h"
#include "storage.h"
#include <iostream>
#include <memory>
#include <string_view>

class PhysicalOperator {
public:
  virtual ~PhysicalOperator() = default;
  virtual bool Next(ColumnarBatch &batch) = 0;
};

class ScanOperator : public PhysicalOperator {
private:
  ColumnarBatch source_batch;
  bool produced = false;

public:
  ScanOperator(ColumnarBatch &&batch) : source_batch(std::move(batch)) {}

  bool Next(ColumnarBatch &batch) override {
    if (produced) {
      return false;
    }
    batch = std::move(source_batch);
    produced = true;
    return true;
  }
};

class FilterOperator : public PhysicalOperator {
private:
  std::unique_ptr<PhysicalOperator> child;
  int64_t threshold;

public:
  FilterOperator(std::unique_ptr<PhysicalOperator> child_op, int64_t val)
      : child(std::move(child_op)), threshold(val) {}

  bool Next(ColumnarBatch &batch) override {
    if (!child->Next(batch)) {
      return false;
    }

    const int64_t *data = static_cast<const int64_t *>(batch.columns[0].data);
    size_t passed_count = 0;

    for (size_t i = 0; i < batch.size; i++) {
      size_t real_idx =
          batch.use_selection_vector ? batch.selection_vector[i] : i;

      if (data[real_idx] > threshold) {
        batch.selection_vector[passed_count] = real_idx;
        passed_count++;
      }
    }

    batch.size = passed_count;
    batch.use_selection_vector = true;

    batch.Flatten();

    return batch.size > 0;
  }
};

class ProjectionOperator : public PhysicalOperator {
private:
  std::unique_ptr<PhysicalOperator> child;
  size_t input_col_idx;
  size_t output_col_idx;

public:
  ProjectionOperator(std::unique_ptr<PhysicalOperator> child_op, size_t in_col,
                     size_t out_col)
      : child(std::move(child_op)), input_col_idx(in_col),
        output_col_idx(out_col) {}

  bool Next(ColumnarBatch &batch) override {
    if (!child->Next(batch)) {
      return false;
    }

    TypeId input_type = batch.columns[input_col_idx].type;

    if (input_type == TypeId::INT64) {
      const int64_t *input_array =
          static_cast<const int64_t *>(batch.columns[input_col_idx].data);
      int64_t *output_array =
          static_cast<int64_t *>(batch.columns[output_col_idx].data);

      for (size_t i = 0; i < batch.size; i++) {
        output_array[i] = input_array[i] + 10;
      }
    } else if (input_type == TypeId::DOUBLE) {
      const double *input_array =
          static_cast<const double *>(batch.columns[input_col_idx].data);
      double *output_array =
          static_cast<double *>(batch.columns[output_col_idx].data);

      for (size_t i = 0; i < batch.size; i++) {
        output_array[i] = input_array[i] + 10.0;
      }
    } else if (input_type == TypeId::VARCHAR) {
      const StringView *input_views =
          static_cast<const StringView *>(batch.columns[input_col_idx].data);
      StringView *output_views =
          static_cast<StringView *>(batch.columns[output_col_idx].data);

      std::copy(input_views, input_views + batch.size, output_views);
      batch.columns[output_col_idx].string_arena =
          batch.columns[input_col_idx].string_arena;
    }

    return true;
  }
};

class AggregationOperator : public PhysicalOperator {
private:
  std::unique_ptr<PhysicalOperator> child;
  size_t column_idx;

public:
  AggregationOperator(std::unique_ptr<PhysicalOperator> child_op,
                      size_t col_idx)
      : child(std::move(child_op)), column_idx(col_idx) {}

  bool Next(ColumnarBatch &batch) override {
    if (!child->Next(batch)) {
      return false;
    }

    TypeId col_type = batch.columns[column_idx].type;

    if (col_type == TypeId::INT64) {
      const int64_t *data_array =
          static_cast<const int64_t *>(batch.columns[column_idx].data);
      int64_t total_sum = 0;

      for (size_t i = 0; i < batch.size; i++) {
        total_sum += data_array[i];
      }
      std::cout << "SUM(Column " << column_idx << " [INT64]) = " << total_sum
                << std::endl;
    } else if (col_type == TypeId::DOUBLE) {
      const double *data_array =
          static_cast<const double *>(batch.columns[column_idx].data);
      double total_sum = 0.0;

      for (size_t i = 0; i < batch.size; i++) {
        total_sum += data_array[i];
      }
      std::cout << "SUM(Column " << column_idx << " [DOUBLE]) = " << total_sum
                << std::endl;
    } else if (col_type == TypeId::VARCHAR) {
      std::cout << "STRINGS(Column " << column_idx
                << " [VARCHAR]):" << std::endl;
      for (size_t i = 0; i < batch.size; i++) {
        std::string_view str = batch.columns[column_idx].GetStringView(i);
        std::cout << "  Row " << i << ": " << str << std::endl;
      }
    }

    return true;
  }
};

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
    // build phase: ingest build table and map all duplicate keys into chains
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
    if (!probe_child->Next(probe_batch)) {
      return false;
    }

    const int64_t *probe_keys =
        static_cast<const int64_t *>(probe_batch.columns[probe_col_idx].data);

    batch.columns.clear();
    batch.columns.push_back(Vector(TypeId::INT64));
    batch.columns.push_back(Vector(TypeId::INT64));

    int64_t *out_col0 = static_cast<int64_t *>(batch.columns[0].data);
    int64_t *out_col1 = static_cast<int64_t *>(batch.columns[1].data);
    size_t match_count = 0;

    // probe phase: iterate over probe keys and chase down the matching row
    // chain
    for (size_t i = 0; i < probe_batch.size; i++) {
      int64_t match_row = hash_table.GetHead(probe_keys[i]);

      while (match_row != -1) {
        // safeguard against raw hash collisions by verifying actual values
        // match
        if (hash_table.KeyMatches(match_row, probe_keys[i])) {
          const int64_t *build_keys = static_cast<const int64_t *>(
              build_batch.columns[build_col_idx].data);
          out_col0[match_count] = build_keys[match_row];
          out_col1[match_count] = probe_keys[i];
          match_count++;
        }
        // crawl deeper into the next linked row for this specific key
        match_row = hash_table.GetNext(match_row);
      }
    }

    batch.size = match_count;
    batch.use_selection_vector = false;

    return batch.size > 0;
  }
};

#endif // OPERATORS_H
