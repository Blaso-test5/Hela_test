#pragma once

#include <intrin.h>

inline void TraceCaller(const char* api, const void* returnAddress) {
    if (!Cfg().traceCallers)
        return;

    char modulePath[MAX_PATH] = {};
    const char* moduleName = "<unknown>";
    uintptr_t relativeAddress = reinterpret_cast<uintptr_t>(returnAddress);
    HMODULE module = nullptr;
    if (returnAddress && GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(returnAddress), &module)) {
        const DWORD length = GetModuleFileNameA(module, modulePath, ARRAYSIZE(modulePath));
        if (length && length < ARRAYSIZE(modulePath)) {
            const char* baseName = strrchr(modulePath, '\\');
            moduleName = baseName ? baseName + 1 : modulePath;
        }
        relativeAddress -= reinterpret_cast<uintptr_t>(module);
    }

    char message[1024] = {};
    snprintf(message, sizeof(message),
        "[d3d12on11] TRACE api=%s caller=%s+0x%llx thread=%lu",
        api, moduleName, static_cast<unsigned long long>(relativeAddress),
        GetCurrentThreadId());
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
    Log("%s", message);
}

#define D3D12ON11_TRACE_CALLER(api) TraceCaller(api, _ReturnAddress())
