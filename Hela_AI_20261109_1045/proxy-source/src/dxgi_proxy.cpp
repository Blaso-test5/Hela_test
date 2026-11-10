#define CreateDXGIFactory CreateDXGIFactorySystemDeclaration
#define CreateDXGIFactory1 CreateDXGIFactory1SystemDeclaration
#define CreateDXGIFactory2 CreateDXGIFactory2SystemDeclaration
#include "core.h"
#undef CreateDXGIFactory
#undef CreateDXGIFactory1
#undef CreateDXGIFactory2

#include "types.h"
#include "bridge.h"
#include "dxgi_factory_fwd.inc"
#include "dxgi_swapchain_fwd.inc"

namespace {

void LogDxgi(const char* format, ...) {
    char message[512]{};
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (written < 0) return;
    message[sizeof(message) - 1] = '\0';
    D3D12On11LogMessage(message);
}

bool DiagnosticMaskNvidiaAdapter() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"D3D12ON11_DIAGNOSTIC_MASK_NVIDIA",
        value, ARRAYSIZE(value)) == 1 && value[0] == L'1';
}

bool BackbufferReadbackEnabled() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"D3D12ON11_BACKBUFFER_READBACK",
        value, ARRAYSIZE(value)) == 1 && value[0] == L'1';
}

bool BackbufferInjectionEnabled() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"D3D12ON11_INJECTION_TEST",
        value, ARRAYSIZE(value)) == 1 && value[0] == L'1';
}

bool FullReadbackEnabled() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"D3D12ON11_FULL_READBACK",
        value, ARRAYSIZE(value)) == 1 && value[0] == L'1';
}

struct ReadbackPixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

bool ReadbackPixelAt(const D3D11_MAPPED_SUBRESOURCE& mapped,
    const D3D11_TEXTURE2D_DESC& desc, UINT x, UINT y, ReadbackPixel& pixel) {
    if (x >= desc.Width || y >= desc.Height || !mapped.pData) return false;
    const auto* row = static_cast<const uint8_t*>(mapped.pData) +
        static_cast<size_t>(y) * mapped.RowPitch;
    if (desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
        uint32_t packed = 0;
        std::memcpy(&packed, row + static_cast<size_t>(x) * sizeof(packed), sizeof(packed));
        pixel.r = static_cast<uint8_t>((((packed & 0x3ffu) * 255u) + 511u) / 1023u);
        pixel.g = static_cast<uint8_t>(((((packed >> 10) & 0x3ffu) * 255u) + 511u) / 1023u);
        pixel.b = static_cast<uint8_t>(((((packed >> 20) & 0x3ffu) * 255u) + 511u) / 1023u);
        pixel.a = static_cast<uint8_t>(((packed >> 30) & 0x3u) * 85u);
        return true;
    }

    const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
        desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
        desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if (!bgra && !rgba) return false;

    const uint8_t* value = row + static_cast<size_t>(x) * 4;
    pixel.r = value[bgra ? 2 : 0];
    pixel.g = value[1];
    pixel.b = value[bgra ? 0 : 2];
    pixel.a = value[3];
    return true;
}

