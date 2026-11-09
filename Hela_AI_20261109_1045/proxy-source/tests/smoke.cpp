#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <string>

static int RunComputeSmoke(ID3D12Device* device) {
    wchar_t systemDir[MAX_PATH]{};
    const UINT systemChars = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
    if (!systemChars || systemChars >= ARRAYSIZE(systemDir)) return 11;
    std::wstring systemD3d12Path(systemDir, systemChars);
    systemD3d12Path += L"\\d3d12.dll";
    HMODULE systemD3d12 = LoadLibraryExW(systemD3d12Path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!systemD3d12) return 11;
    using SerializeRoot = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
                                           ID3DBlob**, ID3DBlob**);
    auto serializeRoot = reinterpret_cast<SerializeRoot>(GetProcAddress(systemD3d12, "D3D12SerializeRootSignature"));
    if (!serializeRoot) {
        FreeLibrary(systemD3d12);
        return 11;
    }

    const char shaderSource[] =
        "cbuffer Input : register(b0) { uint Bias; }"
        "cbuffer Immediate : register(b1) { uint Mode; uint RootBias; }"
        "ByteAddressBuffer Input : register(t0);"
        "Texture2D<uint> TextureInput : register(t1);"
        "RWByteAddressBuffer Output : register(u0);"
        "RWTexture2D<uint> TextureOutput : register(u1);"
        "[numthreads(1,1,1)]"
        "void main(uint3 id : SV_DispatchThreadID) {"
        "if (Mode == 0) { Output.Store(0, Input.Load(0) + Bias + RootBias); TextureOutput[uint2(0,0)] = 0xAABBCCDD; }"
        "else { Output.Store(4, TextureInput.Load(int3(0,0,0))); }}";
    ID3DBlob* shader = nullptr;
    ID3DBlob* diagnostics = nullptr;
    HRESULT result = D3DCompile(shaderSource, sizeof(shaderSource) - 1, "smoke.hlsl", nullptr, nullptr,
                                "main", "cs_5_0", 0, 0, &shader, &diagnostics);
    if (FAILED(result)) {
        if (diagnostics) std::fprintf(stderr, "%s\n", static_cast<const char*>(diagnostics->GetBufferPointer()));
        if (diagnostics) diagnostics->Release();
        return 10;
    }
    if (diagnostics) diagnostics->Release();

    D3D12_DESCRIPTOR_RANGE ranges[3]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 2;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 0;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[2].NumDescriptors = 2;
    ranges[2].BaseShaderRegister = 0;
    ranges[2].OffsetInDescriptorsFromTableStart = 0;
    D3D12_ROOT_PARAMETER parameters[4]{};
    for (UINT i = 0; i < ARRAYSIZE(ranges); ++i) {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i].DescriptorTable.NumDescriptorRanges = 1;
        parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
        parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants.ShaderRegister = 1;
    parameters[3].Constants.Num32BitValues = 2;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = ARRAYSIZE(parameters);
    rootDesc.pParameters = parameters;
    ID3DBlob* serializedRoot = nullptr;
    result = serializeRoot(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRoot, &diagnostics);
    if (FAILED(result)) {
        if (diagnostics) std::fprintf(stderr, "%s\n", static_cast<const char*>(diagnostics->GetBufferPointer()));
        if (diagnostics) diagnostics->Release();
        FreeLibrary(systemD3d12);
        shader->Release();
        return 11;
    }
    if (diagnostics) diagnostics->Release();

    ID3D12RootSignature* rootSignature = nullptr;
    result = device->CreateRootSignature(0, serializedRoot->GetBufferPointer(), serializedRoot->GetBufferSize(),
                                          __uuidof(ID3D12RootSignature),
                                          reinterpret_cast<void**>(&rootSignature));
    serializedRoot->Release();
    FreeLibrary(systemD3d12);
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateRootSignature failed: 0x%08lx\n", result);
        shader->Release();
        return 12;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = rootSignature;
    pipelineDesc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
    ID3D12PipelineState* pipeline = nullptr;
    result = device->CreateComputePipelineState(&pipelineDesc, __uuidof(ID3D12PipelineState),
                                                 reinterpret_cast<void**>(&pipeline));
    shader->Release();
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateComputePipelineState failed: 0x%08lx\n", result);
        rootSignature->Release();
        return 13;
    }

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = defaultHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = 256;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* output = nullptr;
    result = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&output));
    if (FAILED(result)) {
        pipeline->Release();
        rootSignature->Release();
        return 14;
    }

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.NumDescriptors = 5;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* descriptorHeap = nullptr;
    result = device->CreateDescriptorHeap(&descriptorHeapDesc, __uuidof(ID3D12DescriptorHeap),
                                           reinterpret_cast<void**>(&descriptorHeap));
    if (FAILED(result)) {
        output->Release();
        pipeline->Release();
        rootSignature->Release();
        return 15;
    }
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = uploadHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC constantBufferDesc = bufferDesc;
    constantBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource* input = nullptr;
    result = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &constantBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        __uuidof(ID3D12Resource), reinterpret_cast<void**>(&input));
    if (FAILED(result)) {
        descriptorHeap->Release();
        output->Release();
        pipeline->Release();
        rootSignature->Release();
        return 15;
    }
    void* inputData = nullptr;
    D3D12_RANGE noRead{0, 0};
    result = input->Map(0, &noRead, &inputData);
    if (FAILED(result) || !inputData) {
        input->Release();
        descriptorHeap->Release();
        output->Release();
        pipeline->Release();
        rootSignature->Release();
        return 15;
    }
    *static_cast<UINT*>(inputData) = 0x1234AB00;
    D3D12_RANGE written{0, sizeof(UINT)};
    input->Unmap(0, &written);

    ID3D12Resource* constantInput = nullptr;
    result = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &constantBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        __uuidof(ID3D12Resource), reinterpret_cast<void**>(&constantInput));
    if (FAILED(result)) {
        input->Release();
        descriptorHeap->Release();
        output->Release();
        pipeline->Release();
        rootSignature->Release();
        return 15;
    }
    void* constantData = nullptr;
    result = constantInput->Map(0, &noRead, &constantData);
    if (FAILED(result) || !constantData) {
        constantInput->Release();
        input->Release();
        descriptorHeap->Release();
        output->Release();
        pipeline->Release();
        rootSignature->Release();
        return 15;
    }
    *static_cast<UINT*>(constantData) = 0;
    constantInput->Unmap(0, &written);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
    cbv.BufferLocation = constantInput->GetGPUVirtualAddress();
    cbv.SizeInBytes = static_cast<UINT>(constantBufferDesc.Width);
    const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    const UINT descriptorStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CreateConstantBufferView(&cbv, cpuStart);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_TYPELESS;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = 64;
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle{cpuStart.ptr + descriptorStride};
    device->CreateShaderResourceView(input, &srv, srvHandle);

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = 4;
    textureDesc.Height = 4;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R32_UINT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* textureSource = nullptr;
    ID3D12Resource* textureCopy = nullptr;
    result = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&textureSource));
    if (SUCCEEDED(result))
        result = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(&textureCopy));
    if (FAILED(result)) {
        if (textureSource) textureSource->Release();
        if (textureCopy) textureCopy->Release();
        constantInput->Release();
        input->Release();
        descriptorHeap->Release();
        output->Release();
        pipeline->Release();
        rootSignature->Release();
        return 15;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC textureSrv{};
    textureSrv.Format = textureDesc.Format;
    textureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    textureSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    textureSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandle{cpuStart.ptr + 2 * descriptorStride};
    device->CreateShaderResourceView(textureCopy, &textureSrv, textureSrvHandle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 64;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    D3D12_CPU_DESCRIPTOR_HANDLE uavHandle{cpuStart.ptr + 3 * descriptorStride};
    device->CreateUnorderedAccessView(output, nullptr, &uav, uavHandle);
    D3D12_UNORDERED_ACCESS_VIEW_DESC textureUav{};
    textureUav.Format = textureDesc.Format;
    textureUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE textureUavHandle{cpuStart.ptr + 4 * descriptorStride};
    device->CreateUnorderedAccessView(textureSource, nullptr, &textureUav, textureUavHandle);

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    readbackHeap.CreationNodeMask = readbackHeap.VisibleNodeMask = 1;
    ID3D12Resource* readback = nullptr;
    result = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&readback));
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    if (SUCCEEDED(result))
        result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
            __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&allocator));
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    if (SUCCEEDED(result))
        result = device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                                             reinterpret_cast<void**>(&queue));
    if (SUCCEEDED(result))
        result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator,
            nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&list));
    if (SUCCEEDED(result)) {
        list->SetPipelineState(pipeline);
        list->SetComputeRootSignature(rootSignature);
        ID3D12DescriptorHeap* heaps[] = {descriptorHeap};
        list->SetDescriptorHeaps(1, heaps);
        D3D12_GPU_DESCRIPTOR_HANDLE cbvGpu = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{cbvGpu.ptr + descriptorStride};
        D3D12_GPU_DESCRIPTOR_HANDLE uavGpu{cbvGpu.ptr + 3 * descriptorStride};
        list->SetComputeRootDescriptorTable(0, cbvGpu);
        list->SetComputeRootDescriptorTable(1, srvGpu);
        list->SetComputeRootDescriptorTable(2, uavGpu);
        const UINT firstConstants[] = {0, 0xCD};
        list->SetComputeRoot32BitConstants(3, ARRAYSIZE(firstConstants), firstConstants, 0);
        list->Dispatch(1, 1, 1);
        list->CopyResource(textureCopy, textureSource);
        const UINT readTextureConstants[] = {1, 0};
        list->SetComputeRoot32BitConstants(3, ARRAYSIZE(readTextureConstants), readTextureConstants, 0);
        list->Dispatch(1, 1, 1);
        list->CopyBufferRegion(readback, 0, output, 0, bufferDesc.Width);
        result = list->Close();
    }
    if (SUCCEEDED(result))
        result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                     reinterpret_cast<void**>(&fence));
    if (SUCCEEDED(result)) {
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        result = queue->Signal(fence, 1);
    }
    void* data = nullptr;
    D3D12_RANGE readRange{0, 2 * sizeof(UINT)};
    if (SUCCEEDED(result)) result = readback->Map(0, &readRange, &data);
    if (SUCCEEDED(result) && data) {
        const auto* values = static_cast<const UINT*>(data);
        if (values[0] != 0x1234ABCD || values[1] != 0xAABBCCDD) {
            std::fprintf(stderr, "Unexpected compute results: 0x%08x, 0x%08x\n", values[0], values[1]);
            result = E_FAIL;
        }
    }
    if (FAILED(result)) std::fprintf(stderr, "Root signature/PSO/dispatch/readback failed: 0x%08lx\n", result);
    if (data) readback->Unmap(0, &readRange);
    if (fence) fence->Release();
    if (list) list->Release();
    if (queue) queue->Release();
    if (allocator) allocator->Release();
    if (readback) readback->Release();
    textureCopy->Release();
    textureSource->Release();
    descriptorHeap->Release();
    constantInput->Release();
    input->Release();
    output->Release();
    pipeline->Release();
    rootSignature->Release();
    return FAILED(result) ? 16 : 0;
}

