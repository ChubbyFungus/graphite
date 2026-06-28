#include "d3d12_renderer.h"

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include <d3dcompiler.h>
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr std::uint32_t kFrameCount = 2;
constexpr std::uint32_t kSrvDescriptorCount = 128;
constexpr std::uint32_t kProductOverlayVertexCapacity = 8192;

struct ProductOverlayVertex
{
    float x;
    float y;
    float u;
    float v;
    std::uint32_t color;
};

bool ok(HRESULT hr, const char* op)
{
    if (SUCCEEDED(hr)) return true;
    std::fprintf(stderr, "D3D12 %s failed: 0x%08lx\n", op, static_cast<unsigned long>(hr));
    return false;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}
}

bool D3D12Renderer::initialize(HWND hwnd, std::uint32_t textureWidth, std::uint32_t textureHeight)
{
    textureWidth_ = textureWidth;
    textureHeight_ = textureHeight;
    return createDevice(hwnd) && createCanvasResources() && initializeProductOverlay() && initializeImGui(hwnd);
}

void D3D12Renderer::shutdown()
{
    waitForGpu();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (productVertexBuffer_ && productVertexMapped_)
    {
        productVertexBuffer_->Unmap(0, nullptr);
        productVertexMapped_ = nullptr;
    }
    if (fenceEvent_)
    {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    if (sharedCanvasHandle_)
    {
        CloseHandle(sharedCanvasHandle_);
        sharedCanvasHandle_ = nullptr;
    }
    if (cudaFenceHandle_)
    {
        CloseHandle(cudaFenceHandle_);
        cudaFenceHandle_ = nullptr;
    }
}

bool D3D12Renderer::createDevice(HWND hwnd)
{
    if (!ok(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)), "create DXGI factory")) return false;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) break;
    }
    if (!device_ && !ok(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)), "create default device")) return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!ok(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_)), "create command queue")) return false;

    RECT rect{};
    GetClientRect(hwnd, &rect);
    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.BufferCount = kFrameCount;
    swapDesc.Width = std::max<LONG>(1, rect.right - rect.left);
    swapDesc.Height = std::max<LONG>(1, rect.bottom - rect.top);
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    if (!ok(factory_->CreateSwapChainForHwnd(commandQueue_.Get(), hwnd, &swapDesc, nullptr, nullptr, &swapChain), "create swap chain")) return false;
    if (!ok(swapChain.As(&swapChain_), "query swap chain")) return false;
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = kFrameCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (!ok(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_)), "create RTV heap")) return false;
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    auto rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < kFrameCount; ++i)
    {
        if (!ok(swapChain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i])), "get swap buffer")) return false;
        device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize_;
    }

    if (!ok(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_)), "create command allocator")) return false;
    if (!ok(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_)), "create command list")) return false;
    commandList_->Close();

    if (!ok(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "create fence")) return false;
    if (!ok(device_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&cudaFence_)), "create CUDA shared fence")) return false;
    if (!ok(device_->CreateSharedHandle(cudaFence_.Get(), nullptr, GENERIC_ALL, nullptr, &cudaFenceHandle_), "create CUDA shared fence handle")) return false;
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    return fenceEvent_ != nullptr;
}

bool D3D12Renderer::createCanvasResources()
{
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = textureWidth_;
    textureDesc.Height = textureHeight_;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (!ok(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_SHARED, &textureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&canvasTexture_)), "create shared canvas texture")) return false;

    D3D12_RESOURCE_ALLOCATION_INFO allocation = device_->GetResourceAllocationInfo(0, 1, &textureDesc);
    sharedCanvasBytes_ = allocation.SizeInBytes;
    return ok(device_->CreateSharedHandle(canvasTexture_.Get(), nullptr, GENERIC_ALL, nullptr, &sharedCanvasHandle_), "create shared canvas handle");
}

void D3D12Renderer::present()
{
    present(0, {});
}

