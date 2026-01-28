#pragma once

#include "arrow-adbc/adbc.h"

// Error-checking helper for ADBC calls.
// Assumes that there is an AdbcError named `error` in scope.
#define CHECK_ADBC(EXPR)                                          \
  if (AdbcStatusCode status = (EXPR); status != ADBC_STATUS_OK) { \
    if (error.message != nullptr) {                               \
      std::cerr << error.message << std::endl;                    \
    }                                                             \
    return EXIT_FAILURE;                                          \
  }

// Error-checking helper for ArrowArrayStream (no nanoarrow dependency).
#define CHECK_STREAM(STREAM, EXPR)                        \
  if (int status = (EXPR); status != 0) {                 \
    std::cerr << "(" << std::strerror(status) << "): ";   \
    const char* message = nullptr;                        \
    if ((STREAM).get_last_error != nullptr) {             \
      message = (STREAM).get_last_error(&(STREAM));       \
    }                                                     \
    if (message != nullptr) {                             \
      std::cerr << message << std::endl;                  \
    } else {                                              \
      std::cerr << "(no error message)" << std::endl;     \
    }                                                     \
    return EXIT_FAILURE;                                  \
  }

// Error-checking helper for Nanoarrow.
#define CHECK_NANOARROW(EXPR)                                              \
  if (int status = (EXPR); status != 0) {                                  \
    std::cerr << "(" << std::strerror(status) << "): failed" << std::endl; \
    return EXIT_FAILURE;                                                   \
  }
