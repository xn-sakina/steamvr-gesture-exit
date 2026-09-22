#include "app/runtime_app.hpp"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  bailout::app::RuntimeApp app;
  return app.run();
}
