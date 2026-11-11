#include "types.h"

namespace {

enum class TargetHistoryKind {
    ExecutedDraw,
    SkippedDraw,
    Clear
};

struct BackbufferSourceHistory {
    Ptr<ID3D11Resource> resource;
    uint64_t executedDraws = 0;
    uint64_t skippedDraws = 0;
    uint64_t clears = 0;
    std::vector<std::string> entries;
};

std::mutex g_resourceDrawMutex;
std::map<ID3D11Resource*, BackbufferSourceHistory> g_backbufferSourceHistory;
bool g_backbufferSourceHistoryDumped = false;
std::mutex g_targetDrawDiagMutex;
std::map<ID3D11Resource*, UINT> g_targetDrawDiagResourceCounts;
UINT g_targetDrawDiagCount = 0;

struct TargetDrawPipelineStats {
    Ptr<ID3D11Query> query;
    UINT hit = 0;
    UINT resourceHit = 0;
    uintptr_t rtvResource = 0;
    uint64_t vsHash = 0;
    uint64_t psHash = 0;
};

struct PendingTargetDrawPipelineStats {
    Ptr<ID3D11Query> query;
    UINT hit = 0;
    UINT resourceHit = 0;
    uintptr_t rtvResource = 0;
    uint64_t vsHash = 0;
    uint64_t psHash = 0;
};

std::mutex g_pendingTargetDrawStatsMutex;
std::vector<PendingTargetDrawPipelineStats> g_pendingTargetDrawStats;

UINT ClaimTargetDrawDiagSlot(ID3D11Resource* resource, UINT& resourceHit) {
    if (!resource) return 0;
    std::lock_guard<std::mutex> lock(g_targetDrawDiagMutex);
    UINT& resourceCount = g_targetDrawDiagResourceCounts[resource];
    if (resourceCount >= 2 || g_targetDrawDiagCount >= 6) return 0;
    resourceHit = ++resourceCount;
    return ++g_targetDrawDiagCount;
}

void PollTargetDrawPipelineStats(ID3D11DeviceContext* context) {
    if (!context) return;
    std::lock_guard<std::mutex> lock(g_pendingTargetDrawStatsMutex);
    for (auto it = g_pendingTargetDrawStats.begin(); it != g_pendingTargetDrawStats.end();) {
        D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics{};
        const HRESULT result = context->GetData(it->query.Get(), &statistics,
            sizeof(statistics), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (result == S_FALSE) {
            ++it;
            continue;
        }
        if (FAILED(result)) {
            Log("[TARGET_DRAW_STATS] hit=%u resource_hit=%u resource=%p vs=%016llx ps=%016llx "
                "status=failed hr=0x%08lx",
                it->hit, it->resourceHit, reinterpret_cast<void*>(it->rtvResource),
                static_cast<unsigned long long>(it->vsHash),
                static_cast<unsigned long long>(it->psHash), result);
        } else {
            Log("[TARGET_DRAW_STATS] hit=%u resource_hit=%u resource=%p vs=%016llx ps=%016llx "
                "ia_vertices=%llu ia_primitives=%llu vs_invocations=%llu gs_invocations=%llu "
                "gs_primitives=%llu clip_invocations=%llu clip_primitives=%llu "
                "ps_invocations=%llu hs_invocations=%llu ds_invocations=%llu cs_invocations=%llu",
                it->hit, it->resourceHit, reinterpret_cast<void*>(it->rtvResource),
                static_cast<unsigned long long>(it->vsHash),
                static_cast<unsigned long long>(it->psHash),
                static_cast<unsigned long long>(statistics.IAVertices),
                static_cast<unsigned long long>(statistics.IAPrimitives),
                static_cast<unsigned long long>(statistics.VSInvocations),
                static_cast<unsigned long long>(statistics.GSInvocations),
                static_cast<unsigned long long>(statistics.GSPrimitives),
                static_cast<unsigned long long>(statistics.CInvocations),
                static_cast<unsigned long long>(statistics.CPrimitives),
                static_cast<unsigned long long>(statistics.PSInvocations),
                static_cast<unsigned long long>(statistics.HSInvocations),
                static_cast<unsigned long long>(statistics.DSInvocations),
                static_cast<unsigned long long>(statistics.CSInvocations));
        }
        it = g_pendingTargetDrawStats.erase(it);
    }
}

void FinishTargetDrawPipelineStats(ExecState& state, TargetDrawPipelineStats&& pipelineStats) {
    if (!pipelineStats.query || !state.c) return;
    state.c->End(pipelineStats.query.Get());
    PendingTargetDrawPipelineStats pending{};
    pending.query = std::move(pipelineStats.query);
    pending.hit = pipelineStats.hit;
    pending.resourceHit = pipelineStats.resourceHit;
    pending.rtvResource = pipelineStats.rtvResource;
    pending.vsHash = pipelineStats.vsHash;
    pending.psHash = pipelineStats.psHash;
    std::lock_guard<std::mutex> lock(g_pendingTargetDrawStatsMutex);
    g_pendingTargetDrawStats.push_back(std::move(pending));
}

void LogTargetIndexData(ExecState& state, UINT hit, ID3D11Buffer* indexBuffer,
    DXGI_FORMAT indexFormat, UINT indexOffset, UINT indexViewSize, UINT startIndex,
    INT baseVertex, UINT indexCount) {
    if (!state.c || !indexBuffer) {
        Log("[TARGET_INDEX_DATA] hit=%u status=no_context_or_index_buffer", hit);
        return;
    }

    const UINT indexSize = indexFormat == DXGI_FORMAT_R16_UINT ? 2u
        : indexFormat == DXGI_FORMAT_R32_UINT ? 4u : 0u;
    if (!indexSize || !indexCount || indexCount > 6) {
        Log("[TARGET_INDEX_DATA] hit=%u status=unsupported_format_or_count format=%u count=%u",
            hit, static_cast<unsigned int>(indexFormat), indexCount);
        return;
    }

    const UINT64 viewByteOffset = static_cast<UINT64>(startIndex) * indexSize;
    const UINT64 byteCount = static_cast<UINT64>(indexCount) * indexSize;
    if (viewByteOffset > indexViewSize || byteCount > indexViewSize - viewByteOffset) {
        Log("[TARGET_INDEX_DATA] hit=%u status=view_range_invalid format=%u view_bytes=%u "
            "start_index=%u count=%u",
            hit, static_cast<unsigned int>(indexFormat), indexViewSize, startIndex, indexCount);
        return;
    }

    const UINT64 sourceByteOffset = static_cast<UINT64>(indexOffset) + viewByteOffset;
    D3D11_BUFFER_DESC sourceDesc{};
    indexBuffer->GetDesc(&sourceDesc);
    if (sourceByteOffset > sourceDesc.ByteWidth ||
        byteCount > sourceDesc.ByteWidth - sourceByteOffset ||
        sourceByteOffset > UINT_MAX || sourceByteOffset + byteCount > UINT_MAX) {
        Log("[TARGET_INDEX_DATA] hit=%u status=buffer_range_invalid format=%u source_bytes=%u "
            "ib_offset=%u start_index=%u count=%u",
            hit, static_cast<unsigned int>(indexFormat), sourceDesc.ByteWidth,
            indexOffset, startIndex, indexCount);
        return;
    }

    std::array<uint32_t, 6> cpuIndices{};
    std::array<uint32_t, 6> gpuIndices{};
    bool hasCpuShadow = false;
    auto* resource = dynamic_cast<Resource*>(state.indexBufferResource.Get());
    if (resource && resource->heap == D3D12_HEAP_TYPE_UPLOAD) {
        std::lock_guard<std::mutex> lock(resource->mu);
        if (sourceByteOffset <= resource->shadow.size() &&
            byteCount <= resource->shadow.size() - sourceByteOffset) {
            const uint8_t* source = resource->shadow.data() + sourceByteOffset;
            for (UINT i = 0; i < indexCount; ++i) {
                if (indexSize == 2) {
                    uint16_t value = 0;
                    memcpy(&value, source + i * indexSize, sizeof(value));
                    cpuIndices[i] = value;
                } else {
                    memcpy(&cpuIndices[i], source + i * indexSize, sizeof(cpuIndices[i]));
                }
            }
            hasCpuShadow = true;
        }
    }

    D3D11_BUFFER_DESC stagingDesc{};
    stagingDesc.ByteWidth = static_cast<UINT>(byteCount);
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Ptr<ID3D11Buffer> staging;
    const HRESULT createResult = state.dev && state.dev->d11
        ? state.dev->d11->CreateBuffer(&stagingDesc, nullptr, staging.Put())
        : E_POINTER;
    if (FAILED(createResult)) {
        Log("[TARGET_INDEX_DATA] hit=%u status=staging_create_failed hr=0x%08lx",
            hit, createResult);
        return;
    }

    D3D11_BOX sourceBox{};
    sourceBox.left = static_cast<UINT>(sourceByteOffset);
    sourceBox.right = static_cast<UINT>(sourceByteOffset + byteCount);
    sourceBox.top = 0;
    sourceBox.bottom = 1;
    sourceBox.front = 0;
    sourceBox.back = 1;
    state.c->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0,
        indexBuffer, 0, &sourceBox);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = state.c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(mapResult)) {
        Log("[TARGET_INDEX_DATA] hit=%u status=staging_map_failed hr=0x%08lx",
            hit, mapResult);
        return;
    }
    const uint8_t* gpuBytes = static_cast<const uint8_t*>(mapped.pData);
    for (UINT i = 0; i < indexCount; ++i) {
        if (indexSize == 2) {
            uint16_t value = 0;
            memcpy(&value, gpuBytes + i * indexSize, sizeof(value));
            gpuIndices[i] = value;
        } else {
            memcpy(&gpuIndices[i], gpuBytes + i * indexSize, sizeof(gpuIndices[i]));
        }
    }
    state.c->Unmap(staging.Get(), 0);

    std::array<long long, 6> effectiveIndices{};
    for (UINT i = 0; i < indexCount; ++i)
        effectiveIndices[i] = static_cast<long long>(gpuIndices[i]) + baseVertex;
    Log("[TARGET_INDEX_DATA] hit=%u status=ok format=%u source_bytes=%u view_bytes=%u "
        "ib_offset=%u start_index=%u base_vertex=%d count=%u cpu_shadow=%u "
        "cpu=[%u,%u,%u,%u,%u,%u] d3d11=[%u,%u,%u,%u,%u,%u] "
        "effective=[%lld,%lld,%lld,%lld,%lld,%lld]",
        hit, static_cast<unsigned int>(indexFormat), sourceDesc.ByteWidth, indexViewSize,
        indexOffset, startIndex, baseVertex, indexCount, hasCpuShadow ? 1u : 0u,
        cpuIndices[0], cpuIndices[1], cpuIndices[2], cpuIndices[3], cpuIndices[4], cpuIndices[5],
        gpuIndices[0], gpuIndices[1], gpuIndices[2], gpuIndices[3], gpuIndices[4], gpuIndices[5],
        effectiveIndices[0], effectiveIndices[1], effectiveIndices[2],
        effectiveIndices[3], effectiveIndices[4], effectiveIndices[5]);
}

