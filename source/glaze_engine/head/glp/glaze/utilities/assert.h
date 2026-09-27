#pragma once

#include <stdexcept>

#define GLAZE_ASSERT(exp, msg)             \
    do {                                   \
        if (exp) {                         \
            throw std::runtime_error(msg); \
        }                                  \
    } while (0)
