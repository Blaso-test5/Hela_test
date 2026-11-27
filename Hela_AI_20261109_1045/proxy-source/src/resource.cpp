#include "types.h"

#include <limits>

namespace {

bool HasMemoryAccess(const void* address, size_t size, bool write) {
    if (!address || !size) return false;

    const uintptr_t maxAddress = std::numeric_limits<uintptr_t>::max();
    uintptr_t current = reinterpret_cast<uintptr_t>(address);
    if (size > maxAddress - current) return false;
    const uintptr_t end = current + size;

    while (current < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<const void*>(current), &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD)) {
            return false;
        }

        const DWORD protection = memory.Protect & 0xff;
        const bool accessible = write
            ? protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY
            : protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (!accessible) return false;

        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize > maxAddress - regionBase) return false;
        const uintptr_t regionEnd = regionBase + memory.RegionSize;
        if (regionEnd <= current) return false;
        current = std::min(end, regionEnd);
    }

    return true;
}

bool CopyBufferPrefix(ID3D11DeviceContext* context, ID3D11Buffer* destination,
    ID3D11Buffer* source, UINT byteWidth) {
    if (!context || !destination || !source || !byteWidth) return false;
    D3D11_BUFFER_DESC destinationDesc{};
    D3D11_BUFFER_DESC sourceDesc{};
    destination->GetDesc(&destinationDesc);
    source->GetDesc(&sourceDesc);
    if (byteWidth > destinationDesc.ByteWidth || byteWidth > sourceDesc.ByteWidth)
        return false;
    D3D11_BOX box{};
    box.right = byteWidth;
    box.bottom = 1;
    box.back = 1;
    context->CopySubresourceRegion(destination, 0, 0, 0, 0, source, 0, &box);
    return true;
}

UINT BindFlags(const D3D12_RESOURCE_DESC& desc) {
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
        UINT flags = D3D11_BIND_SHADER_RESOURCE;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) flags |= D3D11_BIND_RENDER_TARGET;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) flags |= D3D11_BIND_DEPTH_STENCIL;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) flags |= D3D11_BIND_UNORDERED_ACCESS;
        return flags;
    }

    UINT flags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
    if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) flags |= D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