void LogTargetDrawDiagnostic(ExecState& state, UINT indexCount, UINT instanceCount,
    UINT startIndex, INT baseVertex, UINT startInstance,
    TargetDrawPipelineStats& pipelineStats) {
    constexpr uint64_t targetVs = 0x20e0826b236597a1ULL;
    constexpr uint64_t targetPs0 = 0x44b0a992c2304254ULL;
    constexpr uint64_t targetPs1 = 0xd77600bde0076c33ULL;
    if (!Cfg().diagnosticRenderTrace || !state.c ||
        state.graphicsShaderHash != targetVs ||
        (state.graphicsPixelShaderHash != targetPs0 &&
         state.graphicsPixelShaderHash != targetPs1))
        return;

    PollTargetDrawPipelineStats(state.c);

    ID3D11RenderTargetView* rawRtv = nullptr;
    ID3D11DepthStencilView* rawDsv = nullptr;
    state.c->OMGetRenderTargets(1, &rawRtv, &rawDsv);
    Ptr<ID3D11RenderTargetView> rtv = Ptr<ID3D11RenderTargetView>::Attach(rawRtv);
    Ptr<ID3D11DepthStencilView> dsv = Ptr<ID3D11DepthStencilView>::Attach(rawDsv);
    ID3D11Resource* rawRtvResource = nullptr;
    if (rtv) rtv->GetResource(&rawRtvResource);
    Ptr<ID3D11Resource> rtvResource = Ptr<ID3D11Resource>::Attach(rawRtvResource);

    UINT resourceHit = 0;
    const UINT hit = ClaimTargetDrawDiagSlot(rtvResource.Get(), resourceHit);
    if (!hit) return;

    auto logBoundCb = [&](const char* stage) {
        ID3D11Buffer* rawBuffer = nullptr;
        if (strcmp(stage, "VS") == 0)
            state.c->VSGetConstantBuffers(0, 1, &rawBuffer);
        else
            state.c->PSGetConstantBuffers(0, 1, &rawBuffer);
        Ptr<ID3D11Buffer> buffer = Ptr<ID3D11Buffer>::Attach(rawBuffer);
        if (!buffer) {
            Log("[TARGET_CBV] hit=%u stage=%s slot=0 status=unbound", hit, stage);
            return;
        }

        D3D11_BUFFER_DESC sourceDesc{};
        buffer->GetDesc(&sourceDesc);
        if (!state.dev || sourceDesc.ByteWidth < 16) {
            Log("[TARGET_CBV] hit=%u stage=%s slot=0 buffer=%p width=%u status=unreadable",
                hit, stage, buffer.Get(), sourceDesc.ByteWidth);
            return;
        }

        D3D11_BUFFER_DESC stagingDesc = sourceDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        stagingDesc.StructureByteStride = 0;
        Ptr<ID3D11Buffer> staging;
        const HRESULT createResult =
            state.dev->d11->CreateBuffer(&stagingDesc, nullptr, staging.Put());
        if (FAILED(createResult)) {
            Log("[TARGET_CBV] hit=%u stage=%s slot=0 buffer=%p width=%u "
                "status=staging-create-failed hr=0x%08lx",
                hit, stage, buffer.Get(), sourceDesc.ByteWidth, createResult);
            return;
        }

        state.c->CopyResource(staging.Get(), buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT mapResult =
            state.c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(mapResult)) {
            Log("[TARGET_CBV] hit=%u stage=%s slot=0 buffer=%p width=%u "
                "status=staging-map-failed hr=0x%08lx",
                hit, stage, buffer.Get(), sourceDesc.ByteWidth, mapResult);
            return;
        }
        std::array<uint32_t, 4> words{};
        std::array<float, 4> values{};
        memcpy(words.data(), mapped.pData, sizeof(words));
        memcpy(values.data(), mapped.pData, sizeof(values));
        state.c->Unmap(staging.Get(), 0);
        Log("[TARGET_CBV] hit=%u stage=%s slot=0 buffer=%p width=%u status=ok "
            "words=%08x,%08x,%08x,%08x floats=%.9g,%.9g,%.9g,%.9g",
            hit, stage, buffer.Get(), sourceDesc.ByteWidth,
            words[0], words[1], words[2], words[3],
            values[0], values[1], values[2], values[3]);
    };
    logBoundCb("VS");
    logBoundCb("PS");

    ID3D11Buffer* rawIndexBuffer = nullptr;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
    UINT indexOffset = 0;
    state.c->IAGetIndexBuffer(&rawIndexBuffer, &indexFormat, &indexOffset);
    Ptr<ID3D11Buffer> indexBuffer = Ptr<ID3D11Buffer>::Attach(rawIndexBuffer);
    LogTargetIndexData(state, hit, indexBuffer.Get(), indexFormat,
        indexOffset, state.indexBufferViewSize, startIndex, baseVertex, indexCount);

    std::array<ID3D11ShaderResourceView*, 16> rawPsSrvs{};
    state.c->PSGetShaderResources(0, static_cast<UINT>(rawPsSrvs.size()), rawPsSrvs.data());
    UINT psSrvMask = 0;
    for (UINT i = 0; i < rawPsSrvs.size(); ++i) {
        if (!rawPsSrvs[i]) continue;
        if (i < 16) psSrvMask |= 1u << i;
        if (i == 0) continue;
        rawPsSrvs[i]->Release();
        rawPsSrvs[i] = nullptr;
    }
    Ptr<ID3D11ShaderResourceView> psSrv =
        Ptr<ID3D11ShaderResourceView>::Attach(rawPsSrvs[0]);
    D3D11_SHADER_RESOURCE_VIEW_DESC psSrvDesc{};
    if (psSrv) psSrv->GetDesc(&psSrvDesc);
    ID3D11Resource* rawPsResource = nullptr;
    if (psSrv) psSrv->GetResource(&rawPsResource);
    Ptr<ID3D11Resource> psResource = Ptr<ID3D11Resource>::Attach(rawPsResource);
    D3D11_RESOURCE_DIMENSION psResourceDimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    if (psResource) psResource->GetType(&psResourceDimension);
    UINT psTextureWidth = 0;
    UINT psTextureHeight = 0;
    DXGI_FORMAT psTextureFormat = DXGI_FORMAT_UNKNOWN;
    if (psResourceDimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        Ptr<ID3D11Texture2D> psTexture;
        if (SUCCEEDED(psResource->QueryInterface(__uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(psTexture.Put())))) {
            D3D11_TEXTURE2D_DESC psTextureDesc{};
            psTexture->GetDesc(&psTextureDesc);
            psTextureWidth = psTextureDesc.Width;
            psTextureHeight = psTextureDesc.Height;
            psTextureFormat = psTextureDesc.Format;
        }
    }
    ID3D11SamplerState* rawPsSampler = nullptr;
    state.c->PSGetSamplers(0, 1, &rawPsSampler);
    Ptr<ID3D11SamplerState> psSampler = Ptr<ID3D11SamplerState>::Attach(rawPsSampler);
    D3D11_SAMPLER_DESC psSamplerDesc{};
    if (psSampler) psSampler->GetDesc(&psSamplerDesc);
    Log("[TARGET_PS_BINDINGS] hit=%u srv_mask=0x%04x srv0=%p resource0=%p "
        "srv_dimension=%u srv_format=%u "
        "resource_dimension=%u texture_size=%ux%u texture_format=%u sampler0=%p "
        "sampler_filter=%u address=%u/%u/%u",
        hit, psSrvMask, psSrv.Get(), psResource.Get(),
        static_cast<unsigned int>(psSrvDesc.ViewDimension),
        static_cast<unsigned int>(psSrvDesc.Format),
        static_cast<unsigned int>(psResourceDimension), psTextureWidth, psTextureHeight,
        static_cast<unsigned int>(psTextureFormat), psSampler.Get(),
        static_cast<unsigned int>(psSamplerDesc.Filter),
        static_cast<unsigned int>(psSamplerDesc.AddressU),
        static_cast<unsigned int>(psSamplerDesc.AddressV),
        static_cast<unsigned int>(psSamplerDesc.AddressW));

    std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> rawVertexBuffers{};
    std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexStrides{};
    std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexOffsets{};
    state.c->IAGetVertexBuffers(0, static_cast<UINT>(rawVertexBuffers.size()),
        rawVertexBuffers.data(), vertexStrides.data(), vertexOffsets.data());
    UINT vertexBufferMask = 0;
    UINT vertexBufferCount = 0;
    for (UINT slot = 0; slot < rawVertexBuffers.size(); ++slot) {
        if (!rawVertexBuffers[slot]) continue;
        vertexBufferMask |= 1u << slot;
        ++vertexBufferCount;
        rawVertexBuffers[slot]->Release();
    }
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    state.c->IAGetPrimitiveTopology(&topology);

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    if (rtv) rtv->GetDesc(&rtvDesc);

    Ptr<ID3D11Resource> copySource;
    uint64_t copySequence = 0;
    if (state.dev) {
        std::lock_guard<std::mutex> lock(state.dev->backbufferCopyLock);
        copySource = state.dev->lastBackbufferCopySource;
        copySequence = state.dev->lastBackbufferCopySequence;
    }

    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    UINT viewportCount = static_cast<UINT>(viewports.size());
    state.c->RSGetViewports(&viewportCount, viewports.data());
    std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
    UINT scissorCount = static_cast<UINT>(scissors.size());
    state.c->RSGetScissorRects(&scissorCount, scissors.data());

    ID3D11RasterizerState* rawRasterizer = nullptr;
    state.c->RSGetState(&rawRasterizer);
    Ptr<ID3D11RasterizerState> rasterizer = Ptr<ID3D11RasterizerState>::Attach(rawRasterizer);
    D3D11_RASTERIZER_DESC rasterizerDesc{};
    if (rasterizer) rasterizer->GetDesc(&rasterizerDesc);

    ID3D11DepthStencilState* rawDepthState = nullptr;
    UINT stencilRef = 0;
    state.c->OMGetDepthStencilState(&rawDepthState, &stencilRef);
    Ptr<ID3D11DepthStencilState> depthState = Ptr<ID3D11DepthStencilState>::Attach(rawDepthState);
    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    if (depthState) depthState->GetDesc(&depthDesc);

    ID3D11BlendState* rawBlendState = nullptr;
    FLOAT blendFactor[4]{};
    UINT sampleMask = 0;
    state.c->OMGetBlendState(&rawBlendState, blendFactor, &sampleMask);
    Ptr<ID3D11BlendState> blendState = Ptr<ID3D11BlendState>::Attach(rawBlendState);
    D3D11_BLEND_DESC blendDesc{};
    if (blendState) blendState->GetDesc(&blendDesc);

    const UINT depthEnable = depthState
        ? static_cast<UINT>(depthDesc.DepthEnable) : TRUE;
    const UINT depthFunc = depthState
        ? static_cast<UINT>(depthDesc.DepthFunc)
        : static_cast<UINT>(D3D11_COMPARISON_LESS);
    const UINT depthWrite = depthState
        ? static_cast<UINT>(depthDesc.DepthWriteMask)
        : static_cast<UINT>(D3D11_DEPTH_WRITE_MASK_ALL);
    const UINT stencilEnable =
        depthState && depthDesc.StencilEnable ? 1u : 0u;

    Log("[TARGET_DRAW_DIAG] hit=%u resource_hit=%u vs=%016llx ps=%016llx indices=%u instances=%u "
        "start_index=%u base_vertex=%d start_instance=%u ib=%p ib_format=%u ib_offset=%u "
        "vb_mask=0x%08x vb_count=%u vb0_stride=%u vb0_offset=%u topology=%u",
        hit, resourceHit, static_cast<unsigned long long>(state.graphicsShaderHash),
        static_cast<unsigned long long>(state.graphicsPixelShaderHash),
        indexCount, instanceCount, startIndex, baseVertex, startInstance,
        indexBuffer.Get(), static_cast<unsigned int>(indexFormat), indexOffset,
        vertexBufferMask, vertexBufferCount, vertexStrides[0], vertexOffsets[0],
        static_cast<unsigned int>(topology));
    Log("[TARGET_DRAW_DIAG] hit=%u rtv=%p rtv_format=%u rtv_resource=%p tracked_resource=%p "
        "previous_copy_source=%p previous_copy_sequence=%llu matches_previous_copy_source=%u dsv=%p",
        hit, rtv.Get(), static_cast<unsigned int>(rtvDesc.Format), rtvResource.Get(),
        state.numRtv ? state.rtvResources[0].Get() : nullptr, copySource.Get(),
        static_cast<unsigned long long>(copySequence),
        rtvResource && rtvResource.Get() == copySource.Get() ? 1u : 0u, dsv.Get());
    Log("[TARGET_DRAW_DIAG] hit=%u viewports=%u first_viewport=(%.1f,%.1f,%.1f,%.1f) "
        "scissors=%u first_scissor=(%ld,%ld,%ld,%ld) raster_bound=%u cull=%u scissor_enable=%u "
        "depth_bound=%u depth_enable=%u depth_func=%u stencil_enable=%u depth_write=%u "
        "blend_bound=%u blend_enable=%u write_mask=0x%x blend_factor=(%.6g,%.6g,%.6g,%.6g) "
        "sample_mask=0x%08x stencil_ref=%u",
        hit, viewportCount,
        viewportCount ? viewports[0].TopLeftX : 0.0f,
        viewportCount ? viewports[0].TopLeftY : 0.0f,
        viewportCount ? viewports[0].Width : 0.0f,
        viewportCount ? viewports[0].Height : 0.0f,
        scissorCount,
        scissorCount ? scissors[0].left : 0,
        scissorCount ? scissors[0].top : 0,
        scissorCount ? scissors[0].right : 0,
        scissorCount ? scissors[0].bottom : 0,
        rasterizer ? 1u : 0u,
        rasterizer ? static_cast<unsigned int>(rasterizerDesc.CullMode)
                   : static_cast<unsigned int>(D3D11_CULL_BACK),
        rasterizer ? static_cast<unsigned int>(rasterizerDesc.ScissorEnable) : 0u,
        depthState ? 1u : 0u,
        depthEnable, depthFunc, stencilEnable, depthWrite,
        blendState ? 1u : 0u,
        blendState ? static_cast<unsigned int>(blendDesc.RenderTarget[0].BlendEnable) : 0u,
        blendState ? static_cast<unsigned int>(blendDesc.RenderTarget[0].RenderTargetWriteMask)
                   : static_cast<unsigned int>(D3D11_COLOR_WRITE_ENABLE_ALL),
        blendFactor[0], blendFactor[1], blendFactor[2], blendFactor[3], sampleMask, stencilRef);

    D3D11_QUERY_DESC queryDesc{D3D11_QUERY_PIPELINE_STATISTICS, 0};
    const HRESULT queryResult = state.dev && state.dev->d11
        ? state.dev->d11->CreateQuery(&queryDesc, pipelineStats.query.Put())
        : E_POINTER;
    if (FAILED(queryResult)) {
        Log("[TARGET_DRAW_STATS] hit=%u resource_hit=%u resource=%p create_query_failed hr=0x%08lx",
            hit, resourceHit, rtvResource.Get(), queryResult);
        pipelineStats.query.Reset();
        return;
    }
    pipelineStats.hit = hit;
    pipelineStats.resourceHit = resourceHit;
    pipelineStats.rtvResource = reinterpret_cast<uintptr_t>(rtvResource.Get());
    pipelineStats.vsHash = state.graphicsShaderHash;
    pipelineStats.psHash = state.graphicsPixelShaderHash;
    state.c->Begin(pipelineStats.query.Get());
}

void TrackTargetOperation(ID3D11Resource* resource, const std::string& context,
    TargetHistoryKind kind) {
    if (!resource || !Cfg().diagnosticRenderTrace) return;
    std::lock_guard<std::mutex> lock(g_resourceDrawMutex);
    if (g_backbufferSourceHistoryDumped) return;
    auto it = g_backbufferSourceHistory.find(resource);
    if (it == g_backbufferSourceHistory.end()) return;
    auto& history = it->second;
    switch (kind) {
    case TargetHistoryKind::ExecutedDraw:
        ++history.executedDraws;
        break;
    case TargetHistoryKind::SkippedDraw:
        ++history.skippedDraws;
        break;
    case TargetHistoryKind::Clear:
        ++history.clears;
        break;
    }
    if (history.entries.size() < 20)
        history.entries.push_back(context);
}

void RegisterBackbufferCopySource(ID3D11Resource* resource) {
    if (!resource || !Cfg().diagnosticRenderTrace) return;
    std::lock_guard<std::mutex> lock(g_resourceDrawMutex);
    if (g_backbufferSourceHistoryDumped) return;
    auto& history = g_backbufferSourceHistory[resource];
    if (!history.resource) history.resource = Ptr<ID3D11Resource>(resource);
}

Resource* AsResource(ID3D12Resource* resource) {
    return resource ? dynamic_cast<Resource*>(resource) : nullptr;
}

Fence* AsFence(ID3D12Fence* fence) {
    return fence ? dynamic_cast<Fence*>(fence) : nullptr;
}

HRESULT WaitForContext(Device* device) {
    TraceApi("WaitForContext");
    const bool trace = Cfg().diagnosticRenderTrace;
    const ULONGLONG start = trace ? GetTickCount64() : 0;
    UINT64 spins = 0;
    if (trace) TraceStats().waitCtxCalls.fetch_add(1, std::memory_order_relaxed);
    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
    Ptr<ID3D11Query> query;
    HRESULT result = device->d11->CreateQuery(&desc, query.Put());
    if (FAILED(result)) {
        if (trace) {
            TraceStats().waitCtxFail.fetch_add(1, std::memory_order_relaxed);
            Log("[RENDER_TRACE] [tid=%lu] WaitForContext CreateQuery failed hr=0x%08lx",
                GetCurrentThreadId(), result);
        }
        return result;
    }
    device->ctx->End(query.Get());
    device->ctx->Flush();
    while ((result = device->ctx->GetData(query.Get(), nullptr, 0, 0)) == S_FALSE) {
        ++spins;
        SwitchToThread();
    }
    if (trace) {
        auto& stats = TraceStats();
        UINT64 observed = stats.waitCtxMaxSpins.load(std::memory_order_relaxed);
        while (observed < spins &&
            !stats.waitCtxMaxSpins.compare_exchange_weak(observed, spins, std::memory_order_relaxed)) {}
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (FAILED(result)) stats.waitCtxFail.fetch_add(1, std::memory_order_relaxed);
        if (elapsed > 200 || FAILED(result))
            Log("[RENDER_TRACE] [tid=%lu] WaitForContext %s elapsed=%llums spins=%llu hr=0x%08lx",
                GetCurrentThreadId(), FAILED(result) ? "failed" : "slow",
                static_cast<unsigned long long>(elapsed),
                static_cast<unsigned long long>(spins), result);
    }
    return result;
}

void RecordDrawExecution(ExecState& state, bool indexed, const char* operation,
    UINT elementCount, UINT instanceCount, bool indirect = false) {
    if (!Cfg().diagnosticRenderTrace) return;
    auto& stats = TraceStats();
    const uint64_t drawIndex = stats.drawExec.fetch_add(1, std::memory_order_relaxed) + 1;
    ++state.drawsExecuted;
    bool hasRtv = false;
    for (UINT i = 0; i < state.numRtv && i < ARRAYSIZE(state.rtvs); ++i)
        hasRtv = hasRtv || static_cast<bool>(state.rtvs[i]);
    char viewport[128] = "unset";
    if (state.numViewports) {
        const D3D12_VIEWPORT& first = state.viewports[0];
        snprintf(viewport, sizeof(viewport), "(%.1f,%.1f,%.1f,%.1f)",
            first.TopLeftX, first.TopLeftY, first.Width, first.Height);
    }
    char elements[32]{};
    char instances[32]{};
    if (indirect) {
        snprintf(elements, sizeof(elements), "indirect");
        snprintf(instances, sizeof(instances), "indirect");
    } else {
        snprintf(elements, sizeof(elements), "%u", elementCount);
        snprintf(instances, sizeof(instances), "%u", instanceCount);
    }
    for (UINT i = 0; i < state.numRtv && i < ARRAYSIZE(state.rtvs); ++i) {
        ID3D11Resource* resource = state.rtvResources[i].Get();
        if (!resource) continue;
        char context[512]{};
        snprintf(context, sizeof(context),
            "Draw #%llu api=%s indexed=%u rtv_slot=%u pso=%p vs_hash=%016llx "
            "ps_hash=%016llx elements=%s instances=%s viewport=%s",
            static_cast<unsigned long long>(drawIndex), operation,
            indexed ? 1u : 0u, i,
            reinterpret_cast<void*>(state.graphicsPipelineIdentity),
            static_cast<unsigned long long>(state.graphicsShaderHash),
            static_cast<unsigned long long>(state.graphicsPixelShaderHash),
            elements, instances, viewport);
        TrackTargetOperation(resource, context, TargetHistoryKind::ExecutedDraw);
    }
    if (hasRtv) {
        stats.drawWithRtv.fetch_add(1, std::memory_order_relaxed);
        ++state.rtvBound;
        if (state.rtvBackbuffer) {
            stats.drawWithBackbuffer.fetch_add(1, std::memory_order_relaxed);
            ++state.backbufferDraws;
            Log("[BACKBUFFER] Draw to backbuffer detected! api=%s vs-hash=%016llx ps-hash=%016llx",
                indexed ? "DrawIndexed" : "Draw",
                static_cast<unsigned long long>(state.graphicsShaderHash),
                static_cast<unsigned long long>(state.graphicsPixelShaderHash));
        }
    } else {
        stats.drawNullRtv.fetch_add(1, std::memory_order_relaxed);
    }
    if (!state.hasVertexBuffer) stats.drawMissingVb.fetch_add(1, std::memory_order_relaxed);
    if (indexed && !state.hasIndexBuffer) stats.drawMissingIb.fetch_add(1, std::memory_order_relaxed);
}

void LogBackbufferCopy(const char* operation, const Resource* destination,
    const Resource* source) {
    if (!Cfg().diagnosticRenderTrace || !destination || !destination->backbuffer) return;
    Log("[BACKBUFFER] Copy to backbuffer detected! api=%s dst=%p src=%p",
        operation, destination, source);
}

void LogSkippedDraw(ExecState& state, const char* operation,
    UINT arg0, UINT arg1) {
    if (!Cfg().diagnosticRenderTrace) return;
    ++state.drawsSkipped;
    for (UINT i = 0; i < state.numRtv && i < ARRAYSIZE(state.rtvs); ++i) {
        ID3D11Resource* resource = state.rtvResources[i].Get();
        if (!resource) continue;
        char context[512]{};
        snprintf(context, sizeof(context),
            "Skipped %s rtv_slot=%u pso=%p vs_hash=%016llx ps_hash=%016llx "
            "missing_vs=%u missing_shader=%u arg0=%u arg1=%u",
            operation, i, reinterpret_cast<void*>(state.graphicsPipelineIdentity),
            static_cast<unsigned long long>(state.graphicsShaderHash),
            static_cast<unsigned long long>(state.graphicsPixelShaderHash),
            state.graphicsPlaceholderVs ? 1u : 0u, state.skipDraws ? 1u : 0u,
            arg0, arg1);
        TrackTargetOperation(resource, context, TargetHistoryKind::SkippedDraw);
    }
    Log("[RENDER_TRACE] [DRAW_SKIPPED] api=%s pipeline=%p vs-hash=%016llx ps-hash=%016llx "
        "missing-vs=%u missing-shader=%u arg0=%u arg1=%u rtvs=%u backbuffer=%u",
        operation, reinterpret_cast<void*>(state.graphicsPipelineIdentity),
        static_cast<unsigned long long>(state.graphicsShaderHash),
        static_cast<unsigned long long>(state.graphicsPixelShaderHash),
        state.graphicsPlaceholderVs ? 1u : 0u, state.skipDraws ? 1u : 0u,
        arg0, arg1, state.numRtv, state.rtvBackbuffer ? 1u : 0u);
}

