#include "types.h"

namespace {

DescriptorHeap* GetHeap(SIZE_T handle, UINT* index) {
    DescriptorHeap* heap = FindHeapByCpu(handle, index);
    if (!heap) Log("Descriptor CPU handle %p is not owned by a live heap", reinterpret_cast<void*>(handle));
    return heap;
}

void TraceTargetDescriptorWrite(const char* operation, DescriptorHeap* heap, UINT index,
                                const Descriptor& descriptor,
                                const DescriptorHeap* sourceHeap = nullptr,
                                UINT sourceIndex = UINT_MAX) {
    constexpr UINT firstIndex = 1998;
    constexpr UINT endIndex = 2064;
    static std::atomic<UINT> cpuHeapTraceCount{0};
    static std::atomic<UINT> gpuHeapTraceCount{0};
    if (!Cfg().diagnosticRenderTrace || index < firstIndex || index >= endIndex)
        return;
    auto& traceCount = heap->gpuBase ? gpuHeapTraceCount : cpuHeapTraceCount;
    const UINT traceLimit = heap->gpuBase ? 512 : 64;
    if (traceCount.fetch_add(1, std::memory_order_relaxed) >= traceLimit)
        return;
    Log("[TARGET_DESCRIPTOR_WRITE] op=%s heap=%p heap_type=%u gpu_base=0x%llx index=%u kind=%u "
        "srv=%p resource=%p source_heap=%p source_gpu_base=0x%llx source_index=%u",
        operation, heap, static_cast<unsigned int>(heap->hd.Type),
        static_cast<unsigned long long>(heap->gpuBase), index,
        static_cast<unsigned int>(descriptor.kind), descriptor.srv.Get(), descriptor.keep.Get(),
        sourceHeap, static_cast<unsigned long long>(sourceHeap ? sourceHeap->gpuBase : 0),
        sourceIndex);
}

Resource* AsResource(ID3D12Resource* resource) {
    return resource ? dynamic_cast<Resource*>(resource) : nullptr;
}

void Store(D3D12_CPU_DESCRIPTOR_HANDLE handle, Descriptor&& descriptor) {
    UINT index = 0;
    DescriptorHeap* heap = GetHeap(handle.ptr, &index);
    if (!heap) return;
    TraceTargetDescriptorWrite("store", heap, index, descriptor);
    heap->items[index] = std::move(descriptor);
}

void RememberResource(Descriptor& descriptor, Resource* resource) {
    descriptor.res = resource;
    if (resource) descriptor.keep = Ptr<IUnknown>(static_cast<ID3D12Resource*>(resource));
}

UINT ClampBufferElements(ID3D11Buffer* buffer, DXGI_FORMAT format, UINT structStride,
                         UINT64 firstElement, UINT requestedElements) {
    if (!buffer || !requestedElements) return requestedElements;
    D3D11_BUFFER_DESC bufferDesc{};
    buffer->GetDesc(&bufferDesc);
    UINT elementBytes = structStride;
    if (!elementBytes) {
        const FormatInfo info = GetFormatInfo(format);
        elementBytes = (info.bits && (info.bits % 8) == 0) ? (info.bits / 8) : 4u;
    }
    if (!elementBytes) return requestedElements;
    const UINT maxTotalElements = bufferDesc.ByteWidth / elementBytes;
    if (firstElement >= maxTotalElements) return 0;
    const UINT available = maxTotalElements - static_cast<UINT>(firstElement);
    return std::min<UINT>(requestedElements, available);
}

HRESULT CreateBufferSrv(Device* device, Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& source,
                        Descriptor& result) {
    if (source.ViewDimension != D3D12_SRV_DIMENSION_BUFFER) return E_NOTIMPL;
    const auto& view = source.Buffer;
    if (!view.NumElements) return E_INVALIDARG;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    const UINT stride = view.StructureByteStride;
    ID3D11Buffer* buffer = resource->GpuBuffer();
    if (source.Format == DXGI_FORMAT_R32_TYPELESS && (view.Flags & D3D12_BUFFER_SRV_FLAG_RAW)) {
        D3D11_BUFFER_DESC bufferDesc{};
        if (buffer) buffer->GetDesc(&bufferDesc);
        const bool allowRaw =
            (bufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) != 0;
        const UINT numElements = ClampBufferElements(
            buffer, allowRaw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_UINT,
            4, view.FirstElement, view.NumElements);
        if (!numElements) return E_INVALIDARG;
        if (allowRaw) {
            desc.Format = DXGI_FORMAT_R32_TYPELESS;
            desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            desc.BufferEx.FirstElement = static_cast<UINT>(view.FirstElement);
            desc.BufferEx.NumElements = numElements;
            desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        } else {
            desc.Format = DXGI_FORMAT_R32_UINT;
            desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = static_cast<UINT>(view.FirstElement);
            desc.Buffer.NumElements = numElements;
        }
    } else if (stride) {
        buffer = resource->StructCopy(stride);
        if (!buffer) return E_OUTOFMEMORY;
        const UINT numElements = ClampBufferElements(
            buffer, DXGI_FORMAT_UNKNOWN, stride, view.FirstElement, view.NumElements);
        if (!numElements) return E_INVALIDARG;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = static_cast<UINT>(view.FirstElement);
        desc.Buffer.NumElements = numElements;
        result.structStride = stride;
    } else {
        if (source.Format == DXGI_FORMAT_UNKNOWN) return E_INVALIDARG;
        const UINT numElements = ClampBufferElements(
            buffer, source.Format, 0, view.FirstElement, view.NumElements);
        if (!numElements) return E_INVALIDARG;
        desc.Format = source.Format;
        desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = static_cast<UINT>(view.FirstElement);
        desc.Buffer.NumElements = numElements;
    }

    HRESULT hr = device->d11->CreateShaderResourceView(buffer, &desc, result.srv.Put());
    if (SUCCEEDED(hr)) {
        result.kind = DKind::SRV;
        result.bufOff = view.FirstElement * (stride ? stride : 4);
        result.bufSize = static_cast<UINT64>(view.NumElements) * (stride ? stride : 4);
    }
    return hr;
}

HRESULT CreateTextureSrv(Device* device, Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& source,
                         Descriptor& result) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = source.Format;
    switch (source.ViewDimension) {
    case D3D12_SRV_DIMENSION_TEXTURE1D:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
        desc.Texture1D.MostDetailedMip = source.Texture1D.MostDetailedMip;
        desc.Texture1D.MipLevels = source.Texture1D.MipLevels;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
        desc.Texture1DArray.MostDetailedMip = source.Texture1DArray.MostDetailedMip;
        desc.Texture1DArray.MipLevels = source.Texture1DArray.MipLevels;
        desc.Texture1DArray.FirstArraySlice = source.Texture1DArray.FirstArraySlice;
        desc.Texture1DArray.ArraySize = source.Texture1DArray.ArraySize;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2D:
        if (source.Texture2D.PlaneSlice) return E_NOTIMPL;
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = source.Texture2D.MostDetailedMip;
        desc.Texture2D.MipLevels = source.Texture2D.MipLevels;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
        if (source.Texture2DArray.PlaneSlice) return E_NOTIMPL;
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MostDetailedMip = source.Texture2DArray.MostDetailedMip;
        desc.Texture2DArray.MipLevels = source.Texture2DArray.MipLevels;
        desc.Texture2DArray.FirstArraySlice = source.Texture2DArray.FirstArraySlice;
        desc.Texture2DArray.ArraySize = source.Texture2DArray.ArraySize;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DMS:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
        desc.Texture2DMSArray.FirstArraySlice = source.Texture2DMSArray.FirstArraySlice;
        desc.Texture2DMSArray.ArraySize = source.Texture2DMSArray.ArraySize;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE3D:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MostDetailedMip = source.Texture3D.MostDetailedMip;
        desc.Texture3D.MipLevels = source.Texture3D.MipLevels;
        break;
    case D3D12_SRV_DIMENSION_TEXTURECUBE:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MostDetailedMip = source.TextureCube.MostDetailedMip;
        desc.TextureCube.MipLevels = source.TextureCube.MipLevels;
        break;
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        desc.TextureCubeArray.MostDetailedMip = source.TextureCubeArray.MostDetailedMip;
        desc.TextureCubeArray.MipLevels = source.TextureCubeArray.MipLevels;
        desc.TextureCubeArray.First2DArrayFace = source.TextureCubeArray.First2DArrayFace;
        desc.TextureCubeArray.NumCubes = source.TextureCubeArray.NumCubes;
        break;
    default:
        return E_NOTIMPL;
    }
    const HRESULT hr = device->d11->CreateShaderResourceView(resource->Raw(), &desc, result.srv.Put());
    if (SUCCEEDED(hr)) result.kind = DKind::SRV;
    return hr;
}

} // namespace

