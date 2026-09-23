#include "platform/windows/process_controller.hpp"
#include <windows.h>
#include <vector>
namespace bailout::platform::windows {
namespace {
struct Search {
  DWORD pid;
  std::vector<HWND> windows;
};
BOOL CALLBACK collect(HWND w, LPARAM d) {
  auto& s = *reinterpret_cast<Search*>(d);
  DWORD p = 0;
  GetWindowThreadProcessId(w, &p);
  if (p == s.pid)
    s.windows.push_back(w);
  return TRUE;
}
uint64_t token(HANDLE h) {
  FILETIME c{}, e{}, k{}, u{};
  if (!GetProcessTimes(h, &c, &e, &k, &u))
    return 0;
  ULARGE_INTEGER x{};
  x.LowPart = c.dwLowDateTime;
  x.HighPart = c.dwHighDateTime;
  return x.QuadPart;
}
}  // namespace
bool ProcessController::request_graceful_close(const app::RuntimeTarget& t,
                                               std::chrono::steady_clock::time_point now) {
  clear();
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, t.pid);
  if (!h) {
    status_ = CloseStatus::Failed;
    error_code_ = "EXIT_OPEN_PROCESS_FAILED";
    return false;
  }
  target_ = t;
  process_ = h;
  if (!identity_matches()) {
    status_ = CloseStatus::IdentityInvalid;
    error_code_ = "EXIT_TARGET_IDENTITY_CHANGED";
    return false;
  }
  Search s{t.pid, {}};
  if (!EnumWindows(collect, reinterpret_cast<LPARAM>(&s))) {
    status_ = CloseStatus::Failed;
    error_code_ = "EXIT_ENUM_WINDOWS_FAILED";
    return false;
  }
  if (s.windows.empty()) {
    status_ = CloseStatus::Failed;
    error_code_ = "EXIT_GRACEFUL_WINDOW_NOT_FOUND";
    return false;
  }
  bool requested = false;
  for (HWND w : s.windows)
    requested = PostMessageW(w, WM_CLOSE, 0, 0) != 0 || requested;
  if (!requested) {
    status_ = CloseStatus::Failed;
    error_code_ = "EXIT_GRACEFUL_POST_MESSAGE_FAILED";
    return false;
  }
  deadline_ = now + kGracePeriod;
  status_ = CloseStatus::GracefulRequested;
  return true;
}
CloseStatus ProcessController::poll(std::chrono::steady_clock::time_point now) {
  if (status_ == CloseStatus::ForcePending) {
    const DWORD result = WaitForSingleObject(static_cast<HANDLE>(process_), 0);
    if (result == WAIT_OBJECT_0)
      status_ = CloseStatus::ForceSucceeded;
    else if (result == WAIT_TIMEOUT && now >= force_deadline_)
      status_ = CloseStatus::ForceTimedOut;
    else if (result != WAIT_TIMEOUT)
      status_ = CloseStatus::Failed;
    return status_;
  }
  if (status_ != CloseStatus::GracefulRequested)
    return status_;
  const DWORD result = WaitForSingleObject(static_cast<HANDLE>(process_), 0);
  if (result == WAIT_OBJECT_0) {
    status_ = CloseStatus::ExitedGracefully;
  } else if (result != WAIT_TIMEOUT) {
    status_ = CloseStatus::Failed;
  } else if (!identity_matches()) {
    status_ = CloseStatus::IdentityInvalid;
  } else if (now >= deadline_) {
    status_ = CloseStatus::ForceRequired;
  }
  return status_;
}
CloseStatus ProcessController::force_close(std::chrono::steady_clock::time_point now) {
  if (status_ != CloseStatus::ForceRequired || !process_)
    return status_;
  const DWORD result = WaitForSingleObject(static_cast<HANDLE>(process_), 0);
  if (result == WAIT_OBJECT_0) {
    status_ = CloseStatus::ForceSucceeded;
    return status_;
  }
  if (result != WAIT_TIMEOUT) {
    status_ = CloseStatus::Failed;
    return status_;
  }
  if (!identity_matches()) {
    status_ = CloseStatus::IdentityInvalid;
    return status_;
  }
  HANDLE termination =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, target_->pid);
  if (!termination) {
    status_ = WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_OBJECT_0
                  ? CloseStatus::ForceSucceeded
                  : CloseStatus::Failed;
    return status_;
  }
  if (token(termination) != target_->creation_token) {
    CloseHandle(termination);
    status_ = CloseStatus::IdentityInvalid;
    return status_;
  }
  const bool requested = TerminateProcess(termination, 1) != 0;
  CloseHandle(termination);
  if (!requested) {
    status_ = WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_OBJECT_0
                  ? CloseStatus::ForceSucceeded
                  : CloseStatus::Failed;
    return status_;
  }
  status_ = CloseStatus::ForcePending;
  force_deadline_ = now + kForceWait;
  return status_;
}
void ProcessController::clear() {
  if (process_)
    CloseHandle(static_cast<HANDLE>(process_));
  process_ = nullptr;
  target_.reset();
  deadline_ = {};
  force_deadline_ = {};
  status_ = CloseStatus::Idle;
  error_code_ = {};
}
bool ProcessController::identity_matches() const {
  return process_ && target_ && token(static_cast<HANDLE>(process_)) == target_->creation_token;
}
}  // namespace bailout::platform::windows
