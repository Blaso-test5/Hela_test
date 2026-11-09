#include "types.h"
#include "crash_handler.h"

#include <dbghelp.h>
#include <cstdarg>
#include <cwchar>
#include <exception>

namespace {

constexpr DWORD kReportTimeoutMs = 60000;

INIT_ONCE g_installOnce = INIT_ONCE_STATIC_INIT;
PVOID g_vehHandle = nullptr;
HANDLE g_requestEvent = nullptr;
HANDLE g_completeEvent = nullptr;
HANDLE g_workerThread = nullptr;
volatile LONG g_handlerBusy = 0;
volatile LONG g_stopWorker = 0;
EXCEPTION_RECORD g_exceptionRecord{};
CONTEXT g_exceptionContext{};
EXCEPTION_POINTERS g_exceptionPointers{};
DWORD g_exceptionThreadId = 0;
std::wstring g_outputDirectory;

bool GetModulePath(const void* address, std::wstring& path, void** moduleBase) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(address, &memory, sizeof(memory)) || !memory.AllocationBase)
        return false;

    HMODULE module = static_cast<HMODULE>(memory.AllocationBase);
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(module, buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return false;
    path.assign(buffer.data(), length);
    if (moduleBase) *moduleBase = module;
    return true;
}

bool GetModuleDirectory(HMODULE module, std::wstring& directory) {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(module, buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return false;

    directory.assign(buffer.data(), length);
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    directory.resize(slash);
    return true;
}

bool UnwindOneFrame(CONTEXT& context) {
    const DWORD64 currentPc = context.Rip;
    if (!currentPc) return false;

    DWORD64 imageBase = 0;
    const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(
        currentPc, &imageBase, nullptr);
    if (function) {
        PVOID handlerData = nullptr;
        DWORD64 establisherFrame = 0;
        __try {
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, currentPc, function,
                &context, &handlerData, &establisherFrame, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return context.Rip != currentPc && context.Rsp != 0;
    }

    DWORD64 returnAddress = 0;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<const void*>(static_cast<ULONG_PTR>(context.Rsp)),
            &returnAddress, sizeof(returnAddress), &bytesRead) ||
        bytesRead != sizeof(returnAddress) || !returnAddress)
        return false;

    context.Rip = returnAddress;
    context.Rsp += sizeof(returnAddress);
    return true;
}

void WriteLine(HANDLE file, const wchar_t* format, ...) {
    wchar_t buffer[2048]{};
    va_list arguments;
    va_start(arguments, format);
    const int length = _vsnwprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, format, arguments);
    va_end(arguments);
    if (length <= 0) return;

    DWORD written = 0;
    WriteFile(file, buffer, static_cast<DWORD>(length * sizeof(wchar_t)), &written, nullptr);
    static constexpr wchar_t newline[] = L"\r\n";
    WriteFile(file, newline, sizeof(newline) - sizeof(wchar_t), &written, nullptr);
}

