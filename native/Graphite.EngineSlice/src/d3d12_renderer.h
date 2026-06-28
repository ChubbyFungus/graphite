#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <vector>

struct UiTexture
{
    void* textureId = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

class D3D12Renderer
{
public:
    using ProductUiCallback = std::function<void(D3D12Renderer&)>;

    bool initialize(HWND hwnd, std::uint32_t textureWidth, std::uint32_t textureHeight);
    void shutdown();
    void present();
    void present(const std::function<void()>& drawUi);
    void present(std::uint64_t cudaFenceValue, const ProductUiCallback& drawProductUi, const std::function<void()>& drawDebugUi);
    void present(std::uint64_t cudaFenceValue, const std::function<void()>& drawUi);
    HANDLE sharedCanvasHandle() const;
    HANDLE cudaFenceHandle() const;
    std::uint64_t sharedCanvasBytes() const;
    std::uint64_t nextCudaFenceValue() const;
    UiTexture loadUiTexture(const wchar_t* path);
    UiTexture createSolidUiTexture(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
    void drawProductTexture(const UiTexture& texture, float x, float y, float width, float height, std::uint32_t color = 0xffffffffu);
    void drawProductTextureUv(const UiTexture& texture, float x, float y, float width, float height, float u0, float v0, float u1, float v1, std::uint32_t color = 0xffffffffu);

private:
    bool createDevice(HWND hwnd);
    bool createCanvasResources();
    bool initializeImGui(HWND hwnd);
    bool initializeProductOverlay();
    UiTexture uploadUiTexture(std::uint32_t width, std::uint32_t height, const std::uint8_t* pixels);
    void beginProductOverlay(std::uint32_t width, std::uint32_t height);
    void waitForGpu();

    std::uint32_t textureWidth_ = 0;
    std::uint32_t textureHeight_ = 0;
    std::uint32_t frameIndex_ = 0;
    HANDLE fenceEvent_ = nullptr;
    std::uint64_t fenceValue_ = 0;

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imguiSrvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> renderTargets_[2];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12Resource> canvasTexture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> productRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> productPipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource> productVertexBuffer_;
    std::uint8_t* productVertexMapped_ = nullptr;
    std::uint32_t productVertexOffset_ = 0;
    std::uint32_t productViewportWidth_ = 1;
    std::uint32_t productViewportHeight_ = 1;
    struct OwnedTexture
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        Microsoft::WRL::ComPtr<ID3D12Resource> upload;
        UiTexture view;
    };
    std::vector<OwnedTexture> uiTextures_;
    HANDLE sharedCanvasHandle_ = nullptr;
    HANDLE cudaFenceHandle_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Fence> cudaFence_;
    std::uint64_t cudaFenceValue_ = 0;
    std::uint64_t sharedCanvasBytes_ = 0;
    std::uint32_t rtvDescriptorSize_ = 0;
    std::uint32_t srvDescriptorSize_ = 0;
    std::uint32_t nextSrvDescriptor_ = 1;
};
