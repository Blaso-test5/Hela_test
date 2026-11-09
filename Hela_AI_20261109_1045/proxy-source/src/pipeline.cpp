#include "types.h"

namespace {

std::mutex g_shaderCacheMutex;
std::set<uint64_t> g_loggedShaders;
std::atomic<uint32_t> g_graphicsPsoCount{0};
std::atomic<uint32_t> g_computePsoCount{0};

bool HasBytecode(const D3D12_SHADER_BYTECODE& bytecode) {
    return bytecode.pShaderBytecode && bytecode.BytecodeLength;
}

bool ShouldLogShaderHash(uint64_t hash) {
    std::lock_guard<std::mutex> lock(g_shaderCacheMutex);
    return g_loggedShaders.insert(hash).second;
}

bool ShouldLogPsoMilestone(uint32_t index) {
    return index <= 64 || (index % 100 == 0);
}

HRESULT LoadShader(Device* device, const char* stage, const D3D12_SHADER_BYTECODE& bytecode,
                   std::vector<uint8_t>& resolved) {
    if (!HasBytecode(bytecode)) {
        Log("SHADER BLOCKED stage=%s reason=empty-bytecode", stage);
        return E_INVALIDARG;
    }
    const void* data = bytecode.pShaderBytecode;
    const size_t size = bytecode.BytecodeLength;
    const uint64_t hash = Fnv1a64(data, size);
    const std::string hashText = Hex64(hash);
    const DxbcInfo original = InspectContainer(data, size);
    const bool firstSeen = ShouldLogShaderHash(hash);
    if (firstSeen) {
        Log("SHADER BEGIN stage=%s hash=%s bytes=%zu DXBC=%u DXIL=%u legacy=%u model=%u_%u RTS0=%u",
            stage, hashText.c_str(), size, original.isDxbc, original.hasDxil, original.hasLegacy,
            original.versionMajor, original.versionMinor, original.hasRts0);
    }

    if (original.isDxbc && original.hasLegacy && original.versionMajor <= 5) {
        resolved.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        if (firstSeen) {
            Log("SHADER READY stage=%s hash=%s source=native-DXBC-SM%u_%u bytes=%zu",
                stage, hashText.c_str(), original.versionMajor, original.versionMinor, resolved.size());
        }
        return S_OK;
    }

    std::vector<uint8_t> overrideBytes;
    const std::string overridePath = Cfg().overridesDir + "\\" + hashText + ".cso";
    if (ReadFileBytes(overridePath, overrideBytes)) {
        const DxbcInfo replacement = InspectContainer(overrideBytes.data(), overrideBytes.size());
        if (!replacement.isDxbc || !replacement.hasLegacy || replacement.versionMajor > 5) {
            Log("%s replacement %s is not a valid SM5 DXBC shader", stage, overridePath.c_str());
            return DXGI_ERROR_INVALID_CALL;
        }
        resolved = std::move(overrideBytes);
        if (firstSeen) {
            Log("SHADER READY stage=%s hash=%s source=SM5-override path=%s bytes=%zu model=%u_%u",
                stage, hashText.c_str(), overridePath.c_str(), resolved.size(),
                replacement.versionMajor, replacement.versionMinor);
        }
        return S_OK;
    }

    if (firstSeen && original.hasDxil) {
        if (Cfg().dumpShaders && original.hasDxil) {
            std::lock_guard<std::mutex> lock(g_shaderCacheMutex);
            MakeDirs(Cfg().dumpDir);
            const std::string dumpPath = Cfg().dumpDir + "\\" + hashText + ".dxil";
            if (GetFileAttributesA(dumpPath.c_str()) != INVALID_FILE_ATTRIBUTES ||
                WriteFileBytes(dumpPath, data, size)) {
                Log("SHADER DUMPED stage=%s hash=%s path=%s bytes=%zu; SM5 override is missing",
                    stage, hashText.c_str(), dumpPath.c_str(), size);
            } else {
                Log("SHADER DUMP_FAILED stage=%s hash=%s path=%s bytes=%zu",
                    stage, hashText.c_str(), dumpPath.c_str(), size);
            }
        } else if (original.hasDxil) {
            Log("SHADER DUMP_SKIPPED stage=%s hash=%s reason=dump-disabled", stage, hashText.c_str());
        }
        Log("SHADER PLACEHOLDER stage=%s hash=%s reason=no-usable-SM5-bytecode DXBC=%u DXIL=%u overrides=%s",
            stage, hashText.c_str(), original.isDxbc, original.hasDxil, overridePath.c_str());
    }
    return original.hasDxil ? S_FALSE : DXGI_ERROR_UNSUPPORTED;
}

HRESULT CreateVertexShader(Device* device, const D3D12_SHADER_BYTECODE& bytecode,
                           Ptr<ID3D11VertexShader>& shader, std::vector<uint8_t>& resolvedCode) {
    HRESULT result = LoadShader(device, "VS", bytecode, resolvedCode);
    if (result != S_OK) return result;
    result = device->d11->CreateVertexShader(resolvedCode.data(), resolvedCode.size(), nullptr, shader.Put());
    if (FAILED(result))
        Log("SHADER D3D11_CREATE_FAILED stage=VS result=0x%08lx bytes=%zu", result, resolvedCode.size());
    return result;
}

HRESULT CreatePixelShader(Device* device, const D3D12_SHADER_BYTECODE& bytecode,
                          Ptr<ID3D11PixelShader>& shader) {
    std::vector<uint8_t> code;
    HRESULT result = LoadShader(device, "PS", bytecode, code);
    if (result != S_OK) return result;
    result = device->d11->CreatePixelShader(code.data(), code.size(), nullptr, shader.Put());
    if (FAILED(result))
        Log("SHADER D3D11_CREATE_FAILED stage=PS result=0x%08lx bytes=%zu", result, code.size());
    return result;
}

HRESULT CreateGeometryShader(Device* device, const D3D12_SHADER_BYTECODE& bytecode,
                             Ptr<ID3D11GeometryShader>& shader) {
    std::vector<uint8_t> code;
    HRESULT result = LoadShader(device, "GS", bytecode, code);
    if (result != S_OK) return result;
    result = device->d11->CreateGeometryShader(code.data(), code.size(), nullptr, shader.Put());
    if (FAILED(result))
        Log("SHADER D3D11_CREATE_FAILED stage=GS result=0x%08lx bytes=%zu", result, code.size());
    return result;
}

HRESULT CreateHullShader(Device* device, const D3D12_SHADER_BYTECODE& bytecode,
                         Ptr<ID3D11HullShader>& shader) {
    std::vector<uint8_t> code;
    HRESULT result = LoadShader(device, "HS", bytecode, code);
    if (result != S_OK) return result;
    result = device->d11->CreateHullShader(code.data(), code.size(), nullptr, shader.Put());
    if (FAILED(result))
        Log("SHADER D3D11_CREATE_FAILED stage=HS result=0x%08lx bytes=%zu", result, code.size());
    return result;
}

HRESULT CreateDomainShader(Device* device, const D3D12_SHADER_BYTECODE& bytecode,
                           Ptr<ID3D11DomainShader>& shader) {
    std::vector<uint8_t> code;
    HRESULT result = LoadShader(device, "DS", bytecode, code);
    if (result != S_OK) return result;
    result = device->d11->CreateDomainShader(code.data(), code.size(), nullptr, shader.Put());
    if (FAILED(result))
        Log("SHADER D3D11_CREATE_FAILED stage=DS result=0x%08lx bytes=%zu", result, code.size());
    return result;
}

HRESULT CreateComputeShader(Device* device, const D3D12_SHADER_BYTECODE& bytecode,
                            Ptr<ID3D11ComputeShader>& shader) {
    std::vector<uint8_t> code;
    HRESULT result = LoadShader(device, "CS", bytecode, code);
    if (result != S_OK) return result;
    result = device->d11->CreateComputeShader(code.data(), code.size(), nullptr, shader.Put());
    if (FAILED(result))
        Log("SHADER D3D11_CREATE_FAILED stage=CS result=0x%08lx bytes=%zu", result, code.size());
    return result;
}

HRESULT CreateInputLayout(Device* device, const D3D12_INPUT_LAYOUT_DESC& source,
                          const std::vector<uint8_t>& vertexShaderCode, Ptr<ID3D11InputLayout>& layout) {
    if (!source.NumElements) return S_OK;
    if (!source.pInputElementDescs) return E_INVALIDARG;
    if (vertexShaderCode.empty()) return S_FALSE;
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements(source.NumElements);
    for (UINT i = 0; i < source.NumElements; ++i) {
        const auto& input = source.pInputElementDescs[i];
        if (!input.SemanticName || input.InputSlot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
            return E_INVALIDARG;
        elements[i] = {};
        elements[i].SemanticName = input.SemanticName;
        elements[i].SemanticIndex = input.SemanticIndex;
        elements[i].Format = input.Format;
        elements[i].InputSlot = input.InputSlot;
        elements[i].AlignedByteOffset = input.AlignedByteOffset;
        elements[i].InputSlotClass = static_cast<D3D11_INPUT_CLASSIFICATION>(input.InputSlotClass);
        elements[i].InstanceDataStepRate = input.InstanceDataStepRate;
    }
    const HRESULT createResult = device->d11->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()),
                                                                  vertexShaderCode.data(), vertexShaderCode.size(), layout.Put());
    if (FAILED(createResult))
        Log("INPUT_LAYOUT_FAILED elements=%u result=0x%08lx", source.NumElements, createResult);
    return createResult;
}

