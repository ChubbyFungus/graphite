#pragma once

#include <cstdint>
#include <vector>

enum class ToolKind : std::uint32_t
{
    Pencil = 0,
    RegularEraser = 1,
    KneadedEraser = 2,
    ElectricEraser = 3,
    Tortillon = 4,
    FanBrush = 5,
    PowderBrush = 6,
    GraphitePowder = 7,
};

enum class PencilGrade : std::uint32_t
{
    FourH = 0,
    ThreeH = 1,
    TwoH = 2,
    H = 3,
    HB = 4,
    B = 5,
    TwoB = 6,
    FourB = 7,
    SixB = 8,
    EightB = 9,
};

enum class KneadedEraserShape : std::uint32_t
{
    Blob = 0,
    Point = 1,
    Edge = 2,
    Flat = 3,
};

enum class InputSource : std::uint32_t
{
    MouseFallback = 0,
    WmPointer = 1,
    WinTab = 2,
    RealTimeStylus = 3,
};

enum class TabletInputMode : std::uint32_t
{
    Auto = 0,
    WindowsInkRealTimeStylus = 1,
    WindowsInkRawPointer = 2,
    WinTab = 3,
    MouseOnly = 4,
};

enum class DebugView : std::uint32_t
{
    DisplayTone = 0,
    LooseGraphite = 1,
    BoundGraphite = 2,
    PaperHeight = 3,
    Compaction = 4,
    Damage = 5,
    PaperBinding = 6,
    SurfaceSheen = 7,
    PaperRoughness = 8,
};

enum class PaperPreset : std::uint32_t
{
    SmoothBristol = 0,
    ColdPress = 1,
    RoughSketch = 2,
};

struct StrokePacket
{
    float x = 0.0f;
    float y = 0.0f;
    float rawScreenX = 0.0f;
    float rawScreenY = 0.0f;
    float pressure = 0.0f;
    float tiltX = 0.0f;
    float tiltY = 0.0f;
    float orientation = 0.0f;
    float rotation = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float speed = 0.0f;
    // Arc length in canvas px from the start of the current stroke to this
    // packet. Set by GraphiteDocument; lets material kernels distinguish true
    // stroke boundaries from interior segment joints (stroke-start taper).
    float strokeDistancePx = 0.0f;
    std::uint64_t timestampUs = 0;
    bool isTip = false;
    bool isEraser = false;
    bool barrelButton = false;
    bool hasPressure = false;
    bool hasRotation = false;
    InputSource source = InputSource::MouseFallback;
    std::uint32_t rawPressure = 0;
    std::uint32_t rawPressureMax = 0;
    std::uint32_t pointerType = 0;
    std::uint32_t pointerFlags = 0;
    std::uint32_t penMask = 0;
    std::uint32_t penFlags = 0;
    bool penInfoAvailable = false;
};

struct GraphiteBackendInit
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    void* d3d12SharedDisplayHandle = nullptr;
    std::uint64_t d3d12SharedDisplayBytes = 0;
    void* d3d12CudaFenceHandle = nullptr;
};

struct ToolParams
{
    ToolKind tool = ToolKind::Pencil;
    PencilGrade grade = PencilGrade::HB;
    KneadedEraserShape kneadedShape = KneadedEraserShape::Blob;
    float radiusPx = 3.0f;
};

struct ImportedSketchMaterial
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetWidth = 0.0f;
    float targetHeight = 0.0f;
    std::vector<float> graphite;
};

struct BackendStats
{
    float lastKernelMs = 0.0f;
    std::uint64_t strokePackets = 0;
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
    std::uint64_t lastSignaledCudaFenceValue = 0;
    PaperPreset paperPreset = PaperPreset::ColdPress;
};
