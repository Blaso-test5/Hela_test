#include "crash_handler.h"

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

void Log(const char*, ...) {}

static void RaiseHandledAccessViolation() {
    __try {
        const ULONG_PTR parameters[] = { 1, 0x1234 };
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0,
            static_cast<DWORD>(ARRAYSIZE(parameters)), parameters);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool FindReport(const std::wstring& pattern, std::wstring& path) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return false;

    path = pattern.substr(0, pattern.find_last_of(L"\\/") + 1) + data.cFileName;
    FindClose(find);
    return true;
}

int main() {
    if (!InstallCrashHandler()) {
        std::fprintf(stderr, "InstallCrashHandler failed\n");
        return 1;
    }

    RaiseHandledAccessViolation();
    Sleep(100);

    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (!length || length >= ARRAYSIZE(modulePath)) {
        std::fprintf(stderr, "GetModuleFileNameW failed\n");
        return 2;
    }

    std::wstring directory(modulePath, length);
    directory.resize(directory.find_last_of(L"\\/"));
    wchar_t pattern[MAX_PATH]{};
    _snwprintf_s(pattern, ARRAYSIZE(pattern), _TRUNCATE,
        L"%s\\d3d12on11-crash-%lu-*.txt", directory.c_str(), GetCurrentProcessId());

    std::wstring reportPath;
    if (!FindReport(pattern, reportPath)) {
        std::fprintf(stderr, "Crash report was not created\n");
        return 3;
    }

    std::wstring dumpPattern(pattern);
    dumpPattern.replace(dumpPattern.size() - 3, 3, L"dmp");
    std::wstring dumpPath;
    if (!FindReport(dumpPattern, dumpPath)) {
        std::fprintf(stderr, "Minidump was not created\n");
        return 4;
    }

    HANDLE report = CreateFileW(reportPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (report == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "Could not open crash report\n");
        return 5;
    }

    LARGE_INTEGER size{};
    GetFileSizeEx(report, &size);
    std::vector<wchar_t> contents(static_cast<size_t>(size.QuadPart) /
        sizeof(wchar_t) + 1);
    DWORD bytesRead = 0;
    const BOOL readOk = ReadFile(report, contents.data(),
        static_cast<DWORD>(size.QuadPart), &bytesRead, nullptr);
    CloseHandle(report);
    if (!readOk || !wcsstr(contents.data(), L"Exception code: 0xC0000005") ||
        !wcsstr(contents.data(), L"Access type: WRITE") ||
        !wcsstr(contents.data(), L"Exception module offset:") ||
        !wcsstr(contents.data(), L"Stack frames:") ||
        !wcsstr(contents.data(), L"d3d12on11_crash_handler_smoke.exe+0x")) {
        std::fprintf(stderr, "Crash report did not contain the expected exception details\n");
        return 6;
    }

    WIN32_FILE_ATTRIBUTE_DATA dumpAttributes{};
    if (!GetFileAttributesExW(dumpPath.c_str(), GetFileExInfoStandard,
            &dumpAttributes) || dumpAttributes.nFileSizeLow == 0) {
        std::fprintf(stderr, "Minidump is empty\n");
        return 7;
    }

    std::wprintf(L"Crash handler report and minidump created: %s\n", reportPath.c_str());
    return 0;
}
