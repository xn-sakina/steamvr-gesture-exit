#include "platform/openvr/appconfig_editor.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool read_file(const std::filesystem::path& path, std::string& bytes) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

bool write_file(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.flush();
  const bool written = static_cast<bool>(output);
  output.close();
  if (!written || output.fail())
    return false;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  const bool flushed = file != INVALID_HANDLE_VALUE && FlushFileBuffers(file) != 0;
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  return flushed;
}

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return 1;
}

bool to_utf8(std::wstring_view value, std::string& utf8) {
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0)
    return false;
  utf8.resize(static_cast<std::size_t>(size));
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                             static_cast<int>(value.size()), utf8.data(), size, nullptr, nullptr) ==
         size;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2)
    return fail("an app config edit command is required");
  const std::wstring_view command(argv[1]);
  if ((command == L"--remove-owned" && argc < 4) || (command == L"--add" && argc != 4) ||
      (command != L"--remove-owned" && command != L"--add"))
    return fail(
        "usage: appconfig_editor --remove-owned <appconfig.json> <app-key> "
        "[known manifest ...] | --add <appconfig.json> <manifest>");

  const std::filesystem::path config_path(argv[2]);
  std::string original;
  if (!read_file(config_path, original))
    return fail("could not read OpenVR app config");
  constexpr std::string_view bom("\xEF\xBB\xBF", 3);
  const bool has_bom = std::string_view(original).starts_with(bom);
  const std::string_view document(original.data() + (has_bom ? bom.size() : 0),
                                  original.size() - (has_bom ? bom.size() : 0));

  steamvr_gesture_exit::platform::openvr::AppConfigEditResult edit;
  if (command == L"--remove-owned") {
    std::string app_key;
    if (!to_utf8(argv[3], app_key))
      return fail("app key is not valid UTF-8");
    std::vector<std::filesystem::path> known_paths;
    for (int index = 4; index < argc; ++index) {
      if (argv[index][0] != L'\0')
        known_paths.emplace_back(argv[index]);
    }
    if (!steamvr_gesture_exit::platform::openvr::remove_owned_manifest_paths(
            document, app_key, known_paths, edit))
      return fail(edit.error);
  } else {
    std::string manifest;
    if (!to_utf8(argv[3], manifest))
      return fail("manifest path is not valid UTF-8");
    if (!steamvr_gesture_exit::platform::openvr::add_manifest_path(document, manifest, edit))
      return fail(edit.error);
  }
  if (edit.removed == 0 && !edit.added) {
    std::cout << (command == L"--remove-owned" ? "removed=0\n" : "added=0\n");
    return 0;
  }

  std::string updated;
  updated.reserve((has_bom ? bom.size() : 0) + edit.document.size());
  if (has_bom)
    updated.append(bom);
  updated.append(edit.document);

  auto temporary = config_path;
  temporary += L".steamvr-gesture-exit.tmp";
  std::error_code ignored;
  std::filesystem::remove(temporary, ignored);
  if (!write_file(temporary, updated)) {
    std::filesystem::remove(temporary, ignored);
    return fail("could not write temporary OpenVR app config");
  }
  std::string verification;
  if (!read_file(temporary, verification) || verification != updated) {
    std::filesystem::remove(temporary, ignored);
    return fail("temporary OpenVR app config verification failed");
  }
  if (!ReplaceFileW(config_path.c_str(), temporary.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH,
                    nullptr, nullptr)) {
    std::filesystem::remove(temporary, ignored);
    return fail("atomic OpenVR app config replacement failed");
  }
  if (command == L"--remove-owned")
    std::cout << "removed=" << edit.removed << '\n';
  else
    std::cout << "added=1\n";
  return 0;
}
