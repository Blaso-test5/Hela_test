#include "types.h"

namespace {

D3D12_TILED_RESOURCES_TIER EffectiveTiledResourcesTier(
    D3D11_TILED_RESOURCES_TIER nativeTier, bool hasDevice2, bool hasContext2) {
    wchar_t overrideValue[32] = {};
    const DWORD length = GetEnvironmentVariableW(L"D3D12ON11_TILED_RESOURCES_TIER",
        overrideValue, ARRAYSIZE(overrideValue));
    if (length > 0 && length < ARRAYSIZE(overrideValue)) {
        wchar_t* end = nullptr;
        const unsigned long parsed = wcstoul(overrideValue, &end, 0);
        if (end != overrideValue && *end == L'\0' && parsed <= 3)
            return static_cast<D3D12_TILED_RESOURCES_TIER>(parsed);
    }
    if (Cfg().diagnosticCapabilities)
        return D3D12_TILED_RESOURCES_TIER_3;
    return nativeTier >= D3D11_TILED_RESOURCES_TIER_2 && hasDevice2 && hasContext2
        ? D3D12_TILED_RESOURCES_TIER_1
        : D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
}

}

void DeviceAddRef(Device* device) {
    if (device) device->AddRef();
}

void DeviceRelease(Device* device) {
    if (device) device->Release();
}

HRESULT STDMETHODCALLTYPE Device::QueryInterface(REFIID riid, void** out) {
    D3D12ON11_TRACE_CALLER("Device::QueryInterface");
    if (!out) return E_POINTER;
    *out = nullptr;

    const char* interfaceName = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) ||
        riid == __uuidof(ID3D12Device2)) {
        *out = static_cast<ID3D12Device2*>(this);
        interfaceName = "ID3D12Device2";
    } else if (riid == __uuidof(ID3D12Device1)) {
        *out = static_cast<ID3D12Device1*>(this);
        interfaceName = "ID3D12Device1";
    } else if (riid == __uuidof(ID3D12Device)) {
        *out = static_cast<ID3D12Device*>(this);
        interfaceName = "ID3D12Device";
    }

    if (!interfaceName) {
        Log("Device QueryInterface unsupported iid=%08lx-%04x-%04x",
            riid.Data1, riid.Data2, riid.Data3);
        return E_NOINTERFACE;
    }

    Log("Device QueryInterface %s result=0x%08lx", interfaceName, S_OK);
    AddRef();
    return S_OK;
}

HRESULT DeviceQuery(Device* device, REFIID riid, void** out) {
    if (!device) {
        if (out) *out = nullptr;
        return E_POINTER;
    }
    return device->QueryInterface(riid, out);
}

Device::~Device() {
    Log("Device destroyed");
    stopPoll = true;
    pollCv.notify_all();
    if (poller.joinable()) poller.join();
    ReleaseBackbufferViews();
}

HRESULT Device::Init(D3D_FEATURE_LEVEL minFeatureLevel) {
    if (minFeatureLevel > D3D_FEATURE_LEVEL_12_0) {
        Log("Requested minimum feature level 0x%04x exceeds the proxy limit 0x%04x",
            static_cast<UINT>(minFeatureLevel), static_cast<UINT>(D3D_FEATURE_LEVEL_12_0));
        return DXGI_ERROR_UNSUPPORTED;
    }
    if (minFeatureLevel > D3D_FEATURE_LEVEL_11_1) {
        Log("Accepting D3D12 minimum feature level 0x%04x on the D3D11 translation backend",
            static_cast<UINT>(minFeatureLevel));
    }
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    UINT flags = Cfg().debugLayer ? D3D11_CREATE_DEVICE_DEBUG : 0;
    HRESULT result = D3D11CreateDevice(
        adapter.Get(),
        adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        d11.Put(),
        &fl,
        ctx.Put());
    if (result == E_INVALIDARG) {
        result = D3D11CreateDevice(
            adapter.Get(),
            adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels + 1,
            1,
            D3D11_SDK_VERSION,
            d11.Put(),
            &fl,
            ctx.Put());
    }
    if (FAILED(result) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        Log("D3D11 debug layer unavailable (0x%08lx); retrying without it", result);
        result = D3D11CreateDevice(
            adapter.Get(),
            adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            featureLevels + 1,
            1,
            D3D11_SDK_VERSION,
            d11.Put(),
            &fl,
            ctx.Put());
    }
    if (FAILED(result)) {
        Log("D3D11CreateDevice failed (0x%08lx)", result);
        return result;
    }
    if (fl < D3D_FEATURE_LEVEL_11_0) {
        Log("D3D11 feature level %04x is below the proxy backend minimum %04x",
            static_cast<UINT>(fl), static_cast<UINT>(D3D_FEATURE_LEVEL_11_0));
        return DXGI_ERROR_UNSUPPORTED;
    }
    d11->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(d11_1.Put()));
    d11->QueryInterface(__uuidof(ID3D11Device2), reinterpret_cast<void**>(d11_2.Put()));
    ctx->QueryInterface(__uuidof(ID3D11DeviceContext2), reinterpret_cast<void**>(ctx2.Put()));
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 options1{};
    const HRESULT tiledResult = d11->CheckFeatureSupport(
        D3D11_FEATURE_D3D11_OPTIONS1, &options1, sizeof(options1));
    if (SUCCEEDED(tiledResult)) tiledTier = options1.TiledResourcesTier;
    Log("Created D3D11 device at feature level %04x", fl);
    Log("D3D11 tiled resources tier=%u feature-query=0x%08lx device2=%u context2=%u",
        tiledTier, tiledResult, d11_2 ? 1u : 0u, ctx2 ? 1u : 0u);
    return S_OK;
}

