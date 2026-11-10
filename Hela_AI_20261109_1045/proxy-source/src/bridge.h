#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#if defined(D3D12ON11_BRIDGE_IMPORT)
#define D3D12ON11_BRIDGE_API __declspec(dllimport)
#else
#define D3D12ON11_BRIDGE_API __declspec(dllexport)
#endif

struct __declspec(uuid("4f5a1b7c-5f2d-4c8f-a714-1b3d2c9e6f80")) ID3D12On11AdapterBridge : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetNativeAdapter(IDXGIAdapter** adapter) = 0;
};

extern "C" {
D3D12ON11_BRIDGE_API HRESULT WINAPI D3D12On11GetD3D11Device(IUnknown* source, ID3D11Device** device);
D3D12ON11_BRIDGE_API HRESULT WINAPI D3D12On11GetWrappedDevice(IUnknown* source, REFIID riid, void** device);
D3D12ON11_BRIDGE_API HRESULT WINAPI D3D12On11WrapBackbuffer(IUnknown* source, ID3D11Texture2D* texture,
                                                            REFIID riid, void** resource);
D3D12ON11_BRIDGE_API void WINAPI D3D12On11ReleaseBackbufferViews(IUnknown* source);
D3D12ON11_BRIDGE_API void WINAPI D3D12On11Flush(IUnknown* source);
D3D12ON11_BRIDGE_API void WINAPI D3D12On11DumpBackbufferSourceHistory();
D3D12ON11_BRIDGE_API void WINAPI D3D12On11LogUnsupported(const char* operation);
D3D12ON11_BRIDGE_API void WINAPI D3D12On11LogMessage(const char* message);
D3D12ON11_BRIDGE_API void WINAPI D3D12On11TraceCaller(const char* api, const void* returnAddress);
D3D12ON11_BRIDGE_API void WINAPI D3D12On11TraceApi(const char* api);
D3D12ON11_BRIDGE_API void WINAPI D3D12On11TraceSwapChainCreated();
D3D12ON11_BRIDGE_API void WINAPI D3D12On11TracePresent(const char* api, HRESULT result);
}
