#include "engine_document.h"

#include <cmath>

namespace
{
float effectiveToolRadiusPx(const ToolParams& params)
{
    const float pencilRadius = std::max(0.45f, params.radiusPx);
    if (params.tool == ToolKind::RegularEraser) return std::max(pencilRadius * 7.0f, 7.0f);
    if (params.tool == ToolKind::KneadedEraser)
    {
        if (params.kneadedShape == KneadedEraserShape::Point) return std::max(pencilRadius * 2.4f, 2.4f);
        if (params.kneadedShape == KneadedEraserShape::Edge) return std::max(pencilRadius * 5.2f, 5.2f);
        if (params.kneadedShape == KneadedEraserShape::Flat) return std::max(pencilRadius * 9.0f, 9.0f);
        return std::max(pencilRadius * 6.5f, 6.5f);
    }
    if (params.tool == ToolKind::ElectricEraser) return std::max(pencilRadius * 3.0f, 3.0f);
    if (params.tool == ToolKind::Tortillon) return std::max(pencilRadius * 7.0f, 7.0f);
    if (params.tool == ToolKind::FanBrush) return std::max(pencilRadius * 40.0f, 24.0f);
    if (params.tool == ToolKind::PowderBrush) return std::max(pencilRadius * 34.0f, 22.0f);
    if (params.tool == ToolKind::GraphitePowder) return std::max(pencilRadius * 36.0f, 24.0f);
    return pencilRadius;
}
}

GraphiteDocument::GraphiteDocument(IGraphiteBackend& backend)
    : backend_(backend)
{
    const auto stats = backend_.stats();
    tileSize_ = stats.tileSize;
    tileColumns_ = stats.tileColumns;
    tileRows_ = stats.tileRows;
    params_.tool = ToolKind::Pencil;
    params_.grade = PencilGrade::HB;
    params_.radiusPx = 1.1f;
    backend_.setPaperPreset(paperPreset_);
}

ToolParams& GraphiteDocument::toolParams()
{
    return params_;
}

const ToolParams& GraphiteDocument::toolParams() const
{
    return params_;
}

PaperPreset GraphiteDocument::paperPreset() const
{
    return paperPreset_;
}

void GraphiteDocument::setPaperPreset(PaperPreset preset)
{
    paperPreset_ = preset;
    backend_.setPaperPreset(preset);
}

DebugView GraphiteDocument::debugView() const
{
    return debugView_;
}

void GraphiteDocument::setDebugView(DebugView view)
{
    debugView_ = view;
    backend_.setDebugView(view);
}

const StrokePacket& GraphiteDocument::lastPacket() const
{
    return lastPacket_;
}

void GraphiteDocument::setLastPacket(const StrokePacket& packet)
{
    lastPacket_ = packet;
}

bool GraphiteDocument::drawing() const
{
    return drawing_;
}

bool GraphiteDocument::strokeChanged() const
{
    return strokeChanged_;
}

void GraphiteDocument::beginStroke(const StrokePacket& first)
{
    drawing_ = true;
    strokeChanged_ = false;
    lastPacket_ = first;
    lastPacket_.strokeDistancePx = 0.0f;
    currentStroke_ = GraphiteEvent{};
    currentStroke_.kind = GraphiteEventKind::Stroke;
    currentStroke_.params = params_;
    currentStroke_.paperPreset = paperPreset_;
    currentStroke_.packets.clear();
    currentStroke_.packets.push_back(lastPacket_);
    if (params_.tool == ToolKind::KneadedEraser)
    {
        StrokePacket dab = lastPacket_;
        dab.x += 0.08f;
        dab.y += 0.02f;
        dab.strokeDistancePx = lastPacket_.strokeDistancePx + std::hypot(0.08f, 0.02f);
        appendSegmentTiles(currentStroke_, lastPacket_, dab, params_);
        backend_.beginFrame();
        backend_.submitStrokeSegment(lastPacket_, dab, params_);
        backend_.endFrame();
        lastPacket_ = dab;
        strokeChanged_ = true;
        currentStroke_.packets.push_back(dab);
    }
}

void GraphiteDocument::submitStrokePacket(const StrokePacket& packet)
{
    if (!drawing_) return;
    StrokePacket next = packet;
    next.strokeDistancePx = lastPacket_.strokeDistancePx + std::hypot(packet.x - lastPacket_.x, packet.y - lastPacket_.y);
    appendSegmentTiles(currentStroke_, lastPacket_, next, params_);
    backend_.beginFrame();
    backend_.submitStrokeSegment(lastPacket_, next, params_);
    backend_.endFrame();
    lastPacket_ = next;
    strokeChanged_ = true;
    currentStroke_.packets.push_back(next);
}

void GraphiteDocument::submitStrokePackets(const std::vector<StrokePacket>& packets)
{
    if (!drawing_ || packets.empty()) return;
    backend_.beginFrame();
    for (const auto& packet : packets)
    {
        StrokePacket next = packet;
        next.strokeDistancePx = lastPacket_.strokeDistancePx + std::hypot(packet.x - lastPacket_.x, packet.y - lastPacket_.y);
        appendSegmentTiles(currentStroke_, lastPacket_, next, params_);
        backend_.submitStrokeSegment(lastPacket_, next, params_);
        lastPacket_ = next;
        strokeChanged_ = true;
        currentStroke_.packets.push_back(next);
    }
    backend_.endFrame();
}

