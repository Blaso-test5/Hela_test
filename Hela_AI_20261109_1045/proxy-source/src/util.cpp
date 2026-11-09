#include "types.h"
#include <sys/stat.h>
#include <direct.h>
#include <system_error>
#include <thread>

// ------------------------------------------------------------------ configurazione e log
static Config g_cfg;
Config& Cfg() { return g_cfg; }

static std::mutex g_logMu;
static FILE* g_logFile = nullptr;
static std::string g_logPath;
static bool g_logOpenFailureReported = false;

static std::string EnvOr(const char* name, const std::string& def) {
    char buf[1024];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    return (n > 0 && n < sizeof(buf)) ? std::string(buf) : def;
}

static std::string IniStr(const std::string& ini, const char* key, const char* def) {
    char buf[1024];
    GetPrivateProfileStringA("d3d12on11", key, def, buf, sizeof(buf), ini.c_str());
    return buf;
}

void LoadConfig(HMODULE self) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string p = path;
    size_t slash = p.find_last_of("\\/");
    g_cfg.baseDir = slash == std::string::npos ? "." : p.substr(0, slash);
    std::string ini = g_cfg.baseDir + "\\d3d12on11.ini";
    if (GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const std::string patchIni = g_cfg.baseDir + "\\D3D12On11Patch\\d3d12on11.ini";
        if (GetFileAttributesA(patchIni.c_str()) != INVALID_FILE_ATTRIBUTES)
            ini = patchIni;
    }
    auto rel = [&](const std::string& v) { return (v.size() > 1 && v[1] == ':') ? v : g_cfg.baseDir + "\\" + v; };

    g_cfg.log = EnvOr("D3D12ON11_LOG", IniStr(ini, "log", "1")) != "0";
    g_cfg.debugLayer = EnvOr("D3D12ON11_DEBUG", IniStr(ini, "debug", "0")) == "1";
    g_cfg.diagnosticCapabilities = EnvOr("D3D12ON11_DIAGNOSTIC_CAPABILITIES",
        IniStr(ini, "diagnostic_capabilities", "0")) == "1";
    g_cfg.traceCallers = EnvOr("D3D12ON11_TRACE_CALLERS",
        IniStr(ini, "trace_callers", "0")) == "1";
    g_cfg.diagnosticRenderTrace = EnvOr("D3D12ON11_DIAGNOSTIC_RENDER_TRACE",
        IniStr(ini, "diagnostic_render_trace", "0")) == "1";
    g_cfg.dumpShaders = EnvOr("D3D12ON11_DUMP_SHADERS", IniStr(ini, "dump_shaders", "1")) != "0";
    g_cfg.overridesDir = rel(EnvOr("D3D12ON11_OVERRIDES", IniStr(ini, "overrides", "d3d12on11\\overrides")));
    g_cfg.dumpDir = rel(EnvOr("D3D12ON11_DUMP", IniStr(ini, "dump", "d3d12on11\\dump")));
    const std::string bindingMap = EnvOr("D3D12ON11_BINDING_MAP", IniStr(ini, "binding_map", ""));
    if (!bindingMap.empty()) {
        g_cfg.bindingMapPath = rel(bindingMap);
        std::vector<uint8_t> bytes;
        if (ReadFileBytes(g_cfg.bindingMapPath, bytes)) {
            const std::string contents(bytes.begin(), bytes.end());
            const std::regex entry("\"([btsu]):([0-9]+):([0-9]+)\"\\s*:\\s*([0-9]+)");
            for (std::sregex_iterator it(contents.begin(), contents.end(), entry), end; it != end; ++it) {
                const auto& match = *it;
                g_cfg.bindingMap[match[1].str() + ":" + match[2].str() + ":" + match[3].str()] =
                    static_cast<UINT>(std::stoul(match[4].str()));
            }
            Log("Loaded %zu register binding overrides from %s", g_cfg.bindingMap.size(), g_cfg.bindingMapPath.c_str());
        } else {
            Log("Configured binding map could not be read: %s", g_cfg.bindingMapPath.c_str());
        }
    }
    std::string sm = EnvOr("D3D12ON11_SHADER_MODEL", IniStr(ini, "shader_model", "5_1"));
    g_cfg.shaderModel = sm == "5_0" ? 0x50 : sm == "6_0" ? 0x60 : sm == "6_1" ? 0x61 : sm == "6_2" ? 0x62
                       : sm == "6_3" ? 0x63 : sm == "6_4" ? 0x64 : sm == "6_5" ? 0x65 : sm == "6_6" ? 0x66
                       : sm == "6_7" ? 0x67 : 0x51;
    Log("=== D3D12On11 diagnostic session ===");
    Log("Config ini=%s log=%u debug-layer=%u advertised-shader-model=%u_%u",
        ini.c_str(), g_cfg.log, g_cfg.debugLayer, g_cfg.shaderModel >> 4, g_cfg.shaderModel & 0xf);
    Log("Config diagnostic-capabilities=%u%s", g_cfg.diagnosticCapabilities,
        g_cfg.diagnosticCapabilities ? " (UNSAFE: reports selected capabilities not fully implemented by D3D11 proxy)" : "");
    Log("Config caller-trace=%u", g_cfg.traceCallers);
    Log("Config diagnostic-render-trace=%u", g_cfg.diagnosticRenderTrace);
    Log("Config shader-dump=%u dump-dir=%s overrides-dir=%s binding-map=%s entries=%zu",
        g_cfg.dumpShaders, g_cfg.dumpDir.c_str(), g_cfg.overridesDir.c_str(),
        g_cfg.bindingMapPath.empty() ? "(none)" : g_cfg.bindingMapPath.c_str(), g_cfg.bindingMap.size());
    Log("Note: DXIL is not compiled by the proxy; a matching SM5 .cso override is required.");

    auto& stats = TraceStats();
    const uint64_t now = GetTickCount64();
    stats.startTickMs.store(now, std::memory_order_relaxed);
    stats.lastApiTickMs.store(now, std::memory_order_relaxed);
    if (g_cfg.diagnosticRenderTrace) {
        try {
            std::thread([] {
                for (;;) {
                    Sleep(2000);
                    auto& s = TraceStats();
                    const uint64_t tick = GetTickCount64();
                    const uint64_t start = s.startTickMs.load(std::memory_order_relaxed);
                    const uint64_t lastTick = s.lastApiTickMs.load(std::memory_order_relaxed);
                    const char* lastApi = s.lastApiName.load(std::memory_order_relaxed);
                    Log("[RENDER_TRACE] [HEARTBEAT] tid=%lu up=%llums last-api=%s(tid=%lu,ago=%llums) "
                        "exec=%llu sig=%llu wait=%llu wctx=%llu(fail=%llu,max-spin=%llu) "
                        "swap=%llu present=%llu pso(G=%llu,C=%llu,last=%016llx) "
                        "draw(ok=%llu,rtv=%llu,bb=%llu,nullrtv=%llu,skip-pso=%llu,skip-vs=%llu,"
                        "skip-other=%llu,skip-vb=%llu,skip-ib=%llu,skip-map=%llu,missing-vb=%llu,missing-ib=%llu) "
                        "disp(ok=%llu,skip-pso=%llu,skip-cs=%llu) placed(ok=%llu,ovl=%llu,rel=%llu,live=%lld)",
                        GetCurrentThreadId(),
                        static_cast<unsigned long long>(tick - start),
                        lastApi ? lastApi : "none",
                        s.lastApiTid.load(std::memory_order_relaxed),
                        static_cast<unsigned long long>(tick >= lastTick ? tick - lastTick : 0),
                        static_cast<unsigned long long>(s.execCmdLists.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.signalCount.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.waitCount.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.waitCtxCalls.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.waitCtxFail.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.waitCtxMaxSpins.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.swapChainCreated.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.presentCount.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.psoGraphics.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.psoCompute.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.lastPsoHash.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawExec.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawWithRtv.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawWithBackbuffer.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawNullRtv.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawSkipNoPso.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawSkipPlaceholderVs.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawSkipPlaceholderOther.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawSkipNoVb.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawSkipNoIb.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawSkipMapFail.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawMissingVb.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.drawMissingIb.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.dispatchExec.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.dispatchSkipNoPso.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.dispatchSkipPlaceholderCs.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.placedOk.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.placedOverlap.load(std::memory_order_relaxed)),
                        static_cast<unsigned long long>(s.placedReleased.load(std::memory_order_relaxed)),
                        static_cast<long long>(s.placedLive.load(std::memory_order_relaxed)));
                }
            }).detach();
        } catch (const std::system_error& error) {
            Log("Could not start render-trace heartbeat: %s", error.what());
        }
    }
}

