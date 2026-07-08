# Vectorized OLAP Query Execution Engine

## Why Vectorized Execution?

> Classic databases have a major issue where they are not very CPU friendly. In an old-school database, data is stored row by row. If you want to scan a table with millions of rows to do analytics, the CPU has to jump around in memory constantly. This leads to a massive amount of cache misses if we are going through a lot of rows.

A **vectorized** database engine solves this by representing the database as flat arrays of columns instead of rows:
* You can think of this as having some pointers in a vector that point to the beginning of these **flat arrays**.
* When we decide to query that array, the CPU loads the data in clean, contiguous lines.
* Because the data is completely contiguous in memory, it takes the whole cache line at once and makes processing incredibly fast.

---

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
  The performance testing tool. It feeds 10 million rows into the engine to measure the exact speed of our optimized code against a traditional database loop.

---

## Explanation of the Benchmark Results

When you run the benchmark file, it measures how long it takes the CPU to filter through 10 million rows of data. 

The benchmark compares two different coding methods:

1. **The Naive Method:** Uses a standard if-statement, which forces the CPU to constantly guess whether a row passes the filter. This causes branch mispredictions and slows down the hardware.
2. **The Optimized Method:** Uses a branchless design and a compiler hint. This setup allows the compiler to generate SIMD instructions. 

**SIMD** stands for *Single Instruction, Multiple Data*. Instead of checking rows one by one, the CPU hardware is able to check multiple data slots at the exact same time. This is why the benchmark shows a clear performance speedup and processes billions of rows per second.
