#include "engine_document.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

namespace
{
class FakeBackend final : public IGraphiteBackend
{
public:
    bool initialize(const GraphiteBackendInit&) override { return true; }
    void shutdown() override {}
    void clear() override { clearCount++; }
    void clearTiles(const std::vector<std::uint32_t>& tileIndices) override
    {
        clearTilesCount++;
        lastClearedTiles = tileIndices;
    }
    void compactMaterialPages() override { compactCount++; }
    void setTileReplayFilter(const std::vector<std::uint32_t>& tileIndices) override
    {
        setFilterCount++;
        lastFilter = tileIndices;
    }
    void clearTileReplayFilter() override { clearFilterCount++; }
    void beginFrame() override { beginCount++; }
    void submitStrokeSegment(const StrokePacket&, const StrokePacket&, const ToolParams&) override
    {
        submitCount++;
    }
    void importSketchMaterial(const ImportedSketchMaterial& material) override
    {
        importCount++;
        lastImport = material;
    }
    void endFrame() override { endCount++; }
    void setDebugView(DebugView) override {}
    void setPaperPreset(PaperPreset preset) override { paperPreset = preset; }
    void cleanTool(ToolKind tool) override { lastCleanedTool = tool; }
    std::uint32_t width() const override { return 960; }
    std::uint32_t height() const override { return 640; }
    BackendStats stats() const override
    {
        BackendStats s{};
        s.tileSize = 128;
        s.tileColumns = 8;
        s.tileRows = 5;
        s.paperPreset = paperPreset;
        return s;
    }

    void resetCallCounts()
    {
        clearCount = 0;
        clearTilesCount = 0;
        compactCount = 0;
        setFilterCount = 0;
        clearFilterCount = 0;
        beginCount = 0;
        submitCount = 0;
        importCount = 0;
        endCount = 0;
        lastClearedTiles.clear();
        lastFilter.clear();
        lastImport = {};
    }

    PaperPreset paperPreset = PaperPreset::ColdPress;
    int clearCount = 0;
    int clearTilesCount = 0;
    int compactCount = 0;
    int setFilterCount = 0;
    int clearFilterCount = 0;
    int beginCount = 0;
    int submitCount = 0;
    int importCount = 0;
    int endCount = 0;
    std::vector<std::uint32_t> lastClearedTiles;
    std::vector<std::uint32_t> lastFilter;
    ImportedSketchMaterial lastImport;
    ToolKind lastCleanedTool = ToolKind::Pencil;
};

StrokePacket packet(float x, float y)
{
    StrokePacket p{};
    p.x = x;
    p.y = y;
    p.pressure = 0.7f;
    p.isTip = true;
    return p;
}

void drawStroke(GraphiteDocument& document, float x0, float y0, float x1, float y1)
{
    document.beginStroke(packet(x0, y0));
    document.submitStrokePacket(packet(x1, y1));
    document.endStroke();
}

ImportedSketchMaterial sketchImport(float x, float y, float width, float height)
{
    ImportedSketchMaterial material{};
    material.width = 2;
    material.height = 2;
    material.targetX = x;
    material.targetY = y;
    material.targetWidth = width;
    material.targetHeight = height;
    material.graphite = {0.0f, 0.8f, 0.4f, 1.0f};
    return material;
}

void strokeUndoUsesDirtyReplay()
{
    FakeBackend backend;
    GraphiteDocument document(backend);
    drawStroke(document, 100.0f, 100.0f, 180.0f, 100.0f);
    drawStroke(document, 120.0f, 100.0f, 200.0f, 100.0f);
    backend.resetCallCounts();

    assert(document.undo());
    assert(backend.clearCount == 0);
    assert(backend.clearTilesCount == 1);
    assert(!backend.lastClearedTiles.empty());
    assert(backend.setFilterCount == 1);
    assert(backend.clearFilterCount == 1);
    assert(backend.submitCount == 1);
    assert(backend.beginCount == 1);
    assert(backend.endCount == 1);
    assert(backend.lastFilter == backend.lastClearedTiles);
}

void strokeRedoUsesFilteredReplay()
{
    FakeBackend backend;
    GraphiteDocument document(backend);
    drawStroke(document, 100.0f, 100.0f, 180.0f, 100.0f);
    drawStroke(document, 120.0f, 100.0f, 200.0f, 100.0f);
    assert(document.undo());
    backend.resetCallCounts();

    assert(document.redo());
    assert(backend.clearCount == 0);
    assert(backend.clearTilesCount == 0);
    assert(backend.setFilterCount == 1);
    assert(backend.clearFilterCount == 1);
    assert(backend.submitCount == 1);
    assert(backend.beginCount == 1);
    assert(backend.endCount == 1);
    assert(!backend.lastFilter.empty());
}

void nonOverlappingStrokeUndoDoesNotReplayUnrelatedStroke()
{
    FakeBackend backend;
    GraphiteDocument document(backend);
    drawStroke(document, 80.0f, 80.0f, 120.0f, 80.0f);
    drawStroke(document, 820.0f, 520.0f, 880.0f, 520.0f);
    backend.resetCallCounts();

    assert(document.undo());
    assert(backend.clearCount == 0);
    assert(backend.clearTilesCount == 1);
    assert(backend.setFilterCount == 1);
    assert(backend.clearFilterCount == 1);
    assert(backend.submitCount == 0);
    assert(backend.beginCount == 0);
    assert(backend.endCount == 0);
    assert(!backend.lastClearedTiles.empty());
    assert(backend.lastFilter == backend.lastClearedTiles);
}

void materialImportUndoUsesDirtyReplay()
{
    FakeBackend backend;
    GraphiteDocument document(backend);
    assert(document.importSketchMaterial(sketchImport(40.0f, 40.0f, 180.0f, 120.0f)));
    drawStroke(document, 80.0f, 80.0f, 120.0f, 80.0f);
    backend.resetCallCounts();

    assert(document.undo());
    assert(backend.clearCount == 0);
    assert(backend.clearTilesCount == 1);
    assert(backend.setFilterCount == 1);
    assert(backend.clearFilterCount == 1);
    assert(backend.importCount == 1);
    assert(backend.submitCount == 0);
    assert(!backend.lastClearedTiles.empty());
    assert(backend.lastFilter == backend.lastClearedTiles);
}

void materialImportRedoUsesFilteredReplay()
{
    FakeBackend backend;
    GraphiteDocument document(backend);
    assert(document.importSketchMaterial(sketchImport(40.0f, 40.0f, 180.0f, 120.0f)));
    assert(document.undo());
    backend.resetCallCounts();

    assert(document.redo());
    assert(backend.clearCount == 0);
    assert(backend.clearTilesCount == 0);
    assert(backend.setFilterCount == 1);
    assert(backend.clearFilterCount == 1);
    assert(backend.importCount == 1);
    assert(backend.lastImport.width == 2);
    assert(!backend.lastFilter.empty());
}
}

int main()
{
    strokeUndoUsesDirtyReplay();
    strokeRedoUsesFilteredReplay();
    nonOverlappingStrokeUndoDoesNotReplayUnrelatedStroke();
    materialImportUndoUsesDirtyReplay();
    materialImportRedoUsesFilteredReplay();
    return 0;
}
