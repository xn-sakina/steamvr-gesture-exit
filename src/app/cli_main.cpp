#include "platform/openvr/openvr_runtime.hpp"
#include "platform/windows/paths.hpp"
#include "openvr.h"
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
constexpr char kAppKey[] = "com.steamvr-bail-out.overlay";
constexpr auto kManifestReadyTimeout = std::chrono::seconds(15);

bool wait_for_manifest(vr::IVRApplications* applications) {
  const auto deadline = std::chrono::steady_clock::now() + kManifestReadyTimeout;
  do {
    if (applications->IsApplicationInstalled(kAppKey))
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  } while (std::chrono::steady_clock::now() < deadline);
  return applications->IsApplicationInstalled(kAppKey);
}

int application_command(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::cerr << "a command is required\n";
    return 1;
  }
  std::wstring_view command = argv[1];
  if (command == L"--version") {
    std::cout << "steamvr-bail-out " << BAILOUT_VERSION << "\n";
    return 0;
  }
  if (command == L"--diagnostics") {
    std::wcout << L"settings=" << bailout::platform::windows::settings_path() << L"\nlogs="
               << bailout::platform::windows::log_directory() << L"\nexecutable="
               << bailout::platform::windows::executable_path() << L"\n";
    return 0;
  }
  const bool manifest_command =
      command == L"--register-manifest" || command == L"--unregister-manifest";
  const bool status_command =
      command == L"--manifest-status" || command == L"--autolaunch-status";
  if (!manifest_command && !status_command) {
    std::cerr << "unknown command\n";
    return 1;
  }
  if (manifest_command && argc != 3) {
    std::cerr << "manifest command requires an absolute manifest path\n";
    return 1;
  }
  if (status_command && argc != 3) {
    std::cerr << "status command requires an app key\n";
    return 1;
  }
  bailout::platform::openvr::OpenVrRuntime runtime;
  if (!runtime.initialize(vr::VRApplication_Background)) {
    std::cerr << runtime.error() << "\n";
    return 1;
  }
  auto* apps = runtime.applications();
  int result = 1;
  if (manifest_command) {
    std::filesystem::path path(argv[2]);
    if (!path.is_absolute()) {
      std::cerr << "manifest path must be absolute\n";
    } else {
      auto utf8 = path.u8string();
      std::string native(reinterpret_cast<const char*>(utf8.data()), utf8.size());
      vr::EVRApplicationError error;
      if (command == L"--register-manifest") {
        const bool was_installed = apps->IsApplicationInstalled(kAppKey);
        error = apps->AddApplicationManifest(native.c_str(), false);
        if (error == vr::VRApplicationError_None) {
          if (!wait_for_manifest(apps))
            error = vr::VRApplicationError_UnknownApplication;
          else if (!was_installed)
            error = apps->SetApplicationAutoLaunch(kAppKey, false);
          if (error != vr::VRApplicationError_None && !was_installed)
            apps->RemoveApplicationManifest(native.c_str());
        }
      } else {
        auto disable = apps->SetApplicationAutoLaunch(kAppKey, false);
        auto remove = apps->RemoveApplicationManifest(native.c_str());
        error = remove != vr::VRApplicationError_None ? remove : disable;
      }
      if (error == vr::VRApplicationError_None)
        result = 0;
      else
        std::cerr << apps->GetApplicationsErrorNameFromEnum(error) << "\n";
    }
  } else if (status_command) {
    std::wstring key_w(argv[2]);
    int n = WideCharToMultiByte(CP_UTF8, 0, key_w.data(), static_cast<int>(key_w.size()), nullptr,
                                0, nullptr, nullptr);
    std::string key(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, key_w.data(), static_cast<int>(key_w.size()), key.data(), n,
                        nullptr, nullptr);
    if (command == L"--manifest-status")
      result = apps->IsApplicationInstalled(key.c_str()) ? 0 : 2;
    else if (!apps->IsApplicationInstalled(key.c_str()))
      result = 3;
    else
      result = apps->GetApplicationAutoLaunch(key.c_str()) ? 0 : 2;
  }
  runtime.shutdown();
  return result;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  return application_command(argc, argv);
}