void Device::PollFences() {}
void Device::AddPending(PendingSignal&& signal) {
    std::lock_guard<std::mutex> lock(pendLock);
    pending.emplace_back(std::move(signal));
    pollCv.notify_all();
}
void Device::ReleaseBackbufferViews() {
    {
        std::lock_guard<std::recursive_mutex> lock(ctxLock);
        if (ctx) {
            ctx->ClearState();
            ctx->Flush();
        }
    }
    ClearBackbufferDescriptors(this);
}

HRESULT STDMETHODCALLTYPE Device::CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (type > D3D12_COMMAND_LIST_TYPE_COPY) return E_INVALIDARG;
    auto* allocator = new (std::nothrow) CommandAllocator(this);
    if (!allocator) return E_OUTOFMEMORY;
    const HRESULT result = allocator->QueryInterface(riid, out);
    allocator->Release();
    return result;
}

HRESULT STDMETHODCALLTYPE Device::CheckFeatureSupport(D3D12_FEATURE feature, void* data, UINT size) {
    D3D12ON11_TRACE_CALLER("Device::CheckFeatureSupport");
    const char* featureName = "unknown";
    switch (feature) {
    case D3D12_FEATURE_D3D12_OPTIONS: featureName = "D3D12_OPTIONS"; break;
    case D3D12_FEATURE_ARCHITECTURE: featureName = "ARCHITECTURE"; break;
    case D3D12_FEATURE_FEATURE_LEVELS: featureName = "FEATURE_LEVELS"; break;
    case D3D12_FEATURE_FORMAT_SUPPORT: featureName = "FORMAT_SUPPORT"; break;
    case D3D12_FEATURE_SHADER_MODEL: featureName = "SHADER_MODEL"; break;
    case D3D12_FEATURE_D3D12_OPTIONS1: featureName = "D3D12_OPTIONS1"; break;
    case D3D12_FEATURE_D3D12_OPTIONS9: featureName = "D3D12_OPTIONS9"; break;
    default: break;
    }
    Log("CheckFeatureSupport begin feature=%s(%u) size=%u data=%s",
        featureName, static_cast<UINT>(feature), size, data ? "present" : "null");
    if (!data) {
        Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=null-data",
            featureName, E_INVALIDARG);
        return E_INVALIDARG;
    }
    switch (feature) {
    case D3D12_FEATURE_FORMAT_SUPPORT: {
        if (size < sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)) {
            Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=short-buffer required=%zu",
                featureName, E_INVALIDARG, sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT));
            return E_INVALIDARG;
        }
        auto* info = static_cast<D3D12_FEATURE_DATA_FORMAT_SUPPORT*>(data);
        if (info->Format == DXGI_FORMAT_UNKNOWN) {
            info->Support1 = D3D12_FORMAT_SUPPORT1_NONE;
            info->Support2 = D3D12_FORMAT_SUPPORT2_NONE;
            Log("CheckFeatureSupport end feature=%s format=%u result=0x%08lx reason=unknown-format",
                featureName, info->Format, E_INVALIDARG);
            return E_INVALIDARG;
        }
        D3D11_FEATURE_DATA_FORMAT_SUPPORT support1{info->Format, 0};
        const HRESULT support1Result = d11->CheckFormatSupport(
            info->Format, &support1.OutFormatSupport);
        if (FAILED(support1Result)) {
            info->Support1 = D3D12_FORMAT_SUPPORT1_NONE;
            info->Support2 = D3D12_FORMAT_SUPPORT2_NONE;
            Log("CheckFeatureSupport end feature=%s format=%u result=0x%08lx support1-query=0x%08lx",
                featureName, info->Format, support1Result, support1Result);
            return support1Result;
        }

        const UINT d3d11OnlySupport1 =
            D3D11_FORMAT_SUPPORT_MIP_AUTOGEN | D3D11_FORMAT_SUPPORT_CPU_LOCKABLE;
        info->Support1 = static_cast<D3D12_FORMAT_SUPPORT1>(
            support1.OutFormatSupport & ~d3d11OnlySupport1);

        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support2{info->Format, 0};
        const HRESULT support2Result = d11->CheckFeatureSupport(
            D3D11_FEATURE_FORMAT_SUPPORT2, &support2, sizeof(support2));
        const UINT d3d11OnlySupport2 = D3D11_FORMAT_SUPPORT2_SHAREABLE;
        info->Support2 = SUCCEEDED(support2Result)
            ? static_cast<D3D12_FORMAT_SUPPORT2>(support2.OutFormatSupport2 & ~d3d11OnlySupport2)
            : D3D12_FORMAT_SUPPORT2_NONE;
        Log("CheckFeatureSupport end feature=%s format=%u result=0x%08lx support1=0x%08x support2=0x%08x support2-query=0x%08lx",
            featureName, info->Format, S_OK, static_cast<UINT>(info->Support1),
            static_cast<UINT>(info->Support2), support2Result);
        return S_OK;
    }
    case D3D12_FEATURE_FEATURE_LEVELS: {
        if (size < sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)) {
            Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=short-buffer required=%zu",
                featureName, E_INVALIDARG, sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS));
            return E_INVALIDARG;
        }
        auto* info = static_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(data);
        if (!info->pFeatureLevelsRequested || info->NumFeatureLevels == 0) {
            Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=invalid-request count=%u list=%s",
                featureName, E_INVALIDARG, info->NumFeatureLevels,
                info->pFeatureLevelsRequested ? "present" : "null");
            return E_INVALIDARG;
        }
        std::string requestedLevels;
        for (UINT i = 0; i < info->NumFeatureLevels; ++i) {
            if (!requestedLevels.empty()) requestedLevels += ",";
            char level[16] = {};
            snprintf(level, sizeof(level), "0x%04x",
                static_cast<UINT>(info->pFeatureLevelsRequested[i]));
            requestedLevels += level;
        }
        info->MaxSupportedFeatureLevel = fl >= D3D_FEATURE_LEVEL_11_0
            ? D3D_FEATURE_LEVEL_12_0 : D3D_FEATURE_LEVEL_11_0;
        for (UINT i = 0; i < info->NumFeatureLevels; ++i) {
            if (info->pFeatureLevelsRequested[i] <= info->MaxSupportedFeatureLevel) {
                info->MaxSupportedFeatureLevel = info->pFeatureLevelsRequested[i];
                break;
            }
        }
        Log("CheckFeatureSupport end feature=%s result=0x%08lx requested=[%s] returned=0x%04x",
            featureName, S_OK, requestedLevels.c_str(),
            static_cast<UINT>(info->MaxSupportedFeatureLevel));
        return S_OK;
    }
    case D3D12_FEATURE_SHADER_MODEL: {
        if (size < sizeof(D3D12_FEATURE_DATA_SHADER_MODEL)) {
            Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=short-buffer required=%zu",
                featureName, E_INVALIDARG, sizeof(D3D12_FEATURE_DATA_SHADER_MODEL));
            return E_INVALIDARG;
        }
        auto* info = static_cast<D3D12_FEATURE_DATA_SHADER_MODEL*>(data);
        const D3D_SHADER_MODEL requested = info->HighestShaderModel;
        const D3D_SHADER_MODEL configured = static_cast<D3D_SHADER_MODEL>(Cfg().shaderModel);
        info->HighestShaderModel = info->HighestShaderModel < configured
            ? info->HighestShaderModel : configured;
        Log("CheckFeatureSupport end feature=%s result=0x%08lx requested=0x%x returned=0x%x",
            featureName, S_OK, static_cast<UINT>(requested),
            static_cast<UINT>(info->HighestShaderModel));
        return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS: {
        if (size < sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS)) {
            Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=short-buffer required=%zu",
                featureName, E_INVALIDARG, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS));
            return E_INVALIDARG;
        }
        auto* info = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS*>(data);
        *info = {};
        info->ResourceBindingTier = Cfg().diagnosticCapabilities
            ? D3D12_RESOURCE_BINDING_TIER_3 : D3D12_RESOURCE_BINDING_TIER_1;
        info->TiledResourcesTier = EffectiveTiledResourcesTier(
            tiledTier, static_cast<bool>(d11_2), static_cast<bool>(ctx2));
        info->ConservativeRasterizationTier = D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED;
        info->CrossNodeSharingTier = D3D12_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
        info->ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_1;
        D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpuVa{};
        const HRESULT gpuVaResult = d11->CheckFeatureSupport(
            D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &gpuVa, sizeof(gpuVa));
        if (SUCCEEDED(gpuVaResult))
            info->MaxGPUVirtualAddressBitsPerResource = gpuVa.MaxGPUVirtualAddressBitsPerResource;
        Log("CheckFeatureSupport end feature=%s result=0x%08lx binding-tier=%u tiled-tier=%u conservative-raster=%u heap-tier=%u double-precision=%u logic-op=%u min-precision=0x%x typed-uav-load=%u ROV=%u stencil-ref=%u swizzle64k=%u cross-node=%u cross-adapter-row-major=%u rasterizer-array-index=%u max-gpu-va-bits=%u gpu-va-query=0x%08lx",
            featureName, S_OK, info->ResourceBindingTier, info->TiledResourcesTier,
            info->ConservativeRasterizationTier, info->ResourceHeapTier,
            info->DoublePrecisionFloatShaderOps, info->OutputMergerLogicOp,
            info->MinPrecisionSupport, info->TypedUAVLoadAdditionalFormats, info->ROVsSupported,
            info->PSSpecifiedStencilRefSupported, info->StandardSwizzle64KBSupported,
            info->CrossNodeSharingTier, info->CrossAdapterRowMajorTextureSupported,
            info->VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation,
            info->MaxGPUVirtualAddressBitsPerResource, gpuVaResult);
        return S_OK;
    }
    case D3D12_FEATURE_ARCHITECTURE: {
        if (size < sizeof(D3D12_FEATURE_DATA_ARCHITECTURE)) {
            Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=short-buffer required=%zu",
                featureName, E_INVALIDARG, sizeof(D3D12_FEATURE_DATA_ARCHITECTURE));
            return E_INVALIDARG;
        }
        auto* info = static_cast<D3D12_FEATURE_DATA_ARCHITECTURE*>(data);
        if (info->NodeIndex != 0) {
            Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=unsupported-node node=%u",
                featureName, E_INVALIDARG, info->NodeIndex);
            return E_INVALIDARG;
        }
        info->TileBasedRenderer = FALSE;
        info->UMA = FALSE;
        info->CacheCoherentUMA = FALSE;
        Log("CheckFeatureSupport end feature=%s result=0x%08lx node=0 tiled-renderer=%u UMA=%u coherent-UMA=%u",
            featureName, S_OK, info->TileBasedRenderer, info->UMA, info->CacheCoherentUMA);
        return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS1: {
        if (size < sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS1)) {
            Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=short-buffer required=%zu",
                featureName, E_INVALIDARG, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS1));
            return E_INVALIDARG;
        }
        auto* info = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS1*>(data);
        *info = {};
        if (Cfg().diagnosticCapabilities) {
            info->WaveOps = TRUE;
            info->WaveLaneCountMin = 32;
            info->WaveLaneCountMax = 32;
            info->TotalLaneCount = 32;
            info->Int64ShaderOps = TRUE;
        }
        Log("CheckFeatureSupport end feature=%s result=0x%08lx wave-ops=%u wave-lanes=%u..%u total-lanes=%u expanded-compute-states=%u int64-shader-ops=%u",
            featureName, S_OK, info->WaveOps, info->WaveLaneCountMin,
            info->WaveLaneCountMax, info->TotalLaneCount,
            info->ExpandedComputeResourceStates, info->Int64ShaderOps);
        return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS9: {
        if (size < sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS9)) {
            Log("CheckFeatureSupport end feature=%s result=0x%08lx reason=short-buffer required=%zu",
                featureName, E_INVALIDARG, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS9));
            return E_INVALIDARG;
        }
        auto* info = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS9*>(data);
        *info = {};
        if (Cfg().diagnosticCapabilities)
            info->AtomicInt64OnTypedResourceSupported = TRUE;
        Log("CheckFeatureSupport end feature=%s result=0x%08lx mesh-stats=%u full-rt-array-index=%u atomic-int64-typed=%u atomic-int64-groupshared=%u mesh-derivatives=%u wave-mma-tier=%u",
            featureName, S_OK, info->MeshShaderPipelineStatsSupported,
            info->MeshShaderSupportsFullRangeRenderTargetArrayIndex,
            info->AtomicInt64OnTypedResourceSupported,
            info->AtomicInt64OnGroupSharedSupported,
            info->DerivativesInMeshAndAmplificationShadersSupported, info->WaveMMATier);
        return S_OK;
    }
    default:
        Log("CheckFeatureSupport end feature=%s(%u) result=0x%08lx reason=unimplemented",
            featureName, static_cast<UINT>(feature), E_INVALIDARG);
        Unimpl("ID3D12Device::CheckFeatureSupport unsupported feature");
        return E_INVALIDARG;
    }
}