bool SaveMappedTextureAsBmp(const D3D11_MAPPED_SUBRESOURCE& mapped,
    const D3D11_TEXTURE2D_DESC& desc, const wchar_t* filename,
    uint64_t presentCount, const char* label) {
    if (!filename || !desc.Width || !desc.Height ||
        desc.Width > LONG_MAX || desc.Height > LONG_MAX) {
        LogDxgi("[READBACK_FULL] Present #%llu %s invalid BMP dimensions=%ux%u",
            static_cast<unsigned long long>(presentCount), label, desc.Width, desc.Height);
        return false;
    }

    const uint64_t rowBytes = static_cast<uint64_t>(desc.Width) * 3;
    const uint64_t paddedRowBytes = (rowBytes + 3u) & ~uint64_t(3u);
    constexpr uint64_t headerBytes = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    if (paddedRowBytes > SIZE_MAX || paddedRowBytes > MAXDWORD - headerBytes ||
        desc.Height > (MAXDWORD - headerBytes) / paddedRowBytes) {
        LogDxgi("[READBACK_FULL] Present #%llu %s BMP dimensions exceed file limits=%ux%u",
            static_cast<unsigned long long>(presentCount), label, desc.Width, desc.Height);
        return false;
    }
    const uint64_t imageBytes = paddedRowBytes * desc.Height;

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfSize = static_cast<DWORD>(headerBytes + imageBytes);
    fileHeader.bfOffBits = static_cast<DWORD>(headerBytes);
    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = static_cast<LONG>(desc.Width);
    infoHeader.biHeight = -static_cast<LONG>(desc.Height);
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = static_cast<DWORD>(imageBytes);

    FILE* file = nullptr;
    const errno_t openResult = _wfopen_s(&file, filename, L"wb");
    if (openResult != 0 || !file) {
        LogDxgi("[READBACK_FULL] Present #%llu %s could not open %ls errno=%d",
            static_cast<unsigned long long>(presentCount), label, filename, openResult);
        return false;
    }

    bool succeeded = fwrite(&fileHeader, sizeof(fileHeader), 1, file) == 1 &&
        fwrite(&infoHeader, sizeof(infoHeader), 1, file) == 1;
    std::vector<uint8_t> row(static_cast<size_t>(paddedRowBytes));
    for (UINT y = 0; succeeded && y < desc.Height; ++y) {
        for (UINT x = 0; x < desc.Width; ++x) {
            ReadbackPixel pixel{};
            if (!ReadbackPixelAt(mapped, desc, x, y, pixel)) {
                succeeded = false;
                break;
            }
            const size_t offset = static_cast<size_t>(x) * 3;
            row[offset] = pixel.b;
            row[offset + 1] = pixel.g;
            row[offset + 2] = pixel.r;
        }
        if (succeeded)
            succeeded = fwrite(row.data(), 1, row.size(), file) == row.size();
    }
    if (fclose(file) != 0) succeeded = false;
    if (!succeeded) {
        LogDxgi("[READBACK_FULL] Present #%llu %s BMP write failed path=%ls",
            static_cast<unsigned long long>(presentCount), label, filename);
        return false;
    }

    LogDxgi("[READBACK_FULL] Present #%llu saved %s path=%ls size=%ux%u format=%u bytes=%llu",
        static_cast<unsigned long long>(presentCount), label, filename,
        desc.Width, desc.Height, desc.Format,
        static_cast<unsigned long long>(headerBytes + imageBytes));
    return true;
}

