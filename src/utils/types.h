#pragma once

// Internal convenience header.
// All public type definitions (DataType, ErrorCode, Tensor, TensorInfo, etc.)
// live in the public header "atlas/types.h".  This header simply re-exports
// them so that internal code can keep using #include "src/utils/types.h"
// without changing every include site.
#include "atlas/types.h"