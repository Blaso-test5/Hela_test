#pragma once
#include "core.h"

struct Resource; struct DescriptorHeap; struct RootSignature; struct PipelineState; struct Fence;

enum StageBits : UINT { S_VS = 1, S_HS = 2, S_DS = 4, S_GS = 8, S_PS = 16, S_CS = 32 };

// ------------------------------------------------------------------ descriptor
enum class DKind : uint8_t { None, CBV, SRV, UAV, RTV, DSV, Sampler };

struct Descriptor {
    DKind kind = DKind::None;
    Ptr<ID3D11ShaderResourceView> srv;
    Ptr<ID3D11UnorderedAccessView> uav;
    Ptr<ID3D11RenderTargetView> rtv;
    Ptr<ID3D11DepthStencilView> dsv;
    Ptr<ID3D11SamplerState> smp;
    Ptr<IUnknown> keep;          // risorsa sorgente (CBV / viste buffer)
    Resource* res = nullptr;     // identita' soltanto (non possiede)
    UINT64 bufOff = 0, bufSize = 0;
    UINT structStride = 0;       // >0: vista structured -> usa la copia structured
    bool uavWrite = false;
};
static const UINT kDescStride = (UINT)sizeof(Descriptor);

// ------------------------------------------------------------------ risorse
struct Resource : Stub_ID3D12Resource {
    D3D12_RESOURCE_DESC desc{};
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_HEAP_FLAGS hflags = D3D12_HEAP_FLAG_NONE;
    D3D12_HEAP_TYPE heap = D3D12_HEAP_TYPE_DEFAULT;   // tipo effettivo: DEFAULT / UPLOAD / READBACK
    UINT64 va = 0;
    Ptr<ID3D11Buffer> buf;         // buffer GPU principale
    Ptr<ID3D11Resource> tex;       // texture 1D/2D/3D
    Ptr<ID3D11Buffer> staging;     // READBACK
    std::vector<uint8_t> shadow;   // memoria CPU (UPLOAD / READBACK)
    bool backbuffer = false;
    bool tiled = false;
    UINT64 nativeWidth = 0;
    Ptr<ID3D12Heap> placedHeap;
    UINT64 placedOffset = 0;
    UINT64 placedSize = 0;
    std::map<std::pair<UINT64, UINT64>, Ptr<ID3D11Buffer>> cbWindows;
    std::map<UINT, Ptr<ID3D11Buffer>> structCopies;
    std::mutex mu;
    int mapCount = 0;

    explicit Resource(Device* d) : Stub_ID3D12Resource(d) {}
    ~Resource() override;
    static HRESULT Create(Device* d, const D3D12_HEAP_PROPERTIES* hp, D3D12_HEAP_FLAGS f,
                          const D3D12_RESOURCE_DESC* desc, Resource** out, bool tiled = false);
    bool IsBuffer() const { return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER; }
    UINT NumSubresources() const;
    ID3D11Resource* Raw() const { return IsBuffer() ? (ID3D11Resource*)buf.Get() : tex.Get(); }
    ID3D11Buffer* GpuBuffer();                                   // crea la copia GPU lazy per UPLOAD
    void SyncToGpu(UINT64 off, UINT64 size);                     // UPLOAD: shadow -> GpuBuffer
    ID3D11Buffer* CbWindow(UINT64 off, UINT64 size);             // cbuffer sincronizzato
    ID3D11Buffer* StructCopy(UINT stride);                       // copia structured (crea)
    void SyncStruct(UINT stride);                                // primario/shadow -> copia
    void WriteBackStruct(UINT stride);                           // copia -> primario (dopo UAV)
    HRESULT RefreshReadback();                                   // staging -> shadow

    HRESULT STDMETHODCALLTYPE Map(UINT sub, const D3D12_RANGE* rr, void** data) override;
    void STDMETHODCALLTYPE Unmap(UINT sub, const D3D12_RANGE* wr) override;
    D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override { return desc; }
    D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override { return va; }
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES* p, D3D12_HEAP_FLAGS* f) override {
        if (tiled) {
            Log("GetHeapProperties called on a reserved resource; returning E_INVALIDARG");
            return E_INVALIDARG;
        }
        if (p) *p = hp;
        if (f) *f = hflags;
        return S_OK; }
};

