// GENERATO da tools/gen_stubs.py - non modificare a mano.
#pragma once

struct Stub_ID3D12Heap : Obj<ID3D12Heap> {
    using Obj<ID3D12Heap>::Obj;
    virtual D3D12_HEAP_DESC STDMETHODCALLTYPE GetDesc(void) { Unimpl("ID3D12Heap::GetDesc"); return {}; }
};

struct Stub_ID3D12Resource : Obj<ID3D12Resource> {
    using Obj<ID3D12Resource>::Obj;
    virtual HRESULT STDMETHODCALLTYPE Map(UINT Subresource, const D3D12_RANGE *pReadRange, void **ppData) { Unimpl("ID3D12Resource::Map"); return E_NOTIMPL; }
    virtual void STDMETHODCALLTYPE Unmap(UINT Subresource, const D3D12_RANGE *pWrittenRange) { Unimpl("ID3D12Resource::Unmap"); }
    virtual D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc(void) { Unimpl("ID3D12Resource::GetDesc"); return {}; }
    virtual D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress(void) { Unimpl("ID3D12Resource::GetGPUVirtualAddress"); return {}; }
    virtual HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT DstSubresource, const D3D12_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) { Unimpl("ID3D12Resource::WriteToSubresource"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE ReadFromSubresource(void *pDstData, UINT DstRowPitch, UINT DstDepthPitch, UINT SrcSubresource, const D3D12_BOX *pSrcBox) { Unimpl("ID3D12Resource::ReadFromSubresource"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS *pHeapFlags) { Unimpl("ID3D12Resource::GetHeapProperties"); return E_NOTIMPL; }
};

struct Stub_ID3D12DescriptorHeap : Obj<ID3D12DescriptorHeap> {
    using Obj<ID3D12DescriptorHeap>::Obj;
    virtual D3D12_DESCRIPTOR_HEAP_DESC STDMETHODCALLTYPE GetDesc(void) { Unimpl("ID3D12DescriptorHeap::GetDesc"); return {}; }
    virtual D3D12_CPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetCPUDescriptorHandleForHeapStart(void) { Unimpl("ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart"); return {}; }
    virtual D3D12_GPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetGPUDescriptorHandleForHeapStart(void) { Unimpl("ID3D12DescriptorHeap::GetGPUDescriptorHandleForHeapStart"); return {}; }
};

struct Stub_ID3D12QueryHeap : Obj<ID3D12QueryHeap> {
    using Obj<ID3D12QueryHeap>::Obj;
};

struct Stub_ID3D12CommandSignature : Obj<ID3D12CommandSignature> {
    using Obj<ID3D12CommandSignature>::Obj;
};

struct Stub_ID3D12GraphicsCommandList : Obj<ID3D12GraphicsCommandList> {
    using Obj<ID3D12GraphicsCommandList>::Obj;
    virtual D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType(void) { Unimpl("ID3D12GraphicsCommandList::GetType"); return {}; }
    virtual HRESULT STDMETHODCALLTYPE Close(void) { Unimpl("ID3D12GraphicsCommandList::Close"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator *pAllocator, ID3D12PipelineState *pInitialState) { Unimpl("ID3D12GraphicsCommandList::Reset"); return E_NOTIMPL; }
    virtual void STDMETHODCALLTYPE ClearState(ID3D12PipelineState *pPipelineState) { Unimpl("ID3D12GraphicsCommandList::ClearState"); }
    virtual void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) { Unimpl("ID3D12GraphicsCommandList::DrawInstanced"); }
    virtual void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) { Unimpl("ID3D12GraphicsCommandList::DrawIndexedInstanced"); }
    virtual void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) { Unimpl("ID3D12GraphicsCommandList::Dispatch"); }
    virtual void STDMETHODCALLTYPE CopyBufferRegion(ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer, UINT64 SrcOffset, UINT64 NumBytes) { Unimpl("ID3D12GraphicsCommandList::CopyBufferRegion"); }
    virtual void STDMETHODCALLTYPE CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION *pDst, UINT DstX, UINT DstY, UINT DstZ, const D3D12_TEXTURE_COPY_LOCATION *pSrc, const D3D12_BOX *pSrcBox) { Unimpl("ID3D12GraphicsCommandList::CopyTextureRegion"); }
    virtual void STDMETHODCALLTYPE CopyResource(ID3D12Resource *pDstResource, ID3D12Resource *pSrcResource) { Unimpl("ID3D12GraphicsCommandList::CopyResource"); }
    virtual void STDMETHODCALLTYPE CopyTiles(ID3D12Resource *pTiledResource, const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate, const D3D12_TILE_REGION_SIZE *pTileRegionSize, ID3D12Resource *pBuffer, UINT64 BufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS Flags) { Unimpl("ID3D12GraphicsCommandList::CopyTiles"); }
    virtual void STDMETHODCALLTYPE ResolveSubresource(ID3D12Resource *pDstResource, UINT DstSubresource, ID3D12Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) { Unimpl("ID3D12GraphicsCommandList::ResolveSubresource"); }
    virtual void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) { Unimpl("ID3D12GraphicsCommandList::IASetPrimitiveTopology"); }
    virtual void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D12_VIEWPORT *pViewports) { Unimpl("ID3D12GraphicsCommandList::RSSetViewports"); }
    virtual void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D12_RECT *pRects) { Unimpl("ID3D12GraphicsCommandList::RSSetScissorRects"); }
    virtual void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT BlendFactor[ 4 ]) { Unimpl("ID3D12GraphicsCommandList::OMSetBlendFactor"); }
    virtual void STDMETHODCALLTYPE OMSetStencilRef(UINT StencilRef) { Unimpl("ID3D12GraphicsCommandList::OMSetStencilRef"); }
    virtual void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState *pPipelineState) { Unimpl("ID3D12GraphicsCommandList::SetPipelineState"); }
    virtual void STDMETHODCALLTYPE ResourceBarrier(UINT NumBarriers, const D3D12_RESOURCE_BARRIER *pBarriers) { Unimpl("ID3D12GraphicsCommandList::ResourceBarrier"); }
    virtual void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList *pCommandList) { Unimpl("ID3D12GraphicsCommandList::ExecuteBundle"); }
    virtual void STDMETHODCALLTYPE SetDescriptorHeaps(UINT NumDescriptorHeaps, ID3D12DescriptorHeap *const *ppDescriptorHeaps) { Unimpl("ID3D12GraphicsCommandList::SetDescriptorHeaps"); }
    virtual void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature *pRootSignature) { Unimpl("ID3D12GraphicsCommandList::SetComputeRootSignature"); }
    virtual void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature *pRootSignature) { Unimpl("ID3D12GraphicsCommandList::SetGraphicsRootSignature"); }
    virtual void STDMETHODCALLTYPE SetComputeRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) { Unimpl("ID3D12GraphicsCommandList::SetComputeRootDescriptorTable"); }
    virtual void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) { Unimpl("ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable"); }
    virtual void STDMETHODCALLTYPE SetComputeRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) { Unimpl("ID3D12GraphicsCommandList::SetComputeRoot32BitConstant"); }
    virtual void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) { Unimpl("ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant"); }
    virtual void STDMETHODCALLTYPE SetComputeRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void *pSrcData, UINT DestOffsetIn32BitValues) { Unimpl("ID3D12GraphicsCommandList::SetComputeRoot32BitConstants"); }
    virtual void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void *pSrcData, UINT DestOffsetIn32BitValues) { Unimpl("ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants"); }
    virtual void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) { Unimpl("ID3D12GraphicsCommandList::SetComputeRootConstantBufferView"); }
    virtual void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) { Unimpl("ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView"); }
    virtual void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) { Unimpl("ID3D12GraphicsCommandList::SetComputeRootShaderResourceView"); }
    virtual void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) { Unimpl("ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView"); }
    virtual void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) { Unimpl("ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView"); }
    virtual void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) { Unimpl("ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView"); }
    virtual void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *pView) { Unimpl("ID3D12GraphicsCommandList::IASetIndexBuffer"); }
    virtual void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW *pViews) { Unimpl("ID3D12GraphicsCommandList::IASetVertexBuffers"); }
    virtual void STDMETHODCALLTYPE SOSetTargets(UINT StartSlot, UINT NumViews, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews) { Unimpl("ID3D12GraphicsCommandList::SOSetTargets"); }
    virtual void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors, BOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor) { Unimpl("ID3D12GraphicsCommandList::OMSetRenderTargets"); }
    virtual void STDMETHODCALLTYPE ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12_CLEAR_FLAGS ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, const D3D12_RECT *pRects) { Unimpl("ID3D12GraphicsCommandList::ClearDepthStencilView"); }
    virtual void STDMETHODCALLTYPE ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView, const FLOAT ColorRGBA[ 4 ], UINT NumRects, const D3D12_RECT *pRects) { Unimpl("ID3D12GraphicsCommandList::ClearRenderTargetView"); }
    virtual void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource *pResource, const UINT Values[ 4 ], UINT NumRects, const D3D12_RECT *pRects) { Unimpl("ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint"); }
    virtual void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource *pResource, const FLOAT Values[ 4 ], UINT NumRects, const D3D12_RECT *pRects) { Unimpl("ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat"); }
    virtual void STDMETHODCALLTYPE DiscardResource(ID3D12Resource *pResource, const D3D12_DISCARD_REGION *pRegion) { Unimpl("ID3D12GraphicsCommandList::DiscardResource"); }
    virtual void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) { Unimpl("ID3D12GraphicsCommandList::BeginQuery"); }
    virtual void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) { Unimpl("ID3D12GraphicsCommandList::EndQuery"); }
    virtual void STDMETHODCALLTYPE ResolveQueryData(ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex, UINT NumQueries, ID3D12Resource *pDestinationBuffer, UINT64 AlignedDestinationBufferOffset) { Unimpl("ID3D12GraphicsCommandList::ResolveQueryData"); }
    virtual void STDMETHODCALLTYPE SetPredication(ID3D12Resource *pBuffer, UINT64 AlignedBufferOffset, D3D12_PREDICATION_OP Operation) { Unimpl("ID3D12GraphicsCommandList::SetPredication"); }
    virtual void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void *pData, UINT Size) { Unimpl("ID3D12GraphicsCommandList::SetMarker"); }
    virtual void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void *pData, UINT Size) { Unimpl("ID3D12GraphicsCommandList::BeginEvent"); }
    virtual void STDMETHODCALLTYPE EndEvent(void) { Unimpl("ID3D12GraphicsCommandList::EndEvent"); }
    virtual void STDMETHODCALLTYPE ExecuteIndirect(ID3D12CommandSignature *pCommandSignature, UINT MaxCommandCount, ID3D12Resource *pArgumentBuffer, UINT64 ArgumentBufferOffset, ID3D12Resource *pCountBuffer, UINT64 CountBufferOffset) { Unimpl("ID3D12GraphicsCommandList::ExecuteIndirect"); }
};