void WriteCrashReport() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t stem[MAX_PATH]{};
    const int stemLength = _snwprintf_s(stem, ARRAYSIZE(stem), _TRUNCATE,
        L"d3d12on11-crash-%lu-%04u%02u%02u-%02u%02u%02u",
        GetCurrentProcessId(), time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond);
    if (stemLength <= 0) return;

    const std::wstring prefix = g_outputDirectory + L"\\" + stem;
    const std::wstring dumpPath = prefix + L".dmp";
    const std::wstring reportPath = prefix + L".txt";

    HANDLE dump = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD dumpError = ERROR_SUCCESS;
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        exceptionInfo.ThreadId = g_exceptionThreadId;
        exceptionInfo.ExceptionPointers = &g_exceptionPointers;
        exceptionInfo.ClientPointers = FALSE;
        const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                dumpType, &exceptionInfo, nullptr, nullptr)) {
            dumpError = GetLastError();
        }
        CloseHandle(dump);
        if (dumpError != ERROR_SUCCESS) DeleteFileW(dumpPath.c_str());
    } else {
        dumpError = GetLastError();
    }

    HANDLE report = CreateFileW(reportPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (report == INVALID_HANDLE_VALUE) return;

    static constexpr wchar_t bom[] = { 0xFEFF };
    DWORD written = 0;
    WriteFile(report, bom, sizeof(bom), &written, nullptr);

    const DWORD code = g_exceptionRecord.ExceptionCode;
    const ULONG_PTR* parameters = g_exceptionRecord.ExceptionInformation;
    WriteLine(report, L"Vectored first-chance exception report; the exception may be handled.");
    WriteLine(report, L"Exception code: 0x%08lX", code);
    WriteLine(report, L"Exception flags: 0x%08lX", g_exceptionRecord.ExceptionFlags);
    WriteLine(report, L"Exception address: 0x%016llX",
        static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(
            g_exceptionRecord.ExceptionAddress)));
    WriteLine(report, L"Process ID: %lu", GetCurrentProcessId());
    WriteLine(report, L"Thread ID: %lu", g_exceptionThreadId);

    if (code == EXCEPTION_ACCESS_VIOLATION && g_exceptionRecord.NumberParameters >= 2) {
        const wchar_t* operation = parameters[0] == 0 ? L"READ"
            : parameters[0] == 1 ? L"WRITE"
            : parameters[0] == 8 ? L"EXECUTE" : L"UNKNOWN";
        WriteLine(report, L"Access type: %s", operation);
        WriteLine(report, L"Accessed address: 0x%016llX",
            static_cast<unsigned long long>(parameters[1]));
    }

    std::wstring exceptionModule;
    void* exceptionModuleBase = nullptr;
    if (GetModulePath(g_exceptionRecord.ExceptionAddress, exceptionModule,
            &exceptionModuleBase)) {
        const ULONG_PTR offset = reinterpret_cast<ULONG_PTR>(
            g_exceptionRecord.ExceptionAddress) - reinterpret_cast<ULONG_PTR>(
            exceptionModuleBase);
        WriteLine(report, L"Exception module: %s", exceptionModule.c_str());
        WriteLine(report, L"Exception module base: 0x%016llX",
            static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(
                exceptionModuleBase)));
        WriteLine(report, L"Exception module offset: 0x%llX",
            static_cast<unsigned long long>(offset));
    } else {
        WriteLine(report, L"Exception module: <unresolved>");
    }

    if (dumpError == ERROR_SUCCESS)
        WriteLine(report, L"Minidump: %s", dumpPath.c_str());
    else
        WriteLine(report, L"Minidump failed: Win32 error %lu", dumpError);

    CONTEXT context = g_exceptionContext;
    WriteLine(report, L"Stack frames:");
    for (DWORD index = 0; index < 64 && context.Rip; ++index) {
        const DWORD64 address = context.Rip;
        std::wstring modulePath;
        void* moduleBase = nullptr;
        if (GetModulePath(reinterpret_cast<const void*>(
                static_cast<ULONG_PTR>(address)), modulePath, &moduleBase)) {
            const DWORD64 offset = address - reinterpret_cast<ULONG_PTR>(moduleBase);
            WriteLine(report, L"  [%02lu] 0x%016llX %s+0x%llX",
                index, static_cast<unsigned long long>(address),
                modulePath.c_str(), static_cast<unsigned long long>(offset));
        } else {
            WriteLine(report, L"  [%02lu] 0x%016llX <unresolved module>",
                index, static_cast<unsigned long long>(address));
        }

        if (!UnwindOneFrame(context)) break;
    }

    CloseHandle(report);

    wchar_t output[MAX_PATH]{};
    _snwprintf_s(output, ARRAYSIZE(output), _TRUNCATE,
        L"d3d12on11: first-chance exception captured; report: %s\r\n",
        reportPath.c_str());
    OutputDebugStringW(output);
}

