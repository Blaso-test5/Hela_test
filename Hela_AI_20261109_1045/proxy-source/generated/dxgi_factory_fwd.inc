// GENERATO da tools/gen_stubs.py - non modificare a mano.
#pragma once

struct FactoryFwd : IDXGIFactory7 {
    IDXGIFactory7* real = nullptr;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) { return real->SetPrivateData(Name, DataSize, pData); }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) { return real->SetPrivateDataInterface(Name, pUnknown); }
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) { return real->GetPrivateData(Name, pDataSize, pData); }
    virtual HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) { return real->GetParent(riid, ppParent); }
    virtual HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter, IDXGIAdapter **ppAdapter) { return real->EnumAdapters(Adapter, ppAdapter); }
    virtual HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags) { return real->MakeWindowAssociation(WindowHandle, Flags); }
    virtual HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND *pWindowHandle) { return real->GetWindowAssociation(pWindowHandle); }
    virtual HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) { return real->CreateSwapChain(pDevice, pDesc, ppSwapChain); }
    virtual HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter) { return real->CreateSoftwareAdapter(Module, ppAdapter); }
    virtual HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter, IDXGIAdapter1 **ppAdapter) { return real->EnumAdapters1(Adapter, ppAdapter); }
    virtual BOOL STDMETHODCALLTYPE IsCurrent(void) { return real->IsCurrent(); }
    virtual BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled(void) { return real->IsWindowedStereoEnabled(); }
    virtual HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) { return real->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain); }
    virtual HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) { return real->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain); }
    virtual HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE hResource, LUID *pLuid) { return real->GetSharedResourceAdapterLuid(hResource, pLuid); }
    virtual HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) { return real->RegisterStereoStatusWindow(WindowHandle, wMsg, pdwCookie); }
    virtual HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE hEvent, DWORD *pdwCookie) { return real->RegisterStereoStatusEvent(hEvent, pdwCookie); }
    virtual void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD dwCookie) { real->UnregisterStereoStatus(dwCookie); }
    virtual HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) { return real->RegisterOcclusionStatusWindow(WindowHandle, wMsg, pdwCookie); }
    virtual HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD *pdwCookie) { return real->RegisterOcclusionStatusEvent(hEvent, pdwCookie); }
    virtual void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD dwCookie) { real->UnregisterOcclusionStatus(dwCookie); }
    virtual HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) { return real->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain); }
    virtual UINT STDMETHODCALLTYPE GetCreationFlags(void) { return real->GetCreationFlags(); }
    virtual HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void **ppvAdapter) { return real->EnumAdapterByLuid(AdapterLuid, riid, ppvAdapter); }
    virtual HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID riid, void **ppvAdapter) { return real->EnumWarpAdapter(riid, ppvAdapter); }
    virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) { return real->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize); }
    virtual HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void **ppvAdapter) { return real->EnumAdapterByGpuPreference(Adapter, GpuPreference, riid, ppvAdapter); }
    virtual HRESULT STDMETHODCALLTYPE RegisterAdaptersChangedEvent(HANDLE hEvent, DWORD *pdwCookie) { return real->RegisterAdaptersChangedEvent(hEvent, pdwCookie); }
    virtual HRESULT STDMETHODCALLTYPE UnregisterAdaptersChangedEvent(DWORD dwCookie) { return real->UnregisterAdaptersChangedEvent(dwCookie); }
};