static int RunTextureFootprintSmoke(ID3D12Device* device, DXGI_FORMAT format) {
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = 8;
    textureDesc.Height = 4;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = format;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows, &rowSize, &totalBytes);
    if (!totalBytes || rows != textureDesc.Height || rowSize != textureDesc.Width * sizeof(UINT))
        return 20;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = defaultHeap.VisibleNodeMask = 1;
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = uploadHeap.VisibleNodeMask = 1;
    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    readbackHeap.CreationNodeMask = readbackHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* texture = nullptr;
    ID3D12Resource* upload = nullptr;
    ID3D12Resource* readback = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE event = nullptr;
    HRESULT result = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&texture));
    if (SUCCEEDED(result))
        result = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(&upload));
    if (SUCCEEDED(result))
        result = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(&readback));
    void* uploadData = nullptr;
    D3D12_RANGE noRead{0, 0};
    if (SUCCEEDED(result)) result = upload->Map(0, &noRead, &uploadData);
    if (SUCCEEDED(result) && uploadData) {
        auto* bytes = static_cast<uint8_t*>(uploadData);
        for (UINT y = 0; y < textureDesc.Height; ++y) {
            auto* row = reinterpret_cast<UINT*>(bytes + footprint.Offset +
                static_cast<UINT64>(y) * footprint.Footprint.RowPitch);
            for (UINT x = 0; x < textureDesc.Width; ++x)
                row[x] = 0xA5000000u | (y << 8) | x;
        }
        D3D12_RANGE written{0, static_cast<SIZE_T>(totalBytes)};
        upload->Unmap(0, &written);
    } else if (uploadData) {
        upload->Unmap(0, &noRead);
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (SUCCEEDED(result))
        result = device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
            reinterpret_cast<void**>(&queue));
    if (SUCCEEDED(result))
        result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&allocator));
    if (SUCCEEDED(result))
        result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
            nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&list));
    if (SUCCEEDED(result)) {
        D3D12_TEXTURE_COPY_LOCATION textureLocation{};
        textureLocation.pResource = texture;
        textureLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        textureLocation.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION uploadLocation{};
        uploadLocation.pResource = upload;
        uploadLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        uploadLocation.PlacedFootprint = footprint;
        list->CopyTextureRegion(&textureLocation, 0, 0, 0, &uploadLocation, nullptr);

        const D3D12_BOX partialBox{2, 1, 0, 6, 3, 1};
        list->CopyTextureRegion(&textureLocation, 1, 1, 0, &uploadLocation, &partialBox);

        D3D12_TEXTURE_COPY_LOCATION readbackLocation{};
        readbackLocation.pResource = readback;
        readbackLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        readbackLocation.PlacedFootprint = footprint;
        list->CopyTextureRegion(&readbackLocation, 0, 0, 0, &textureLocation, nullptr);
        const D3D12_BOX readbackBox{0, 0, 0, 4, 2, 1};
        list->CopyTextureRegion(&readbackLocation, 4, 2, 0, &textureLocation, &readbackBox);
        result = list->Close();
    }
    if (SUCCEEDED(result))
        result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
            reinterpret_cast<void**>(&fence));
    if (SUCCEEDED(result)) {
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        result = queue->Signal(fence, 1);
    }
    if (SUCCEEDED(result) && fence->GetCompletedValue() < 1) {
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) result = HRESULT_FROM_WIN32(GetLastError());
        else {
            result = fence->SetEventOnCompletion(1, event);
            if (SUCCEEDED(result) && WaitForSingleObject(event, INFINITE) != WAIT_OBJECT_0)
                result = HRESULT_FROM_WIN32(GetLastError());
        }
    }

    void* readbackData = nullptr;
    D3D12_RANGE readRange{0, static_cast<SIZE_T>(totalBytes)};
    if (SUCCEEDED(result)) result = readback->Map(0, &readRange, &readbackData);
    if (SUCCEEDED(result) && readbackData) {
        const auto* bytes = static_cast<const uint8_t*>(readbackData);
        const auto textureValue = [](UINT x, UINT y) {
            const UINT sourceX = y >= 1 && y < 3 && x >= 1 && x < 5 ? x + 1 : x;
            return 0xA5000000u | (y << 8) | sourceX;
        };
        for (UINT y = 0; y < textureDesc.Height && SUCCEEDED(result); ++y) {
            const auto* row = reinterpret_cast<const UINT*>(bytes + footprint.Offset +
                static_cast<UINT64>(y) * footprint.Footprint.RowPitch);
            for (UINT x = 0; x < textureDesc.Width; ++x) {
                const bool readbackRegion = y >= 2 && x >= 4;
                const UINT expected = readbackRegion
                    ? textureValue(x - 4, y - 2)
                    : textureValue(x, y);
                if (row[x] != expected) {
                    std::fprintf(stderr, "Texture footprint mismatch at (%u,%u): 0x%08x != 0x%08x\n",
                        x, y, row[x], expected);
                    result = E_FAIL;
                    break;
                }
            }
        }
        readback->Unmap(0, &readRange);
    }
    if (FAILED(result)) std::fprintf(stderr, "Texture footprint upload/readback failed: 0x%08lx\n", result);
    if (event) CloseHandle(event);
    if (fence) fence->Release();
    if (queue) queue->Release();
    if (list) list->Release();
    if (allocator) allocator->Release();
    if (readback) readback->Release();
    if (upload) upload->Release();
    if (texture) texture->Release();
    return FAILED(result) ? 21 : 0;
}