HRESULT CreateBuffer(Resource* resource) {
    if (resource->desc.Width == 0 || resource->desc.Width > UINT_MAX) return E_INVALIDARG;
    if (resource->tiled) {
        if (resource->heap != D3D12_HEAP_TYPE_DEFAULT) return E_INVALIDARG;
        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
            bufferDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

        const UINT64 alignedRequested = AlignUp(resource->desc.Width,
            D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
        std::vector<UINT> candidates;
        if (alignedRequested <= 0x7fff0000ull)
            candidates.push_back(static_cast<UINT>(alignedRequested));
        constexpr UINT kStepBytes = 64u * 1024u * 1024u;
        const UINT maxCandidate = static_cast<UINT>(std::min<UINT64>(
            alignedRequested, 1024ull * 1024ull * 1024ull));
        for (UINT candidate = (maxCandidate / kStepBytes) * kStepBytes;
             candidate >= kStepBytes; candidate -= kStepBytes) {
            if (candidate < alignedRequested) candidates.push_back(candidate);
            if (candidate == kStepBytes) break;
        }
        candidates.push_back(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

        const UINT miscCandidates[] = {
            D3D11_RESOURCE_MISC_TILED | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
            D3D11_RESOURCE_MISC_TILED
        };
        HRESULT result = E_INVALIDARG;
        for (UINT misc : miscCandidates) {
            bufferDesc.MiscFlags = misc;
            for (UINT candidate : candidates) {
                bufferDesc.ByteWidth = candidate;
                result = resource->dev->d11->CreateBuffer(&bufferDesc, nullptr, resource->buf.Put());
                if (SUCCEEDED(result)) break;
            }
            if (SUCCEEDED(result)) break;
        }
        if (FAILED(result)) {
            Log("CreateBuffer tiled failed requested-width=%llu hr=0x%08lx",
                static_cast<unsigned long long>(resource->desc.Width), result);
            return result;
        }
        resource->nativeWidth = bufferDesc.ByteWidth;
        Log("CreateBuffer tiled succeeded requested-width=%llu native-width=%u bind=0x%x misc=0x%x",
            static_cast<unsigned long long>(resource->desc.Width),
            bufferDesc.ByteWidth, bufferDesc.BindFlags, bufferDesc.MiscFlags);
        return S_OK;
    }

    const UINT byteWidth = static_cast<UINT>(resource->desc.Width);
    resource->nativeWidth = byteWidth;
    if (resource->heap != D3D12_HEAP_TYPE_DEFAULT) {
        try {
            resource->shadow.assign(byteWidth, 0);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }

    if (resource->heap == D3D12_HEAP_TYPE_READBACK) {
        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth = byteWidth;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        const HRESULT bufferResult = resource->dev->d11->CreateBuffer(&bufferDesc, nullptr, resource->buf.Put());
        if (FAILED(bufferResult)) return bufferResult;
        D3D11_BUFFER_DESC stagingDesc{};
        stagingDesc.ByteWidth = byteWidth;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        return resource->dev->d11->CreateBuffer(&stagingDesc, nullptr, resource->staging.Put());
    }

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = byteWidth;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = BindFlags(resource->desc);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS |
        D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    const HRESULT result = resource->dev->d11->CreateBuffer(&bufferDesc, nullptr, resource->buf.Put());
    if (FAILED(result)) return result;

    if (resource->heap == D3D12_HEAP_TYPE_UPLOAD && byteWidth)
        resource->dev->ctx->UpdateSubresource(resource->buf.Get(), 0, nullptr, resource->shadow.data(), 0, 0);
    return S_OK;
}

HRESULT CreateTexture(Resource* resource) {
    if (resource->heap != D3D12_HEAP_TYPE_DEFAULT) return E_NOTIMPL;

    const UINT bindFlags = BindFlags(resource->desc);
    switch (resource->desc.Dimension) {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D: {
        D3D11_TEXTURE1D_DESC d{};
        d.Width = static_cast<UINT>(resource->desc.Width);
        d.MipLevels = resource->desc.MipLevels;
        d.ArraySize = resource->desc.DepthOrArraySize;
        d.Format = resource->desc.Format;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bindFlags;
        Ptr<ID3D11Texture1D> texture;
        const HRESULT result = resource->dev->d11->CreateTexture1D(&d, nullptr, texture.Put());
        if (SUCCEEDED(result)) {
            ID3D11Resource* base = texture.Get();
            base->AddRef();
            resource->tex = Ptr<ID3D11Resource>::Attach(base);
        }
        return result;
    }
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D: {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = static_cast<UINT>(resource->desc.Width);
        d.Height = resource->desc.Height;
        d.MipLevels = resource->desc.MipLevels;
        d.ArraySize = resource->desc.DepthOrArraySize;
        d.Format = resource->desc.Format;
        d.SampleDesc.Count = resource->desc.SampleDesc.Count;
        d.SampleDesc.Quality = resource->desc.SampleDesc.Quality;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bindFlags;
        if (resource->tiled) d.MiscFlags |= D3D11_RESOURCE_MISC_TILED;
        if (d.Width == d.Height && d.ArraySize >= 6 && (d.ArraySize % 6) == 0 && d.SampleDesc.Count == 1)
            d.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
        if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER) return E_NOTIMPL;
        Ptr<ID3D11Texture2D> texture;
        if (Cfg().traceCallers) {
            Log("CreateTexture2D begin width=%u height=%u mips=%u array=%u format=%u sample=%u bind=0x%x misc=0x%x tiled=%u",
                d.Width, d.Height, d.MipLevels, d.ArraySize, d.Format, d.SampleDesc.Count,
                d.BindFlags, d.MiscFlags, resource->tiled);
        }
        HRESULT result = resource->dev->d11->CreateTexture2D(&d, nullptr, texture.Put());
        if (FAILED(result) && (d.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)) {
            d.MiscFlags &= ~D3D11_RESOURCE_MISC_TEXTURECUBE;
            result = resource->dev->d11->CreateTexture2D(&d, nullptr, texture.Put());
        }
        if (Cfg().traceCallers) Log("CreateTexture2D end result=0x%08lx", result);
        if (SUCCEEDED(result)) {
            ID3D11Resource* base = texture.Get();
            base->AddRef();
            resource->tex = Ptr<ID3D11Resource>::Attach(base);
        }
        return result;
    }
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D: {
        D3D11_TEXTURE3D_DESC d{};
        d.Width = static_cast<UINT>(resource->desc.Width);
        d.Height = resource->desc.Height;
        d.Depth = resource->desc.DepthOrArraySize;
        d.MipLevels = resource->desc.MipLevels;
        d.Format = resource->desc.Format;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bindFlags;
        Ptr<ID3D11Texture3D> texture;
        const HRESULT result = resource->dev->d11->CreateTexture3D(&d, nullptr, texture.Put());
        if (SUCCEEDED(result)) {
            ID3D11Resource* base = texture.Get();
            base->AddRef();
            resource->tex = Ptr<ID3D11Resource>::Attach(base);
        }
        return result;
    }
    default:
        return E_INVALIDARG;
    }
}

} // namespace

D3D12_HEAP_TYPE EffectiveHeapType(const D3D12_HEAP_PROPERTIES& properties) {
    if (properties.Type != D3D12_HEAP_TYPE_CUSTOM) return properties.Type;
    switch (properties.CPUPageProperty) {
    case D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE:
    case D3D12_CPU_PAGE_PROPERTY_WRITE_BACK:
        return D3D12_HEAP_TYPE_UPLOAD;
    case D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE:
        return D3D12_HEAP_TYPE_DEFAULT;
    default:
        return D3D12_HEAP_TYPE_READBACK;
    }
}

Resource::~Resource() {
    if (backbuffer && dev) {
        std::lock_guard<std::mutex> lock(dev->swapLock);
        dev->backbuffers.erase(this);
    }
    if (placedHeap) {
        auto* heap = dynamic_cast<HeapObj*>(placedHeap.Get());
        if (heap) heap->ReleasePlacedRange(placedOffset, this);
    }
    if (va) FreeVA(va);
}

HRESULT Resource::Create(Device* device, const D3D12_HEAP_PROPERTIES* heapProperties,
                         D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC* resourceDesc,
                         Resource** out, bool tiled) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!device || !device->d11 || !heapProperties || !resourceDesc) return E_INVALIDARG;
    if (resourceDesc->Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
        return E_INVALIDARG;
    if (resourceDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
        (resourceDesc->Height != 1 || resourceDesc->DepthOrArraySize != 1 || resourceDesc->MipLevels != 1))
        return E_INVALIDARG;

    std::unique_ptr<Resource> resource(new (std::nothrow) Resource(device));
    if (!resource) return E_OUTOFMEMORY;
    resource->desc = *resourceDesc;
    resource->hp = *heapProperties;
    resource->heap = EffectiveHeapType(*heapProperties);
    resource->hflags = heapFlags;
    resource->tiled = tiled;
    if (tiled) {
        const bool validTiledTex2D =
            resourceDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
            resourceDesc->Layout == D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE &&
            resourceDesc->SampleDesc.Count == 1;
        const bool validTiledBuf =
            resourceDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
            resourceDesc->Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR &&
            resourceDesc->SampleDesc.Count == 1;
        if (resource->heap != D3D12_HEAP_TYPE_DEFAULT || (!validTiledTex2D && !validTiledBuf))
            return E_INVALIDARG;
    }

    HRESULT result = resourceDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER
        ? CreateBuffer(resource.get())
        : CreateTexture(resource.get());
    if (FAILED(result)) return result;

    if (resource->IsBuffer()) resource->va = AllocVA(resourceDesc->Width, resource.get());
    *out = resource.release();
    return S_OK;
}