HRESULT CreateRasterizer(Device* device, const D3D12_RASTERIZER_DESC& source,
                         Ptr<ID3D11RasterizerState>& state) {
    if (source.ForcedSampleCount != 0 ||
        source.ConservativeRaster != D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF) {
        Unimpl("D3D12 rasterizer forced sample count/conservative rasterization");
    }
    D3D11_RASTERIZER_DESC desc{};
    desc.FillMode = source.FillMode ? static_cast<D3D11_FILL_MODE>(source.FillMode) : D3D11_FILL_SOLID;
    desc.CullMode = source.CullMode ? static_cast<D3D11_CULL_MODE>(source.CullMode) : D3D11_CULL_NONE;
    desc.FrontCounterClockwise = source.FrontCounterClockwise;
    desc.DepthBias = source.DepthBias;
    desc.DepthBiasClamp = source.DepthBiasClamp;
    desc.SlopeScaledDepthBias = source.SlopeScaledDepthBias;
    desc.DepthClipEnable = source.DepthClipEnable;
    desc.ScissorEnable = TRUE;
    desc.MultisampleEnable = source.MultisampleEnable;
    desc.AntialiasedLineEnable = source.AntialiasedLineEnable;
    return device->d11->CreateRasterizerState(&desc, state.Put());
}

