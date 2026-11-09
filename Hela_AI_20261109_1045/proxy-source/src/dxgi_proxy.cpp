#define CreateDXGIFactory CreateDXGIFactorySystemDeclaration
#define CreateDXGIFactory1 CreateDXGIFactory1SystemDeclaration
#define CreateDXGIFactory2 CreateDXGIFactory2SystemDeclaration
#include "core.h"
#undef CreateDXGIFactory
#undef CreateDXGIFactory1
#undef CreateDXGIFactory2

#include "bridge.h"
#include "dxgi_factory_fwd.inc"
#include "dxgi_swapchain_fwd.inc"

namespace {

bool DiagnosticMaskNvidiaAdapter() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"D3D12ON11_DIAGNOSTIC_MASK_NVIDIA",
        value, ARRAYSIZE(value)) == 1 && value[0] == L'1';
}

UINT DiagnosticMaskedVendorId() {
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(L"D3D12ON11_MASK_VENDOR_ID",
        value, ARRAYSIZE(value));
    if (!length) return 0x1414;
    if (length >= ARRAYSIZE(value)) {
        D3D12On11LogMessage("D3D12ON11_MASK_VENDOR_ID is too long; using default 0x1414");
        return 0x1414;
    }

    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(value, &end, 16);
    if (end == value || *end != L'\0' || parsed > UINT_MAX) {
        D3D12On11LogMessage("D3D12ON11_MASK_VENDOR_ID is invalid; using default 0x1414");
        return 0x1414;
    }
    return static_cast<UINT>(parsed);
}

class SystemDxgi {
public:
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    using CreateFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);

    HMODULE module = nullptr;
    CreateFactoryFn createFactory = nullptr;
    CreateFactoryFn createFactory1 = nullptr;
    CreateFactory2Fn createFactory2 = nullptr;

    SystemDxgi() {
        std::vector<wchar_t> path(32768);
        const UINT length = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
        if (!length || length >= path.size()) return;
        std::wstring fullPath(path.data(), length);
        fullPath += L"\\dxgi.dll";
        module = LoadLibraryW(fullPath.c_str());
        if (!module) return;
        createFactory = reinterpret_cast<CreateFactoryFn>(GetProcAddress(module, "CreateDXGIFactory"));
        createFactory1 = reinterpret_cast<CreateFactoryFn>(GetProcAddress(module, "CreateDXGIFactory1"));
        createFactory2 = reinterpret_cast<CreateFactory2Fn>(GetProcAddress(module, "CreateDXGIFactory2"));
    }

    HRESULT Create(UINT flags, REFIID riid, void** out) const {
        if (!module) return HRESULT_FROM_WIN32(GetLastError());
        if (createFactory2) return createFactory2(flags, riid, out);
        if (flags) {
            D3D12On11LogUnsupported("DXGI CreateDXGIFactory2 flags fallback");
            return E_NOTIMPL;
        }
        if (createFactory1) return createFactory1(riid, out);
        if (createFactory) return createFactory(riid, out);
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    FARPROC GetExport(const char* name) const {
        return module ? GetProcAddress(module, name) : nullptr;
    }
};

SystemDxgi& GetSystemDxgi() {
    static SystemDxgi dxgi;
    return dxgi;
}

class Factory;

class Adapter final : public IDXGIAdapter4, public ID3D12On11AdapterBridge {
    std::atomic<ULONG> refs{1};
    Ptr<IDXGIAdapter4> real;

