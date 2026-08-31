#include "platform/windows/paths.hpp"
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <array>
namespace bailout::platform::windows {
std::filesystem::path data_directory(){PWSTR p=nullptr;std::filesystem::path out;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&p))&&p)out=std::filesystem::path(p)/L"SteamVRBailOut";if(p)CoTaskMemFree(p);return out;}
std::filesystem::path settings_path(){return data_directory()/L"settings.json";}
std::filesystem::path log_directory(){return data_directory()/L"logs";}
std::filesystem::path executable_path(){std::array<wchar_t,32768> p{};DWORD n=GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()));return std::filesystem::path(std::wstring(p.data(),n));}
}
