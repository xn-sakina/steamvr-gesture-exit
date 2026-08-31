#include "platform/windows/process_controller.hpp"
#include <windows.h>
#include <vector>
namespace bailout::platform::windows { namespace {
struct Search{DWORD pid;std::vector<HWND> windows;};
BOOL CALLBACK collect(HWND w,LPARAM d){auto& s=*reinterpret_cast<Search*>(d);DWORD p=0;GetWindowThreadProcessId(w,&p);if(p==s.pid&&GetWindow(w,GW_OWNER)==nullptr)s.windows.push_back(w);return TRUE;}
uint64_t token(HANDLE h){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(h,&c,&e,&k,&u))return 0;ULARGE_INTEGER x{};x.LowPart=c.dwLowDateTime;x.HighPart=c.dwHighDateTime;return x.QuadPart;}
}
bool ProcessController::request_graceful_close(const app::RuntimeTarget& t,std::chrono::steady_clock::time_point now){
  clear();HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE|PROCESS_TERMINATE,FALSE,t.pid);if(!h){status_=CloseStatus::Failed;return false;}target_=t;process_=h;
  if(!identity_matches()){status_=CloseStatus::IdentityInvalid;return false;}Search s{t.pid,{}};EnumWindows(collect,reinterpret_cast<LPARAM>(&s));for(HWND w:s.windows)PostMessageW(w,WM_CLOSE,0,0);
  deadline_=now+kGracePeriod;status_=CloseStatus::GracefulRequested;return true;
}
CloseStatus ProcessController::poll(std::chrono::steady_clock::time_point now){if(status_!=CloseStatus::GracefulRequested)return status_;if(!is_alive()){status_=CloseStatus::ExitedGracefully;}else if(!identity_matches()){status_=CloseStatus::IdentityInvalid;}else if(now>=deadline_){status_=CloseStatus::ForceRequired;}return status_;}
CloseStatus ProcessController::force_close(){if(status_!=CloseStatus::ForceRequired||!process_)return status_;if(!is_alive()){status_=CloseStatus::ForceSucceeded;return status_;}if(!identity_matches()){status_=CloseStatus::IdentityInvalid;return status_;}if(!TerminateProcess(static_cast<HANDLE>(process_),1)){status_=is_alive()?CloseStatus::Failed:CloseStatus::ForceSucceeded;return status_;}status_=WaitForSingleObject(static_cast<HANDLE>(process_),1000)==WAIT_OBJECT_0?CloseStatus::ForceSucceeded:CloseStatus::Failed;return status_;}
void ProcessController::clear(){if(process_)CloseHandle(static_cast<HANDLE>(process_));process_=nullptr;target_.reset();status_=CloseStatus::Idle;}
bool ProcessController::identity_matches()const{return process_&&target_&&token(static_cast<HANDLE>(process_))==target_->creation_token;}
bool ProcessController::is_alive()const{return process_&&WaitForSingleObject(static_cast<HANDLE>(process_),0)==WAIT_TIMEOUT;}
}