bool D3D12Renderer::initializeImGui(HWND hwnd)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = kSrvDescriptorCount;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!ok(device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&imguiSrvHeap_)), "create ImGui descriptor heap")) return false;
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(
        device_.Get(),
        kFrameCount,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        imguiSrvHeap_.Get(),
        imguiSrvHeap_->GetCPUDescriptorHandleForHeapStart(),
        imguiSrvHeap_->GetGPUDescriptorHandleForHeapStart());
    return true;
}

bool D3D12Renderer::initializeProductOverlay()
{
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 2;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (!ok(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error), "serialize product UI root signature")) return false;
    if (!ok(device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&productRootSignature_)), "create product UI root signature")) return false;

    const char* shader = R"(
cbuffer Viewport : register(b0) { float2 viewportSize; };
Texture2D uiTexture : register(t0);
SamplerState uiSampler : register(s0);
struct VSIn { float2 position : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
struct PSIn { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
PSIn vs_main(VSIn input) {
    PSIn output;
    float2 clip = float2((input.position.x / viewportSize.x) * 2.0 - 1.0, 1.0 - (input.position.y / viewportSize.y) * 2.0);
    output.position = float4(clip, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
float4 ps_main(PSIn input) : SV_TARGET {
    return uiTexture.Sample(uiSampler, input.uv) * input.color;
}
)";

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    if (!ok(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vertexShader, &error), "compile product UI vertex shader")) return false;
    if (!ok(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &pixelShader, &error), "compile product UI pixel shader")) return false;

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ProductOverlayVertex, x), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ProductOverlayVertex, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ProductOverlayVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.DepthClipEnable = TRUE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = productRootSignature_.Get();
    pso.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    pso.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    pso.BlendState = blend;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = rasterizer;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {inputLayout, static_cast<UINT>(std::size(inputLayout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (!ok(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&productPipeline_)), "create product UI pipeline")) return false;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(ProductOverlayVertex) * kProductOverlayVertexCapacity;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (!ok(device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&productVertexBuffer_)), "create product UI vertex buffer")) return false;
    return ok(productVertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&productVertexMapped_)), "map product UI vertex buffer");
}

UiTexture D3D12Renderer::loadUiTexture(const wchar_t* path)
{
    if (!device_ || !commandQueue_ || !imguiSrvHeap_ || nextSrvDescriptor_ >= kSrvDescriptorCount) return {};

    ComPtr<IWICImagingFactory> factory;
    if (!ok(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "create WIC factory")) return {};

    ComPtr<IWICBitmapDecoder> decoder;
    if (!ok(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder), "decode UI image")) return {};

    ComPtr<IWICBitmapFrameDecode> frame;
    if (!ok(decoder->GetFrame(0, &frame), "get UI image frame")) return {};

    UINT width = 0;
    UINT height = 0;
    if (!ok(frame->GetSize(&width, &height), "get UI image size") || width == 0 || height == 0) return {};

    ComPtr<IWICFormatConverter> converter;
    if (!ok(factory->CreateFormatConverter(&converter), "create UI image converter")) return {};
    if (!ok(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom), "convert UI image")) return {};

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    if (!ok(converter->CopyPixels(nullptr, width * 4u, static_cast<UINT>(pixels.size()), pixels.data()), "copy UI image pixels")) return {};

    return uploadUiTexture(width, height, pixels.data());
}

UiTexture D3D12Renderer::createSolidUiTexture(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    const std::uint8_t pixels[] = {r, g, b, a};
    return uploadUiTexture(1, 1, pixels);
}