HRESULT ReadbackTextureSamples(Device* device, ID3D11Texture2D* texture,
    const char* label, uint64_t presentCount, UINT bufferIndex,
    const wchar_t* fullReadbackFilename = nullptr) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (!desc.Width || !desc.Height || desc.SampleDesc.Count != 1) {
        LogDxgi("[%s] Present #%llu unsupported texture size=%ux%u samples=%u",
            label, static_cast<unsigned long long>(presentCount), desc.Width,
            desc.Height, desc.SampleDesc.Count);
        return E_NOTIMPL;
    }

    Ptr<ID3D11Texture2D> staging;
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    HRESULT result = device->d11->CreateTexture2D(&stagingDesc, nullptr, staging.Put());
    if (FAILED(result) || !staging) {
        LogDxgi("[%s] Present #%llu staging texture creation failed hr=0x%08lx format=%u",
            label, static_cast<unsigned long long>(presentCount), result, desc.Format);
        return FAILED(result) ? result : E_FAIL;
    }

    device->ctx->CopyResource(staging.Get(), texture);
    device->ctx->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = device->ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        LogDxgi("[%s] Present #%llu Map failed hr=0x%08lx format=%u",
            label, static_cast<unsigned long long>(presentCount), result, desc.Format);
        return result;
    }

    ReadbackPixel pixel{};
    if (!ReadbackPixelAt(mapped, desc, 0, 0, pixel)) {
        device->ctx->Unmap(staging.Get(), 0);
        LogDxgi("[%s] Present #%llu unsupported pixel format=%u",
            label, static_cast<unsigned long long>(presentCount), desc.Format);
        return E_NOTIMPL;
    }

    const UINT sampleXs[] = { 0, desc.Width - 1, desc.Width / 2, 0, desc.Width - 1 };
    const UINT sampleYs[] = { 0, 0, desc.Height / 2, desc.Height - 1, desc.Height - 1 };
    const char* sampleNames[] = {
        "Top-left", "Top-right", "Center", "Bottom-left", "Bottom-right"
    };
    LogDxgi("[%s] Present #%llu size=%ux%u format=%u buffer=%u",
        label, static_cast<unsigned long long>(presentCount), desc.Width,
        desc.Height, desc.Format, bufferIndex);
    for (size_t i = 0; i < ARRAYSIZE(sampleXs); ++i) {
        if (ReadbackPixelAt(mapped, desc, sampleXs[i], sampleYs[i], pixel)) {
            LogDxgi("[%s]   %s: R=%u G=%u B=%u A=%u",
                label, sampleNames[i], pixel.r, pixel.g, pixel.b, pixel.a);
        }
    }

    UINT nonBlackCount = 0;
    constexpr UINT sampleCount = 100;
    for (UINT i = 0; i < sampleCount; ++i) {
        const UINT x = static_cast<UINT>((static_cast<uint64_t>(i) * 7) % desc.Width);
        const UINT y = static_cast<UINT>((static_cast<uint64_t>(i) * 13) % desc.Height);
        if (ReadbackPixelAt(mapped, desc, x, y, pixel) &&
            (pixel.r > 5 || pixel.g > 5 || pixel.b > 5)) {
            ++nonBlackCount;
        }
    }
    LogDxgi("[%s]   Non-black pixels: %u/%u (%.1f%%)",
        label, nonBlackCount, sampleCount, 100.0f * nonBlackCount / sampleCount);
    if (fullReadbackFilename)
        SaveMappedTextureAsBmp(mapped, desc, fullReadbackFilename, presentCount, label);
    device->ctx->Unmap(staging.Get(), 0);
    return S_OK;
}