    template <class T>
    HRESULT GetMaskedDesc(HRESULT (STDMETHODCALLTYPE IDXGIAdapter4::*getDesc)(T*), T* desc) {
        D3D12On11TraceCaller("Adapter::GetDesc", _ReturnAddress());
        if (!desc) return E_POINTER;
        const HRESULT result = (real.Get()->*getDesc)(desc);
        if (SUCCEEDED(result)) desc->VendorId = DiagnosticMaskedVendorId();
        return result;
    }

public:
    explicit Adapter(IDXGIAdapter4* native) : real(native) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
            riid == __uuidof(IDXGIAdapter) || riid == __uuidof(IDXGIAdapter1) ||
            riid == __uuidof(IDXGIAdapter2) || riid == __uuidof(IDXGIAdapter3) ||
            riid == __uuidof(IDXGIAdapter4)) {
            *out = static_cast<IDXGIAdapter4*>(this);
        } else if (riid == __uuidof(ID3D12On11AdapterBridge)) {
            *out = static_cast<ID3D12On11AdapterBridge*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --refs;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name, UINT size, const void* data) override {
        return real->SetPrivateData(name, size, data);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown* object) override {
        return real->SetPrivateDataInterface(name, object);
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT* size, void* data) override {
        return real->GetPrivateData(name, size, data);
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** parent) override {
        D3D12On11TraceCaller("Adapter::GetParent", _ReturnAddress());
        return real->GetParent(riid, parent);
    }
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT output, IDXGIOutput** out) override {
        D3D12On11TraceCaller("Adapter::EnumOutputs", _ReturnAddress());
        return real->EnumOutputs(output, out);
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* desc) override {
        return GetMaskedDesc(&IDXGIAdapter4::GetDesc, desc);
    }
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID name, LARGE_INTEGER* version) override {
        D3D12On11TraceCaller("Adapter::CheckInterfaceSupport", _ReturnAddress());
        return real->CheckInterfaceSupport(name, version);
    }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* desc) override {
        return GetMaskedDesc(&IDXGIAdapter4::GetDesc1, desc);
    }
    HRESULT STDMETHODCALLTYPE GetDesc2(DXGI_ADAPTER_DESC2* desc) override {
        return GetMaskedDesc(&IDXGIAdapter4::GetDesc2, desc);
    }
    HRESULT STDMETHODCALLTYPE RegisterHardwareContentProtectionTeardownStatusEvent(
        HANDLE event, DWORD* cookie) override {
        return real->RegisterHardwareContentProtectionTeardownStatusEvent(event, cookie);
    }
    void STDMETHODCALLTYPE UnregisterHardwareContentProtectionTeardownStatus(DWORD cookie) override {
        real->UnregisterHardwareContentProtectionTeardownStatus(cookie);
    }
    HRESULT STDMETHODCALLTYPE QueryVideoMemoryInfo(UINT node, DXGI_MEMORY_SEGMENT_GROUP group,
        DXGI_QUERY_VIDEO_MEMORY_INFO* info) override {
        D3D12On11TraceCaller("Adapter::QueryVideoMemoryInfo", _ReturnAddress());
        return real->QueryVideoMemoryInfo(node, group, info);
    }
    HRESULT STDMETHODCALLTYPE SetVideoMemoryReservation(UINT node, DXGI_MEMORY_SEGMENT_GROUP group,
        UINT64 reservation) override {
        return real->SetVideoMemoryReservation(node, group, reservation);
    }
    HRESULT STDMETHODCALLTYPE RegisterVideoMemoryBudgetChangeNotificationEvent(
        HANDLE event, DWORD* cookie) override {
        return real->RegisterVideoMemoryBudgetChangeNotificationEvent(event, cookie);
    }
    void STDMETHODCALLTYPE UnregisterVideoMemoryBudgetChangeNotification(DWORD cookie) override {
        real->UnregisterVideoMemoryBudgetChangeNotification(cookie);
    }
    HRESULT STDMETHODCALLTYPE GetDesc3(DXGI_ADAPTER_DESC3* desc) override {
        return GetMaskedDesc(&IDXGIAdapter4::GetDesc3, desc);
    }
    HRESULT STDMETHODCALLTYPE GetNativeAdapter(IDXGIAdapter** adapter) override {
        D3D12On11TraceCaller("Adapter::GetNativeAdapter", _ReturnAddress());
        if (!adapter) return E_POINTER;
        *adapter = nullptr;
        return real->QueryInterface(__uuidof(IDXGIAdapter),
            reinterpret_cast<void**>(adapter));
    }
};

