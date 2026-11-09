#define D3D12CreateDevice D3D12CreateDeviceSdkDeclaration
#define D3D12GetDebugInterface D3D12GetDebugInterfaceSdkDeclaration
#define D3D12SerializeRootSignature D3D12SerializeRootSignatureSdkDeclaration
#define D3D12SerializeVersionedRootSignature D3D12SerializeVersionedRootSignatureSdkDeclaration
#include "types.h"
#include "bridge.h"
#undef D3D12CreateDevice
#undef D3D12GetDebugInterface
#undef D3D12SerializeRootSignature
#undef D3D12SerializeVersionedRootSignature

namespace {
INIT_ONCE configOnce = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK InitializeConfig(PINIT_ONCE, PVOID parameter, PVOID*) {
    LoadConfig(static_cast<HMODULE>(parameter));
    return TRUE;
}

HRESULT EnsureConfig() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&EnsureConfig), &module))
        return HRESULT_FROM_WIN32(GetLastError());
    if (!InitOnceExecuteOnce(&configOnce, InitializeConfig, module, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

HMODULE GetSystemD3D12() {
    static HMODULE module = [] {
        wchar_t path[MAX_PATH] = {};
        const UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
        if (!length || length >= ARRAYSIZE(path)) return static_cast<HMODULE>(nullptr);
        std::wstring fullPath(path, length);
        fullPath += L"\\d3d12.dll";
        return LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    return module;
}
}

extern "C" HRESULT WINAPI D3D12CreateDevice(
    IUnknown* adapterUnknown,
    D3D_FEATURE_LEVEL minimumFeatureLevel,
    REFIID riid,
    void** deviceOut) {
    if (!deviceOut) return E_POINTER;
    *deviceOut = nullptr;

    const HRESULT configResult = EnsureConfig();
    if (FAILED(configResult)) return configResult;
    D3D12ON11_TRACE_CALLER("D3D12CreateDevice");
    Log("D3D12CreateDevice minimum feature level 0x%04x iid=%08lx-%04x-%04x",
        static_cast<UINT>(minimumFeatureLevel), riid.Data1, riid.Data2, riid.Data3);

    IDXGIAdapter* adapter = nullptr;
    bool adapterUnwrapped = false;
    if (adapterUnknown) {
        Ptr<ID3D12On11AdapterBridge> adapterBridge;
        const HRESULT bridgeResult = adapterUnknown->QueryInterface(
            __uuidof(ID3D12On11AdapterBridge),
            reinterpret_cast<void**>(adapterBridge.Put()));
        adapterUnwrapped = SUCCEEDED(bridgeResult);
        const HRESULT queryResult = SUCCEEDED(bridgeResult)
            ? adapterBridge->GetNativeAdapter(&adapter)
            : adapterUnknown->QueryInterface(__uuidof(IDXGIAdapter),
                reinterpret_cast<void**>(&adapter));
        if (FAILED(queryResult)) return queryResult;
    }
    if (adapter) {
        DXGI_ADAPTER_DESC adapterDesc{};
        const HRESULT descResult = adapter->GetDesc(&adapterDesc);
        if (SUCCEEDED(descResult)) {
            Log("D3D12CreateDevice adapter source=%s native-vendor=0x%04x device=0x%04x luid=%08lx:%08lx",
                adapterUnwrapped ? "proxy-unwrap" : "direct",
                adapterDesc.VendorId, adapterDesc.DeviceId,
                static_cast<unsigned long>(adapterDesc.AdapterLuid.HighPart),
                static_cast<unsigned long>(adapterDesc.AdapterLuid.LowPart));
        } else {
            Log("D3D12CreateDevice adapter source=%s GetDesc failed (0x%08lx)",
                adapterUnwrapped ? "proxy-unwrap" : "direct", descResult);
        }
    } else {
        Log("D3D12CreateDevice adapter source=default-hardware");
    }

    auto* device = new (std::nothrow) Device(adapter);
    if (adapter) adapter->Release();
    if (!device) return E_OUTOFMEMORY;

    const HRESULT initResult = device->Init(minimumFeatureLevel);
    if (FAILED(initResult)) {
        Log("D3D12CreateDevice backend initialization failed (0x%08lx)", initResult);
        device->Release();
        return initResult;
    }

    const HRESULT queryResult = device->QueryInterface(riid, deviceOut);
    if (FAILED(queryResult))
        Log("D3D12CreateDevice QueryInterface failed for iid=%08lx-%04x-%04x (0x%08lx)",
            riid.Data1, riid.Data2, riid.Data3, queryResult);
    device->Release();
    return queryResult;
}

extern "C" HRESULT WINAPI D3D12GetDebugInterface(
    REFIID,
    void** debugInterface) {
    if (!debugInterface) return E_POINTER;
    *debugInterface = nullptr;
    if (SUCCEEDED(EnsureConfig()))
        Log("D3D12GetDebugInterface requested");
    return DXGI_ERROR_SDK_COMPONENT_MISSING;
}

extern "C" HRESULT WINAPI D3D12SerializeRootSignature(
    const D3D12_ROOT_SIGNATURE_DESC* desc,
    D3D_ROOT_SIGNATURE_VERSION version,
    ID3DBlob** blob,
    ID3DBlob** errorBlob) {
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*,
        D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    const HMODULE module = GetSystemD3D12();
    const auto serialize = module
        ? reinterpret_cast<SerializeFn>(GetProcAddress(module, "D3D12SerializeRootSignature"))
        : nullptr;
    return serialize ? serialize(desc, version, blob, errorBlob)
        : HRESULT_FROM_WIN32(GetLastError());
}

extern "C" HRESULT WINAPI D3D12SerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc,
    ID3DBlob** blob,
    ID3DBlob** errorBlob) {
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*,
        ID3DBlob**, ID3DBlob**);
    const HMODULE module = GetSystemD3D12();
    const auto serialize = module
        ? reinterpret_cast<SerializeFn>(GetProcAddress(module, "D3D12SerializeVersionedRootSignature"))
        : nullptr;
    return serialize ? serialize(desc, blob, errorBlob)
        : HRESULT_FROM_WIN32(GetLastError());
}

