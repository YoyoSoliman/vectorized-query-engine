#ifndef OPERATORS_IMPL_H
#define OPERATORS_IMPL_H

#include "operators.h"
#include <iostream>
#include <memory>

class ScanOperator : public PhysicalOperator {
private:
  ColumnarBatch source_batch;
  bool produced = false;

public:
  ScanOperator(ColumnarBatch &&batch) : source_batch(std::move(batch)) {}
  bool Next(ColumnarBatch &batch) override {
    if (produced)
      return false;
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
    if (!child->Next(batch))
      return false;

    const int64_t *data = static_cast<const int64_t *>(batch.columns[0].data);
    size_t passed_count = 0;
    size_t limit = batch.size;

// Force compiler auto-vectorization loop transformations (AVX2/AVX-512)
#pragma omp simd
    for (size_t i = 0; i < limit; i++) {
      // Branchless indexing evaluation maximizes pipelined instruction
      // throughput
      bool match = data[i] > threshold;
      if (match) {
        batch.selection_vector[passed_count] = i;
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
    if (!child->Next(batch))
      return false;
    TypeId input_type = batch.columns[input_col_idx].type;

    if (input_type == TypeId::INT64) {
      const int64_t *input_array =
          static_cast<const int64_t *>(batch.columns[input_col_idx].data);
      int64_t *output_array =
          static_cast<int64_t *>(batch.columns[output_col_idx].data);
      for (size_t i = 0; i < batch.size; i++)
        output_array[i] = input_array[i] + 10;
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
    if (!child->Next(batch))
      return false;
    TypeId col_type = batch.columns[column_idx].type;

    if (col_type == TypeId::INT64) {
      const int64_t *data_array =
          static_cast<const int64_t *>(batch.columns[column_idx].data);
      int64_t total_sum = 0;
      for (size_t i = 0; i < batch.size; i++)
        total_sum += data_array[i];
      std::cout << "SUM(Column " << column_idx << ") = " << total_sum
                << std::endl;
    } else if (col_type == TypeId::VARCHAR) {
      std::cout << "STRINGS(Column " << column_idx << "):" << std::endl;
      for (size_t i = 0; i < batch.size; i++) {
        std::string_view str = batch.columns[column_idx].GetStringView(i);
        std::cout << "  Row " << i << ": " << str << std::endl;
      }
    }
    return true;
  }
};

#endif // OPERATORS_IMPL_H