void GraphiteDocument::endStroke()
{
    if (!drawing_) return;
    if (strokeChanged_)
    {
        commitCurrentStroke();
    }
    drawing_ = false;
    strokeChanged_ = false;
    currentStroke_ = GraphiteEvent{};
}

void GraphiteDocument::cancelStroke()
{
    endStroke();
}

void GraphiteDocument::clear()
{
    backend_.clear();

    GraphiteEvent event{};
    event.kind = GraphiteEventKind::Clear;
    event.params = params_;
    event.paperPreset = paperPreset_;
    eventLog_.push_back(std::move(event));
    redoEvents_.clear();
}

bool GraphiteDocument::importSketchMaterial(const ImportedSketchMaterial& material)
{
    if (material.width == 0 || material.height == 0 || material.graphite.empty()) return false;
    if (material.graphite.size() != static_cast<std::size_t>(material.width) * material.height) return false;
    if (material.targetWidth <= 0.0f || material.targetHeight <= 0.0f) return false;

    GraphiteEvent event{};
    event.kind = GraphiteEventKind::MaterialImport;
    event.params = params_;
    event.paperPreset = paperPreset_;
    event.importedSketch = material;
    appendMaterialImportTiles(event, event.importedSketch);
    if (event.touchedTiles.empty()) return false;

    backend_.importSketchMaterial(event.importedSketch);
    eventLog_.push_back(std::move(event));
    redoEvents_.clear();
    return true;
}

void GraphiteDocument::cleanCurrentTool()
{
    backend_.cleanTool(params_.tool);
}

bool GraphiteDocument::undo()
{
    if (eventLog_.empty()) return false;
    const GraphiteEvent undone = eventLog_.back();
    redoEvents_.push_back(std::move(eventLog_.back()));
    eventLog_.pop_back();
    if ((undone.kind == GraphiteEventKind::Stroke || undone.kind == GraphiteEventKind::MaterialImport) && !undone.touchedTiles.empty())
    {
        replayDirtyTiles(undone.touchedTiles);
    }
    else
    {
        replayEvents();
    }
    return true;
}

bool GraphiteDocument::redo()
{
    if (!redoEvents_.empty())
    {
        const GraphiteEvent redone = redoEvents_.back();
        eventLog_.push_back(std::move(redoEvents_.back()));
        redoEvents_.pop_back();
        if ((redone.kind == GraphiteEventKind::Stroke || redone.kind == GraphiteEventKind::MaterialImport) && !redone.touchedTiles.empty())
        {
            backend_.setTileReplayFilter(redone.touchedTiles);
            if (redone.kind == GraphiteEventKind::Stroke) replayStrokeEvent(redone);
            else replayMaterialImportEvent(redone);
            backend_.clearTileReplayFilter();
        }
        else
        {
            replayEvents();
        }
        return true;
    }
    return false;
}

DocumentHistoryStats GraphiteDocument::historyStats() const
{
    std::uint32_t replayTrackedTiles = 0;
    for (const auto& event : eventLog_)
    {
        replayTrackedTiles += static_cast<std::uint32_t>(event.touchedTiles.size());
    }
    return DocumentHistoryStats{
        static_cast<std::uint32_t>(eventLog_.size()),
        static_cast<std::uint32_t>(redoEvents_.size()),
        static_cast<std::uint32_t>(eventLog_.size()),
        static_cast<std::uint32_t>(currentStroke_.packets.size()),
        static_cast<std::uint32_t>(currentStroke_.touchedTiles.size()),
        replayTrackedTiles,
    };
}

void GraphiteDocument::commitCurrentStroke()
{
    eventLog_.push_back(std::move(currentStroke_));
    redoEvents_.clear();
}

void GraphiteDocument::replayEvents()
{
    PaperPreset activePreset = PaperPreset::ColdPress;
    backend_.setPaperPreset(activePreset);
    backend_.clear();
    for (const auto& event : eventLog_)
    {
        if (event.kind == GraphiteEventKind::Clear)
        {
            activePreset = event.paperPreset;
            backend_.setPaperPreset(activePreset);
            backend_.clear();
            continue;
        }
        if (event.kind == GraphiteEventKind::Stroke) replayStrokeEvent(event);
        else if (event.kind == GraphiteEventKind::MaterialImport) replayMaterialImportEvent(event);
    }
    paperPreset_ = activePreset;
    backend_.compactMaterialPages();
}

