#include "core/last_exit_store.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif

namespace steamvr_gesture_exit::core {
namespace {

void discard_temporary_file(const std::filesystem::path& path) {
  auto temporary = path;
  temporary += ".tmp";
  std::error_code ec;
  if (std::filesystem::is_regular_file(temporary, ec))
    std::filesystem::remove(temporary, ec);
}

}  // namespace

LastExitStore::LastExitStore(std::filesystem::path path) : path_(std::move(path)) {}

std::optional<std::chrono::system_clock::time_point> LastExitStore::load(std::string* error) const {
  if (error)
    error->clear();
  discard_temporary_file(path_);
  std::error_code ec;
  if (!std::filesystem::exists(path_, ec)) {
    if (ec && error)
      *error = "LAST_EXIT_STAT_FAILED";
    return std::nullopt;
  }
  std::ifstream input(path_, std::ios::binary);
  const std::string document{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
  if ((!input.good() && !input.eof()) || document.empty() || document.size() > 32) {
    if (error)
      *error = "LAST_EXIT_READ_FAILED";
    return std::nullopt;
  }
  std::int64_t unix_seconds = 0;
  const char* begin = document.data();
  const char* end = begin + document.size();
  auto parsed = std::from_chars(begin, end, unix_seconds);
  const char* remainder = parsed.ptr;
  if (remainder != end && *remainder == '\r')
    ++remainder;
  if (remainder != end && *remainder == '\n')
    ++remainder;
  const auto maximum =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::duration::max())
          .count();
  if (parsed.ec != std::errc{} || remainder != end || unix_seconds < 0 || unix_seconds > maximum) {
    if (error)
      *error = "LAST_EXIT_INVALID";
    return std::nullopt;
  }
  return std::chrono::system_clock::time_point{std::chrono::seconds{unix_seconds}};
}

bool LastExitStore::save(const std::chrono::system_clock::time_point time,
                         std::string* error) const {
  if (error)
    error->clear();
  const auto unix_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
  if (unix_seconds < 0) {
    if (error)
      *error = "LAST_EXIT_INVALID";
    return false;
  }
  std::error_code ec;
  if (!path_.parent_path().empty()) {
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) {
      if (error)
        *error = "LAST_EXIT_CREATE_DIRECTORY_FAILED";
      return false;
    }
  }
  auto temporary = path_;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << unix_seconds << '\n';
    output.flush();
    if (!output) {
      output.close();
      discard_temporary_file(path_);
      if (error)
        *error = "LAST_EXIT_WRITE_FAILED";
      return false;
    }
  }
#ifdef _WIN32
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  const bool flushed = file != INVALID_HANDLE_VALUE && FlushFileBuffers(file) != 0;
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  if (!flushed) {
    discard_temporary_file(path_);
    if (error)
      *error = "LAST_EXIT_FLUSH_FAILED";
    return false;
  }
  if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
  }
#else
  std::filesystem::rename(temporary, path_, ec);
#endif
  if (ec) {
    discard_temporary_file(path_);
    if (error)
      *error = "LAST_EXIT_REPLACE_FAILED";
    return false;
  }
  return true;
}

}  // namespace steamvr_gesture_exit::core
