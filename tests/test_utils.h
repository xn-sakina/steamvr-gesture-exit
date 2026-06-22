#pragma once

#include <cstdlib>
#include <iostream>

#define CHECK_TRUE(expr)                                                                         \
  do {                                                                                            \
    if (!(expr)) {                                                                                \
      std::cerr << __FILE__ << ":" << __LINE__ << " CHECK_TRUE failed: " #expr << "\n";      \
      std::exit(1);                                                                               \
    }                                                                                             \
  } while (false)

#define CHECK_EQ(a, b)                                                                            \
  do {                                                                                            \
    const auto va = (a);                                                                          \
    const auto vb = (b);                                                                          \
    if (!(va == vb)) {                                                                            \
      std::cerr << __FILE__ << ":" << __LINE__ << " CHECK_EQ failed: " #a " == " #b << "\n"; \
      std::exit(1);                                                                               \
    }                                                                                             \
  } while (false)