void DumpBackbufferSourceHistoryImpl() {
    struct Snapshot {
        ID3D11Resource* resource = nullptr;
        uint64_t executedDraws = 0;
        uint64_t skippedDraws = 0;
        uint64_t clears = 0;
        std::vector<std::string> entries;
    };
    std::vector<Snapshot> snapshots;
    {
        std::lock_guard<std::mutex> lock(g_resourceDrawMutex);
        g_backbufferSourceHistoryDumped = true;
        snapshots.reserve(g_backbufferSourceHistory.size());
        for (const auto& pair : g_backbufferSourceHistory) {
            Snapshot snapshot{};
            snapshot.resource = pair.first;
            snapshot.executedDraws = pair.second.executedDraws;
            snapshot.skippedDraws = pair.second.skippedDraws;
            snapshot.clears = pair.second.clears;
            snapshot.entries = pair.second.entries;
            snapshots.push_back(std::move(snapshot));
        }
        g_backbufferSourceHistory.clear();
    }

    if (snapshots.empty()) {
        Log("[BB_SOURCE_HISTORY] No CopyResource-to-backbuffer sources registered");
        return;
    }
    for (const auto& snapshot : snapshots) {
        const size_t total = static_cast<size_t>(snapshot.executedDraws +
            snapshot.skippedDraws + snapshot.clears);
        if (!total) {
            Log("[BB_SOURCE_HISTORY] Resource %p has NO draw/clear history",
                snapshot.resource);
            continue;
        }
        Log("[BB_SOURCE_HISTORY] Resource %p operations=%llu draws=%llu skipped=%llu clears=%llu",
            snapshot.resource, static_cast<unsigned long long>(total),
            static_cast<unsigned long long>(snapshot.executedDraws),
            static_cast<unsigned long long>(snapshot.skippedDraws),
            static_cast<unsigned long long>(snapshot.clears));
        for (const auto& entry : snapshot.entries)
            Log("[BB_SOURCE_HISTORY]   %s", entry.c_str());
        if (total > snapshot.entries.size())
            Log("[BB_SOURCE_HISTORY]   ... and %llu more operations",
                static_cast<unsigned long long>(total - snapshot.entries.size()));
    }
}

void RecordDispatchExecution() {
    if (Cfg().diagnosticRenderTrace)
        TraceStats().dispatchExec.fetch_add(1, std::memory_order_relaxed);
}

HRESULT WaitForQuery(Device* device, ID3D11Query* query, void* data, UINT dataSize) {
    const ULONGLONG deadline = GetTickCount64() + 5000;
    HRESULT result = S_FALSE;
    while ((result = device->ctx->GetData(query, data, dataSize,
            D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
        if (GetTickCount64() >= deadline)
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        SwitchToThread();
    }
    return result;
}

HRESULT GetDeviceTimestampFrequency(Device* device, UINT64* frequency) {
    if (device->timestampFrequency) {
        *frequency = device->timestampFrequency;
        return S_OK;
    }

    D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    Ptr<ID3D11Query> query;
    HRESULT result = device->d11->CreateQuery(&desc, query.Put());
    if (FAILED(result)) {
        Log("GetTimestampFrequency could not create D3D11 disjoint query: hr=0x%08lx", result);
        return result;
    }

    device->ctx->Begin(query.Get());
    device->ctx->End(query.Get());
    device->ctx->Flush();

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
    result = WaitForQuery(device, query.Get(), &data, sizeof(data));
    if (FAILED(result)) {
        Log("GetTimestampFrequency D3D11 disjoint query failed: hr=0x%08lx", result);
        return result;
    }
    if (data.Disjoint || !data.Frequency) {
        Log("GetTimestampFrequency received disjoint/zero-frequency timestamp data");
        return E_FAIL;
    }

    device->timestampFrequency = data.Frequency;
    *frequency = data.Frequency;
    return S_OK;
}

Descriptor DescriptorAt(D3D12_CPU_DESCRIPTOR_HANDLE handle, DKind expected) {
    UINT index = 0;
    DescriptorHeap* heap = FindHeapByCpu(handle.ptr, &index);
    if (!heap || heap->items[index].kind != expected) return {};
    return heap->items[index];
}

Descriptor DescriptorAt(D3D12_GPU_DESCRIPTOR_HANDLE handle, DKind expected) {
    UINT index = 0;
    DescriptorHeap* heap = FindHeapByGpu(handle.ptr, &index);
    if (!heap || heap->items[index].kind != expected) return {};
    return heap->items[index];
}

struct FootprintCopyInfo {
    UINT rows = 0;
    UINT blocksPerRow = 0;
    UINT64 rowBytes = 0;
    UINT64 sliceBytes = 0;
    UINT bytesPerBlock = 0;
};

bool ValidateFootprint(Resource* buffer, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& placed,
                       FootprintCopyInfo& info) {
    const auto& footprint = placed.Footprint;
    const FormatInfo format = GetFormatInfo(footprint.Format);
    if (!buffer || !buffer->IsBuffer() || !footprint.Width || !footprint.Height ||
        !footprint.Depth || !format.bits || format.bits % 8 ||
        !format.blockW || !format.blockH || !footprint.RowPitch ||
        footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT ||
        placed.Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT)
        return false;
    info.bytesPerBlock = format.bits / 8;
    info.blocksPerRow = (footprint.Width + format.blockW - 1) / format.blockW;
    info.rows = (footprint.Height + format.blockH - 1) / format.blockH;
    info.rowBytes = static_cast<UINT64>(info.blocksPerRow) * info.bytesPerBlock;
    if (footprint.RowPitch < info.rowBytes) return false;
    info.sliceBytes = static_cast<UINT64>(footprint.RowPitch) * info.rows;
    const UINT64 lastSlice = static_cast<UINT64>(footprint.Depth - 1) * info.sliceBytes;
    const UINT64 lastRow = static_cast<UINT64>(info.rows - 1) * footprint.RowPitch;
    if (placed.Offset > buffer->desc.Width || lastSlice > buffer->desc.Width - placed.Offset ||
        lastRow > buffer->desc.Width - placed.Offset - lastSlice ||
        info.rowBytes > buffer->desc.Width - placed.Offset - lastSlice - lastRow)
        return false;
    return true;
}

HRESULT CreateStagingTexture(Device* device, D3D12_RESOURCE_DIMENSION dimension,
    const D3D12_SUBRESOURCE_FOOTPRINT& footprint, UINT cpuAccess, Ptr<ID3D11Resource>& out) {
    auto logFailure = [&](const char* api, HRESULT result) {
        if (Cfg().diagnosticRenderTrace) {
            Log("[RENDER_TRACE] [STAGING_CREATE_FAIL] api=%s dimension=%u format=%u "
                "width=%u height=%u depth=%u row-pitch=%u cpu-access=0x%x hr=0x%08lx",
                api, static_cast<UINT>(dimension), static_cast<UINT>(footprint.Format),
                footprint.Width, footprint.Height, footprint.Depth, footprint.RowPitch,
                cpuAccess, result);
        }
        return result;
    };
    if (dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
        D3D11_TEXTURE1D_DESC desc{};
        desc.Width = footprint.Width;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = footprint.Format;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = cpuAccess;
        Ptr<ID3D11Texture1D> texture;
        const HRESULT result = device->d11->CreateTexture1D(&desc, nullptr, texture.Put());
        if (FAILED(result)) return logFailure("CreateTexture1D", result);
        if (SUCCEEDED(result)) {
            ID3D11Resource* base = texture.Get();
            base->AddRef();
            out = Ptr<ID3D11Resource>::Attach(base);
        }
        return result;
    }
    if (dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        if (footprint.Depth != 1) return logFailure("validation-depth-2D", E_INVALIDARG);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = footprint.Width;
        desc.Height = footprint.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = footprint.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = cpuAccess;
        Ptr<ID3D11Texture2D> texture;
        const HRESULT result = device->d11->CreateTexture2D(&desc, nullptr, texture.Put());
        if (FAILED(result)) return logFailure("CreateTexture2D", result);
        if (SUCCEEDED(result)) {
            ID3D11Resource* base = texture.Get();
            base->AddRef();
            out = Ptr<ID3D11Resource>::Attach(base);
        }
        return result;
    }
    if (dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        D3D11_TEXTURE3D_DESC desc{};
        desc.Width = footprint.Width;
        desc.Height = footprint.Height;
        desc.Depth = footprint.Depth;
        desc.MipLevels = 1;
        desc.Format = footprint.Format;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = cpuAccess;
        Ptr<ID3D11Texture3D> texture;
        const HRESULT result = device->d11->CreateTexture3D(&desc, nullptr, texture.Put());
        if (FAILED(result)) return logFailure("CreateTexture3D", result);
        if (SUCCEEDED(result)) {
            ID3D11Resource* base = texture.Get();
            base->AddRef();
            out = Ptr<ID3D11Resource>::Attach(base);
        }
        return result;
    }
    return logFailure("validation-dimension", E_INVALIDARG);
}

bool NeedsBcMipStaging(const D3D12_SUBRESOURCE_FOOTPRINT& footprint) {
    const FormatInfo format = GetFormatInfo(footprint.Format);
    return format.blockW == 4 && format.blockH == 4 &&
        (footprint.Width < format.blockW || footprint.Height < format.blockH);
}

HRESULT CreateBcMipStagingTexture(Device* device,
    const D3D12_SUBRESOURCE_FOOTPRINT& footprint, UINT cpuAccess,
    Ptr<ID3D11Resource>& out, UINT& stagingSubresource) {
    UINT mip = 0;
    while ((static_cast<UINT64>(footprint.Width) << mip) < 4 ||
           (static_cast<UINT64>(footprint.Height) << mip) < 4)
        ++mip;

    const UINT64 width = static_cast<UINT64>(footprint.Width) << mip;
    const UINT64 height = static_cast<UINT64>(footprint.Height) << mip;
    if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = mip + 1;
    desc.ArraySize = 1;
    desc.Format = footprint.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = cpuAccess;

    Ptr<ID3D11Texture2D> texture;
    const HRESULT result = device->d11->CreateTexture2D(&desc, nullptr, texture.Put());
    if (FAILED(result)) return result;
    ID3D11Resource* base = texture.Get();
    base->AddRef();
    out = Ptr<ID3D11Resource>::Attach(base);
    stagingSubresource = mip;
    return S_OK;
}

HRESULT MapBufferForRead(ExecState& state, Resource* resource, Ptr<ID3D11Buffer>& staging,
                         D3D11_MAPPED_SUBRESOURCE& mapped, const uint8_t*& data) {
    if (!resource || !resource->IsBuffer() || !resource->buf) return E_INVALIDARG;
    if (resource->heap == D3D12_HEAP_TYPE_UPLOAD && !resource->shadow.empty()) {
        data = resource->shadow.data();
        return S_OK;
    }
    D3D11_BUFFER_DESC desc{};
    resource->buf->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    HRESULT result = state.dev->d11->CreateBuffer(&desc, nullptr, staging.Put());
    if (FAILED(result)) return result;
    state.c->CopyResource(staging.Get(), resource->buf.Get());
    result = state.c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(result)) data = static_cast<const uint8_t*>(mapped.pData);
    return result;
}

bool ValidCopyBox(const D3D12_BOX& box, const D3D12_SUBRESOURCE_FOOTPRINT& footprint) {
    return box.left < box.right && box.top < box.bottom && box.front < box.back &&
        box.right <= footprint.Width && box.bottom <= footprint.Height && box.back <= footprint.Depth;
}

UINT VisibilityStages(D3D12_SHADER_VISIBILITY visibility, bool compute) {
    if (compute) return S_CS;
    switch (visibility) {
    case D3D12_SHADER_VISIBILITY_VERTEX: return S_VS;
    case D3D12_SHADER_VISIBILITY_HULL: return S_HS;
    case D3D12_SHADER_VISIBILITY_DOMAIN: return S_DS;
    case D3D12_SHADER_VISIBILITY_GEOMETRY: return S_GS;
    case D3D12_SHADER_VISIBILITY_PIXEL: return S_PS;
    default: return S_VS | S_HS | S_DS | S_GS | S_PS;
    }
}

void BindCb(ExecState& state, UINT stages, UINT slot, ID3D11Buffer* buffer) {
    if (slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) return;
    ID3D11Buffer* buffers[] = {buffer};
    if (stages & S_VS) state.c->VSSetConstantBuffers(slot, 1, buffers);
    if (stages & S_HS) state.c->HSSetConstantBuffers(slot, 1, buffers);
    if (stages & S_DS) state.c->DSSetConstantBuffers(slot, 1, buffers);
    if (stages & S_GS) state.c->GSSetConstantBuffers(slot, 1, buffers);
    if (stages & S_PS) state.c->PSSetConstantBuffers(slot, 1, buffers);
    if (stages & S_CS) state.c->CSSetConstantBuffers(slot, 1, buffers);
}

void BindSrv(ExecState& state, UINT stages, UINT slot, ID3D11ShaderResourceView* view) {
    if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT) return;
    ID3D11ShaderResourceView* views[] = {view};
    if (stages & S_VS) state.c->VSSetShaderResources(slot, 1, views);
    if (stages & S_HS) state.c->HSSetShaderResources(slot, 1, views);
    if (stages & S_DS) state.c->DSSetShaderResources(slot, 1, views);
    if (stages & S_GS) state.c->GSSetShaderResources(slot, 1, views);
    if (stages & S_PS) state.c->PSSetShaderResources(slot, 1, views);
    if (stages & S_CS) state.c->CSSetShaderResources(slot, 1, views);
}

void BindSampler(ExecState& state, UINT stages, UINT slot, ID3D11SamplerState* sampler) {
    if (slot >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) return;
    ID3D11SamplerState* samplers[] = {sampler};
    if (stages & S_VS) state.c->VSSetSamplers(slot, 1, samplers);
    if (stages & S_HS) state.c->HSSetSamplers(slot, 1, samplers);
    if (stages & S_DS) state.c->DSSetSamplers(slot, 1, samplers);
    if (stages & S_GS) state.c->GSSetSamplers(slot, 1, samplers);
    if (stages & S_PS) state.c->PSSetSamplers(slot, 1, samplers);
    if (stages & S_CS) state.c->CSSetSamplers(slot, 1, samplers);
}

void BindUav(ExecState& state, UINT stages, UINT slot, ID3D11UnorderedAccessView* view) {
    if (!view) return;
    if (stages & S_CS) {
        if (slot < D3D11_1_UAV_SLOT_COUNT) state.c->CSSetUnorderedAccessViews(slot, 1, &view, nullptr);
    }
    if (stages & S_PS) {
        if (slot < D3D11_1_UAV_SLOT_COUNT && slot >= state.numRtv)
            state.c->OMSetRenderTargetsAndUnorderedAccessViews(
                D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr,
                slot, 1, &view, nullptr);
        else
            Log("Pixel UAV slot %u conflicts with %u active render targets", slot, state.numRtv);
    }
}

void BindDescriptor(ExecState& state, const Descriptor& descriptor, UINT slot, UINT stages) {
    switch (descriptor.kind) {
    case DKind::CBV: {
        auto* resource = dynamic_cast<Resource*>(descriptor.keep.Get());
        ID3D11Buffer* buffer = resource ? resource->CbWindow(descriptor.bufOff, descriptor.bufSize) : nullptr;
        BindCb(state, stages, slot, buffer);
        break;
    }
    case DKind::SRV:
        if (descriptor.structStride && descriptor.res) {
            descriptor.res->SyncStruct(descriptor.structStride);
        } else {
            auto* uploadResource = dynamic_cast<Resource*>(descriptor.keep.Get());
            if (uploadResource && uploadResource->heap == D3D12_HEAP_TYPE_UPLOAD)
                uploadResource->SyncToGpu(descriptor.bufOff, descriptor.bufSize);
        }
        BindSrv(state, stages, slot, descriptor.srv.Get());
        break;
    case DKind::UAV:
        if (descriptor.structStride && descriptor.res) descriptor.res->SyncStruct(descriptor.structStride);
        BindUav(state, stages, slot, descriptor.uav.Get());
        if (descriptor.structStride && descriptor.res)
            state.structWrites.emplace_back(descriptor.keep, descriptor.structStride);
        break;
    case DKind::Sampler:
        BindSampler(state, stages, slot, descriptor.smp.Get());
        break;
    default:
        break;
    }
}

void BindDirectDescriptor(ExecState& state, const RootParam& param, const RootArg& arg, UINT stages) {
    UINT64 offset = 0;
    auto* resource = ResolveVA(arg.value, &offset);
    if (!resource || !resource->IsBuffer()) {
        Log("Root descriptor GPU VA 0x%llx does not resolve to a buffer",
            static_cast<unsigned long long>(arg.value));
        return;
    }
    if (arg.kind == 2) {
        const UINT64 size = std::min<UINT64>(resource->desc.Width - offset,
                                             D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16);
        ID3D11Buffer* buffer = resource->CbWindow(offset, size & ~UINT64_C(15));
        BindCb(state, stages, param.slot, buffer);
        return;
    }
    const UINT64 elements = (resource->desc.Width - offset) / 4;
    if (!elements || offset / 4 > UINT_MAX || elements > UINT_MAX) return;
    if (arg.kind == 3) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        desc.BufferEx.FirstElement = static_cast<UINT>(offset / 4);
        desc.BufferEx.NumElements = static_cast<UINT>(elements);
        desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        Ptr<ID3D11ShaderResourceView> view;
        if (FAILED(state.dev->d11->CreateShaderResourceView(resource->GpuBuffer(), &desc, view.Put()))) return;
        BindSrv(state, stages, param.slot, view.Get());
    } else if (arg.kind == 4) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = static_cast<UINT>(offset / 4);
        desc.Buffer.NumElements = static_cast<UINT>(elements);
        desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        Ptr<ID3D11UnorderedAccessView> view;
        if (FAILED(state.dev->d11->CreateUnorderedAccessView(resource->GpuBuffer(), &desc, view.Put()))) return;
        BindUav(state, stages, param.slot, view.Get());
    }
}

