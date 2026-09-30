#include "platform/openvr/appconfig_editor.hpp"

#include "json/json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace steamvr_gesture_exit::platform::openvr {
namespace {

bool parse_json(std::string_view document, Json::Value& root, std::string& error) {
  Json::CharReaderBuilder builder;
  Json::CharReaderBuilder::strictMode(&builder.settings_);
  builder["collectComments"] = false;
  const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  return reader->parse(document.data(), document.data() + document.size(), &root, &error);
}

bool read_file(const std::filesystem::path& path, std::string& bytes) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

std::string_view without_utf8_bom(std::string_view bytes) {
  constexpr std::string_view bom("\xEF\xBB\xBF", 3);
  return bytes.starts_with(bom) ? bytes.substr(bom.size()) : bytes;
}

std::optional<std::filesystem::path> path_from_utf8(std::string_view value) {
  if (value.find('\0') != std::string_view::npos)
    return std::nullopt;
#ifdef _WIN32
  if (value.empty())
    return std::filesystem::path{};
  if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return std::nullopt;
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0)
    return std::nullopt;
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), wide.data(), length) != length)
    return std::nullopt;
  return std::filesystem::path(std::move(wide));
#else
  try {
    const std::u8string utf8(reinterpret_cast<const char8_t*>(value.data()), value.size());
    return std::filesystem::path(utf8);
  } catch (...) {
    return std::nullopt;
  }
#endif
}

std::filesystem::path full_lexical_path(const std::filesystem::path& path) {
  std::error_code error;
  auto full = std::filesystem::absolute(path, error);
  return (error ? path : full).lexically_normal();
}

bool paths_equal(const std::filesystem::path& left, const std::filesystem::path& right) {
  const auto a = full_lexical_path(left).native();
  const auto b = full_lexical_path(right).native();
#ifdef _WIN32
  return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
#else
  return a == b;
#endif
}

bool filename_is_owned(const std::filesystem::path& path) {
  const auto filename = path.filename().native();
#ifdef _WIN32
  constexpr wchar_t expected[] = L"steamvr_gesture_exit.vrmanifest";
  return CompareStringOrdinal(filename.c_str(), static_cast<int>(filename.size()), expected, -1,
                              TRUE) == CSTR_EQUAL;
#else
  return filename == "steamvr_gesture_exit.vrmanifest";
#endif
}

bool manifest_has_app_key(const std::filesystem::path& path, std::string_view app_key) {
  std::string bytes;
  if (!read_file(path, bytes))
    return false;
  Json::Value root;
  std::string error;
  if (!parse_json(without_utf8_bom(bytes), root, error) || !root.isObject())
    return false;
  const auto& applications = root["applications"];
  return applications.isArray() && applications.size() == 1 && applications[0].isObject() &&
         applications[0]["app_key"].isString() &&
         applications[0]["app_key"].asString() == app_key;
}

bool is_owned_manifest(std::string_view value, std::string_view app_key,
                       const std::vector<std::filesystem::path>& known_paths) {
  const auto path = path_from_utf8(value);
  if (!path)
    return false;
  if (std::ranges::any_of(known_paths,
                          [&](const auto& known) { return paths_equal(*path, known); }))
    return true;
  std::error_code error;
  if (!std::filesystem::is_regular_file(*path, error))
    return filename_is_owned(*path);
  return manifest_has_app_key(*path, app_key);
}

struct Gap {
  std::string_view before_comma;
  std::string_view after_comma;
};

std::optional<Gap> split_gap(std::string_view gap) {
  const auto comma = gap.find(',');
  if (comma == std::string_view::npos || gap.find(',', comma + 1) != std::string_view::npos)
    return std::nullopt;
  const auto whitespace_only = [](std::string_view value) {
    return std::ranges::all_of(value, [](unsigned char character) {
      return std::isspace(character) != 0;
    });
  };
  const auto before = gap.substr(0, comma);
  const auto after = gap.substr(comma + 1);
  if (!whitespace_only(before) || !whitespace_only(after))
    return std::nullopt;
  return Gap{before, after};
}