UiTexture D3D12Renderer::uploadUiTexture(std::uint32_t width, std::uint32_t height, const std::uint8_t* pixels)
{
    if (!device_ || !commandQueue_ || !imguiSrvHeap_ || nextSrvDescriptor_ >= kSrvDescriptorCount) return {};
    if (!pixels || width == 0 || height == 0) return {};

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    OwnedTexture owned;
    if (!ok(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&owned.texture)), "create UI texture")) return {};

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 uploadBytes = 0;
    device_->GetCopyableFootprints(&textureDesc, 0, 1, 0, &layout, &rows, &rowBytes, &uploadBytes);

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadBytes;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (!ok(device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&owned.upload)), "create UI texture upload")) return {};

    std::uint8_t* mapped = nullptr;
    if (!ok(owned.upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "map UI texture upload")) return {};
    const std::size_t sourcePitch = static_cast<std::size_t>(width) * 4u;
    for (UINT y = 0; y < height; ++y)
    {
        std::memcpy(mapped + layout.Footprint.RowPitch * y, pixels + sourcePitch * y, sourcePitch);
    }
    owned.upload->Unmap(0, nullptr);

    commandAllocator_->Reset();
    commandList_->Reset(commandAllocator_.Get(), nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = owned.texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = owned.upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = layout;
    commandList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    auto toShader = transition(owned.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList_->ResourceBarrier(1, &toShader);
    commandList_->Close();
    ID3D12CommandList* lists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, lists);
    waitForGpu();

    const std::uint32_t descriptorIndex = nextSrvDescriptor_++;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = imguiSrvHeap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(descriptorIndex) * srvDescriptorSize_;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = imguiSrvHeap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(descriptorIndex) * srvDescriptorSize_;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(owned.texture.Get(), &srv, cpu);

    owned.view.textureId = reinterpret_cast<void*>(gpu.ptr);
    owned.view.width = width;
    owned.view.height = height;
    const UiTexture view = owned.view;
    uiTextures_.push_back(owned);
    return view;
}

void D3D12Renderer::present(const std::function<void()>& drawUi)
{
    present(0, drawUi);
}

void D3D12Renderer::present(std::uint64_t cudaFenceValue, const std::function<void()>& drawUi)
{
    present(cudaFenceValue, {}, drawUi);
}