static int RunTiledResourceSmoke(ID3D12Device* device) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 1;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ID3D12Resource* resource = nullptr;
    HRESULT result = device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&resource));
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateReservedResource failed: 0x%08lx\n", result);
        return 22;
    }

    UINT tileCount = 0;
    D3D12_PACKED_MIP_INFO packedMips{};
    D3D12_TILE_SHAPE tileShape{};
    UINT subresourceCount = 1;
    D3D12_SUBRESOURCE_TILING subresourceTiling{};
    device->GetResourceTiling(resource, &tileCount, &packedMips, &tileShape,
        &subresourceCount, 0, &subresourceTiling);
    if (tileCount != 1 || packedMips.NumPackedMips != 1 || subresourceCount != 1) {
        std::fprintf(stderr, "Unexpected tiled-resource layout: tiles=%u packed-mips=%u subresources=%u\n",
            tileCount, packedMips.NumPackedMips, subresourceCount);
        resource->Release();
        return 23;
    }
    D3D12_HEAP_PROPERTIES reservedHeapProperties{};
    D3D12_HEAP_FLAGS reservedHeapFlags{};
    if (resource->GetHeapProperties(&reservedHeapProperties, &reservedHeapFlags) != E_INVALIDARG) {
        std::fprintf(stderr, "Reserved resource unexpectedly reported committed heap properties\n");
        resource->Release();
        return 24;
    }

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.NumDescriptors = 1;
    ID3D12DescriptorHeap* descriptorHeap = nullptr;
    result = device->CreateDescriptorHeap(&descriptorHeapDesc, __uuidof(ID3D12DescriptorHeap),
        reinterpret_cast<void**>(&descriptorHeap));
    if (FAILED(result)) {
        std::fprintf(stderr, "Descriptor heap for tiled UAV failed: 0x%08lx\n", result);
        resource->Release();
        return 25;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(resource, nullptr, &uavDesc,
        descriptorHeap->GetCPUDescriptorHandleForHeapStart());
    descriptorHeap->Release();

    D3D12_HEAP_DESC heapDesc{};
    heapDesc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapDesc.Properties.CreationNodeMask = heapDesc.Properties.VisibleNodeMask = 1;
    ID3D12Heap* tilePool = nullptr;
    result = device->CreateHeap(&heapDesc, __uuidof(ID3D12Heap),
        reinterpret_cast<void**>(&tilePool));
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateHeap for tile pool failed: 0x%08lx\n", result);
        resource->Release();
        return 26;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    result = device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
        reinterpret_cast<void**>(&queue));
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateCommandQueue for tiled-resource smoke failed: 0x%08lx\n", result);
        tilePool->Release();
        resource->Release();
        return 27;
    }

    D3D12_TILED_RESOURCE_COORDINATE coordinate{};
    D3D12_TILE_REGION_SIZE region{};
    region.NumTiles = 1;
    const D3D12_TILE_RANGE_FLAGS rangeFlag = D3D12_TILE_RANGE_FLAG_NONE;
    const UINT heapOffset = 0;
    const UINT rangeTileCount = 1;
    queue->UpdateTileMappings(resource, 1, &coordinate, &region, tilePool, 1,
        &rangeFlag, &heapOffset, &rangeTileCount, D3D12_TILE_MAPPING_FLAG_NONE);
    queue->CopyTileMappings(resource, &coordinate, resource, &coordinate, &region,
        D3D12_TILE_MAPPING_FLAG_NONE);

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = defaultHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC tileBufferDesc{};
    tileBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    tileBufferDesc.Width = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    tileBufferDesc.Height = 1;
    tileBufferDesc.DepthOrArraySize = 1;
    tileBufferDesc.MipLevels = 1;
    tileBufferDesc.SampleDesc.Count = 1;
    tileBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* tileBuffer = nullptr;
    result = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &tileBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        __uuidof(ID3D12Resource), reinterpret_cast<void**>(&tileBuffer));
    if (FAILED(result)) {
        std::fprintf(stderr, "Tile-copy buffer creation failed: 0x%08lx\n", result);
        queue->Release();
        tilePool->Release();
        resource->Release();
        return 28;
    }

    ID3D12CommandAllocator* allocator = nullptr;
    result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&allocator));
    ID3D12GraphicsCommandList* list = nullptr;
    if (SUCCEEDED(result))
        result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator, nullptr, __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void**>(&list));
    if (SUCCEEDED(result)) {
        list->CopyTiles(resource, &coordinate, &region, tileBuffer, 0,
            D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
        list->CopyTiles(resource, &coordinate, &region, tileBuffer, 0,
            D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
        result = list->Close();
    }
    if (SUCCEEDED(result)) {
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
    }
    if (FAILED(result)) {
        std::fprintf(stderr, "Tile-copy command recording failed: 0x%08lx\n", result);
        if (list) list->Release();
        if (allocator) allocator->Release();
        tileBuffer->Release();
        queue->Release();
        tilePool->Release();
        resource->Release();
        return 29;
    }

    list->Release();
    allocator->Release();
    tileBuffer->Release();
    queue->Release();
    tilePool->Release();
    resource->Release();
    return 0;
}

static int RunPlacedResourceSmoke(ID3D12Device* device) {
    D3D12_HEAP_DESC heapDesc{};
    heapDesc.SizeInBytes = 2 * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapDesc.Properties.CreationNodeMask = heapDesc.Properties.VisibleNodeMask = 1;
    heapDesc.Flags = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES |
        D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;

    ID3D12Heap* heap = nullptr;
    HRESULT result = device->CreateHeap(&heapDesc, __uuidof(ID3D12Heap),
        reinterpret_cast<void**>(&heap));
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateHeap for placed-resource smoke failed: 0x%08lx\n", result);
        return 30;
    }

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = 4096;
    resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* first = nullptr;
    result = device->CreatePlacedResource(heap, 0, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&first));
    if (FAILED(result)) {
        std::fprintf(stderr, "CreatePlacedResource at offset 0 failed: 0x%08lx\n", result);
        heap->Release();
        return 31;
    }

    ID3D12Resource* overlap = nullptr;
    result = device->CreatePlacedResource(heap, 0, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&overlap));
    if (result != E_INVALIDARG || overlap) {
        std::fprintf(stderr, "Overlapping active placement was not rejected: 0x%08lx\n", result);
        if (overlap) overlap->Release();
        first->Release();
        heap->Release();
        return 32;
    }

    ID3D12Resource* second = nullptr;
    result = device->CreatePlacedResource(heap, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
        &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        __uuidof(ID3D12Resource), reinterpret_cast<void**>(&second));
    if (FAILED(result)) {
        std::fprintf(stderr, "Non-overlapping placed resource failed: 0x%08lx\n", result);
        first->Release();
        heap->Release();
        return 33;
    }

    D3D12_HEAP_PROPERTIES properties{};
    D3D12_HEAP_FLAGS flags{};
    if (FAILED(first->GetHeapProperties(&properties, &flags)) ||
        properties.Type != D3D12_HEAP_TYPE_UPLOAD || flags != heapDesc.Flags) {
        std::fprintf(stderr, "Placed resource returned incorrect heap properties\n");
        second->Release();
        first->Release();
        heap->Release();
        return 34;
    }

    first->Release();
    ID3D12Resource* reused = nullptr;
    result = device->CreatePlacedResource(heap, 0, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&reused));
    if (FAILED(result)) {
        std::fprintf(stderr, "Released placement range was not reusable: 0x%08lx\n", result);
        second->Release();
        heap->Release();
        return 35;
    }

    reused->Release();
    second->Release();
    heap->Release();
    return 0;
}