DWORD WINAPI CrashReportWorker(void*) {
    for (;;) {
        const DWORD wait = WaitForSingleObject(g_requestEvent, INFINITE);
        if (wait != WAIT_OBJECT_0 || InterlockedCompareExchange(
                &g_stopWorker, 0, 0) != 0)
            return 0;

        try {
            WriteCrashReport();
        } catch (const std::exception& error) {
            char message[256]{};
            snprintf(message, sizeof(message),
                "d3d12on11: crash report generation failed: %s\n", error.what());
            OutputDebugStringA(message);
        } catch (...) {
            OutputDebugStringA(
                "d3d12on11: crash report generation failed with an unknown error\n");
        }
        SetEvent(g_completeEvent);
        InterlockedExchange(&g_handlerBusy, 0);
    }
}

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS info) {
    if (!info || !info->ExceptionRecord || !info->ContextRecord ||
        info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    if (InterlockedCompareExchange(&g_handlerBusy, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    memcpy(&g_exceptionRecord, info->ExceptionRecord, sizeof(g_exceptionRecord));
    memcpy(&g_exceptionContext, info->ContextRecord, sizeof(g_exceptionContext));
    g_exceptionPointers.ExceptionRecord = &g_exceptionRecord;
    g_exceptionPointers.ContextRecord = &g_exceptionContext;
    g_exceptionThreadId = GetCurrentThreadId();

    ResetEvent(g_completeEvent);
    if (!SetEvent(g_requestEvent)) {
        InterlockedExchange(&g_handlerBusy, 0);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    WaitForSingleObject(g_completeEvent, kReportTimeoutMs);
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL CALLBACK InitializeCrashHandler(PINIT_ONCE, PVOID, PVOID*) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&InstallCrashHandler), &module))
        return FALSE;

    if (!GetModuleDirectory(module, g_outputDirectory)) {
        std::vector<wchar_t> buffer(MAX_PATH);
        const DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()),
            buffer.data());
        if (!length || length >= buffer.size()) {
            SetLastError(ERROR_PATH_NOT_FOUND);
            return FALSE;
        }
        g_outputDirectory.assign(buffer.data(), length);
    }

    g_requestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_completeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_requestEvent || !g_completeEvent) {
        const DWORD error = GetLastError();
        if (g_requestEvent) CloseHandle(g_requestEvent);
        if (g_completeEvent) CloseHandle(g_completeEvent);
        g_requestEvent = nullptr;
        g_completeEvent = nullptr;
        SetLastError(error);
        return FALSE;
    }

    g_workerThread = CreateThread(nullptr, 0, CrashReportWorker, nullptr, 0, nullptr);
    if (!g_workerThread) {
        const DWORD error = GetLastError();
        CloseHandle(g_requestEvent);
        CloseHandle(g_completeEvent);
        g_requestEvent = nullptr;
        g_completeEvent = nullptr;
        SetLastError(error);
        return FALSE;
    }

    g_vehHandle = AddVectoredExceptionHandler(1, VectoredExceptionHandler);
    if (!g_vehHandle) {
        const DWORD error = GetLastError();
        InterlockedExchange(&g_stopWorker, 1);
        SetEvent(g_requestEvent);
        WaitForSingleObject(g_workerThread, INFINITE);
        CloseHandle(g_workerThread);
        CloseHandle(g_requestEvent);
        CloseHandle(g_completeEvent);
        g_workerThread = nullptr;
        g_requestEvent = nullptr;
        g_completeEvent = nullptr;
        InterlockedExchange(&g_stopWorker, 0);
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

} // namespace

bool InstallCrashHandler() {
    if (!InitOnceExecuteOnce(&g_installOnce, InitializeCrashHandler, nullptr, nullptr)) {
        Log("[CRASH_HANDLER] Installation failed: Win32 error %lu", GetLastError());
        return false;
    }
    Log("[CRASH_HANDLER] Installed first-chance access-violation diagnostics; output=%ls",
        g_outputDirectory.c_str());
    return true;
}