void D3D12Renderer::beginProductOverlay(std::uint32_t width, std::uint32_t height)
{
    productViewportWidth_ = std::max<std::uint32_t>(1, width);
    productViewportHeight_ = std::max<std::uint32_t>(1, height);
    productVertexOffset_ = 0;

    commandList_->SetPipelineState(productPipeline_.Get());
    commandList_->SetGraphicsRootSignature(productRootSignature_.Get());
    const float constants[] = {static_cast<float>(productViewportWidth_), static_cast<float>(productViewportHeight_)};
    commandList_->SetGraphicsRoot32BitConstants(0, 2, constants, 0);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void D3D12Renderer::drawProductTexture(const UiTexture& texture, float x, float y, float width, float height, std::uint32_t color)
{
    drawProductTextureUv(texture, x, y, width, height, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void D3D12Renderer::drawProductTextureUv(const UiTexture& texture, float x, float y, float width, float height, float u0, float v0, float u1, float v1, std::uint32_t color)
{
    if (!texture.textureId || !productVertexMapped_ || width <= 0.0f || height <= 0.0f) return;
    if (productVertexOffset_ + 6 > kProductOverlayVertexCapacity) return;

    auto* vertices = reinterpret_cast<ProductOverlayVertex*>(productVertexMapped_) + productVertexOffset_;
    const float x0 = x;
    const float y0 = y;
    const float x1 = x + width;
    const float y1 = y + height;
    vertices[0] = {x0, y0, u0, v0, color};
    vertices[1] = {x1, y0, u1, v0, color};
    vertices[2] = {x1, y1, u1, v1, color};
    vertices[3] = {x0, y0, u0, v0, color};
    vertices[4] = {x1, y1, u1, v1, color};
    vertices[5] = {x0, y1, u0, v1, color};

    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = productVertexBuffer_->GetGPUVirtualAddress() + sizeof(ProductOverlayVertex) * productVertexOffset_;
    view.SizeInBytes = sizeof(ProductOverlayVertex) * 6;
    view.StrideInBytes = sizeof(ProductOverlayVertex);
    commandList_->IASetVertexBuffers(0, 1, &view);
    commandList_->SetGraphicsRootDescriptorTable(1, D3D12_GPU_DESCRIPTOR_HANDLE{static_cast<UINT64>(reinterpret_cast<std::uintptr_t>(texture.textureId))});
    commandList_->DrawInstanced(6, 1, 0, 0);
    productVertexOffset_ += 6;
}

void D3D12Renderer::present(std::uint64_t cudaFenceValue, const ProductUiCallback& drawProductUi, const std::function<void()>& drawDebugUi)
{
    if (cudaFence_ && cudaFenceValue > cudaFenceValue_)
    {
        commandQueue_->Wait(cudaFence_.Get(), cudaFenceValue);
        cudaFenceValue_ = cudaFenceValue;
    }

    commandAllocator_->Reset();
    commandList_->Reset(commandAllocator_.Get(), nullptr);

    auto canvasToCopySource = transition(canvasTexture_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList_->ResourceBarrier(1, &canvasToCopySource);

    auto toCopyDest = transition(renderTargets_[frameIndex_].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    commandList_->ResourceBarrier(1, &toCopyDest);
    D3D12_TEXTURE_COPY_LOCATION backBuffer{};
    backBuffer.pResource = renderTargets_[frameIndex_].Get();
    backBuffer.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    backBuffer.SubresourceIndex = 0;
    D3D12_BOX sourceBox{0, 0, 0, textureWidth_, textureHeight_, 1};
    D3D12_TEXTURE_COPY_LOCATION canvasSource{};
    canvasSource.pResource = canvasTexture_.Get();
    canvasSource.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    canvasSource.SubresourceIndex = 0;
    commandList_->CopyTextureRegion(&backBuffer, 0, 0, 0, &canvasSource, &sourceBox);
    auto canvasToCommon = transition(canvasTexture_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    commandList_->ResourceBarrier(1, &canvasToCommon);

    if (drawProductUi || drawDebugUi)
    {
        auto toRenderTarget = transition(renderTargets_[frameIndex_].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList_->ResourceBarrier(1, &toRenderTarget);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(frameIndex_) * rtvDescriptorSize_;
        commandList_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        ID3D12DescriptorHeap* heaps[] = {imguiSrvHeap_.Get()};
        commandList_->SetDescriptorHeaps(1, heaps);

        RECT rect{};
        DXGI_SWAP_CHAIN_DESC swapDesc{};
        swapChain_->GetDesc(&swapDesc);
        rect.right = static_cast<LONG>(std::max<UINT>(1, swapDesc.BufferDesc.Width));
        rect.bottom = static_cast<LONG>(std::max<UINT>(1, swapDesc.BufferDesc.Height));
        if (drawProductUi)
        {
            D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(rect.right), static_cast<float>(rect.bottom), 0.0f, 1.0f};
            D3D12_RECT scissor{0, 0, rect.right, rect.bottom};
            commandList_->RSSetViewports(1, &viewport);
            commandList_->RSSetScissorRects(1, &scissor);
            beginProductOverlay(static_cast<std::uint32_t>(rect.right), static_cast<std::uint32_t>(rect.bottom));
            drawProductUi(*this);
        }

        if (drawDebugUi)
        {
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            drawDebugUi();
            ImGui::Render();
            commandList_->SetDescriptorHeaps(1, heaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList_.Get());
        }

        auto renderTargetToPresent = transition(renderTargets_[frameIndex_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commandList_->ResourceBarrier(1, &renderTargetToPresent);
    }
    else
    {
        auto toPresent = transition(renderTargets_[frameIndex_].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        commandList_->ResourceBarrier(1, &toPresent);
    }
    commandList_->Close();

    ID3D12CommandList* lists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, lists);
    swapChain_->Present(1, 0);
    waitForGpu();
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

HANDLE D3D12Renderer::sharedCanvasHandle() const
{
    return sharedCanvasHandle_;
}

HANDLE D3D12Renderer::cudaFenceHandle() const
{
    return cudaFenceHandle_;
}

std::uint64_t D3D12Renderer::sharedCanvasBytes() const
{
    return sharedCanvasBytes_;
}

std::uint64_t D3D12Renderer::nextCudaFenceValue() const
{
    return cudaFenceValue_ + 1;
}

void D3D12Renderer::waitForGpu()
{
    if (!commandQueue_ || !fence_ || !fenceEvent_) return;
    const std::uint64_t value = ++fenceValue_;
    commandQueue_->Signal(fence_.Get(), value);
    if (fence_->GetCompletedValue() < value)
    {
        fence_->SetEventOnCompletion(value, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}
