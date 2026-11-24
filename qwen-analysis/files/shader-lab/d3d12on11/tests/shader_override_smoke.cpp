#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3 || (std::strcmp(argv[1], "cs") != 0 && std::strcmp(argv[1], "vs") != 0)) {
        std::fprintf(stderr, "Usage: d3d12on11_shader_override_smoke <cs|vs> <shader.dxil>\n");
        return 2;
    }

    std::ifstream input(argv[2], std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "Could not open DXIL shader: %s\n", argv[2]);
        return 3;
    }
    const std::vector<char> shader{std::istreambuf_iterator<char>(input), {}};
    if (shader.empty()) {
        std::fprintf(stderr, "DXIL shader is empty: %s\n", argv[2]);
        return 3;
    }

    ID3D12Device* device = nullptr;
    HRESULT result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
        __uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    if (FAILED(result)) {
        std::fprintf(stderr, "D3D12CreateDevice failed: 0x%08lx\n", result);
        return 4;
    }

    ID3D12PipelineState* pipeline = nullptr;
    if (std::strcmp(argv[1], "cs") == 0) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.CS.pShaderBytecode = shader.data();
        desc.CS.BytecodeLength = shader.size();
        result = device->CreateComputePipelineState(&desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void**>(&pipeline));
    } else {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.VS.pShaderBytecode = shader.data();
        desc.VS.BytecodeLength = shader.size();
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        desc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;
        result = device->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void**>(&pipeline));
    }
    if (FAILED(result))
        std::fprintf(stderr, "%s PSO creation/SM5 override lookup failed: 0x%08lx\n", argv[1], result);
    else {
        std::printf("%s PSO created with the configured SM5 override.\n", argv[1]);
        pipeline->Release();
    }
    device->Release();
    return FAILED(result) ? 5 : 0;
}
