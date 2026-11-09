#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdio>

int main() {
    IDXGIFactory7* factory = nullptr;
    HRESULT result = CreateDXGIFactory1(__uuidof(IDXGIFactory7),
        reinterpret_cast<void**>(&factory));
    if (FAILED(result) || !factory) {
        std::fprintf(stderr, "CreateDXGIFactory1 failed: 0x%08lx\n", result);
        return 1;
    }

    IDXGIFactory* baseFactory = nullptr;
    result = factory->QueryInterface(__uuidof(IDXGIFactory),
        reinterpret_cast<void**>(&baseFactory));
    if (FAILED(result) || !baseFactory) {
        std::fprintf(stderr, "Factory QueryInterface failed: 0x%08lx\n", result);
        factory->Release();
        return 2;
    }
    baseFactory->Release();

    IDXGIAdapter1* originalAdapter = nullptr;
    result = factory->EnumAdapters1(0, &originalAdapter);
    if (FAILED(result) || !originalAdapter) {
        std::fprintf(stderr, "EnumAdapters1 failed: 0x%08lx\n", result);
        factory->Release();
        return 3;
    }
    DXGI_ADAPTER_DESC1 originalDesc{};
    result = originalAdapter->GetDesc1(&originalDesc);
    originalAdapter->Release();
    if (FAILED(result)) {
        std::fprintf(stderr, "Original adapter GetDesc1 failed: 0x%08lx\n", result);
        factory->Release();
        return 4;
    }

    SetEnvironmentVariableW(L"D3D12ON11_DIAGNOSTIC_MASK_NVIDIA", L"1");
    IDXGIAdapter1* adapter = nullptr;
    result = factory->EnumAdapters1(0, &adapter);
    if (FAILED(result) || !adapter) {
        SetEnvironmentVariableW(L"D3D12ON11_DIAGNOSTIC_MASK_NVIDIA", nullptr);
        std::fprintf(stderr, "Diagnostic EnumAdapters1 failed: 0x%08lx\n", result);
        factory->Release();
        return 5;
    }
    DXGI_ADAPTER_DESC1 diagnosticDesc{};
    result = adapter->GetDesc1(&diagnosticDesc);
    const UINT expectedVendor = originalDesc.VendorId == 0x10de
        ? 0x1414 : originalDesc.VendorId;
    if (FAILED(result) || diagnosticDesc.VendorId != expectedVendor) {
        SetEnvironmentVariableW(L"D3D12ON11_DIAGNOSTIC_MASK_NVIDIA", nullptr);
        std::fprintf(stderr, "Diagnostic adapter vendor mismatch: 0x%08lx vendor=0x%x expected=0x%x\n",
            result, diagnosticDesc.VendorId, expectedVendor);
        adapter->Release();
        factory->Release();
        return 6;
    }

    ID3D12Device* device = nullptr;
    result = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
        __uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    SetEnvironmentVariableW(L"D3D12ON11_DIAGNOSTIC_MASK_NVIDIA", nullptr);
    if (FAILED(result) || !device) {
        std::fprintf(stderr, "D3D12CreateDevice with enumerated adapter failed: 0x%08lx\n", result);
        if (device) device->Release();
        adapter->Release();
        factory->Release();
        return 7;
    }
    device->Release();
    adapter->Release();
    factory->Release();
    return 0;
}
