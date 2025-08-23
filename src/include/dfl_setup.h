#ifndef DFL_SETUP_H
#define DFL_SETUP_H


// src/passes/dfl/dfl.cpp
// src/lib/dfl/dfl.c

#define CACHE_LINE_ALIGNMENT (64uL)
#define CACHE_LINE_SHIFT (6uL)

// DFL_STRIDE: The stride for the data flow linearization
// DFL_VECTORIZE: Set to true to enable vectorization, false to disable
#include "conf.h"

#endif