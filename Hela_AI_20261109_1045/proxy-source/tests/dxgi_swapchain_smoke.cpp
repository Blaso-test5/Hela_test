#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

static HRESULT CreateEmptyRootSignature(ID3D12Device* device, ID3D12RootSignature** out) {
    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
    if (!length || length >= ARRAYSIZE(systemDir)) return HRESULT_FROM_WIN32(GetLastError());
    std::wstring path(systemDir, length);
    path += L"\\d3d12.dll";
    HMODULE systemD3d12 = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!systemD3d12) return HRESULT_FROM_WIN32(GetLastError());
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
        ID3DBlob**, ID3DBlob**);
    auto serialize = reinterpret_cast<SerializeFn>(
        GetProcAddress(systemD3d12, "D3D12SerializeRootSignature"));
    if (!serialize) {
        FreeLibrary(systemD3d12);
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    D3D12_ROOT_SIGNATURE_DESC desc{};
    ID3DBlob* blob = nullptr;
    ID3DBlob* diagnostics = nullptr;
    HRESULT result = serialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &diagnostics);
    if (diagnostics) diagnostics->Release();
    if (SUCCEEDED(result))
        result = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
            __uuidof(ID3D12RootSignature), reinterpret_cast<void**>(out));
    if (blob) blob->Release();
    FreeLibrary(systemD3d12);
    return result;
}

static HRESULT CompileShader(const char* source, const char* target, ID3DBlob** out) {
    ID3DBlob* diagnostics = nullptr;
    HRESULT result = D3DCompile(source, strlen(source), "swapchain-smoke.hlsl", nullptr, nullptr,
        "main", target, 0, 0, out, &diagnostics);
    if (FAILED(result) && diagnostics)
        std::fprintf(stderr, "%s\n", static_cast<const char*>(diagnostics->GetBufferPointer()));
    if (diagnostics) diagnostics->Release();
    return result;
}

static HRESULT DrawSmoke(ID3D12Device* device, ID3D12CommandQueue* queue,
    IDXGISwapChain1* swapChain, ID3D12Resource* backbuffer) {
    ID3D12RootSignature* root = nullptr;
    HRESULT result = CreateEmptyRootSignature(device, &root);
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateEmptyRootSignature failed: 0x%08lx\n", result);
        return result;
    }

    const char vertexSource[] =
        "float4 main(uint id : SV_VertexID) : SV_Position {"
        "float2 p[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };"
        "return float4(p[id],0,1);}";
    const char pixelSource[] =
        "float4 main() : SV_Target { return float4(0.2,0.4,0.8,1);}";
    ID3DBlob* vertex = nullptr;
    ID3DBlob* pixel = nullptr;
    result = CompileShader(vertexSource, "vs_5_0", &vertex);
    if (SUCCEEDED(result)) result = CompileShader(pixelSource, "ps_5_0", &pixel);
    if (FAILED(result)) {
        std::fprintf(stderr, "CompileShader failed: 0x%08lx\n", result);
        if (vertex) vertex->Release();
        root->Release();
        return result;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = root;
    pipelineDesc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    pipelineDesc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    pipelineDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipelineDesc.SampleMask = UINT_MAX;
    pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipelineDesc.RasterizerState.DepthClipEnable = TRUE;
    pipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.NumRenderTargets = 1;
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipelineDesc.SampleDesc.Count = 1;
    ID3D12PipelineState* pipeline = nullptr;
    result = device->CreateGraphicsPipelineState(&pipelineDesc, __uuidof(ID3D12PipelineState),
        reinterpret_cast<void**>(&pipeline));
    vertex->Release();
    pixel->Release();
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateGraphicsPipelineState failed: 0x%08lx\n", result);
        root->Release();
        return result;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = 1;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    result = device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
        reinterpret_cast<void**>(&rtvHeap));
    if (SUCCEEDED(result)) {
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(backbuffer, nullptr, rtv);
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* list = nullptr;
        result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&allocator));
        if (SUCCEEDED(result))
            result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator, pipeline, __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void**>(&list));
        if (SUCCEEDED(result)) {
            list->SetGraphicsRootSignature(root);
            list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            const FLOAT clearColor[] = {0, 0, 0, 1};
            list->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->DrawInstanced(3, 1, 0, 0);
            result = list->Close();
        }
        if (SUCCEEDED(result)) {
            ID3D12CommandList* lists[] = {list};
            queue->ExecuteCommandLists(1, lists);
            result = swapChain->Present(0, 0);
        }
        if (FAILED(result)) std::fprintf(stderr, "DrawSmoke setup/execute failed: 0x%08lx\n", result);
        if (list) list->Release();
        if (allocator) allocator->Release();
        rtvHeap->Release();
    }
    pipeline->Release();
    root->Release();
    return result;
}

int main() {
    const wchar_t className[] = L"D3D12On11SwapchainSmoke";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
    HWND window = CreateWindowExW(0, className, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window) return 2;

    ID3D12Device* device = nullptr;
    HRESULT result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
        __uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    if (FAILED(result)) {
        std::fprintf(stderr, "D3D12CreateDevice failed: 0x%08lx\n", result);
        DestroyWindow(window);
        return 3;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    result = device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
        reinterpret_cast<void**>(&queue));
    IDXGIFactory7* factory = nullptr;
    if (SUCCEEDED(result))
        result = CreateDXGIFactory1(__uuidof(IDXGIFactory7),
            reinterpret_cast<void**>(&factory));

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 64;
    desc.Height = 64;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    IDXGISwapChain1* swapChain = nullptr;
    if (SUCCEEDED(result))
        result = factory->CreateSwapChainForHwnd(queue, window, &desc, nullptr,
            nullptr, &swapChain);
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateSwapChainForHwnd failed: 0x%08lx\n", result);
        if (factory) factory->Release();
        if (queue) queue->Release();
        device->Release();
        DestroyWindow(window);
        return 4;
    }

    ID3D12Resource* backbuffer = nullptr;
    result = swapChain->GetBuffer(0, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&backbuffer));
    if (SUCCEEDED(result)) {
        const D3D12_RESOURCE_DESC bufferDesc = backbuffer->GetDesc();
        if (bufferDesc.Width != desc.Width || bufferDesc.Height != desc.Height)
            result = E_FAIL;
    }
    if (SUCCEEDED(result)) result = DrawSmoke(device, queue, swapChain, backbuffer);
    if (backbuffer) {
        backbuffer->Release();
        backbuffer = nullptr;
    }
    if (SUCCEEDED(result)) result = swapChain->ResizeBuffers(0, 80, 80,
        DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(result))
        result = swapChain->GetBuffer(0, __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(&backbuffer));
    if (backbuffer) backbuffer->Release();

    if (FAILED(result)) std::fprintf(stderr, "Swapchain operation failed: 0x%08lx\n", result);
    swapChain->Release();
    factory->Release();
    queue->Release();
    device->Release();
    DestroyWindow(window);
    return FAILED(result) ? 5 : 0;
}