void BindRootSnapshot(ExecState& state, const std::shared_ptr<const BindSnap>& snapshot, bool compute) {
    static std::atomic<UINT> targetRootBindingLogCount{0};
    constexpr uint64_t targetVs = 0x20e0826b236597a1ULL;
    constexpr uint64_t targetPs0 = 0x44b0a992c2304254ULL;
    constexpr uint64_t targetPs1 = 0xd77600bde0076c33ULL;
    const bool targetPipeline = !compute && state.graphicsShaderHash == targetVs &&
        (state.graphicsPixelShaderHash == targetPs0 ||
         state.graphicsPixelShaderHash == targetPs1);
    const UINT targetDraw = targetPipeline
        ? targetRootBindingLogCount.fetch_add(1, std::memory_order_relaxed) + 1
        : 0;
    const bool logTarget = targetDraw > 0 && targetDraw <= 12;
    if (!snapshot || !snapshot->rs) {
        if (logTarget)
            Log("[TARGET_ROOT_BIND] draw=%u status=no_snapshot", targetDraw);
        return;
    }
    const auto& root = *snapshot->rs;
    if (logTarget)
        Log("[TARGET_ROOT_BIND] draw=%u root=%p params=%zu heaps=%zu",
            targetDraw, snapshot->rs, root.params.size(), snapshot->descriptorHeaps.size());
    for (const auto& sampler : root.statics)
        BindSampler(state, VisibilityStages(sampler.vis, compute), sampler.slot, sampler.st.Get());

    for (UINT i = 0; i < root.params.size(); ++i) {
        const RootParam& param = root.params[i];
        if (i >= snapshot->args.size()) {
            if (logTarget)
                Log("[TARGET_ROOT_PARAM] draw=%u index=%u type=%u visibility=%u arg_kind=missing "
                    "ranges=%zu",
                    targetDraw, i, static_cast<unsigned int>(param.type),
                    static_cast<unsigned int>(param.vis), param.ranges.size());
            continue;
        }
        const RootArg& arg = snapshot->args[i];
        const UINT stages = VisibilityStages(param.vis, compute);
        if (logTarget)
            Log("[TARGET_ROOT_PARAM] draw=%u index=%u type=%u visibility=%u arg_kind=%u "
                "arg_value=0x%llx ranges=%zu",
                targetDraw, i, static_cast<unsigned int>(param.type),
                static_cast<unsigned int>(param.vis), arg.kind,
                static_cast<unsigned long long>(arg.value), param.ranges.size());
        if (param.type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE && arg.kind == 5) {
            UINT baseIndex = 0;
            DescriptorHeap* heap = FindHeapByGpu(arg.value, &baseIndex);
            if (logTarget)
                Log("[TARGET_ROOT_TABLE] draw=%u index=%u handle=0x%llx heap=%p base=%u "
                    "heap_type=%u heap_flags=0x%x heap_size=%u gpu_base=0x%llx",
                    targetDraw, i, static_cast<unsigned long long>(arg.value), heap, baseIndex,
                    heap ? static_cast<unsigned int>(heap->hd.Type) : UINT_MAX,
                    heap ? static_cast<UINT>(heap->hd.Flags) : 0u,
                    heap ? static_cast<UINT>(heap->items.size()) : 0u,
                    static_cast<unsigned long long>(heap ? heap->gpuBase : 0));
            if (!heap) {
                Log("Root descriptor table handle 0x%llx is not in a live heap",
                    static_cast<unsigned long long>(arg.value));
                continue;
            }
            const bool isBound = std::any_of(snapshot->descriptorHeaps.begin(), snapshot->descriptorHeaps.end(),
                [heap](const Ptr<IUnknown>& item) {
                    return dynamic_cast<ID3D12DescriptorHeap*>(item.Get()) == static_cast<ID3D12DescriptorHeap*>(heap);
                });
            if (logTarget)
                Log("[TARGET_ROOT_HEAP] draw=%u index=%u bound=%u",
                    targetDraw, i, isBound ? 1u : 0u);
            if (!isBound) {
                Log("Root descriptor table references a heap that was not set on the command list");
                continue;
            }
            for (const auto& range : param.ranges) {
                if (logTarget)
                    Log("[TARGET_ROOT_RANGE] draw=%u index=%u type=%u base_reg=%u space=%u "
                        "offset=%u count=%u first_slot=%u",
                        targetDraw, i, static_cast<unsigned int>(range.type), range.baseReg,
                        range.space, range.offset, range.num,
                        range.slots.empty() ? UINT_MAX : range.slots.front());
                for (UINT n = 0; n < range.num; ++n) {
                    if (baseIndex + range.offset + n >= heap->items.size()) {
                        Log("Descriptor table range exceeds its heap");
                        break;
                    }
                    const Descriptor& descriptor = heap->items[baseIndex + range.offset + n];
                    if (logTarget && range.slots[n] == 0 &&
                        (range.type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
                         range.type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)) {
                        ID3D11Resource* rawSrvResource = nullptr;
                        if (descriptor.srv) descriptor.srv->GetResource(&rawSrvResource);
                        Ptr<ID3D11Resource> srvResource =
                            Ptr<ID3D11Resource>::Attach(rawSrvResource);
                        Log("[TARGET_ROOT_SLOT0] draw=%u range_type=%u descriptor=%p kind=%u "
                            "srv=%p srv_resource=%p sampler=%p resource=%p",
                            targetDraw, static_cast<unsigned int>(range.type), &descriptor,
                            static_cast<unsigned int>(descriptor.kind), descriptor.srv.Get(),
                            srvResource.Get(), descriptor.smp.Get(), descriptor.keep.Get());
                    }
                    BindDescriptor(state, descriptor, range.slots[n], stages);
                }
            }
        } else if (param.type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS && arg.kind == 1) {
            std::vector<UINT> values(param.num32, 0);
            if (arg.value < param.num32) {
                const size_t count = std::min<size_t>(arg.consts.size(), param.num32 - static_cast<UINT>(arg.value));
                std::copy_n(arg.consts.begin(), count, values.begin() + static_cast<size_t>(arg.value));
            }
            state.c->UpdateSubresource(param.cb.Get(), 0, nullptr, values.data(), 0, 0);
            BindCb(state, stages, param.slot, param.cb.Get());
        } else if ((param.type == D3D12_ROOT_PARAMETER_TYPE_CBV && arg.kind == 2) ||
                   (param.type == D3D12_ROOT_PARAMETER_TYPE_SRV && arg.kind == 3) ||
                   (param.type == D3D12_ROOT_PARAMETER_TYPE_UAV && arg.kind == 4)) {
            if (logTarget && param.type == D3D12_ROOT_PARAMETER_TYPE_CBV) {
                UINT64 offset = 0;
                Resource* resource = ResolveVA(arg.value, &offset);
                std::array<uint32_t, 4> words{};
                std::array<float, 4> values{};
                bool shadowAvailable = false;
                if (resource) {
                    std::lock_guard<std::mutex> lock(resource->mu);
                    if (offset <= resource->shadow.size() &&
                        resource->shadow.size() - static_cast<size_t>(offset) >= sizeof(words)) {
                        memcpy(words.data(), resource->shadow.data() + offset, sizeof(words));
                        memcpy(values.data(), resource->shadow.data() + offset, sizeof(values));
                        shadowAvailable = true;
                    }
                }
                const UINT64 remaining = resource && resource->desc.Width >= offset
                    ? resource->desc.Width - offset : 0;
                const UINT64 cbSize = std::min<UINT64>(remaining,
                    D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16) & ~UINT64_C(15);
                Log("[TARGET_ROOT_CBV] draw=%u index=%u stages=0x%x gpu_va=0x%llx "
                    "resource=%p resource_va=0x%llx offset=%llu width=%llu heap=%u "
                    "cb_bytes=%llu shadow_bytes=%zu shadow_status=%s "
                    "words=%08x,%08x,%08x,%08x floats=%.9g,%.9g,%.9g,%.9g",
                    targetDraw, i, stages, static_cast<unsigned long long>(arg.value),
                    resource, static_cast<unsigned long long>(resource ? resource->va : 0),
                    static_cast<unsigned long long>(offset),
                    static_cast<unsigned long long>(resource ? resource->desc.Width : 0),
                    static_cast<unsigned int>(resource ? resource->heap : D3D12_HEAP_TYPE_DEFAULT),
                    static_cast<unsigned long long>(cbSize),
                    resource ? resource->shadow.size() : 0,
                    shadowAvailable ? "ok" : "unavailable",
                    words[0], words[1], words[2], words[3],
                    values[0], values[1], values[2], values[3]);
            }
            BindDirectDescriptor(state, param, arg, stages);
        }
    }
}

void FlushStructWrites(ExecState& state) {
    for (const auto& write : state.structWrites) {
        auto* resource = dynamic_cast<Resource*>(write.first.Get());
        if (resource) resource->WriteBackStruct(write.second);
    }
    state.structWrites.clear();
}

void ApplyPipeline(ExecState& state, PipelineState* pipeline) {
    if (!pipeline) {
        state.skipDraws = false;
        state.skipDispatches = false;
        state.hasGraphicsPipeline = false;
        state.hasComputePipeline = false;
        state.graphicsPlaceholderVs = false;
        state.computePlaceholderCs = false;
        state.graphicsPipelineIdentity = 0;
        state.graphicsShaderHash = 0;
        state.graphicsPixelShaderHash = 0;
        return;
    }
    state.skipDraws = !pipeline->compute && pipeline->missingShader;
    state.skipDispatches = pipeline->compute && pipeline->missingShader;
    state.hasGraphicsPipeline = !pipeline->compute;
    state.hasComputePipeline = pipeline->compute;
    state.graphicsPlaceholderVs = !pipeline->compute && pipeline->missingVs;
    state.computePlaceholderCs = pipeline->compute && pipeline->missingCs;
    state.graphicsPipelineIdentity = pipeline->compute
        ? 0 : reinterpret_cast<uintptr_t>(pipeline);
    state.graphicsShaderHash = pipeline->compute ? 0 : pipeline->shaderHash;
    state.graphicsPixelShaderHash = pipeline->compute ? 0 : pipeline->pixelShaderHash;
    if (pipeline->missingShader)
        Log("Skipped execution for PSO placeholder with missing SM5 shader override");
    state.c->VSSetShader(pipeline->vs.Get(), nullptr, 0);
    state.c->PSSetShader(pipeline->ps.Get(), nullptr, 0);
    state.c->GSSetShader(pipeline->gs.Get(), nullptr, 0);
    state.c->HSSetShader(pipeline->hs.Get(), nullptr, 0);
    state.c->DSSetShader(pipeline->ds.Get(), nullptr, 0);
    state.c->CSSetShader(pipeline->cs.Get(), nullptr, 0);
    state.c->IASetInputLayout(pipeline->il.Get());
    state.c->RSSetState(pipeline->rs.Get());
    state.bs = pipeline->bs;
    state.dss = pipeline->dss;
    state.sampleMask = pipeline->sampleMask;
    state.ApplyOM();
}

} // namespace

void DumpBackbufferSourceHistory() {
    DumpBackbufferSourceHistoryImpl();
}

void STDMETHODCALLTYPE CommandList::SetDescriptorHeaps(UINT count, ID3D12DescriptorHeap* const* heaps) {
    if (count > 2 || (count && !heaps)) {
        Log("Rejected invalid descriptor heap array");
        return;
    }
    std::vector<Ptr<IUnknown>> replacement;
    bool hasResourceHeap = false;
    bool hasSamplerHeap = false;
    for (UINT i = 0; i < count; ++i) {
        auto* heap = dynamic_cast<DescriptorHeap*>(heaps[i]);
        if (!heap || !(heap->hd.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)) {
            Log("SetDescriptorHeaps requires wrapper-owned shader-visible heaps");
            return;
        }
        bool& alreadySet = heap->hd.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
            ? hasSamplerHeap : hasResourceHeap;
        if (alreadySet) {
            Log("SetDescriptorHeaps received duplicate heap types");
            return;
        }
        alreadySet = true;
        replacement.emplace_back(static_cast<ID3D12DescriptorHeap*>(heap));
    }
    descriptorHeaps = std::move(replacement);
}

HRESULT STDMETHODCALLTYPE Device::CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* source,
    REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!source ||
        (source->Type != D3D12_COMMAND_LIST_TYPE_DIRECT &&
         source->Type != D3D12_COMMAND_LIST_TYPE_COMPUTE &&
         source->Type != D3D12_COMMAND_LIST_TYPE_COPY) ||
        (source->Priority != D3D12_COMMAND_QUEUE_PRIORITY_NORMAL &&
         source->Priority != D3D12_COMMAND_QUEUE_PRIORITY_HIGH) ||
        source->Flags != D3D12_COMMAND_QUEUE_FLAG_NONE ||
        (source->NodeMask != 0 && source->NodeMask != 1))
        return E_INVALIDARG;
    auto* queue = new (std::nothrow) CommandQueue(this, *source);
    if (!queue) return E_OUTOFMEMORY;
    const HRESULT result = queue->QueryInterface(riid, out);
    queue->Release();
    Log("CreateCommandQueue type=%u result=0x%08lx", source->Type, result);
    return result;
}

HRESULT STDMETHODCALLTYPE Device::CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
    ID3D12CommandAllocator* allocator, ID3D12PipelineState* initialState, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if ((nodeMask != 0 && nodeMask != 1) || type > D3D12_COMMAND_LIST_TYPE_COPY || !allocator)
        return E_INVALIDARG;
    auto* commandList = new (std::nothrow) CommandList(this, type);
    if (!commandList) return E_OUTOFMEMORY;
    if (initialState) commandList->SetPipelineState(initialState);
    const HRESULT result = commandList->QueryInterface(riid, out);
    commandList->Release();
    Log("CreateCommandList type=%u result=0x%08lx", type, result);
    return result;
}

HRESULT STDMETHODCALLTYPE Device::CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* desc,
    ID3D12RootSignature* rootSignature, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!desc || !desc->pArgumentDescs || desc->NumArgumentDescs != 1 ||
        !desc->ByteStride || (desc->ByteStride & 3) ||
        (desc->NodeMask != 0 && desc->NodeMask != 1)) {
        Log("CreateCommandSignature rejected invalid descriptor");
        return E_INVALIDARG;
    }
    UINT argumentSize = 0;
    switch (desc->pArgumentDescs[0].Type) {
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
        argumentSize = sizeof(UINT) * 3;
        break;
    case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
        argumentSize = sizeof(UINT) * 4;
        break;
    case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
        argumentSize = sizeof(UINT) * 5;
        break;
    default:
        Log("CreateCommandSignature unsupported argument type=%u",
            desc->pArgumentDescs[0].Type);
        return E_NOTIMPL;
    }
    if (desc->ByteStride < argumentSize) {
        Log("CreateCommandSignature stride=%u is smaller than argument size=%u",
            desc->ByteStride, argumentSize);
        return E_INVALIDARG;
    }

    auto* signature = new (std::nothrow) CommandSignature(this,
        desc->pArgumentDescs[0].Type, desc->ByteStride, rootSignature);
    if (!signature) return E_OUTOFMEMORY;
    const HRESULT result = signature->QueryInterface(riid, out);
    signature->Release();
    Log("CreateCommandSignature argument-type=%u stride=%u result=0x%08lx",
        desc->pArgumentDescs[0].Type, desc->ByteStride, result);
    return result;
}

std::shared_ptr<const BindSnap> CommandList::Snap(BindState& bind) {
    if (!bind.snap) {
        auto snapshot = std::make_shared<BindSnap>();
        snapshot->keep = bind.keep;
        snapshot->rs = bind.rs;
        snapshot->args = bind.args;
        snapshot->descriptorHeaps = descriptorHeaps;
        bind.snap = std::move(snapshot);
    }
    return bind.snap;
}

void CommandList::SetRootArg(BindState& bind, UINT index, RootArg&& arg) {
    if (bind.args.size() <= index) bind.args.resize(static_cast<size_t>(index) + 1);
    bind.args[index] = std::move(arg);
    bind.snap.reset();
}

void CommandList::DrawCommon(Cmd draw, bool) {
    Add(std::move(draw));
}

