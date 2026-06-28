#pragma once

#include "graphite_types.h"

#include <cstdint>
#include <vector>

class IGraphiteBackend
{
public:
    virtual ~IGraphiteBackend() = default;

    virtual bool initialize(const GraphiteBackendInit& init) = 0;
    virtual void shutdown() = 0;
    virtual void clear() = 0;
    virtual void clearTiles(const std::vector<std::uint32_t>& tileIndices) = 0;
    virtual void compactMaterialPages() = 0;
    virtual void setTileReplayFilter(const std::vector<std::uint32_t>& tileIndices) = 0;
    virtual void clearTileReplayFilter() = 0;
    virtual void beginFrame() = 0;
    virtual void submitStrokeSegment(const StrokePacket& from, const StrokePacket& to, const ToolParams& params) = 0;
    virtual void importSketchMaterial(const ImportedSketchMaterial& material) = 0;
    virtual void endFrame() = 0;
    virtual void setDebugView(DebugView view) = 0;
    virtual void setPaperPreset(PaperPreset preset) = 0;
    virtual void cleanTool(ToolKind tool) = 0;
    virtual std::uint32_t width() const = 0;
    virtual std::uint32_t height() const = 0;
    virtual BackendStats stats() const = 0;
};
