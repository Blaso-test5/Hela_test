#include "types.h"

namespace {

using CreateVersionedDeserializer = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);
using CreateDeserializer = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);

struct SystemD3D12DeserializerApi {
    HMODULE module = nullptr;
    CreateVersionedDeserializer createVersioned = nullptr;
    CreateDeserializer createLegacy = nullptr;
};

const SystemD3D12DeserializerApi& GetDeserializerApi() {
    static const SystemD3D12DeserializerApi api = []() {
        SystemD3D12DeserializerApi loaded{};
        wchar_t systemDir[MAX_PATH]{};
        const UINT chars = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
        if (!chars || chars >= ARRAYSIZE(systemDir)) {
            Log("GetSystemDirectoryW failed while loading system root-signature deserializer: %lu",
                GetLastError());
            return loaded;
        }
        std::wstring path(systemDir, chars);
        path += L"\\d3d12.dll";
        loaded.module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!loaded.module) {
            Log("Could not load system d3d12.dll for root-signature deserialization: %lu",
                GetLastError());
            return loaded;
        }
        loaded.createVersioned = reinterpret_cast<CreateVersionedDeserializer>(
            GetProcAddress(loaded.module, "D3D12CreateVersionedRootSignatureDeserializer"));
        loaded.createLegacy = reinterpret_cast<CreateDeserializer>(
            GetProcAddress(loaded.module, "D3D12CreateRootSignatureDeserializer"));
        return loaded;
    }();
    return api;
}

struct NativeDeserializer {
    Ptr<ID3D12VersionedRootSignatureDeserializer> versioned;
    Ptr<ID3D12RootSignatureDeserializer> legacy;

    HRESULT Open(const void* data, SIZE_T size) {
        const auto& api = GetDeserializerApi();
        if (!api.module) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);

        if (api.createVersioned) {
            const HRESULT result = api.createVersioned(data, size,
                __uuidof(ID3D12VersionedRootSignatureDeserializer),
                reinterpret_cast<void**>(versioned.Put()));
            if (SUCCEEDED(result)) return S_OK;
            Log("Versioned root-signature deserializer rejected blob: 0x%08lx", result);
        }

        if (!api.createLegacy) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        return api.createLegacy(data, size, __uuidof(ID3D12RootSignatureDeserializer),
                            reinterpret_cast<void**>(legacy.Put()));
    }

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* GetVersioned() {
        if (!versioned) return nullptr;
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc = nullptr;
        if (FAILED(versioned->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &desc)))
            return nullptr;
        return desc;
    }
};

char RegisterClass(D3D12_DESCRIPTOR_RANGE_TYPE type) {
    switch (type) {
    case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return 'b';
    case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: return 't';
    case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: return 'u';
    case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: return 's';
    default: return 0;
    }
}

char RegisterClass(D3D12_ROOT_PARAMETER_TYPE type) {
    switch (type) {
    case D3D12_ROOT_PARAMETER_TYPE_CBV: return 'b';
    case D3D12_ROOT_PARAMETER_TYPE_SRV: return 't';
    case D3D12_ROOT_PARAMETER_TYPE_UAV: return 'u';
    default: return 0;
    }
}

HRESULT CreateStaticSampler(Device* device, const D3D12_STATIC_SAMPLER_DESC& source,
                            StaticSamplerInfo& result) {
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = static_cast<D3D11_FILTER>(source.Filter);
    desc.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(source.AddressU);
    desc.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(source.AddressV);
    desc.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(source.AddressW);
    desc.MipLODBias = source.MipLODBias;
    desc.MaxAnisotropy = std::min<UINT>(std::max<UINT>(source.MaxAnisotropy, 1), D3D11_MAX_MAXANISOTROPY);
    desc.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(source.ComparisonFunc);
    switch (source.BorderColor) {
    case D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK:
        desc.BorderColor[0] = desc.BorderColor[1] = desc.BorderColor[2] = desc.BorderColor[3] = 0.0f;
        break;
    case D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK:
    case static_cast<D3D12_STATIC_BORDER_COLOR>(3):
        desc.BorderColor[0] = desc.BorderColor[1] = desc.BorderColor[2] = 0.0f;
        desc.BorderColor[3] = 1.0f;
        break;
    case D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE:
    case static_cast<D3D12_STATIC_BORDER_COLOR>(4):
        desc.BorderColor[0] = desc.BorderColor[1] = desc.BorderColor[2] = desc.BorderColor[3] = 1.0f;
        break;
    default:
        desc.BorderColor[0] = desc.BorderColor[1] = desc.BorderColor[2] = desc.BorderColor[3] = 0.0f;
        break;
    }
    desc.MinLOD = source.MinLOD;
    desc.MaxLOD = source.MaxLOD;
    HRESULT hr = device->d11->CreateSamplerState(&desc, result.st.Put());
    if (FAILED(hr)) {
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        hr = device->d11->CreateSamplerState(&desc, result.st.Put());
    }
    return hr;
}