HRESULT STDMETHODCALLTYPE CommandList::Close() {
    if (closed) return E_FAIL;
    closed = true;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CommandList::Reset(ID3D12CommandAllocator* allocator, ID3D12PipelineState* initialState) {
    if (!allocator) return E_INVALIDARG;
    if (!closed) return E_FAIL;
    cmds.clear();
    gfx = {};
    comp = {};
    descriptorHeaps.clear();
    closed = false;
    if (initialState) SetPipelineState(initialState);
    return S_OK;
}

void STDMETHODCALLTYPE CommandList::ClearState(ID3D12PipelineState* initialState) {
    gfx = {};
    comp = {};
    Ptr<IUnknown> keep = initialState ? Ptr<IUnknown>(initialState) : Ptr<IUnknown>();
    Add([keep](ExecState& state) {
        state.skipDraws = false;
        state.skipDispatches = false;
        state.hasGraphicsPipeline = false;
        state.hasComputePipeline = false;
        state.graphicsPlaceholderVs = false;
        state.computePlaceholderCs = false;
        state.graphicsPipelineIdentity = 0;
        state.graphicsShaderHash = 0;
        state.c->ClearState();
        if (keep) {
            auto* pipeline = dynamic_cast<PipelineState*>(keep.Get());
            if (pipeline) ApplyPipeline(state, pipeline);
        }
    });
}

void STDMETHODCALLTYPE CommandList::DrawInstanced(UINT vertexCount, UINT instanceCount,
    UINT startVertex, UINT startInstance) {
    TraceApi("CommandList::DrawInstanced");
    auto snapshot = Snap(gfx);
    Add([=](ExecState& state) {
        if (state.skipDraws) {
            LogSkippedDraw(state, "DrawInstanced", vertexCount, instanceCount);
            if (Cfg().diagnosticRenderTrace) {
                auto& stats = TraceStats();
                if (state.graphicsPlaceholderVs)
                    stats.drawSkipPlaceholderVs.fetch_add(1, std::memory_order_relaxed);
                else
                    stats.drawSkipPlaceholderOther.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
        BindRootSnapshot(state, snapshot, false);
        state.c->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
        RecordDrawExecution(state, false, "DrawInstanced", vertexCount, instanceCount);
        FlushStructWrites(state);
    });
}

void STDMETHODCALLTYPE CommandList::DrawIndexedInstanced(UINT indexCount, UINT instanceCount,
    UINT startIndex, INT baseVertex, UINT startInstance) {
    TraceApi("CommandList::DrawIndexedInstanced");
    auto snapshot = Snap(gfx);
    Add([=](ExecState& state) {
        if (state.skipDraws) {
            LogSkippedDraw(state, "DrawIndexedInstanced", indexCount, instanceCount);
            if (Cfg().diagnosticRenderTrace) {
                auto& stats = TraceStats();
                if (state.graphicsPlaceholderVs)
                    stats.drawSkipPlaceholderVs.fetch_add(1, std::memory_order_relaxed);
                else
                    stats.drawSkipPlaceholderOther.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
        BindRootSnapshot(state, snapshot, false);
        TargetDrawPipelineStats pipelineStats{};
        LogTargetDrawDiagnostic(state, indexCount, instanceCount,
            startIndex, baseVertex, startInstance, pipelineStats);
        state.c->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
        FinishTargetDrawPipelineStats(state, std::move(pipelineStats));
        RecordDrawExecution(state, true, "DrawIndexedInstanced", indexCount, instanceCount);
        FlushStructWrites(state);
    });
}

void STDMETHODCALLTYPE CommandList::Dispatch(UINT x, UINT y, UINT z) {
    TraceApi("CommandList::Dispatch");
    auto snapshot = Snap(comp);
    Add([=](ExecState& state) {
        if (state.skipDispatches) {
            if (Cfg().diagnosticRenderTrace) {
                auto& stats = TraceStats();
                if (state.computePlaceholderCs)
                    stats.dispatchSkipPlaceholderCs.fetch_add(1, std::memory_order_relaxed);
                else
                    stats.dispatchSkipNoPso.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
        BindRootSnapshot(state, snapshot, true);
        state.c->Dispatch(x, y, z);
        RecordDispatchExecution();
        FlushStructWrites(state);
    });
}

void STDMETHODCALLTYPE CommandList::ExecuteIndirect(ID3D12CommandSignature* sourceSignature,
    UINT maxCommandCount, ID3D12Resource* sourceArgumentBuffer, UINT64 argumentBufferOffset,
    ID3D12Resource* sourceCountBuffer, UINT64 countBufferOffset) {
    TraceApi("CommandList::ExecuteIndirect");
    auto* signature = sourceSignature ? dynamic_cast<CommandSignature*>(sourceSignature) : nullptr;
    auto* arguments = sourceArgumentBuffer ? dynamic_cast<Resource*>(sourceArgumentBuffer) : nullptr;
    const bool compute = signature &&
        signature->argumentType == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    if (!signature || !arguments || !arguments->IsBuffer() || sourceCountBuffer ||
        (compute && type == D3D12_COMMAND_LIST_TYPE_COPY) ||
        (!compute && type != D3D12_COMMAND_LIST_TYPE_DIRECT)) {
        Log("ExecuteIndirect unsupported signature/list type or count buffer=%p offset=%llu",
            sourceCountBuffer, static_cast<unsigned long long>(countBufferOffset));
        Unimpl("ID3D12GraphicsCommandList::ExecuteIndirect");
        return;
    }
    if (!maxCommandCount) return;

    const UINT argumentSize = compute ? sizeof(UINT) * 3
        : signature->argumentType == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW
            ? sizeof(UINT) * 4 : sizeof(UINT) * 5;
    const UINT64 lastOffset = static_cast<UINT64>(maxCommandCount - 1) *
        signature->byteStride;
    if ((argumentBufferOffset & 3) ||
        lastOffset > UINT64_MAX - argumentSize ||
        argumentBufferOffset > arguments->desc.Width ||
        lastOffset + argumentSize > arguments->desc.Width - argumentBufferOffset ||
        argumentBufferOffset > UINT64_MAX - lastOffset ||
        argumentBufferOffset + lastOffset > UINT_MAX) {
        Log("ExecuteIndirect rejected argument range offset=%llu count=%u stride=%u width=%llu",
            static_cast<unsigned long long>(argumentBufferOffset), maxCommandCount,
            signature->byteStride, static_cast<unsigned long long>(arguments->desc.Width));
        return;
    }

    auto snapshot = Snap(compute ? comp : gfx);
    Ptr<IUnknown> signatureKeep(sourceSignature);
    Ptr<IUnknown> argumentsKeep(sourceArgumentBuffer);
    const D3D12_INDIRECT_ARGUMENT_TYPE argumentType = signature->argumentType;
    const UINT byteStride = signature->byteStride;
    Add([=](ExecState& state) {
        if (compute ? state.skipDispatches : state.skipDraws) {
            if (!compute)
                LogSkippedDraw(state, "ExecuteIndirect", maxCommandCount, byteStride);
            if (Cfg().diagnosticRenderTrace) {
                auto& stats = TraceStats();
                if (compute && state.computePlaceholderCs)
                    stats.dispatchSkipPlaceholderCs.fetch_add(1, std::memory_order_relaxed);
                else if (compute)
                    stats.dispatchSkipNoPso.fetch_add(1, std::memory_order_relaxed);
                else if (state.graphicsPlaceholderVs)
                    stats.drawSkipPlaceholderVs.fetch_add(1, std::memory_order_relaxed);
                else
                    stats.drawSkipPlaceholderOther.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
        auto* commandSignature = dynamic_cast<CommandSignature*>(signatureKeep.Get());
        auto* argumentResource = dynamic_cast<Resource*>(argumentsKeep.Get());
        if (!commandSignature || !argumentResource || !argumentResource->GpuBuffer()) {
            Log("ExecuteIndirect backing resource unavailable");
            return;
        }
        const UINT64 byteCount = static_cast<UINT64>(maxCommandCount - 1) *
            byteStride + argumentSize;
        if (argumentResource->heap == D3D12_HEAP_TYPE_UPLOAD)
            argumentResource->SyncToGpu(argumentBufferOffset, byteCount);
        BindRootSnapshot(state, snapshot, compute);
        ID3D11Buffer* argumentBuffer = argumentResource->GpuBuffer();
        for (UINT i = 0; i < maxCommandCount; ++i) {
            const UINT offset = static_cast<UINT>(argumentBufferOffset +
                static_cast<UINT64>(i) * byteStride);
            switch (argumentType) {
            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                state.c->DispatchIndirect(argumentBuffer, offset);
                RecordDispatchExecution();
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                state.c->DrawInstancedIndirect(argumentBuffer, offset);
                RecordDrawExecution(state, false, "ExecuteIndirect", 0, 0, true);
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                state.c->DrawIndexedInstancedIndirect(argumentBuffer, offset);
                RecordDrawExecution(state, true, "ExecuteIndirect", 0, 0, true);
                break;
            default:
                Log("ExecuteIndirect encountered unsupported argument type=%u", argumentType);
                return;
            }
            FlushStructWrites(state);
        }
    });
}

void STDMETHODCALLTYPE CommandList::CopyBufferRegion(ID3D12Resource* destination, UINT64 destinationOffset,
    ID3D12Resource* source, UINT64 sourceOffset, UINT64 byteCount) {
    Resource* dst = AsResource(destination);
    Resource* src = AsResource(source);
    if (!dst || !src || !dst->IsBuffer() || !src->IsBuffer() ||
        destinationOffset > dst->desc.Width || byteCount > dst->desc.Width - destinationOffset ||
        sourceOffset > src->desc.Width || byteCount > src->desc.Width - sourceOffset ||
        destinationOffset > UINT_MAX || sourceOffset > UINT_MAX || byteCount > UINT_MAX) {
        Log("Rejected invalid CopyBufferRegion request");
        return;
    }
    Ptr<IUnknown> keepDst(static_cast<ID3D12Resource*>(dst));
    Ptr<IUnknown> keepSrc(static_cast<ID3D12Resource*>(src));
    Add([keepDst, keepSrc, destinationOffset, sourceOffset, byteCount](ExecState& state) {
        auto* dstResource = dynamic_cast<Resource*>(keepDst.Get());
        auto* srcResource = dynamic_cast<Resource*>(keepSrc.Get());
        if (!dstResource || !srcResource) return;
        D3D11_BOX box{};
        box.left = static_cast<UINT>(sourceOffset);
        box.right = static_cast<UINT>(sourceOffset + byteCount);
        box.top = 0;
        box.bottom = 1;
        box.front = 0;
        box.back = 1;
        if (srcResource->heap == D3D12_HEAP_TYPE_UPLOAD)
            srcResource->SyncToGpu(sourceOffset, byteCount);
        state.c->CopySubresourceRegion(dstResource->Raw(), 0, static_cast<UINT>(destinationOffset), 0, 0,
                                       srcResource->Raw(), 0, &box);
    });
}

void STDMETHODCALLTYPE CommandList::CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* destination,
    UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION* source, const D3D12_BOX* sourceBox) {
    if (!destination || !source || !destination->pResource || !source->pResource) {
        Log("Rejected null CopyTextureRegion location");
        return;
    }

    Resource* dst = AsResource(destination->pResource);
    Resource* src = AsResource(source->pResource);
    if (!dst || !src) {
        Log("CopyTextureRegion received a resource not owned by this proxy");
        return;
    }

    const bool dstTexture = destination->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    const bool srcTexture = source->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    const bool dstFootprint = destination->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    const bool srcFootprint = source->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    if (dstTexture && srcTexture) {
        if (dst->IsBuffer() || src->IsBuffer() ||
            destination->SubresourceIndex >= dst->NumSubresources() ||
            source->SubresourceIndex >= src->NumSubresources()) {
            Log("Rejected invalid texture copy locations");
            return;
        }
    const UINT sourceMip = source->SubresourceIndex % std::max<UINT>(src->desc.MipLevels, 1);
    const D3D12_BOX sourceRegion = sourceBox ? *sourceBox : D3D12_BOX{
        0, 0, 0,
        static_cast<UINT>(std::max<UINT64>(1, src->desc.Width >> sourceMip)),
        std::max<UINT>(1, src->desc.Height >> sourceMip),
        src->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
            ? std::max<UINT>(1, src->desc.DepthOrArraySize >> sourceMip) : 1
    };
    Ptr<IUnknown> keepDst(static_cast<ID3D12Resource*>(dst));
    Ptr<IUnknown> keepSrc(static_cast<ID3D12Resource*>(src));
    const UINT dstSubresource = destination->SubresourceIndex;
    const UINT srcSubresource = source->SubresourceIndex;
    D3D11_BOX box{sourceRegion.left, sourceRegion.top, sourceRegion.front,
                  sourceRegion.right, sourceRegion.bottom, sourceRegion.back};
    Add([=](ExecState& state) {
        auto* dstResource = dynamic_cast<Resource*>(keepDst.Get());
        auto* srcResource = dynamic_cast<Resource*>(keepSrc.Get());
        if (dstResource && srcResource) {
            LogBackbufferCopy("CopyTextureRegion", dstResource, srcResource);
            state.c->CopySubresourceRegion(dstResource->Raw(), dstSubresource, x, y, z,
                                           srcResource->Raw(), srcSubresource, &box);
        }
    });
        return;
    }

    if (dstTexture && srcFootprint) {
        const auto& placed = source->PlacedFootprint;
        FootprintCopyInfo info{};
        if (dst->IsBuffer() || destination->SubresourceIndex >= dst->NumSubresources() ||
            !src->IsBuffer() || src->heap == D3D12_HEAP_TYPE_READBACK ||
            !ValidateFootprint(src, placed, info) ||
            placed.Footprint.Format != dst->desc.Format) {
            Log("Rejected buffer-to-texture footprint copy with incompatible resources or layout");
            return;
        }
        const FormatInfo format = GetFormatInfo(placed.Footprint.Format);
        const D3D12_BOX region = sourceBox ? *sourceBox : D3D12_BOX{
            0, 0, 0, placed.Footprint.Width, placed.Footprint.Height, placed.Footprint.Depth
        };
        if (!ValidCopyBox(region, placed.Footprint) ||
            region.left % format.blockW || region.top % format.blockH ||
            (region.right % format.blockW && region.right != placed.Footprint.Width) ||
            (region.bottom % format.blockH && region.bottom != placed.Footprint.Height)) {
            Log("Rejected invalid or block-misaligned source footprint box");
            return;
        }

        const UINT mipLevels = std::max<UINT>(dst->desc.MipLevels, 1);
        const UINT mip = destination->SubresourceIndex % mipLevels;
        const UINT64 targetWidth = std::max<UINT64>(1, dst->desc.Width >> mip);
        const UINT targetHeight = dst->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D
            ? 1 : std::max<UINT>(1, dst->desc.Height >> mip);
        const UINT targetDepth = dst->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
            ? std::max<UINT>(1, dst->desc.DepthOrArraySize >> mip) : 1;
        const UINT copyWidth = region.right - region.left;
        const UINT copyHeight = region.bottom - region.top;
        const UINT copyDepth = region.back - region.front;
        if (x > targetWidth || copyWidth > targetWidth - x ||
            y > targetHeight || copyHeight > targetHeight - y ||
            z > targetDepth || copyDepth > targetDepth - z) {
            Log("Rejected buffer-to-texture footprint copy outside the destination subresource");
            return;
        }

        Ptr<IUnknown> keepDst(static_cast<ID3D12Resource*>(dst));
        Ptr<IUnknown> keepSrc(static_cast<ID3D12Resource*>(src));
        const D3D12_SUBRESOURCE_FOOTPRINT footprint = placed.Footprint;
        const UINT subresource = destination->SubresourceIndex;
        Add([=](ExecState& state) {
            auto* dstResource = dynamic_cast<Resource*>(keepDst.Get());
            auto* srcResource = dynamic_cast<Resource*>(keepSrc.Get());
            if (!dstResource || !srcResource) return;
            Ptr<ID3D11Resource> stagingTexture;
            UINT stagingSubresource = 0;
            HRESULT result = NeedsBcMipStaging(footprint)
                ? CreateBcMipStagingTexture(state.dev, footprint, D3D11_CPU_ACCESS_WRITE,
                    stagingTexture, stagingSubresource)
                : CreateStagingTexture(state.dev, dstResource->desc.Dimension, footprint,
                    D3D11_CPU_ACCESS_WRITE, stagingTexture);
            if (FAILED(result)) {
                Log("Could not create texture staging resource for upload: 0x%08lx", result);
                return;
            }
            const uint8_t* sourceData = nullptr;
            D3D11_MAPPED_SUBRESOURCE mappedBuffer{};
            Ptr<ID3D11Buffer> stagingBuffer;
            result = MapBufferForRead(state, srcResource, stagingBuffer, mappedBuffer, sourceData);
            if (FAILED(result) || !sourceData) {
                Log("Could not read upload footprint buffer: 0x%08lx", result);
                return;
            }
            D3D11_MAPPED_SUBRESOURCE mappedTexture{};
            result = state.c->Map(stagingTexture.Get(), stagingSubresource,
                D3D11_MAP_WRITE, 0, &mappedTexture);
            if (SUCCEEDED(result)) {
                const UINT sourceRows = info.rows;
                const UINT sourceSlices = footprint.Depth;
                const size_t rowBytes = static_cast<size_t>(info.rowBytes);
                for (UINT slice = 0; slice < sourceSlices; ++slice) {
                    for (UINT row = 0; row < sourceRows; ++row) {
                        const uint8_t* sourceRow = sourceData + placed.Offset +
                            static_cast<UINT64>(slice) * info.sliceBytes +
                            static_cast<UINT64>(row) * footprint.RowPitch;
                        uint8_t* destinationRow = static_cast<uint8_t*>(mappedTexture.pData) +
                            static_cast<size_t>(slice) * mappedTexture.DepthPitch +
                            static_cast<size_t>(row) * mappedTexture.RowPitch;
                        memcpy(destinationRow, sourceRow, rowBytes);
                    }
                }
                state.c->Unmap(stagingTexture.Get(), stagingSubresource);
            }
            if (mappedBuffer.pData) state.c->Unmap(stagingBuffer.Get(), 0);
            if (FAILED(result)) {
                Log("Could not map texture staging resource for upload: 0x%08lx", result);
                return;
            }
            const D3D11_BOX box{region.left, region.top, region.front,
                region.right, region.bottom, region.back};
            LogBackbufferCopy("CopyTextureRegion", dstResource, srcResource);
            state.c->CopySubresourceRegion(dstResource->Raw(), subresource, x, y, z,
                stagingTexture.Get(), stagingSubresource, &box);
        });
        return;
    }

    if (dstFootprint && srcTexture) {
        const auto& placed = destination->PlacedFootprint;
        FootprintCopyInfo info{};
        if (!dst->IsBuffer() || dst->heap == D3D12_HEAP_TYPE_UPLOAD ||
            src->IsBuffer() || source->SubresourceIndex >= src->NumSubresources() ||
            !ValidateFootprint(dst, placed, info) ||
            placed.Footprint.Format != src->desc.Format) {
            Log("Rejected texture-to-buffer footprint copy with incompatible resources or layout");
            return;
        }
        const FormatInfo format = GetFormatInfo(placed.Footprint.Format);
        const UINT sourceMip = source->SubresourceIndex % std::max<UINT>(src->desc.MipLevels, 1);
        const UINT64 sourceWidth = std::max<UINT64>(1, src->desc.Width >> sourceMip);
        const UINT sourceHeight = src->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D
            ? 1 : std::max<UINT>(1, src->desc.Height >> sourceMip);
        const UINT sourceDepth = src->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
            ? std::max<UINT>(1, src->desc.DepthOrArraySize >> sourceMip) : 1;
        const D3D12_BOX region = sourceBox ? *sourceBox : D3D12_BOX{
            0, 0, 0, static_cast<UINT>(sourceWidth), sourceHeight, sourceDepth
        };
        if (region.left >= region.right || region.top >= region.bottom ||
            region.front >= region.back || region.right > sourceWidth ||
            region.bottom > sourceHeight || region.back > sourceDepth ||
            region.left % format.blockW || region.top % format.blockH ||
            (region.right % format.blockW && region.right != sourceWidth) ||
            (region.bottom % format.blockH && region.bottom != sourceHeight)) {
            Log("Rejected invalid or block-misaligned source texture box");
            return;
        }
        const UINT copyWidth = region.right - region.left;
        const UINT copyHeight = region.bottom - region.top;
        const UINT copyDepth = region.back - region.front;
        if (x > placed.Footprint.Width || copyWidth > placed.Footprint.Width - x ||
            y > placed.Footprint.Height || copyHeight > placed.Footprint.Height - y ||
            z > placed.Footprint.Depth || copyDepth > placed.Footprint.Depth - z ||
            x % format.blockW || y % format.blockH ||
            (x + copyWidth) % format.blockW && x + copyWidth != placed.Footprint.Width ||
            (y + copyHeight) % format.blockH && y + copyHeight != placed.Footprint.Height) {
            Log("Rejected texture-to-buffer footprint copy outside the destination layout");
            return;
        }

        const UINT sourceSubresource = source->SubresourceIndex;
        const D3D12_SUBRESOURCE_FOOTPRINT footprint = placed.Footprint;
        const UINT xBlocks = x / format.blockW;
        const UINT yBlocks = y / format.blockH;
        const UINT copyBlocks = (copyWidth + format.blockW - 1) / format.blockW;
        const UINT copyRows = (copyHeight + format.blockH - 1) / format.blockH;
        const UINT64 copyRowBytes = static_cast<UINT64>(copyBlocks) * info.bytesPerBlock;
        const UINT64 copySliceBytes = static_cast<UINT64>(footprint.RowPitch) * info.rows;
        Ptr<IUnknown> keepDst(static_cast<ID3D12Resource*>(dst));
        Ptr<IUnknown> keepSrc(static_cast<ID3D12Resource*>(src));
        Add([=](ExecState& state) {
            auto* dstResource = dynamic_cast<Resource*>(keepDst.Get());
            auto* srcResource = dynamic_cast<Resource*>(keepSrc.Get());
            if (!dstResource || !srcResource) return;
            Ptr<ID3D11Resource> stagingTexture;
            UINT stagingSubresource = 0;
            HRESULT result = NeedsBcMipStaging(footprint)
                ? CreateBcMipStagingTexture(state.dev, footprint, D3D11_CPU_ACCESS_READ,
                    stagingTexture, stagingSubresource)
                : CreateStagingTexture(state.dev, srcResource->desc.Dimension, footprint,
                    D3D11_CPU_ACCESS_READ, stagingTexture);
            if (FAILED(result)) {
                Log("Could not create texture staging resource for readback: 0x%08lx", result);
                return;
            }
            const D3D11_BOX box{region.left, region.top, region.front,
                region.right, region.bottom, region.back};
            state.c->CopySubresourceRegion(stagingTexture.Get(), stagingSubresource, x, y, z,
                srcResource->Raw(), sourceSubresource, &box);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            result = state.c->Map(stagingTexture.Get(), stagingSubresource,
                D3D11_MAP_READ, 0, &mapped);
            if (FAILED(result)) {
                Log("Could not map texture staging resource for readback: 0x%08lx", result);
                return;
            }
            for (UINT slice = 0; slice < copyDepth; ++slice) {
                for (UINT row = 0; row < copyRows; ++row) {
                    const UINT64 destinationOffset = placed.Offset +
                        static_cast<UINT64>(z + slice) * copySliceBytes +
                        static_cast<UINT64>(yBlocks + row) * footprint.RowPitch +
                        static_cast<UINT64>(xBlocks) * info.bytesPerBlock;
                    const uint8_t* sourceData = static_cast<const uint8_t*>(mapped.pData) +
                        static_cast<size_t>(z + slice) * mapped.DepthPitch +
                        static_cast<size_t>(yBlocks + row) * mapped.RowPitch +
                        static_cast<size_t>(xBlocks) * info.bytesPerBlock;
                    D3D11_BOX bufferBox{};
                    bufferBox.left = static_cast<UINT>(destinationOffset);
                    bufferBox.right = static_cast<UINT>(destinationOffset + copyRowBytes);
                    bufferBox.top = 0;
                    bufferBox.bottom = 1;
                    bufferBox.front = 0;
                    bufferBox.back = 1;
                    state.c->UpdateSubresource(dstResource->buf.Get(), 0, &bufferBox, sourceData, 0, 0);
                    if (!dstResource->shadow.empty())
                        memcpy(dstResource->shadow.data() + destinationOffset, sourceData,
                            static_cast<size_t>(copyRowBytes));
                }
            }
            state.c->Unmap(stagingTexture.Get(), stagingSubresource);
        });
        return;
    }

    Log("CopyTextureRegion supports texture subresources and buffer placed footprints only");
}

void STDMETHODCALLTYPE CommandList::CopyResource(ID3D12Resource* destination, ID3D12Resource* source) {
    Resource* dst = AsResource(destination);
    Resource* src = AsResource(source);
    if (!dst || !src || dst->desc.Dimension != src->desc.Dimension ||
        dst->desc.Width != src->desc.Width || dst->desc.Height != src->desc.Height ||
        dst->desc.DepthOrArraySize != src->desc.DepthOrArraySize ||
        dst->desc.MipLevels != src->desc.MipLevels || dst->desc.Format != src->desc.Format ||
        dst->desc.SampleDesc.Count != src->desc.SampleDesc.Count ||
        dst->desc.SampleDesc.Quality != src->desc.SampleDesc.Quality) {
        Log("Rejected incompatible CopyResource request");
        return;
    }
    if (dst->backbuffer && src->tex)
        RegisterBackbufferCopySource(src->tex.Get());
    Ptr<IUnknown> keepDst(static_cast<ID3D12Resource*>(dst));
    Ptr<IUnknown> keepSrc(static_cast<ID3D12Resource*>(src));
    Add([keepDst, keepSrc](ExecState& state) {
        auto* dstResource = dynamic_cast<Resource*>(keepDst.Get());
        auto* srcResource = dynamic_cast<Resource*>(keepSrc.Get());
        if (dstResource && srcResource) {
            LogBackbufferCopy("CopyResource", dstResource, srcResource);
            if (dstResource->backbuffer && Cfg().diagnosticRenderTrace)
                ++state.copyToBackbuffer;
            state.c->CopyResource(dstResource->Raw(), srcResource->Raw());
            if (dstResource->backbuffer && srcResource->tex && state.dev) {
                std::lock_guard<std::mutex> lock(state.dev->backbufferCopyLock);
                state.dev->lastBackbufferCopySource = srcResource->tex;
                state.dev->lastBackbufferCopySourceDesc = srcResource->desc;
                ++state.dev->lastBackbufferCopySequence;
                Log("[COPY_TO_BB] CopyResource source=%p dimension=%u width=%llu height=%u "
                    "format=%u sequence=%llu",
                    srcResource, srcResource->desc.Dimension,
                    static_cast<unsigned long long>(srcResource->desc.Width),
                    srcResource->desc.Height, srcResource->desc.Format,
                    static_cast<unsigned long long>(state.dev->lastBackbufferCopySequence));
            } else if (dstResource->backbuffer && state.dev) {
                std::lock_guard<std::mutex> lock(state.dev->backbufferCopyLock);
                state.dev->lastBackbufferCopySource.Reset();
                state.dev->lastBackbufferCopySourceDesc = srcResource->desc;
                ++state.dev->lastBackbufferCopySequence;
                Log("[COPY_TO_BB] CopyResource source is not a texture resource=%p "
                    "dimension=%u width=%llu height=%u format=%u sequence=%llu",
                    srcResource, srcResource->desc.Dimension,
                    static_cast<unsigned long long>(srcResource->desc.Width),
                    srcResource->desc.Height, srcResource->desc.Format,
                    static_cast<unsigned long long>(state.dev->lastBackbufferCopySequence));
            }
        }
    });
}

void STDMETHODCALLTYPE CommandList::CopyTiles(ID3D12Resource* tiledResource,
    const D3D12_TILED_RESOURCE_COORDINATE* coordinate, const D3D12_TILE_REGION_SIZE* region,
    ID3D12Resource* buffer, UINT64 offset, D3D12_TILE_COPY_FLAGS flags) {
    Resource* texture = AsResource(tiledResource);
    Resource* tileBuffer = AsResource(buffer);
    const UINT supportedFlags = D3D12_TILE_COPY_FLAG_NO_HAZARD |
        D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE |
        D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER;
    if (!texture || !texture->tiled || !tileBuffer || !tileBuffer->IsBuffer() ||
        !coordinate || !region || offset % D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT ||
        (static_cast<UINT>(flags) & ~supportedFlags) != 0) {
        Log("Rejected invalid CopyTiles request");
        return;
    }
    if ((flags & D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE) &&
        (flags & D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER)) {
        Log("Rejected CopyTiles request with conflicting copy directions");
        return;
    }
    D3D11_TILED_RESOURCE_COORDINATE nativeCoordinate{
        coordinate->X, coordinate->Y, coordinate->Z, coordinate->Subresource
    };
    D3D11_TILE_REGION_SIZE nativeRegion{
        region->NumTiles, region->UseBox, region->Width, region->Height, region->Depth
    };
    Ptr<IUnknown> keepTexture(static_cast<ID3D12Resource*>(texture));
    Ptr<IUnknown> keepBuffer(static_cast<ID3D12Resource*>(tileBuffer));
    Add([keepTexture, keepBuffer, nativeCoordinate, nativeRegion, offset, flags](ExecState& state) {
        auto* srcTexture = dynamic_cast<Resource*>(keepTexture.Get());
        auto* srcBuffer = dynamic_cast<Resource*>(keepBuffer.Get());
        if (!state.dev->ctx2 || !srcTexture || !srcBuffer || !srcBuffer->buf) {
            Log("CopyTiles skipped: D3D11 context2 or backing resource unavailable");
            return;
        }
        state.dev->ctx2->CopyTiles(srcTexture->Raw(), &nativeCoordinate, &nativeRegion,
            srcBuffer->buf.Get(), offset, static_cast<UINT>(flags));
        Log("CopyTiles submitted offset=%llu flags=0x%x",
            static_cast<unsigned long long>(offset), static_cast<UINT>(flags));
    });
}

void STDMETHODCALLTYPE CommandList::ResolveSubresource(ID3D12Resource* destination, UINT destinationSubresource,
    ID3D12Resource* source, UINT sourceSubresource, DXGI_FORMAT format) {
    Resource* dst = AsResource(destination);
    Resource* src = AsResource(source);
    if (!dst || !src || destinationSubresource >= dst->NumSubresources() ||
        sourceSubresource >= src->NumSubresources()) {
        Log("Rejected invalid ResolveSubresource request");
        return;
    }
    Ptr<IUnknown> keepDst(static_cast<ID3D12Resource*>(dst));
    Ptr<IUnknown> keepSrc(static_cast<ID3D12Resource*>(src));
    Add([keepDst, keepSrc, destinationSubresource, sourceSubresource, format](ExecState& state) {
        auto* dstResource = dynamic_cast<Resource*>(keepDst.Get());
        auto* srcResource = dynamic_cast<Resource*>(keepSrc.Get());
        if (dstResource && srcResource)
            state.c->ResolveSubresource(dstResource->Raw(), destinationSubresource,
                                        srcResource->Raw(), sourceSubresource, format);
    });
}

void STDMETHODCALLTYPE CommandList::IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) {
    Add([topology](ExecState& state) { state.c->IASetPrimitiveTopology(static_cast<D3D11_PRIMITIVE_TOPOLOGY>(topology)); });
}

void STDMETHODCALLTYPE CommandList::RSSetViewports(UINT count, const D3D12_VIEWPORT* viewports) {
    if (!viewports || count > D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) return;
    std::vector<D3D11_VIEWPORT> native(count);
    for (UINT i = 0; i < count; ++i) {
        native[i].TopLeftX = viewports[i].TopLeftX;
        native[i].TopLeftY = viewports[i].TopLeftY;
        native[i].Width = viewports[i].Width;
        native[i].Height = viewports[i].Height;
        native[i].MinDepth = viewports[i].MinDepth;
        native[i].MaxDepth = viewports[i].MaxDepth;
    }
    std::vector<D3D12_VIEWPORT> diagnosticViewports(viewports, viewports + count);
    Add([native = std::move(native), diagnosticViewports = std::move(diagnosticViewports)](ExecState& state) {
        state.c->RSSetViewports(static_cast<UINT>(native.size()), native.data());
        state.numViewports = static_cast<UINT>(diagnosticViewports.size());
        std::copy(diagnosticViewports.begin(), diagnosticViewports.end(), state.viewports.begin());
    });
}

void STDMETHODCALLTYPE CommandList::RSSetScissorRects(UINT count, const D3D12_RECT* rects) {
    if (!rects || count > D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) return;
    std::vector<D3D11_RECT> native(rects, rects + count);
    Add([native = std::move(native)](ExecState& state) {
        state.c->RSSetScissorRects(static_cast<UINT>(native.size()), native.data());
    });
}

void STDMETHODCALLTYPE CommandList::OMSetBlendFactor(const FLOAT factor[4]) {
    if (!factor) return;
    float copy[4] = {factor[0], factor[1], factor[2], factor[3]};
    Add([copy](ExecState& state) {
        memcpy(state.blend, copy, sizeof(copy));
        state.ApplyOM();
    });
}

void STDMETHODCALLTYPE CommandList::OMSetStencilRef(UINT reference) {
    Add([reference](ExecState& state) {
        state.stencilRef = reference;
        state.ApplyOM();
    });
}

void STDMETHODCALLTYPE CommandList::SetPipelineState(ID3D12PipelineState* pipelineState) {
    Ptr<IUnknown> keep = pipelineState ? Ptr<IUnknown>(pipelineState) : Ptr<IUnknown>();
    Add([keep](ExecState& state) {
        if (auto* pipeline = dynamic_cast<PipelineState*>(keep.Get())) ApplyPipeline(state, pipeline);
        else if (keep) Unimpl("ID3D12GraphicsCommandList::SetPipelineState object");
    });
}

void STDMETHODCALLTYPE CommandList::ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER* barriers) {
    if (count && !barriers) {
        Log("ResourceBarrier received a null barrier array");
        return;
    }
    for (UINT i = 0; i < count; ++i) {
        if (barriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            Unimpl("ID3D12GraphicsCommandList::ResourceBarrier transition (ignored)");
        } else if (barriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING ||
                   barriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_UAV) {
            Unimpl("ID3D12GraphicsCommandList::ResourceBarrier aliasing/UAV");
        } else {
            Unimpl("ID3D12GraphicsCommandList::ResourceBarrier unknown type");
        }
    }
}

void STDMETHODCALLTYPE CommandList::ExecuteBundle(ID3D12GraphicsCommandList* list) {
    auto* bundle = dynamic_cast<CommandList*>(list);
    if (!bundle) {
        Unimpl("ID3D12GraphicsCommandList::ExecuteBundle");
        return;
    }
    Ptr<IUnknown> keep(static_cast<ID3D12GraphicsCommandList*>(bundle));
    Add([keep](ExecState& state) {
        auto* commandList = dynamic_cast<CommandList*>(keep.Get());
        if (commandList && commandList->closed) commandList->Run(state);
    });
}

void STDMETHODCALLTYPE CommandList::SetComputeRootSignature(ID3D12RootSignature* signature) {
    comp.rs = dynamic_cast<RootSignature*>(signature);
    comp.keep = signature ? Ptr<IUnknown>(signature) : Ptr<IUnknown>();
    comp.args.clear();
    comp.snap.reset();
}

void STDMETHODCALLTYPE CommandList::SetGraphicsRootSignature(ID3D12RootSignature* signature) {
    gfx.rs = dynamic_cast<RootSignature*>(signature);
    gfx.keep = signature ? Ptr<IUnknown>(signature) : Ptr<IUnknown>();
    gfx.args.clear();
    gfx.snap.reset();
}

void STDMETHODCALLTYPE CommandList::SetComputeRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    RootArg arg{}; arg.kind = 5; arg.value = handle.ptr;
    UINT descriptorIndex = 0;
    if (auto* heap = FindHeapByGpu(handle.ptr, &descriptorIndex)) arg.keep = Ptr<IUnknown>(static_cast<ID3D12DescriptorHeap*>(heap));
    SetRootArg(comp, index, std::move(arg));
}
void STDMETHODCALLTYPE CommandList::SetGraphicsRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    RootArg arg{}; arg.kind = 5; arg.value = handle.ptr;
    UINT descriptorIndex = 0;
    if (auto* heap = FindHeapByGpu(handle.ptr, &descriptorIndex)) arg.keep = Ptr<IUnknown>(static_cast<ID3D12DescriptorHeap*>(heap));
    SetRootArg(gfx, index, std::move(arg));
}
void STDMETHODCALLTYPE CommandList::SetComputeRoot32BitConstant(UINT index, UINT value, UINT offset) {
    SetRootConstants(comp, index, 1, &value, offset);
}
void STDMETHODCALLTYPE CommandList::SetGraphicsRoot32BitConstant(UINT index, UINT value, UINT offset) {
    SetRootConstants(gfx, index, 1, &value, offset);
}
void STDMETHODCALLTYPE CommandList::SetComputeRoot32BitConstants(UINT index, UINT count, const void* values, UINT offset) {
    SetRootConstants(comp, index, count, values, offset);
}
void STDMETHODCALLTYPE CommandList::SetGraphicsRoot32BitConstants(UINT index, UINT count, const void* values, UINT offset) {
    SetRootConstants(gfx, index, count, values, offset);
}

void CommandList::SetRootConstants(BindState& bind, UINT index, UINT count, const void* values, UINT offset) {
    if (!values || !count || !bind.rs || index >= bind.rs->params.size() ||
        bind.rs->params[index].type != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS ||
        offset > bind.rs->params[index].num32 || count > bind.rs->params[index].num32 - offset) {
        Log("Rejected invalid root-constant write at parameter %u", index);
        return;
    }
    RootArg arg{};
    if (index < bind.args.size() && bind.args[index].kind == 1) arg = bind.args[index];
    if (arg.consts.size() < bind.rs->params[index].num32)
        arg.consts.resize(bind.rs->params[index].num32, 0);
    std::copy_n(static_cast<const UINT*>(values), count, arg.consts.begin() + offset);
    arg.kind = 1;
    arg.value = 0;
    SetRootArg(bind, index, std::move(arg));
}

namespace {
void SetRootAddress(BindState& state, UINT index, UINT64 address, uint8_t kind) {
    RootArg arg{};
    arg.kind = kind;
    arg.value = address;
    UINT64 offset = 0;
    if (Resource* resource = ResolveVA(address, &offset))
        arg.keep = Ptr<IUnknown>(static_cast<ID3D12Resource*>(resource));
    else
        Log("Root descriptor address 0x%llx does not resolve to a live resource",
            static_cast<unsigned long long>(address));
    if (state.args.size() <= index) state.args.resize(static_cast<size_t>(index) + 1);
    state.args[index] = std::move(arg);
    state.snap.reset();
}
}

void STDMETHODCALLTYPE CommandList::SetComputeRootConstantBufferView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) { SetRootAddress(comp, i, a, 2); }
void STDMETHODCALLTYPE CommandList::SetGraphicsRootConstantBufferView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) { SetRootAddress(gfx, i, a, 2); }
void STDMETHODCALLTYPE CommandList::SetComputeRootShaderResourceView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) { SetRootAddress(comp, i, a, 3); }
void STDMETHODCALLTYPE CommandList::SetGraphicsRootShaderResourceView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) { SetRootAddress(gfx, i, a, 3); }
void STDMETHODCALLTYPE CommandList::SetComputeRootUnorderedAccessView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) { SetRootAddress(comp, i, a, 4); }
void STDMETHODCALLTYPE CommandList::SetGraphicsRootUnorderedAccessView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) { SetRootAddress(gfx, i, a, 4); }