std::string quote_json_string(std::string_view value) {
  Json::StreamWriterBuilder writer;
  writer["commentStyle"] = "None";
  writer["indentation"] = "";
  return Json::writeString(writer, Json::Value(std::string(value)));
}

}  // namespace

bool remove_owned_manifest_paths(std::string_view document, std::string_view app_key,
                                 const std::vector<std::filesystem::path>& known_paths,
                                 AppConfigEditResult& result) {
  result = {};
  Json::Value root;
  if (!parse_json(document, root, result.error) || !root.isObject()) {
    if (result.error.empty())
      result.error = "OpenVR app config root is not an object.";
    return false;
  }
  if (!root.isMember("manifest_paths")) {
    result.document.assign(document);
    return true;
  }
  const auto& paths = root["manifest_paths"];
  if (!paths.isArray()) {
    result.error = "OpenVR manifest_paths is not an array.";
    return false;
  }

  const auto array_start = paths.getOffsetStart();
  const auto array_limit = paths.getOffsetLimit();
  if (array_start >= array_limit || array_limit > document.size() || document[array_start] != '[' ||
      document[array_limit - 1] != ']') {
    result.error = "JsonCpp returned invalid manifest_paths source offsets.";
    return false;
  }

  std::vector<std::size_t> kept;
  Json::Value expected_paths(Json::arrayValue);
  for (Json::ArrayIndex index = 0; index < paths.size(); ++index) {
    const auto& path = paths[index];
    if (!path.isString() || path.getOffsetStart() < array_start + 1 ||
        path.getOffsetLimit() > array_limit - 1 || path.getOffsetStart() >= path.getOffsetLimit()) {
      result.error = "OpenVR manifest_paths contains an invalid string entry.";
      return false;
    }
    if (is_owned_manifest(path.asString(), app_key, known_paths)) {
      ++result.removed;
    } else {
      kept.push_back(index);
      expected_paths.append(path);
    }
  }
  if (result.removed == 0) {
    result.document.assign(document);
    return true;
  }

  const auto inner_start = array_start + 1;
  const auto inner_limit = array_limit - 1;
  std::string replacement;
  if (kept.empty()) {
    replacement.assign(document.substr(paths[paths.size() - 1].getOffsetLimit(),
                                        inner_limit - paths[paths.size() - 1].getOffsetLimit()));
  } else {
    const auto first = kept.front();
    if (first == 0) {
      replacement.append(document.substr(inner_start, paths[0].getOffsetStart() - inner_start));
    } else {
      const auto gap = split_gap(document.substr(
          paths[static_cast<Json::ArrayIndex>(first - 1)].getOffsetLimit(),
          paths[static_cast<Json::ArrayIndex>(first)].getOffsetStart() -
              paths[static_cast<Json::ArrayIndex>(first - 1)].getOffsetLimit()));
      if (!gap) {
        result.error = "Could not preserve the first manifest_paths separator.";
        return false;
      }
      replacement.append(gap->after_comma);
    }

    for (std::size_t kept_index = 0; kept_index < kept.size(); ++kept_index) {
      const auto original = static_cast<Json::ArrayIndex>(kept[kept_index]);
      replacement.append(document.substr(paths[original].getOffsetStart(),
                                          paths[original].getOffsetLimit() -
                                              paths[original].getOffsetStart()));
      if (kept_index + 1 == kept.size())
        continue;
      const auto next = static_cast<Json::ArrayIndex>(kept[kept_index + 1]);
      const auto left_gap = split_gap(document.substr(
          paths[original].getOffsetLimit(),
          paths[original + 1].getOffsetStart() - paths[original].getOffsetLimit()));
      const auto right_gap = split_gap(document.substr(
          paths[next - 1].getOffsetLimit(),
          paths[next].getOffsetStart() - paths[next - 1].getOffsetLimit()));
      if (!left_gap || !right_gap) {
        result.error = "Could not preserve a manifest_paths separator.";
        return false;
      }
      replacement.append(left_gap->before_comma);
      replacement.push_back(',');
      replacement.append(right_gap->after_comma);
    }
    replacement.append(document.substr(paths[paths.size() - 1].getOffsetLimit(),
                                        inner_limit - paths[paths.size() - 1].getOffsetLimit()));
  }

  result.document.reserve(document.size());
  result.document.append(document.substr(0, inner_start));
  result.document.append(replacement);
  result.document.append(document.substr(inner_limit));

  Json::Value expected = root;
  expected["manifest_paths"] = std::move(expected_paths);
  Json::Value verified;
  std::string verification_error;
  if (!parse_json(result.document, verified, verification_error) || verified != expected) {
    result.error = "Edited OpenVR app config failed semantic verification: " + verification_error;
    result.document.clear();
    result.removed = 0;
    return false;
  }
  return true;
}