HRESULT CreateBlend(Device* device, const D3D12_BLEND_DESC& source, Ptr<ID3D11BlendState>& state) {
    D3D11_BLEND_DESC desc{};
    desc.AlphaToCoverageEnable = source.AlphaToCoverageEnable;
    desc.IndependentBlendEnable = source.IndependentBlendEnable;
    for (UINT i = 0; i < 8; ++i) {
        const auto& input = source.RenderTarget[i];
        auto& output = desc.RenderTarget[i];
        if (input.LogicOpEnable) {
            Unimpl("D3D12 blend logic operations");
        }
        output.BlendEnable = input.BlendEnable;
        output.SrcBlend = input.SrcBlend ? static_cast<D3D11_BLEND>(input.SrcBlend) : D3D11_BLEND_ONE;
        output.DestBlend = input.DestBlend ? static_cast<D3D11_BLEND>(input.DestBlend) : D3D11_BLEND_ZERO;
        output.BlendOp = input.BlendOp ? static_cast<D3D11_BLEND_OP>(input.BlendOp) : D3D11_BLEND_OP_ADD;
        output.SrcBlendAlpha = input.SrcBlendAlpha ? static_cast<D3D11_BLEND>(input.SrcBlendAlpha) : D3D11_BLEND_ONE;
        output.DestBlendAlpha = input.DestBlendAlpha ? static_cast<D3D11_BLEND>(input.DestBlendAlpha) : D3D11_BLEND_ZERO;
        output.BlendOpAlpha = input.BlendOpAlpha ? static_cast<D3D11_BLEND_OP>(input.BlendOpAlpha) : D3D11_BLEND_OP_ADD;
        output.RenderTargetWriteMask = input.RenderTargetWriteMask;
    }
    return device->d11->CreateBlendState(&desc, state.Put());
}