HRESULT HeapObj::GetTilePool(ID3D11Buffer** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (hd.Properties.Type != D3D12_HEAP_TYPE_DEFAULT ||
        hd.SizeInBytes == 0 ||
        hd.SizeInBytes % D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT != 0 ||
        hd.SizeInBytes > UINT_MAX)
        return E_INVALIDARG;

    std::lock_guard<std::mutex> lock(tilePoolMutex);
    if (!tilePool) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(hd.SizeInBytes);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TILE_POOL;
        const HRESULT result = dev->d11->CreateBuffer(&desc, nullptr, tilePool.Put());
        if (FAILED(result)) return result;
        Log("Created D3D11 tile pool size=%u bytes", desc.ByteWidth);
    }
    *out = tilePool.Get();
    (*out)->AddRef();
    return S_OK;
}

HRESULT HeapObj::ReservePlacedRange(UINT64 offset, UINT64 size, const D3D12_RESOURCE_DESC* desc,
    Resource* resource, PlacedRangeInfo* outOverlap) {
    if (outOverlap) *outOverlap = {};
    if (!desc || !resource) return E_INVALIDARG;
    if (!size || offset > UINT64_MAX - size) return E_INVALIDARG;

    std::lock_guard<std::mutex> lock(placementMutex);
    for (const auto& placed : placedRanges) {
        if (offset < placed.offset + placed.size && placed.offset < offset + size) {
            if (outOverlap) *outOverlap = placed;
            break;
        }
    }

    PlacedRangeInfo info{};
    info.offset = offset;
    info.size = size;
    info.resource = resource;
    info.dimension = desc->Dimension;
    info.width = desc->Width;
    info.height = desc->Height;
    info.depthOrArray = desc->DepthOrArraySize;
    info.mipLevels = desc->MipLevels;
    info.format = desc->Format;
    info.flags = desc->Flags;
    info.creatorTid = GetCurrentThreadId();
    try {
        placedRanges.push_back(info);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }

    if (outOverlap && outOverlap->size) {
        if (Cfg().diagnosticRenderTrace) {
            TraceStats().placedOverlap.fetch_add(1, std::memory_order_relaxed);
            Log("[RENDER_TRACE] [tid=%lu] CreatePlacedResource ALIAS_ACCEPTED heap=%p "
                "req=[%llu..%llu) res=%p existing=[%llu..%llu) existing-res=%p "
                "backing=independent-d3d11 result=0x%08lx",
                GetCurrentThreadId(), this,
                static_cast<unsigned long long>(offset),
                static_cast<unsigned long long>(offset + size), resource,
                static_cast<unsigned long long>(outOverlap->offset),
                static_cast<unsigned long long>(outOverlap->offset + outOverlap->size),
                outOverlap->resource, S_OK);
        } else {
            Log("CreatePlacedResource overlapping placement accepted; D3D11 backing is independent");
        }
    }

    if (Cfg().diagnosticRenderTrace) {
        auto& stats = TraceStats();
        stats.placedOk.fetch_add(1, std::memory_order_relaxed);
        stats.placedLive.fetch_add(1, std::memory_order_relaxed);
        Log("[RENDER_TRACE] [tid=%lu] CreatePlacedResource OK heap=%p res=%p range=[%llu..%llu) "
            "dim=%u size=%llux%ux%u mips=%u format=%u flags=0x%x live=%zu",
            GetCurrentThreadId(), this, resource,
            static_cast<unsigned long long>(info.offset),
            static_cast<unsigned long long>(info.offset + info.size),
            info.dimension, static_cast<unsigned long long>(info.width),
            info.height, info.depthOrArray, info.mipLevels,
            info.format, static_cast<UINT>(info.flags), placedRanges.size());
    }
    return S_OK;
}