HRESULT STDMETHODCALLTYPE Device::CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* desc, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!desc || !desc->NumDescriptors || desc->Type >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES ||
        (desc->Flags & ~D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
        return E_INVALIDARG;
    if (desc->Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        desc->Type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER &&
        (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
        return E_INVALIDARG;
    auto* heap = new (std::nothrow) DescriptorHeap(this, *desc);
    if (!heap) return E_OUTOFMEMORY;
    const HRESULT result = heap->QueryInterface(riid, out);
    heap->Release();
    return result;
}

D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE Device::GetResourceAllocationInfo(
    UINT, UINT count, const D3D12_RESOURCE_DESC* descs) {
    D3D12_RESOURCE_ALLOCATION_INFO info{};
    info.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    if (!descs || !count) {
        info.SizeInBytes = UINT64_MAX;
        return info;
    }
    UINT64 total = 0;
    for (UINT i = 0; i < count; ++i) {
        if (descs[i].Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
            total = AlignUp(total, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
            total += AlignUp(descs[i].Width, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
            continue;
        }
        const auto format = GetFormatInfo(descs[i].Format);
        if (!format.bits) {
            info.SizeInBytes = UINT64_MAX;
            return info;
        }
        total = AlignUp(total, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
        UINT64 width = descs[i].Width;
        UINT height = descs[i].Height;
        for (UINT mip = 0; mip < std::max<UINT>(descs[i].MipLevels, 1); ++mip) {
            const UINT64 blocksWide = (width + format.blockW - 1) / format.blockW;
            const UINT64 blocksHigh = (height + format.blockH - 1) / format.blockH;
            const UINT64 depth = descs[i].Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                ? std::max<UINT>(1, descs[i].DepthOrArraySize >> mip) : descs[i].DepthOrArraySize;
            const UINT64 rowPitch = AlignUp((blocksWide * format.bits + 7) / 8, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
            total += AlignUp(rowPitch * blocksHigh * depth, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            width = std::max<UINT64>(1, width >> 1);
            height = std::max<UINT>(1, height >> 1);
        }
    }
    info.SizeInBytes = AlignUp(total, info.Alignment);
    return info;
}

D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE Device::GetCustomHeapProperties(UINT, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = properties.VisibleNodeMask = 1;
    return properties;
}

HRESULT STDMETHODCALLTYPE Device::CreateCommittedResource(const D3D12_HEAP_PROPERTIES* heapProperties,
    D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initialState,
    const D3D12_CLEAR_VALUE* clearValue, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    Log("CreateCommittedResource request dimension=%u width=%llu height=%u depth-or-array=%u mips=%u format=%u heap=%u state=0x%x clear=%s",
        desc ? desc->Dimension : D3D12_RESOURCE_DIMENSION_UNKNOWN,
        static_cast<unsigned long long>(desc ? desc->Width : 0),
        desc ? desc->Height : 0, desc ? desc->DepthOrArraySize : 0,
        desc ? desc->MipLevels : 0, desc ? desc->Format : DXGI_FORMAT_UNKNOWN,
        heapProperties ? heapProperties->Type : D3D12_HEAP_TYPE_CUSTOM,
        static_cast<UINT>(initialState), clearValue ? "present" : "none");
    Resource* resource = nullptr;
    const HRESULT createResult = Resource::Create(this, heapProperties, heapFlags, desc, &resource);
    if (FAILED(createResult)) {
        Log("CreateCommittedResource failed: dimension=%u format=%u heap=%u hr=0x%08lx",
            desc ? desc->Dimension : D3D12_RESOURCE_DIMENSION_UNKNOWN,
            desc ? desc->Format : DXGI_FORMAT_UNKNOWN,
            heapProperties ? heapProperties->Type : D3D12_HEAP_TYPE_CUSTOM,
            createResult);
        return createResult;
    }
    const HRESULT queryResult = resource->QueryInterface(riid, out);
    resource->Release();
    Log("CreateCommittedResource result=0x%08lx", queryResult);
    return queryResult;
}

HRESULT STDMETHODCALLTYPE Device::CreateReservedResource(const D3D12_RESOURCE_DESC* desc,
    D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, REFIID riid, void** out) {
    D3D12ON11_TRACE_CALLER("Device::CreateReservedResource");
    if (!out) return E_POINTER;
    *out = nullptr;
    Log("CreateReservedResource request dimension=%u width=%llu height=%u depth-or-array=%u mips=%u format=%u layout=%u flags=0x%x state=0x%x clear=%s",
        desc ? desc->Dimension : D3D12_RESOURCE_DIMENSION_UNKNOWN,
        static_cast<unsigned long long>(desc ? desc->Width : 0),
        desc ? desc->Height : 0, desc ? desc->DepthOrArraySize : 0,
        desc ? desc->MipLevels : 0, desc ? desc->Format : DXGI_FORMAT_UNKNOWN,
        desc ? desc->Layout : D3D12_TEXTURE_LAYOUT_UNKNOWN,
        desc ? desc->Flags : D3D12_RESOURCE_FLAG_NONE,
        static_cast<UINT>(initialState), clearValue ? "present" : "none");
    const bool validTexture2D = desc &&
        desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        desc->Layout == D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE &&
        desc->SampleDesc.Count == 1;
    const bool validBuffer = desc &&
        desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
        desc->Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR &&
        desc->SampleDesc.Count == 1 &&
        desc->Height == 1 &&
        desc->DepthOrArraySize == 1 &&
        desc->MipLevels == 1 &&
        desc->Format == DXGI_FORMAT_UNKNOWN;
    if (!validTexture2D && !validBuffer) {
        Log("CreateReservedResource unsupported descriptor (only single-sample 2D 64KB-undefined-swizzle textures and row-major buffers are translated)");
        return E_NOTIMPL;
    }
    if (tiledTier < D3D11_TILED_RESOURCES_TIER_2 || !d11_2 || !ctx2) {
        Log("CreateReservedResource unavailable: D3D11 tiled resources tier=%u device2=%u context2=%u",
            tiledTier, d11_2 ? 1u : 0u, ctx2 ? 1u : 0u);
        return E_NOTIMPL;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    Resource* resource = nullptr;
    if (Cfg().traceCallers) Log("CreateReservedResource backing-create begin");
    const HRESULT createResult = Resource::Create(this, &heap, D3D12_HEAP_FLAG_NONE,
        desc, &resource, true);
    if (Cfg().traceCallers)
        Log("CreateReservedResource backing-create end result=0x%08lx", createResult);
    if (FAILED(createResult)) {
        Log("CreateReservedResource tiled resource creation failed dimension=%u result=0x%08lx",
            desc->Dimension, createResult);
        return createResult;
    }
    const HRESULT queryResult = resource->QueryInterface(riid, out);
    Log("CreateReservedResource tiled resource dimension=%u width=%llu native-width=%llu result=0x%08lx",
        desc->Dimension, static_cast<unsigned long long>(desc->Width),
        static_cast<unsigned long long>(resource->nativeWidth), queryResult);
    resource->Release();
    return queryResult;
}

void STDMETHODCALLTYPE Device::GetResourceTiling(ID3D12Resource* source, UINT* numTiles,
    D3D12_PACKED_MIP_INFO* packedMips, D3D12_TILE_SHAPE* tileShape,
    UINT* numSubresourceTilings, UINT firstSubresourceTiling,
    D3D12_SUBRESOURCE_TILING* subresourceTilings) {
    auto* resource = source ? dynamic_cast<Resource*>(source) : nullptr;
    if (!resource || !resource->tiled || !resource->Raw() || !d11_2) {
        Log("GetResourceTiling ignored invalid or non-tiled resource");
        return;
    }
    UINT requestedTilings = numSubresourceTilings ? *numSubresourceTilings : 0;
    std::vector<D3D11_SUBRESOURCE_TILING> nativeTilings;
    if (subresourceTilings && requestedTilings) {
        try {
            nativeTilings.resize(requestedTilings);
        } catch (const std::bad_alloc&) {
            Log("GetResourceTiling could not allocate %u subresource entries", requestedTilings);
            return;
        }
    }
    D3D11_PACKED_MIP_DESC nativePacked{};
    D3D11_TILE_SHAPE nativeShape{};
    UINT nativeNumTiles = 0;
    UINT nativeCount = requestedTilings;
    d11_2->GetResourceTiling(resource->Raw(), &nativeNumTiles, packedMips ? &nativePacked : nullptr,
        tileShape ? &nativeShape : nullptr, numSubresourceTilings ? &nativeCount : nullptr,
        firstSubresourceTiling, nativeTilings.empty() ? nullptr : nativeTilings.data());
    if (resource->IsBuffer()) {
        const UINT virtualTiles = static_cast<UINT>(
            AlignUp(resource->desc.Width, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) /
            D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
        if (numTiles) *numTiles = virtualTiles;
        if (packedMips) {
            packedMips->NumStandardMips = 1;
            packedMips->NumPackedMips = 0;
            packedMips->NumTilesForPackedMips = 0;
            packedMips->StartTileIndexInOverallResource = 0;
        }
        if (tileShape) {
            tileShape->WidthInTexels = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            tileShape->HeightInTexels = 1;
            tileShape->DepthInTexels = 1;
        }
        const UINT returnedSubresources =
            numSubresourceTilings && firstSubresourceTiling == 0 && requestedTilings > 0 ? 1u : 0u;
        if (numSubresourceTilings) *numSubresourceTilings = returnedSubresources;
        if (subresourceTilings && returnedSubresources > 0) {
            subresourceTilings[0].WidthInTiles = virtualTiles;
            subresourceTilings[0].HeightInTiles = 1;
            subresourceTilings[0].DepthInTiles = 1;
            subresourceTilings[0].StartTileIndexInOverallResource = 0;
        }
        Log("GetResourceTiling buffer virtual-tiles=%u native-tiles=%u requested-subresources=%u returned-subresources=%u",
            virtualTiles, nativeNumTiles, requestedTilings, returnedSubresources);
        return;
    }
    if (numTiles) *numTiles = nativeNumTiles;
    if (packedMips) {
        packedMips->NumStandardMips = nativePacked.NumStandardMips;
        packedMips->NumPackedMips = nativePacked.NumPackedMips;
        packedMips->NumTilesForPackedMips = nativePacked.NumTilesForPackedMips;
        packedMips->StartTileIndexInOverallResource = nativePacked.StartTileIndexInOverallResource;
    }
    if (tileShape) {
        tileShape->WidthInTexels = nativeShape.WidthInTexels;
        tileShape->HeightInTexels = nativeShape.HeightInTexels;
        tileShape->DepthInTexels = nativeShape.DepthInTexels;
    }
    if (numSubresourceTilings) *numSubresourceTilings = nativeCount;
    for (size_t i = 0; i < nativeTilings.size(); ++i) {
        subresourceTilings[i].WidthInTiles = nativeTilings[i].WidthInTiles;
        subresourceTilings[i].HeightInTiles = nativeTilings[i].HeightInTiles;
        subresourceTilings[i].DepthInTiles = nativeTilings[i].DepthInTiles;
        subresourceTilings[i].StartTileIndexInOverallResource =
            nativeTilings[i].StartTileIndexInOverallResource;
    }
    Log("GetResourceTiling tiles=%u packed-mips=%u standard-mips=%u requested-subresources=%u returned-subresources=%u",
        nativeNumTiles, nativePacked.NumPackedMips, nativePacked.NumStandardMips,
        requestedTilings, nativeCount);
}

HRESULT STDMETHODCALLTYPE Device::CreateHeap(const D3D12_HEAP_DESC* desc, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    Log("CreateHeap request size=%llu type=%u flags=0x%x alignment=%llu",
        static_cast<unsigned long long>(desc ? desc->SizeInBytes : 0),
        desc ? desc->Properties.Type : D3D12_HEAP_TYPE_CUSTOM,
        desc ? static_cast<UINT>(desc->Flags) : 0,
        static_cast<unsigned long long>(desc ? desc->Alignment : 0));
    if (!desc || !desc->SizeInBytes || desc->Properties.Type > D3D12_HEAP_TYPE_CUSTOM) return E_INVALIDARG;
    auto* heap = new (std::nothrow) HeapObj(this, *desc);
    if (!heap) return E_OUTOFMEMORY;
    const HRESULT result = heap->QueryInterface(riid, out);
    heap->Release();
    Log("CreateHeap result=0x%08lx", result);
    return result;
}

HRESULT STDMETHODCALLTYPE Device::CreatePlacedResource(ID3D12Heap* sourceHeap, UINT64 offset,
    const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initialState,
    const D3D12_CLEAR_VALUE*, REFIID riid, void** out) {
    TraceApi("Device::CreatePlacedResource");
    if (!out) return E_POINTER;
    *out = nullptr;
    auto* heap = sourceHeap ? dynamic_cast<HeapObj*>(sourceHeap) : nullptr;
    if (!heap || !desc) return E_INVALIDARG;
    Log("CreatePlacedResource request offset=%llu dimension=%u width=%llu sample-count=%u alignment=%llu layout=%u resource-flags=0x%x heap-size=%llu heap-type=%u heap-flags=0x%x state=0x%x",
        static_cast<unsigned long long>(offset), desc->Dimension,
        static_cast<unsigned long long>(desc->Width), desc->SampleDesc.Count,
        static_cast<unsigned long long>(desc->Alignment), desc->Layout,
        static_cast<UINT>(desc->Flags),
        static_cast<unsigned long long>(heap->hd.SizeInBytes),
        heap->hd.Properties.Type, static_cast<UINT>(heap->hd.Flags),
        static_cast<UINT>(initialState));

    constexpr D3D12_HEAP_FLAGS supportedHeapFlags =
        D3D12_HEAP_FLAG_DENY_BUFFERS |
        D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES |
        D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN ||
        desc->Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D ||
        desc->SampleDesc.Count == 0)
        return E_INVALIDARG;
    if (desc->SampleDesc.Count != 1 ||
        desc->Layout == D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE ||
        (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER) ||
        (heap->hd.Flags & ~supportedHeapFlags)) {
        Log("CreatePlacedResource unsupported resource or heap flags");
        return E_NOTIMPL;
    }
    if (heap->hd.Properties.CreationNodeMask != 1 ||
        heap->hd.Properties.VisibleNodeMask != 1) {
        Log("CreatePlacedResource does not support multi-node heaps");
        return E_NOTIMPL;
    }
    const UINT64 heapAlignment = heap->hd.Alignment
        ? heap->hd.Alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    if ((heapAlignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT &&
         heapAlignment != D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT) ||
        heap->hd.SizeInBytes % heapAlignment != 0) {
        Log("CreatePlacedResource heap alignment or size is invalid");
        return E_INVALIDARG;
    }

    const bool isBuffer = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    const bool isRenderTargetOrDepth = (desc->Flags &
        (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0;
    if ((isBuffer && (heap->hd.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS)) ||
        (!isBuffer && isRenderTargetOrDepth &&
            (heap->hd.Flags & D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES)) ||
        (!isBuffer && !isRenderTargetOrDepth &&
            (heap->hd.Flags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES))) {
        Log("CreatePlacedResource rejected by heap resource-class flags");
        return E_INVALIDARG;
    }

    const auto heapType = EffectiveHeapType(heap->hd.Properties);
    if ((heapType == D3D12_HEAP_TYPE_UPLOAD && initialState != D3D12_RESOURCE_STATE_GENERIC_READ) ||
        (heapType == D3D12_HEAP_TYPE_READBACK && initialState != D3D12_RESOURCE_STATE_COPY_DEST)) {
        Log("CreatePlacedResource initial state is incompatible with heap type=%u",
            heap->hd.Properties.Type);
        return E_INVALIDARG;
    }

    const D3D12_RESOURCE_ALLOCATION_INFO allocation = GetResourceAllocationInfo(1, 1, desc);
    if (allocation.SizeInBytes == UINT64_MAX || !allocation.Alignment ||
        (desc->Alignment && desc->Alignment != allocation.Alignment) ||
        offset % allocation.Alignment != 0 ||
        offset > heap->hd.SizeInBytes ||
        allocation.SizeInBytes > heap->hd.SizeInBytes - offset) {
        Log("CreatePlacedResource allocation range invalid alignment=%llu size=%llu",
            static_cast<unsigned long long>(allocation.Alignment),
            static_cast<unsigned long long>(allocation.SizeInBytes));
        return E_INVALIDARG;
    }

    PlacedRangeInfo conflict{};
    const HRESULT reserveResult = heap->ReservePlacedRange(offset, allocation.SizeInBytes, desc, &conflict);
    if (FAILED(reserveResult)) {
        if (conflict.size) {
            if (Cfg().diagnosticRenderTrace) {
                TraceStats().placedOverlap.fetch_add(1, std::memory_order_relaxed);
                Log("[RENDER_TRACE] [tid=%lu] CreatePlacedResource OVERLAP heap=%p "
                    "req=[%llu..%llu) dim=%u size=%llux%ux%u format=%u flags=0x%x "
                    "existing=[%llu..%llu) res=%p tid=%lu dim=%u size=%llux%ux%u "
                    "mips=%u format=%u flags=0x%x result=0x%08lx",
                    GetCurrentThreadId(), heap,
                    static_cast<unsigned long long>(offset),
                    static_cast<unsigned long long>(offset + allocation.SizeInBytes),
                    desc->Dimension, static_cast<unsigned long long>(desc->Width),
                    desc->Height, desc->DepthOrArraySize, desc->Format,
                    static_cast<UINT>(desc->Flags),
                    static_cast<unsigned long long>(conflict.offset),
                    static_cast<unsigned long long>(conflict.offset + conflict.size),
                    conflict.resource, conflict.creatorTid, conflict.dimension,
                    static_cast<unsigned long long>(conflict.width), conflict.height,
                    conflict.depthOrArray, conflict.mipLevels, conflict.format,
                    static_cast<UINT>(conflict.flags), reserveResult);
            } else {
                Log("CreatePlacedResource overlapping placement rejected offset=%llu size=%llu result=0x%08lx",
                    static_cast<unsigned long long>(offset),
                    static_cast<unsigned long long>(allocation.SizeInBytes), reserveResult);
            }
        } else {
            Log("CreatePlacedResource placement reservation failed offset=%llu size=%llu result=0x%08lx",
                static_cast<unsigned long long>(offset),
                static_cast<unsigned long long>(allocation.SizeInBytes), reserveResult);
        }
        return reserveResult;
    }

    Resource* resource = nullptr;
    const HRESULT createResult = Resource::Create(this, &heap->hd.Properties, heap->hd.Flags,
        desc, &resource);
    if (FAILED(createResult)) {
        heap->ReleasePlacedRange(offset);
        Log("CreatePlacedResource backing creation failed result=0x%08lx", createResult);
        return createResult;
    }
    resource->placedHeap = Ptr<ID3D12Heap>(heap);
    resource->placedOffset = offset;
    resource->placedSize = allocation.SizeInBytes;
    heap->BindPlacedResource(offset, resource);
    const HRESULT queryResult = resource->QueryInterface(riid, out);
    resource->Release();
    Log("CreatePlacedResource result=0x%08lx", queryResult);
    return queryResult;
}

HRESULT STDMETHODCALLTYPE Device::CreateFence(UINT64 initialValue, D3D12_FENCE_FLAGS flags, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (flags != D3D12_FENCE_FLAG_NONE) {
        Unimpl("ID3D12Device::CreateFence nonzero flags");
        return E_NOTIMPL;
    }
    auto* fence = new (std::nothrow) Fence(this, initialValue);
    if (!fence) return E_OUTOFMEMORY;
    const HRESULT result = fence->QueryInterface(riid, out);
    fence->Release();
    return result;
}

void STDMETHODCALLTYPE Device::GetCopyableFootprints(const D3D12_RESOURCE_DESC* desc, UINT first, UINT count,
    UINT64 baseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* layouts, UINT* rows,
    UINT64* rowSizes, UINT64* totalBytes) {
    if (totalBytes) *totalBytes = 0;
    if (!desc) return;
    const auto format = GetFormatInfo(desc->Format);
    UINT64 offset = baseOffset;
    for (UINT i = 0; i < count; ++i) {
        const UINT subresource = first + i;
        const UINT mipLevels = std::max<UINT>(desc->MipLevels, 1);
        const UINT mip = subresource % mipLevels;
        const UINT64 width = std::max<UINT64>(1, desc->Width >> mip);
        const UINT height = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER
            ? 1 : std::max<UINT>(1, desc->Height >> mip);
        const UINT rowCount = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER
            ? 1 : static_cast<UINT>((height + format.blockH - 1) / format.blockH);
        const UINT64 rowSize = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER
            ? desc->Width : ((width + format.blockW - 1) / format.blockW * format.bits + 7) / 8;
        const UINT depth = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
            ? std::max<UINT>(1, desc->DepthOrArraySize >> mip) : 1;
        const UINT rowPitch = static_cast<UINT>(AlignUp(rowSize, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
        offset = AlignUp(offset, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        if (layouts) {
            layouts[i] = {};
            layouts[i].Offset = offset;
            layouts[i].Footprint.Format = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER
                ? DXGI_FORMAT_UNKNOWN : desc->Format;
            layouts[i].Footprint.Width = static_cast<UINT>(width);
            layouts[i].Footprint.Height = height;
            layouts[i].Footprint.Depth = depth;
            layouts[i].Footprint.RowPitch = rowPitch;
        }
        if (rows) rows[i] = rowCount;
        if (rowSizes) rowSizes[i] = rowSize;
        offset += static_cast<UINT64>(rowPitch) * rowCount * depth;
    }
    if (totalBytes) *totalBytes = offset - baseOffset;
}

HRESULT STDMETHODCALLTYPE Device::CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* desc, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!desc || !desc->Count || desc->NodeMask > 1 ||
        desc->Type > D3D12_QUERY_HEAP_TYPE_VIDEO_DECODE_STATISTICS)
        return E_INVALIDARG;
    Log("CreateQueryHeap request type=%u count=%u node-mask=0x%x",
        static_cast<unsigned>(desc->Type), desc->Count, desc->NodeMask);
    QueryHeapObj* heap = nullptr;
    try {
        heap = new (std::nothrow) QueryHeapObj(this, *desc);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    if (!heap) return E_OUTOFMEMORY;
    const HRESULT result = heap->QueryInterface(riid, out);
    heap->Release();
    Log("CreateQueryHeap result=0x%08lx", result);
    return result;
}

LUID STDMETHODCALLTYPE Device::GetAdapterLuid() {
    D3D12ON11_TRACE_CALLER("Device::GetAdapterLuid");
    DXGI_ADAPTER_DESC desc{};
    if (adapter && SUCCEEDED(adapter->GetDesc(&desc))) return desc.AdapterLuid;
    LUID luid{};
    return luid;
}

void Fence::CompleteTo(UINT64 value) {
    std::vector<HANDLE> ready;
    {
        std::lock_guard<std::mutex> lock(m);
        if (value > val) val = value;
        auto it = waiters.begin();
        while (it != waiters.end()) {
            if (it->v <= val) {
                ready.push_back(it->ev);
                it = waiters.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (HANDLE eventHandle : ready) SetEvent(eventHandle);
}

UINT64 STDMETHODCALLTYPE Fence::GetCompletedValue() {
    std::lock_guard<std::mutex> lock(m);
    return val;
}

HRESULT STDMETHODCALLTYPE Fence::SetEventOnCompletion(UINT64 value, HANDLE eventHandle) {
    TraceApi("Fence::SetEventOnCompletion");
    if (!eventHandle) return E_INVALIDARG;
    {
        std::lock_guard<std::mutex> lock(m);
        if (val >= value) {
            SetEvent(eventHandle);
            return S_OK;
        }
        waiters.push_back({value, eventHandle});
    }
    return S_OK;
}
