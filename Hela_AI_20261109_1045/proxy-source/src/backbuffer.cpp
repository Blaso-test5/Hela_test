#include "types.h"
#include "bridge.h"

namespace {

Device* GetDevice(IUnknown* source) {
    if (auto* queue = dynamic_cast<CommandQueue*>(source)) return queue->dev;
    return dynamic_cast<Device*>(source);
}

}

extern "C" HRESULT WINAPI D3D12On11GetD3D11Device(IUnknown* source, ID3D11Device** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    Device* device = GetDevice(source);
    if (!device || !device->d11) return E_INVALIDARG;
    device->d11->AddRef();
    *out = device->d11.Get();
    return S_OK;
}

extern "C" HRESULT WINAPI D3D12On11GetWrappedDevice(IUnknown* source, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    Device* device = GetDevice(source);
    return device ? device->QueryInterface(riid, out) : E_INVALIDARG;
}

extern "C" HRESULT WINAPI D3D12On11WrapBackbuffer(IUnknown* source, ID3D11Texture2D* texture,
    REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    Device* device = GetDevice(source);
    if (!device || !texture) return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC nativeDesc{};
    texture->GetDesc(&nativeDesc);
    auto* resource = new (std::nothrow) Resource(device);
    if (!resource) return E_OUTOFMEMORY;
    resource->desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource->desc.Width = nativeDesc.Width;
    resource->desc.Height = nativeDesc.Height;
    resource->desc.DepthOrArraySize = static_cast<UINT16>(nativeDesc.ArraySize);
    resource->desc.MipLevels = static_cast<UINT16>(nativeDesc.MipLevels);
    resource->desc.Format = nativeDesc.Format;
    resource->desc.SampleDesc = nativeDesc.SampleDesc;
    resource->desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (nativeDesc.BindFlags & D3D11_BIND_RENDER_TARGET)
        resource->desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (nativeDesc.BindFlags & D3D11_BIND_DEPTH_STENCIL)
        resource->desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (nativeDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
        resource->desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resource->hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource->hp.CreationNodeMask = resource->hp.VisibleNodeMask = 1;
    resource->backbuffer = true;
    ID3D11Resource* base = nullptr;
    HRESULT result = texture->QueryInterface(__uuidof(ID3D11Resource), reinterpret_cast<void**>(&base));
    if (FAILED(result)) {
        resource->Release();
        return result;
    }
    resource->tex = Ptr<ID3D11Resource>::Attach(base);
    {
        std::lock_guard<std::mutex> lock(device->swapLock);
        device->backbuffers.insert(resource);
    }
    result = resource->QueryInterface(riid, out);
    resource->Release();
    return result;
}

extern "C" void WINAPI D3D12On11ReleaseBackbufferViews(IUnknown* source) {
    if (Device* device = GetDevice(source)) device->ReleaseBackbufferViews();
}

extern "C" void WINAPI D3D12On11Flush(IUnknown* source) {
    if (Device* device = GetDevice(source)) {
        std::lock_guard<std::recursive_mutex> lock(device->ctxLock);
        if (device->ctx) device->ctx->Flush();
    }
}