void HeapObj::ReleasePlacedRange(UINT64 offset, Resource* resource) {
    std::lock_guard<std::mutex> lock(placementMutex);
    const auto found = std::find_if(placedRanges.begin(), placedRanges.end(),
        [offset, resource](const PlacedRangeInfo& placed) {
            return placed.offset == offset && placed.resource == resource;
        });
    if (found == placedRanges.end()) {
        const auto sameOffset = std::find_if(placedRanges.begin(), placedRanges.end(),
            [offset](const PlacedRangeInfo& placed) { return placed.offset == offset; });
        if (sameOffset != placedRanges.end()) {
            Log("[RENDER_TRACE] [tid=%lu] ReleasePlacedRange resource mismatch heap=%p offset=%llu expected=%p got=%p",
                GetCurrentThreadId(), this, static_cast<unsigned long long>(offset),
                sameOffset->resource, resource);
        }
        return;
    }
    const PlacedRangeInfo released = *found;
    placedRanges.erase(found);
    if (Cfg().diagnosticRenderTrace && released.resource) {
        auto& stats = TraceStats();
        stats.placedReleased.fetch_add(1, std::memory_order_relaxed);
        stats.placedLive.fetch_sub(1, std::memory_order_relaxed);
        Log("[RENDER_TRACE] [tid=%lu] ReleasePlacedRange heap=%p res=%p range=[%llu..%llu) live=%zu",
            GetCurrentThreadId(), this, released.resource,
            static_cast<unsigned long long>(released.offset),
            static_cast<unsigned long long>(released.offset + released.size), placedRanges.size());
    }
}