HRESULT WrapAdapter(IUnknown* native, REFIID riid, void** out) {
    if (!native || !out) {
        if (out) *out = nullptr;
        if (native) native->Release();
        return E_POINTER;
    }
    *out = nullptr;
    Ptr<IDXGIAdapter4> adapter;
    const HRESULT queryResult = native->QueryInterface(__uuidof(IDXGIAdapter4),
        reinterpret_cast<void**>(adapter.Put()));
    native->Release();
    if (FAILED(queryResult)) return queryResult;

    DXGI_ADAPTER_DESC desc{};
    const HRESULT descResult = adapter->GetDesc(&desc);
    if (FAILED(descResult) || desc.VendorId != 0x10de)
        return adapter->QueryInterface(riid, out);

    char message[192] = {};
    snprintf(message, sizeof(message),
        "DIAGNOSTIC_MASK_NVIDIA: native-vendor=0x%04x device=0x%04x exposed-vendor=0x%04x luid=%08lx:%08lx",
        desc.VendorId, desc.DeviceId, DiagnosticMaskedVendorId(),
        static_cast<unsigned long>(desc.AdapterLuid.HighPart),
        static_cast<unsigned long>(desc.AdapterLuid.LowPart));
    D3D12On11LogMessage(message);
    auto* wrapper = new (std::nothrow) Adapter(adapter.Get());
    if (!wrapper) return E_OUTOFMEMORY;
    const HRESULT result = wrapper->QueryInterface(riid, out);
    wrapper->Release();
    return result;
}

class SwapChain final : public SwapChainFwd {
    std::atomic<ULONG> refs{1};
    IUnknown* source;
    Factory* parent;

public:
    SwapChain(IDXGISwapChain4* native, IUnknown* deviceSource, Factory* factory);
    ~SwapChain();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --refs;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** out) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID riid, void** out) override;
    HRESULT STDMETHODCALLTYPE Present(UINT interval, UINT flags) override;
    HRESULT STDMETHODCALLTYPE Present1(UINT interval, UINT flags,
        const DXGI_PRESENT_PARAMETERS* parameters) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT width, UINT height,
        DXGI_FORMAT format, UINT flags) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count, UINT width, UINT height,
        DXGI_FORMAT format, UINT flags, const UINT* nodeMask, IUnknown* const* presentQueue) override;

private:
    void ReleaseBackbuffers();
};