void STDMETHODCALLTYPE Device::CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* source,
                                                           D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    Descriptor descriptor{};
    if (!source || !source->BufferLocation || !source->SizeInBytes) {
        Store(handle, std::move(descriptor));
        return;
    }
    if ((source->BufferLocation & 255) != 0 || (source->SizeInBytes & 255) != 0) {
        Log("Rejected misaligned D3D12 CBV at GPU VA 0x%llx, size %u",
            static_cast<unsigned long long>(source->BufferLocation), source->SizeInBytes);
        Store(handle, std::move(descriptor));
        return;
    }

    UINT64 offset = 0;
    Resource* resource = ResolveVA(source->BufferLocation, &offset);
    ID3D11Buffer* buffer = resource ? resource->CbWindow(offset, source->SizeInBytes) : nullptr;
    if (!buffer) {
        Log("Could not resolve D3D12 CBV at GPU VA 0x%llx", static_cast<unsigned long long>(source->BufferLocation));
        Store(handle, std::move(descriptor));
        return;
    }
    descriptor.kind = DKind::CBV;
    descriptor.res = resource;
    descriptor.keep = Ptr<IUnknown>(static_cast<ID3D12Resource*>(resource));
    descriptor.bufOff = offset;
    descriptor.bufSize = source->SizeInBytes;
    Store(handle, std::move(descriptor));
}