static int RunIndirectDispatchSmoke(ID3D12Device* device) {
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
    signatureDesc.ByteStride = sizeof(UINT) * 3;
    signatureDesc.NumArgumentDescs = 1;
    signatureDesc.pArgumentDescs = &argument;

    D3D12_COMMAND_SIGNATURE_DESC invalidDesc = signatureDesc;
    invalidDesc.ByteStride = sizeof(UINT) * 2;
    ID3D12CommandSignature* invalidSignature = nullptr;
    HRESULT result = device->CreateCommandSignature(&invalidDesc, nullptr,
        __uuidof(ID3D12CommandSignature), reinterpret_cast<void**>(&invalidSignature));
    if (result != E_INVALIDARG || invalidSignature) {
        std::fprintf(stderr, "Invalid command signature was not rejected: 0x%08lx\n", result);
        if (invalidSignature) invalidSignature->Release();
        return 36;
    }

    ID3D12CommandSignature* signature = nullptr;
    result = device->CreateCommandSignature(&signatureDesc, nullptr,
        __uuidof(ID3D12CommandSignature), reinterpret_cast<void**>(&signature));
    if (FAILED(result) || !signature) {
        std::fprintf(stderr, "Dispatch command signature creation failed: 0x%08lx\n", result);
        return 37;
    }

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = uploadHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC argumentBufferDesc{};
    argumentBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    argumentBufferDesc.Width = 256;
    argumentBufferDesc.Height = 1;
    argumentBufferDesc.DepthOrArraySize = 1;
    argumentBufferDesc.MipLevels = 1;
    argumentBufferDesc.SampleDesc.Count = 1;
    argumentBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* argumentBuffer = nullptr;
    result = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &argumentBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        __uuidof(ID3D12Resource), reinterpret_cast<void**>(&argumentBuffer));
    if (FAILED(result)) {
        std::fprintf(stderr, "Indirect argument buffer creation failed: 0x%08lx\n", result);
        signature->Release();
        return 38;
    }
    void* mapped = nullptr;
    D3D12_RANGE noReads{0, 0};
    result = argumentBuffer->Map(0, &noReads, &mapped);
    if (FAILED(result) || !mapped) {
        std::fprintf(stderr, "Indirect argument buffer map failed: 0x%08lx\n", result);
        argumentBuffer->Release();
        signature->Release();
        return 39;
    }
    const UINT dispatchArgs[3] = {1, 1, 1};
    std::memcpy(mapped, dispatchArgs, sizeof(dispatchArgs));
    D3D12_RANGE written{0, sizeof(dispatchArgs)};
    argumentBuffer->Unmap(0, &written);

    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Fence* fence = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&allocator));
    if (SUCCEEDED(result))
        result = device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
            reinterpret_cast<void**>(&queue));
    if (SUCCEEDED(result))
        result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
            nullptr, __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void**>(&commandList));
    if (SUCCEEDED(result))
        result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
            reinterpret_cast<void**>(&fence));
    if (FAILED(result)) {
        std::fprintf(stderr, "Indirect command objects creation failed: 0x%08lx\n", result);
        if (fence) fence->Release();
        if (commandList) commandList->Release();
        if (queue) queue->Release();
        if (allocator) allocator->Release();
        argumentBuffer->Release();
        signature->Release();
        return 40;
    }

    commandList->ExecuteIndirect(signature, 1, argumentBuffer, 0, nullptr, 0);
    result = commandList->Close();
    if (SUCCEEDED(result)) {
        ID3D12CommandList* lists[] = {commandList};
        queue->ExecuteCommandLists(1, lists);
        result = queue->Signal(fence, 1);
        if (SUCCEEDED(result)) result = queue->Wait(fence, 1);
    }
    if (FAILED(result) || fence->GetCompletedValue() != 1) {
        std::fprintf(stderr, "Indirect dispatch execution failed: 0x%08lx\n", result);
        fence->Release();
        commandList->Release();
        queue->Release();
        allocator->Release();
        argumentBuffer->Release();
        signature->Release();
        return 41;
    }

    fence->Release();
    commandList->Release();
    queue->Release();
    allocator->Release();
    argumentBuffer->Release();
    signature->Release();
    return 0;
}