class Factory final : public FactoryFwd {
    std::atomic<ULONG> refs{1};

public:
    explicit Factory(IDXGIFactory7* native) { real = native; }
    ~Factory() { if (real) real->Release(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
            riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1) ||
            riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
            riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) ||
            riid == __uuidof(IDXGIFactory6) || riid == __uuidof(IDXGIFactory7)) {
            *out = static_cast<IDXGIFactory7*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --refs;
        if (!count) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT index, IDXGIAdapter** out) override {
        D3D12On11TraceCaller("Factory::EnumAdapters", _ReturnAddress());
        if (!DiagnosticMaskNvidiaAdapter()) return real->EnumAdapters(index, out);
        if (!out) return E_POINTER;
        *out = nullptr;
        IDXGIAdapter* native = nullptr;
        const HRESULT result = real->EnumAdapters(index, &native);
        return FAILED(result) ? result : WrapAdapter(native, __uuidof(IDXGIAdapter),
            reinterpret_cast<void**>(out));
    }

    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT index, IDXGIAdapter1** out) override {
        D3D12On11TraceCaller("Factory::EnumAdapters1", _ReturnAddress());
        if (!DiagnosticMaskNvidiaAdapter()) return real->EnumAdapters1(index, out);
        if (!out) return E_POINTER;
        *out = nullptr;
        IDXGIAdapter1* native = nullptr;
        const HRESULT result = real->EnumAdapters1(index, &native);
        return FAILED(result) ? result : WrapAdapter(native, __uuidof(IDXGIAdapter1),
            reinterpret_cast<void**>(out));
    }

    HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID luid, REFIID riid, void** out) override {
        D3D12On11TraceCaller("Factory::EnumAdapterByLuid", _ReturnAddress());
        if (!DiagnosticMaskNvidiaAdapter()) return real->EnumAdapterByLuid(luid, riid, out);
        if (!out) return E_POINTER;
        *out = nullptr;
        IDXGIAdapter4* native = nullptr;
        const HRESULT result = real->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter4),
            reinterpret_cast<void**>(&native));
        return FAILED(result) ? result : WrapAdapter(native, riid, out);
    }

    HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID riid, void** out) override {
        D3D12On11TraceCaller("Factory::EnumWarpAdapter", _ReturnAddress());
        if (!DiagnosticMaskNvidiaAdapter()) return real->EnumWarpAdapter(riid, out);
        if (!out) return E_POINTER;
        *out = nullptr;
        IDXGIAdapter4* native = nullptr;
        const HRESULT result = real->EnumWarpAdapter(__uuidof(IDXGIAdapter4),
            reinterpret_cast<void**>(&native));
        return FAILED(result) ? result : WrapAdapter(native, riid, out);
    }

    HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(UINT index, DXGI_GPU_PREFERENCE preference,
        REFIID riid, void** out) override {
        D3D12On11TraceCaller("Factory::EnumAdapterByGpuPreference", _ReturnAddress());
        if (!DiagnosticMaskNvidiaAdapter())
            return real->EnumAdapterByGpuPreference(index, preference, riid, out);
        if (!out) return E_POINTER;
        *out = nullptr;
        IDXGIAdapter4* native = nullptr;
        const HRESULT result = real->EnumAdapterByGpuPreference(index, preference,
            __uuidof(IDXGIAdapter4), reinterpret_cast<void**>(&native));
        return FAILED(result) ? result : WrapAdapter(native, riid, out);
    }

    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
        IDXGISwapChain** out) override {
        D3D12On11TraceApi("Factory::CreateSwapChain");
        if (!out) return E_POINTER;
        *out = nullptr;
        Ptr<ID3D11Device> nativeDevice;
        HRESULT result = D3D12On11GetD3D11Device(device, nativeDevice.Put());
        if (FAILED(result)) return result;
        IDXGISwapChain* native = nullptr;
        result = real->CreateSwapChain(nativeDevice.Get(), desc, &native);
        if (FAILED(result)) return result;
        result = WrapSwapChain(native, device, __uuidof(IDXGISwapChain),
            reinterpret_cast<void**>(out));
        if (SUCCEEDED(result)) D3D12On11TraceSwapChainCreated();
        return result;
    }

    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* device, HWND window,
        const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
        IDXGIOutput* restrictOutput, IDXGISwapChain1** out) override {
        D3D12On11TraceApi("Factory::CreateSwapChainForHwnd");
        if (!out) return E_POINTER;
        *out = nullptr;
        Ptr<ID3D11Device> nativeDevice;
        HRESULT result = D3D12On11GetD3D11Device(device, nativeDevice.Put());
        if (FAILED(result)) return result;
        IDXGISwapChain1* native = nullptr;
        result = real->CreateSwapChainForHwnd(nativeDevice.Get(), window, desc, fullscreen,
            restrictOutput, &native);
        if (FAILED(result)) return result;
        result = WrapSwapChain(native, device, __uuidof(IDXGISwapChain1),
            reinterpret_cast<void**>(out));
        if (SUCCEEDED(result)) D3D12On11TraceSwapChainCreated();
        return result;
    }

    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown* device, IUnknown* window,
        const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* restrictOutput,
        IDXGISwapChain1** out) override {
        D3D12On11TraceApi("Factory::CreateSwapChainForCoreWindow");
        if (!out) return E_POINTER;
        *out = nullptr;
        Ptr<ID3D11Device> nativeDevice;
        HRESULT result = D3D12On11GetD3D11Device(device, nativeDevice.Put());
        if (FAILED(result)) return result;
        IDXGISwapChain1* native = nullptr;
        result = real->CreateSwapChainForCoreWindow(nativeDevice.Get(), window, desc,
            restrictOutput, &native);
        if (FAILED(result)) return result;
        result = WrapSwapChain(native, device, __uuidof(IDXGISwapChain1),
            reinterpret_cast<void**>(out));
        if (SUCCEEDED(result)) D3D12On11TraceSwapChainCreated();
        return result;
    }

    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown* device,
        const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* restrictOutput,
        IDXGISwapChain1** out) override {
        D3D12On11TraceApi("Factory::CreateSwapChainForComposition");
        if (!out) return E_POINTER;
        *out = nullptr;
        Ptr<ID3D11Device> nativeDevice;
        HRESULT result = D3D12On11GetD3D11Device(device, nativeDevice.Put());
        if (FAILED(result)) return result;
        IDXGISwapChain1* native = nullptr;
        result = real->CreateSwapChainForComposition(nativeDevice.Get(), desc, restrictOutput,
            &native);
        if (FAILED(result)) return result;
        result = WrapSwapChain(native, device, __uuidof(IDXGISwapChain1),
            reinterpret_cast<void**>(out));
        if (SUCCEEDED(result)) D3D12On11TraceSwapChainCreated();
        return result;
    }

    HRESULT WrapSwapChain(IUnknown* native, IUnknown* device, REFIID riid, void** out) {
        Ptr<IDXGISwapChain4> chain;
        HRESULT result = native->QueryInterface(__uuidof(IDXGISwapChain4),
            reinterpret_cast<void**>(chain.Put()));
        native->Release();
        if (FAILED(result)) return result;
        auto* wrapper = new (std::nothrow) SwapChain(chain.Get(), device, this);
        if (!wrapper) return E_OUTOFMEMORY;
        result = wrapper->QueryInterface(riid, out);
        wrapper->Release();
        return result;
    }

    friend class SwapChain;
};