extern "C" __declspec(dllexport) void WINAPI D3D12On11LogUnsupported(const char* operation) {
    if (!operation) return;
    if (FAILED(EnsureConfig())) {
        OutputDebugStringA("d3d12on11: could not initialize logging configuration\n");
        return;
    }
    Unimpl(operation);
}

extern "C" __declspec(dllexport) void WINAPI D3D12On11LogMessage(const char* message) {
    if (!message) return;
    if (FAILED(EnsureConfig())) {
        OutputDebugStringA("d3d12on11: could not initialize logging configuration\n");
        return;
    }
    Log("%s", message);
}

extern "C" __declspec(dllexport) void WINAPI D3D12On11TraceCaller(
    const char* api, const void* returnAddress) {
    if (!api) return;
    if (FAILED(EnsureConfig())) {
        OutputDebugStringA("d3d12on11: could not initialize caller tracing configuration\n");
        return;
    }
    TraceCaller(api, returnAddress);
}

extern "C" __declspec(dllexport) void WINAPI D3D12On11TraceApi(const char* api) {
    if (!api) return;
    if (FAILED(EnsureConfig())) {
        OutputDebugStringA("d3d12on11: could not initialize render tracing configuration\n");
        return;
    }
    TraceApi(api);
}

extern "C" __declspec(dllexport) void WINAPI D3D12On11TraceSwapChainCreated() {
    if (FAILED(EnsureConfig())) {
        OutputDebugStringA("d3d12on11: could not initialize swapchain tracing configuration\n");
        return;
    }
    if (Cfg().diagnosticRenderTrace) {
        TraceStats().swapChainCreated.fetch_add(1, std::memory_order_relaxed);
        Log("[RENDER_TRACE] [tid=%lu] DXGI swapchain created", GetCurrentThreadId());
    }
}

extern "C" __declspec(dllexport) void WINAPI D3D12On11TracePresent(const char* api, HRESULT result) {
    if (FAILED(EnsureConfig())) {
        OutputDebugStringA("d3d12on11: could not initialize present tracing configuration\n");
        return;
    }
    if (Cfg().diagnosticRenderTrace) {
        TraceStats().presentCount.fetch_add(1, std::memory_order_relaxed);
        Log("[RENDER_TRACE] [tid=%lu] %s hr=0x%08lx",
            GetCurrentThreadId(), api ? api : "DXGI Present", result);
    }
}