void CaptureBackbufferReadback(IDXGISwapChain4* swapChain, IUnknown* source) {
    static std::atomic<uint64_t> presentCount{0};
    static const bool enabled = BackbufferReadbackEnabled();
    static const bool fullReadbackEnabled = FullReadbackEnabled();
    static std::atomic<bool> fullReadbackAttempted{false};
    Device* device = nullptr;
    if (auto* queue = dynamic_cast<CommandQueue*>(source)) {
        device = queue->dev;
    } else {
        device = dynamic_cast<Device*>(source);
    }
    if (!device || !device->ctx) {
        D3D12On11Flush(source);
        if (enabled)
            LogDxgi("[READBACK] Skipped: wrapped D3D11 device/context unavailable");
        return;
    }

    std::lock_guard<std::recursive_mutex> contextLock(device->ctxLock);
    device->ctx->Flush();
    const uint64_t count = presentCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count == 100)
        D3D12On11DumpBackbufferSourceHistory();
    if (!enabled || count % 100 != 0) return;
    if (!device->d11) {
        LogDxgi("[READBACK] Present #%llu skipped: wrapped D3D11 device unavailable",
            static_cast<unsigned long long>(count));
        return;
    }

    const bool saveFullReadback = fullReadbackEnabled && count == 100 &&
        !fullReadbackAttempted.exchange(true, std::memory_order_relaxed);
    wchar_t sourceFilename[MAX_PATH]{};
    wchar_t backbufferFilename[MAX_PATH]{};
    if (saveFullReadback) {
        swprintf_s(sourceFilename, ARRAYSIZE(sourceFilename),
            L"D:\\HelaReadback_Present%llu_source.bmp",
            static_cast<unsigned long long>(count));
        swprintf_s(backbufferFilename, ARRAYSIZE(backbufferFilename),
            L"D:\\HelaReadback_Present%llu_backbuffer.bmp",
            static_cast<unsigned long long>(count));
    }

    const UINT bufferIndex = swapChain->GetCurrentBackBufferIndex();
    Ptr<ID3D11Texture2D> backbuffer;
    HRESULT result = swapChain->GetBuffer(bufferIndex, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(backbuffer.Put()));
    if (FAILED(result) || !backbuffer) {
        LogDxgi("[READBACK] Present #%llu GetBuffer(%u) failed hr=0x%08lx",
            static_cast<unsigned long long>(count), bufferIndex, result);
        return;
    }

    Ptr<ID3D11Resource> sourceResource;
    D3D12_RESOURCE_DESC sourceDesc{};
    uint64_t sourceSequence = 0;
    {
        std::lock_guard<std::mutex> sourceLock(device->backbufferCopyLock);
        sourceResource = device->lastBackbufferCopySource;
        sourceDesc = device->lastBackbufferCopySourceDesc;
        sourceSequence = device->lastBackbufferCopySequence;
    }
    if (!sourceSequence) {
        LogDxgi("[READBACK_SRC] Present #%llu no CopyResource-to-backbuffer source recorded",
            static_cast<unsigned long long>(count));
    } else if (!sourceResource) {
        LogDxgi("[READBACK_SRC] Present #%llu last source is not a texture "
            "dimension=%u width=%llu height=%u format=%u sequence=%llu",
            static_cast<unsigned long long>(count), sourceDesc.Dimension,
            static_cast<unsigned long long>(sourceDesc.Width), sourceDesc.Height,
            sourceDesc.Format, static_cast<unsigned long long>(sourceSequence));
    } else {
        Ptr<ID3D11Texture2D> sourceTexture;
        result = sourceResource->QueryInterface(__uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(sourceTexture.Put()));
        if (FAILED(result) || !sourceTexture) {
            LogDxgi("[READBACK_SRC] Present #%llu source QueryInterface failed "
                "hr=0x%08lx sequence=%llu",
                static_cast<unsigned long long>(count), result,
                static_cast<unsigned long long>(sourceSequence));
        } else {
            D3D11_TEXTURE2D_DESC sourceTextureDesc{};
            sourceTexture->GetDesc(&sourceTextureDesc);
            LogDxgi("[READBACK_SRC] Present #%llu CopyResource source=%p "
                "dimension=%u width=%llu height=%u format=%u sequence=%llu",
                static_cast<unsigned long long>(count), sourceTexture.Get(),
                sourceDesc.Dimension, static_cast<unsigned long long>(sourceDesc.Width),
                sourceDesc.Height, sourceDesc.Format,
                static_cast<unsigned long long>(sourceSequence));
            if (sourceTextureDesc.Width != sourceDesc.Width ||
                sourceTextureDesc.Height != sourceDesc.Height ||
                sourceTextureDesc.Format != sourceDesc.Format) {
                LogDxgi("[READBACK_SRC] Native texture desc differs: "
                    "width=%u height=%u format=%u",
                    sourceTextureDesc.Width, sourceTextureDesc.Height,
                    sourceTextureDesc.Format);
            }
            ReadbackTextureSamples(device, sourceTexture.Get(), "READBACK_SRC",
                count, bufferIndex, saveFullReadback ? sourceFilename : nullptr);
        }
    }

    ReadbackTextureSamples(device, backbuffer.Get(), "READBACK", count, bufferIndex,
        saveFullReadback ? backbufferFilename : nullptr);
}

