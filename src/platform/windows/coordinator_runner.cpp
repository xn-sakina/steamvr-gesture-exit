#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace {

constexpr DWORD kParentExited = ERROR_PROCESS_ABORTED;

std::wstring quote_argument(const std::wstring& value) {
  if (value.empty()) {
    return L"\"\"";
  }
  if (value.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return value;
  }

  std::wstring quoted = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t character : value) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'\"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted.push_back(L'\"');
      backslashes = 0;
      continue;
    }
    quoted.append(backslashes, L'\\');
    backslashes = 0;
    quoted.push_back(character);
  }
  quoted.append(backslashes * 2, L'\\');
  quoted.push_back(L'\"');
  return quoted;
}

DWORD parse_process_id(const wchar_t* value) {
  if (value == nullptr || *value == L'\0') {
    return 0;
  }
  wchar_t* end = nullptr;
  const unsigned long parsed = std::wcstoul(value, &end, 10);
  if (end == value || *end != L'\0' || parsed == 0) {
    return 0;
  }
  return static_cast<DWORD>(parsed);
}

int fail_with_last_error() {
  const DWORD error = GetLastError();
  return error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : static_cast<int>(error);
}

}  // namespace

int wmain() {
  int argument_count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments == nullptr) {
    return fail_with_last_error();
  }

  int command_index = -1;
  DWORD parent_id = 0;
  for (int index = 1; index < argument_count; ++index) {
    if (std::wcscmp(arguments[index], L"--parent-pid") == 0 && index + 1 < argument_count) {
      parent_id = parse_process_id(arguments[++index]);
    } else if (std::wcscmp(arguments[index], L"--") == 0) {
      command_index = index + 1;
      break;
    } else {
      LocalFree(arguments);
      return ERROR_BAD_ARGUMENTS;
    }
  }
  if (parent_id == 0 || command_index < 0 || command_index >= argument_count) {
    LocalFree(arguments);
    return ERROR_BAD_ARGUMENTS;
  }

  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_id);
  if (parent == nullptr) {
    LocalFree(arguments);
    return fail_with_last_error();
  }

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job == nullptr) {
    CloseHandle(parent);
    LocalFree(arguments);
    return fail_with_last_error();
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    const int result = fail_with_last_error();
    CloseHandle(job);
    CloseHandle(parent);
    LocalFree(arguments);
    return result;
  }

  std::wstring command_line;
  for (int index = command_index; index < argument_count; ++index) {
    if (!command_line.empty()) {
      command_line.push_back(L' ');
    }
    command_line += quote_argument(arguments[index]);
  }
  const std::wstring application = arguments[command_index];
  LocalFree(arguments);

  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(application.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                      CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, nullptr,
                      nullptr, &startup, &process)) {
    const int result = fail_with_last_error();
    CloseHandle(job);
    CloseHandle(parent);
    return result;
  }

  if (!AssignProcessToJobObject(job, process.hProcess)) {
    const int result = fail_with_last_error();
    TerminateProcess(process.hProcess, static_cast<UINT>(result));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    CloseHandle(parent);
    return result;
  }
  if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
    const int result = fail_with_last_error();
    TerminateJobObject(job, static_cast<UINT>(result));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    CloseHandle(parent);
    return result;
  }
  CloseHandle(process.hThread);

  HANDLE waits[] = {parent, process.hProcess};
  const DWORD wait_result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
  DWORD exit_code = ERROR_GEN_FAILURE;
  if (wait_result == WAIT_OBJECT_0) {
    TerminateJobObject(job, kParentExited);
    WaitForSingleObject(process.hProcess, INFINITE);
    exit_code = kParentExited;
  } else if (wait_result == WAIT_OBJECT_0 + 1) {
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
      exit_code = GetLastError();
    }
  } else {
    exit_code = GetLastError();
    TerminateJobObject(job, exit_code == ERROR_SUCCESS ? ERROR_GEN_FAILURE : exit_code);
  }

  CloseHandle(process.hProcess);
  CloseHandle(job);
  CloseHandle(parent);
  return static_cast<int>(exit_code);
}