HRESULT CreateDepthStencil(Device* device, const D3D12_DEPTH_STENCIL_DESC& source,
                           Ptr<ID3D11DepthStencilState>& state) {
    D3D11_DEPTH_STENCIL_DESC desc{};
    desc.DepthEnable = source.DepthEnable;
    desc.DepthWriteMask = static_cast<D3D11_DEPTH_WRITE_MASK>(source.DepthWriteMask);
    desc.DepthFunc = source.DepthFunc ? static_cast<D3D11_COMPARISON_FUNC>(source.DepthFunc) : D3D11_COMPARISON_LESS;
    desc.StencilEnable = source.StencilEnable;
    desc.StencilReadMask = source.StencilReadMask;
    desc.StencilWriteMask = source.StencilWriteMask;
    auto convertFace = [](const D3D12_DEPTH_STENCILOP_DESC& in, D3D11_DEPTH_STENCILOP_DESC& out) {
        out.StencilFailOp = in.StencilFailOp ? static_cast<D3D11_STENCIL_OP>(in.StencilFailOp) : D3D11_STENCIL_OP_KEEP;
        out.StencilDepthFailOp = in.StencilDepthFailOp ? static_cast<D3D11_STENCIL_OP>(in.StencilDepthFailOp) : D3D11_STENCIL_OP_KEEP;
        out.StencilPassOp = in.StencilPassOp ? static_cast<D3D11_STENCIL_OP>(in.StencilPassOp) : D3D11_STENCIL_OP_KEEP;
        out.StencilFunc = in.StencilFunc ? static_cast<D3D11_COMPARISON_FUNC>(in.StencilFunc) : D3D11_COMPARISON_ALWAYS;
    };
    convertFace(source.FrontFace, desc.FrontFace);
    convertFace(source.BackFace, desc.BackFace);
    return device->d11->CreateDepthStencilState(&desc, state.Put());
}

HRESULT CheckRootSignature(ID3D12RootSignature* source, Ptr<IUnknown>& root) {
    if (!source) return S_OK;
    auto* wrapped = dynamic_cast<RootSignature*>(source);
    if (!wrapped) return E_INVALIDARG;
    root = Ptr<IUnknown>(source);
    return S_OK;
}

D3D12_BLEND_DESC DefaultBlendDesc() {
    D3D12_BLEND_DESC desc{};
    for (UINT i = 0; i < 8; ++i) {
        auto& rt = desc.RenderTarget[i];
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ZERO;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    return desc;
}

D3D12_RASTERIZER_DESC DefaultRasterizerDesc() {
    D3D12_RASTERIZER_DESC desc{};
    desc.FillMode = D3D12_FILL_MODE_SOLID;
    desc.CullMode = D3D12_CULL_MODE_BACK;
    desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.DepthClipEnable = TRUE;
    return desc;
}

D3D12_DEPTH_STENCIL_DESC DefaultDepthStencilDesc() {
    D3D12_DEPTH_STENCIL_DESC desc{};
    desc.DepthEnable = TRUE;
    desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    const D3D12_DEPTH_STENCILOP_DESC defaultFace{
        D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS
    };
    desc.FrontFace = defaultFace;
    desc.BackFace = defaultFace;
    return desc;
}

template <typename Inner>
struct alignas(void*) StreamSubobject {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
    Inner inner;
};

struct CompatDepthStencilOpDesc1 {
    D3D12_STENCIL_OP StencilFailOp;
    D3D12_STENCIL_OP StencilDepthFailOp;
    D3D12_STENCIL_OP StencilPassOp;
    D3D12_COMPARISON_FUNC StencilFunc;
    UINT8 StencilReadMask;
    UINT8 StencilWriteMask;
};

struct CompatDepthStencilDesc2 {
    BOOL DepthEnable;
    D3D12_DEPTH_WRITE_MASK DepthWriteMask;
    D3D12_COMPARISON_FUNC DepthFunc;
    BOOL StencilEnable;
    CompatDepthStencilOpDesc1 FrontFace;
    CompatDepthStencilOpDesc1 BackFace;
    BOOL DepthBoundsTestEnable;
};

} // namespace