RenderTraceStats& TraceStats() {
    static RenderTraceStats stats;
    return stats;
}

void TraceApi(const char* apiName) {
    if (!Cfg().diagnosticRenderTrace || !apiName) return;
    auto& stats = TraceStats();
    stats.lastApiName.store(apiName, std::memory_order_relaxed);
    stats.lastApiTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    stats.lastApiTid.store(GetCurrentThreadId(), std::memory_order_relaxed);
}

void Log(const char* fmt, ...) {
    if (!g_cfg.log) return;
    std::lock_guard<std::mutex> l(g_logMu);
    if (!g_logFile) {
        g_logPath = g_cfg.baseDir + "\\d3d12on11.log";
        g_logFile = fopen(g_logPath.c_str(), "w");
        if (!g_logFile) {
            char tempPath[MAX_PATH] = {};
            const DWORD length = GetTempPathA(ARRAYSIZE(tempPath), tempPath);
            if (length && length < ARRAYSIZE(tempPath)) {
                char fallbackPath[MAX_PATH] = {};
                const int written = snprintf(fallbackPath, ARRAYSIZE(fallbackPath),
                    "%sd3d12on11_%lu.log", tempPath, GetCurrentProcessId());
                if (written > 0 && static_cast<size_t>(written) < ARRAYSIZE(fallbackPath)) {
                    g_logPath = fallbackPath;
                    g_logFile = fopen(g_logPath.c_str(), "w");
                }
            }
        }
        if (!g_logFile) {
            if (!g_logOpenFailureReported) {
                OutputDebugStringA("d3d12on11: unable to open log beside the proxy or in the temp directory\n");
                g_logOpenFailureReported = true;
            }
            return;
        }
        if (g_cfg.diagnosticRenderTrace)
            fprintf(g_logFile, "Log file: %s [tid=%lu]\n", g_logPath.c_str(), GetCurrentThreadId());
        else
            fprintf(g_logFile, "Log file: %s\n", g_logPath.c_str());
    }
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_logFile, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    if (g_cfg.diagnosticRenderTrace)
        fprintf(g_logFile, "[tid=%lu] ", GetCurrentThreadId());
    va_list ap; va_start(ap, fmt);
    vfprintf(g_logFile, fmt, ap);
    va_end(ap);
    fputc('\n', g_logFile);
    fflush(g_logFile);
}

