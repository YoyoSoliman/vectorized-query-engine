#include "operators.h"
#include <iostream>
#include <memory>

int main() {
  // 1. Build Side Table — containing intentional duplicates of key 150
  ColumnarBatch build_table;
  Vector build_col(TypeId::INT64);
  build_table.columns.push_back(std::move(build_col));

  int64_t *raw_build_array =
      static_cast<int64_t *>(build_table.columns[0].data);
  raw_build_array[0] = 50;
  raw_build_array[1] = 150; // first instance of 150
  raw_build_array[2] = 30;
  raw_build_array[3] = 150; // duplicate instance of 150!
  build_table.size = 4;

  // 2. Probe Side Table — containing keys to look up
  ColumnarBatch probe_table;
  Vector probe_col(TypeId::INT64);
  probe_table.columns.push_back(std::move(probe_col));

  int64_t *raw_probe_array =
      static_cast<int64_t *>(probe_table.columns[0].data);
  raw_probe_array[0] = 30;  // matches row 2
  raw_probe_array[1] = 99;  // no match
  raw_probe_array[2] = 150; // matches row 1 AND row 3!
  probe_table.size = 3;

  std::cout << "--- ASSEMBLING MULTI-MATCH HASH JOIN TREE ---" << std::endl;

  auto build_scan = std::make_unique<ScanOperator>(std::move(build_table));
  auto probe_scan = std::make_unique<ScanOperator>(std::move(probe_table));

  auto join_pipeline = std::make_unique<HashJoinOperator>(
      std::move(build_scan), std::move(probe_scan), 0, 0);

  ColumnarBatch output_joined_batch;
  std::cout << "\n--- EXECUTING RELATIONAL JOIN PHASE ---" << std::endl;

  if (join_pipeline->Next(output_joined_batch)) {
    std::cout << "Join successful! Emitted " << output_joined_batch.size
              << " matching rows:\n"
              << std::endl;

    int64_t *res_col0 =
        static_cast<int64_t *>(output_joined_batch.columns[0].data);
    int64_t *res_col1 =
        static_cast<int64_t *>(output_joined_batch.columns[1].data);

    for (size_t i = 0; i < output_joined_batch.size; i++) {
      std::cout << "Row " << i << " -> [Build Key: " << res_col0[i]
                << " | Probe Key: " << res_col1[i] << "]" << std::endl;
    }
  } else {
    std::cout << "Join execution yielded 0 matching key sets." << std::endl;
  }

  return 0;
}