UINT Resource::NumSubresources() const {
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return 1;
    const UINT mips = std::max<UINT>(desc.MipLevels, 1);
    const UINT arrays = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc.DepthOrArraySize;
    return mips * arrays;
}

ID3D11Buffer* Resource::GpuBuffer() {
    if (!buf) return nullptr;
    return buf.Get();
}

void Resource::SyncToGpu(UINT64 offset, UINT64 size) {
    if (!buf || shadow.empty() || offset >= shadow.size()) return;
    const UINT64 end = size == UINT64_MAX ? shadow.size() : std::min<UINT64>(shadow.size(), offset + size);
    if (end <= offset || end - offset > UINT_MAX || offset > UINT_MAX) return;
    D3D11_BOX box{};
    box.left = static_cast<UINT>(offset);
    box.right = static_cast<UINT>(end);
    box.top = 0;
    box.bottom = 1;
    box.front = 0;
    box.back = 1;
    dev->ctx->UpdateSubresource(buf.Get(), 0, &box, shadow.data() + offset, 0, 0);
}

ID3D11Buffer* Resource::CbWindow(UINT64 offset, UINT64 size) {
    if (!IsBuffer() || size == 0 || size > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16 ||
        offset > desc.Width || size > desc.Width - offset)
        return nullptr;
    const UINT64 byteWidth = AlignUp(size, 16);
    if (byteWidth > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16) return nullptr;
    std::lock_guard<std::mutex> lock(mu);
    const auto key = std::make_pair(offset, size);
    auto found = cbWindows.find(key);
    if (found != cbWindows.end()) {
        if (!shadow.empty()) {
            std::vector<uint8_t> bytes(static_cast<size_t>(byteWidth), 0);
            memcpy(bytes.data(), shadow.data() + offset, static_cast<size_t>(size));
            dev->ctx->UpdateSubresource(found->second.Get(), 0, nullptr, bytes.data(), 0, 0);
        } else if (buf) {
            D3D11_BOX box{};
            box.left = static_cast<UINT>(offset);
            box.right = static_cast<UINT>(offset + size);
            box.top = 0;
            box.bottom = 1;
            box.front = 0;
            box.back = 1;
            dev->ctx->CopySubresourceRegion(found->second.Get(), 0, 0, 0, 0, buf.Get(), 0, &box);
        }
        return found->second.Get();
    }

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = static_cast<UINT>(byteWidth);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Ptr<ID3D11Buffer> window;
    if (FAILED(dev->d11->CreateBuffer(&bufferDesc, nullptr, window.Put()))) return nullptr;

    std::vector<uint8_t> bytes(static_cast<size_t>(byteWidth), 0);
    if (!shadow.empty()) {
        memcpy(bytes.data(), shadow.data() + offset, static_cast<size_t>(size));
        dev->ctx->UpdateSubresource(window.Get(), 0, nullptr, bytes.data(), 0, 0);
    } else if (buf) {
        D3D11_BOX box{};
        box.left = static_cast<UINT>(offset);
        box.right = static_cast<UINT>(offset + size);
        box.top = 0;
        box.bottom = 1;
        box.front = 0;
        box.back = 1;
        dev->ctx->CopySubresourceRegion(window.Get(), 0, 0, 0, 0, buf.Get(), 0, &box);
    }

    auto inserted = cbWindows.emplace(key, std::move(window));
    return inserted.first->second.Get();
}