void Unimpl(const char* name) {
    static std::mutex m;
    static std::set<std::string> seen;
    std::lock_guard<std::mutex> l(m);
    if (seen.insert(name).second) Log("UNIMPL %s", name);
}

// ------------------------------------------------------------------ utilita' varie
UINT64 AlignUp(UINT64 v, UINT64 a) { return (v + a - 1) / a * a; }

uint64_t Fnv1a64(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < len; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
std::string Hex64(uint64_t v) { char b[32]; snprintf(b, sizeof(b), "%016llx", (unsigned long long)v); return b; }

UINT MapRegister(char kind, UINT space, UINT reg) {
    if (!space) return reg;
    const std::string key = std::string(1, kind) + ":" + std::to_string(space) + ":" + std::to_string(reg);
    auto it = Cfg().bindingMap.find(key);
    if (it != Cfg().bindingMap.end()) return it->second;
    Log("No binding-map entry for %s; using register %u (possible collision)", key.c_str(), reg);
    return reg;
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t rd = n ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return rd == (size_t)n;
}
void MakeDirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if ((path[i] == '\\' || path[i] == '/') && cur.size() > 3) _mkdir(cur.c_str());
    }
    _mkdir(path.c_str());
}
bool WriteFileBytes(const std::string& path, const void* data, size_t len) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(data, 1, len, f);
    fclose(f);
    return true;
}

// ------------------------------------------------------------------ container DXBC
DxbcInfo InspectContainer(const void* data, size_t len) {
    DxbcInfo r;
    const uint8_t* b = (const uint8_t*)data;
    if (len < 32 || memcmp(b, "DXBC", 4) != 0) return r;
    r.isDxbc = true;
    uint32_t chunks; memcpy(&chunks, b + 28, 4);
    if (chunks > 64 || 32 + chunks * 4 > len) return r;
    for (uint32_t i = 0; i < chunks; ++i) {
        uint32_t off; memcpy(&off, b + 32 + i * 4, 4);
        if (off + 8 > len) continue;
        const char* cc = (const char*)b + off;
        if (!memcmp(cc, "DXIL", 4) || !memcmp(cc, "ILDB", 4)) r.hasDxil = true;
        if (!memcmp(cc, "SHDR", 4) || !memcmp(cc, "SHEX", 4)) {
            r.hasLegacy = true;
            if (off + 12 <= len) {
                uint32_t ver; memcpy(&ver, b + off + 8, 4);
                r.versionMajor = (ver >> 4) & 0xF; r.versionMinor = ver & 0xF;
            }
        }
        if (!memcmp(cc, "RTS0", 4)) r.hasRts0 = true;
    }
    return r;
}

