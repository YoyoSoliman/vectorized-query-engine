#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "storage.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class MorselScheduler {
private:
  std::vector<ColumnarBatch> morsel_queue;
  std::mutex queue_mutex;
  size_t current_idx = 0;

public:
  // Add distinct chunks of data to our global queue
  void AddMorsel(ColumnarBatch &&batch) {
    morsel_queue.push_back(std::move(batch));
  }

  // Thread-safe extraction of the next data morsel to process
  bool GetNextMorsel(ColumnarBatch &target) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    if (current_idx >= morsel_queue.size()) {
      return false;
    }
    target = std::move(morsel_queue[current_idx]);
    current_idx++;
    return true;
  }

  // Concurrent pipeline runner driving a worker pool across available CPU cores
  void ExecuteParallel(
      const std::function<void(ColumnarBatch &&)> &pipeline_worker) {
    unsigned int thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0)
      thread_count = 2; // Fallback

    std::vector<std::thread> workers;

    for (unsigned int i = 0; i < thread_count; i++) {
      workers.emplace_back([this, &pipeline_worker]() {
        ColumnarBatch batch;
        // Keep draining morsels until the queue is completely exhausted
        while (GetNextMorsel(batch)) {
          pipeline_worker(std::move(batch));
        }
      });
    }

    // Synchronize and join all worker paths back into the main execution tree
    for (auto &thread : workers) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }
};

#endif // SCHEDULER_H
