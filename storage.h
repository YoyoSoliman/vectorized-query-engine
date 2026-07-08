#ifndef STORAGE_H
#define STORAGE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

const size_t VECTOR_SIZE = 1024;

// added VARCHAR type support
enum class TypeId { INT64, DOUBLE, VARCHAR };

// a high-performance 16-byte database string tracking handle
struct StringView {
  uint32_t length;
  uint32_t offset;
};

struct Vector {
  TypeId type;
  void *data = nullptr;

  // a single dense memory heap to pool all raw characters together
  std::vector<char> string_arena;

  Vector(TypeId t) : type(t) {
    switch (type) {
    case TypeId::INT64:
      data = new int64_t[VECTOR_SIZE];
      break;
    case TypeId::DOUBLE:
      data = new double[VECTOR_SIZE];
      break;
    case TypeId::VARCHAR:
      // allocate an array of 16-byte tracking handles
      data = new StringView[VECTOR_SIZE];
      break;
    }
  }

  Vector(const Vector &) = delete;
  Vector &operator=(const Vector &) = delete;

  Vector(Vector &&other) noexcept
      : type(other.type), data(other.data),
        string_arena(std::move(other.string_arena)) {
    other.data = nullptr;
  }

  ~Vector() {
    if (data != nullptr) {
      switch (type) {
      case TypeId::INT64:
        delete[] static_cast<int64_t *>(data);
        break;
      case TypeId::DOUBLE:
        delete[] static_cast<double *>(data);
        break;
      case TypeId::VARCHAR:
        delete[] static_cast<StringView *>(data);
        break;
      }
      data = nullptr;
    }
  }

  // helper function to append text into our contiguous memory pool
  void AppendString(size_t slot, const std::string &str) {
    if (type != TypeId::VARCHAR)
      return;

    StringView *views = static_cast<StringView *>(data);
    views[slot].length = static_cast<uint32_t>(str.length());
    views[slot].offset = static_cast<uint32_t>(string_arena.size());

    // push the characters sequentially to the back of our arena
    string_arena.insert(string_arena.end(), str.begin(), str.end());
  }

  // helper function to materialize an absolute std::string_view for
  // calculations
  std::string_view GetStringView(size_t slot) const {
    if (type != TypeId::VARCHAR)
      return "";
    const StringView *views = static_cast<const StringView *>(data);
    return std::string_view(&string_arena[views[slot].offset],
                            views[slot].length);
  }
};

struct ColumnarBatch {
  std::vector<Vector> columns;
  size_t size = 0;
  uint16_t selection_vector[VECTOR_SIZE];
  bool use_selection_vector = false;

  void Flatten() {
    if (!use_selection_vector)
      return;

    for (auto &col : columns) {
      if (col.type == TypeId::INT64) {
        int64_t *raw_data = static_cast<int64_t *>(col.data);
        std::vector<int64_t> tmp(size);
        for (size_t i = 0; i < size; i++) {
          tmp[i] = raw_data[selection_vector[i]];
        }
        std::copy(tmp.begin(), tmp.end(), raw_data);
      } else if (col.type == TypeId::DOUBLE) {
        double *raw_data = static_cast<double *>(col.data);
        std::vector<double> tmp(size);
        for (size_t i = 0; i < size; i++) {
          tmp[i] = raw_data[selection_vector[i]];
        }
        std::copy(tmp.begin(), tmp.end(), raw_data);
      } else if (col.type == TypeId::VARCHAR) {
        // to safely flatten varchar data, we copy the views and maintain the
        // arena integrity
        StringView *raw_data = static_cast<StringView *>(col.data);
        std::vector<StringView> tmp(size);
        for (size_t i = 0; i < size; i++) {
          tmp[i] = raw_data[selection_vector[i]];
        }
        std::copy(tmp.begin(), tmp.end(), raw_data);
      }
    }

    use_selection_vector = false;
  }
};

#endif // STORAGE_H
