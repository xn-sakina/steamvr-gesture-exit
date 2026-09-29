#include "app/runtime_app.hpp"
#include <windows.h>
#include <tlhelp32.h>

namespace {
bool steamvr_server_running() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, L"vrserver.exe") != 0)
        continue;
      HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID);
      if (process) {
        found = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
        CloseHandle(process);
      }
      if (found)
        break;
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}

class RuntimeInstance {
 public:
  RuntimeInstance() : mutex_(CreateMutexW(nullptr, FALSE, L"Local\\SteamVRGestureExit.Runtime")) {
    if (mutex_) {
      const DWORD initial = WaitForSingleObject(mutex_, 0);
      if (initial == WAIT_OBJECT_0 || initial == WAIT_ABANDONED) {
        acquired_ = true;
        return;
      }
      if (initial != WAIT_TIMEOUT)
        return;
      // A previous SteamVR session may still be shutting down. Wait only a
      // bounded time; a crashed owner grants WAIT_ABANDONED to this process.
      for (int attempt = 0; attempt < 60 && steamvr_server_running(); ++attempt) {
        const DWORD result = WaitForSingleObject(mutex_, 250);
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
          acquired_ = true;
          break;
        }
        if (result != WAIT_TIMEOUT)
          break;
      }
    }
  }
  ~RuntimeInstance() {
    if (acquired_)
      ReleaseMutex(mutex_);
    if (mutex_)
      CloseHandle(mutex_);
  }
  RuntimeInstance(const RuntimeInstance&) = delete;
  RuntimeInstance& operator=(const RuntimeInstance&) = delete;
  bool acquired() const { return acquired_; }

 private:
  HANDLE mutex_ = nullptr;
  bool acquired_ = false;
};
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  RuntimeInstance instance;
  if (!instance.acquired())
    return 0;
  try {
    steamvr_gesture_exit::app::RuntimeApp app;
    return app.run();
  } catch (...) {
    return 1;
  }
}