// ------------------------------------------------------------------ formati DXGI
FormatInfo GetFormatInfo(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: return {128, 1, 1};
    case DXGI_FORMAT_R32G32B32_TYPELESS: case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT: return {96, 1, 1};
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return {64, 1, 1};
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SNORM: case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT: case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return {32, 1, 1};
    case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM: case DXGI_FORMAT_B4G4R4A4_UNORM: return {16, 1, 1};
    case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM: case DXGI_FORMAT_R8_SINT: case DXGI_FORMAT_A8_UNORM: return {8, 1, 1};
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: return {64, 4, 4};
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: return {128, 4, 4};
    default: return {0, 1, 1};
    }
}

// ------------------------------------------------------------------ registro indirizzi virtuali GPU
static std::mutex g_vaMu;
static std::map<UINT64, std::pair<UINT64, Resource*>> g_va;   // base -> (size, risorsa)
static UINT64 g_nextVa = 0x100000000ull;

UINT64 AllocVA(UINT64 size, Resource* r) {
    std::lock_guard<std::mutex> l(g_vaMu);
    UINT64 base = g_nextVa;
    g_nextVa += AlignUp(size ? size : 1, 0x10000) + 0x10000;
    g_va[base] = {size, r};
    return base;
}
void FreeVA(UINT64 va) { std::lock_guard<std::mutex> l(g_vaMu); g_va.erase(va); }
Resource* ResolveVA(UINT64 va, UINT64* offset) {
    std::lock_guard<std::mutex> l(g_vaMu);
    auto it = g_va.upper_bound(va);
    if (it == g_va.begin()) return nullptr;
    --it;
    if (va >= it->first + std::max<UINT64>(it->second.first, 1)) return nullptr;
    if (offset) *offset = va - it->first;
    return it->second.second;
}

// ------------------------------------------------------------------ registro descriptor heap (handle GPU)
static std::mutex g_hpMu;
static std::map<UINT64, DescriptorHeap*> g_heaps;
static std::map<SIZE_T, DescriptorHeap*> g_cpuHeaps;
static UINT64 g_nextGpu = 0x1000;

DescriptorHeap::DescriptorHeap(Device* d, const D3D12_DESCRIPTOR_HEAP_DESC& x)
    : Stub_ID3D12DescriptorHeap(d), hd(x), items(x.NumDescriptors) {
    std::lock_guard<std::mutex> l(g_hpMu);
    g_cpuHeaps[(SIZE_T)items.data()] = this;
    if (x.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        gpuBase = g_nextGpu;
        g_nextGpu += AlignUp((UINT64)(x.NumDescriptors + 1) * kDescStride, 0x1000);
        g_heaps[gpuBase] = this;
    }
}
DescriptorHeap::~DescriptorHeap() {
    std::lock_guard<std::mutex> l(g_hpMu);
    if (gpuBase) g_heaps.erase(gpuBase);
    g_cpuHeaps.erase((SIZE_T)items.data());
}
DescriptorHeap* FindHeapByGpu(UINT64 p, UINT* index) {
    if (!p) return nullptr;
    std::lock_guard<std::mutex> l(g_hpMu);
    auto it = g_heaps.upper_bound(p);
    if (it == g_heaps.begin()) return nullptr;
    --it;
    UINT64 idx = (p - it->first) / kDescStride;
    if (idx >= it->second->items.size()) return nullptr;
    if (index) *index = (UINT)idx;
    return it->second;
}

DescriptorHeap* FindHeapByCpu(SIZE_T p, UINT* index) {
    std::lock_guard<std::mutex> l(g_hpMu);
    auto it = g_cpuHeaps.upper_bound(p);
    if (it == g_cpuHeaps.begin()) return nullptr;
    --it;
    const SIZE_T base = it->first;
    const SIZE_T bytes = it->second->items.size() * sizeof(Descriptor);
    if (p < base || p - base >= bytes || (p - base) % sizeof(Descriptor) != 0) return nullptr;
    if (index) *index = static_cast<UINT>((p - base) / sizeof(Descriptor));
    return it->second;
}

void ClearBackbufferDescriptors(Device* device) {
    std::lock_guard<std::mutex> lock(g_hpMu);
    for (auto& entry : g_cpuHeaps) {
        DescriptorHeap* heap = entry.second;
        if (heap->dev != device) continue;
        for (auto& descriptor : heap->items)
            if (descriptor.res && descriptor.res->backbuffer) descriptor = {};
    }
}