int main() {
    HMODULE proxy = GetModuleHandleW(L"d3d12.dll");
    if (!proxy || !GetProcAddress(proxy, MAKEINTRESOURCEA(101))
        || !GetProcAddress(proxy, MAKEINTRESOURCEA(102))
        || !GetProcAddress(proxy, MAKEINTRESOURCEA(116))
        || !GetProcAddress(proxy, MAKEINTRESOURCEA(117))
        || !GetProcAddress(proxy, "D3D12SerializeRootSignature")
        || !GetProcAddress(proxy, "D3D12SerializeVersionedRootSignature")) {
        std::fprintf(stderr, "D3D12 proxy is missing an application-required export\n");
        return 1;
    }

    ID3D12Device* device = nullptr;
    HRESULT result = D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_12_0,
        __uuidof(ID3D12Device),
        reinterpret_cast<void**>(&device));
    if (FAILED(result)) {
        std::fprintf(stderr, "D3D12CreateDevice failed: 0x%08lx\n", result);
        return 1;
    }

    ID3D12Device1* device1 = nullptr;
    result = device->QueryInterface(__uuidof(ID3D12Device1),
                                    reinterpret_cast<void**>(&device1));
    if (FAILED(result) || !device1) {
        std::fprintf(stderr, "ID3D12Device1 QueryInterface failed: 0x%08lx\n", result);
        device->Release();
        return 2;
    }
    device1->Release();

    ID3D12Device2* device2 = nullptr;
    result = device->QueryInterface(__uuidof(ID3D12Device2),
                                    reinterpret_cast<void**>(&device2));
    if (FAILED(result) || !device2) {
        std::fprintf(stderr, "ID3D12Device2 QueryInterface failed: 0x%08lx\n", result);
        device->Release();
        return 2;
    }
    device2->Release();

    const int placedResult = RunPlacedResourceSmoke(device);
    if (placedResult) {
        device->Release();
        return placedResult;
    }
    const int indirectResult = RunIndirectDispatchSmoke(device);
    if (indirectResult) {
        device->Release();
        return indirectResult;
    }

    D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{};
    formatSupport.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    result = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
        &formatSupport, sizeof(formatSupport));
    if (FAILED(result) ||
        !(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D)) {
        std::fprintf(stderr, "Format support query failed: 0x%08lx support1=0x%08x support2=0x%08x\n",
            result, static_cast<UINT>(formatSupport.Support1),
            static_cast<UINT>(formatSupport.Support2));
        device->Release();
        return 42;
    }
    D3D12_FEATURE_DATA_FORMAT_SUPPORT invalidFormatSupport{};
    invalidFormatSupport.Format = DXGI_FORMAT_UNKNOWN;
    result = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
        &invalidFormatSupport, sizeof(invalidFormatSupport));
    if (SUCCEEDED(result)) {
        std::fprintf(stderr, "Unknown format unexpectedly passed the format support query\n");
        device->Release();
        return 43;
    }

    const D3D_FEATURE_LEVEL requestedLevels[] = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{
        ARRAYSIZE(requestedLevels), requestedLevels, D3D_FEATURE_LEVEL_9_1
    };
    result = device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels));
    if (FAILED(result) || featureLevels.MaxSupportedFeatureLevel != D3D_FEATURE_LEVEL_12_0) {
        std::fprintf(stderr, "Unexpected feature level response: 0x%08lx / 0x%x\n",
                     result, featureLevels.MaxSupportedFeatureLevel);
        device->Release();
        return 2;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    result = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    if (FAILED(result) || options.ResourceBindingTier != D3D12_RESOURCE_BINDING_TIER_1
        || options.ResourceHeapTier != D3D12_RESOURCE_HEAP_TIER_1
        || options.TiledResourcesTier > D3D12_TILED_RESOURCES_TIER_1) {
        std::fprintf(stderr, "Unexpected D3D12 options response: 0x%08lx binding=%u heap=%u tiled=%u\n",
                     result, options.ResourceBindingTier, options.ResourceHeapTier,
                     options.TiledResourcesTier);
        device->Release();
        return 2;
    }
    if (options.TiledResourcesTier == D3D12_TILED_RESOURCES_TIER_1) {
        const int tiledResult = RunTiledResourceSmoke(device);
        if (tiledResult) {
            device->Release();
            return tiledResult;
        }
    }

    D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
    result = device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
    if (FAILED(result) || architecture.TileBasedRenderer || architecture.UMA || architecture.CacheCoherentUMA) {
        std::fprintf(stderr, "Unexpected architecture response: 0x%08lx\n", result);
        device->Release();
        return 2;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
    result = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));
    if (FAILED(result) || options1.WaveOps || options1.Int64ShaderOps) {
        std::fprintf(stderr, "Unexpected D3D12 options1 response: 0x%08lx\n", result);
        device->Release();
        return 2;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9{};
    result = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));
    if (FAILED(result) || options9.AtomicInt64OnTypedResourceSupported
        || options9.AtomicInt64OnGroupSharedSupported
        || options9.WaveMMATier != D3D12_WAVE_MMA_TIER_NOT_SUPPORTED) {
        std::fprintf(stderr, "Unexpected D3D12 options9 response: 0x%08lx\n", result);
        device->Release();
        return 2;
    }

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_7};
    result = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
    if (FAILED(result) || shaderModel.HighestShaderModel != D3D_SHADER_MODEL_6_7) {
        std::fprintf(stderr, "Unexpected shader model response: 0x%08lx / 0x%x\n",
                     result, shaderModel.HighestShaderModel);
        device->Release();
        return 3;
    }
    const int computeResult = RunComputeSmoke(device);
    if (computeResult) {
        device->Release();
        return computeResult;
    }
    int textureResult = RunTextureFootprintSmoke(device, DXGI_FORMAT_R32_UINT);
    if (!textureResult)
        textureResult = RunTextureFootprintSmoke(device, DXGI_FORMAT_R8G8B8A8_UINT);
    if (textureResult) {
        device->Release();
        return textureResult;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 256;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* resource = nullptr;
    result = device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&resource));
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateCommittedResource failed: 0x%08lx\n", result);
        device->Release();
        return 3;
    }

    void* mapped = nullptr;
    D3D12_RANGE noReads{0, 0};
    result = resource->Map(0, &noReads, &mapped);
    if (FAILED(result) || !mapped || !resource->GetGPUVirtualAddress()) {
        std::fprintf(stderr, "Resource Map/GPU virtual address failed: 0x%08lx\n", result);
        resource->Release();
        device->Release();
        return 4;
    }
    static_cast<unsigned char*>(mapped)[17] = 0x5a;
    D3D12_RANGE written{17, 18};
    resource->Unmap(0, &written);

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    readbackHeap.CreationNodeMask = readbackHeap.VisibleNodeMask = 1;
    ID3D12Resource* readback = nullptr;
    result = device->CreateCommittedResource(
        &readbackHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&readback));
    if (FAILED(result)) {
        std::fprintf(stderr, "Readback buffer creation failed: 0x%08lx\n", result);
        resource->Release();
        device->Release();
        return 5;
    }

    ID3D12CommandAllocator* allocator = nullptr;
    result = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_COPY, __uuidof(ID3D12CommandAllocator),
        reinterpret_cast<void**>(&allocator));
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    ID3D12CommandQueue* queue = nullptr;
    if (SUCCEEDED(result))
        result = device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                                            reinterpret_cast<void**>(&queue));
    ID3D12GraphicsCommandList* commandList = nullptr;
    if (SUCCEEDED(result))
        result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, allocator, nullptr,
                                           __uuidof(ID3D12GraphicsCommandList),
                                           reinterpret_cast<void**>(&commandList));
    if (FAILED(result)) {
        std::fprintf(stderr, "Command object creation failed: 0x%08lx\n", result);
        if (queue) queue->Release();
        if (allocator) allocator->Release();
        readback->Release();
        resource->Release();
        device->Release();
        return 6;
    }
    commandList->CopyBufferRegion(readback, 0, resource, 0, desc.Width);
    result = commandList->Close();
    ID3D12Fence* fence = nullptr;
    if (SUCCEEDED(result))
        result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                     reinterpret_cast<void**>(&fence));
    if (SUCCEEDED(result)) {
        ID3D12CommandList* lists[] = {commandList};
        queue->ExecuteCommandLists(1, lists);
        result = queue->Signal(fence, 1);
        if (SUCCEEDED(result)) result = queue->Wait(fence, 1);
        if (SUCCEEDED(result) && fence->GetCompletedValue() != 1) result = E_FAIL;
    }
    void* readbackData = nullptr;
    D3D12_RANGE readRange{17, 18};
    if (SUCCEEDED(result)) result = readback->Map(0, &readRange, &readbackData);
    if (FAILED(result) || !readbackData || static_cast<unsigned char*>(readbackData)[17] != 0x5a) {
        std::fprintf(stderr, "Queue copy/fence/readback failed: 0x%08lx\n", result);
        if (fence) fence->Release();
        commandList->Release();
        queue->Release();
        allocator->Release();
        readback->Release();
        resource->Release();
        device->Release();
        return 7;
    }
    readback->Unmap(0, &readRange);
    fence->Release();
    commandList->Release();
    queue->Release();
    allocator->Release();
    readback->Release();

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.NumDescriptors = 5;
    ID3D12DescriptorHeap* descriptorHeap = nullptr;
    result = device->CreateDescriptorHeap(
        &descriptorHeapDesc, __uuidof(ID3D12DescriptorHeap),
        reinterpret_cast<void**>(&descriptorHeap));
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateDescriptorHeap failed: 0x%08lx\n", result);
        resource->Release();
        device->Release();
        return 8;
    }

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
    cbv.BufferLocation = resource->GetGPUVirtualAddress();
    cbv.SizeInBytes = 256;
    const UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateConstantBufferView(&cbv, cpuHandle);

    D3D12_RESOURCE_DESC uavResourceDesc = desc;
    uavResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = defaultHeap.VisibleNodeMask = 1;
    ID3D12Resource* uavResource = nullptr;
    result = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE, &uavResourceDesc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&uavResource));
    if (FAILED(result)) {
        std::fprintf(stderr, "Default buffer creation failed: 0x%08lx\n", result);
        descriptorHeap->Release();
        resource->Release();
        device->Release();
        return 9;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_TYPELESS;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = 64;
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    auto srvHandle = cpuHandle;
    srvHandle.ptr += stride;
    device->CreateShaderResourceView(uavResource, &srv, srvHandle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 64;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    auto uavHandle = srvHandle;
    uavHandle.ptr += stride;
    device->CreateUnorderedAccessView(uavResource, nullptr, &uav, uavHandle);

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = 16;
    textureDesc.Height = 16;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource* texture = nullptr;
    result = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&texture));
    if (FAILED(result)) {
        std::fprintf(stderr, "Texture creation failed: 0x%08lx\n", result);
        uavResource->Release();
        descriptorHeap->Release();
        resource->Release();
        device->Release();
        return 10;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC textureSrv{};
    textureSrv.Format = textureDesc.Format;
    textureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    textureSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    textureSrv.Texture2D.MipLevels = 1;
    auto textureSrvHandle = uavHandle;
    textureSrvHandle.ptr += stride;
    device->CreateShaderResourceView(texture, &textureSrv, textureSrvHandle);

    D3D12_RENDER_TARGET_VIEW_DESC textureRtv{};
    textureRtv.Format = textureDesc.Format;
    textureRtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    auto textureRtvHandle = textureSrvHandle;
    textureRtvHandle.ptr += stride;
    device->CreateRenderTargetView(texture, &textureRtv, textureRtvHandle);

    texture->Release();
    uavResource->Release();
    descriptorHeap->Release();
    resource->Release();
    device->Release();
    return 0;
}