HRESULT PipelineState::CreateGraphics(Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                                      PipelineState** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!device || !desc || !HasBytecode(desc->VS) ||
        (desc->NumRenderTargets > 8) ||
        desc->NodeMask > 1)
        return E_INVALIDARG;
    const uint32_t psoIndex = ++g_graphicsPsoCount;
    const bool logMilestone = ShouldLogPsoMilestone(psoIndex);
    if (logMilestone) {
        Log("PSO GRAPHICS #%u BEGIN VS=%zu PS=%zu GS=%zu HS=%zu DS=%zu input-elements=%u RTVs=%u samples=%u/%u topology=%u flags=0x%x",
            psoIndex, desc->VS.BytecodeLength, desc->PS.BytecodeLength, desc->GS.BytecodeLength,
            desc->HS.BytecodeLength, desc->DS.BytecodeLength, desc->InputLayout.NumElements,
            desc->NumRenderTargets, desc->SampleDesc.Count, desc->SampleDesc.Quality,
            desc->PrimitiveTopologyType, desc->Flags);
    }
    if (desc->StreamOutput.NumEntries || desc->StreamOutput.NumStrides || desc->StreamOutput.pSODeclaration ||
        desc->StreamOutput.pBufferStrides || desc->StreamOutput.RasterizedStream) {
        Unimpl("ID3D12Device::CreateGraphicsPipelineState stream output");
        Log("PSO GRAPHICS BLOCKED reason=stream-output");
        return E_NOTIMPL;
    }

    std::unique_ptr<PipelineState> pipeline(new (std::nothrow) PipelineState(device));
    if (!pipeline) return E_OUTOFMEMORY;
    HRESULT result = CheckRootSignature(desc->pRootSignature, pipeline->rootSig);
    if (FAILED(result)) return result;
    bool anyMissingOverride = false;
    std::vector<uint8_t> vsCode;
    if (FAILED(result = CreateVertexShader(device, desc->VS, pipeline->vs, vsCode))) return result;
    pipeline->missingVs = result == S_FALSE;
    pipeline->shaderHash = Fnv1a64(desc->VS.pShaderBytecode, desc->VS.BytecodeLength);
    if (result == S_FALSE) anyMissingOverride = true;
    if (HasBytecode(desc->PS)) {
        if (FAILED(result = CreatePixelShader(device, desc->PS, pipeline->ps))) return result;
        if (result == S_FALSE) anyMissingOverride = true;
    }
    if (HasBytecode(desc->GS)) {
        if (FAILED(result = CreateGeometryShader(device, desc->GS, pipeline->gs))) return result;
        if (result == S_FALSE) anyMissingOverride = true;
    }
    if (HasBytecode(desc->HS)) {
        if (FAILED(result = CreateHullShader(device, desc->HS, pipeline->hs))) return result;
        if (result == S_FALSE) anyMissingOverride = true;
    }
    if (HasBytecode(desc->DS)) {
        if (FAILED(result = CreateDomainShader(device, desc->DS, pipeline->ds))) return result;
        if (result == S_FALSE) anyMissingOverride = true;
    }
    if (FAILED(result = CreateInputLayout(device, desc->InputLayout, vsCode, pipeline->il))) return result;
    if (FAILED(result = CreateRasterizer(device, desc->RasterizerState, pipeline->rs))) return result;
    if (FAILED(result = CreateBlend(device, desc->BlendState, pipeline->bs))) return result;
    if (FAILED(result = CreateDepthStencil(device, desc->DepthStencilState, pipeline->dss))) return result;

    pipeline->sampleMask = desc->SampleMask;
    pipeline->missingShader = anyMissingOverride;
    if (Cfg().diagnosticRenderTrace) {
        auto& stats = TraceStats();
        stats.psoGraphics.fetch_add(1, std::memory_order_relaxed);
        stats.lastPsoHash.store(pipeline->shaderHash, std::memory_order_relaxed);
    }
    *out = pipeline.release();
    if (logMilestone) {
        Log("PSO GRAPHICS #%u READY status=%s",
            psoIndex, (*out)->missingShader ? "placeholder-missing-sm5-override" : "ready");
    }
    return S_OK;
}