HRESULT ConvertRange(const D3D12_DESCRIPTOR_RANGE1& source, RangeInfo& result, UINT& appendOffset) {
    const char registerClass = RegisterClass(source.RangeType);
    if (!registerClass || source.NumDescriptors == 0 || source.NumDescriptors == UINT_MAX)
        return E_INVALIDARG;
    if (source.BaseShaderRegister > D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT ||
        source.NumDescriptors > D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
        return E_INVALIDARG;

    result.type = source.RangeType;
    result.num = source.NumDescriptors;
    result.baseReg = source.BaseShaderRegister;
    result.space = source.RegisterSpace;
    result.offset = source.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
        ? appendOffset : source.OffsetInDescriptorsFromTableStart;
    result.slots.reserve(source.NumDescriptors);

    std::set<UINT> used;
    for (UINT i = 0; i < source.NumDescriptors; ++i) {
        UINT slot = MapRegister(registerClass, source.RegisterSpace, source.BaseShaderRegister + i);
        if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT) return E_INVALIDARG;
        if (!used.insert(slot).second) {
            Log("Root signature maps multiple %c registers onto slot %u", registerClass, slot);
            return E_INVALIDARG;
        }
        result.slots.push_back(slot);
    }
    if (result.offset > UINT_MAX - source.NumDescriptors) return E_INVALIDARG;
    appendOffset = result.offset + source.NumDescriptors;
    return S_OK;
}

HRESULT ConvertParam(Device* device, const D3D12_ROOT_PARAMETER1& source, RootParam& result) {
    result.type = source.ParameterType;
    result.vis = source.ShaderVisibility;
    switch (source.ParameterType) {
    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
        const auto& table = source.DescriptorTable;
        if (table.NumDescriptorRanges && !table.pDescriptorRanges) return E_INVALIDARG;
        UINT appendOffset = 0;
        result.ranges.reserve(table.NumDescriptorRanges);
        for (UINT i = 0; i < table.NumDescriptorRanges; ++i) {
            RangeInfo range{};
            const HRESULT hr = ConvertRange(table.pDescriptorRanges[i], range, appendOffset);
            if (FAILED(hr)) return hr;
            result.ranges.push_back(std::move(range));
        }
        return S_OK;
    }
    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: {
        const auto& constants = source.Constants;
        if (!constants.Num32BitValues || constants.Num32BitValues > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT)
            return E_INVALIDARG;
        result.num32 = constants.Num32BitValues;
        result.reg = constants.ShaderRegister;
        result.space = constants.RegisterSpace;
        result.slot = MapRegister('b', result.space, result.reg);
        if (result.slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) return E_INVALIDARG;

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(AlignUp(static_cast<UINT64>(result.num32) * sizeof(UINT), 16));
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return device->d11->CreateBuffer(&desc, nullptr, result.cb.Put());
    }
    case D3D12_ROOT_PARAMETER_TYPE_CBV:
    case D3D12_ROOT_PARAMETER_TYPE_SRV:
    case D3D12_ROOT_PARAMETER_TYPE_UAV: {
        const auto& descriptor = source.Descriptor;
        const char registerClass = RegisterClass(source.ParameterType);
        result.reg = descriptor.ShaderRegister;
        result.space = descriptor.RegisterSpace;
        result.slot = MapRegister(registerClass, result.space, result.reg);
        const UINT limit = registerClass == 'b' ? D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
            : registerClass == 's' ? D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT
            : D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        return result.slot < limit ? S_OK : E_INVALIDARG;
    }
    default:
        return E_INVALIDARG;
    }
}

HRESULT BuildRoot(Device* device, const D3D12_ROOT_PARAMETER1* params, UINT paramCount,
                  const D3D12_STATIC_SAMPLER_DESC* samplers, UINT samplerCount,
                  D3D12_ROOT_SIGNATURE_FLAGS flags, RootSignature* root) {
    if (paramCount > 64 || samplerCount > 2048 || (paramCount && !params) || (samplerCount && !samplers))
        return E_INVALIDARG;
    root->flags = flags;
    root->params.reserve(paramCount);
    for (UINT i = 0; i < paramCount; ++i) {
        RootParam param{};
        const HRESULT result = ConvertParam(device, params[i], param);
        if (FAILED(result)) {
            Log("Unsupported root parameter %u (type %u): 0x%08lx", i, params[i].ParameterType, result);
            return result;
        }
        root->params.push_back(std::move(param));
    }
    root->statics.reserve(samplerCount);
    for (UINT i = 0; i < samplerCount; ++i) {
        StaticSamplerInfo sampler{};
        sampler.slot = MapRegister('s', samplers[i].RegisterSpace, samplers[i].ShaderRegister);
        sampler.space = samplers[i].RegisterSpace;
        sampler.vis = samplers[i].ShaderVisibility;
        if (sampler.slot >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) return E_INVALIDARG;
        const HRESULT result = CreateStaticSampler(device, samplers[i], sampler);
        if (FAILED(result)) {
            Log("Static sampler %u could not be created: 0x%08lx", i, result);
            return result;
        }
        root->statics.push_back(std::move(sampler));
    }
    root->id = ++device->rootSigCounter;
    return S_OK;
}

