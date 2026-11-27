#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {

bool TestFormat(ID3D11Device* device, ID3D11DeviceContext* context,
                DXGI_FORMAT format, UINT blockBytes, const char* name) {
    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width = 4;
    stagingDesc.Height = 4;
    stagingDesc.MipLevels = 3;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = format;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ComPtr<ID3D11Texture2D> upload;
    HRESULT result = device->CreateTexture2D(&stagingDesc, nullptr, &upload);
    if (FAILED(result)) {
        std::fprintf(stderr, "%s aligned staging mip-chain creation failed: 0x%08lx\n", name, result);
        return false;
    }

    D3D11_TEXTURE2D_DESC targetDesc{};
    targetDesc.Width = 4;
    targetDesc.Height = 4;
    targetDesc.MipLevels = 3;
    targetDesc.ArraySize = 1;
    targetDesc.Format = format;
    targetDesc.SampleDesc.Count = 1;
    targetDesc.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture2D> target;
    result = device->CreateTexture2D(&targetDesc, nullptr, &target);
    if (FAILED(result)) {
        std::fprintf(stderr, "%s default mip-chain creation failed: 0x%08lx\n", name, result);
        return false;
    }
    D3D11_TEXTURE2D_DESC readbackDesc = stagingDesc;
    readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    result = device->CreateTexture2D(&readbackDesc, nullptr, &readback);
    if (FAILED(result)) {
        std::fprintf(stderr, "%s readback texture creation failed: 0x%08lx\n", name, result);
        return false;
    }
    for (UINT mip = 1; mip <= 2; ++mip) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        result = context->Map(upload.Get(), mip, D3D11_MAP_WRITE, 0, &mapped);
        if (FAILED(result) || !mapped.pData || mapped.RowPitch < blockBytes) {
            std::fprintf(stderr, "%s mip %u map/write failed: 0x%08lx rowPitch=%u\n",
                name, mip, result, mapped.RowPitch);
            return false;
        }
        std::memset(mapped.pData, static_cast<int>(0x5a + mip), blockBytes);
        context->Unmap(upload.Get(), mip);
        context->CopySubresourceRegion(target.Get(), mip, 0, 0, 0, upload.Get(), mip, nullptr);
        context->CopySubresourceRegion(readback.Get(), mip, 0, 0, 0, target.Get(), mip, nullptr);

        result = context->Map(readback.Get(), mip, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(result) || !mapped.pData || mapped.RowPitch < blockBytes) {
            std::fprintf(stderr, "%s mip %u map/read failed: 0x%08lx rowPitch=%u\n",
                name, mip, result, mapped.RowPitch);
            return false;
        }
        const auto* bytes = static_cast<const unsigned char*>(mapped.pData);
        bool matches = true;
        for (UINT i = 0; i < blockBytes; ++i)
            matches = matches && bytes[i] == static_cast<unsigned char>(0x5a + mip);
        context->Unmap(readback.Get(), mip);
        if (!matches) {
            std::fprintf(stderr, "%s mip %u block data mismatch\n", name, mip);
            return false;
        }
    }

    D3D11_TEXTURE2D_DESC smallDesc = stagingDesc;
    smallDesc.Width = 2;
    smallDesc.Height = 2;
    smallDesc.MipLevels = 1;
    ComPtr<ID3D11Texture2D> smallTexture;
    result = device->CreateTexture2D(&smallDesc, nullptr, &smallTexture);
    std::printf("%s: 4x4 staging mips 1 (2x2) and 2 (1x1) upload/readback passed; standalone 2x2 staging=%s (0x%08lx)\n",
        name, SUCCEEDED(result) ? "accepted" : "rejected", result);
    return true;
}

} // namespace

int main() {
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

    std::printf("D3D11 feature level: 0x%04x\n", static_cast<unsigned>(featureLevel));
    if (!TestFormat(device.Get(), context.Get(), DXGI_FORMAT_BC1_UNORM, 8, "BC1") ||
        !TestFormat(device.Get(), context.Get(), DXGI_FORMAT_BC7_UNORM, 16, "BC7"))
        return 2;
    return 0;
}