HRESULT PipelineState::CreateCompute(Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
                                     PipelineState** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!device || !desc || !HasBytecode(desc->CS)) return E_INVALIDARG;
    const uint32_t psoIndex = ++g_computePsoCount;
    const bool logMilestone = ShouldLogPsoMilestone(psoIndex);
    if (logMilestone)
        Log("PSO COMPUTE #%u BEGIN CS=%zu flags=0x%x", psoIndex, desc->CS.BytecodeLength, desc->Flags);
    std::unique_ptr<PipelineState> pipeline(new (std::nothrow) PipelineState(device));
    if (!pipeline) return E_OUTOFMEMORY;
    HRESULT result = CheckRootSignature(desc->pRootSignature, pipeline->rootSig);
    if (FAILED(result)) return result;
    if (FAILED(result = CreateComputeShader(device, desc->CS, pipeline->cs))) return result;
    pipeline->missingCs = result == S_FALSE;
    pipeline->shaderHash = Fnv1a64(desc->CS.pShaderBytecode, desc->CS.BytecodeLength);
    pipeline->missingShader = result == S_FALSE;
    pipeline->compute = true;
    if (Cfg().diagnosticRenderTrace) {
        auto& stats = TraceStats();
        stats.psoCompute.fetch_add(1, std::memory_order_relaxed);
        stats.lastPsoHash.store(pipeline->shaderHash, std::memory_order_relaxed);
    }
    *out = pipeline.release();
    if (logMilestone) {
        Log("PSO COMPUTE #%u READY status=%s",
            psoIndex, (*out)->missingShader ? "placeholder-missing-sm5-override" : "ready");
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
    REFIID riid, void** out) {
    TraceApi("Device::CreateGraphicsPipelineState");
    if (!out) return E_POINTER;
    *out = nullptr;
    PipelineState* pipeline = nullptr;
    const HRESULT createResult = PipelineState::CreateGraphics(this, desc, &pipeline);
    if (FAILED(createResult)) {
        Log("PSO GRAPHICS FAILED result=0x%08lx", createResult);
        return createResult;
    }
    const HRESULT queryResult = pipeline->QueryInterface(riid, out);
    pipeline->Release();
    return queryResult;
}

HRESULT STDMETHODCALLTYPE Device::CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
    REFIID riid, void** out) {
    TraceApi("Device::CreateComputePipelineState");
    if (!out) return E_POINTER;
    *out = nullptr;
    PipelineState* pipeline = nullptr;
    const HRESULT createResult = PipelineState::CreateCompute(this, desc, &pipeline);
    if (FAILED(createResult)) {
        Log("PSO COMPUTE FAILED result=0x%08lx", createResult);
        return createResult;
    }
    const HRESULT queryResult = pipeline->QueryInterface(riid, out);
    pipeline->Release();
    return queryResult;
}