struct PlacedRangeInfo {
    UINT64 offset = 0;
    UINT64 size = 0;
    Resource* resource = nullptr;
    D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_UNKNOWN;
    UINT64 width = 0;
    UINT height = 0;
    UINT16 depthOrArray = 0;
    UINT16 mipLevels = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    DWORD creatorTid = 0;
};

struct HeapObj : Stub_ID3D12Heap {
    D3D12_HEAP_DESC hd{};
    Ptr<ID3D11Buffer> tilePool;
    std::mutex tilePoolMutex;
    std::mutex placementMutex;
    std::map<UINT64, PlacedRangeInfo> placedRanges;
    HeapObj(Device* d, const D3D12_HEAP_DESC& x) : Stub_ID3D12Heap(d), hd(x) {}
    D3D12_HEAP_DESC STDMETHODCALLTYPE GetDesc() override { return hd; }
    HRESULT GetTilePool(ID3D11Buffer** out);
    HRESULT ReservePlacedRange(UINT64 offset, UINT64 size, const D3D12_RESOURCE_DESC* desc,
        PlacedRangeInfo* outConflict = nullptr);
    void BindPlacedResource(UINT64 offset, Resource* resource);
    void ReleasePlacedRange(UINT64 offset, Resource* resource = nullptr);
};

inline UINT64 CurrentGpuTimestampNs() {
    LARGE_INTEGER qpc{}, frequency{};
    if (!QueryPerformanceCounter(&qpc) || !QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0 || qpc.QuadPart < 0)
        return 0;
    const UINT64 ticks = static_cast<UINT64>(qpc.QuadPart);
    const UINT64 hz = static_cast<UINT64>(frequency.QuadPart);
    return (ticks / hz) * 1000000000ULL +
        ((ticks % hz) * 1000000000ULL) / hz;
}

struct QueryHeapObj : Stub_ID3D12QueryHeap {
    D3D12_QUERY_HEAP_DESC desc{};
    std::mutex mutex;
    std::vector<Ptr<ID3D11Query>> queries;
    std::vector<UINT64> timestamps;

    QueryHeapObj(Device* d, const D3D12_QUERY_HEAP_DESC& queryDesc)
        : Stub_ID3D12QueryHeap(d), desc(queryDesc),
          queries(queryDesc.Count), timestamps(queryDesc.Count, 0) {}
};

UINT64 AllocVA(UINT64 size, Resource* r);
void FreeVA(UINT64 va);
Resource* ResolveVA(UINT64 va, UINT64* offset);
D3D12_HEAP_TYPE EffectiveHeapType(const D3D12_HEAP_PROPERTIES& hp);

struct DescriptorHeap : Stub_ID3D12DescriptorHeap {
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    std::vector<Descriptor> items;
    UINT64 gpuBase = 0;
    DescriptorHeap(Device* d, const D3D12_DESCRIPTOR_HEAP_DESC& x);
    ~DescriptorHeap() override;
    D3D12_DESCRIPTOR_HEAP_DESC STDMETHODCALLTYPE GetDesc() override { return hd; }
    D3D12_CPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetCPUDescriptorHandleForHeapStart() override {
        D3D12_CPU_DESCRIPTOR_HANDLE h; h.ptr = (SIZE_T)items.data(); return h; }
    D3D12_GPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetGPUDescriptorHandleForHeapStart() override {
        D3D12_GPU_DESCRIPTOR_HANDLE h; h.ptr = gpuBase; return h; }
};
DescriptorHeap* FindHeapByGpu(UINT64 gpuPtr, UINT* index);
DescriptorHeap* FindHeapByCpu(SIZE_T cpuPtr, UINT* index);
void ClearBackbufferDescriptors(Device* device);

// ------------------------------------------------------------------ root signature
struct RangeInfo {
    D3D12_DESCRIPTOR_RANGE_TYPE type;
    UINT num, baseReg, space, offset;
    std::vector<UINT> slots;
};
struct RootParam {
    D3D12_ROOT_PARAMETER_TYPE type = D3D12_ROOT_PARAMETER_TYPE_CBV;
    D3D12_SHADER_VISIBILITY vis = D3D12_SHADER_VISIBILITY_ALL;
    UINT num32 = 0, reg = 0, space = 0, slot = 0;
    std::vector<RangeInfo> ranges;
    Ptr<ID3D11Buffer> cb;   // root constants
};
struct StaticSamplerInfo { UINT slot; UINT space; D3D12_SHADER_VISIBILITY vis; Ptr<ID3D11SamplerState> st; };