void STDMETHODCALLTYPE Device::CreateShaderResourceView(ID3D12Resource* sourceResource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* source, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    TraceApi("Device::CreateShaderResourceView");
    Descriptor descriptor{};
    Resource* resource = AsResource(sourceResource);
    if (!resource) {
        Store(handle, std::move(descriptor));
        return;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC inferred{};
    if (!source) {
        inferred.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (resource->IsBuffer()) {
            inferred.Format = DXGI_FORMAT_R32_TYPELESS;
            inferred.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            inferred.Buffer.NumElements = static_cast<UINT>(resource->desc.Width / 4);
            inferred.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        } else {
            inferred.Format = resource->desc.Format;
            inferred.ViewDimension = resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D
                ? D3D12_SRV_DIMENSION_TEXTURE1D
                : resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                    ? D3D12_SRV_DIMENSION_TEXTURE3D : D3D12_SRV_DIMENSION_TEXTURE2D;
            inferred.Texture2D.MipLevels = resource->desc.MipLevels;
        }
        source = &inferred;
    }

    Log("CreateShaderResourceView resource=%p tiled=%u dimension=%u format=%u",
        resource, resource->tiled ? 1u : 0u, source->ViewDimension, source->Format);
    const HRESULT result = resource->IsBuffer()
        ? CreateBufferSrv(this, resource, *source, descriptor)
        : CreateTextureSrv(this, resource, *source, descriptor);
    Log("CreateShaderResourceView result=0x%08lx", result);
    if (FAILED(result) && Cfg().diagnosticRenderTrace) {
        UINT64 firstElement = 0;
        UINT64 numElements = 0;
        UINT firstMip = 0;
        UINT mipLevels = 0;
        UINT structureStride = 0;
        UINT viewFlags = 0;
        if (source->ViewDimension == D3D12_SRV_DIMENSION_BUFFER) {
            firstElement = source->Buffer.FirstElement;
            numElements = source->Buffer.NumElements;
            structureStride = source->Buffer.StructureByteStride;
            viewFlags = static_cast<UINT>(source->Buffer.Flags);
        } else if (source->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE1D) {
            firstMip = source->Texture1D.MostDetailedMip;
            mipLevels = source->Texture1D.MipLevels;
        } else if (source->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE1DARRAY) {
            firstMip = source->Texture1DArray.MostDetailedMip;
            mipLevels = source->Texture1DArray.MipLevels;
        } else if (source->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D) {
            firstMip = source->Texture2D.MostDetailedMip;
            mipLevels = source->Texture2D.MipLevels;
        } else if (source->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY) {
            firstMip = source->Texture2DArray.MostDetailedMip;
            mipLevels = source->Texture2DArray.MipLevels;
        } else if (source->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE3D) {
            firstMip = source->Texture3D.MostDetailedMip;
            mipLevels = source->Texture3D.MipLevels;
        } else if (source->ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE) {
            firstMip = source->TextureCube.MostDetailedMip;
            mipLevels = source->TextureCube.MipLevels;
        } else if (source->ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY) {
            firstMip = source->TextureCubeArray.MostDetailedMip;
            mipLevels = source->TextureCubeArray.MipLevels;
        }
        Log("[RENDER_TRACE] [tid=%lu] CreateShaderResourceView failed resource=%p "
            "resource-dim=%u size=%llux%ux%u resource-format=%u resource-flags=0x%x "
            "view-dim=%u view-format=%u first-element=%llu num-elements=%llu "
            "first-mip=%u mip-levels=%u stride=%u view-flags=0x%x hr=0x%08lx",
            GetCurrentThreadId(), resource, resource->desc.Dimension,
            static_cast<unsigned long long>(resource->desc.Width), resource->desc.Height,
            resource->desc.DepthOrArraySize, resource->desc.Format,
            static_cast<UINT>(resource->desc.Flags), source->ViewDimension, source->Format,
            static_cast<unsigned long long>(firstElement),
            static_cast<unsigned long long>(numElements), firstMip, mipLevels,
            structureStride, viewFlags, result);
    }
    if (descriptor.kind != DKind::None) RememberResource(descriptor, resource);
    Store(handle, std::move(descriptor));
}

void STDMETHODCALLTYPE Device::CreateUnorderedAccessView(ID3D12Resource* sourceResource, ID3D12Resource* counterResource,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* source, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    Descriptor descriptor{};
    Resource* resource = AsResource(sourceResource);
    if (!resource || counterResource) {
        if (counterResource) Log("Counter UAVs are not supported");
        Store(handle, std::move(descriptor));
        return;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC inferred{};
    if (!source) {
        if (resource->IsBuffer()) {
            inferred.Format = DXGI_FORMAT_R32_TYPELESS;
            inferred.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            inferred.Buffer.NumElements = static_cast<UINT>(resource->desc.Width / 4);
            inferred.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        } else {
            inferred.Format = resource->desc.Format;
            if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
                inferred.ViewDimension = resource->desc.DepthOrArraySize > 1
                    ? D3D12_UAV_DIMENSION_TEXTURE1DARRAY : D3D12_UAV_DIMENSION_TEXTURE1D;
                inferred.Texture1D.MipSlice = 0;
                inferred.Texture1DArray.ArraySize = resource->desc.DepthOrArraySize;
            } else if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
                inferred.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                inferred.Texture3D.WSize = resource->desc.DepthOrArraySize;
            } else {
                inferred.ViewDimension = resource->desc.DepthOrArraySize > 1
                    ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
                inferred.Texture2D.MipSlice = 0;
                inferred.Texture2DArray.ArraySize = resource->desc.DepthOrArraySize;
            }
        }
        source = &inferred;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = source->Format;
    ID3D11Resource* nativeResource = resource->Raw();
    if (resource->IsBuffer()) {
        if (source->ViewDimension != D3D12_UAV_DIMENSION_BUFFER) {
            Log("Buffer UAV has a non-buffer view dimension");
            Store(handle, std::move(descriptor));
            return;
        }
        const auto& view = source->Buffer;
        ID3D11Buffer* buffer = resource->GpuBuffer();
        if (source->Format == DXGI_FORMAT_R32_TYPELESS && (view.Flags & D3D12_BUFFER_UAV_FLAG_RAW)) {
            D3D11_BUFFER_DESC bufferDesc{};
            if (buffer) buffer->GetDesc(&bufferDesc);
            const bool allowRaw =
                (bufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) != 0;
            const UINT numElements = ClampBufferElements(
                buffer, allowRaw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_UINT,
                4, view.FirstElement, view.NumElements);
            if (!numElements) {
                Store(handle, std::move(descriptor));
                return;
            }
            desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = static_cast<UINT>(view.FirstElement);
            desc.Buffer.NumElements = numElements;
            desc.Buffer.Flags = allowRaw ? D3D11_BUFFER_UAV_FLAG_RAW : 0;
            desc.Format = allowRaw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_UINT;
        } else if (view.StructureByteStride) {
            buffer = resource->StructCopy(view.StructureByteStride);
            if (!buffer) {
                Store(handle, std::move(descriptor));
                return;
            }
            const UINT numElements = ClampBufferElements(
                buffer, DXGI_FORMAT_UNKNOWN, view.StructureByteStride,
                view.FirstElement, view.NumElements);
            desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = static_cast<UINT>(view.FirstElement);
            desc.Buffer.NumElements = numElements;
            desc.Buffer.Flags = 0;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            descriptor.structStride = view.StructureByteStride;
        } else {
            const UINT numElements = ClampBufferElements(
                buffer, source->Format, 0, view.FirstElement, view.NumElements);
            desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = static_cast<UINT>(view.FirstElement);
            desc.Buffer.NumElements = numElements;
        }
        nativeResource = buffer;
        descriptor.bufOff = view.FirstElement * (view.StructureByteStride ? view.StructureByteStride : 4);
        descriptor.bufSize = static_cast<UINT64>(view.NumElements) *
            (view.StructureByteStride ? view.StructureByteStride : 4);
    } else if (source->ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D) {
        if (source->Texture2D.PlaneSlice) {
            Log("Plane-slice UAVs are not supported");
            Store(handle, std::move(descriptor));
            return;
        }
        desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = source->Texture2D.MipSlice;
    } else if (source->ViewDimension == D3D12_UAV_DIMENSION_TEXTURE1D) {
        desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
        desc.Texture1D.MipSlice = source->Texture1D.MipSlice;
    } else if (source->ViewDimension == D3D12_UAV_DIMENSION_TEXTURE1DARRAY) {
        desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1DARRAY;
        desc.Texture1DArray.MipSlice = source->Texture1DArray.MipSlice;
        desc.Texture1DArray.FirstArraySlice = source->Texture1DArray.FirstArraySlice;
        desc.Texture1DArray.ArraySize = source->Texture1DArray.ArraySize;
    } else if (source->ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2DARRAY) {
        if (source->Texture2DArray.PlaneSlice) {
            Log("Plane-slice UAVs are not supported");
            Store(handle, std::move(descriptor));
            return;
        }
        desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice = source->Texture2DArray.MipSlice;
        desc.Texture2DArray.FirstArraySlice = source->Texture2DArray.FirstArraySlice;
        desc.Texture2DArray.ArraySize = source->Texture2DArray.ArraySize;
    } else if (source->ViewDimension == D3D12_UAV_DIMENSION_TEXTURE3D) {
        desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = source->Texture3D.MipSlice;
        desc.Texture3D.FirstWSlice = source->Texture3D.FirstWSlice;
        desc.Texture3D.WSize = source->Texture3D.WSize;
    } else {
        Log("Texture UAV dimension %u is not supported", source->ViewDimension);
        Store(handle, std::move(descriptor));
        return;
    }

    Log("CreateUnorderedAccessView resource=%p tiled=%u dimension=%u format=%u",
        resource, resource->tiled ? 1u : 0u, source->ViewDimension, source->Format);
    const HRESULT result = d11->CreateUnorderedAccessView(nativeResource, &desc, descriptor.uav.Put());
    Log("CreateUnorderedAccessView result=0x%08lx", result);
    if (FAILED(result)) {
        Store(handle, std::move(descriptor));
        return;
    }
    descriptor.kind = DKind::UAV;
    descriptor.uavWrite = true;
    RememberResource(descriptor, resource);
    Store(handle, std::move(descriptor));
}

void STDMETHODCALLTYPE Device::CreateRenderTargetView(ID3D12Resource* sourceResource,
    const D3D12_RENDER_TARGET_VIEW_DESC* source, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    Descriptor descriptor{};
    Resource* resource = AsResource(sourceResource);
    if (!resource || resource->IsBuffer()) {
        Store(handle, std::move(descriptor));
        return;
    }
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    if (source) {
        desc.Format = source->Format;
        switch (source->ViewDimension) {
        case D3D12_RTV_DIMENSION_TEXTURE1D:
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1D;
            desc.Texture1D.MipSlice = source->Texture1D.MipSlice;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
            desc.Texture1DArray.MipSlice = source->Texture1DArray.MipSlice;
            desc.Texture1DArray.FirstArraySlice = source->Texture1DArray.FirstArraySlice;
            desc.Texture1DArray.ArraySize = source->Texture1DArray.ArraySize;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2D:
            if (source->Texture2D.PlaneSlice) {
                Log("Plane-slice RTVs are not supported");
                Store(handle, std::move(descriptor));
                return;
            }
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = source->Texture2D.MipSlice;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
            if (source->Texture2DArray.PlaneSlice) {
                Log("Plane-slice RTVs are not supported");
                Store(handle, std::move(descriptor));
                return;
            }
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = source->Texture2DArray.MipSlice;
            desc.Texture2DArray.FirstArraySlice = source->Texture2DArray.FirstArraySlice;
            desc.Texture2DArray.ArraySize = source->Texture2DArray.ArraySize;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2DMS:
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
            desc.Texture2DMSArray.FirstArraySlice = source->Texture2DMSArray.FirstArraySlice;
            desc.Texture2DMSArray.ArraySize = source->Texture2DMSArray.ArraySize;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE3D:
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
            desc.Texture3D.MipSlice = source->Texture3D.MipSlice;
            desc.Texture3D.FirstWSlice = source->Texture3D.FirstWSlice;
            desc.Texture3D.WSize = source->Texture3D.WSize;
            break;
        default:
            Log("Unsupported RTV view dimension %u", source->ViewDimension);
            Store(handle, std::move(descriptor));
            return;
        }
    }
    const HRESULT result = d11->CreateRenderTargetView(resource->Raw(), source ? &desc : nullptr, descriptor.rtv.Put());
    if (FAILED(result)) {
        Log("CreateRenderTargetView failed: 0x%08lx", result);
        Store(handle, std::move(descriptor));
        return;
    }
    descriptor.kind = DKind::RTV;
    RememberResource(descriptor, resource);
    Store(handle, std::move(descriptor));
}

void STDMETHODCALLTYPE Device::CreateDepthStencilView(ID3D12Resource* sourceResource,
    const D3D12_DEPTH_STENCIL_VIEW_DESC* source, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    Descriptor descriptor{};
    Resource* resource = AsResource(sourceResource);
    if (!resource || resource->IsBuffer()) {
        Store(handle, std::move(descriptor));
        return;
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
    if (source) {
        desc.Format = source->Format;
        desc.Flags = 0;
        if (source->Flags & D3D12_DSV_FLAG_READ_ONLY_DEPTH) desc.Flags |= D3D11_DSV_READ_ONLY_DEPTH;
        if (source->Flags & D3D12_DSV_FLAG_READ_ONLY_STENCIL) desc.Flags |= D3D11_DSV_READ_ONLY_STENCIL;
        switch (source->ViewDimension) {
        case D3D12_DSV_DIMENSION_TEXTURE1D:
            desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1D;
            desc.Texture1D.MipSlice = source->Texture1D.MipSlice;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
            desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1DARRAY;
            desc.Texture1DArray.MipSlice = source->Texture1DArray.MipSlice;
            desc.Texture1DArray.FirstArraySlice = source->Texture1DArray.FirstArraySlice;
            desc.Texture1DArray.ArraySize = source->Texture1DArray.ArraySize;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE2D:
            desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = source->Texture2D.MipSlice;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
            desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = source->Texture2DArray.MipSlice;
            desc.Texture2DArray.FirstArraySlice = source->Texture2DArray.FirstArraySlice;
            desc.Texture2DArray.ArraySize = source->Texture2DArray.ArraySize;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE2DMS:
            desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
            desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
            desc.Texture2DMSArray.FirstArraySlice = source->Texture2DMSArray.FirstArraySlice;
            desc.Texture2DMSArray.ArraySize = source->Texture2DMSArray.ArraySize;
            break;
        default:
            Log("Unsupported DSV view dimension %u", source->ViewDimension);
            Store(handle, std::move(descriptor));
            return;
        }
    }
    const HRESULT result = d11->CreateDepthStencilView(resource->Raw(), source ? &desc : nullptr, descriptor.dsv.Put());
    if (FAILED(result)) {
        Log("CreateDepthStencilView failed: 0x%08lx", result);
        Store(handle, std::move(descriptor));
        return;
    }
    descriptor.kind = DKind::DSV;
    RememberResource(descriptor, resource);
    Store(handle, std::move(descriptor));
}

void STDMETHODCALLTYPE Device::CreateSampler(const D3D12_SAMPLER_DESC* source, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    Descriptor descriptor{};
    if (!source) {
        Store(handle, std::move(descriptor));
        return;
    }
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = static_cast<D3D11_FILTER>(source->Filter);
    desc.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(source->AddressU);
    desc.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(source->AddressV);
    desc.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(source->AddressW);
    desc.MipLODBias = source->MipLODBias;
    desc.MaxAnisotropy = source->MaxAnisotropy;
    desc.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(source->ComparisonFunc);
    memcpy(desc.BorderColor, source->BorderColor, sizeof(desc.BorderColor));
    desc.MinLOD = source->MinLOD;
    desc.MaxLOD = source->MaxLOD;
    const HRESULT result = d11->CreateSamplerState(&desc, descriptor.smp.Put());
    if (FAILED(result)) {
        Log("CreateSamplerState failed: 0x%08lx", result);
        Store(handle, std::move(descriptor));
        return;
    }
    descriptor.kind = DKind::Sampler;
    Store(handle, std::move(descriptor));
}

void STDMETHODCALLTYPE Device::CopyDescriptors(UINT destCount, const D3D12_CPU_DESCRIPTOR_HANDLE* destStarts,
    const UINT* destSizes, UINT sourceCount, const D3D12_CPU_DESCRIPTOR_HANDLE* sourceStarts,
    const UINT* sourceSizes, D3D12_DESCRIPTOR_HEAP_TYPE type) {
    if ((destCount && !destStarts) || (sourceCount && !sourceStarts)) {
        Log("CopyDescriptors received a null range-start array dest_count=%u source_count=%u "
            "dest_starts=%p source_starts=%p type=%u",
            destCount, sourceCount, destStarts, sourceStarts, static_cast<unsigned int>(type));
        return;
    }
    const auto rangeSize = [](const UINT* sizes, UINT index) {
        return sizes ? sizes[index] : 1u;
    };
    UINT64 destTotal = 0, sourceTotal = 0;
    for (UINT i = 0; i < destCount; ++i) destTotal += rangeSize(destSizes, i);
    for (UINT i = 0; i < sourceCount; ++i) sourceTotal += rangeSize(sourceSizes, i);
    if (destTotal != sourceTotal || destTotal > SIZE_MAX / sizeof(Descriptor)) {
        Log("CopyDescriptors range counts do not match");
        return;
    }

    std::vector<Descriptor> copies;
    copies.reserve(static_cast<size_t>(sourceTotal));
    for (UINT i = 0; i < sourceCount; ++i) {
        UINT startIndex = 0;
        DescriptorHeap* heap = GetHeap(sourceStarts[i].ptr, &startIndex);
        const UINT size = rangeSize(sourceSizes, i);
        if (!heap || heap->hd.Type != type || startIndex > heap->items.size() ||
            size > heap->items.size() - startIndex) {
            Log("CopyDescriptors source range is invalid index=%u size=%u type=%u",
                startIndex, size, static_cast<unsigned int>(type));
            return;
        }
        for (UINT n = 0; n < size; ++n) copies.push_back(heap->items[startIndex + n]);
    }

    std::vector<std::pair<DescriptorHeap*, UINT>> destinations;
    destinations.reserve(destCount);
    for (UINT i = 0; i < destCount; ++i) {
        UINT startIndex = 0;
        DescriptorHeap* heap = GetHeap(destStarts[i].ptr, &startIndex);
        const UINT size = rangeSize(destSizes, i);
        if (!heap || heap->hd.Type != type || startIndex > heap->items.size() ||
            size > heap->items.size() - startIndex) {
            Log("CopyDescriptors destination range is invalid index=%u size=%u type=%u",
                startIndex, size, static_cast<unsigned int>(type));
            return;
        }
        destinations.emplace_back(heap, startIndex);
    }

    size_t cursor = 0;
    for (UINT i = 0; i < destCount; ++i) {
        auto* heap = destinations[i].first;
        const UINT startIndex = destinations[i].second;
        const UINT size = rangeSize(destSizes, i);
        for (UINT n = 0; n < size; ++n) {
            TraceTargetDescriptorWrite("copy", heap, startIndex + n, copies[cursor]);
            heap->items[startIndex + n] = copies[cursor++];
        }
    }
}

void STDMETHODCALLTYPE Device::CopyDescriptorsSimple(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE dest,
    D3D12_CPU_DESCRIPTOR_HANDLE source, D3D12_DESCRIPTOR_HEAP_TYPE type) {
    if (!count) return;
    UINT destIndex = 0, sourceIndex = 0;
    DescriptorHeap* destHeap = GetHeap(dest.ptr, &destIndex);
    DescriptorHeap* sourceHeap = GetHeap(source.ptr, &sourceIndex);
    if (!destHeap || !sourceHeap || destHeap->hd.Type != type || sourceHeap->hd.Type != type ||
        count > destHeap->items.size() - destIndex || count > sourceHeap->items.size() - sourceIndex) {
        Log("CopyDescriptorsSimple received an invalid descriptor range");
        return;
    }
    std::vector<Descriptor> copies;
    copies.reserve(count);
    for (UINT i = 0; i < count; ++i) copies.push_back(sourceHeap->items[sourceIndex + i]);
    for (UINT i = 0; i < count; ++i) {
        TraceTargetDescriptorWrite("copy-simple", destHeap, destIndex + i, copies[i],
                                   sourceHeap, sourceIndex + i);
        destHeap->items[destIndex + i] = copies[i];
    }
}