void InjectRedBackbuffer(IDXGISwapChain4* swapChain, IUnknown* source) {
    static const bool enabled = [] {
        const bool requested = BackbufferInjectionEnabled();
        if (requested) LogDxgi("[INJECTION_TEST] Enabled");
        return requested;
    }();
    if (!enabled) return;

    static std::atomic<bool> resultLogged{false};
    Device* device = nullptr;
    if (auto* queue = dynamic_cast<CommandQueue*>(source)) {
        device = queue->dev;
    } else {
        device = dynamic_cast<Device*>(source);
    }
    if (!device || !device->d11 || !device->ctx) {
        if (!resultLogged.exchange(true, std::memory_order_relaxed))
            LogDxgi("[INJECTION_TEST] Failed: wrapped D3D11 device/context unavailable");
        return;
    }

    std::lock_guard<std::recursive_mutex> contextLock(device->ctxLock);
    const UINT bufferIndex = swapChain->GetCurrentBackBufferIndex();
    Ptr<ID3D11Texture2D> backbuffer;
    HRESULT result = swapChain->GetBuffer(bufferIndex, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(backbuffer.Put()));
    if (FAILED(result) || !backbuffer) {
        if (!resultLogged.exchange(true, std::memory_order_relaxed))
            LogDxgi("[INJECTION_TEST] GetBuffer(%u) failed hr=0x%08lx",
                bufferIndex, result);
        return;
    }

    Ptr<ID3D11RenderTargetView> rtv;
    result = device->d11->CreateRenderTargetView(backbuffer.Get(), nullptr, rtv.Put());
    if (FAILED(result) || !rtv) {
        if (!resultLogged.exchange(true, std::memory_order_relaxed))
            LogDxgi("[INJECTION_TEST] CreateRenderTargetView failed "
                "hr=0x%08lx format/buffer=%u", result, bufferIndex);
        return;
    }

    constexpr float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    device->ctx->ClearRenderTargetView(rtv.Get(), red);
    device->ctx->Flush();
    if (!resultLogged.exchange(true, std::memory_order_relaxed))
        LogDxgi("[INJECTION_TEST] Backbuffer %u cleared to RED", bufferIndex);
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
    LogDxgi("[SWAPCHAIN] GetBuffer called index=%u riid=%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        index, riid.Data1, static_cast<unsigned int>(riid.Data2),
        static_cast<unsigned int>(riid.Data3),
        static_cast<unsigned int>(riid.Data4[0]), static_cast<unsigned int>(riid.Data4[1]),
        static_cast<unsigned int>(riid.Data4[2]), static_cast<unsigned int>(riid.Data4[3]),
        static_cast<unsigned int>(riid.Data4[4]), static_cast<unsigned int>(riid.Data4[5]),
        static_cast<unsigned int>(riid.Data4[6]), static_cast<unsigned int>(riid.Data4[7]));
    if (!out) {
        LogDxgi("[SWAPCHAIN] GetBuffer rejected null output pointer");
        return E_POINTER;
    }
    *out = nullptr;
    Ptr<ID3D11Texture2D> texture;
    HRESULT result = real->GetBuffer(index, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(texture.Put()));
    if (FAILED(result)) {
        LogDxgi("[SWAPCHAIN] GetBuffer failed hr=0x%08lx", result);
        return result;
    }
    LogDxgi("[SWAPCHAIN] GetBuffer acquired texture=%p", texture.Get());
    result = D3D12On11WrapBackbuffer(source, texture.Get(), riid, out);
    LogDxgi("[SWAPCHAIN] GetBuffer wrapping completed hr=0x%08lx resource=%p",
        result, *out);
    return result;
}

HRESULT STDMETHODCALLTYPE SwapChain::Present(UINT interval, UINT flags) {
    D3D12On11TraceApi("IDXGISwapChain::Present");
    InjectRedBackbuffer(real, source);
    CaptureBackbufferReadback(real, source);
    const HRESULT result = real->Present(interval, flags);
    D3D12On11TracePresent("IDXGISwapChain::Present", result);
    return result;
}

HRESULT STDMETHODCALLTYPE SwapChain::Present1(UINT interval, UINT flags,
    const DXGI_PRESENT_PARAMETERS* parameters) {
    D3D12On11TraceApi("IDXGISwapChain1::Present1");
    InjectRedBackbuffer(real, source);
    CaptureBackbufferReadback(real, source);
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