struct RootSignature : Stub_ID3D12RootSignature {
    std::vector<RootParam> params;
    std::vector<StaticSamplerInfo> statics;
    UINT id = 0;
    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    using Stub_ID3D12RootSignature::Stub_ID3D12RootSignature;
    static HRESULT Create(Device* d, const void* blob, SIZE_T len, RootSignature** out);
};

// ------------------------------------------------------------------ PSO
struct PipelineState : Stub_ID3D12PipelineState {
    bool compute = false, missingShader = false, missingVs = false, missingCs = false;
    uint64_t shaderHash = 0;
    Ptr<ID3D11VertexShader> vs; Ptr<ID3D11PixelShader> ps; Ptr<ID3D11GeometryShader> gs;
    Ptr<ID3D11HullShader> hs; Ptr<ID3D11DomainShader> ds; Ptr<ID3D11ComputeShader> cs;
    Ptr<ID3D11InputLayout> il;
    Ptr<ID3D11RasterizerState> rs; Ptr<ID3D11BlendState> bs; Ptr<ID3D11DepthStencilState> dss;
    UINT sampleMask = 0xffffffffu;
    Ptr<IUnknown> rootSig;
    using Stub_ID3D12PipelineState::Stub_ID3D12PipelineState;
    static HRESULT CreateGraphics(Device* d, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, PipelineState** out);
    static HRESULT CreateCompute(Device* d, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, PipelineState** out);
};

struct CommandSignature : Stub_ID3D12CommandSignature {
    D3D12_INDIRECT_ARGUMENT_TYPE argumentType;
    UINT byteStride;
    Ptr<IUnknown> rootSignature;
    CommandSignature(Device* d, D3D12_INDIRECT_ARGUMENT_TYPE type, UINT stride,
        ID3D12RootSignature* root)
        : Stub_ID3D12CommandSignature(d), argumentType(type), byteStride(stride),
          rootSignature(root) {}
};

// ------------------------------------------------------------------ fence / queue / lista di comandi
struct Fence : Stub_ID3D12Fence {
    std::mutex m;
    UINT64 val = 0;
    struct Waiter { UINT64 v; HANDLE ev; };
    std::vector<Waiter> waiters;
    Fence(Device* d, UINT64 init) : Stub_ID3D12Fence(d), val(init) {}
    void CompleteTo(UINT64 v);
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override;
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 v, HANDLE ev) override;
    HRESULT STDMETHODCALLTYPE Signal(UINT64 v) override { CompleteTo(v); return S_OK; }
};

struct CommandAllocator : Stub_ID3D12CommandAllocator {
    using Stub_ID3D12CommandAllocator::Stub_ID3D12CommandAllocator;
    HRESULT STDMETHODCALLTYPE Reset() override { return S_OK; }
};

struct ExecState {
    Device* dev = nullptr;
    ID3D11DeviceContext* c = nullptr;
    bool skipDraws = false;
    bool skipDispatches = false;
    bool hasGraphicsPipeline = false;
    bool hasComputePipeline = false;
    bool graphicsPlaceholderVs = false;
    bool computePlaceholderCs = false;
    uintptr_t graphicsPipelineIdentity = 0;
    uint64_t graphicsShaderHash = 0;
    bool hasVertexBuffer = false;
    bool hasIndexBuffer = false;
    bool rtvBackbuffer = false;
    std::array<bool, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexBuffers{};
    Ptr<ID3D11RenderTargetView> rtvs[8];
    UINT numRtv = 0;
    Ptr<ID3D11DepthStencilView> dsv;
    Ptr<ID3D11BlendState> bs;
    Ptr<ID3D11DepthStencilState> dss;
    float blend[4] = {1, 1, 1, 1};
    UINT stencilRef = 0, sampleMask = 0xffffffffu;
    std::vector<Ptr<IUnknown>> readbacks;
    std::vector<std::pair<Ptr<IUnknown>, UINT>> structWrites;   // (Resource, stride)
    void ApplyOM() { c->OMSetBlendState(bs.Get(), blend, sampleMask); c->OMSetDepthStencilState(dss.Get(), stencilRef); }
};
using Cmd = std::function<void(ExecState&)>;

