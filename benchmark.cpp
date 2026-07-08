#include "hash_join_op.h"
#include "operators_impl.h"
#include "storage.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

// Performance metrics for isolated SIMD filtration
size_t RunSIMDFilterLoop(const int64_t *data, size_t size, int64_t threshold,
                         uint16_t *sel_vec) {
  size_t passed_count = 0;
#pragma omp simd
  for (size_t i = 0; i < size; i++) {
    bool match = data[i] > threshold;
    if (match) {
      sel_vec[passed_count] = i;
      passed_count++;
    }
  }
  return passed_count;
}

int main() {
  // 1. Scale Up to 100 Million Rows
  const size_t NUM_ROWS = 100'000'000;
  const int64_t FILTER_THRESHOLD = 800; // ~20% selectivity path

  std::cout << "=== STAGE 1: ALLOCATING 100 MILLION ROWS ===" << std::endl;
  std::vector<int64_t> probe_keys(NUM_ROWS);
  auto *sel_vector = new uint16_t[NUM_ROWS];

  // 2. Track Real-World String Distributions
  std::cout << "\n=== STAGE 2: LOADING STRING DISTRIBUTION ===" << std::endl;
  ColumnarBatch string_batch;
  string_batch.columns.push_back(Vector(TypeId::VARCHAR));

  // Simulating real-world logs: a mix of status codes, short hashes, and long
  // URLs
  std::vector<std::string> real_world_strings = {
      "OK",                         // 2 bytes  (Inline)
      "NOT_FOUND",                  // 9 bytes  (Inline)
      "UNAUTHORIZED",               // 12 bytes (Inline Max)
      "ERR_CONNECTION_TIMED_OUT",   // 24 bytes (Heap Overflow)
      "SUCCESS_DATA_FETCH_COMPLETE" // 28 bytes (Heap Overflow)
  };

  std::mt19937 rng(42);
  std::uniform_int_distribution<size_t> str_dist(0,
                                                 real_world_strings.size() - 1);

  size_t test_sample_size = 10000;
  size_t inline_count = 0;
  size_t heap_count = 0;

  for (size_t i = 0; i < test_sample_size; i++) {
    std::string selected_str = real_world_strings[str_dist(rng)];
    string_batch.columns[0].AppendString(i, selected_str);

    // Track where the engine stored it internally
    if (selected_str.length() <= 12) {
      inline_count++;
    } else {
      heap_count++;
    }
  }

  std::cout << "Processed " << test_sample_size
            << " sample strings into German-Style layouts:" << std::endl;
  std::cout << "  -> Inlined Strings (<= 12 bytes): " << inline_count << " ("
            << (double)inline_count / test_sample_size * 100 << "%)"
            << std::endl;
  std::cout << "  -> Heap Overflow Strings (> 12 bytes): " << heap_count << " ("
            << (double)heap_count / test_sample_size * 100 << "%)" << std::endl;

  // 3. Run 100M Row SIMD Scan Benchmark
  std::cout << "\n=== STAGE 3: RUNNING 100M ROW SIMD SCAN ===" << std::endl;
  std::uniform_int_distribution<int64_t> key_dist(1, 1000);
  for (size_t i = 0; i < NUM_ROWS; i++) {
    probe_keys[i] = key_dist(rng);
  }

  auto start_simd = std::chrono::high_resolution_clock::now();
  size_t count_simd = RunSIMDFilterLoop(probe_keys.data(), NUM_ROWS,
                                        FILTER_THRESHOLD, sel_vector);
  auto end_simd = std::chrono::high_resolution_clock::now();
  auto dur_simd = std::chrono::duration_cast<std::chrono::microseconds>(
                      end_simd - start_simd)
                      .count();

  double simd_seconds = dur_simd / 1'000'000.0;
  double million_rows_per_sec = (NUM_ROWS / 1'000'000.0) / simd_seconds;

  std::cout << "SIMD Selection Scan Throughput: " << million_rows_per_sec
            << " Million rows/sec" << std::endl;
  // CRITICAL FIX: Printing this count forces the compiler to keep the loop
  // intact!
  std::cout << "  [Verified Rows Passed Filter: " << count_simd << "]"
            << std::endl;

  // 4. Full End-to-End Pipeline Execution (Scan -> Filter -> Join ->
  // Aggregation)
  std::cout << "\n=== STAGE 4: EXECUTING END-TO-END TPC-H STYLE PIPELINE ==="
            << std::endl;

  // Setup Small Build Table for Hash Join (e.g., DimCustomer or DimStatus)
  ColumnarBatch build_batch;
  build_batch.columns.push_back(Vector(TypeId::INT64)); // Join Key
  int64_t *build_raw = static_cast<int64_t *>(build_batch.columns[0].data);
  build_raw[0] = 850;
  build_raw[1] = 900;
  build_raw[2] = 950;
  build_batch.size = 3;

  // Setup Mock Streaming Probe Batch
  ColumnarBatch stream_batch;
  stream_batch.columns.push_back(Vector(TypeId::INT64)); // Scan Key
  int64_t *stream_raw = static_cast<int64_t *>(stream_batch.columns[0].data);
  stream_raw[0] = 100; // Drops (fails filter)
  stream_raw[1] = 900; // Passes filter, Matches Join!
  stream_raw[2] = 950; // Passes filter, Matches Join!
  stream_raw[3] = 200; // Drops (fails filter)
  stream_raw[4] = 850; // Passes filter, Matches Join!
  stream_batch.size = 5;

  // Construct the processing pipeline tree
  auto scan = std::make_unique<ScanOperator>(std::move(stream_batch));
  auto filter = std::make_unique<FilterOperator>(std::move(scan),
                                                 500); // Filter out keys <= 500
  auto build_scan = std::make_unique<ScanOperator>(std::move(build_batch));

  // Join on Column 0 of both inputs
  auto join = std::make_unique<HashJoinOperator>(std::move(build_scan),
                                                 std::move(filter), 0, 0);
  auto pipeline = std::make_unique<AggregationOperator>(std::move(join), 1);

  ColumnarBatch output;
  pipeline->Next(output);

  delete[] sel_vector;
  return 0;
}