void STDMETHODCALLTYPE CommandList::IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view) {
    TraceApi("CommandList::IASetIndexBuffer");
    if (!view) return;
    UINT64 offset = 0;
    Resource* resource = ResolveVA(view->BufferLocation, &offset);
    if (!resource || offset > UINT_MAX) return;
    Ptr<ID3D11Buffer> buffer(resource->GpuBuffer());
    Ptr<ID3D12Resource> resourceRef(resource);
    const UINT nativeOffset = static_cast<UINT>(offset);
    const DXGI_FORMAT format = view->Format;
    const UINT viewSize = view->SizeInBytes;
    Add([buffer, resourceRef, nativeOffset, format, viewSize](ExecState& state) {
        auto* uploadResource = dynamic_cast<Resource*>(resourceRef.Get());
        if (uploadResource && uploadResource->heap == D3D12_HEAP_TYPE_UPLOAD)
            uploadResource->SyncToGpu(nativeOffset, viewSize);
        state.c->IASetIndexBuffer(buffer.Get(), format, nativeOffset);
        state.hasIndexBuffer = buffer.Get() != nullptr;
        state.indexBufferResource = resourceRef;
        state.indexBufferViewSize = viewSize;
    });
}

void STDMETHODCALLTYPE CommandList::IASetVertexBuffers(UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW* views) {
    TraceApi("CommandList::IASetVertexBuffers");
    if (!views || startSlot + count > D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) return;
    std::vector<Ptr<ID3D11Buffer>> buffers(count);
    std::vector<Ptr<ID3D12Resource>> resources(count);
    std::vector<UINT> strides(count), offsets(count);
    std::vector<UINT64> resourceOffsets(count), sizes(count);
    for (UINT i = 0; i < count; ++i) {
        UINT64 offset = 0;
        Resource* resource = ResolveVA(views[i].BufferLocation, &offset);
        if (!resource || offset > UINT_MAX) return;
        buffers[i] = Ptr<ID3D11Buffer>(resource->GpuBuffer());
        resources[i] = Ptr<ID3D12Resource>(resource);
        strides[i] = views[i].StrideInBytes;
        offsets[i] = static_cast<UINT>(offset);
        resourceOffsets[i] = offset;
        sizes[i] = views[i].SizeInBytes;
    }
    Add([startSlot, buffers = std::move(buffers), resources = std::move(resources),
        strides = std::move(strides), offsets = std::move(offsets),
        resourceOffsets = std::move(resourceOffsets), sizes = std::move(sizes)](ExecState& state) {
        std::vector<ID3D11Buffer*> raw;
        raw.reserve(buffers.size());
        for (size_t i = 0; i < buffers.size(); ++i) {
            auto* uploadResource = dynamic_cast<Resource*>(resources[i].Get());
            if (uploadResource && uploadResource->heap == D3D12_HEAP_TYPE_UPLOAD)
                uploadResource->SyncToGpu(resourceOffsets[i], sizes[i]);
            raw.push_back(buffers[i].Get());
            state.vertexBuffers[startSlot + static_cast<UINT>(i)] = buffers[i].Get() != nullptr;
        }
        state.hasVertexBuffer = std::any_of(state.vertexBuffers.begin(), state.vertexBuffers.end(),
            [](bool bound) { return bound; });
        state.c->IASetVertexBuffers(startSlot, static_cast<UINT>(raw.size()), raw.data(), strides.data(), offsets.data());
    });
}