struct RootArg {
    uint8_t kind = 0;
    UINT64 value = 0;
    std::vector<UINT> consts;
    Ptr<IUnknown> keep;
};   // 1=consts 2=cbv 3=srv 4=uav 5=table
struct BindSnap {
    Ptr<IUnknown> keep;
    RootSignature* rs = nullptr;
    std::vector<RootArg> args;
    std::vector<Ptr<IUnknown>> descriptorHeaps;
};
struct BindState {
    Ptr<IUnknown> keep; RootSignature* rs = nullptr; std::vector<RootArg> args;
    std::shared_ptr<const BindSnap> snap;
};

struct CommandList : Stub_ID3D12GraphicsCommandList {
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    std::vector<Cmd> cmds;
    bool closed = false;
    BindState gfx, comp;
    std::vector<Ptr<IUnknown>> descriptorHeaps;
    CommandList(Device* d, D3D12_COMMAND_LIST_TYPE t) : Stub_ID3D12GraphicsCommandList(d), type(t) {}
    void Run(ExecState& ex) { for (auto& c : cmds) c(ex); }
    void Add(Cmd c) { cmds.push_back(std::move(c)); }
    std::shared_ptr<const BindSnap> Snap(BindState& b);
    void SetRootArg(BindState& b, UINT idx, RootArg&& a);
    void SetRootConstants(BindState& b, UINT idx, UINT count, const void* values, UINT offset);
    void DrawCommon(Cmd draw, bool gfxBind);

    D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() override { return type; }
    HRESULT STDMETHODCALLTYPE Close() override;
    HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator* a, ID3D12PipelineState* p) override;
    void STDMETHODCALLTYPE ClearState(ID3D12PipelineState* p) override;
    void STDMETHODCALLTYPE DrawInstanced(UINT vc, UINT ic, UINT sv, UINT si) override;
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT ic, UINT inst, UINT si, INT bv, UINT sinst) override;
    void STDMETHODCALLTYPE Dispatch(UINT x, UINT y, UINT z) override;
    void STDMETHODCALLTYPE ExecuteIndirect(ID3D12CommandSignature* signature, UINT maxCommandCount,
        ID3D12Resource* argumentBuffer, UINT64 argumentBufferOffset,
        ID3D12Resource* countBuffer, UINT64 countBufferOffset) override;
    void STDMETHODCALLTYPE CopyBufferRegion(ID3D12Resource* dst, UINT64 dOff, ID3D12Resource* src, UINT64 sOff, UINT64 n) override;
    void STDMETHODCALLTYPE CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* dst, UINT dx, UINT dy, UINT dz,
                                             const D3D12_TEXTURE_COPY_LOCATION* src, const D3D12_BOX* box) override;
    void STDMETHODCALLTYPE CopyResource(ID3D12Resource* dst, ID3D12Resource* src) override;
    void STDMETHODCALLTYPE CopyTiles(ID3D12Resource* tiledResource,
        const D3D12_TILED_RESOURCE_COORDINATE* coordinate, const D3D12_TILE_REGION_SIZE* region,
        ID3D12Resource* buffer, UINT64 offset, D3D12_TILE_COPY_FLAGS flags) override;
    void STDMETHODCALLTYPE ResolveSubresource(ID3D12Resource* dst, UINT ds, ID3D12Resource* src, UINT ss, DXGI_FORMAT f) override;
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY t) override;
    void STDMETHODCALLTYPE RSSetViewports(UINT n, const D3D12_VIEWPORT* v) override;
    void STDMETHODCALLTYPE RSSetScissorRects(UINT n, const D3D12_RECT* r) override;
    void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT f[4]) override;
    void STDMETHODCALLTYPE OMSetStencilRef(UINT r) override;
    void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState* p) override;
    void STDMETHODCALLTYPE ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER* barriers) override;
    void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList* l) override;
    void STDMETHODCALLTYPE SetDescriptorHeaps(UINT count, ID3D12DescriptorHeap* const* heaps) override;
    void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature* rs) override;
    void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature* rs) override;
    void STDMETHODCALLTYPE SetComputeRootDescriptorTable(UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) override;
    void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) override;
    void STDMETHODCALLTYPE SetComputeRoot32BitConstant(UINT i, UINT d, UINT off) override;
    void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(UINT i, UINT d, UINT off) override;
    void STDMETHODCALLTYPE SetComputeRoot32BitConstants(UINT i, UINT n, const void* d, UINT off) override;
    void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(UINT i, UINT n, const void* d, UINT off) override;
    void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) override;
    void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) override;
    void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) override;
    void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) override;
    void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) override;
    void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) override;
    void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* v) override;
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT s, UINT n, const D3D12_VERTEX_BUFFER_VIEW* v) override;
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE* r, BOOL single,
                                              const D3D12_CPU_DESCRIPTOR_HANDLE* d) override;
    void STDMETHODCALLTYPE ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE h, D3D12_CLEAR_FLAGS f, FLOAT d, UINT8 s, UINT n, const D3D12_RECT* r) override;
    void STDMETHODCALLTYPE ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE h, const FLOAT c[4], UINT n, const D3D12_RECT* r) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE g, D3D12_CPU_DESCRIPTOR_HANDLE c, ID3D12Resource* r, const UINT v[4], UINT n, const D3D12_RECT* rc) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE g, D3D12_CPU_DESCRIPTOR_HANDLE c, ID3D12Resource* r, const float v[4], UINT n, const D3D12_RECT* rc) override;
    void STDMETHODCALLTYPE DiscardResource(ID3D12Resource*, const D3D12_DISCARD_REGION*) override {
        Unimpl("ID3D12GraphicsCommandList::DiscardResource");
    }
    void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT) override;
    void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT) override;
    void STDMETHODCALLTYPE ResolveQueryData(ID3D12QueryHeap* h, D3D12_QUERY_TYPE t, UINT s, UINT n, ID3D12Resource* dst, UINT64 off) override;
    void STDMETHODCALLTYPE SetPredication(ID3D12Resource*, UINT64, D3D12_PREDICATION_OP) override {
        Unimpl("ID3D12GraphicsCommandList::SetPredication");
    }
    void STDMETHODCALLTYPE SetMarker(UINT, const void*, UINT) override {}
    void STDMETHODCALLTYPE BeginEvent(UINT, const void*, UINT) override {}
    void STDMETHODCALLTYPE EndEvent() override {}
};