SwapChain::SwapChain(IDXGISwapChain4* native, IUnknown* deviceSource, Factory* factory)
    : source(deviceSource), parent(factory) {
    real = native;
    if (real) real->AddRef();
    if (source) source->AddRef();
    if (parent) parent->AddRef();
}

SwapChain::~SwapChain() {
    if (real) real->Release();
    if (source) source->Release();
    if (parent) parent->Release();
}

HRESULT STDMETHODCALLTYPE SwapChain::QueryInterface(REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGISwapChain) ||
        riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2) ||
        riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4)) {
        *out = static_cast<IDXGISwapChain4*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE SwapChain::GetParent(REFIID riid, void** out) {
    if (!out) return E_POINTER;
    return parent ? parent->QueryInterface(riid, out) : DXGI_ERROR_NOT_FOUND;
}

HRESULT STDMETHODCALLTYPE SwapChain::GetDevice(REFIID riid, void** out) {
    if (!out) return E_POINTER;
    return D3D12On11GetWrappedDevice(source, riid, out);
}

HRESULT STDMETHODCALLTYPE SwapChain::GetBuffer(UINT index, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    Ptr<ID3D11Texture2D> texture;
    HRESULT result = real->GetBuffer(index, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(texture.Put()));
    if (FAILED(result)) return result;
    return D3D12On11WrapBackbuffer(source, texture.Get(), riid, out);
}

HRESULT STDMETHODCALLTYPE SwapChain::Present(UINT interval, UINT flags) {
    D3D12On11TraceApi("IDXGISwapChain::Present");
    D3D12On11Flush(source);
    const HRESULT result = real->Present(interval, flags);
    D3D12On11TracePresent("IDXGISwapChain::Present", result);
    return result;
}

HRESULT STDMETHODCALLTYPE SwapChain::Present1(UINT interval, UINT flags,
    const DXGI_PRESENT_PARAMETERS* parameters) {
    D3D12On11TraceApi("IDXGISwapChain1::Present1");
    D3D12On11Flush(source);
    const HRESULT result = real->Present1(interval, flags, parameters);
    D3D12On11TracePresent("IDXGISwapChain1::Present1", result);
    return result;
}

void SwapChain::ReleaseBackbuffers() {
    D3D12On11ReleaseBackbufferViews(source);
}

HRESULT STDMETHODCALLTYPE SwapChain::ResizeBuffers(UINT count, UINT width, UINT height,
    DXGI_FORMAT format, UINT flags) {
    ReleaseBackbuffers();
    return real->ResizeBuffers(count, width, height, format, flags);
}

HRESULT STDMETHODCALLTYPE SwapChain::ResizeBuffers1(UINT count, UINT width, UINT height,
    DXGI_FORMAT format, UINT flags, const UINT*, IUnknown* const*) {
    ReleaseBackbuffers();
    return real->ResizeBuffers(count, width, height, format, flags);
}

HRESULT CreateWrappedFactory(UINT flags, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    Ptr<IDXGIFactory7> native;
    HRESULT result = GetSystemDxgi().Create(flags, __uuidof(IDXGIFactory7),
        reinterpret_cast<void**>(native.Put()));
    if (FAILED(result)) return result;
    auto* wrapper = new (std::nothrow) Factory(native.Get());
    if (!wrapper) return E_OUTOFMEMORY;
    native.Get()->AddRef();
    result = wrapper->QueryInterface(riid, out);
    wrapper->Release();
    return result;
}

}

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** out) {
    return CreateWrappedFactory(0, riid, out);
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** out) {
    return CreateWrappedFactory(0, riid, out);
}

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void** out) {
    return CreateWrappedFactory(flags, riid, out);
}

