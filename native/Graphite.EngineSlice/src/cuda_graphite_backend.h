#pragma once

#include "igraphite_backend.h"

#include <cstdint>
#include <memory>
#include <vector>

struct CudaGraphiteSnapshot
{
    CudaGraphiteSnapshot(std::uint32_t width, std::uint32_t height, std::uint32_t pageCapacity);
    CudaGraphiteSnapshot(const CudaGraphiteSnapshot&) = delete;
    CudaGraphiteSnapshot& operator=(const CudaGraphiteSnapshot&) = delete;
    CudaGraphiteSnapshot(CudaGraphiteSnapshot&& other) noexcept;
    CudaGraphiteSnapshot& operator=(CudaGraphiteSnapshot&& other) noexcept;
    ~CudaGraphiteSnapshot();

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pageCapacity = 0;
    std::uint32_t allocatedPageCount = 0;
    float* paperHeight = nullptr;
    float* paperRoughness = nullptr;
    float* looseGraphite = nullptr;
    float* boundGraphite = nullptr;
    float* compaction = nullptr;
    float* damage = nullptr;
    float* paperBinding = nullptr;
    unsigned char* tileAllocated = nullptr;
    std::uint32_t* tilePageTable = nullptr;
};

class CudaGraphiteBackend final : public IGraphiteBackend
{
public:
    CudaGraphiteBackend() = default;
    ~CudaGraphiteBackend() override;

    bool initialize(const GraphiteBackendInit& init) override;
    void shutdown() override;
    void clear() override;
    void clearTiles(const std::vector<std::uint32_t>& tileIndices) override;
    void compactMaterialPages() override;
    void setTileReplayFilter(const std::vector<std::uint32_t>& tileIndices) override;
    void clearTileReplayFilter() override;
    void beginFrame() override;
    void submitStrokeSegment(const StrokePacket& from, const StrokePacket& to, const ToolParams& params) override;
    void importSketchMaterial(const ImportedSketchMaterial& material) override;
    void endFrame() override;
    void setDebugView(DebugView view) override;
    void setPaperPreset(PaperPreset preset) override;
    void cleanTool(ToolKind tool) override;
    std::uint32_t width() const override;
    std::uint32_t height() const override;
    BackendStats stats() const override;
    std::unique_ptr<CudaGraphiteSnapshot> capture() const;
    void restore(const CudaGraphiteSnapshot& snapshot);

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint64_t packets_ = 0;
    float lastKernelMs_ = 0.0f;
    DebugView debugView_ = DebugView::DisplayTone;
    PaperPreset paperPreset_ = PaperPreset::ColdPress;
    std::uint32_t tileSize_ = 128;
    std::uint32_t tileColumns_ = 0;
    std::uint32_t tileRows_ = 0;
    std::uint32_t maxMaterialPages_ = 0;
    std::uint32_t materialPageCapacity_ = 0;
    std::uint32_t allocatedPageCount_ = 0;
    std::size_t materialStorageCells_ = 0;
    std::uint32_t activeTiles_ = 0;
    std::uint32_t allocatedTiles_ = 0;
    std::uint32_t lastTouchedTiles_ = 0;
    bool lastRenderUsedDirtyTiles_ = false;
    std::uint32_t lastRenderTiles_ = 0;
    std::uint32_t lastRenderPixels_ = 0;
    std::vector<std::uint8_t> tileActive_;
    std::vector<std::uint8_t> tileTouched_;
    std::vector<std::uint8_t> tileAllocated_;
    std::vector<std::uint32_t> touchedTileIndices_;
    std::vector<std::uint32_t> newlyAllocatedTileIndices_;
    std::vector<std::uint32_t> tilePageTable_;
    std::vector<std::uint8_t> tileReplayFilter_;
    bool tileReplayFilterEnabled_ = false;
    unsigned char* tileActiveDevice_ = nullptr;
    unsigned char* tileTouchedDevice_ = nullptr;
    unsigned char* tileAllocatedDevice_ = nullptr;
    std::uint32_t* touchedTileIndicesDevice_ = nullptr;
    std::uint32_t* newlyAllocatedTileIndicesDevice_ = nullptr;
    std::uint32_t* tilePageTableDevice_ = nullptr;
    bool forceFullRender_ = true;
    float* paperHeight_ = nullptr;
    float* paperRoughness_ = nullptr;
    float* looseGraphite_ = nullptr;
    float* boundGraphite_ = nullptr;
    float* compaction_ = nullptr;
    float* damage_ = nullptr;
    float* paperBinding_ = nullptr;
    float* damageSum_ = nullptr;
    float* bindingSum_ = nullptr;
    float* sheenSum_ = nullptr;
    float* roughnessSum_ = nullptr;
    float* looseGraphiteSum_ = nullptr;
    float* boundGraphiteSum_ = nullptr;
    float* toolLoads_ = nullptr;
    mutable float toolLoadsHost_[4]{};
    void* displayExternal_ = nullptr;
    void* d3d12FenceExternal_ = nullptr;
    void* displayMipmappedArray_ = nullptr;
    unsigned long long displaySurface_ = 0;
    std::uint64_t signalFenceValue_ = 1;
    std::uint64_t lastSignaledFenceValue_ = 0;

    bool ensureMaterialPageCapacity(std::uint32_t requiredPages);
    bool compactMaterialPagePool();
    void releaseMaterialBuffers();
};
