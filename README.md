# Vectorized OLAP Query Execution Engine

## Why Vectorized Execution?

> Classic databases have a major issue where they are not very CPU friendly. In an old-school database, data is stored row by row. If you want to scan a table with millions of rows to do analytics, the CPU has to jump around in memory constantly. This leads to a massive amount of cache misses if we are going through a lot of rows.

A **vectorized** database engine solves this by representing the database as flat arrays of columns instead of rows:
* You can think of this as having some pointers in a vector that point to the beginning of these **flat arrays**.
* When we decide to query that array, the CPU loads the data in clean, contiguous lines.
* Because the data is completely contiguous in memory, it takes the whole cache line at once and makes processing faster (less cache misses).


## File Guide

### Memory and Storage Layer

* **`types.h`**
  Just defines the behavior for how big we want these flat arrays to be and how we should handle the different data types like integers, doubles, and strings.

* **`string_view.h`**
  Implements the German-style string inlining optimization (https://www.e6data.com/blog/german-strings-faster-analytics) to fit short strings inside the metadata handle and eliminate pointer-chasing.

* **`storage.h`**
  Defines the data structures we use for the flat array layout and handles how we access them in memory.

### Execution and Parallelism Layer

* **`operators.h`**
  Creates the basic interface for our pipeline execution model so different database actions can be chained together.

* **`operators_impl.h`**
  Contains the actual database operations like scanning, projecting data, and filtering rows using branchless loops that trigger SIMD vectorization.

* **`hashtable.h`**
   A customized, completely flat hash table designed to map keys using simple integer arrays instead of slow pointer structures.

* **`hash_join_op.h`**
  The physical operator that executes relational table joins using our flat chained-bucket hash table to avoid heavy memory allocations.

* **`scheduler.h`**
  The multi-threaded task manager that breaks large data blocks into smaller chunks called morsels and runs them across all available CPU cores at the same time.

* **`vec.cpp`**
  The main program file that sets up our sample data, creates the execution pipeline, and runs the entire engine.

* **`benchmark.cpp`**
  The performance testing tool. It feeds 100 million rows into the engine to measure the exact speed and track real-world string distributions.


## Explanation of the Benchmark Results

When you run the benchmark file, it evaluates our data structures and tracking optimizations under heavy load:

### 1. Real-World String Allocations
Our German-Style string optimization splits mock enterprise log data dynamically based on size rules:
* **Inlined Strings (<= 12 bytes):** 60.06% of strings are packed straight inside the structural handle, which completely eliminates pointer chasing.
* **Heap Overflow Strings (> 12 bytes):** 39.94% of strings safely overflow to our contiguous memory arena pools.

### 2. Mass Data Scalability
The engine uses branchless loop designs to maximize hardware efficiency and prevent CPU branch mispredictions:
* **15% Performance Uplift:** Comparative benchmarks against traditional database branching loops show a clean 15% optimization latency reduction.
* **SIMD Selection Throughput:** When scaled up to **100 Million Rows**, the branchless scanning architecture fully saturates the hardware pipelines, sustaining an execution speed of **8,206.14 Million rows/sec**.
* **Pipeline Validation:** Successfully drives an end-to-end multi-operator pipeline (`Scan` -> `Filter` -> `Hash Join` -> `Aggregation`) to calculate accurate analytical query outputs.
