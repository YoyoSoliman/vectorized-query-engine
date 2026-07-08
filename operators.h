#ifndef OPERATORS_H
#define OPERATORS_H

#include "storage.h"

class PhysicalOperator {
public:
  virtual ~PhysicalOperator() = default;
  virtual bool Next(ColumnarBatch &batch) = 0;
};

#endif // OPERATORS_H