struct CommandQueue : Stub_ID3D12CommandQueue {
    D3D12_COMMAND_QUEUE_DESC qd{};
    CommandQueue(Device* d, const D3D12_COMMAND_QUEUE_DESC& x) : Stub_ID3D12CommandQueue(d), qd(x) {}
    void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource*, UINT, const D3D12_TILED_RESOURCE_COORDINATE*,
        const D3D12_TILE_REGION_SIZE*, ID3D12Heap*, UINT, const D3D12_TILE_RANGE_FLAGS*,
        const UINT*, const UINT*, D3D12_TILE_MAPPING_FLAGS) override;
    void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* dst,
        const D3D12_TILED_RESOURCE_COORDINATE* dstCoordinate, ID3D12Resource* src,
        const D3D12_TILED_RESOURCE_COORDINATE* srcCoordinate, const D3D12_TILE_REGION_SIZE* region,
        D3D12_TILE_MAPPING_FLAGS flags) override;
    void STDMETHODCALLTYPE ExecuteCommandLists(UINT n, ID3D12CommandList* const* lists) override;
    void STDMETHODCALLTYPE SetMarker(UINT, const void*, UINT) override {}
    void STDMETHODCALLTYPE BeginEvent(UINT, const void*, UINT) override {}
    void STDMETHODCALLTYPE EndEvent() override {}
    HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* f, UINT64 v) override;
    HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* f, UINT64 v) override;
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* f) override;
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* g, UINT64* c) override;
    D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override { return qd; }
};

// ------------------------------------------------------------------ device
struct PendingSignal {
    Ptr<ID3D11Query> q;
    Ptr<IUnknown> fence;
    UINT64 value = 0;
    std::vector<Ptr<IUnknown>> readbacks;
};

