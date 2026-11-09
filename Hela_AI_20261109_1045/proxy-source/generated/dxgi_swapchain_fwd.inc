// GENERATO da tools/gen_stubs.py - non modificare a mano.
#pragma once

struct SwapChainFwd : IDXGISwapChain4 {
    IDXGISwapChain4* real = nullptr;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) { return real->SetPrivateData(Name, DataSize, pData); }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) { return real->SetPrivateDataInterface(Name, pUnknown); }
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) { return real->GetPrivateData(Name, pDataSize, pData); }
    virtual HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) { return real->GetParent(riid, ppParent); }
    virtual HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppDevice) { return real->GetDevice(riid, ppDevice); }
    virtual HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) { return real->Present(SyncInterval, Flags); }
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void **ppSurface) { return real->GetBuffer(Buffer, riid, ppSurface); }
    virtual HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget) { return real->SetFullscreenState(Fullscreen, pTarget); }
    virtual HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *pFullscreen, IDXGIOutput **ppTarget) { return real->GetFullscreenState(pFullscreen, ppTarget); }
    virtual HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC *pDesc) { return real->GetDesc(pDesc); }
    virtual HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) { return real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags); }
    virtual HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *pNewTargetParameters) { return real->ResizeTarget(pNewTargetParameters); }
    virtual HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **ppOutput) { return real->GetContainingOutput(ppOutput); }
    virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS *pStats) { return real->GetFrameStatistics(pStats); }
    virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *pLastPresentCount) { return real->GetLastPresentCount(pLastPresentCount); }
    virtual HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1 *pDesc) { return real->GetDesc1(pDesc); }
    virtual HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) { return real->GetFullscreenDesc(pDesc); }
    virtual HRESULT STDMETHODCALLTYPE GetHwnd(HWND *pHwnd) { return real->GetHwnd(pHwnd); }
    virtual HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID refiid, void **ppUnk) { return real->GetCoreWindow(refiid, ppUnk); }
    virtual HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters) { return real->Present1(SyncInterval, PresentFlags, pPresentParameters); }
    virtual BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported(void) { return real->IsTemporaryMonoSupported(); }
    virtual HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput **ppRestrictToOutput) { return real->GetRestrictToOutput(ppRestrictToOutput); }
    virtual HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA *pColor) { return real->SetBackgroundColor(pColor); }
    virtual HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA *pColor) { return real->GetBackgroundColor(pColor); }
    virtual HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION Rotation) { return real->SetRotation(Rotation); }
    virtual HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION *pRotation) { return real->GetRotation(pRotation); }
    virtual HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) { return real->SetSourceSize(Width, Height); }
    virtual HRESULT STDMETHODCALLTYPE GetSourceSize(UINT *pWidth, UINT *pHeight) { return real->GetSourceSize(pWidth, pHeight); }
    virtual HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) { return real->SetMaximumFrameLatency(MaxLatency); }
    virtual HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *pMaxLatency) { return real->GetMaximumFrameLatency(pMaxLatency); }
    virtual HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject(void) { return real->GetFrameLatencyWaitableObject(); }
    virtual HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F *pMatrix) { return real->SetMatrixTransform(pMatrix); }
    virtual HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F *pMatrix) { return real->GetMatrixTransform(pMatrix); }
    virtual UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex(void) { return real->GetCurrentBackBufferIndex(); }
    virtual HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport) { return real->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport); }
    virtual HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) { return real->SetColorSpace1(ColorSpace); }
    virtual HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT *pCreationNodeMask, IUnknown *const *ppPresentQueue) { return real->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue); }
    virtual HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void *pMetaData) { return real->SetHDRMetaData(Type, Size, pMetaData); }
};
