#pragma once

#include "graphite_types.h"

#include <windows.h>

enum class DebugCommand
{
    None,
    Clear,
    Pencil4H,
    PencilHB,
    Pencil8B,
    RegularEraser,
    KneadedEraser,
    ElectricEraser,
    Tortillon,
    FanBrush,
    PowderBrush,
    GraphitePowder,
    CleanTool,
    KneadedShapeBlob,
    KneadedShapePoint,
    KneadedShapeEdge,
    KneadedShapeFlat,
    PaperSmooth,
    PaperColdPress,
    PaperRough,
    Undo,
    Redo,
    ViewDisplayTone,
    ViewLooseGraphite,
    ViewBoundGraphite,
    ViewPaperHeight,
    ViewCompaction,
    ViewDamage,
    ViewPaperBinding,
    ViewSurfaceSheen,
    ViewPaperRoughness,
};

struct DebugPanelState
{
    ToolKind tool = ToolKind::Pencil;
    PencilGrade grade = PencilGrade::HB;
    PaperPreset paperPreset = PaperPreset::ColdPress;
    InputSource source = InputSource::MouseFallback;
    float pressure = 0.0f;
    float speed = 0.0f;
    float rotation = 0.0f;
    bool hasRotation = false;
    float lastKernelMs = 0.0f;
    std::uint64_t packets = 0;
    std::uint32_t undoDepth = 0;
    std::uint32_t redoDepth = 0;
    std::uint32_t replayTrackedTiles = 0;
    DebugView debugView = DebugView::DisplayTone;
    std::uint32_t tileSize = 0;
    std::uint32_t tileColumns = 0;
    std::uint32_t tileRows = 0;
    std::uint32_t activeTiles = 0;
    std::uint32_t allocatedTiles = 0;
    std::uint32_t allocatedMaterialPages = 0;
    std::uint32_t materialPageCapacity = 0;
    std::uint32_t lastTouchedTiles = 0;
    bool lastRenderUsedDirtyTiles = false;
    std::uint32_t lastRenderTiles = 0;
    std::uint32_t lastRenderPixels = 0;
    bool diagnosticsRecording = false;
    std::uint64_t diagnosticsPackets = 0;
    std::uint64_t diagnosticsWmPointerPackets = 0;
    std::uint64_t diagnosticsMousePackets = 0;
    float diagnosticsMinPressure = 0.0f;
    float diagnosticsMaxPressure = 0.0f;
    bool diagnosticsSawTilt = false;
    bool diagnosticsSawRotation = false;
    bool diagnosticsSawEraser = false;
    bool diagnosticsSawBarrel = false;
    float tortillonLoad = 0.0f;
    float fanBrushLoad = 0.0f;
    float powderBrushLoad = 0.0f;
    float kneadedEraserLoad = 0.0f;
    float averageLooseGraphite = 0.0f;
    float averageBoundGraphite = 0.0f;
    float averageDamage = 0.0f;
    float averageBinding = 0.0f;
    float averageSheen = 0.0f;
    float averageRoughness = 0.0f;
};

class DebugPanel
{
public:
    bool create(HWND parent);
    void update(const DebugPanelState& state);
    DebugCommand commandFromControl(WPARAM wParam) const;

private:
    HWND panel_ = nullptr;
    HWND status_ = nullptr;
    HWND tool_ = nullptr;
    HWND input_ = nullptr;
    HWND backend_ = nullptr;
    HWND history_ = nullptr;
    HWND view_ = nullptr;
    HWND tiles_ = nullptr;
    HWND diagnostics_ = nullptr;
    HWND loads_ = nullptr;
    HWND damage_ = nullptr;
    HWND binding_ = nullptr;

    HWND addLabel(HWND parent, int x, int y, int width, int height, const wchar_t* text);
    HWND addButton(HWND parent, int id, int x, int y, int width, int height, const wchar_t* text);
};