struct Device : Stub_ID3D12Device2 {
    Ptr<ID3D11Device> d11;
    Ptr<ID3D11Device1> d11_1;
    Ptr<ID3D11Device2> d11_2;
    Ptr<ID3D11DeviceContext> ctx;
    Ptr<ID3D11DeviceContext2> ctx2;
    D3D11_TILED_RESOURCES_TIER tiledTier = D3D11_TILED_RESOURCES_NOT_SUPPORTED;
    Ptr<IDXGIAdapter> adapter;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    std::recursive_mutex ctxLock;
    UINT64 timestampFrequency = 0;
    std::mutex pendLock;
    std::deque<PendingSignal> pending;
    std::thread poller;
    std::condition_variable_any pollCv;
    std::atomic<bool> stopPoll{false};
    std::atomic<UINT> rootSigCounter{0};
    std::mutex swapLock;
    std::set<Resource*> backbuffers;   // wrapper dei backbuffer attivi (identita')

    explicit Device(IDXGIAdapter* a) : Stub_ID3D12Device2(this), adapter(a) {}
    ~Device() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    HRESULT Init(D3D_FEATURE_LEVEL minFl);
    void PollFences();
    void AddPending(PendingSignal&& p);
    void ReleaseBackbufferViews();

    UINT STDMETHODCALLTYPE GetNodeCount() override {
        D3D12ON11_TRACE_CALLER("GetNodeCount");
        return 1;
    }
    HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* d, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE t, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC* d, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* d,
        ID3D12RootSignature* r, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT node, D3D12_COMMAND_LIST_TYPE t, ID3D12CommandAllocator* a, ID3D12PipelineState* ps, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE f, void* data, UINT size) override;
    HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* d, REFIID riid, void** p) override;
    UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE) override { return kDescStride; }
    HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT node, const void* b, SIZE_T len, REFIID riid, void** p) override;
    void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override;
    void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* r, const D3D12_SHADER_RESOURCE_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override;
    void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* r, ID3D12Resource* c, const D3D12_UNORDERED_ACCESS_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override;
    void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* r, const D3D12_RENDER_TARGET_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override;
    void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* r, const D3D12_DEPTH_STENCIL_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override;
    void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h) override;
    void STDMETHODCALLTYPE CopyDescriptors(UINT dn, const D3D12_CPU_DESCRIPTOR_HANDLE* dof, const UINT* dsz, UINT sn,
                                           const D3D12_CPU_DESCRIPTOR_HANDLE* sof, const UINT* ssz, D3D12_DESCRIPTOR_HEAP_TYPE t) override;
    void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE d, const D3D12_CPU_DESCRIPTOR_HANDLE s, D3D12_DESCRIPTOR_HEAP_TYPE t) override;
    D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(UINT mask, UINT n, const D3D12_RESOURCE_DESC* d) override;
    D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(UINT node, D3D12_HEAP_TYPE t) override;
    HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES* hp, D3D12_HEAP_FLAGS f, const D3D12_RESOURCE_DESC* d,
                                                      D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE* cv, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC* d, D3D12_RESOURCE_STATES s,
                                                     const D3D12_CLEAR_VALUE* cv, REFIID riid, void** p) override;
    void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource* resource, UINT* numTiles,
        D3D12_PACKED_MIP_INFO* packedMips, D3D12_TILE_SHAPE* tileShape,
        UINT* numSubresourceTilings, UINT firstSubresourceTiling,
        D3D12_SUBRESOURCE_TILING* subresourceTilings) override;
    HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC* d, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap* h, UINT64 off, const D3D12_RESOURCE_DESC* d, D3D12_RESOURCE_STATES s,
                                                   const D3D12_CLEAR_VALUE* cv, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE MakeResident(UINT, ID3D12Pageable* const*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Evict(UINT, ID3D12Pageable* const*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateFence(UINT64 init, D3D12_FENCE_FLAGS f, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override { return d11 ? d11->GetDeviceRemovedReason() : S_OK; }
    void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC* d, UINT first, UINT count, UINT64 base,
                                                 D3D12_PLACED_SUBRESOURCE_FOOTPRINT* lay, UINT* rows, UINT64* rowSize, UINT64* total) override;
    HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* d, REFIID riid, void** p) override;
    HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL) override { return S_OK; }
    LUID STDMETHODCALLTYPE GetAdapterLuid() override;
};
