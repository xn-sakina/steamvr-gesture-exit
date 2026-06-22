#include "runtime.h"

#include <iostream>

int main() {
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
  sbo::RuntimeSession session;
  return session.run();
#else
  std::cerr << "SteamVR Bail Out runtime is Windows-only for product validation.\n";
  return 1;
#endif
}
