#pragma once

#include "graphite_types.h"
#include "igraphite_backend.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

enum class GraphiteEventKind : std::uint32_t
{
    Clear = 0,
    Stroke = 1,
    MaterialImport = 2,
};

struct GraphiteEvent
{
    GraphiteEventKind kind = GraphiteEventKind::Stroke;
    ToolParams params;
    PaperPreset paperPreset = PaperPreset::ColdPress;
    std::vector<StrokePacket> packets;
    ImportedSketchMaterial importedSketch;
    std::vector<std::uint32_t> touchedTiles;
};

struct DocumentHistoryStats
{
    std::uint32_t undoDepth = 0;
    std::uint32_t redoDepth = 0;
    std::uint32_t eventCount = 0;
    std::uint32_t currentStrokePackets = 0;
    std::uint32_t currentStrokeTiles = 0;
    std::uint32_t replayTrackedTiles = 0;
};

class GraphiteDocument
{
public:
    explicit GraphiteDocument(IGraphiteBackend& backend);

    ToolParams& toolParams();
    const ToolParams& toolParams() const;
    PaperPreset paperPreset() const;
    void setPaperPreset(PaperPreset preset);

    DebugView debugView() const;
    void setDebugView(DebugView view);

    const StrokePacket& lastPacket() const;
    void setLastPacket(const StrokePacket& packet);

    bool drawing() const;
    bool strokeChanged() const;

    void beginStroke(const StrokePacket& first);
    void submitStrokePacket(const StrokePacket& packet);
    void submitStrokePackets(const std::vector<StrokePacket>& packets);
    void endStroke();
    void cancelStroke();

    void clear();
    bool importSketchMaterial(const ImportedSketchMaterial& material);
    void cleanCurrentTool();
    bool undo();
    bool redo();

    DocumentHistoryStats historyStats() const;

private:
    void commitCurrentStroke();
    void replayEvents();
    void replayDirtyTiles(const std::vector<std::uint32_t>& tileIndices);
    void replayStrokeEvent(const GraphiteEvent& event);
    void replayMaterialImportEvent(const GraphiteEvent& event);
    void appendSegmentTiles(GraphiteEvent& event, const StrokePacket& from, const StrokePacket& to, const ToolParams& params);
    void appendMaterialImportTiles(GraphiteEvent& event, const ImportedSketchMaterial& material);

    IGraphiteBackend& backend_;
    std::uint32_t tileSize_ = 0;
    std::uint32_t tileColumns_ = 0;
    std::uint32_t tileRows_ = 0;
    ToolParams params_;
    PaperPreset paperPreset_ = PaperPreset::ColdPress;
    DebugView debugView_ = DebugView::DisplayTone;
    StrokePacket lastPacket_;
    bool drawing_ = false;
    bool strokeChanged_ = false;
    GraphiteEvent currentStroke_;
    std::vector<GraphiteEvent> eventLog_;
    std::vector<GraphiteEvent> redoEvents_;
};