ID3D11Buffer* Resource::StructCopy(UINT stride) {
    if (!IsBuffer() || stride == 0 || desc.Width > UINT_MAX) {
        if (Cfg().diagnosticRenderTrace)
            Log("[RENDER_TRACE] [tid=%lu] StructCopy rejected resource=%p width=%llu stride=%u buffer=%u",
                GetCurrentThreadId(), this, static_cast<unsigned long long>(desc.Width), stride, IsBuffer());
        return nullptr;
    }
    auto found = structCopies.find(stride);
    if (found != structCopies.end()) return found->second.Get();

    UINT64 effectiveWidth = nativeWidth ? std::min(desc.Width, nativeWidth) : desc.Width;
    if (tiled) effectiveWidth = std::min<UINT64>(effectiveWidth, 64ull * 1024ull * 1024ull);
    const UINT byteWidth = static_cast<UINT>((effectiveWidth / stride) * stride);
    if (!byteWidth) {
        if (Cfg().diagnosticRenderTrace)
            Log("[RENDER_TRACE] [tid=%lu] StructCopy computed zero ByteWidth resource=%p width=%llu native-width=%llu stride=%u",
                GetCurrentThreadId(), this, static_cast<unsigned long long>(desc.Width),
                static_cast<unsigned long long>(nativeWidth), stride);
        return nullptr;
    }
    if (Cfg().diagnosticRenderTrace && desc.Width % stride != 0) {
        static std::atomic<UINT> nonAlignedLogCount{0};
        if (nonAlignedLogCount.fetch_add(1, std::memory_order_relaxed) < 16) {
            Log("[STRUCT_COPY_TRUNCATE] resource=%p width=%llu stride=%u "
                "copy_bytes=%u trailing_bytes=%llu",
                this, static_cast<unsigned long long>(desc.Width), stride, byteWidth,
                static_cast<unsigned long long>(desc.Width % stride));
        }
    }

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = byteWidth;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) bufferDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = stride;
    Ptr<ID3D11Buffer> copy;
    const HRESULT createResult = dev->d11->CreateBuffer(&bufferDesc, nullptr, copy.Put());
    if (FAILED(createResult)) {
        if (Cfg().diagnosticRenderTrace)
            Log("[RENDER_TRACE] [tid=%lu] StructCopy CreateBuffer failed resource=%p width=%llu effective-width=%llu stride=%u hr=0x%08lx",
                GetCurrentThreadId(), this, static_cast<unsigned long long>(desc.Width),
                static_cast<unsigned long long>(effectiveWidth), stride, createResult);
        return nullptr;
    }

    if (buf && !CopyBufferPrefix(dev->ctx.Get(), copy.Get(), buf.Get(), byteWidth)) {
        if (Cfg().diagnosticRenderTrace)
            Log("[RENDER_TRACE] [tid=%lu] StructCopy source range invalid resource=%p "
                "copy_bytes=%u stride=%u",
                GetCurrentThreadId(), this, byteWidth, stride);
        return nullptr;
    }
    if (!shadow.empty()) {
        if (shadow.size() < byteWidth) {
            if (Cfg().diagnosticRenderTrace)
                Log("[RENDER_TRACE] [tid=%lu] StructCopy shadow too small resource=%p "
                    "shadow_bytes=%zu copy_bytes=%u stride=%u",
                    GetCurrentThreadId(), this, shadow.size(), byteWidth, stride);
            return nullptr;
        }
        dev->ctx->UpdateSubresource(copy.Get(), 0, nullptr, shadow.data(), 0, 0);
    }
    auto inserted = structCopies.emplace(stride, std::move(copy));
    return inserted.first->second.Get();
}

void Resource::SyncStruct(UINT stride) {
    ID3D11Buffer* copy = StructCopy(stride);
    if (!copy || !buf) return;
    D3D11_BUFFER_DESC copyDesc{};
    copy->GetDesc(&copyDesc);
    if (!CopyBufferPrefix(dev->ctx.Get(), copy, buf.Get(), copyDesc.ByteWidth) &&
        Cfg().diagnosticRenderTrace) {
        Log("[RENDER_TRACE] [tid=%lu] SyncStruct source range invalid resource=%p "
            "copy_bytes=%u stride=%u",
            GetCurrentThreadId(), this, copyDesc.ByteWidth, stride);
    }
}

