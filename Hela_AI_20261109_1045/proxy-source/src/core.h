// d3d12on11 - livello di traduzione D3D12 -> D3D11 (alpha).
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// ------------------------------------------------------------------ puntatore COM minimale
template <class T> class Ptr {
    T* p = nullptr;
public:
    Ptr() = default;
    Ptr(T* x) : p(x) { if (p) p->AddRef(); }
    Ptr(const Ptr& o) : p(o.p) { if (p) p->AddRef(); }
    Ptr(Ptr&& o) noexcept : p(o.p) { o.p = nullptr; }
    ~Ptr() { if (p) p->Release(); }
    Ptr& operator=(const Ptr& o) { if (o.p) o.p->AddRef(); if (p) p->Release(); p = o.p; return *this; }
    Ptr& operator=(Ptr&& o) noexcept { if (this != &o) { if (p) p->Release(); p = o.p; o.p = nullptr; } return *this; }
    T* Get() const { return p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    T** Put() { if (p) { p->Release(); p = nullptr; } return &p; }
    void Reset() { if (p) { p->Release(); p = nullptr; } }
    static Ptr Attach(T* x) { Ptr r; r.p = x; return r; }   // non fa AddRef
};

// ------------------------------------------------------------------ log / configurazione
struct Config {
    std::string baseDir;        // cartella della DLL
    std::string overridesDir;   // shader SM5 sostitutivi (<hash>.cso)
    std::string dumpDir;        // shader DXIL scaricati dal wrapper
    std::string bindingMapPath; // register-space mapping condivisa col convertitore
    std::map<std::string, UINT> bindingMap;
    bool log = true;
    bool debugLayer = false;
    bool diagnosticCapabilities = false;
    bool traceCallers = false;
    bool diagnosticRenderTrace = false;
    bool crashDiagnostics = false;
    UINT shaderModel = 0x51;    // D3D_SHADER_MODEL riportato al gioco
    bool dumpShaders = true;
};
Config& Cfg();
void LoadConfig(HMODULE self);
void Log(const char* fmt, ...);
bool Sha256Hex(const void* data, size_t size, std::string& result);
void LogModuleFingerprint(const char* role, HMODULE module);
void Unimpl(const char* name);   // registra una sola volta ogni metodo non implementato

struct RenderTraceStats {
    std::atomic<uint64_t> startTickMs{0};
    std::atomic<const char*> lastApiName{"init"};
    std::atomic<uint64_t> lastApiTickMs{0};
    std::atomic<uint32_t> lastApiTid{0};
    std::atomic<uint64_t> execCmdLists{0};
    std::atomic<uint64_t> signalCount{0};
    std::atomic<uint64_t> waitCount{0};
    std::atomic<uint64_t> waitCtxCalls{0};
    std::atomic<uint64_t> waitCtxFail{0};
    std::atomic<uint64_t> waitCtxMaxSpins{0};
    std::atomic<uint64_t> swapChainCreated{0};
    std::atomic<uint64_t> presentCount{0};
    std::atomic<uint64_t> psoGraphics{0};
    std::atomic<uint64_t> psoCompute{0};
    std::atomic<uint64_t> lastPsoHash{0};
    std::atomic<uint64_t> drawExec{0};
    std::atomic<uint64_t> drawWithRtv{0};
    std::atomic<uint64_t> drawWithBackbuffer{0};
    std::atomic<uint64_t> drawNullRtv{0};
    std::atomic<uint64_t> drawSkipNoPso{0};
    std::atomic<uint64_t> drawSkipPlaceholderVs{0};
    std::atomic<uint64_t> drawSkipPlaceholderOther{0};
    std::atomic<uint64_t> drawSkipNoVb{0};
    std::atomic<uint64_t> drawSkipNoIb{0};
    std::atomic<uint64_t> drawSkipMapFail{0};
    std::atomic<uint64_t> drawMissingVb{0};
    std::atomic<uint64_t> drawMissingIb{0};
    std::atomic<uint64_t> dispatchExec{0};
    std::atomic<uint64_t> dispatchSkipNoPso{0};
    std::atomic<uint64_t> dispatchSkipPlaceholderCs{0};
    std::atomic<uint64_t> placedOk{0};
    std::atomic<uint64_t> placedOverlap{0};
    std::atomic<uint64_t> placedReleased{0};
    std::atomic<int64_t> placedLive{0};
};
RenderTraceStats& TraceStats();
void TraceApi(const char* apiName);

#include "diag_trace.h"

struct Device;
void DeviceAddRef(Device*);
void DeviceRelease(Device*);
HRESULT DeviceQuery(Device*, REFIID, void**);

// ------------------------------------------------------------------ oggetto COM base
struct GuidLess {
    bool operator()(const GUID& a, const GUID& b) const { return memcmp(&a, &b, sizeof(GUID)) < 0; }
};

template <class I> struct Obj : I {
    std::atomic<ULONG> refs{1};
    Device* dev;
    std::mutex pmu;
    std::map<GUID, std::vector<uint8_t>, GuidLess> pdata;
    std::map<GUID, Ptr<IUnknown>, GuidLess> pif;

    explicit Obj(Device* d) : dev(d) { if (d && (void*)d != (void*)this) DeviceAddRef(d); }
    virtual ~Obj() { if (dev && (void*)dev != (void*)this) DeviceRelease(dev); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        bool ok = riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(I);
        if (std::is_base_of<ID3D12DeviceChild, I>::value && riid == __uuidof(ID3D12DeviceChild)) ok = true;
        if (std::is_base_of<ID3D12Pageable, I>::value && riid == __uuidof(ID3D12Pageable)) ok = true;
        if (std::is_base_of<ID3D12CommandList, I>::value && riid == __uuidof(ID3D12CommandList)) ok = true;
        if (ok) { *ppv = static_cast<I*>(this); AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* size, void* data) override {
        D3D12ON11_TRACE_CALLER("GetPrivateData");
        const UINT requestedSize = size ? *size : 0;
        HRESULT result = E_INVALIDARG;
        {
            std::lock_guard<std::mutex> l(pmu);
            if (size) {
                auto ii = pif.find(guid);
                if (ii != pif.end()) {
                    if (data && *size >= sizeof(void*)) {
                        ii->second->AddRef();
                        *(IUnknown**)data = ii->second.Get();
                    } else if (data) {
                        *size = sizeof(void*);
                        result = DXGI_ERROR_MORE_DATA;
                    }
                    if (result != DXGI_ERROR_MORE_DATA) {
                        *size = sizeof(void*);
                        result = S_OK;
                    }
                } else {
                    auto it = pdata.find(guid);
                    if (it == pdata.end()) {
                        *size = 0;
                        result = DXGI_ERROR_NOT_FOUND;
                    } else {
                        const UINT have = static_cast<UINT>(it->second.size());
                        if (data && *size < have) {
                            *size = have;
                            result = DXGI_ERROR_MORE_DATA;
                        } else {
                            if (data) memcpy(data, it->second.data(), have);
                            *size = have;
                            result = S_OK;
                        }
                    }
                }
            }
        }
        if (Cfg().traceCallers) {
            Log("GetPrivateData end guid=%08lx-%04x-%04x requested=%u returned=%u has-data=%u result=0x%08lx",
                guid.Data1, guid.Data2, guid.Data3, requestedSize, size ? *size : 0,
                data != nullptr, result);
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void* data) override {
        D3D12ON11_TRACE_CALLER("SetPrivateData");
        {
            std::lock_guard<std::mutex> l(pmu);
            pif.erase(guid);
            if (!data) pdata.erase(guid);
            else pdata[guid].assign((const uint8_t*)data, (const uint8_t*)data + size);
        }
        if (Cfg().traceCallers) {
            Log("SetPrivateData end guid=%08lx-%04x-%04x size=%u has-data=%u result=0x%08lx",
                guid.Data1, guid.Data2, guid.Data3, size, data != nullptr, S_OK);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* data) override {
        D3D12ON11_TRACE_CALLER("SetPrivateDataInterface");
        {
            std::lock_guard<std::mutex> l(pmu);
            pdata.erase(guid);
            if (!data) pif.erase(guid);
            else pif[guid] = Ptr<IUnknown>(const_cast<IUnknown*>(data));
        }
        if (Cfg().traceCallers) {
            Log("SetPrivateDataInterface end guid=%08lx-%04x-%04x has-interface=%u result=0x%08lx",
                guid.Data1, guid.Data2, guid.Data3, data != nullptr, S_OK);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetName(const WCHAR*) override { return S_OK; }
    // ID3D12DeviceChild::GetDevice (implicito override quando I deriva da ID3D12DeviceChild)
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppv) { return DeviceQuery(dev, riid, ppv); }
};

#include "d3d12_stubs.inc"

// ------------------------------------------------------------------ utilita'
struct FormatInfo { UINT bits; UINT blockW; UINT blockH; };   // bits per pixel, oppure per blocco se blockW>1
FormatInfo GetFormatInfo(DXGI_FORMAT f);
UINT64 AlignUp(UINT64 v, UINT64 a);
uint64_t Fnv1a64(const void* data, size_t len);
std::string Hex64(uint64_t v);
UINT MapRegister(char kind, UINT space, UINT reg);

struct DxbcInfo { bool isDxbc = false, hasDxil = false, hasLegacy = false, hasRts0 = false; UINT versionMajor = 0, versionMinor = 0; };
DxbcInfo InspectContainer(const void* data, size_t len);
bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out);
bool WriteFileBytes(const std::string& path, const void* data, size_t len);
void MakeDirs(const std::string& path);