HRESULT BuildRoot(Device* device, const D3D12_ROOT_SIGNATURE_DESC& source, RootSignature* root) {
    if (source.NumParameters > 64 || source.NumStaticSamplers > 2048 ||
        (source.NumParameters && !source.pParameters) ||
        (source.NumStaticSamplers && !source.pStaticSamplers))
        return E_INVALIDARG;
    std::vector<D3D12_ROOT_PARAMETER1> params;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE1>> ranges(source.NumParameters);
    params.resize(source.NumParameters);
    for (UINT i = 0; i < source.NumParameters; ++i) {
        const auto& old = source.pParameters[i];
        auto& current = params[i];
        current.ParameterType = old.ParameterType;
        current.ShaderVisibility = old.ShaderVisibility;
        if (old.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            const auto& table = old.DescriptorTable;
            if (table.NumDescriptorRanges && !table.pDescriptorRanges) return E_INVALIDARG;
            ranges[i].reserve(table.NumDescriptorRanges);
            for (UINT r = 0; r < table.NumDescriptorRanges; ++r) {
                const auto& oldRange = table.pDescriptorRanges[r];
                D3D12_DESCRIPTOR_RANGE1 range{};
                range.RangeType = oldRange.RangeType;
                range.NumDescriptors = oldRange.NumDescriptors;
                range.BaseShaderRegister = oldRange.BaseShaderRegister;
                range.RegisterSpace = oldRange.RegisterSpace;
                range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
                range.OffsetInDescriptorsFromTableStart = oldRange.OffsetInDescriptorsFromTableStart;
                ranges[i].push_back(range);
            }
            current.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges[i].size());
            current.DescriptorTable.pDescriptorRanges = ranges[i].data();
        } else if (old.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
            current.Constants = old.Constants;
        } else {
            current.Descriptor.ShaderRegister = old.Descriptor.ShaderRegister;
            current.Descriptor.RegisterSpace = old.Descriptor.RegisterSpace;
            current.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
        }
    }
    return BuildRoot(device, params.data(), static_cast<UINT>(params.size()), source.pStaticSamplers,
                     source.NumStaticSamplers, source.Flags, root);
}

} // namespace

HRESULT RootSignature::Create(Device* device, const void* blob, SIZE_T length, RootSignature** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!device || !blob || length < 32 || length > 16 * 1024 * 1024) return E_INVALIDARG;

    auto* root = new (std::nothrow) RootSignature(device);
    if (!root) return E_OUTOFMEMORY;
    NativeDeserializer deserializer;
    Log("Opening root signature blob (%zu bytes)", static_cast<size_t>(length));
    HRESULT result = deserializer.Open(blob, length);
    Log("Opened root signature deserializer: 0x%08lx", result);
    if (FAILED(result)) {
        Log("Root signature deserializer unavailable or rejected blob: 0x%08lx", result);
        root->Release();
        return result;
    }

    if (const auto* desc = deserializer.GetVersioned()) {
        Log("Root signature desc version %u", desc->Version);
        switch (desc->Version) {
        case D3D_ROOT_SIGNATURE_VERSION_1_1:
            result = BuildRoot(device, desc->Desc_1_1.pParameters, desc->Desc_1_1.NumParameters,
                               desc->Desc_1_1.pStaticSamplers, desc->Desc_1_1.NumStaticSamplers,
                               desc->Desc_1_1.Flags, root);
            break;
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
            result = BuildRoot(device, desc->Desc_1_0, root);
            break;
        default:
            Unimpl("D3D12 root signature version");
            result = E_NOTIMPL;
            break;
        }
    } else if (deserializer.legacy) {
        const auto* desc = deserializer.legacy->GetRootSignatureDesc();
        result = desc ? BuildRoot(device, *desc, root) : E_INVALIDARG;
    } else {
        result = E_INVALIDARG;
    }

    if (FAILED(result)) {
        root->Release();
        return result;
    }
    *out = root;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateRootSignature(UINT nodeMask, const void* blob, SIZE_T length,
    REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (nodeMask != 0 && nodeMask != 1) return E_INVALIDARG;
    RootSignature* root = nullptr;
    const HRESULT createResult = RootSignature::Create(this, blob, length, &root);
    if (FAILED(createResult)) return createResult;
    const HRESULT queryResult = root->QueryInterface(riid, out);
    root->Release();
    return queryResult;
}
