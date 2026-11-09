#include <windows.h>
#include <d3d12.h>
#include <cstdio>

int main() {
    ID3D12Device* device = nullptr;
    HRESULT result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
        __uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    if (FAILED(result)) {
        std::fprintf(stderr, "D3D12CreateDevice failed: 0x%08lx\n", result);
        return 1;
    }

    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    result = device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue),
        reinterpret_cast<void**>(&queue));
    if (FAILED(result)) {
        std::fprintf(stderr, "CreateCommandQueue failed: 0x%08lx\n", result);
        device->Release();
        return 2;
    }

    UINT64 frequency = 0;
    result = queue->GetTimestampFrequency(&frequency);
    if (FAILED(result) || !frequency) {
        std::fprintf(stderr, "GetTimestampFrequency failed: 0x%08lx frequency=%llu\n",
            result, static_cast<unsigned long long>(frequency));
        queue->Release();
        device->Release();
        return 3;
    }
    if (queue->GetTimestampFrequency(nullptr) != E_INVALIDARG) {
        std::fprintf(stderr, "GetTimestampFrequency accepted a null output pointer\n");
        queue->Release();
        device->Release();
        return 4;
    }

    UINT64 firstGpu = 0;
    UINT64 firstCpu = 0;
    result = queue->GetClockCalibration(&firstGpu, &firstCpu);
    if (FAILED(result) || !firstGpu || !firstCpu) {
        std::fprintf(stderr, "First GetClockCalibration failed: 0x%08lx gpu=%llu cpu=%llu\n",
            result, static_cast<unsigned long long>(firstGpu),
            static_cast<unsigned long long>(firstCpu));
        queue->Release();
        device->Release();
        return 5;
    }
    if (queue->GetClockCalibration(nullptr, &firstCpu) != E_INVALIDARG ||
        queue->GetClockCalibration(&firstGpu, nullptr) != E_INVALIDARG) {
        std::fprintf(stderr, "GetClockCalibration accepted a null output pointer\n");
        queue->Release();
        device->Release();
        return 6;
    }

    UINT64 secondGpu = 0;
    UINT64 secondCpu = 0;
    result = queue->GetClockCalibration(&secondGpu, &secondCpu);
    if (FAILED(result) || !secondGpu || secondCpu <= firstCpu ||
        secondGpu < firstGpu) {
        std::fprintf(stderr,
            "Second GetClockCalibration failed or returned non-monotonic samples: "
            "hr=0x%08lx gpu=%llu->%llu cpu=%llu->%llu\n",
            result, static_cast<unsigned long long>(firstGpu),
            static_cast<unsigned long long>(secondGpu),
            static_cast<unsigned long long>(firstCpu),
            static_cast<unsigned long long>(secondCpu));
        queue->Release();
        device->Release();
        return 7;
    }

    std::printf("Queue clock: frequency=%llu gpu=%llu->%llu cpu=%llu->%llu\n",
        static_cast<unsigned long long>(frequency),
        static_cast<unsigned long long>(firstGpu),
        static_cast<unsigned long long>(secondGpu),
        static_cast<unsigned long long>(firstCpu),
        static_cast<unsigned long long>(secondCpu));
    queue->Release();
    device->Release();
    return 0;
}