void STDMETHODCALLTYPE CommandList::OMSetRenderTargets(UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* handles,
    BOOL single, const D3D12_CPU_DESCRIPTOR_HANDLE* depthHandle) {
    TraceApi("CommandList::OMSetRenderTargets");
    if (count > 8 || (count && !handles)) return;
    std::vector<Ptr<ID3D11RenderTargetView>> views(count);
    std::vector<Ptr<ID3D11Resource>> resources(count);
    std::vector<bool> backbuffers(count, false);
    for (UINT i = 0; i < count; ++i) {
        auto handle = handles[i];
        if (single) handle.ptr += static_cast<SIZE_T>(i) * kDescStride;
        Descriptor descriptor = DescriptorAt(handle, DKind::RTV);
        views[i] = descriptor.rtv;
        if (descriptor.res && descriptor.res->tex)
            resources[i] = Ptr<ID3D11Resource>(descriptor.res->tex.Get());
        backbuffers[i] = descriptor.res && descriptor.res->backbuffer;
    }
    Ptr<ID3D11DepthStencilView> depth;
    if (depthHandle) depth = DescriptorAt(*depthHandle, DKind::DSV).dsv;
    Add([views = std::move(views), resources = std::move(resources),
        backbuffers = std::move(backbuffers), depth](ExecState& state) {
        std::vector<ID3D11RenderTargetView*> raw;
        raw.reserve(views.size());
        for (const auto& view : views) raw.push_back(view.Get());
        state.c->OMSetRenderTargets(static_cast<UINT>(raw.size()), raw.data(), depth.Get());
        for (UINT i = 0; i < ARRAYSIZE(state.rtvs); ++i) {
            state.rtvs[i].Reset();
            state.rtvResources[i].Reset();
        }
        state.numRtv = static_cast<UINT>(views.size());
        state.dsv = depth;
        state.rtvBackbuffer = false;
        for (UINT i = 0; i < views.size() && i < 8; ++i) {
            state.rtvs[i] = views[i];
            state.rtvResources[i] = resources[i];
        }
        for (size_t i = 0; i < backbuffers.size(); ++i)
            state.rtvBackbuffer = state.rtvBackbuffer || backbuffers[i];
    });
}

void STDMETHODCALLTYPE CommandList::ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE handle,
    D3D12_CLEAR_FLAGS flags, FLOAT depth, UINT8 stencil, UINT rectCount, const D3D12_RECT*) {
    if (rectCount) Unimpl("ID3D12GraphicsCommandList::ClearDepthStencilView rectangles");
    Descriptor descriptor = DescriptorAt(handle, DKind::DSV);
    if (!descriptor.dsv) return;
    UINT nativeFlags = 0;
    if (flags & D3D12_CLEAR_FLAG_DEPTH) nativeFlags |= D3D11_CLEAR_DEPTH;
    if (flags & D3D12_CLEAR_FLAG_STENCIL) nativeFlags |= D3D11_CLEAR_STENCIL;
    Add([descriptor, nativeFlags, depth, stencil](ExecState& state) {
        state.c->ClearDepthStencilView(descriptor.dsv.Get(), nativeFlags, depth, stencil);
    });
}

void STDMETHODCALLTYPE CommandList::ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE handle,
    const FLOAT color[4], UINT rectCount, const D3D12_RECT*) {
    if (rectCount) Unimpl("ID3D12GraphicsCommandList::ClearRenderTargetView rectangles");
    if (!color) return;
    Descriptor descriptor = DescriptorAt(handle, DKind::RTV);
    if (!descriptor.rtv) return;
    float copy[4] = {color[0], color[1], color[2], color[3]};
    Ptr<ID3D11Resource> target;
    if (descriptor.res && descriptor.res->tex)
        target = Ptr<ID3D11Resource>(descriptor.res->tex.Get());
    Add([descriptor, copy, target](ExecState& state) {
        state.c->ClearRenderTargetView(descriptor.rtv.Get(), copy);
        char context[128]{};
        snprintf(context, sizeof(context), "ClearRenderTargetView color=(%.3f,%.3f,%.3f,%.3f)",
            copy[0], copy[1], copy[2], copy[3]);
        TrackTargetOperation(target.Get(), context, TargetHistoryKind::Clear);
    });
}

void STDMETHODCALLTYPE CommandList::ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle,
    D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const UINT values[4], UINT rectCount, const D3D12_RECT*) {
    if (rectCount) Unimpl("ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint rectangles");
    if (!values) return;
    Descriptor descriptor = DescriptorAt(gpuHandle, DKind::UAV);
    if (!descriptor.uav) return;
    UINT copy[4] = {values[0], values[1], values[2], values[3]};
    Add([descriptor, copy](ExecState& state) { state.c->ClearUnorderedAccessViewUint(descriptor.uav.Get(), copy); });
}

void STDMETHODCALLTYPE CommandList::ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle,
    D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const FLOAT values[4], UINT rectCount, const D3D12_RECT*) {
    if (rectCount) Unimpl("ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat rectangles");
    if (!values) return;
    Descriptor descriptor = DescriptorAt(gpuHandle, DKind::UAV);
    if (!descriptor.uav) return;
    float copy[4] = {values[0], values[1], values[2], values[3]};
    Add([descriptor, copy](ExecState& state) { state.c->ClearUnorderedAccessViewFloat(descriptor.uav.Get(), copy); });
}

void STDMETHODCALLTYPE CommandList::BeginQuery(ID3D12QueryHeap* heapIn, D3D12_QUERY_TYPE type, UINT index) {
    auto* heap = dynamic_cast<QueryHeapObj*>(heapIn);
    if (!heap || index >= heap->desc.Count ||
        heap->desc.Type != D3D12_QUERY_HEAP_TYPE_OCCLUSION ||
        (type != D3D12_QUERY_TYPE_OCCLUSION && type != D3D12_QUERY_TYPE_BINARY_OCCLUSION)) {
        Log("BeginQuery rejected heap=%p type=%u index=%u",
            heapIn, static_cast<unsigned>(type), index);
        return;
    }

    Ptr<ID3D12QueryHeap> keep(heapIn);
    Add([keep, index](ExecState& state) {
        auto* queryHeap = dynamic_cast<QueryHeapObj*>(keep.Get());
        if (!queryHeap || !state.dev || !state.c) return;
        Ptr<ID3D11Query> query;
        {
            std::lock_guard<std::mutex> lock(queryHeap->mutex);
            if (!queryHeap->queries[index]) {
                D3D11_QUERY_DESC desc{D3D11_QUERY_OCCLUSION, 0};
                const HRESULT result = state.dev->d11->CreateQuery(&desc, queryHeap->queries[index].Put());
                if (FAILED(result)) {
                    Log("BeginQuery could not create D3D11 occlusion query: hr=0x%08lx", result);
                    return;
                }
            }
            query = queryHeap->queries[index];
        }
        std::lock_guard<std::recursive_mutex> lock(state.dev->ctxLock);
        state.c->Begin(query.Get());
    });
}

void STDMETHODCALLTYPE CommandList::EndQuery(ID3D12QueryHeap* heapIn, D3D12_QUERY_TYPE type, UINT index) {
    auto* heap = dynamic_cast<QueryHeapObj*>(heapIn);
    if (!heap || index >= heap->desc.Count) {
        Log("EndQuery rejected heap=%p type=%u index=%u",
            heapIn, static_cast<unsigned>(type), index);
        return;
    }

    const bool timestamp = type == D3D12_QUERY_TYPE_TIMESTAMP;
    const bool occlusion = type == D3D12_QUERY_TYPE_OCCLUSION ||
        type == D3D12_QUERY_TYPE_BINARY_OCCLUSION;
    if ((timestamp && heap->desc.Type != D3D12_QUERY_HEAP_TYPE_TIMESTAMP) ||
        (occlusion && heap->desc.Type != D3D12_QUERY_HEAP_TYPE_OCCLUSION) ||
        (!timestamp && !occlusion)) {
        Log("EndQuery rejected incompatible heap/type heap-type=%u query-type=%u",
            static_cast<unsigned>(heap->desc.Type), static_cast<unsigned>(type));
        return;
    }

    Ptr<ID3D12QueryHeap> keep(heapIn);
    Add([keep, index, timestamp](ExecState& state) {
        auto* queryHeap = dynamic_cast<QueryHeapObj*>(keep.Get());
        if (!queryHeap || !state.dev || !state.c) return;
        if (timestamp) {
            std::lock_guard<std::mutex> lock(queryHeap->mutex);
            queryHeap->timestamps[index] = CurrentGpuTimestampNs();
            return;
        }
        Ptr<ID3D11Query> query;
        {
            std::lock_guard<std::mutex> lock(queryHeap->mutex);
            query = queryHeap->queries[index];
        }
        if (!query) {
            Log("EndQuery has no matching D3D11 occlusion query at index=%u", index);
            return;
        }
        std::lock_guard<std::recursive_mutex> lock(state.dev->ctxLock);
        state.c->End(query.Get());
    });
}

