#include "platform/windows/exit_target.hpp"
#include "openvr.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <string_view>

namespace bailout::platform::windows { namespace {
constexpr std::string_view kOwnAppKey = "com.steamvr-bail-out.overlay";
std::uint64_t token(const FILETIME& f) { ULARGE_INTEGER u{}; u.LowPart=f.dwLowDateTime; u.HighPart=f.dwHighDateTime; return u.QuadPart; }
std::optional<std::pair<std::wstring,std::uint64_t>> inspect(std::uint32_t pid) {
  HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid); if(!h) return std::nullopt;
  FILETIME c{},e{},k{},u{}; std::array<wchar_t,32768> p{}; DWORD n=static_cast<DWORD>(p.size());
  bool ok=GetProcessTimes(h,&c,&e,&k,&u)&&QueryFullProcessImageNameW(h,0,p.data(),&n); CloseHandle(h);
  if(!ok||!n) return std::nullopt; return std::pair{std::wstring(p.data(),n),token(c)};
}
std::wstring lower(std::wstring v){std::transform(v.begin(),v.end(),v.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});return v;}
std::string lower(std::string v){std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return v;}
bool prefix(const std::string& v,std::string_view p){return v.size()>=p.size()&&std::equal(p.begin(),p.end(),v.begin());}
std::wstring utf8(const char* s){int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0); if(n<=1)return{};std::wstring w(static_cast<size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s,-1,w.data(),n);w.pop_back();return w;}
}
ExitTargetResolver::ExitTargetResolver(vr::IVRApplications* a):applications_(a){}
std::optional<app::RuntimeTarget> ExitTargetResolver::resolve_foreground() const {
  if(!applications_||applications_->GetSceneApplicationState()!=vr::EVRSceneApplicationState_Running)return std::nullopt;
  auto pid=applications_->GetCurrentSceneProcessId(); if(!pid||pid==GetCurrentProcessId())return std::nullopt;
  std::array<char,vr::k_unMaxApplicationKeyLength> key{};
  if(applications_->GetApplicationKeyByProcessId(pid,key.data(),static_cast<uint32_t>(key.size()))!=vr::VRApplicationError_None||!app_is_safe_scene(key.data()))return std::nullopt;
  auto p=inspect(pid); if(!p||!process_is_safe(p->first))return std::nullopt;
  std::array<char,512> name{}; vr::EVRApplicationError err{};
  applications_->GetApplicationPropertyString(key.data(),vr::VRApplicationProperty_Name_String,name.data(),static_cast<uint32_t>(name.size()),&err);
  app::RuntimeTarget out{pid,p->second,key.data(),p->first,{}};
  if(err==vr::VRApplicationError_None)out.display_name=utf8(name.data());
  if(out.display_name.empty())out.display_name=std::filesystem::path(out.executable_path).stem().wstring();
  return out;
}
bool ExitTargetResolver::revalidate(const app::RuntimeTarget& t) const {
  if(!applications_||!t.pid||t.app_key.empty())return false; auto p=inspect(t.pid);
  if(!p||p->second!=t.creation_token||lower(p->first)!=lower(t.executable_path))return false;
  std::array<char,vr::k_unMaxApplicationKeyLength> k{};
  return applications_->GetApplicationKeyByProcessId(t.pid,k.data(),static_cast<uint32_t>(k.size()))==vr::VRApplicationError_None&&t.app_key==k.data()&&app_is_safe_scene(t.app_key);
}
bool ExitTargetResolver::app_is_safe_scene(const std::string& raw) const {
  auto key=lower(raw); if(key.empty()||key==kOwnAppKey)return false;
  if(!prefix(key,"steam.app."))return false;
  constexpr std::array<std::string_view,11> deny={"openvr.tool.","openvr.component.","openvr.overlay.","steamvr.","com.valvesoftware.steamvr","vrmonitor","vrdashboard","vrserver","vrcompositor","vrwebhelper","vrstartup"};
  for(auto p:deny)if(prefix(key,p))return false;
  vr::EVRApplicationError e{};
  if(applications_->GetApplicationPropertyBool(raw.c_str(),vr::VRApplicationProperty_IsDashboardOverlay_Bool,&e)||e!=vr::VRApplicationError_None)return false;
  if(applications_->GetApplicationPropertyBool(raw.c_str(),vr::VRApplicationProperty_IsTemplate_Bool,&e)||e!=vr::VRApplicationError_None)return false;
  if(applications_->GetApplicationPropertyBool(raw.c_str(),vr::VRApplicationProperty_IsInternal_Bool,&e)||e!=vr::VRApplicationError_None)return false;
  return applications_->GetApplicationProcessId(raw.c_str())!=0;
}
bool ExitTargetResolver::process_is_safe(const std::wstring& path) const {
  auto n=lower(std::filesystem::path(path).filename().wstring());
  constexpr std::array<std::wstring_view,12> deny={L"vrserver.exe",L"vrmonitor.exe",L"vrcompositor.exe",L"vrdashboard.exe",L"vrwebhelper.exe",L"vrstartup.exe",L"steam.exe",L"steamservice.exe",L"steamwebhelper.exe",L"steamtours.exe",L"roomsetup.exe",L"steamvr-bail-out.exe"};
  return std::find(deny.begin(),deny.end(),n)==deny.end();
}
}