void GraphiteDocument::replayDirtyTiles(const std::vector<std::uint32_t>& tileIndices)
{
    if (tileIndices.empty())
    {
        replayEvents();
        return;
    }

    PaperPreset activePreset = PaperPreset::ColdPress;
    std::size_t replayStart = 0;
    for (std::size_t i = 0; i < eventLog_.size(); ++i)
    {
        if (eventLog_[i].kind == GraphiteEventKind::Clear)
        {
            activePreset = eventLog_[i].paperPreset;
            replayStart = i + 1;
        }
    }
    paperPreset_ = activePreset;
    backend_.setPaperPreset(activePreset);
    backend_.clearTiles(tileIndices);
    backend_.setTileReplayFilter(tileIndices);
    for (std::size_t i = replayStart; i < eventLog_.size(); ++i)
    {
        const auto& event = eventLog_[i];
        if (event.kind != GraphiteEventKind::Stroke && event.kind != GraphiteEventKind::MaterialImport) continue;
        bool intersects = false;
        for (std::uint32_t tileIndex : event.touchedTiles)
        {
            if (std::find(tileIndices.begin(), tileIndices.end(), tileIndex) != tileIndices.end())
            {
                intersects = true;
                break;
            }
        }
        if (!intersects) continue;
        if (event.kind == GraphiteEventKind::Stroke) replayStrokeEvent(event);
        else replayMaterialImportEvent(event);
    }
    backend_.clearTileReplayFilter();
}

void GraphiteDocument::replayStrokeEvent(const GraphiteEvent& event)
{
    if (event.packets.size() < 2) return;
    backend_.beginFrame();
    for (std::size_t i = 1; i < event.packets.size(); ++i)
    {
        backend_.submitStrokeSegment(event.packets[i - 1], event.packets[i], event.params);
    }
    backend_.endFrame();
}

void GraphiteDocument::replayMaterialImportEvent(const GraphiteEvent& event)
{
    backend_.importSketchMaterial(event.importedSketch);
}

void GraphiteDocument::appendSegmentTiles(GraphiteEvent& event, const StrokePacket& from, const StrokePacket& to, const ToolParams& params)
{
    if (tileSize_ == 0 || tileColumns_ == 0 || tileRows_ == 0) return;

    const float radius = std::max(1.0f, effectiveToolRadiusPx(params) * 2.0f);
    const int minX = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x) - radius)));
    const int maxX = std::min(static_cast<int>(backend_.width()) - 1, static_cast<int>(std::ceil(std::max(from.x, to.x) + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min(from.y, to.y) - radius)));
    const int maxY = std::min(static_cast<int>(backend_.height()) - 1, static_cast<int>(std::ceil(std::max(from.y, to.y) + radius)));
    const std::uint32_t minTileX = static_cast<std::uint32_t>(minX) / tileSize_;
    const std::uint32_t maxTileX = static_cast<std::uint32_t>(maxX) / tileSize_;
    const std::uint32_t minTileY = static_cast<std::uint32_t>(minY) / tileSize_;
    const std::uint32_t maxTileY = static_cast<std::uint32_t>(maxY) / tileSize_;

    for (std::uint32_t tileY = minTileY; tileY <= maxTileY && tileY < tileRows_; ++tileY)
    {
        for (std::uint32_t tileX = minTileX; tileX <= maxTileX && tileX < tileColumns_; ++tileX)
        {
            const std::uint32_t tileIndex = tileY * tileColumns_ + tileX;
            if (std::find(event.touchedTiles.begin(), event.touchedTiles.end(), tileIndex) == event.touchedTiles.end())
            {
                event.touchedTiles.push_back(tileIndex);
            }
        }
    }
}

void GraphiteDocument::appendMaterialImportTiles(GraphiteEvent& event, const ImportedSketchMaterial& material)
{
    if (tileSize_ == 0 || tileColumns_ == 0 || tileRows_ == 0) return;
    if (material.targetWidth <= 0.0f || material.targetHeight <= 0.0f) return;

    const int minX = std::max(0, static_cast<int>(std::floor(material.targetX)));
    const int maxX = std::min(static_cast<int>(backend_.width()) - 1, static_cast<int>(std::ceil(material.targetX + material.targetWidth)));
    const int minY = std::max(0, static_cast<int>(std::floor(material.targetY)));
    const int maxY = std::min(static_cast<int>(backend_.height()) - 1, static_cast<int>(std::ceil(material.targetY + material.targetHeight)));
    if (maxX < minX || maxY < minY) return;

    const std::uint32_t minTileX = static_cast<std::uint32_t>(minX) / tileSize_;
    const std::uint32_t maxTileX = static_cast<std::uint32_t>(maxX) / tileSize_;
    const std::uint32_t minTileY = static_cast<std::uint32_t>(minY) / tileSize_;
    const std::uint32_t maxTileY = static_cast<std::uint32_t>(maxY) / tileSize_;

    for (std::uint32_t tileY = minTileY; tileY <= maxTileY && tileY < tileRows_; ++tileY)
    {
        for (std::uint32_t tileX = minTileX; tileX <= maxTileX && tileX < tileColumns_; ++tileX)
        {
            const std::uint32_t tileIndex = tileY * tileColumns_ + tileX;
            if (std::find(event.touchedTiles.begin(), event.touchedTiles.end(), tileIndex) == event.touchedTiles.end())
            {
                event.touchedTiles.push_back(tileIndex);
            }
        }
    }
}