void STDMETHODCALLTYPE CommandList::ResolveQueryData(ID3D12QueryHeap* heapIn, D3D12_QUERY_TYPE type,
    UINT startIndex, UINT numQueries, ID3D12Resource* dstIn, UINT64 alignedDstBufferOffset) {
    auto* heap = dynamic_cast<QueryHeapObj*>(heapIn);
    auto* dst = dynamic_cast<Resource*>(dstIn);
    if (!heap || !dst || !dst->IsBuffer() || !numQueries ||
        startIndex >= heap->desc.Count || numQueries > heap->desc.Count - startIndex ||
        alignedDstBufferOffset > dst->desc.Width || (alignedDstBufferOffset & 7)) {
        Log("ResolveQueryData rejected heap=%p destination=%p start=%u count=%u offset=%llu",
            heapIn, dstIn, startIndex, numQueries,
            static_cast<unsigned long long>(alignedDstBufferOffset));
        return;
    }

    size_t elementSize = sizeof(UINT64);
    if (type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS) {
        elementSize = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
    } else if (type >= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 &&
               type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3) {
        elementSize = sizeof(D3D12_QUERY_DATA_SO_STATISTICS);
    }
    const size_t payloadSize = static_cast<size_t>(numQueries) * elementSize;
    if (payloadSize > dst->desc.Width - alignedDstBufferOffset) {
        Log("ResolveQueryData result exceeds destination buffer");
        return;
    }

    Ptr<ID3D12QueryHeap> heapKeep(heapIn);
    Ptr<ID3D12Resource> dstKeep(dstIn);
    Add([heapKeep, dstKeep, type, startIndex, numQueries, alignedDstBufferOffset,
         payloadSize](ExecState& state) {
        auto* queryHeap = dynamic_cast<QueryHeapObj*>(heapKeep.Get());
        auto* destination = dynamic_cast<Resource*>(dstKeep.Get());
        if (!queryHeap || !destination || !state.dev || !state.c) return;

        std::vector<uint8_t> payload(payloadSize, 0);
        if (type == D3D12_QUERY_TYPE_TIMESTAMP) {
            std::lock_guard<std::mutex> lock(queryHeap->mutex);
            auto* values = reinterpret_cast<UINT64*>(payload.data());
            for (UINT i = 0; i < numQueries; ++i) {
                UINT64 value = queryHeap->timestamps[startIndex + i];
                if (!value) value = CurrentGpuTimestampNs();
                values[i] = value;
            }
        } else if (type == D3D12_QUERY_TYPE_OCCLUSION ||
                   type == D3D12_QUERY_TYPE_BINARY_OCCLUSION) {
            std::lock_guard<std::recursive_mutex> contextLock(state.dev->ctxLock);
            state.c->Flush();
            auto* values = reinterpret_cast<UINT64*>(payload.data());
            for (UINT i = 0; i < numQueries; ++i) {
                Ptr<ID3D11Query> query;
                {
                    std::lock_guard<std::mutex> lock(queryHeap->mutex);
                    query = queryHeap->queries[startIndex + i];
                }
                if (!query) {
                    Log("ResolveQueryData has no D3D11 occlusion query at index=%u", startIndex + i);
                    continue;
                }
                UINT64 value = 0;
                const HRESULT result = WaitForQuery(state.dev, query.Get(), &value, sizeof(value));
                if (FAILED(result)) {
                    Log("ResolveQueryData occlusion query failed at index=%u: hr=0x%08lx",
                        startIndex + i, result);
                    continue;
                }
                values[i] = type == D3D12_QUERY_TYPE_BINARY_OCCLUSION ? (value ? 1 : 0) : value;
            }
        } else if (type != D3D12_QUERY_TYPE_PIPELINE_STATISTICS &&
                   (type < D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 ||
                    type > D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3)) {
            Log("ResolveQueryData query type %u is not implemented; writing zero results",
                static_cast<unsigned>(type));
        }

        if (!destination->shadow.empty() &&
            alignedDstBufferOffset <= destination->shadow.size() &&
            payloadSize <= destination->shadow.size() - alignedDstBufferOffset) {
            std::memcpy(destination->shadow.data() + alignedDstBufferOffset,
                payload.data(), payloadSize);
        }
        if (destination->buf) {
            std::lock_guard<std::recursive_mutex> lock(state.dev->ctxLock);
            D3D11_BOX box{
                static_cast<UINT>(alignedDstBufferOffset), 0, 0,
                static_cast<UINT>(alignedDstBufferOffset + payloadSize), 1, 1
            };
            state.c->UpdateSubresource(destination->buf.Get(), 0, &box, payload.data(),
                static_cast<UINT>(payloadSize), static_cast<UINT>(payloadSize));
        }
    });
}

void STDMETHODCALLTYPE CommandQueue::ExecuteCommandLists(UINT count, ID3D12CommandList* const* lists) {
    TraceApi("CommandQueue::ExecuteCommandLists");
    const bool trace = Cfg().diagnosticRenderTrace;
    if (trace)
        TraceStats().execCmdLists.fetch_add(1, std::memory_order_relaxed);
    if (!lists || !count) return;
    std::lock_guard<std::recursive_mutex> lock(dev->ctxLock);
    ExecState state{};
    state.dev = dev;
    state.c = dev->ctx.Get();
    for (UINT i = 0; i < count; ++i) {
        auto* commandList = dynamic_cast<CommandList*>(lists[i]);
        if (!commandList || !commandList->closed) {
            Log("Ignored invalid or open command list at queue submission");
            continue;
        }
        if (trace) {
            state.drawsExecuted = 0;
            state.drawsSkipped = 0;
            state.rtvBound = 0;
            state.backbufferDraws = 0;
            state.copyToBackbuffer = 0;
        }
        commandList->Run(state);
        if (trace) {
            Log("[CMDLIST_SUMMARY] list=%u draws_executed=%llu draws_skipped=%llu "
                "rtv_bound=%llu backbuffer_draws=%llu copy_to_bb=%llu",
                i,
                static_cast<unsigned long long>(state.drawsExecuted),
                static_cast<unsigned long long>(state.drawsSkipped),
                static_cast<unsigned long long>(state.rtvBound),
                static_cast<unsigned long long>(state.backbufferDraws),
                static_cast<unsigned long long>(state.copyToBackbuffer));
        }
    }
}

void STDMETHODCALLTYPE CommandQueue::UpdateTileMappings(ID3D12Resource* tiledResource,
    UINT regionCount, const D3D12_TILED_RESOURCE_COORDINATE* coordinates,
    const D3D12_TILE_REGION_SIZE* regions, ID3D12Heap* sourceHeap, UINT rangeCount,
    const D3D12_TILE_RANGE_FLAGS* rangeFlags, const UINT* heapOffsets,
    const UINT* rangeTileCounts, D3D12_TILE_MAPPING_FLAGS flags) {
    TraceApi("CommandQueue::UpdateTileMappings");
    auto* texture = AsResource(tiledResource);
    auto* heap = sourceHeap ? dynamic_cast<HeapObj*>(sourceHeap) : nullptr;
    const UINT supportedFlags = D3D12_TILE_MAPPING_FLAG_NO_HAZARD;
    if (!dev->ctx2 || !texture || !texture->tiled || !regionCount || !coordinates || !regions ||
        !rangeCount || !rangeFlags || !rangeTileCounts ||
        (static_cast<UINT>(flags) & ~supportedFlags) != 0) {
        Log("Rejected invalid UpdateTileMappings request");
        return;
    }
    if (sourceHeap && !heap) {
        Log("Rejected UpdateTileMappings request for an unknown heap");
        return;
    }

    std::vector<D3D11_TILED_RESOURCE_COORDINATE> nativeCoordinates;
    std::vector<D3D11_TILE_REGION_SIZE> nativeRegions;
    std::vector<UINT> nativeRangeFlags;
    std::vector<UINT> nativeHeapOffsets;
    try {
        nativeCoordinates.reserve(regionCount);
        nativeRegions.reserve(regionCount);
        nativeRangeFlags.reserve(rangeCount);
        if (heapOffsets) nativeHeapOffsets.assign(heapOffsets, heapOffsets + rangeCount);
        for (UINT i = 0; i < regionCount; ++i) {
            nativeCoordinates.push_back({coordinates[i].X, coordinates[i].Y,
                coordinates[i].Z, coordinates[i].Subresource});
            nativeRegions.push_back({regions[i].NumTiles, regions[i].UseBox,
                regions[i].Width, regions[i].Height, regions[i].Depth});
        }
        for (UINT i = 0; i < rangeCount; ++i) {
            const UINT range = static_cast<UINT>(rangeFlags[i]);
            if (range != D3D12_TILE_RANGE_FLAG_NONE && range != D3D12_TILE_RANGE_FLAG_NULL &&
                range != D3D12_TILE_RANGE_FLAG_SKIP &&
                range != D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE) {
                Log("Rejected UpdateTileMappings request with unsupported range flag 0x%x", range);
                return;
            }
            nativeRangeFlags.push_back(range);
            if (!heap && range != D3D12_TILE_RANGE_FLAG_NULL &&
                range != D3D12_TILE_RANGE_FLAG_SKIP) {
                Log("Rejected UpdateTileMappings request that maps tiles without a tile-pool heap");
                return;
            }
        }
    } catch (const std::bad_alloc&) {
        Log("UpdateTileMappings could not allocate mapping arrays");
        return;
    }

    Ptr<ID3D11Buffer> tilePool;
    if (heap) {
        ID3D11Buffer* nativePool = nullptr;
        const HRESULT poolResult = heap->GetTilePool(&nativePool);
        if (FAILED(poolResult)) {
            Log("UpdateTileMappings could not create tile pool: hr=0x%08lx", poolResult);
            return;
        }
        tilePool = Ptr<ID3D11Buffer>::Attach(nativePool);
    }

    std::lock_guard<std::recursive_mutex> lock(dev->ctxLock);
    const HRESULT result = dev->ctx2->UpdateTileMappings(texture->Raw(), regionCount,
        nativeCoordinates.data(), nativeRegions.data(), tilePool.Get(), rangeCount,
        nativeRangeFlags.data(), nativeHeapOffsets.empty() ? nullptr : nativeHeapOffsets.data(),
        rangeTileCounts, static_cast<UINT>(flags));
    if (FAILED(result))
        Log("UpdateTileMappings failed: hr=0x%08lx", result);
    else
        Log("UpdateTileMappings completed resource-regions=%u heap-ranges=%u tile-pool=%s",
            regionCount, rangeCount, tilePool ? "present" : "none");
}

void STDMETHODCALLTYPE CommandQueue::CopyTileMappings(ID3D12Resource* destination,
    const D3D12_TILED_RESOURCE_COORDINATE* destinationCoordinate, ID3D12Resource* source,
    const D3D12_TILED_RESOURCE_COORDINATE* sourceCoordinate,
    const D3D12_TILE_REGION_SIZE* region, D3D12_TILE_MAPPING_FLAGS flags) {
    TraceApi("CommandQueue::CopyTileMappings");
    auto* dst = AsResource(destination);
    auto* src = AsResource(source);
    if (!dev->ctx2 || !dst || !src || !dst->tiled || !src->tiled ||
        !destinationCoordinate || !sourceCoordinate || !region ||
        (static_cast<UINT>(flags) & ~D3D12_TILE_MAPPING_FLAG_NO_HAZARD) != 0) {
        Log("Rejected invalid CopyTileMappings request");
        return;
    }
    const D3D11_TILED_RESOURCE_COORDINATE nativeDestination{
        destinationCoordinate->X, destinationCoordinate->Y,
        destinationCoordinate->Z, destinationCoordinate->Subresource
    };
    const D3D11_TILED_RESOURCE_COORDINATE nativeSource{
        sourceCoordinate->X, sourceCoordinate->Y, sourceCoordinate->Z,
        sourceCoordinate->Subresource
    };
    const D3D11_TILE_REGION_SIZE nativeRegion{
        region->NumTiles, region->UseBox, region->Width, region->Height, region->Depth
    };
    std::lock_guard<std::recursive_mutex> lock(dev->ctxLock);
    const HRESULT result = dev->ctx2->CopyTileMappings(dst->Raw(), &nativeDestination,
        src->Raw(), &nativeSource, &nativeRegion, static_cast<UINT>(flags));
    if (FAILED(result))
        Log("CopyTileMappings failed: hr=0x%08lx", result);
    else
        Log("CopyTileMappings completed");
}

HRESULT STDMETHODCALLTYPE CommandQueue::Signal(ID3D12Fence* sourceFence, UINT64 value) {
    TraceApi("CommandQueue::Signal");
    if (Cfg().diagnosticRenderTrace)
        TraceStats().signalCount.fetch_add(1, std::memory_order_relaxed);
    auto* fence = AsFence(sourceFence);
    if (!fence) {
        if (Cfg().diagnosticRenderTrace)
            Log("[RENDER_TRACE] [tid=%lu] Signal invalid fence=%p value=%llu hr=0x%08lx",
                GetCurrentThreadId(), sourceFence, static_cast<unsigned long long>(value), E_INVALIDARG);
        return E_INVALIDARG;
    }
    if (Cfg().diagnosticRenderTrace)
        Log("[RENDER_TRACE] [tid=%lu] Signal begin fence=%p value=%llu",
            GetCurrentThreadId(), fence, static_cast<unsigned long long>(value));
    std::lock_guard<std::recursive_mutex> lock(dev->ctxLock);
    const HRESULT result = WaitForContext(dev);
    TraceApi("CommandQueue::Signal");
    if (SUCCEEDED(result)) fence->CompleteTo(value);
    const HRESULT signalResult = FAILED(result) ? result : S_OK;
    if (Cfg().diagnosticRenderTrace)
        Log("[RENDER_TRACE] [tid=%lu] Signal fence=%p value=%llu hr=0x%08lx",
            GetCurrentThreadId(), fence, static_cast<unsigned long long>(value), signalResult);
    return signalResult;
}

HRESULT STDMETHODCALLTYPE CommandQueue::Wait(ID3D12Fence* sourceFence, UINT64 value) {
    TraceApi("CommandQueue::Wait");
    const bool trace = Cfg().diagnosticRenderTrace;
    if (trace) TraceStats().waitCount.fetch_add(1, std::memory_order_relaxed);
    auto* fence = AsFence(sourceFence);
    if (!fence) {
        if (trace)
            Log("[RENDER_TRACE] [tid=%lu] Queue Wait invalid fence=%p expected=%llu hr=0x%08lx",
                GetCurrentThreadId(), sourceFence, static_cast<unsigned long long>(value), E_INVALIDARG);
        return E_INVALIDARG;
    }
    if (trace)
        Log("[RENDER_TRACE] [tid=%lu] Queue Wait begin fence=%p expected=%llu completed=%llu",
            GetCurrentThreadId(), fence, static_cast<unsigned long long>(value),
            static_cast<unsigned long long>(fence->GetCompletedValue()));
    if (fence->GetCompletedValue() >= value) return S_OK;
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) return HRESULT_FROM_WIN32(GetLastError());
    const HRESULT result = fence->SetEventOnCompletion(value, eventHandle);
    HRESULT waitResultHr = result;
    if (SUCCEEDED(result)) {
        TraceApi("CommandQueue::Wait");
        if (trace) {
            for (;;) {
                const DWORD waitResult = WaitForSingleObject(eventHandle, 2000);
                if (waitResult == WAIT_OBJECT_0) break;
                if (waitResult == WAIT_TIMEOUT) {
                    Log("[RENDER_TRACE] [tid=%lu] Fence wait STILL WAITING fence=%p expected=%llu completed=%llu",
                        GetCurrentThreadId(), fence, static_cast<unsigned long long>(value),
                        static_cast<unsigned long long>(fence->GetCompletedValue()));
                    continue;
                }
                const DWORD error = GetLastError();
                waitResultHr = waitResult == WAIT_FAILED
                    ? HRESULT_FROM_WIN32(error) : E_UNEXPECTED;
                break;
            }
        } else {
            const DWORD waitResult = WaitForSingleObject(eventHandle, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                const DWORD error = GetLastError();
                waitResultHr = waitResult == WAIT_FAILED
                    ? HRESULT_FROM_WIN32(error) : E_UNEXPECTED;
            }
        }
    }
    CloseHandle(eventHandle);
    if (trace)
        Log("[RENDER_TRACE] [tid=%lu] Queue Wait fence=%p expected=%llu completed=%llu hr=0x%08lx",
            GetCurrentThreadId(), fence, static_cast<unsigned long long>(value),
            static_cast<unsigned long long>(fence->GetCompletedValue()), waitResultHr);
    return waitResultHr;
}

HRESULT STDMETHODCALLTYPE CommandQueue::GetClockCalibration(UINT64* gpuTimestamp,
    UINT64* cpuTimestamp) {
    D3D12ON11_TRACE_CALLER("CommandQueue::GetClockCalibration");
    if (!gpuTimestamp || !cpuTimestamp) {
        Log("GetClockCalibration rejected null output pointer");
        return E_INVALIDARG;
    }

    std::lock_guard<std::recursive_mutex> lock(dev->ctxLock);
    D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP, 0};
    Ptr<ID3D11Query> query;
    HRESULT result = dev->d11->CreateQuery(&desc, query.Put());
    if (FAILED(result)) {
        Log("GetClockCalibration could not create D3D11 timestamp query: hr=0x%08lx", result);
        return result;
    }

    dev->ctx->End(query.Get());
    LARGE_INTEGER cpuSample{};
    if (!QueryPerformanceCounter(&cpuSample)) {
        Log("GetClockCalibration QueryPerformanceCounter failed");
        return E_FAIL;
    }
    dev->ctx->Flush();

    UINT64 gpuSample = 0;
    result = WaitForQuery(dev, query.Get(), &gpuSample, sizeof(gpuSample));
    if (FAILED(result)) {
        Log("GetClockCalibration D3D11 timestamp query failed: hr=0x%08lx", result);
        return result;
    }

    *gpuTimestamp = gpuSample;
    *cpuTimestamp = static_cast<UINT64>(cpuSample.QuadPart);
    Log("GetClockCalibration result=S_OK gpu=%llu cpu-qpc=%llu",
        static_cast<unsigned long long>(*gpuTimestamp),
        static_cast<unsigned long long>(*cpuTimestamp));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CommandQueue::GetTimestampFrequency(UINT64* frequency) {
    D3D12ON11_TRACE_CALLER("CommandQueue::GetTimestampFrequency");
    if (!frequency) {
        Log("GetTimestampFrequency rejected null output pointer");
        return E_INVALIDARG;
    }

    std::lock_guard<std::recursive_mutex> lock(dev->ctxLock);
    const HRESULT result = GetDeviceTimestampFrequency(dev, frequency);
    Log("GetTimestampFrequency result=0x%08lx frequency=%llu", result,
        static_cast<unsigned long long>(SUCCEEDED(result) ? *frequency : 0));
    return result;
}
