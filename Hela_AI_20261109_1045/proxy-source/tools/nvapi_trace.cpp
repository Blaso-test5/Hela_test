#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cstdio>

namespace {

using NvapiQueryInterface = void* (__cdecl*)(std::uint32_t);

HMODULE g_self = nullptr;
HMODULE g_originalModule = nullptr;
NvapiQueryInterface g_originalQueryInterface = nullptr;
INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;

void WriteTrace(const char* message) {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(g_self, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path))
        return;

    wchar_t* separator = wcsrchr(path, L'\\');
    if (!separator)
        return;

    wcscpy_s(separator + 1, ARRAYSIZE(path) - (separator + 1 - path),
        L"nvapi_query_trace.log");

    HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(file, message, static_cast<DWORD>(strlen(message)), &written, nullptr);
    CloseHandle(file);
}

BOOL CALLBACK InitializeOriginal(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(g_self, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) {
        char message[160] = {};
        sprintf_s(message, "NVAPI trace: cannot resolve shim path, error=%lu\r\n",
            GetLastError());
        WriteTrace(message);
        return TRUE;
    }

    wchar_t* separator = wcsrchr(path, L'\\');
    if (!separator) {
        WriteTrace("NVAPI trace: shim path has no directory separator\r\n");
        return TRUE;
    }

    wcscpy_s(separator + 1, ARRAYSIZE(path) - (separator + 1 - path),
        L"nvapi64_real.dll");
    g_originalModule = LoadLibraryW(path);
    if (!g_originalModule) {
        char message[160] = {};
        sprintf_s(message, "NVAPI trace: loading nvapi64_real.dll failed, error=%lu\r\n",
            GetLastError());
        WriteTrace(message);
        return TRUE;
    }

    g_originalQueryInterface = reinterpret_cast<NvapiQueryInterface>(
        GetProcAddress(g_originalModule, "nvapi_QueryInterface"));
    if (!g_originalQueryInterface) {
        char message[160] = {};
        sprintf_s(message, "NVAPI trace: resolving nvapi_QueryInterface failed, error=%lu\r\n",
            GetLastError());
        WriteTrace(message);
        return TRUE;
    }

    WriteTrace("NVAPI trace: original NVAPI loaded; QueryInterface forwarding enabled\r\n");
    return TRUE;
}

}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" void* __cdecl nvapi_QueryInterface(std::uint32_t interfaceId) {
    if (!InitOnceExecuteOnce(&g_initOnce, InitializeOriginal, nullptr, nullptr)) {
        char message[160] = {};
        sprintf_s(message, "NVAPI trace: initialization failed, error=%lu\r\n",
            GetLastError());
        WriteTrace(message);
        return nullptr;
    }

    void* result = g_originalQueryInterface
        ? g_originalQueryInterface(interfaceId)
        : nullptr;
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    char message[192] = {};
    sprintf_s(message,
        "[%02u:%02u:%02u.%03u] QueryInterface id=0x%08lx result=%p thread=%lu\r\n",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
        static_cast<unsigned long>(interfaceId), result, GetCurrentThreadId());
    WriteTrace(message);
    return result;
}
