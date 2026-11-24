#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

int main() {
    constexpr UINT sourceBytes = 4112;
    constexpr UINT stride = 20;
    constexpr UINT structuredBytes = (sourceBytes / stride) * stride;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, &featureLevel, &context);
    if (FAILED(result)) {
        std::fprintf(stderr, "Hardware D3D11CreateDevice failed: 0x%08lx; trying WARP\n", result);
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, &featureLevel, &context);
    }
    if (FAILED(result)) {
        std::fprintf(stderr, "D3D11CreateDevice failed: 0x%08lx\n", result);
        return 1;
    }

    std::vector<unsigned char> sourceData(sourceBytes);
    for (UINT i = 0; i < sourceBytes; ++i)
        sourceData[i] = static_cast<unsigned char>(i % 251);

    D3D11_BUFFER_DESC sourceDesc{};
    sourceDesc.ByteWidth = sourceBytes;
    sourceDesc.Usage = D3D11_USAGE_DEFAULT;
    sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = sourceData.data();
    ComPtr<ID3D11Buffer> source;
    result = device->CreateBuffer(&sourceDesc, &initialData, &source);
    if (FAILED(result)) {
        std::fprintf(stderr, "Source buffer creation failed: 0x%08lx\n", result);
        return 2;
    }

    D3D11_BUFFER_DESC structuredDesc{};
    structuredDesc.ByteWidth = structuredBytes;
    structuredDesc.Usage = D3D11_USAGE_DEFAULT;
    structuredDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    structuredDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    structuredDesc.StructureByteStride = stride;
    ComPtr<ID3D11Buffer> structured;
    result = device->CreateBuffer(&structuredDesc, nullptr, &structured);
    if (FAILED(result)) {
        std::fprintf(stderr, "Truncated structured buffer creation failed: 0x%08lx\n", result);
        return 3;
    }

    D3D11_BOX sourceBox{};
    sourceBox.right = structuredBytes;
    sourceBox.bottom = 1;
    sourceBox.back = 1;
    context->CopySubresourceRegion(structured.Get(), 0, 0, 0, 0,
        source.Get(), 0, &sourceBox);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = structuredBytes / stride - 1;
    srvDesc.Buffer.NumElements = 1;
    ComPtr<ID3D11ShaderResourceView> srv;
    result = device->CreateShaderResourceView(structured.Get(), &srvDesc, &srv);
    if (FAILED(result)) {
        std::fprintf(stderr, "Structured SRV creation failed: 0x%08lx\n", result);
        return 4;
    }

    D3D11_BUFFER_DESC readbackDesc{};
    readbackDesc.ByteWidth = structuredBytes;
    readbackDesc.Usage = D3D11_USAGE_STAGING;
    readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> readback;
    result = device->CreateBuffer(&readbackDesc, nullptr, &readback);
    if (FAILED(result)) {
        std::fprintf(stderr, "Readback buffer creation failed: 0x%08lx\n", result);
        return 5;
    }
    context->CopyResource(readback.Get(), structured.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result) || !mapped.pData) {
        std::fprintf(stderr, "Readback map failed: 0x%08lx\n", result);
        return 6;
    }
    const bool matches = std::memcmp(mapped.pData, sourceData.data(), structuredBytes) == 0;
    context->Unmap(readback.Get(), 0);
    if (!matches) {
        std::fprintf(stderr, "Structured prefix copy data mismatch\n");
        return 7;
    }

    std::printf("Structured prefix copy passed: source=%u stride=%u copied=%u trailing=%u\n",
        sourceBytes, stride, structuredBytes, sourceBytes - structuredBytes);
    return 0;
}
