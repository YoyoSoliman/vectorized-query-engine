#ifndef TYPES_H
#define TYPES_H

#include <cstddef>

// Standard vectorized data block width across analytical engines
const size_t VECTOR_SIZE = 1024;

enum class TypeId { 
    INT64, 
    DOUBLE, 
    VARCHAR 
};

#endif // TYPES_H