void Resource::WriteBackStruct(UINT stride) {
    auto found = structCopies.find(stride);
    if (found != structCopies.end() && buf) {
        D3D11_BUFFER_DESC copyDesc{};
        found->second->GetDesc(&copyDesc);
        if (!CopyBufferPrefix(dev->ctx.Get(), buf.Get(), found->second.Get(), copyDesc.ByteWidth) &&
            Cfg().diagnosticRenderTrace) {
            Log("[RENDER_TRACE] [tid=%lu] WriteBackStruct destination range invalid "
                "resource=%p copy_bytes=%u stride=%u",
                GetCurrentThreadId(), this, copyDesc.ByteWidth, stride);
        }
    }
}

HRESULT Resource::RefreshReadback() {
    if (heap != D3D12_HEAP_TYPE_READBACK || !staging || !buf || shadow.empty()) return E_INVALIDARG;
    std::lock_guard<std::recursive_mutex> contextLock(dev->ctxLock);
    dev->ctx->CopyResource(staging.Get(), buf.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = dev->ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    Log("RESOURCE_MAP resource=%p hr=0x%08lx pData=%p shadowData=%p RowPitch=%u DepthPitch=%u "
        "shadowSize=%zu caller=Resource::Map",
        staging.Get(), static_cast<unsigned long>(mapResult), mapped.pData,
        shadow.data(), mapped.RowPitch, mapped.DepthPitch, shadow.size());
    if (FAILED(mapResult)) {
        Log("RESOURCE_MAP_SKIPPED reason=map-failed hr=0x%08lx caller=Resource::Map",
            static_cast<unsigned long>(mapResult));
        return mapResult;
    }
    if (!mapped.pData) {
        Log("RESOURCE_MAP_SKIPPED reason=null-pdata hr=0x%08lx caller=Resource::Map",
            static_cast<unsigned long>(mapResult));
        dev->ctx->Unmap(staging.Get(), 0);
        return E_FAIL;
    }
    const bool sourceReadable = HasMemoryAccess(mapped.pData, shadow.size(), false);
    const bool destinationWritable = HasMemoryAccess(shadow.data(), shadow.size(), true);
    if (!sourceReadable || !destinationWritable) {
        Log("RESOURCE_MAP_SKIPPED reason=invalid-memory-range pData=%p shadowData=%p "
            "bytes=%zu sourceReadable=%d destinationWritable=%d caller=Resource::Map",
            mapped.pData, shadow.data(), shadow.size(), sourceReadable ? 1 : 0,
            destinationWritable ? 1 : 0);
        dev->ctx->Unmap(staging.Get(), 0);
        return E_FAIL;
    }
    memcpy(shadow.data(), mapped.pData, shadow.size());
    dev->ctx->Unmap(staging.Get(), 0);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Resource::Map(UINT subresource, const D3D12_RANGE* readRange, void** data) {
    if (!data) return E_POINTER;
    *data = nullptr;
    if (subresource != 0 || !IsBuffer()) return E_INVALIDARG;
    if (heap == D3D12_HEAP_TYPE_DEFAULT) return DXGI_ERROR_INVALID_CALL;
    if (readRange && (readRange->Begin > readRange->End || readRange->End > desc.Width)) return E_INVALIDARG;
    if (heap == D3D12_HEAP_TYPE_READBACK) {
        const HRESULT result = RefreshReadback();
        if (FAILED(result)) return result;
    }
    std::lock_guard<std::mutex> lock(mu);
    ++mapCount;
    *data = shadow.data();
    return S_OK;
}

void STDMETHODCALLTYPE Resource::Unmap(UINT subresource, const D3D12_RANGE* writtenRange) {
    if (subresource != 0 || !IsBuffer() || heap != D3D12_HEAP_TYPE_UPLOAD) return;
    std::lock_guard<std::mutex> lock(mu);
    if (mapCount > 0) --mapCount;
    if (writtenRange && writtenRange->Begin <= writtenRange->End && writtenRange->End <= desc.Width)
        SyncToGpu(writtenRange->Begin, writtenRange->End - writtenRange->Begin);
    else if (!writtenRange)
        SyncToGpu(0, UINT64_MAX);
    else
        Log("Ignored invalid written range [%llu, %llu) for a %llu-byte resource",
            static_cast<unsigned long long>(writtenRange->Begin),
            static_cast<unsigned long long>(writtenRange->End),
            static_cast<unsigned long long>(desc.Width));
}
