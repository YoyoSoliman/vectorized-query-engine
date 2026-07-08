#include "hash_join_op.h"
#include "operators_impl.h"
#include "scheduler.h"
#include <iostream>
#include <memory>
#include <mutex>

int main() {
  MorselScheduler scheduler;
  std::mutex stdout_mutex;

  std::cout << "--- GENERATING MULTI-THREADED MORSEL CHUNKS ---" << std::endl;

  // Morsel Chunks 1 & 2 mimic an upscale partitioned storage scan
  {
    ColumnarBatch chunk1;
    chunk1.columns.push_back(Vector(TypeId::INT64));
    chunk1.columns.push_back(Vector(TypeId::VARCHAR));
    int64_t *ints = static_cast<int64_t *>(chunk1.columns[0].data);
    ints[0] = 150;
    ints[1] = 20;
    ints[2] = 300;
    chunk1.columns[1].AppendString(0, "morsel_1_pass_a");
    chunk1.columns[1].AppendString(1, "morsel_1_drop");
    chunk1.columns[1].AppendString(2, "morsel_1_pass_b");
    chunk1.size = 3;
    scheduler.AddMorsel(std::move(chunk1));
  }

  {
    ColumnarBatch chunk2;
    chunk2.columns.push_back(Vector(TypeId::INT64));
    chunk2.columns.push_back(Vector(TypeId::VARCHAR));
    int64_t *ints = static_cast<int64_t *>(chunk2.columns[0].data);
    ints[0] = 40;
    ints[1] = 500;
    ints[2] = 120;
    chunk2.columns[1].AppendString(0, "morsel_2_drop");
    chunk2.columns[1].AppendString(1, "morsel_2_pass_a");
    chunk2.columns[1].AppendString(2, "morsel_2_pass_b");
    chunk2.size = 3;
    scheduler.AddMorsel(std::move(chunk2));
  }

  std::cout << "\n--- EXECUTING CONCURRENT MORSEL-DRIVEN PIPELINE ---"
            << std::endl;

  // Multi-threaded lambda processing execution branches in parallel
  scheduler.ExecuteParallel([&stdout_mutex](ColumnarBatch &&morsel) {
    auto scan = std::make_unique<ScanOperator>(std::move(morsel));
    auto filter = std::make_unique<FilterOperator>(std::move(scan), 100);
    auto pipeline = std::make_unique<AggregationOperator>(std::move(filter), 1);

    ColumnarBatch result_batch;

    // Isolate console access to keep output clean across threads
    std::lock_guard<std::mutex> lock(stdout_mutex);
    pipeline->Next(result_batch);
  });

  return 0;
}