extern "C" HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
    using Fn = HRESULT(WINAPI*)();
    auto function = reinterpret_cast<Fn>(GetSystemDxgi().GetExport("DXGIDeclareAdapterRemovalSupport"));
    return function ? function() : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

extern "C" HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID riid, void** debug) {
    using Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    auto function = reinterpret_cast<Fn>(GetSystemDxgi().GetExport("DXGIGetDebugInterface1"));
    return function ? function(flags, riid, debug) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

#define DXGI_FORWARD_ARGS \
    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, \
    uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8, \
    uintptr_t a9, uintptr_t a10, uintptr_t a11, uintptr_t a12
#define DXGI_FORWARD_CALL \
    a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12
#define DEFINE_DXGI_FORWARDER(name) \
    extern "C" uintptr_t WINAPI DxgiForward_##name(DXGI_FORWARD_ARGS) { \
        using Fn = uintptr_t(WINAPI*)(DXGI_FORWARD_ARGS); \
        auto function = reinterpret_cast<Fn>(GetSystemDxgi().GetExport(#name)); \
        return function ? function(DXGI_FORWARD_CALL) : static_cast<uintptr_t>(HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)); \
    }

DEFINE_DXGI_FORWARDER(ApplyCompatResolutionQuirking)
DEFINE_DXGI_FORWARDER(CompatString)
DEFINE_DXGI_FORWARDER(CompatValue)
DEFINE_DXGI_FORWARDER(DXGID3D10CreateDevice)
DEFINE_DXGI_FORWARDER(DXGID3D10CreateLayeredDevice)
DEFINE_DXGI_FORWARDER(DXGID3D10GetLayeredDeviceSize)
DEFINE_DXGI_FORWARDER(DXGID3D10RegisterLayers)
DEFINE_DXGI_FORWARDER(DXGIDumpJournal)
DEFINE_DXGI_FORWARDER(DXGIReportAdapterConfiguration)
DEFINE_DXGI_FORWARDER(PIXBeginCapture)
DEFINE_DXGI_FORWARDER(PIXEndCapture)
DEFINE_DXGI_FORWARDER(PIXGetCaptureState)
DEFINE_DXGI_FORWARDER(SetAppCompatStringPointer)
DEFINE_DXGI_FORWARDER(UpdateHMDEmulationStatus)

#undef DEFINE_DXGI_FORWARDER
#undef DXGI_FORWARD_CALL
#undef DXGI_FORWARD_ARGS
