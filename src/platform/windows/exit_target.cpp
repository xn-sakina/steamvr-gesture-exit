#include "platform/windows/exit_target.hpp"
#include "core/vr_game_classifier.hpp"
#include "openvr.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string_view>

namespace bailout::platform::windows { namespace {
std::uint64_t token(const FILETIME& f) { ULARGE_INTEGER u{}; u.LowPart=f.dwLowDateTime; u.HighPart=f.dwHighDateTime; return u.QuadPart; }
std::optional<std::pair<std::wstring,std::uint64_t>> inspect(std::uint32_t pid) {
  HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid); if(!h) return std::nullopt;
  FILETIME c{},e{},k{},u{}; std::array<wchar_t,32768> p{}; DWORD n=static_cast<DWORD>(p.size());
  bool ok=GetProcessTimes(h,&c,&e,&k,&u)&&QueryFullProcessImageNameW(h,0,p.data(),&n); CloseHandle(h);
  if(!ok||!n) return std::nullopt;
  return std::pair{std::wstring(p.data(),n),token(c)};
}
std::wstring lower(std::wstring v){std::transform(v.begin(),v.end(),v.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});return v;}
std::wstring utf8(const char* s){int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0); if(n<=1)return{};std::wstring w(static_cast<size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s,-1,w.data(),n);w.pop_back();return w;}
std::optional<std::string> property_string(vr::IVRApplications* applications,const std::string& key,vr::EVRApplicationProperty property){
  std::array<char,4096> value{};vr::EVRApplicationError error{};
  applications->GetApplicationPropertyString(key.c_str(),property,value.data(),static_cast<uint32_t>(value.size()),&error);
  if(error!=vr::VRApplicationError_None||value[0]=='\0')return std::nullopt;
  return std::string(value.data());
}
std::optional<bool> property_bool(vr::IVRApplications* applications,const std::string& key,vr::EVRApplicationProperty property){
  vr::EVRApplicationError error{};const bool value=applications->GetApplicationPropertyBool(key.c_str(),property,&error);
  if(error!=vr::VRApplicationError_None)return std::nullopt;
  return value;
}
}
ExitTargetResolver::ExitTargetResolver(vr::IVRApplications* a):applications_(a){}
std::optional<app::RuntimeTarget> ExitTargetResolver::resolve_foreground() const {
  if(!applications_||applications_->GetSceneApplicationState()!=vr::EVRSceneApplicationState_Running)return std::nullopt;
  auto pid=applications_->GetCurrentSceneProcessId(); if(!pid||pid==GetCurrentProcessId())return std::nullopt;
  std::array<char,vr::k_unMaxApplicationKeyLength> key{};
  if(applications_->GetApplicationKeyByProcessId(pid,key.data(),static_cast<uint32_t>(key.size()))!=vr::VRApplicationError_None||!app_is_safe_scene(key.data(),pid))return std::nullopt;
  auto p=inspect(pid); if(!p||!process_is_safe(p->first))return std::nullopt;
  const auto name=property_string(applications_,key.data(),vr::VRApplicationProperty_Name_String);
  if(!name)return std::nullopt;
  app::RuntimeTarget out{pid,p->second,key.data(),p->first,{}};
  out.display_name=utf8(name->c_str());
  if(out.display_name.empty())return std::nullopt;
  return out;
}
bool ExitTargetResolver::revalidate(const app::RuntimeTarget& t) const {
  if(!applications_||!t.pid||t.app_key.empty())return false;
  auto p=inspect(t.pid);
  if(!p||p->second!=t.creation_token||lower(p->first)!=lower(t.executable_path))return false;
  std::array<char,vr::k_unMaxApplicationKeyLength> k{};
  return applications_->GetApplicationKeyByProcessId(t.pid,k.data(),static_cast<uint32_t>(k.size()))==vr::VRApplicationError_None&&t.app_key==k.data()&&app_is_safe_scene(t.app_key,t.pid);
}
bool ExitTargetResolver::app_is_safe_scene(const std::string& raw,std::uint32_t expected_pid) const {
  if(!applications_||!expected_pid||applications_->GetApplicationProcessId(raw.c_str())!=expected_pid)return false;
  const auto launch_type=property_string(applications_,raw,vr::VRApplicationProperty_LaunchType_String);
  const auto dashboard=property_bool(applications_,raw,vr::VRApplicationProperty_IsDashboardOverlay_Bool);
  const auto app_template=property_bool(applications_,raw,vr::VRApplicationProperty_IsTemplate_Bool);
  const auto internal=property_bool(applications_,raw,vr::VRApplicationProperty_IsInternal_Bool);
  const auto hidden=property_bool(applications_,raw,vr::VRApplicationProperty_IsHidden_Bool);
  if(!launch_type||!dashboard||!app_template||!internal||!hidden)return false;
  return core::is_current_vr_game_candidate({raw,*launch_type,*dashboard,*app_template,*internal,*hidden});
}
bool ExitTargetResolver::process_is_safe(const std::wstring& path) const {
  auto n=lower(std::filesystem::path(path).filename().wstring());
  constexpr std::array<std::wstring_view,12> deny={L"vrserver.exe",L"vrmonitor.exe",L"vrcompositor.exe",L"vrdashboard.exe",L"vrwebhelper.exe",L"vrstartup.exe",L"steam.exe",L"steamservice.exe",L"steamwebhelper.exe",L"steamtours.exe",L"roomsetup.exe",L"steamvr-bail-out.exe"};
  return std::find(deny.begin(),deny.end(),n)==deny.end();
}
}