HRESULT STDMETHODCALLTYPE Device::CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC* desc,
    REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!desc || !desc->SizeInBytes || !desc->pPipelineStateSubobjectStream ||
        (reinterpret_cast<uintptr_t>(desc->pPipelineStateSubobjectStream) & (alignof(void*) - 1)))
        return E_INVALIDARG;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gfx{};
    gfx.BlendState = DefaultBlendDesc();
    gfx.SampleMask = UINT_MAX;
    gfx.RasterizerState = DefaultRasterizerDesc();
    gfx.DepthStencilState = DefaultDepthStencilDesc();
    gfx.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gfx.SampleDesc.Count = 1;

    D3D12_COMPUTE_PIPELINE_STATE_DESC comp{};
    bool hasCs = false;
    bool hasVs = false;
    bool hasExplicitDepthStencil = false;
    bool hasDsvFormat = false;
    std::set<UINT> seenSubobjects;

    const auto* stream = static_cast<const uint8_t*>(desc->pPipelineStateSubobjectStream);
    if (desc->SizeInBytes > static_cast<SIZE_T>(PTRDIFF_MAX)) return E_INVALIDARG;
    const auto* cursor = stream;
    const auto* end = stream + desc->SizeInBytes;

    while (cursor < end) {
        const size_t remaining = static_cast<size_t>(end - cursor);
        if (remaining < sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE)) return E_INVALIDARG;
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type{};
        std::memcpy(&type, cursor, sizeof(type));
        if (!seenSubobjects.insert(static_cast<UINT>(type)).second) {
            Log("PSO STREAM duplicate subobject type=%u", static_cast<UINT>(type));
            return E_INVALIDARG;
        }
        size_t advance = 0;

        switch (type) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: {
            using Sub = StreamSubobject<ID3D12RootSignature*>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            ID3D12RootSignature* root = reinterpret_cast<const Sub*>(cursor)->inner;
            gfx.pRootSignature = root;
            comp.pRootSignature = root;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: {
            using Sub = StreamSubobject<D3D12_SHADER_BYTECODE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.VS = reinterpret_cast<const Sub*>(cursor)->inner;
            hasVs = HasBytecode(gfx.VS);
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: {
            using Sub = StreamSubobject<D3D12_SHADER_BYTECODE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.PS = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: {
            using Sub = StreamSubobject<D3D12_SHADER_BYTECODE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.DS = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: {
            using Sub = StreamSubobject<D3D12_SHADER_BYTECODE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.HS = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: {
            using Sub = StreamSubobject<D3D12_SHADER_BYTECODE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.GS = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: {
            using Sub = StreamSubobject<D3D12_SHADER_BYTECODE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            comp.CS = reinterpret_cast<const Sub*>(cursor)->inner;
            hasCs = HasBytecode(comp.CS);
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: {
            using Sub = StreamSubobject<D3D12_STREAM_OUTPUT_DESC>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.StreamOutput = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: {
            using Sub = StreamSubobject<D3D12_BLEND_DESC>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.BlendState = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK: {
            using Sub = StreamSubobject<UINT>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.SampleMask = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: {
            using Sub = StreamSubobject<D3D12_RASTERIZER_DESC>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.RasterizerState = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: {
            using Sub = StreamSubobject<D3D12_DEPTH_STENCIL_DESC>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.DepthStencilState = reinterpret_cast<const Sub*>(cursor)->inner;
            hasExplicitDepthStencil = true;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: {
            using Sub = StreamSubobject<D3D12_INPUT_LAYOUT_DESC>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.InputLayout = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: {
            using Sub = StreamSubobject<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.IBStripCutValue = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: {
            using Sub = StreamSubobject<D3D12_PRIMITIVE_TOPOLOGY_TYPE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.PrimitiveTopologyType = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: {
            using Sub = StreamSubobject<D3D12_RT_FORMAT_ARRAY>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            const auto& formats = reinterpret_cast<const Sub*>(cursor)->inner;
            if (formats.NumRenderTargets > ARRAYSIZE(gfx.RTVFormats)) return E_INVALIDARG;
            gfx.NumRenderTargets = formats.NumRenderTargets;
            std::copy(std::begin(formats.RTFormats), std::end(formats.RTFormats), std::begin(gfx.RTVFormats));
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: {
            using Sub = StreamSubobject<DXGI_FORMAT>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.DSVFormat = reinterpret_cast<const Sub*>(cursor)->inner;
            hasDsvFormat = true;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: {
            using Sub = StreamSubobject<DXGI_SAMPLE_DESC>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            gfx.SampleDesc = reinterpret_cast<const Sub*>(cursor)->inner;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: {
            using Sub = StreamSubobject<UINT>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            const UINT mask = reinterpret_cast<const Sub*>(cursor)->inner;
            gfx.NodeMask = mask;
            comp.NodeMask = mask;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: {
            using Sub = StreamSubobject<D3D12_CACHED_PIPELINE_STATE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            const auto& cached = reinterpret_cast<const Sub*>(cursor)->inner;
            gfx.CachedPSO = cached;
            comp.CachedPSO = cached;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: {
            using Sub = StreamSubobject<D3D12_PIPELINE_STATE_FLAGS>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            const auto flags = reinterpret_cast<const Sub*>(cursor)->inner;
            gfx.Flags = flags;
            comp.Flags = flags;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: {
            using Sub = StreamSubobject<D3D12_DEPTH_STENCIL_DESC1>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            const auto& ds1 = reinterpret_cast<const Sub*>(cursor)->inner;
            gfx.DepthStencilState.DepthEnable = ds1.DepthEnable;
            gfx.DepthStencilState.DepthWriteMask = ds1.DepthWriteMask;
            gfx.DepthStencilState.DepthFunc = ds1.DepthFunc;
            gfx.DepthStencilState.StencilEnable = ds1.StencilEnable;
            gfx.DepthStencilState.StencilReadMask = ds1.StencilReadMask;
            gfx.DepthStencilState.StencilWriteMask = ds1.StencilWriteMask;
            gfx.DepthStencilState.FrontFace = ds1.FrontFace;
            gfx.DepthStencilState.BackFace = ds1.BackFace;
            hasExplicitDepthStencil = true;
            advance = sizeof(Sub);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: {
            using Sub = StreamSubobject<D3D12_VIEW_INSTANCING_DESC>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            advance = sizeof(Sub);
            break;
        }
        case static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(24):
        case static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(25): {
            using Sub = StreamSubobject<D3D12_SHADER_BYTECODE>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            advance = sizeof(Sub);
            break;
        }
        case static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(26): {
            using Sub = StreamSubobject<CompatDepthStencilDesc2>;
            if (remaining < sizeof(Sub)) return E_INVALIDARG;
            const auto& ds2 = reinterpret_cast<const Sub*>(cursor)->inner;
            gfx.DepthStencilState.DepthEnable = ds2.DepthEnable;
            gfx.DepthStencilState.DepthWriteMask = ds2.DepthWriteMask;
            gfx.DepthStencilState.DepthFunc = ds2.DepthFunc;
            gfx.DepthStencilState.StencilEnable = ds2.StencilEnable;
            gfx.DepthStencilState.StencilReadMask = ds2.FrontFace.StencilReadMask;
            gfx.DepthStencilState.StencilWriteMask = ds2.FrontFace.StencilWriteMask;
            gfx.DepthStencilState.FrontFace = {
                ds2.FrontFace.StencilFailOp, ds2.FrontFace.StencilDepthFailOp,
                ds2.FrontFace.StencilPassOp, ds2.FrontFace.StencilFunc
            };
            gfx.DepthStencilState.BackFace = {
                ds2.BackFace.StencilFailOp, ds2.BackFace.StencilDepthFailOp,
                ds2.BackFace.StencilPassOp, ds2.BackFace.StencilFunc
            };
            hasExplicitDepthStencil = true;
            advance = sizeof(Sub);
            break;
        }
        default:
            Log("PSO STREAM unknown subobject type=%u at offset=%zu/%zu",
                static_cast<UINT>(type), static_cast<size_t>(cursor - stream), desc->SizeInBytes);
            return E_INVALIDARG;
        }

        const size_t alignedAdvance = static_cast<size_t>(AlignUp(advance, sizeof(void*)));
        if (!advance || alignedAdvance > remaining) return E_INVALIDARG;
        cursor += alignedAdvance;
    }

    if ((!hasDsvFormat || gfx.DSVFormat == DXGI_FORMAT_UNKNOWN) && !hasExplicitDepthStencil)
        gfx.DepthStencilState.DepthEnable = FALSE;

    if (hasCs && !hasVs) return CreateComputePipelineState(&comp, riid, out);
    if (hasVs && !hasCs) return CreateGraphicsPipelineState(&gfx, riid, out);

    Log("PSO STREAM invalid shader combination hasVs=%u hasCs=%u bytes=%zu",
        hasVs ? 1u : 0u, hasCs ? 1u : 0u, desc->SizeInBytes);
    return E_INVALIDARG;
}