struct Stub_ID3D12CommandQueue : Obj<ID3D12CommandQueue> {
    using Obj<ID3D12CommandQueue>::Obj;
    virtual void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource *pResource, UINT NumResourceRegions, const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates, const D3D12_TILE_REGION_SIZE *pResourceRegionSizes, ID3D12Heap *pHeap, UINT NumRanges, const D3D12_TILE_RANGE_FLAGS *pRangeFlags, const UINT *pHeapRangeStartOffsets, const UINT *pRangeTileCounts, D3D12_TILE_MAPPING_FLAGS Flags) { Unimpl("ID3D12CommandQueue::UpdateTileMappings"); }
    virtual void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource *pDstResource, const D3D12_TILED_RESOURCE_COORDINATE *pDstRegionStartCoordinate, ID3D12Resource *pSrcResource, const D3D12_TILED_RESOURCE_COORDINATE *pSrcRegionStartCoordinate, const D3D12_TILE_REGION_SIZE *pRegionSize, D3D12_TILE_MAPPING_FLAGS Flags) { Unimpl("ID3D12CommandQueue::CopyTileMappings"); }
    virtual void STDMETHODCALLTYPE ExecuteCommandLists(UINT NumCommandLists, ID3D12CommandList *const *ppCommandLists) { Unimpl("ID3D12CommandQueue::ExecuteCommandLists"); }
    virtual void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void *pData, UINT Size) { Unimpl("ID3D12CommandQueue::SetMarker"); }
    virtual void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void *pData, UINT Size) { Unimpl("ID3D12CommandQueue::BeginEvent"); }
    virtual void STDMETHODCALLTYPE EndEvent(void) { Unimpl("ID3D12CommandQueue::EndEvent"); }
    virtual HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence *pFence, UINT64 Value) { Unimpl("ID3D12CommandQueue::Signal"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence *pFence, UINT64 Value) { Unimpl("ID3D12CommandQueue::Wait"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64 *pFrequency) { Unimpl("ID3D12CommandQueue::GetTimestampFrequency"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64 *pGpuTimestamp, UINT64 *pCpuTimestamp) { Unimpl("ID3D12CommandQueue::GetClockCalibration"); return E_NOTIMPL; }
    virtual D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc(void) { Unimpl("ID3D12CommandQueue::GetDesc"); return {}; }
};

struct Stub_ID3D12RootSignature : Obj<ID3D12RootSignature> {
    using Obj<ID3D12RootSignature>::Obj;
};

struct Stub_ID3D12PipelineState : Obj<ID3D12PipelineState> {
    using Obj<ID3D12PipelineState>::Obj;
    virtual HRESULT STDMETHODCALLTYPE GetCachedBlob(ID3DBlob **ppBlob) { Unimpl("ID3D12PipelineState::GetCachedBlob"); return E_NOTIMPL; }
};

struct Stub_ID3D12Fence : Obj<ID3D12Fence> {
    using Obj<ID3D12Fence>::Obj;
    virtual UINT64 STDMETHODCALLTYPE GetCompletedValue(void) { Unimpl("ID3D12Fence::GetCompletedValue"); return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 Value, HANDLE hEvent) { Unimpl("ID3D12Fence::SetEventOnCompletion"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE Signal(UINT64 Value) { Unimpl("ID3D12Fence::Signal"); return E_NOTIMPL; }
};

struct Stub_ID3D12CommandAllocator : Obj<ID3D12CommandAllocator> {
    using Obj<ID3D12CommandAllocator>::Obj;
    virtual HRESULT STDMETHODCALLTYPE Reset(void) { Unimpl("ID3D12CommandAllocator::Reset"); return E_NOTIMPL; }
};

struct Stub_ID3D12Device2 : Obj<ID3D12Device2> {
    using Obj<ID3D12Device2>::Obj;
    virtual UINT STDMETHODCALLTYPE GetNodeCount(void) { Unimpl("ID3D12Device2::GetNodeCount"); return {}; }
    virtual HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue) { Unimpl("ID3D12Device2::CreateCommandQueue"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **ppCommandAllocator) { Unimpl("ID3D12Device2::CreateCommandAllocator"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) { Unimpl("ID3D12Device2::CreateGraphicsPipelineState"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) { Unimpl("ID3D12Device2::CreateComputePipelineState"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *pCommandAllocator, ID3D12PipelineState *pInitialState, REFIID riid, void **ppCommandList) { Unimpl("ID3D12Device2::CreateCommandList"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) { Unimpl("ID3D12Device2::CheckFeatureSupport"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc, REFIID riid, void **ppvHeap) { Unimpl("ID3D12Device2::CreateDescriptorHeap"); return E_NOTIMPL; }
    virtual UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) { Unimpl("ID3D12Device2::GetDescriptorHandleIncrementSize"); return {}; }
    virtual HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT nodeMask, const void *pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void **ppvRootSignature) { Unimpl("ID3D12Device2::CreateRootSignature"); return E_NOTIMPL; }
    virtual void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) { Unimpl("ID3D12Device2::CreateConstantBufferView"); }
    virtual void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) { Unimpl("ID3D12Device2::CreateShaderResourceView"); }
    virtual void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource *pResource, ID3D12Resource *pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) { Unimpl("ID3D12Device2::CreateUnorderedAccessView"); }
    virtual void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource *pResource, const D3D12_RENDER_TARGET_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) { Unimpl("ID3D12Device2::CreateRenderTargetView"); }
    virtual void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource *pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) { Unimpl("ID3D12Device2::CreateDepthStencilView"); }
    virtual void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) { Unimpl("ID3D12Device2::CreateSampler"); }
    virtual void STDMETHODCALLTYPE CopyDescriptors(UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pDestDescriptorRangeStarts, const UINT *pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pSrcDescriptorRangeStarts, const UINT *pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) { Unimpl("ID3D12Device2::CopyDescriptors"); }
    virtual void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) { Unimpl("ID3D12Device2::CopyDescriptorsSimple"); }
    virtual D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs) { Unimpl("ID3D12Device2::GetResourceAllocationInfo"); return {}; }
    virtual D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(UINT nodeMask, D3D12_HEAP_TYPE heapType) { Unimpl("ID3D12Device2::GetCustomHeapProperties"); return {}; }
    virtual HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riidResource, void **ppvResource) { Unimpl("ID3D12Device2::CreateCommittedResource"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) { Unimpl("ID3D12Device2::CreateHeap"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid, void **ppvResource) { Unimpl("ID3D12Device2::CreatePlacedResource"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid, void **ppvResource) { Unimpl("ID3D12Device2::CreateReservedResource"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild *pObject, const SECURITY_ATTRIBUTES *pAttributes, DWORD Access, LPCWSTR Name, HANDLE *pHandle) { Unimpl("ID3D12Device2::CreateSharedHandle"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE NTHandle, REFIID riid, void **ppvObj) { Unimpl("ID3D12Device2::OpenSharedHandle"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE *pNTHandle) { Unimpl("ID3D12Device2::OpenSharedHandleByName"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable *const *ppObjects) { Unimpl("ID3D12Device2::MakeResident"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable *const *ppObjects) { Unimpl("ID3D12Device2::Evict"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) { Unimpl("ID3D12Device2::CreateFence"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason(void) { Unimpl("ID3D12Device2::GetDeviceRemovedReason"); return E_NOTIMPL; }
    virtual void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC *pResourceDesc, UINT FirstSubresource, UINT NumSubresources, UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows, UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes) { Unimpl("ID3D12Device2::GetCopyableFootprints"); }
    virtual HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) { Unimpl("ID3D12Device2::CreateQueryHeap"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) { Unimpl("ID3D12Device2::SetStablePowerState"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC *pDesc, ID3D12RootSignature *pRootSignature, REFIID riid, void **ppvCommandSignature) { Unimpl("ID3D12Device2::CreateCommandSignature"); return E_NOTIMPL; }
    virtual void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource *pTiledResource, UINT *pNumTilesForEntireResource, D3D12_PACKED_MIP_INFO *pPackedMipDesc, D3D12_TILE_SHAPE *pStandardTileShapeForNonPackedMips, UINT *pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips) { Unimpl("ID3D12Device2::GetResourceTiling"); }
    virtual LUID STDMETHODCALLTYPE GetAdapterLuid(void) { Unimpl("ID3D12Device2::GetAdapterLuid"); return {}; }
    virtual HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(const void *pLibraryBlob, SIZE_T BlobLength, REFIID riid, void **ppPipelineLibrary) { Unimpl("ID3D12Device2::CreatePipelineLibrary"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE SetEventOnMultipleFenceCompletion(ID3D12Fence *const *ppFences, const UINT64 *pFenceValues, UINT NumFences, D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags, HANDLE hEvent) { Unimpl("ID3D12Device2::SetEventOnMultipleFenceCompletion"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE SetResidencyPriority(UINT NumObjects, ID3D12Pageable *const *ppObjects, const D3D12_RESIDENCY_PRIORITY *pPriorities) { Unimpl("ID3D12Device2::SetResidencyPriority"); return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC *pDesc, REFIID riid, void **ppPipelineState) { Unimpl("ID3D12Device2::CreatePipelineState"); return E_NOTIMPL; }
};