bool add_manifest_path(std::string_view document, std::string_view manifest_path,
                       AppConfigEditResult& result) {
  result = {};
  Json::Value root;
  if (!parse_json(document, root, result.error) || !root.isObject()) {
    if (result.error.empty())
      result.error = "OpenVR app config root is not an object.";
    return false;
  }
  if (!root.isMember("manifest_paths") || !root["manifest_paths"].isArray()) {
    result.error = "OpenVR manifest_paths array is missing.";
    return false;
  }
  const auto manifest = path_from_utf8(manifest_path);
  if (!manifest) {
    result.error = "Manifest path is not valid UTF-8.";
    return false;
  }

  const auto& paths = root["manifest_paths"];
  for (const auto& path : paths) {
    if (!path.isString()) {
      result.error = "OpenVR manifest_paths contains a non-string entry.";
      return false;
    }
    const auto existing = path_from_utf8(path.asString());
    if (existing && paths_equal(*existing, *manifest)) {
      result.document.assign(document);
      return true;
    }
  }

  const auto array_start = paths.getOffsetStart();
  const auto array_limit = paths.getOffsetLimit();
  if (array_start >= array_limit || array_limit > document.size() || document[array_start] != '[' ||
      document[array_limit - 1] != ']') {
    result.error = "JsonCpp returned invalid manifest_paths source offsets.";
    return false;
  }
  const auto inner_start = array_start + 1;
  const auto inner_limit = array_limit - 1;
  const auto quoted = quote_json_string(manifest_path);
  std::string replacement;
  if (paths.empty()) {
    const auto whitespace = document.substr(inner_start, inner_limit - inner_start);
    const bool multiline = whitespace.find('\n') != std::string_view::npos ||
                           whitespace.find('\r') != std::string_view::npos;
    replacement.append(whitespace);
    if (multiline)
      replacement.append("  ");
    replacement.append(quoted);
    replacement.append(whitespace);
  } else {
    for (const auto& path : paths) {
      if (path.getOffsetStart() < inner_start || path.getOffsetLimit() > inner_limit ||
          path.getOffsetStart() >= path.getOffsetLimit()) {
        result.error = "OpenVR manifest_paths contains an invalid string entry.";
        return false;
      }
    }
    const auto& first = paths[0];
    const auto& last = paths[paths.size() - 1];
    const auto leading = document.substr(inner_start, first.getOffsetStart() - inner_start);
    replacement.append(document.substr(inner_start, last.getOffsetLimit() - inner_start));
    replacement.push_back(',');
    replacement.append(leading);
    replacement.append(quoted);
    replacement.append(document.substr(last.getOffsetLimit(), inner_limit - last.getOffsetLimit()));
  }

  result.document.reserve(document.size() + quoted.size() + 8);
  result.document.append(document.substr(0, inner_start));
  result.document.append(replacement);
  result.document.append(document.substr(inner_limit));

  Json::Value expected = root;
  expected["manifest_paths"].append(std::string(manifest_path));
  Json::Value verified;
  std::string verification_error;
  if (!parse_json(result.document, verified, verification_error) || verified != expected) {
    result.error = "Edited OpenVR app config failed semantic verification: " + verification_error;
    result.document.clear();
    return false;
  }
  result.added = true;
  return true;
}

}  // namespace steamvr_gesture_exit::platform::openvr
