#include "cuda_graphite_backend.h"
#include "debug_panel.h"
#include "d3d12_renderer.h"
#include "engine_document.h"
#include "input_adapter.h"
#include "pen_input_diagnostics.h"
#include "realtime_stylus_adapter.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include <commdlg.h>
#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <deque>
#include <memory>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;

namespace
{
constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr float kFallbackDisplayPixelsPerMm = 141.0f / 25.4f;
constexpr float kSharpenedPencilContactRadiusMm = 0.20f;
constexpr float kBluntPencilContactRadiusMm = 0.72f;
constexpr UINT_PTR kAnimationTimerId = 1;
constexpr UINT kAnimationTimerMs = 16;

struct AppState
{
    CudaGraphiteBackend backend;
    std::unique_ptr<GraphiteDocument> document;
    DebugPanel debugPanel;
    D3D12Renderer renderer;
    InputAdapter input;
    RealTimeStylusAdapter realTimeStylus;
    PenInputDiagnostics diagnostics;
    TabletInputMode tabletMode = TabletInputMode::WindowsInkRawPointer;
    std::uint32_t canvasWidth = 960;
    std::uint32_t canvasHeight = 640;
    std::uint32_t clientWidth = 960;
    std::uint32_t clientHeight = 640;
    float displayPixelsPerMm = kFallbackDisplayPixelsPerMm;
    std::deque<StrokePacket> visibleInputTrace;
    bool showInputTrace = false;
    bool showGridOverlay = false;
    float gridOpacity = 0.28f;
    float gridSpacingPx = 32.0f;
    bool referenceImageVisible = false;
    float referenceImageOpacity = 0.45f;
    bool pendingReferenceImageDialog = false;
    bool pendingSketchImportDialog = false;
    bool referenceImageLoadFailed = false;
    bool sketchImportFailed = false;
    bool sketchImportSucceeded = false;
    std::wstring referenceImagePath;
    std::wstring sketchImportPath;
    bool trayDragging = false;
    bool hiddenTabDragging = false;
    bool hiddenTabMoved = false;
    UINT32 trayDragPointerId = 0;
    POINT trayDragLastPoint{};
    POINT lastClientPoint{};
    bool hasLastClientPoint = false;
    UINT32 activePointerId = 0;
    bool pendingDebugViewChange = false;
    DebugView pendingDebugView = DebugView::DisplayTone;
    bool pendingPaperPresetChange = false;
    PaperPreset pendingPaperPreset = PaperPreset::ColdPress;
    bool paperPickerOpen = true;
    bool toolOptionsCollapsed = false;
    ImVec2 trayPosition = ImVec2(18.0f, 18.0f);
    float trayRoll = 0.0f;
    float trayRollTarget = 0.0f;
    float trayHide = 0.0f;
    float trayHideTarget = 0.0f;
    ImVec2 hiddenTabCenter = ImVec2(0.0f, 0.0f);
    int hiddenTabEdge = 1;
    DebugCommand activeTrayCommand = DebugCommand::None;
    int activeTrayPencilIndex = -1;
    float activeTraySourceCenterX = 0.0f;
    float activeTraySourceCenterY = 0.0f;
    float activeTraySourceMaxWidth = 0.0f;
    float activeTraySourceMaxHeight = 0.0f;
    float activeTrayLift = 1.0f;
    DebugCommand returningTrayCommand = DebugCommand::None;
    int returningTrayPencilIndex = -1;
    float returningTraySourceCenterX = 0.0f;
    float returningTraySourceCenterY = 0.0f;
    float returningTraySourceMaxWidth = 0.0f;
    float returningTraySourceMaxHeight = 0.0f;
    float returningTrayT = 1.0f;
    UiTexture pencil4H;
    UiTexture pencil3H;
    UiTexture pencil2H;
    UiTexture pencilH;
    UiTexture pencilHB;
    UiTexture pencilB;
    UiTexture pencil2B;
    UiTexture pencil4B;
    UiTexture pencil6B;
    UiTexture pencil8B;
    UiTexture trayBackground;
    UiTexture vinylEraser;
    UiTexture kneadedEraser;
    UiTexture electricEraser;
    UiTexture tortillon;
    UiTexture fanBrush;
    UiTexture powderBrush;
    UiTexture graphitePowder;
    UiTexture pencilStrap;
    UiTexture rolledLeatherOutside;
    UiTexture tortillonStrapLeft;
    UiTexture tortillonStrapRight;
    UiTexture vinylEraserStrap;
    UiTexture electricEraserStrap;
    UiTexture fanBrushStrap;
    UiTexture powderBrushStrap;
    UiTexture leatherSideTab;
    UiTexture leatherSideTabLeft;
    UiTexture leatherSideTabTop;
    UiTexture leatherSideTabBottom;
    UiTexture leatherPencilPocket;
    UiTexture leatherEraserPocket;
    UiTexture leatherLongToolPocket;
    UiTexture imagegenToolOptionsPanel;
    UiTexture imagegenPaperPickerPanel;
    UiTexture imagegenOptionTag;
    UiTexture whitePixel;
    UiTexture referenceImage;
};

struct StudioLayout
{
    ImVec2 trayPos;
    ImVec2 traySize;
    ImVec2 paperMin;
    ImVec2 paperMax;
    float trayScale = 1.0f;
};

struct TrayRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ScreenRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct TrayTextureSlot
{
    float centerX = 0.0f;
    float centerY = 0.0f;
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;
};

struct DecodedImagePixels
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

constexpr const char* kDiagnosticsPath = "pen_input_diagnostics.csv";
constexpr const char* kRealTimeStylusStatusPath = "realtime_stylus_status.txt";
constexpr float kTrayDesignWidth = 1280.0f;
constexpr float kTrayDesignHeight = 520.0f;
constexpr float kTrayMargin = 12.0f;
constexpr float kPaperMargin = 28.0f;
constexpr float kPaperGap = 32.0f;
constexpr float kMinimumTrayScale = 0.24f;
constexpr float kMaximumTrayScale = 0.48f;
constexpr TrayTextureSlot kPencilSlots[] = {
    {647.0f, 230.0f, 24.0f, 270.0f},
    {698.0f, 230.0f, 24.0f, 270.0f},
    {746.0f, 230.0f, 24.0f, 270.0f},
    {796.0f, 230.0f, 24.0f, 270.0f},
    {844.0f, 230.0f, 24.0f, 270.0f},
    {896.0f, 230.0f, 24.0f, 270.0f},
    {945.0f, 230.0f, 24.0f, 270.0f},
    {997.0f, 230.0f, 24.0f, 270.0f},
    {1047.0f, 230.0f, 24.0f, 270.0f},
    {1100.0f, 230.0f, 24.0f, 270.0f},
};
constexpr TrayTextureSlot kTortillonSlot{580.0f, 230.0f, 48.0f, 300.0f};
constexpr TrayTextureSlot kVinylEraserSlot{222.0f, 110.0f, 96.0f, 96.0f};
constexpr TrayTextureSlot kKneadedEraserSlot{219.0f, 299.0f, 104.0f, 104.0f};
constexpr TrayTextureSlot kElectricEraserSlot{350.0f, 228.0f, 58.0f, 260.0f};
constexpr TrayTextureSlot kFanBrushSlot{496.0f, 253.0f, 64.0f, 280.0f};
constexpr TrayTextureSlot kPowderBrushSlot{433.0f, 234.0f, 64.0f, 280.0f};
constexpr TrayTextureSlot kGraphitePowderSlot{124.0f, 306.0f, 156.0f, 112.0f};
constexpr TrayRect kTortillonHitRect{546.0f, 70.0f, 68.0f, 330.0f};
constexpr TrayRect kVinylEraserHitRect{159.0f, 62.0f, 125.0f, 125.0f};
constexpr TrayRect kKneadedEraserHitRect{154.0f, 235.0f, 130.0f, 130.0f};
constexpr TrayRect kElectricEraserHitRect{305.0f, 70.0f, 90.0f, 330.0f};
constexpr TrayRect kFanBrushHitRect{465.0f, 90.0f, 70.0f, 330.0f};
constexpr TrayRect kPowderBrushHitRect{399.0f, 80.0f, 70.0f, 330.0f};
constexpr TrayRect kGraphitePowderHitRect{48.0f, 238.0f, 152.0f, 128.0f};
constexpr TrayRect kTrayCloseToggleRect{0.0f, 16.0f, 112.0f, 82.0f};
constexpr TrayRect kPaperSmoothRect{42.0f, 420.0f, 82.0f, 42.0f};
constexpr TrayRect kPaperColdRect{134.0f, 420.0f, 82.0f, 42.0f};
constexpr TrayRect kPaperRoughRect{226.0f, 420.0f, 82.0f, 42.0f};
constexpr TrayRect kKneadedBlobRect{198.0f, 254.0f, 42.0f, 36.0f};
constexpr TrayRect kKneadedPointRect{246.0f, 254.0f, 42.0f, 36.0f};
constexpr TrayRect kKneadedEdgeRect{198.0f, 294.0f, 42.0f, 36.0f};
constexpr TrayRect kKneadedFlatRect{246.0f, 294.0f, 42.0f, 36.0f};
constexpr float kHiddenTrayRecallWidth = 56.0f;
constexpr float kHiddenTrayRecallHeight = 44.0f;
constexpr float kHiddenTrayTabWidth = 123.0f;
constexpr float kHiddenTrayTabHeight = 44.0f;
constexpr float kHiddenTrayTabEdgeInset = 0.42f;
constexpr float kHiddenTrayDragThreshold = 4.0f;
constexpr float kOptionsPanelWidth = 312.0f;
constexpr float kOptionsPanelHeight = 252.0f;
constexpr float kOptionsPanelMargin = 18.0f;
constexpr float kOptionsTabWidth = 122.0f;
constexpr float kOptionsTabHeight = 44.0f;
constexpr float kPaperPickerHeight = 174.0f;
constexpr float kCanvasLayersPanelWidth = 286.0f;
constexpr float kCanvasLayersPanelHeight = 306.0f;
constexpr float kCanvasLayersPanelTop = 286.0f;
constexpr float kCanvasLayerMinGridSpacingPx = 12.0f;
constexpr float kCanvasLayerMaxGridSpacingPx = 128.0f;

enum HiddenTabEdge
{
    HiddenTabLeft = 0,
    HiddenTabRight = 1,
    HiddenTabTop = 2,
    HiddenTabBottom = 3,
};

AppState* state(HWND hwnd)
{
    return reinterpret_cast<AppState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

std::wstring parentDirectory(std::wstring path)
{
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
    {
        path.pop_back();
    }

    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
    {
        return {};
    }
    return path.substr(0, slash);
}

bool hasEngineSliceUiAssets(const std::wstring& directory)
{
    std::wstring probe = directory;
    if (!probe.empty() && probe.back() != L'\\' && probe.back() != L'/')
    {
        probe += L'\\';
    }
    probe += L"native\\Graphite.EngineSlice\\assets\\ui\\imagegen-roll-case-base-no-pocket-fronts.png";

    const DWORD attributes = GetFileAttributesW(probe.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void useRepoRootForUiAssets()
{
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return;
    }

    std::wstring search = parentDirectory(std::wstring(modulePath, length));
    for (int depth = 0; depth < 8 && !search.empty(); ++depth)
    {
        if (hasEngineSliceUiAssets(search))
        {
            SetCurrentDirectoryW(search.c_str());
            return;
        }
        search = parentDirectory(search);
    }
}

std::wstring chooseImagePath(HWND hwnd, const wchar_t* title)
{
    std::vector<wchar_t> fileName(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrFilter = L"Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0All files\0*.*\0";
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog))
    {
        return {};
    }
    return std::wstring(fileName.data());
}

bool decodeImagePixels(const std::wstring& path, DecodedImagePixels& image)
{
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;

    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width) * height * 4u, 0);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4u, static_cast<UINT>(image.rgba.size()), image.rgba.data()));
}

float luminanceFromRgba(const std::uint8_t* pixel)
{
    const float r = static_cast<float>(pixel[0]) / 255.0f;
    const float g = static_cast<float>(pixel[1]) / 255.0f;
    const float b = static_cast<float>(pixel[2]) / 255.0f;
    const float a = static_cast<float>(pixel[3]) / 255.0f;
    const float sourceLuminance = r * 0.2126f + g * 0.7152f + b * 0.0722f;
    return 1.0f - a + sourceLuminance * a;
}

float percentile(std::vector<float> values, float fraction)
{
    if (values.empty()) return 1.0f;
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    const std::size_t index = std::min(values.size() - 1, static_cast<std::size_t>(clamped * static_cast<float>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

ImportedSketchMaterial sketchMaterialFromImage(const DecodedImagePixels& image, std::uint32_t canvasWidth, std::uint32_t canvasHeight)
{
    ImportedSketchMaterial material{};
    material.width = image.width;
    material.height = image.height;
    material.graphite.assign(static_cast<std::size_t>(image.width) * image.height, 0.0f);
    if (image.width == 0 || image.height == 0 || image.rgba.size() < material.graphite.size() * 4u) return material;

    std::vector<float> opaqueLuminance;
    opaqueLuminance.reserve(material.graphite.size());
    for (std::size_t i = 0; i < material.graphite.size(); ++i)
    {
        const std::uint8_t* pixel = image.rgba.data() + i * 4u;
        if (pixel[3] > 12) opaqueLuminance.push_back(luminanceFromRgba(pixel));
    }
    const float whitePoint = std::max(0.18f, percentile(opaqueLuminance, 0.96f));
    const float darkPoint = std::min(whitePoint - 0.08f, percentile(opaqueLuminance, 0.04f));
    const float range = std::max(0.18f, whitePoint - darkPoint);

    for (std::size_t i = 0; i < material.graphite.size(); ++i)
    {
        const float lum = luminanceFromRgba(image.rgba.data() + i * 4u);
        float graphite = std::clamp((whitePoint - lum) / range, 0.0f, 1.0f);
        graphite = graphite <= 0.025f ? 0.0f : std::pow((graphite - 0.025f) / 0.975f, 1.08f);
        material.graphite[i] = graphite;
    }

    const float scale = std::min(
        static_cast<float>(std::max<std::uint32_t>(1, canvasWidth)) / static_cast<float>(image.width),
        static_cast<float>(std::max<std::uint32_t>(1, canvasHeight)) / static_cast<float>(image.height));
    material.targetWidth = static_cast<float>(image.width) * scale;
    material.targetHeight = static_cast<float>(image.height) * scale;
    material.targetX = (static_cast<float>(std::max<std::uint32_t>(1, canvasWidth)) - material.targetWidth) * 0.5f;
    material.targetY = (static_cast<float>(std::max<std::uint32_t>(1, canvasHeight)) - material.targetHeight) * 0.5f;
    return material;
}

bool loadReferenceImage(HWND hwnd, AppState& app)
{
    const std::wstring path = chooseImagePath(hwnd, L"Choose canvas reference image");
    if (path.empty())
    {
        return false;
    }

    const UiTexture texture = app.renderer.loadUiTexture(path.c_str());
    if (!texture.textureId)
    {
        app.referenceImageLoadFailed = true;
        return true;
    }

    app.referenceImage = texture;
    app.referenceImagePath = path;
    app.referenceImageVisible = true;
    app.referenceImageLoadFailed = false;
    return true;
}

bool importSketchAsGraphite(HWND hwnd, AppState& app)
{
    const std::wstring path = chooseImagePath(hwnd, L"Import sketch as graphite material");
    if (path.empty())
    {
        return false;
    }

    DecodedImagePixels image;
    if (!decodeImagePixels(path, image))
    {
        app.sketchImportFailed = true;
        app.sketchImportSucceeded = false;
        return true;
    }

    ImportedSketchMaterial material = sketchMaterialFromImage(image, app.canvasWidth, app.canvasHeight);
    if (!app.document || !app.document->importSketchMaterial(material))
    {
        app.sketchImportFailed = true;
        app.sketchImportSucceeded = false;
        return true;
    }

    app.sketchImportPath = path;
    app.sketchImportFailed = false;
    app.sketchImportSucceeded = true;
    return true;
}

float displayPixelsPerMm(HWND hwnd)
{
    HDC dc = GetDC(hwnd);
    if (!dc) return kFallbackDisplayPixelsPerMm;
    const int widthPx = GetDeviceCaps(dc, HORZRES);
    const int widthMm = GetDeviceCaps(dc, HORZSIZE);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);
    if (widthPx > 0 && widthMm > 0)
    {
        const float physical = static_cast<float>(widthPx) / static_cast<float>(widthMm);
        if (physical >= 3.0f && physical <= 12.0f) return physical;
    }
    if (dpi > 0) return static_cast<float>(dpi) / 25.4f;
    return kFallbackDisplayPixelsPerMm;
}

const char* toolName(ToolKind tool, PencilGrade grade)
{
    if (tool == ToolKind::RegularEraser) return "Regular eraser";
    if (tool == ToolKind::KneadedEraser) return "Kneaded eraser";
    if (tool == ToolKind::ElectricEraser) return "Electric eraser";
    if (tool == ToolKind::Tortillon) return "Tortillon";
    if (tool == ToolKind::FanBrush) return "Fan brush";
    if (tool == ToolKind::PowderBrush) return "Powder brush";
    if (tool == ToolKind::GraphitePowder) return "Graphite powder";
    if (grade == PencilGrade::FourH) return "Pencil 4H";
    if (grade == PencilGrade::ThreeH) return "Pencil 3H";
    if (grade == PencilGrade::TwoH) return "Pencil 2H";
    if (grade == PencilGrade::H) return "Pencil H";
    if (grade == PencilGrade::B) return "Pencil B";
    if (grade == PencilGrade::TwoB) return "Pencil 2B";
    if (grade == PencilGrade::FourB) return "Pencil 4B";
    if (grade == PencilGrade::SixB) return "Pencil 6B";
    if (grade == PencilGrade::EightB) return "Pencil 8B";
    return "Pencil HB";
}

PencilGrade pencilGradeForTrayIndex(int index)
{
    if (index == 0) return PencilGrade::FourH;
    if (index == 1) return PencilGrade::ThreeH;
    if (index == 2) return PencilGrade::TwoH;
    if (index == 3) return PencilGrade::H;
    if (index == 5) return PencilGrade::B;
    if (index == 6) return PencilGrade::TwoB;
    if (index == 7) return PencilGrade::FourB;
    if (index == 8) return PencilGrade::SixB;
    if (index == 9) return PencilGrade::EightB;
    return PencilGrade::HB;
}

const char* kneadedShapeName(KneadedEraserShape shape)
{
    if (shape == KneadedEraserShape::Point) return "point";
    if (shape == KneadedEraserShape::Edge) return "edge";
    if (shape == KneadedEraserShape::Flat) return "flat";
    return "blob";
}

const char* sourceName(InputSource source)
{
    if (source == InputSource::WmPointer) return "WM_POINTER";
    if (source == InputSource::RealTimeStylus) return "RealTimeStylus";
    return "Mouse fallback";
}

const char* tabletModeName(TabletInputMode mode)
{
    if (mode == TabletInputMode::WindowsInkRealTimeStylus) return "Windows Ink RTS";
    if (mode == TabletInputMode::WindowsInkRawPointer) return "Windows Ink raw";
    if (mode == TabletInputMode::MouseOnly) return "Mouse only";
    return "Auto";
}

const char* paperName(PaperPreset preset)
{
    if (preset == PaperPreset::SmoothBristol) return "Smooth bristol";
    if (preset == PaperPreset::RoughSketch) return "Rough sketch paper";
    return "Vellum drawing paper";
}

const char* pencilTipName(float radiusPx, float displayPixelsPerMm)
{
    const float midpoint = (kSharpenedPencilContactRadiusMm + kBluntPencilContactRadiusMm) * 0.5f * displayPixelsPerMm;
    return radiusPx <= midpoint ? "sharpened" : "blunt";
}

const char* viewName(DebugView view)
{
    if (view == DebugView::LooseGraphite) return "Loose graphite";
    if (view == DebugView::BoundGraphite) return "Bound graphite";
    if (view == DebugView::PaperHeight) return "Paper height";
    if (view == DebugView::Compaction) return "Compaction";
    if (view == DebugView::Damage) return "Damage";
    if (view == DebugView::PaperBinding) return "Paper binding";
    if (view == DebugView::SurfaceSheen) return "Surface sheen";
    if (view == DebugView::PaperRoughness) return "Paper roughness";
    return "Display tone";
}

bool acceptsRealTimeStylus(TabletInputMode mode)
{
    return mode == TabletInputMode::Auto || mode == TabletInputMode::WindowsInkRealTimeStylus;
}

bool acceptsRawWindowsInkPointer(const AppState& app)
{
    return app.tabletMode == TabletInputMode::WindowsInkRawPointer || (app.tabletMode == TabletInputMode::Auto && !app.realTimeStylus.available());
}

bool acceptsMouse(TabletInputMode mode)
{
    return mode == TabletInputMode::Auto || mode == TabletInputMode::MouseOnly;
}

RECT primaryMonitorWorkRect()
{
    RECT rect{0, 0, 1600, 900};
    HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfo(monitor, &info))
    {
        return info.rcWork;
    }
    return rect;
}

bool activeButton(const char* label, bool active, const ImVec2& size = ImVec2(0, 0))
{
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.38f, 0.34f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.48f, 0.43f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.32f, 0.29f, 1.0f));
    }
    const bool pressed = ImGui::Button(label, size);
    if (active)
    {
        ImGui::PopStyleColor(3);
    }
    return pressed;
}

void drawProductTextureFit(D3D12Renderer& renderer, const UiTexture& texture, ImVec2 center, ImVec2 maxSize, std::uint32_t color = 0xffffffffu)
{
    if (!texture.textureId) return;
    const float scale = std::min(maxSize.x / std::max(1.0f, static_cast<float>(texture.width)), maxSize.y / std::max(1.0f, static_cast<float>(texture.height)));
    const ImVec2 imageSize(static_cast<float>(texture.width) * scale, static_cast<float>(texture.height) * scale);
    renderer.drawProductTexture(texture, center.x - imageSize.x * 0.5f, center.y - imageSize.y * 0.5f, imageSize.x, imageSize.y, color);
}

void drawProductTextureFill(D3D12Renderer& renderer, const UiTexture& texture, ImVec2 center, ImVec2 size, std::uint32_t color = 0xffffffffu)
{
    if (!texture.textureId) return;
    renderer.drawProductTexture(texture, center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y, color);
}

ImVec2 trayPoint(const StudioLayout& layout, float x, float y)
{
    return ImVec2(layout.trayPos.x + x * layout.trayScale, layout.trayPos.y + y * layout.trayScale);
}

ImVec2 trayScaledSize(const StudioLayout& layout, float width, float height)
{
    return ImVec2(width * layout.trayScale, height * layout.trayScale);
}

ImVec2 traySlotCenter(const StudioLayout& layout, const TrayTextureSlot& slot)
{
    return trayPoint(layout, slot.centerX, slot.centerY);
}

ImVec2 traySlotMaxSize(const StudioLayout& layout, const TrayTextureSlot& slot)
{
    return trayScaledSize(layout, slot.maxWidth, slot.maxHeight);
}

void drawProductTraySlotTexture(D3D12Renderer& renderer, const UiTexture& texture, const StudioLayout& layout, const TrayTextureSlot& slot)
{
    drawProductTextureFit(renderer, texture, traySlotCenter(layout, slot), traySlotMaxSize(layout, slot));
}

void drawProductTrayTexture(D3D12Renderer& renderer, const UiTexture& texture, const StudioLayout& layout, float centerX, float centerY, float maxWidth, float maxHeight)
{
    drawProductTextureFit(renderer, texture, trayPoint(layout, centerX, centerY), trayScaledSize(layout, maxWidth, maxHeight));
}

void drawProductTrayTextureFill(D3D12Renderer& renderer, const UiTexture& texture, const StudioLayout& layout, float centerX, float centerY, float width, float height)
{
    drawProductTextureFill(renderer, texture, trayPoint(layout, centerX, centerY), trayScaledSize(layout, width, height));
}

std::uint32_t layerColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, float opacity)
{
    const float clamped = std::clamp(opacity, 0.0f, 1.0f);
    const int alpha = static_cast<int>(clamped * 255.0f + 0.5f);
    return IM_COL32(r, g, b, alpha);
}

void drawCanvasReferenceImage(const AppState& app, D3D12Renderer& renderer)
{
    if (!app.referenceImageVisible || !app.referenceImage.textureId) return;
    const float opacity = std::clamp(app.referenceImageOpacity, 0.0f, 1.0f);
    if (opacity <= 0.001f) return;

    const float canvasWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const float canvasHeight = static_cast<float>(std::max<std::uint32_t>(1, app.clientHeight));
    const float scale = std::min(
        canvasWidth / std::max(1.0f, static_cast<float>(app.referenceImage.width)),
        canvasHeight / std::max(1.0f, static_cast<float>(app.referenceImage.height)));
    const float imageWidth = static_cast<float>(app.referenceImage.width) * scale;
    const float imageHeight = static_cast<float>(app.referenceImage.height) * scale;
    const float x = (canvasWidth - imageWidth) * 0.5f;
    const float y = (canvasHeight - imageHeight) * 0.5f;
    renderer.drawProductTexture(app.referenceImage, x, y, imageWidth, imageHeight, layerColor(255, 255, 255, opacity));
}

void drawCanvasGridOverlay(const AppState& app, D3D12Renderer& renderer)
{
    if (!app.showGridOverlay || !app.whitePixel.textureId) return;
    const float opacity = std::clamp(app.gridOpacity, 0.0f, 1.0f);
    if (opacity <= 0.001f) return;

    const float canvasWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const float canvasHeight = static_cast<float>(std::max<std::uint32_t>(1, app.clientHeight));
    const float spacing = std::clamp(app.gridSpacingPx, kCanvasLayerMinGridSpacingPx, kCanvasLayerMaxGridSpacingPx);
    const std::uint32_t minor = layerColor(48, 54, 52, opacity * 0.55f);
    const std::uint32_t major = layerColor(30, 36, 34, opacity);

    int index = 0;
    for (float x = 0.0f; x <= canvasWidth; x += spacing, ++index)
    {
        const bool majorLine = index % 4 == 0;
        renderer.drawProductTexture(app.whitePixel, std::floor(x), 0.0f, majorLine ? 1.6f : 1.0f, canvasHeight, majorLine ? major : minor);
    }

    index = 0;
    for (float y = 0.0f; y <= canvasHeight; y += spacing, ++index)
    {
        const bool majorLine = index % 4 == 0;
        renderer.drawProductTexture(app.whitePixel, 0.0f, std::floor(y), canvasWidth, majorLine ? 1.6f : 1.0f, majorLine ? major : minor);
    }
}

void drawCanvasLayers(AppState& app, D3D12Renderer& renderer)
{
    drawCanvasReferenceImage(app, renderer);
    drawCanvasGridOverlay(app, renderer);
}

float easeOutCubic(float t)
{
    t = std::min(1.0f, std::max(0.0f, t));
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

float rolledVisibleWidth(const AppState& app)
{
    constexpr float kMinimumVisibleWidth = 96.0f;
    return kTrayDesignWidth - (kTrayDesignWidth - kMinimumVisibleWidth) * std::min(1.0f, std::max(0.0f, app.trayRoll));
}

float rolledCylinderDesignWidth(const AppState& app)
{
    if (app.trayRoll <= 0.02f) return 0.0f;
    return 36.0f + app.trayRoll * 150.0f;
}

float visibleFlatTrayWidth(const AppState& app)
{
    return std::max(0.0f, rolledVisibleWidth(app) - rolledCylinderDesignWidth(app));
}

bool slotVisibleForRoll(const AppState& app, const TrayTextureSlot& slot)
{
    return slot.centerX + slot.maxWidth * 0.5f <= visibleFlatTrayWidth(app) - 8.0f;
}

bool trayLayerVisibleForRoll(const AppState& app, float centerX, float width)
{
    return centerX + width * 0.5f <= visibleFlatTrayWidth(app) - 8.0f;
}

bool activeToolMatchesPencil(const AppState& app, int index)
{
    const bool active = app.activeTrayCommand == DebugCommand::Pencil4H && app.activeTrayPencilIndex == index ||
        app.activeTrayCommand == DebugCommand::PencilHB && app.activeTrayPencilIndex == index ||
        app.activeTrayCommand == DebugCommand::Pencil8B && app.activeTrayPencilIndex == index;
    const bool returning = app.returningTrayT < 1.0f &&
        ((app.returningTrayCommand == DebugCommand::Pencil4H && app.returningTrayPencilIndex == index) ||
            (app.returningTrayCommand == DebugCommand::PencilHB && app.returningTrayPencilIndex == index) ||
            (app.returningTrayCommand == DebugCommand::Pencil8B && app.returningTrayPencilIndex == index));
    return active || returning;
}

bool activeToolMatchesCommand(const AppState& app, DebugCommand command)
{
    return (app.activeTrayCommand == command && app.activeTrayPencilIndex < 0) ||
        (app.returningTrayT < 1.0f && app.returningTrayCommand == command && app.returningTrayPencilIndex < 0);
}

void activateTrayTool(AppState& app, DebugCommand command, int pencilIndex, const TrayTextureSlot& slot)
{
    if (app.activeTrayCommand != DebugCommand::None &&
        (app.activeTrayCommand != command || app.activeTrayPencilIndex != pencilIndex))
    {
        app.returningTrayCommand = app.activeTrayCommand;
        app.returningTrayPencilIndex = app.activeTrayPencilIndex;
        app.returningTraySourceCenterX = app.activeTraySourceCenterX;
        app.returningTraySourceCenterY = app.activeTraySourceCenterY;
        app.returningTraySourceMaxWidth = app.activeTraySourceMaxWidth;
        app.returningTraySourceMaxHeight = app.activeTraySourceMaxHeight;
        app.returningTrayT = 0.0f;
    }
    app.activeTrayCommand = command;
    app.activeTrayPencilIndex = pencilIndex;
    app.activeTraySourceCenterX = slot.centerX;
    app.activeTraySourceCenterY = slot.centerY;
    app.activeTraySourceMaxWidth = slot.maxWidth;
    app.activeTraySourceMaxHeight = slot.maxHeight;
    app.activeTrayLift = 0.0f;
}

const UiTexture* trayTextureFor(const AppState& app, DebugCommand command, int pencilIndex)
{
    const UiTexture* pencils[] = {&app.pencil4H, &app.pencil3H, &app.pencil2H, &app.pencilH, &app.pencilHB, &app.pencilB, &app.pencil2B, &app.pencil4B, &app.pencil6B, &app.pencil8B};
    if (pencilIndex >= 0 && pencilIndex < 10) return pencils[pencilIndex];
    if (command == DebugCommand::RegularEraser) return &app.vinylEraser;
    if (command == DebugCommand::KneadedEraser) return &app.kneadedEraser;
    if (command == DebugCommand::ElectricEraser) return &app.electricEraser;
    if (command == DebugCommand::Tortillon) return &app.tortillon;
    if (command == DebugCommand::FanBrush) return &app.fanBrush;
    if (command == DebugCommand::PowderBrush) return &app.powderBrush;
    if (command == DebugCommand::GraphitePowder) return &app.graphitePowder;
    return nullptr;
}

const UiTexture* activeTrayTexture(const AppState& app)
{
    return trayTextureFor(app, app.activeTrayCommand, app.activeTrayPencilIndex);
}

bool pointInTrayRect(POINT point, const TrayRect& rect)
{
    return point.x >= static_cast<LONG>(rect.x) &&
        point.x < static_cast<LONG>(rect.x + rect.width) &&
        point.y >= static_cast<LONG>(rect.y) &&
        point.y < static_cast<LONG>(rect.y + rect.height);
}

bool pointInScreenRect(POINT point, const ScreenRect& rect)
{
    return point.x >= static_cast<LONG>(rect.x) &&
        point.x < static_cast<LONG>(rect.x + rect.width) &&
        point.y >= static_cast<LONG>(rect.y) &&
        point.y < static_cast<LONG>(rect.y + rect.height);
}

TrayRect trayCloseRect(const AppState& app)
{
    return TrayRect{
        std::max(12.0f, visibleFlatTrayWidth(app) - kTrayCloseToggleRect.width - 16.0f),
        kTrayCloseToggleRect.y,
        kTrayCloseToggleRect.width,
        kTrayCloseToggleRect.height,
    };
}

ScreenRect toolOptionsPanelRect(const AppState& app)
{
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    return ScreenRect{
        std::max(18.0f, clientWidth - kOptionsPanelWidth - kOptionsPanelMargin),
        22.0f,
        kOptionsPanelWidth,
        kOptionsPanelHeight,
    };
}

ScreenRect toolOptionsTabRect(const AppState& app)
{
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    return ScreenRect{
        clientWidth - kOptionsTabWidth * 0.58f,
        122.0f,
        kOptionsTabWidth,
        kOptionsTabHeight,
    };
}

ScreenRect paperPickerPanelRect(const AppState& app)
{
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const float panelWidth = std::min(568.0f, clientWidth - 36.0f);
    return ScreenRect{
        (clientWidth - panelWidth) * 0.5f,
        24.0f,
        panelWidth,
        kPaperPickerHeight,
    };
}

ScreenRect canvasLayersPanelRect(const AppState& app)
{
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const float rightX = std::max(18.0f, clientWidth - kCanvasLayersPanelWidth - 14.0f);
    float x = rightX;
    if (app.showInputTrace && rightX > kCanvasLayersPanelWidth + 42.0f)
    {
        x = rightX - kCanvasLayersPanelWidth - 18.0f;
    }
    return ScreenRect{x, kCanvasLayersPanelTop, kCanvasLayersPanelWidth, kCanvasLayersPanelHeight};
}

ScreenRect insetRect(const ScreenRect& rect, float x, float y, float width, float height)
{
    return ScreenRect{rect.x + x, rect.y + y, width, height};
}

POINT pointToTraySpace(const StudioLayout& layout, POINT point)
{
    point.x = static_cast<LONG>((static_cast<float>(point.x) - layout.trayPos.x) / layout.trayScale);
    point.y = static_cast<LONG>((static_cast<float>(point.y) - layout.trayPos.y) / layout.trayScale);
    return point;
}

StudioLayout computeStudioLayout(const AppState& app)
{
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const float clientHeight = static_cast<float>(std::max<std::uint32_t>(1, app.clientHeight));
    const float widthScale = (clientWidth - kTrayMargin * 2.0f) / kTrayDesignWidth;
    const float heightScale = (clientHeight - kTrayMargin * 2.0f) / kTrayDesignHeight;
    const float trayScale = std::min(kMaximumTrayScale, std::max(kMinimumTrayScale, std::min(widthScale, heightScale)));
    const ImVec2 traySize(kTrayDesignWidth * trayScale, kTrayDesignHeight * trayScale);
    const float visibleTrayWidth = rolledVisibleWidth(app) * trayScale;
    const ImVec2 trayPos(
        std::min(std::max(0.0f, app.trayPosition.x), std::max(0.0f, clientWidth - visibleTrayWidth)),
        std::min(std::max(0.0f, app.trayPosition.y), std::max(0.0f, clientHeight - traySize.y)));
    return StudioLayout{
        trayPos,
        traySize,
        ImVec2(0.0f, 0.0f),
        ImVec2(clientWidth, clientHeight),
        trayScale,
    };
}

StudioLayout trayPresentationLayout(const AppState& app)
{
    StudioLayout layout = computeStudioLayout(app);
    const float hidePresentation = app.trayHideTarget > app.trayHide ? app.trayHideTarget : app.trayHide;
    layout.trayPos.x += hidePresentation * (static_cast<float>(app.clientWidth) - layout.trayPos.x + kTrayMargin);
    return layout;
}

ImVec2 hiddenTabSize(const AppState& app)
{
    if (app.hiddenTabEdge == HiddenTabTop || app.hiddenTabEdge == HiddenTabBottom)
    {
        return ImVec2(kHiddenTrayTabHeight, kHiddenTrayTabWidth);
    }
    return ImVec2(kHiddenTrayTabWidth, kHiddenTrayTabHeight);
}

void clampHiddenTabToEdge(AppState& app)
{
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const float clientHeight = static_cast<float>(std::max<std::uint32_t>(1, app.clientHeight));
    ImVec2 size = hiddenTabSize(app);
    if (app.hiddenTabCenter.x <= 0.0f || app.hiddenTabCenter.y <= 0.0f)
    {
        app.hiddenTabEdge = HiddenTabRight;
        size = hiddenTabSize(app);
        app.hiddenTabCenter = ImVec2(clientWidth - size.x * kHiddenTrayTabEdgeInset, clientHeight * 0.5f);
    }

    if (app.hiddenTabEdge == HiddenTabLeft || app.hiddenTabEdge == HiddenTabRight)
    {
        app.hiddenTabCenter.y = std::min(std::max(size.y * 0.5f, app.hiddenTabCenter.y), clientHeight - size.y * 0.5f);
        app.hiddenTabCenter.x = app.hiddenTabEdge == HiddenTabLeft ? size.x * kHiddenTrayTabEdgeInset : clientWidth - size.x * kHiddenTrayTabEdgeInset;
    }
    else
    {
        app.hiddenTabCenter.x = std::min(std::max(size.x * 0.5f, app.hiddenTabCenter.x), clientWidth - size.x * 0.5f);
        app.hiddenTabCenter.y = app.hiddenTabEdge == HiddenTabTop ? size.y * kHiddenTrayTabEdgeInset : clientHeight - size.y * kHiddenTrayTabEdgeInset;
    }
}

void dockHiddenTabNearPoint(AppState& app, POINT point)
{
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const float clientHeight = static_cast<float>(std::max<std::uint32_t>(1, app.clientHeight));
    const float left = static_cast<float>(point.x);
    const float right = clientWidth - static_cast<float>(point.x);
    const float top = static_cast<float>(point.y);
    const float bottom = clientHeight - static_cast<float>(point.y);
    float nearest = left;
    app.hiddenTabEdge = HiddenTabLeft;
    if (right < nearest) { nearest = right; app.hiddenTabEdge = HiddenTabRight; }
    if (top < nearest) { nearest = top; app.hiddenTabEdge = HiddenTabTop; }
    if (bottom < nearest) { app.hiddenTabEdge = HiddenTabBottom; }
    app.hiddenTabCenter = ImVec2(static_cast<float>(point.x), static_cast<float>(point.y));
    clampHiddenTabToEdge(app);
}

TrayRect hiddenTabRect(const AppState& app)
{
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const float clientHeight = static_cast<float>(std::max<std::uint32_t>(1, app.clientHeight));
    const ImVec2 size = hiddenTabSize(app);
    ImVec2 center = app.hiddenTabCenter;
    if (center.x <= 0.0f || center.y <= 0.0f)
    {
        center = ImVec2(clientWidth - size.x * kHiddenTrayTabEdgeInset, clientHeight * 0.5f);
    }
    if (app.hiddenTabEdge == HiddenTabLeft || app.hiddenTabEdge == HiddenTabRight)
    {
        center.y = std::min(std::max(size.y * 0.5f, center.y), clientHeight - size.y * 0.5f);
        center.x = app.hiddenTabEdge == HiddenTabLeft ? size.x * kHiddenTrayTabEdgeInset : clientWidth - size.x * kHiddenTrayTabEdgeInset;
    }
    else
    {
        center.x = std::min(std::max(size.x * 0.5f, center.x), clientWidth - size.x * 0.5f);
        center.y = app.hiddenTabEdge == HiddenTabTop ? size.y * kHiddenTrayTabEdgeInset : clientHeight - size.y * kHiddenTrayTabEdgeInset;
    }
    return TrayRect{center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y};
}

bool pointOverHiddenTrayTab(const AppState& app, POINT point)
{
    const TrayRect rect = hiddenTabRect(app);
    return pointInTrayRect(point, rect);
}

DebugCommand trayCommandFromPoint(AppState& app, POINT point)
{
    const StudioLayout layout = computeStudioLayout(app);
    const POINT screenPoint = point;
    if (app.trayHideTarget > 0.5f || app.trayHide > 0.5f)
    {
        if (pointOverHiddenTrayTab(app, point))
        {
            app.trayHideTarget = 0.0f;
        }
        return DebugCommand::None;
    }

    point = pointToTraySpace(layout, point);

    if (pointInTrayRect(point, trayCloseRect(app)))
    {
        app.trayHideTarget = 1.0f;
        dockHiddenTabNearPoint(app, screenPoint);
        return DebugCommand::None;
    }
    if (pointInTrayRect(point, kPaperSmoothRect)) return DebugCommand::PaperSmooth;
    if (pointInTrayRect(point, kPaperColdRect)) return DebugCommand::PaperColdPress;
    if (pointInTrayRect(point, kPaperRoughRect)) return DebugCommand::PaperRough;
    if (pointInTrayRect(point, kKneadedBlobRect)) return DebugCommand::KneadedShapeBlob;
    if (pointInTrayRect(point, kKneadedPointRect)) return DebugCommand::KneadedShapePoint;
    if (pointInTrayRect(point, kKneadedEdgeRect)) return DebugCommand::KneadedShapeEdge;
    if (pointInTrayRect(point, kKneadedFlatRect)) return DebugCommand::KneadedShapeFlat;

    for (LONG i = 0; i < 10; ++i)
    {
        if (!slotVisibleForRoll(app, kPencilSlots[i])) continue;
        const TrayTextureSlot& slot = kPencilSlots[i];
        TrayRect pencilRect{slot.centerX - 19.0f, slot.centerY - 150.0f, 38.0f, 300.0f};
        if (!pointInTrayRect(point, pencilRect)) continue;
        const DebugCommand command = i <= 3 ? DebugCommand::Pencil4H : (i <= 6 ? DebugCommand::PencilHB : DebugCommand::Pencil8B);
        activateTrayTool(app, command, static_cast<int>(i), kPencilSlots[i]);
        return command;
    }

    if (slotVisibleForRoll(app, kTortillonSlot) && pointInTrayRect(point, kTortillonHitRect)) { activateTrayTool(app, DebugCommand::Tortillon, -1, kTortillonSlot); return DebugCommand::Tortillon; }
    if (slotVisibleForRoll(app, kVinylEraserSlot) && pointInTrayRect(point, kVinylEraserHitRect)) { activateTrayTool(app, DebugCommand::RegularEraser, -1, kVinylEraserSlot); return DebugCommand::RegularEraser; }
    if (slotVisibleForRoll(app, kKneadedEraserSlot) && pointInTrayRect(point, kKneadedEraserHitRect)) { activateTrayTool(app, DebugCommand::KneadedEraser, -1, kKneadedEraserSlot); return DebugCommand::KneadedEraser; }
    if (slotVisibleForRoll(app, kElectricEraserSlot) && pointInTrayRect(point, kElectricEraserHitRect)) { activateTrayTool(app, DebugCommand::ElectricEraser, -1, kElectricEraserSlot); return DebugCommand::ElectricEraser; }
    if (slotVisibleForRoll(app, kFanBrushSlot) && pointInTrayRect(point, kFanBrushHitRect)) { activateTrayTool(app, DebugCommand::FanBrush, -1, kFanBrushSlot); return DebugCommand::FanBrush; }
    if (slotVisibleForRoll(app, kPowderBrushSlot) && pointInTrayRect(point, kPowderBrushHitRect)) { activateTrayTool(app, DebugCommand::PowderBrush, -1, kPowderBrushSlot); return DebugCommand::PowderBrush; }
    if (slotVisibleForRoll(app, kGraphitePowderSlot) && pointInTrayRect(point, kGraphitePowderHitRect)) { activateTrayTool(app, DebugCommand::GraphitePowder, -1, kGraphitePowderSlot); return DebugCommand::GraphitePowder; }

    return DebugCommand::None;
}

bool pointOverTrayDragHandle(const AppState& app, POINT point)
{
    if (app.trayHideTarget > 0.5f || app.trayHide > 0.5f) return pointOverHiddenTrayTab(app, point);
    const StudioLayout layout = computeStudioLayout(app);
    const POINT trayPoint = pointToTraySpace(layout, point);
    const TrayRect fullTray{0.0f, 0.0f, rolledVisibleWidth(app), kTrayDesignHeight};
    return pointInTrayRect(trayPoint, fullTray);
}

void moveTrayByPixels(AppState& app, int dx, int dy)
{
    const StudioLayout layout = computeStudioLayout(app);
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const float clientHeight = static_cast<float>(std::max<std::uint32_t>(1, app.clientHeight));
    const float visibleTrayWidth = rolledVisibleWidth(app) * layout.trayScale;
    app.trayPosition.x = std::min(std::max(0.0f, app.trayPosition.x + static_cast<float>(dx)), std::max(0.0f, clientWidth - visibleTrayWidth));
    app.trayPosition.y = std::min(std::max(0.0f, app.trayPosition.y + static_cast<float>(dy)), std::max(0.0f, clientHeight - layout.traySize.y));
}

bool uiPointerMessage(UINT message)
{
    return message == WM_POINTERDOWN ||
        message == WM_POINTERUPDATE ||
        message == WM_POINTERUP ||
        message == WM_POINTERCAPTURECHANGED ||
        message == WM_LBUTTONDOWN ||
        message == WM_LBUTTONUP ||
        message == WM_RBUTTONDOWN ||
        message == WM_RBUTTONUP ||
        message == WM_MOUSEMOVE;
}

bool clientPointFromMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, POINT& point)
{
    if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE || message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED)
    {
        POINTER_INFO info{};
        if (!GetPointerInfo(GET_POINTERID_WPARAM(wParam), &info)) return false;
        point = info.ptPixelLocation;
        ScreenToClient(hwnd, &point);
        return true;
    }
    point.x = GET_X_LPARAM(lParam);
    point.y = GET_Y_LPARAM(lParam);
    return true;
}

bool clientPointOverUi(const AppState& app, POINT point)
{
    if (app.trayHideTarget > 0.5f || app.trayHide > 0.5f)
    {
        return pointOverHiddenTrayTab(app, point);
    }

    const StudioLayout layout = trayPresentationLayout(app);
    const float clientWidth = static_cast<float>(std::max<std::uint32_t>(1, app.clientWidth));
    const LONG rightX = static_cast<LONG>(std::max(760.0f, clientWidth - 300.0f));
    const LONG bottomY = static_cast<LONG>(std::max(680.0f, static_cast<float>(app.clientHeight) - 92.0f));
    const LONG trayRight = static_cast<LONG>(layout.trayPos.x + rolledVisibleWidth(app) * layout.trayScale);
    const LONG trayBottom = static_cast<LONG>(layout.trayPos.y + layout.traySize.y);
    const bool overToolTray = point.x >= static_cast<LONG>(layout.trayPos.x) && point.x <= trayRight && point.y >= static_cast<LONG>(layout.trayPos.y) && point.y <= trayBottom;
    const bool overToolOptions = app.toolOptionsCollapsed ?
        pointInScreenRect(point, toolOptionsTabRect(app)) :
        pointInScreenRect(point, toolOptionsPanelRect(app));
    const bool overPaperPicker = app.paperPickerOpen && pointInScreenRect(point, paperPickerPanelRect(app));
    const bool overCanvasLayers = pointInScreenRect(point, canvasLayersPanelRect(app));
    const bool overPaperPanel = app.showInputTrace && point.x >= rightX && point.x <= rightX + 286 && point.y >= 276 && point.y <= 552;
    const bool overInputPanel = app.showInputTrace && point.x >= rightX && point.x <= rightX + 286 && point.y >= 568 && point.y <= 782;
    const bool overStatus = point.x >= 406 && point.x <= rightX && point.y >= bottomY && point.y <= bottomY + 72;
    return overToolTray || overToolOptions || overPaperPicker || overCanvasLayers || overPaperPanel || overInputPanel || overStatus;
}

bool clientPointOverPaper(const AppState& app, POINT point)
{
    return point.x >= 0 &&
        point.y >= 0 &&
        point.x < static_cast<LONG>(app.clientWidth) &&
        point.y < static_cast<LONG>(app.clientHeight) &&
        !clientPointOverUi(app, point);
}

float cursorContactRadiusPx(const ToolParams& params)
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

void queuePaperPreset(AppState& app, PaperPreset preset)
{
    if (preset == app.document->paperPreset()) return;
    app.pendingPaperPreset = preset;
    app.pendingPaperPresetChange = true;
}

void choosePaperPreset(AppState& app, PaperPreset preset)
{
    queuePaperPreset(app, preset);
    app.paperPickerOpen = false;
}

ScreenRect paperSwatchRect(const AppState& app, int index)
{
    const ScreenRect panel = paperPickerPanelRect(app);
    const float gap = 14.0f;
    const float swatchWidth = (panel.width - 52.0f - gap * 2.0f) / 3.0f;
    return insetRect(panel, 26.0f + static_cast<float>(index) * (swatchWidth + gap), 84.0f, swatchWidth, 70.0f);
}

ScreenRect optionChoiceRect(const AppState& app, int index, int columns, float top)
{
    const ScreenRect panel = toolOptionsPanelRect(app);
    const float gap = 10.0f;
    const float left = 22.0f;
    const float width = (panel.width - left * 2.0f - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    return insetRect(panel, left + static_cast<float>(index) * (width + gap), top, width, 32.0f);
}

bool handleStudioOptionPoint(AppState& app, POINT point, DebugCommand& command)
{
    command = DebugCommand::None;
    if (app.paperPickerOpen)
    {
        if (pointInScreenRect(point, paperSwatchRect(app, 0))) { choosePaperPreset(app, PaperPreset::SmoothBristol); return true; }
        if (pointInScreenRect(point, paperSwatchRect(app, 1))) { choosePaperPreset(app, PaperPreset::ColdPress); return true; }
        if (pointInScreenRect(point, paperSwatchRect(app, 2))) { choosePaperPreset(app, PaperPreset::RoughSketch); return true; }
        if (pointInScreenRect(point, paperPickerPanelRect(app))) return true;
    }

    if (app.toolOptionsCollapsed)
    {
        if (!pointInScreenRect(point, toolOptionsTabRect(app))) return false;
        app.toolOptionsCollapsed = false;
        return true;
    }

    const ScreenRect panel = toolOptionsPanelRect(app);
    if (!pointInScreenRect(point, panel)) return false;

    if (pointInScreenRect(point, insetRect(panel, panel.width - 62.0f, 14.0f, 40.0f, 28.0f)))
    {
        app.toolOptionsCollapsed = true;
        return true;
    }

    auto& params = app.document->toolParams();
    if (params.tool == ToolKind::Pencil)
    {
        if (pointInScreenRect(point, optionChoiceRect(app, 0, 2, 84.0f))) { params.radiusPx = kSharpenedPencilContactRadiusMm * app.displayPixelsPerMm; return true; }
        if (pointInScreenRect(point, optionChoiceRect(app, 1, 2, 84.0f))) { params.radiusPx = kBluntPencilContactRadiusMm * app.displayPixelsPerMm; return true; }
    }
    else if (params.tool == ToolKind::KneadedEraser)
    {
        if (pointInScreenRect(point, optionChoiceRect(app, 0, 2, 84.0f))) { command = DebugCommand::KneadedShapeBlob; return true; }
        if (pointInScreenRect(point, optionChoiceRect(app, 1, 2, 84.0f))) { command = DebugCommand::KneadedShapePoint; return true; }
        if (pointInScreenRect(point, optionChoiceRect(app, 0, 2, 126.0f))) { command = DebugCommand::KneadedShapeEdge; return true; }
        if (pointInScreenRect(point, optionChoiceRect(app, 1, 2, 126.0f))) { command = DebugCommand::KneadedShapeFlat; return true; }
        if (pointInScreenRect(point, insetRect(panel, 22.0f, 178.0f, panel.width - 44.0f, 32.0f))) { command = DebugCommand::CleanTool; return true; }
    }
    else if (params.tool == ToolKind::Tortillon || params.tool == ToolKind::FanBrush || params.tool == ToolKind::PowderBrush ||
        params.tool == ToolKind::RegularEraser || params.tool == ToolKind::ElectricEraser)
    {
        if (pointInScreenRect(point, insetRect(panel, 22.0f, 112.0f, panel.width - 44.0f, 34.0f))) { command = DebugCommand::CleanTool; return true; }
    }

    return true;
}

void queueDebugView(AppState& app, DebugView view)
{
    if (view == app.document->debugView()) return;
    app.pendingDebugView = view;
    app.pendingDebugViewChange = true;
}

void cycleTabletMode(AppState& app, bool reverse)
{
    constexpr TabletInputMode modes[] = {
        TabletInputMode::Auto,
        TabletInputMode::WindowsInkRealTimeStylus,
        TabletInputMode::WindowsInkRawPointer,
        TabletInputMode::MouseOnly,
    };
    int index = 0;
    for (int i = 0; i < static_cast<int>(std::size(modes)); ++i)
    {
        if (modes[i] == app.tabletMode) index = i;
    }
    index += reverse ? -1 : 1;
    if (index < 0) index = static_cast<int>(std::size(modes)) - 1;
    if (index >= static_cast<int>(std::size(modes))) index = 0;
    app.tabletMode = modes[index];
}

void drawHiddenTrayTab(AppState& app, D3D12Renderer& renderer)
{
    const float hidePresentation = app.trayHideTarget > app.trayHide ? app.trayHideTarget : app.trayHide;
    if (hidePresentation <= 0.02f) return;

    clampHiddenTabToEdge(app);
    const float alphaT = std::min(1.0f, std::max(0.0f, hidePresentation));
    const ImVec2 size = hiddenTabSize(app);
    const UiTexture* tabTexture = &app.leatherSideTabLeft;
    if (app.hiddenTabEdge == HiddenTabLeft) tabTexture = &app.leatherSideTab;
    else if (app.hiddenTabEdge == HiddenTabTop) tabTexture = &app.leatherSideTabBottom;
    else if (app.hiddenTabEdge == HiddenTabBottom) tabTexture = &app.leatherSideTabTop;
    const int alpha = static_cast<int>(255.0f * alphaT);
    const std::uint32_t color = 0x00ffffffu | (static_cast<std::uint32_t>(alpha) << 24);
    drawProductTextureFit(renderer, *tabTexture, app.hiddenTabCenter, size, color);
}

void drawStudioOptionBackings(AppState& app, D3D12Renderer& renderer);

void drawProductTray(AppState& app, D3D12Renderer& renderer)
{
    const StudioLayout baseLayout = computeStudioLayout(app);
    StudioLayout layout = trayPresentationLayout(app);
    const float visibleWidth = rolledVisibleWidth(app);
    if (app.trayHideTarget > 0.5f)
    {
        drawHiddenTrayTab(app, renderer);
        drawStudioOptionBackings(app, renderer);
        return;
    }

    renderer.drawProductTextureUv(app.trayBackground, layout.trayPos.x, layout.trayPos.y, visibleWidth * layout.trayScale, layout.traySize.y, 0.0f, 0.0f, visibleWidth / kTrayDesignWidth, 1.0f);

    const UiTexture pencils[] = {app.pencil4H, app.pencil3H, app.pencil2H, app.pencilH, app.pencilHB, app.pencilB, app.pencil2B, app.pencil4B, app.pencil6B, app.pencil8B};
    for (int i = 0; i < 10; ++i)
    {
        if (!slotVisibleForRoll(app, kPencilSlots[i]) || activeToolMatchesPencil(app, i)) continue;
        drawProductTraySlotTexture(renderer, pencils[i], layout, kPencilSlots[i]);
    }

    if (slotVisibleForRoll(app, kTortillonSlot) && !activeToolMatchesCommand(app, DebugCommand::Tortillon)) drawProductTraySlotTexture(renderer, app.tortillon, layout, kTortillonSlot);
    if (slotVisibleForRoll(app, kVinylEraserSlot) && !activeToolMatchesCommand(app, DebugCommand::RegularEraser)) drawProductTraySlotTexture(renderer, app.vinylEraser, layout, kVinylEraserSlot);
    if (slotVisibleForRoll(app, kKneadedEraserSlot) && !activeToolMatchesCommand(app, DebugCommand::KneadedEraser)) drawProductTraySlotTexture(renderer, app.kneadedEraser, layout, kKneadedEraserSlot);
    if (slotVisibleForRoll(app, kElectricEraserSlot) && !activeToolMatchesCommand(app, DebugCommand::ElectricEraser)) drawProductTraySlotTexture(renderer, app.electricEraser, layout, kElectricEraserSlot);
    if (slotVisibleForRoll(app, kFanBrushSlot) && !activeToolMatchesCommand(app, DebugCommand::FanBrush)) drawProductTraySlotTexture(renderer, app.fanBrush, layout, kFanBrushSlot);
    if (slotVisibleForRoll(app, kPowderBrushSlot) && !activeToolMatchesCommand(app, DebugCommand::PowderBrush)) drawProductTraySlotTexture(renderer, app.powderBrush, layout, kPowderBrushSlot);
    if (slotVisibleForRoll(app, kGraphitePowderSlot) && !activeToolMatchesCommand(app, DebugCommand::GraphitePowder)) drawProductTraySlotTexture(renderer, app.graphitePowder, layout, kGraphitePowderSlot);

    if (trayLayerVisibleForRoll(app, 221.0f, 125.0f)) drawProductTrayTextureFill(renderer, app.leatherEraserPocket, layout, 221.0f, 148.0f, 125.0f, 78.0f);
    if (trayLayerVisibleForRoll(app, 218.0f, 125.0f)) drawProductTrayTextureFill(renderer, app.leatherEraserPocket, layout, 218.0f, 348.0f, 125.0f, 78.0f);
    if (trayLayerVisibleForRoll(app, 878.0f, 511.0f)) drawProductTrayTextureFill(renderer, app.leatherPencilPocket, layout, 878.0f, 356.0f, 511.0f, 92.0f);

    if (trayLayerVisibleForRoll(app, 347.0f, 94.0f)) drawProductTrayTextureFill(renderer, app.pencilStrap, layout, 347.0f, 264.0f, 94.0f, 19.0f);
    if (trayLayerVisibleForRoll(app, 581.0f, 68.0f)) drawProductTrayTextureFill(renderer, app.pencilStrap, layout, 581.0f, 221.0f, 68.0f, 14.0f);
    if (trayLayerVisibleForRoll(app, 434.0f, 40.0f)) drawProductTrayTextureFill(renderer, app.pencilStrap, layout, 434.0f, 260.0f, 40.0f, 14.0f);
    if (trayLayerVisibleForRoll(app, 500.0f, 38.0f)) drawProductTrayTextureFill(renderer, app.pencilStrap, layout, 500.0f, 260.0f, 38.0f, 14.0f);

    const TrayTextureSlot pencilElasticSlots[] = {
        {650.0f, 258.0f, 38.0f, 12.0f},
        {703.0f, 258.0f, 38.0f, 12.0f},
        {752.0f, 258.0f, 38.0f, 12.0f},
        {800.0f, 258.0f, 38.0f, 12.0f},
        {847.0f, 258.0f, 38.0f, 12.0f},
        {900.0f, 258.0f, 38.0f, 12.0f},
        {949.0f, 258.0f, 38.0f, 12.0f},
        {1002.0f, 258.0f, 38.0f, 12.0f},
        {1050.0f, 258.0f, 38.0f, 12.0f},
        {1099.0f, 258.0f, 38.0f, 12.0f},
    };
    for (const TrayTextureSlot& slot : pencilElasticSlots)
    {
        if (trayLayerVisibleForRoll(app, slot.centerX, slot.maxWidth)) drawProductTrayTextureFill(renderer, app.pencilStrap, layout, slot.centerX, slot.centerY, slot.maxWidth, slot.maxHeight);
    }

    if (trayLayerVisibleForRoll(app, 1214.0f, 112.0f)) drawProductTrayTextureFill(renderer, app.leatherSideTab, layout, 1214.0f, 260.0f, 112.0f, 46.0f);

    if (app.trayRoll > 0.02f)
    {
        const float rollWidth = rolledCylinderDesignWidth(app) * layout.trayScale;
        const float rollHeight = layout.traySize.y;
        const float rollX = layout.trayPos.x + visibleWidth * layout.trayScale - rollWidth;
        const float rollY = layout.trayPos.y;
        renderer.drawProductTexture(app.rolledLeatherOutside, rollX, rollY, rollWidth, rollHeight);
    }

    if (const UiTexture* returningTexture = trayTextureFor(app, app.returningTrayCommand, app.returningTrayPencilIndex))
    {
        if (app.returningTrayT < 1.0f)
        {
            const float t = easeOutCubic(app.returningTrayT);
            const ImVec2 source = trayPoint(layout, app.returningTraySourceCenterX, app.returningTraySourceCenterY);
            const ImVec2 center(source.x, source.y - 78.0f * layout.trayScale * (1.0f - t));
            const ImVec2 maxSize = trayScaledSize(layout, app.returningTraySourceMaxWidth, app.returningTraySourceMaxHeight);
            drawProductTextureFit(renderer, *returningTexture, center, maxSize);
        }
    }

    if (const UiTexture* activeTexture = activeTrayTexture(app))
    {
        const float t = easeOutCubic(app.activeTrayLift);
        const ImVec2 source = trayPoint(layout, app.activeTraySourceCenterX, app.activeTraySourceCenterY);
        const float slideY = -78.0f * layout.trayScale * t;
        const ImVec2 center(source.x, source.y + slideY);
        const ImVec2 maxSize = trayScaledSize(layout, app.activeTraySourceMaxWidth * (1.0f + 0.08f * t), app.activeTraySourceMaxHeight * (1.0f + 0.08f * t));
        const int alpha = static_cast<int>(255.0f * std::max(0.0f, 1.0f - t));
        const std::uint32_t color = 0x00ffffffu | (static_cast<std::uint32_t>(alpha) << 24);
        drawProductTextureFit(renderer, *activeTexture, center, maxSize, color);
    }

    const float hidePresentation = app.trayHideTarget > app.trayHide ? app.trayHideTarget : app.trayHide;
    if (hidePresentation > 0.02f)
    {
        drawHiddenTrayTab(app, renderer);
    }
    drawStudioOptionBackings(app, renderer);
}

void drawStudioOptionBackings(AppState& app, D3D12Renderer& renderer)
{
    if (app.paperPickerOpen)
    {
        const ScreenRect picker = paperPickerPanelRect(app);
        renderer.drawProductTexture(app.imagegenPaperPickerPanel, picker.x, picker.y, picker.width, picker.height, 0xffffffffu);
    }

    if (app.toolOptionsCollapsed)
    {
        const ScreenRect tab = toolOptionsTabRect(app);
        renderer.drawProductTexture(app.imagegenOptionTag, tab.x, tab.y, tab.width, tab.height, 0xffffffffu);
        return;
    }

    const ScreenRect panel = toolOptionsPanelRect(app);
    renderer.drawProductTexture(app.imagegenToolOptionsPanel, panel.x, panel.y, panel.width, panel.height, 0xffffffffu);
    const auto drawTag = [&renderer, &app](const ScreenRect& rect, bool active)
    {
        renderer.drawProductTexture(app.imagegenOptionTag, rect.x, rect.y, rect.width, rect.height, active ? 0xffffffffu : 0xe8ffffffu);
    };
    auto& params = app.document->toolParams();
    drawTag(insetRect(panel, panel.width - 62.0f, 14.0f, 40.0f, 28.0f), false);
    if (params.tool == ToolKind::Pencil)
    {
        const bool sharp = pencilTipName(params.radiusPx, app.displayPixelsPerMm)[0] == 's';
        drawTag(optionChoiceRect(app, 0, 2, 84.0f), sharp);
        drawTag(optionChoiceRect(app, 1, 2, 84.0f), !sharp);
    }
    else if (params.tool == ToolKind::KneadedEraser)
    {
        drawTag(optionChoiceRect(app, 0, 2, 84.0f), params.kneadedShape == KneadedEraserShape::Blob);
        drawTag(optionChoiceRect(app, 1, 2, 84.0f), params.kneadedShape == KneadedEraserShape::Point);
        drawTag(optionChoiceRect(app, 0, 2, 126.0f), params.kneadedShape == KneadedEraserShape::Edge);
        drawTag(optionChoiceRect(app, 1, 2, 126.0f), params.kneadedShape == KneadedEraserShape::Flat);
        drawTag(insetRect(panel, 22.0f, 178.0f, panel.width - 44.0f, 32.0f), false);
    }
    else if (params.tool != ToolKind::GraphitePowder)
    {
        drawTag(insetRect(panel, 22.0f, 112.0f, panel.width - 44.0f, 34.0f), false);
    }
}

void drawCenteredLabel(ImDrawList* drawList, const ScreenRect& rect, const char* label, ImU32 color)
{
    const ImVec2 size = ImGui::CalcTextSize(label);
    drawList->AddText(ImVec2(rect.x + (rect.width - size.x) * 0.5f, rect.y + (rect.height - size.y) * 0.5f), color, label);
}

void drawOptionTag(ImDrawList* drawList, const ScreenRect& rect, const char* label, bool active)
{
    const ImU32 border = active ? IM_COL32(224, 211, 168, 230) : IM_COL32(120, 92, 58, 190);
    drawList->AddRect(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.width, rect.y + rect.height), border, 2.0f, 0, active ? 2.0f : 1.0f);
    drawCenteredLabel(drawList, rect, label, IM_COL32(246, 232, 192, 245));
}

void drawPaperSwatch(ImDrawList* drawList, const ScreenRect& rect, const char* title, const char* detail, PaperPreset preset, PaperPreset currentPaper)
{
    ImU32 paperColor = IM_COL32(239, 230, 212, 255);
    if (preset == PaperPreset::SmoothBristol) paperColor = IM_COL32(246, 241, 228, 255);
    if (preset == PaperPreset::RoughSketch) paperColor = IM_COL32(226, 213, 188, 255);
    const bool active = preset == currentPaper;
    drawList->AddRectFilled(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.width, rect.y + rect.height), paperColor, 1.0f);
    for (int i = 0; i < 7; ++i)
    {
        const float y = rect.y + 10.0f + static_cast<float>(i) * 8.0f;
        const int alpha = preset == PaperPreset::SmoothBristol ? 18 : (preset == PaperPreset::ColdPress ? 30 : 44);
        drawList->AddLine(ImVec2(rect.x + 12.0f, y), ImVec2(rect.x + rect.width - 12.0f, y + (i % 2 == 0 ? 1.5f : -1.5f)), IM_COL32(126, 104, 78, alpha), 1.0f);
    }
    drawList->AddRect(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.width, rect.y + rect.height), active ? IM_COL32(74, 98, 82, 255) : IM_COL32(102, 78, 50, 210), 1.0f, 0, active ? 3.0f : 1.0f);
    drawList->AddText(ImVec2(rect.x + 13.0f, rect.y + 16.0f), IM_COL32(46, 34, 24, 245), title);
    drawList->AddText(ImVec2(rect.x + 13.0f, rect.y + 38.0f), IM_COL32(78, 58, 40, 230), detail);
}

void drawStudioOptionLabels(AppState& app, const BackendStats& stats)
{
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (app.paperPickerOpen)
    {
        const ScreenRect picker = paperPickerPanelRect(app);
        drawList->AddText(ImVec2(picker.x + 24.0f, picker.y + 18.0f), IM_COL32(246, 232, 192, 250), "Choose paper");
        drawList->AddText(ImVec2(picker.x + 24.0f, picker.y + 38.0f), IM_COL32(210, 185, 137, 225), "The paper sample becomes the fresh drawing surface.");
        const PaperPreset currentPaper = app.document->paperPreset();
        drawPaperSwatch(drawList, paperSwatchRect(app, 0), "Smooth bristol", "plate finish", PaperPreset::SmoothBristol, currentPaper);
        drawPaperSwatch(drawList, paperSwatchRect(app, 1), "Vellum drawing", "medium tooth", PaperPreset::ColdPress, currentPaper);
        drawPaperSwatch(drawList, paperSwatchRect(app, 2), "Rough sketch", "open tooth", PaperPreset::RoughSketch, currentPaper);
    }

    if (app.toolOptionsCollapsed)
    {
        const ScreenRect tab = toolOptionsTabRect(app);
        drawCenteredLabel(drawList, ScreenRect{tab.x + 10.0f, tab.y + 7.0f, 74.0f, 28.0f}, "Options", IM_COL32(246, 232, 192, 245));
        return;
    }

    const ScreenRect panel = toolOptionsPanelRect(app);
    auto& params = app.document->toolParams();
    drawList->AddText(ImVec2(panel.x + 22.0f, panel.y + 17.0f), IM_COL32(246, 232, 192, 250), "Tool options");
    drawOptionTag(drawList, insetRect(panel, panel.width - 62.0f, 14.0f, 40.0f, 28.0f), "Tab", false);
    drawList->AddText(ImVec2(panel.x + 22.0f, panel.y + 52.0f), IM_COL32(226, 206, 159, 240), toolName(params.tool, params.grade));

    if (params.tool == ToolKind::Pencil)
    {
        const bool sharp = pencilTipName(params.radiusPx, app.displayPixelsPerMm)[0] == 's';
        drawList->AddText(ImVec2(panel.x + 22.0f, panel.y + 70.0f), IM_COL32(198, 170, 122, 225), sharp ? "Tip: sharpened" : "Tip: blunt");
        drawOptionTag(drawList, optionChoiceRect(app, 0, 2, 84.0f), "Sharpen", sharp);
        drawOptionTag(drawList, optionChoiceRect(app, 1, 2, 84.0f), "Blunt", !sharp);
    }
    else if (params.tool == ToolKind::KneadedEraser)
    {
        drawList->AddText(ImVec2(panel.x + 22.0f, panel.y + 70.0f), IM_COL32(198, 170, 122, 225), "Shape");
        drawOptionTag(drawList, optionChoiceRect(app, 0, 2, 84.0f), "Blob", params.kneadedShape == KneadedEraserShape::Blob);
        drawOptionTag(drawList, optionChoiceRect(app, 1, 2, 84.0f), "Point", params.kneadedShape == KneadedEraserShape::Point);
        drawOptionTag(drawList, optionChoiceRect(app, 0, 2, 126.0f), "Edge", params.kneadedShape == KneadedEraserShape::Edge);
        drawOptionTag(drawList, optionChoiceRect(app, 1, 2, 126.0f), "Flat", params.kneadedShape == KneadedEraserShape::Flat);
        char load[48]{};
        std::snprintf(load, sizeof(load), "Graphite load %.0f%%", stats.kneadedEraserLoad * 100.0f);
        drawList->AddText(ImVec2(panel.x + 22.0f, panel.y + 160.0f), IM_COL32(198, 170, 122, 225), load);
        drawOptionTag(drawList, insetRect(panel, 22.0f, 178.0f, panel.width - 44.0f, 32.0f), "Clean kneaded eraser", false);
    }
    else
    {
        float load = 0.0f;
        if (params.tool == ToolKind::Tortillon) load = stats.tortillonLoad;
        if (params.tool == ToolKind::FanBrush) load = stats.fanBrushLoad;
        if (params.tool == ToolKind::PowderBrush) load = stats.powderBrushLoad;
        char description[64]{};
        if (params.tool == ToolKind::RegularEraser) std::snprintf(description, sizeof(description), "Broad cleanup tool");
        else if (params.tool == ToolKind::ElectricEraser) std::snprintf(description, sizeof(description), "Small clean clearing tool");
        else if (params.tool == ToolKind::GraphitePowder) std::snprintf(description, sizeof(description), "Loose powder source");
        else std::snprintf(description, sizeof(description), "Graphite load %.0f%%", load * 100.0f);
        drawList->AddText(ImVec2(panel.x + 22.0f, panel.y + 78.0f), IM_COL32(198, 170, 122, 225), description);
        if (params.tool != ToolKind::GraphitePowder)
        {
            drawOptionTag(drawList, insetRect(panel, 22.0f, 112.0f, panel.width - 44.0f, 34.0f), "Clean tool", false);
        }
    }

}

void drawCanvasLayersPanel(AppState& app, ImGuiWindowFlags panelFlags)
{
    const ScreenRect panel = canvasLayersPanelRect(app);
    ImGui::SetNextWindowPos(ImVec2(panel.x, panel.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel.width, panel.height), ImGuiCond_Always);
    ImGui::Begin("Canvas Layers", nullptr, panelFlags);

    ImGui::Text("Canvas layers");
    ImGui::Separator();
    ImGui::Checkbox("Grid overlay", &app.showGridOverlay);
    if (!app.showGridOverlay) ImGui::BeginDisabled();
    ImGui::TextUnformatted("Grid opacity");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##GridOpacity", &app.gridOpacity, 0.05f, 1.0f, "%.2f");
    ImGui::TextUnformatted("Grid spacing");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##GridSpacing", &app.gridSpacingPx, kCanvasLayerMinGridSpacingPx, kCanvasLayerMaxGridSpacingPx, "%.0f px");
    if (!app.showGridOverlay) ImGui::EndDisabled();

    ImGui::Separator();
    const ImVec2 halfButton(126.0f, 28.0f);
    const ImVec2 wideButton(260.0f, 28.0f);
    if (ImGui::Button("Load ref", halfButton))
    {
        app.pendingReferenceImageDialog = true;
    }
    ImGui::SameLine();
    const bool hasImage = app.referenceImage.textureId != nullptr;
    if (!hasImage) ImGui::BeginDisabled();
    if (ImGui::Button("Clear ref", halfButton))
    {
        app.referenceImage = {};
        app.referenceImagePath.clear();
        app.referenceImageVisible = false;
        app.referenceImageLoadFailed = false;
    }
    if (!hasImage) ImGui::EndDisabled();

    if (app.referenceImageLoadFailed)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.56f, 0.42f, 1.0f), "Image load failed");
    }
    else
    {
        ImGui::TextUnformatted(hasImage ? "Reference loaded" : "No reference loaded");
    }

    if (!hasImage) ImGui::BeginDisabled();
    ImGui::Checkbox("Show reference", &app.referenceImageVisible);
    ImGui::TextUnformatted("Reference opacity");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##ImageOpacity", &app.referenceImageOpacity, 0.05f, 1.0f, "%.2f");
    if (!hasImage) ImGui::EndDisabled();

    ImGui::Separator();
    if (ImGui::Button("Import as graphite", wideButton))
    {
        app.pendingSketchImportDialog = true;
    }
    if (app.sketchImportFailed)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.56f, 0.42f, 1.0f), "Graphite import failed");
    }
    else if (app.sketchImportSucceeded)
    {
        ImGui::TextUnformatted("Sketch imported as graphite");
    }
    else
    {
        ImGui::TextUnformatted("No graphite import yet");
    }

    ImGui::End();
}

void drawImGui(AppState& app)
{
    const auto stats = app.backend.stats();
    const auto history = app.document->historyStats();
    auto& params = app.document->toolParams();
    const auto& lastPacket = app.document->lastPacket();
    if (app.diagnostics.recording())
    {
        ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(560, 82), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.72f, 0.04f, 0.02f, 0.94f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.96f, 0.86f, 1.0f));
        ImGui::Begin("Input Recording Banner", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(1.35f);
        ImGui::Text("PEN INPUT RECORDING - press F10 to stop");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Text("Packets: %llu   RTS: %llu   WM: %llu",
            static_cast<unsigned long long>(app.diagnostics.packetCount()),
            static_cast<unsigned long long>(app.diagnostics.realTimeStylusPackets()),
            static_cast<unsigned long long>(app.diagnostics.wmPointerPackets()));
        ImGui::End();
        ImGui::PopStyleColor(2);
    }
    auto* drawList = ImGui::GetForegroundDrawList();
    drawStudioOptionLabels(app, stats);
    const StudioLayout trayLayout = trayPresentationLayout(app);
    const float visibleWidth = rolledVisibleWidth(app);

    if (app.trayHideTarget <= 0.5f && visibleWidth > 140.0f)
    {
        const TrayRect close = trayCloseRect(app);
        const float x = trayLayout.trayPos.x + close.x * trayLayout.trayScale;
        const float y = trayLayout.trayPos.y + close.y * trayLayout.trayScale;
        const float w = close.width * trayLayout.trayScale;
        const float h = close.height * trayLayout.trayScale;
        const ImVec2 min(x, y);
        const ImVec2 max(x + w, y + h);
        drawList->AddRectFilled(min, max, IM_COL32(34, 24, 17, 176), 8.0f * trayLayout.trayScale);
        drawList->AddRect(min, max, IM_COL32(238, 215, 172, 210), 8.0f * trayLayout.trayScale, 0, 2.0f);
        const float pad = 18.0f * trayLayout.trayScale;
        drawList->AddLine(ImVec2(x + pad, y + pad), ImVec2(x + w - pad, y + h - pad), IM_COL32(246, 232, 192, 245), 3.2f);
        drawList->AddLine(ImVec2(x + w - pad, y + pad), ImVec2(x + pad, y + h - pad), IM_COL32(246, 232, 192, 245), 3.2f);
    }

    const float arrowX = trayLayout.trayPos.x + std::max(46.0f, visibleWidth * trayLayout.trayScale - 36.0f);
    const float arrowY = trayLayout.trayPos.y - 18.0f * trayLayout.trayScale;
    if (app.trayRollTarget > 0.5f && app.trayHideTarget <= 0.5f)
    {
        const float size = 17.0f * trayLayout.trayScale;
        drawList->AddLine(ImVec2(arrowX - size * 0.45f, arrowY - size * 0.70f), ImVec2(arrowX + size * 0.45f, arrowY), IM_COL32(8, 8, 8, 240), 3.0f);
        drawList->AddLine(ImVec2(arrowX + size * 0.45f, arrowY), ImVec2(arrowX - size * 0.45f, arrowY + size * 0.70f), IM_COL32(8, 8, 8, 240), 3.0f);
    }

    if (app.hasLastClientPoint && (clientPointOverPaper(app, app.lastClientPoint) || (app.document && app.document->drawing())))
    {
        const auto& params = app.document->toolParams();
        const ImVec2 c(static_cast<float>(app.lastClientPoint.x), static_cast<float>(app.lastClientPoint.y));
        const float radius = cursorContactRadiusPx(params);
        drawList->AddCircle(c, radius, IM_COL32(255, 255, 255, 210), 40, 2.2f);
        drawList->AddCircle(c, radius, IM_COL32(24, 24, 24, 230), 40, 1.1f);
    }
    if (app.showInputTrace && app.visibleInputTrace.size() > 1)
    {
        const float displayScaleX = static_cast<float>(app.clientWidth) / static_cast<float>(std::max<std::uint32_t>(1, app.canvasWidth));
        const float displayScaleY = static_cast<float>(app.clientHeight) / static_cast<float>(std::max<std::uint32_t>(1, app.canvasHeight));
        for (std::size_t i = 1; i < app.visibleInputTrace.size(); ++i)
        {
            const auto& from = app.visibleInputTrace[i - 1];
            const auto& to = app.visibleInputTrace[i];
            drawList->AddLine(
                ImVec2(from.x * displayScaleX, from.y * displayScaleY),
                ImVec2(to.x * displayScaleX, to.y * displayScaleY),
                IM_COL32(0, 120, 255, 150),
            2.0f);
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.073f, 0.066f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.91f, 0.88f, 0.80f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.52f, 0.50f, 0.45f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.135f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.21f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.29f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.115f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.19f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.26f, 0.25f, 0.21f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.74f, 0.72f, 0.62f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.62f, 0.58f, 0.48f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.30f, 0.28f, 0.22f, 0.8f));

    const float rightX = std::max(760.0f, static_cast<float>(app.clientWidth) - 300.0f);
    const ImGuiWindowFlags panelFlags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    drawCanvasLayersPanel(app, panelFlags);

    if (app.showInputTrace)
    {
    ImGui::SetNextWindowPos(ImVec2(rightX, 276), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(286, 276), ImGuiCond_Always);
    ImGui::Begin("Paper And View", nullptr, panelFlags);
    ImGui::Text("Paper: %s", paperName(app.document->paperPreset()));
    const PaperPreset currentPaper = app.document->paperPreset();
    const ImVec2 paperButton(126, 28);
    const ImVec2 wideButton(260, 28);
    if (activeButton("Smooth", currentPaper == PaperPreset::SmoothBristol, paperButton)) queuePaperPreset(app, PaperPreset::SmoothBristol);
    ImGui::SameLine();
    if (activeButton("Vellum", currentPaper == PaperPreset::ColdPress, paperButton)) queuePaperPreset(app, PaperPreset::ColdPress);
    if (activeButton("Rough sketch", currentPaper == PaperPreset::RoughSketch, wideButton)) queuePaperPreset(app, PaperPreset::RoughSketch);
    ImGui::Separator();
    ImGui::Text("View: %s", viewName(app.document->debugView()));
    const DebugView currentView = app.document->debugView();
    if (activeButton("Display", currentView == DebugView::DisplayTone, paperButton)) queueDebugView(app, DebugView::DisplayTone);
    ImGui::SameLine();
    if (activeButton("Loose", currentView == DebugView::LooseGraphite, paperButton)) queueDebugView(app, DebugView::LooseGraphite);
    if (activeButton("Bound", currentView == DebugView::BoundGraphite, paperButton)) queueDebugView(app, DebugView::BoundGraphite);
    ImGui::SameLine();
    if (activeButton("Height", currentView == DebugView::PaperHeight, paperButton)) queueDebugView(app, DebugView::PaperHeight);
    if (activeButton("Roughness", currentView == DebugView::PaperRoughness, paperButton)) queueDebugView(app, DebugView::PaperRoughness);
    ImGui::SameLine();
    if (activeButton("Binding", currentView == DebugView::PaperBinding, paperButton)) queueDebugView(app, DebugView::PaperBinding);
    if (activeButton("Compaction", currentView == DebugView::Compaction, paperButton)) queueDebugView(app, DebugView::Compaction);
    ImGui::SameLine();
    if (activeButton("Damage", currentView == DebugView::Damage, paperButton)) queueDebugView(app, DebugView::Damage);
    if (activeButton("Sheen", currentView == DebugView::SurfaceSheen, wideButton)) queueDebugView(app, DebugView::SurfaceSheen);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(rightX, 568), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(286, 214), ImGuiCond_Always);
    ImGui::Begin("Input And Engine", nullptr, panelFlags);
    if (lastPacket.hasRotation)
    {
        ImGui::Text("Input: %s  %.0f%%  rot %.0f", sourceName(lastPacket.source), lastPacket.pressure * 100.0f, lastPacket.rotation * kRadiansToDegrees);
    }
    else
    {
        ImGui::Text("Input: %s  %.0f%%", sourceName(lastPacket.source), lastPacket.pressure * 100.0f);
    }
    ImGui::Text("Speed %.0f  CUDA %.3f ms", lastPacket.speed, stats.lastKernelMs);
    int mode = static_cast<int>(app.tabletMode);
    if (ImGui::RadioButton("Auto", mode == static_cast<int>(TabletInputMode::Auto))) mode = static_cast<int>(TabletInputMode::Auto);
    ImGui::SameLine();
    if (ImGui::RadioButton("Raw", mode == static_cast<int>(TabletInputMode::WindowsInkRawPointer))) mode = static_cast<int>(TabletInputMode::WindowsInkRawPointer);
    if (ImGui::RadioButton("RTS", mode == static_cast<int>(TabletInputMode::WindowsInkRealTimeStylus))) mode = static_cast<int>(TabletInputMode::WindowsInkRealTimeStylus);
    ImGui::SameLine();
    if (ImGui::RadioButton("Mouse", mode == static_cast<int>(TabletInputMode::MouseOnly))) mode = static_cast<int>(TabletInputMode::MouseOnly);
    if (mode < static_cast<int>(TabletInputMode::Auto) || mode > static_cast<int>(TabletInputMode::MouseOnly)) mode = static_cast<int>(TabletInputMode::WindowsInkRawPointer);
    app.tabletMode = static_cast<TabletInputMode>(mode);
    ImGui::Checkbox("Input trace overlay", &app.showInputTrace);
    if (activeButton(app.diagnostics.recording() ? "Stop input CSV" : "Record input CSV", app.diagnostics.recording(), wideButton))
    {
        app.diagnostics.toggle(kDiagnosticsPath);
    }
    ImGui::Text("Packets %llu  pressure %.0f-%.0f%%",
        static_cast<unsigned long long>(app.diagnostics.packetCount()),
        app.diagnostics.minPressure() * 100.0f,
        app.diagnostics.maxPressure() * 100.0f);
    ImGui::End();
    }

    ImGui::PopStyleColor(12);
    ImGui::PopStyleVar(4);
}

void presentFrame(HWND hwnd)
{
    auto* app = state(hwnd);
    if (!app) return;
    const auto presentLatestCanvas = [app]()
    {
        const auto stats = app->backend.stats();
        app->renderer.present(
            stats.lastSignaledCudaFenceValue,
            [app](D3D12Renderer& renderer)
            {
                drawCanvasLayers(*app, renderer);
                drawProductTray(*app, renderer);
            },
            [app]() { drawImGui(*app); });
    };
    presentLatestCanvas();
    if (app->pendingReferenceImageDialog)
    {
        app->pendingReferenceImageDialog = false;
        if (loadReferenceImage(hwnd, *app))
        {
            presentLatestCanvas();
        }
    }
    if (app->pendingSketchImportDialog)
    {
        app->pendingSketchImportDialog = false;
        if (importSketchAsGraphite(hwnd, *app))
        {
            presentLatestCanvas();
        }
    }
    if (app->pendingPaperPresetChange)
    {
        app->document->setPaperPreset(app->pendingPaperPreset);
        app->document->clear();
        app->pendingPaperPresetChange = false;
        app->pendingDebugViewChange = false;
        presentLatestCanvas();
    }
    else if (app->pendingDebugViewChange)
    {
        app->document->setDebugView(app->pendingDebugView);
        app->pendingDebugViewChange = false;
        presentLatestCanvas();
    }
}

void updateTitle(HWND hwnd)
{
    auto* app = state(hwnd);
    if (!app) return;
    const auto stats = app->backend.stats();
    const auto history = app->document->historyStats();
    const auto& params = app->document->toolParams();
    const auto& lastPacket = app->document->lastPacket();
    const auto realTimeStylusStatus = app->realTimeStylus.status();
    const wchar_t* source = L"mouse";
    if (lastPacket.source == InputSource::WmPointer) source = L"WM_POINTER";
    if (lastPacket.source == InputSource::RealTimeStylus) source = L"RTS";
    const wchar_t* tool = L"HB Pencil";
    if (params.tool == ToolKind::RegularEraser) tool = L"Regular Eraser";
    if (params.tool == ToolKind::KneadedEraser)
    {
        if (params.kneadedShape == KneadedEraserShape::Point) tool = L"Kneaded Eraser Point";
        else if (params.kneadedShape == KneadedEraserShape::Edge) tool = L"Kneaded Eraser Edge";
        else if (params.kneadedShape == KneadedEraserShape::Flat) tool = L"Kneaded Eraser Flat";
        else tool = L"Kneaded Eraser Blob";
    }
    if (params.tool == ToolKind::ElectricEraser) tool = L"Electric Eraser";
    if (params.tool == ToolKind::Tortillon) tool = L"Tortillon";
    if (params.tool == ToolKind::FanBrush) tool = L"Fan Brush";
    if (params.tool == ToolKind::PowderBrush) tool = L"Powder Brush";
    if (params.tool == ToolKind::GraphitePowder) tool = L"Graphite Powder";
    wchar_t title[256]{};
    const wchar_t* modeName = L"Auto";
    if (app->tabletMode == TabletInputMode::WindowsInkRealTimeStylus) modeName = L"WindowsInkRTS";
    if (app->tabletMode == TabletInputMode::WindowsInkRawPointer) modeName = L"WindowsInkRaw";
    if (app->tabletMode == TabletInputMode::MouseOnly) modeName = L"MouseOnly";
    std::swprintf(title, 256, L"%sGraphite - %s - CUDA %.3f ms - packets %llu - %.0f%% - %s - RTS E%d pkt %llu press %llu",
        app->diagnostics.recording() ? L"[RECORDING PEN INPUT] " : L"",
        modeName,
        stats.lastKernelMs,
        static_cast<unsigned long long>(stats.strokePackets),
        lastPacket.pressure * 100.0f,
        source,
        realTimeStylusStatus.enabled ? 1 : 0,
        static_cast<unsigned long long>(realTimeStylusStatus.packetCount),
        static_cast<unsigned long long>(realTimeStylusStatus.pressurePayloadPackets));
    SetWindowText(hwnd, title);
    {
        std::ofstream status(kRealTimeStylusStatusPath, std::ios::out | std::ios::trunc);
        if (status.is_open())
        {
            status << "comInitialized=" << (realTimeStylusStatus.comInitialized ? 1 : 0) << '\n'
                   << "objectCreated=" << (realTimeStylusStatus.objectCreated ? 1 : 0) << '\n'
                   << "hwndAssigned=" << (realTimeStylusStatus.hwndAssigned ? 1 : 0) << '\n'
                   << "desiredPacketDescriptionSet=" << (realTimeStylusStatus.desiredPacketDescriptionSet ? 1 : 0) << '\n'
                   << "pluginAdded=" << (realTimeStylusStatus.pluginAdded ? 1 : 0) << '\n'
                   << "enabled=" << (realTimeStylusStatus.enabled ? 1 : 0) << '\n'
                   << "packetCount=" << realTimeStylusStatus.packetCount << '\n'
                   << "pressurePayloadPackets=" << realTimeStylusStatus.pressurePayloadPackets << '\n'
                   << "packetPropertyCount=" << realTimeStylusStatus.packetPropertyCount << '\n'
                   << "xPropertyIndex=" << realTimeStylusStatus.xPropertyIndex << '\n'
                   << "yPropertyIndex=" << realTimeStylusStatus.yPropertyIndex << '\n'
                   << "pressurePropertyIndex=" << realTimeStylusStatus.pressurePropertyIndex << '\n'
                   << "xLogicalMin=" << realTimeStylusStatus.xLogicalMin << '\n'
                   << "xLogicalMax=" << realTimeStylusStatus.xLogicalMax << '\n'
                   << "yLogicalMin=" << realTimeStylusStatus.yLogicalMin << '\n'
                   << "yLogicalMax=" << realTimeStylusStatus.yLogicalMax << '\n'
                   << "pressureLogicalMin=" << realTimeStylusStatus.pressureLogicalMin << '\n'
                   << "pressureLogicalMax=" << realTimeStylusStatus.pressureLogicalMax << '\n'
                   << "inkToDeviceScaleX=" << realTimeStylusStatus.inkToDeviceScaleX << '\n'
                   << "inkToDeviceScaleY=" << realTimeStylusStatus.inkToDeviceScaleY << '\n'
                   << "lastRawX=" << realTimeStylusStatus.lastRawX << '\n'
                   << "lastRawY=" << realTimeStylusStatus.lastRawY << '\n'
                   << "lastRawPressure=" << realTimeStylusStatus.lastRawPressure << '\n'
                   << "failure=" << realTimeStylusStatus.failure << '\n';
        }
    }

    app->debugPanel.update(DebugPanelState{
        params.tool,
        params.grade,
        app->document->paperPreset(),
        lastPacket.source,
        lastPacket.pressure,
        lastPacket.speed,
        lastPacket.rotation,
        lastPacket.hasRotation,
        stats.lastKernelMs,
        stats.strokePackets,
        history.undoDepth,
        history.redoDepth,
        history.replayTrackedTiles,
        app->document->debugView(),
        stats.tileSize,
        stats.tileColumns,
        stats.tileRows,
        stats.activeTiles,
        stats.allocatedTiles,
        stats.allocatedMaterialPages,
        stats.materialPageCapacity,
        stats.lastTouchedTiles,
        stats.lastRenderUsedDirtyTiles,
        stats.lastRenderTiles,
        stats.lastRenderPixels,
        app->diagnostics.recording(),
        app->diagnostics.packetCount(),
        app->diagnostics.wmPointerPackets(),
        app->diagnostics.mousePackets(),
        app->diagnostics.minPressure(),
        app->diagnostics.maxPressure(),
        app->diagnostics.sawTilt(),
        app->diagnostics.sawRotation(),
        app->diagnostics.sawEraser(),
        app->diagnostics.sawBarrel(),
        stats.tortillonLoad,
        stats.fanBrushLoad,
        stats.powderBrushLoad,
        stats.kneadedEraserLoad,
        stats.averageLooseGraphite,
        stats.averageBoundGraphite,
        stats.averageDamage,
        stats.averageBinding,
        stats.averageSheen,
        stats.averageRoughness,
    });
}

void undo(HWND hwnd)
{
    auto* app = state(hwnd);
    if (!app || !app->document->undo()) return;
    presentFrame(hwnd);
    updateTitle(hwnd);
}

void redo(HWND hwnd)
{
    auto* app = state(hwnd);
    if (!app || !app->document->redo()) return;
    presentFrame(hwnd);
    updateTitle(hwnd);
}

void applyDebugCommand(HWND hwnd, DebugCommand command)
{
    auto* app = state(hwnd);
    if (!app) return;
    const bool keepClickedTraySelection = app->activeTrayLift == 0.0f && app->activeTrayCommand == command;

    switch (command)
    {
    case DebugCommand::Clear:
        app->document->clear();
        presentFrame(hwnd);
        break;
    case DebugCommand::Pencil4H:
        app->document->toolParams().tool = ToolKind::Pencil;
        app->document->toolParams().grade = pencilGradeForTrayIndex(keepClickedTraySelection && app->activeTrayPencilIndex >= 0 ? app->activeTrayPencilIndex : 0);
        if (!keepClickedTraySelection) activateTrayTool(*app, command, 0, kPencilSlots[0]);
        break;
    case DebugCommand::PencilHB:
        app->document->toolParams().tool = ToolKind::Pencil;
        app->document->toolParams().grade = pencilGradeForTrayIndex(keepClickedTraySelection && app->activeTrayPencilIndex >= 0 ? app->activeTrayPencilIndex : 4);
        if (!keepClickedTraySelection) activateTrayTool(*app, command, 4, kPencilSlots[4]);
        break;
    case DebugCommand::Pencil8B:
        app->document->toolParams().tool = ToolKind::Pencil;
        app->document->toolParams().grade = pencilGradeForTrayIndex(keepClickedTraySelection && app->activeTrayPencilIndex >= 0 ? app->activeTrayPencilIndex : 9);
        if (!keepClickedTraySelection) activateTrayTool(*app, command, 9, kPencilSlots[9]);
        break;
    case DebugCommand::RegularEraser:
        app->document->toolParams().tool = ToolKind::RegularEraser;
        if (!keepClickedTraySelection) activateTrayTool(*app, command, -1, kVinylEraserSlot);
        break;
    case DebugCommand::KneadedEraser:
        app->document->toolParams().tool = ToolKind::KneadedEraser;
        if (!keepClickedTraySelection) activateTrayTool(*app, command, -1, kKneadedEraserSlot);
        break;
    case DebugCommand::ElectricEraser:
        app->document->toolParams().tool = ToolKind::ElectricEraser;
        if (!keepClickedTraySelection) activateTrayTool(*app, command, -1, kElectricEraserSlot);
        break;
    case DebugCommand::Tortillon:
        app->document->toolParams().tool = ToolKind::Tortillon;
        if (!keepClickedTraySelection) activateTrayTool(*app, command, -1, kTortillonSlot);
        break;
    case DebugCommand::FanBrush:
        app->document->toolParams().tool = ToolKind::FanBrush;
        if (!keepClickedTraySelection) activateTrayTool(*app, command, -1, kFanBrushSlot);
        break;
    case DebugCommand::PowderBrush:
        app->document->toolParams().tool = ToolKind::PowderBrush;
        if (!keepClickedTraySelection) activateTrayTool(*app, command, -1, kPowderBrushSlot);
        break;
    case DebugCommand::GraphitePowder:
        app->document->toolParams().tool = ToolKind::GraphitePowder;
        if (!keepClickedTraySelection) activateTrayTool(*app, command, -1, kGraphitePowderSlot);
        break;
    case DebugCommand::CleanTool:
        app->document->cleanCurrentTool();
        break;
    case DebugCommand::KneadedShapeBlob:
        app->document->toolParams().tool = ToolKind::KneadedEraser;
        app->document->toolParams().kneadedShape = KneadedEraserShape::Blob;
        activateTrayTool(*app, DebugCommand::KneadedEraser, -1, kKneadedEraserSlot);
        break;
    case DebugCommand::KneadedShapePoint:
        app->document->toolParams().tool = ToolKind::KneadedEraser;
        app->document->toolParams().kneadedShape = KneadedEraserShape::Point;
        activateTrayTool(*app, DebugCommand::KneadedEraser, -1, kKneadedEraserSlot);
        break;
    case DebugCommand::KneadedShapeEdge:
        app->document->toolParams().tool = ToolKind::KneadedEraser;
        app->document->toolParams().kneadedShape = KneadedEraserShape::Edge;
        activateTrayTool(*app, DebugCommand::KneadedEraser, -1, kKneadedEraserSlot);
        break;
    case DebugCommand::KneadedShapeFlat:
        app->document->toolParams().tool = ToolKind::KneadedEraser;
        app->document->toolParams().kneadedShape = KneadedEraserShape::Flat;
        activateTrayTool(*app, DebugCommand::KneadedEraser, -1, kKneadedEraserSlot);
        break;
    case DebugCommand::PaperSmooth:
        app->document->setPaperPreset(PaperPreset::SmoothBristol);
        app->document->clear();
        presentFrame(hwnd);
        break;
    case DebugCommand::PaperColdPress:
        app->document->setPaperPreset(PaperPreset::ColdPress);
        app->document->clear();
        presentFrame(hwnd);
        break;
    case DebugCommand::PaperRough:
        app->document->setPaperPreset(PaperPreset::RoughSketch);
        app->document->clear();
        presentFrame(hwnd);
        break;
    case DebugCommand::Undo:
        undo(hwnd);
        return;
    case DebugCommand::Redo:
        redo(hwnd);
        return;
    case DebugCommand::ViewDisplayTone:
        app->document->setDebugView(DebugView::DisplayTone);
        presentFrame(hwnd);
        break;
    case DebugCommand::ViewLooseGraphite:
        app->document->setDebugView(DebugView::LooseGraphite);
        presentFrame(hwnd);
        break;
    case DebugCommand::ViewBoundGraphite:
        app->document->setDebugView(DebugView::BoundGraphite);
        presentFrame(hwnd);
        break;
    case DebugCommand::ViewPaperHeight:
        app->document->setDebugView(DebugView::PaperHeight);
        presentFrame(hwnd);
        break;
    case DebugCommand::ViewCompaction:
        app->document->setDebugView(DebugView::Compaction);
        presentFrame(hwnd);
        break;
    case DebugCommand::ViewDamage:
        app->document->setDebugView(DebugView::Damage);
        presentFrame(hwnd);
        break;
    case DebugCommand::ViewPaperBinding:
        app->document->setDebugView(DebugView::PaperBinding);
        presentFrame(hwnd);
        break;
    case DebugCommand::ViewSurfaceSheen:
        app->document->setDebugView(DebugView::SurfaceSheen);
        presentFrame(hwnd);
        break;
    case DebugCommand::ViewPaperRoughness:
        app->document->setDebugView(DebugView::PaperRoughness);
        presentFrame(hwnd);
        break;
    case DebugCommand::None:
        return;
    }

    if ((app->activeTrayCommand != DebugCommand::None && app->activeTrayLift < 1.0f) ||
        (app->returningTrayCommand != DebugCommand::None && app->returningTrayT < 1.0f))
    {
        SetTimer(hwnd, kAnimationTimerId, kAnimationTimerMs, nullptr);
    }
    presentFrame(hwnd);
    updateTitle(hwnd);
}

void drawSegment(HWND hwnd, const StrokePacket& next)
{
    auto* app = state(hwnd);
    if (!app) return;
    app->diagnostics.record(next, "move");
    app->document->submitStrokePacket(next);
    app->visibleInputTrace.push_back(next);
    while (app->visibleInputTrace.size() > 256) app->visibleInputTrace.pop_front();
    presentFrame(hwnd);
    updateTitle(hwnd);
}

void pushVisibleInputTrace(AppState& app, const StrokePacket& packet)
{
    app.visibleInputTrace.push_back(packet);
    while (app.visibleInputTrace.size() > 256) app.visibleInputTrace.pop_front();
}

float packetDistance(const StrokePacket& a, const StrokePacket& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

bool continuousStrokeNeighbors(const StrokePacket& a, const StrokePacket& b)
{
    return a.isTip && b.isTip && packetDistance(a, b) <= 28.0f;
}

void smoothTabletPacketBatch(AppState& app, std::vector<StrokePacket>& packets)
{
    if (packets.empty() || !app.document) return;
    const std::vector<StrokePacket> original = packets;
    const StrokePacket anchor = app.document->lastPacket();

    for (std::size_t i = 0; i < packets.size(); ++i)
    {
        if (!original[i].isTip) continue;
        float x = original[i].x * 0.56f;
        float y = original[i].y * 0.56f;
        float weight = 0.56f;

        const StrokePacket& previous = i > 0 ? original[i - 1] : anchor;
        if (continuousStrokeNeighbors(previous, original[i]))
        {
            x += previous.x * 0.22f;
            y += previous.y * 0.22f;
            weight += 0.22f;
        }

        if (i + 1 < original.size() && continuousStrokeNeighbors(original[i], original[i + 1]))
        {
            x += original[i + 1].x * 0.22f;
            y += original[i + 1].y * 0.22f;
            weight += 0.22f;
        }

        packets[i].x = x / weight;
        packets[i].y = y / weight;
    }

    StrokePacket previous = anchor;
    for (auto& packet : packets)
    {
        if (!packet.isTip) continue;
        const float dx = packet.x - previous.x;
        const float dy = packet.y - previous.y;
        const auto dtUs = packet.timestampUs > previous.timestampUs ? packet.timestampUs - previous.timestampUs : 0;
        const float dt = dtUs > 0 ? static_cast<float>(dtUs) / 1000000.0f : 0.004f;
        packet.velocityX = dx / dt;
        packet.velocityY = dy / dt;
        packet.speed = std::sqrt(packet.velocityX * packet.velocityX + packet.velocityY * packet.velocityY);
        packet.orientation = std::atan2(dy, dx);
        previous = packet;
    }
}

void processTabletPackets(HWND hwnd, std::vector<StrokePacket> packets)
{
    auto* app = state(hwnd);
    if (!app || packets.empty()) return;
    if (!app->document->drawing())
    {
        const auto& first = packets.front();
        if (first.isEraser)
        {
            app->document->toolParams().tool = ToolKind::RegularEraser;
        }
        app->document->beginStroke(first);
        app->visibleInputTrace.clear();
        app->visibleInputTrace.push_back(first);
        app->diagnostics.record(first, "begin");
    }
    if (app->document->drawing())
    {
        const auto anchor = app->document->lastPacket();
        std::vector<StrokePacket> freshPackets;
        freshPackets.reserve(packets.size());
        for (const auto& packet : packets)
        {
            if (!packet.isTip || packet.timestampUs > anchor.timestampUs)
            {
                freshPackets.push_back(packet);
            }
        }
        packets = std::move(freshPackets);
        if (packets.empty()) return;
    }
    smoothTabletPacketBatch(*app, packets);
    std::vector<StrokePacket> strokePackets;
    strokePackets.reserve(packets.size());
    bool strokeEnded = false;
    for (const auto& packet : packets)
    {
        if (!packet.isTip)
        {
            if (!strokePackets.empty())
            {
                app->document->submitStrokePackets(strokePackets);
                strokePackets.clear();
            }
            app->document->endStroke();
            strokeEnded = true;
            continue;
        }
        app->diagnostics.record(packet, "move");
        pushVisibleInputTrace(*app, packet);
        strokePackets.push_back(packet);
    }
    if (!strokePackets.empty())
    {
        app->document->submitStrokePackets(strokePackets);
    }
    if (strokeEnded || !strokePackets.empty())
    {
        presentFrame(hwnd);
        updateTitle(hwnd);
    }
}

bool handleGlobalKeyDown(HWND hwnd, WPARAM wParam)
{
    auto* app = state(hwnd);
    if (!app) return false;

    DebugCommand command = DebugCommand::None;
    if (wParam == 'C') command = DebugCommand::Clear;
    else if (wParam == '1') command = DebugCommand::Pencil4H;
    else if (wParam == '2') command = DebugCommand::PencilHB;
    else if (wParam == '3') command = DebugCommand::Pencil8B;
    else if (wParam == 'E') command = DebugCommand::RegularEraser;
    else if (wParam == 'K') command = DebugCommand::KneadedEraser;
    else if (wParam == 'B') command = DebugCommand::KneadedShapeBlob;
    else if (wParam == 'N') command = DebugCommand::KneadedShapePoint;
    else if (wParam == 'M') command = DebugCommand::KneadedShapeEdge;
    else if (wParam == 'V') command = DebugCommand::KneadedShapeFlat;
    else if (wParam == 'X') command = DebugCommand::ElectricEraser;
    else if (wParam == 'T') command = DebugCommand::Tortillon;
    else if (wParam == 'F') command = DebugCommand::FanBrush;
    else if (wParam == 'P') command = DebugCommand::PowderBrush;
    else if (wParam == 'G') command = DebugCommand::GraphitePowder;
    else if (wParam == 'O') command = DebugCommand::CleanTool;
    else if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) command = DebugCommand::Undo;
    else if (wParam == 'Y' && (GetKeyState(VK_CONTROL) & 0x8000)) command = DebugCommand::Redo;
    else if (wParam == VK_F1) command = DebugCommand::ViewDisplayTone;
    else if (wParam == VK_F2) command = DebugCommand::ViewLooseGraphite;
    else if (wParam == VK_F3) command = DebugCommand::ViewBoundGraphite;
    else if (wParam == VK_F4) command = DebugCommand::ViewPaperHeight;
    else if (wParam == VK_F5) command = DebugCommand::ViewCompaction;
    else if (wParam == VK_F6) command = DebugCommand::ViewDamage;
    else if (wParam == VK_F7) command = DebugCommand::ViewPaperBinding;
    else if (wParam == VK_F8) command = DebugCommand::ViewSurfaceSheen;
    else if (wParam == VK_F9) command = DebugCommand::ViewPaperRoughness;

    if (command != DebugCommand::None)
    {
        applyDebugCommand(hwnd, command);
        presentFrame(hwnd);
        updateTitle(hwnd);
        return true;
    }

    if (wParam == VK_F10)
    {
        app->diagnostics.toggle(kDiagnosticsPath);
        presentFrame(hwnd);
        updateTitle(hwnd);
        return true;
    }
    if (wParam == VK_F11)
    {
        cycleTabletMode(*app, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        presentFrame(hwnd);
        updateTitle(hwnd);
        return true;
    }
    if (wParam == VK_F12)
    {
        app->showInputTrace = !app->showInputTrace;
        presentFrame(hwnd);
        updateTitle(hwnd);
        return true;
    }

    return false;
}

bool tickTrayAnimation(AppState& app)
{
    bool changed = false;
    if (app.activeTrayCommand != DebugCommand::None && app.activeTrayLift < 1.0f)
    {
        app.activeTrayLift = std::min(1.0f, app.activeTrayLift + 0.11f);
        changed = true;
    }
    if (app.returningTrayCommand != DebugCommand::None && app.returningTrayT < 1.0f)
    {
        app.returningTrayT = std::min(1.0f, app.returningTrayT + 0.12f);
        changed = true;
    }
    const float rollDelta = app.trayRollTarget - app.trayRoll;
    if (std::fabs(rollDelta) > 0.0015f)
    {
        app.trayRoll += rollDelta * 0.22f;
        changed = true;
    }
    else if (app.trayRoll != app.trayRollTarget)
    {
        app.trayRoll = app.trayRollTarget;
        changed = true;
    }
    const float hideDelta = app.trayHideTarget - app.trayHide;
    if (std::fabs(hideDelta) > 0.0015f)
    {
        app.trayHide += hideDelta * 0.22f;
        changed = true;
    }
    else if (app.trayHide != app.trayHideTarget)
    {
        app.trayHide = app.trayHideTarget;
        changed = true;
    }
    return changed;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* app = state(hwnd);
    if (app && uiPointerMessage(message))
    {
        POINT point{};
        if (clientPointFromMessage(hwnd, message, wParam, lParam, point))
        {
            app->lastClientPoint = point;
            app->hasLastClientPoint = true;
        }
    }
    if (message == WM_KEYDOWN && handleGlobalKeyDown(hwnd, wParam))
    {
        return 0;
    }

    if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT)
    {
        POINT screenPoint{};
        GetCursorPos(&screenPoint);
        POINT clientPoint = screenPoint;
        ScreenToClient(hwnd, &clientPoint);
        if (app && app->document && app->document->drawing())
        {
            SetCursor(nullptr);
        }
        else if (app && clientPointOverUi(*app, clientPoint))
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
        }
        else if (app && clientPointOverPaper(*app, clientPoint))
        {
            SetCursor(nullptr);
        }
        else
        {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
        return TRUE;
    }

    if (app && message == WM_TIMER && wParam == kAnimationTimerId)
    {
        if (tickTrayAnimation(*app))
        {
            presentFrame(hwnd);
            updateTitle(hwnd);
            return 0;
        }
        KillTimer(hwnd, kAnimationTimerId);
        return 0;
    }

    if (app && message == WM_MOUSEWHEEL)
    {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);
        if ((app->trayHideTarget <= 0.5f && app->trayHide <= 0.5f) && pointOverTrayDragHandle(*app, point))
        {
            const float wheel = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
            app->trayRollTarget = std::min(1.0f, std::max(0.0f, app->trayRollTarget - wheel * 0.22f));
            SetTimer(hwnd, kAnimationTimerId, kAnimationTimerMs, nullptr);
            presentFrame(hwnd);
            updateTitle(hwnd);
            return 0;
        }
    }

    if (app && app->trayDragging && (message == WM_POINTERUPDATE || message == WM_MOUSEMOVE))
    {
        if (message == WM_POINTERUPDATE && app->trayDragPointerId != GET_POINTERID_WPARAM(wParam)) return 0;
        POINT point{};
        if (clientPointFromMessage(hwnd, message, wParam, lParam, point))
        {
            moveTrayByPixels(*app, point.x - app->trayDragLastPoint.x, point.y - app->trayDragLastPoint.y);
            app->trayDragLastPoint = point;
            presentFrame(hwnd);
            updateTitle(hwnd);
        }
        return 0;
    }

    if (app && app->hiddenTabDragging && (message == WM_POINTERUPDATE || message == WM_MOUSEMOVE))
    {
        if (message == WM_POINTERUPDATE && app->trayDragPointerId != GET_POINTERID_WPARAM(wParam)) return 0;
        POINT point{};
        if (clientPointFromMessage(hwnd, message, wParam, lParam, point))
        {
            if (std::abs(point.x - app->trayDragLastPoint.x) > kHiddenTrayDragThreshold ||
                std::abs(point.y - app->trayDragLastPoint.y) > kHiddenTrayDragThreshold)
            {
                app->hiddenTabMoved = true;
            }
            dockHiddenTabNearPoint(*app, point);
            presentFrame(hwnd);
            updateTitle(hwnd);
        }
        return 0;
    }

    if (app && (app->trayDragging || app->hiddenTabDragging) && (message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED || message == WM_LBUTTONUP || message == WM_CANCELMODE))
    {
        const bool openHiddenTray = app->hiddenTabDragging && !app->hiddenTabMoved && (message == WM_POINTERUP || message == WM_LBUTTONUP);
        app->trayDragging = false;
        app->hiddenTabDragging = false;
        app->hiddenTabMoved = false;
        app->trayDragPointerId = 0;
        ReleaseCapture();
        if (openHiddenTray)
        {
            app->trayHideTarget = 0.0f;
            SetTimer(hwnd, kAnimationTimerId, kAnimationTimerMs, nullptr);
        }
        presentFrame(hwnd);
        updateTitle(hwnd);
        return 0;
    }

    if (app && (message == WM_POINTERDOWN || message == WM_LBUTTONDOWN))
    {
        POINT point{};
        if (clientPointFromMessage(hwnd, message, wParam, lParam, point))
        {
            if ((app->trayHideTarget > 0.5f || app->trayHide > 0.5f) && pointOverHiddenTrayTab(*app, point))
            {
                app->hiddenTabDragging = true;
                app->hiddenTabMoved = false;
                app->trayDragPointerId = message == WM_POINTERDOWN ? GET_POINTERID_WPARAM(wParam) : 0;
                app->trayDragLastPoint = point;
                SetCapture(hwnd);
                return 0;
            }
            const float oldRollTarget = app->trayRollTarget;
            const float oldHideTarget = app->trayHideTarget;
            DebugCommand studioCommand = DebugCommand::None;
            if (handleStudioOptionPoint(*app, point, studioCommand))
            {
                if (studioCommand != DebugCommand::None)
                {
                    applyDebugCommand(hwnd, studioCommand);
                }
                presentFrame(hwnd);
                updateTitle(hwnd);
                return 0;
            }
            const DebugCommand trayCommand = trayCommandFromPoint(*app, point);
            if (oldRollTarget != app->trayRollTarget || oldHideTarget != app->trayHideTarget)
            {
                SetTimer(hwnd, kAnimationTimerId, kAnimationTimerMs, nullptr);
                presentFrame(hwnd);
                updateTitle(hwnd);
                return 0;
            }
            if (trayCommand != DebugCommand::None)
            {
                applyDebugCommand(hwnd, trayCommand);
                presentFrame(hwnd);
                updateTitle(hwnd);
                return 0;
            }
            if (pointOverTrayDragHandle(*app, point))
            {
                app->trayDragging = true;
                app->trayDragPointerId = message == WM_POINTERDOWN ? GET_POINTERID_WPARAM(wParam) : 0;
                app->trayDragLastPoint = point;
                SetCapture(hwnd);
                return 0;
            }
        }
    }

    if (app && acceptsRealTimeStylus(app->tabletMode) && message == kRealTimeStylusPacketMessage)
    {
        processTabletPackets(hwnd, app->realTimeStylus.drainPackets(hwnd));
        return 0;
    }

    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
        POINT point{};
        const bool overUi = app && uiPointerMessage(message) && clientPointFromMessage(hwnd, message, wParam, lParam, point) && clientPointOverUi(*app, point);
        if (uiPointerMessage(message) && (ImGui::GetIO().WantCaptureMouse || overUi))
        {
            if (app && overUi)
            {
                presentFrame(hwnd);
                updateTitle(hwnd);
            }
            // With EnableMouseInPointer(TRUE), mouse input arrives as WM_POINTER.
            // ImGui's Win32 backend only consumes legacy mouse messages, and
            // Windows only synthesizes those when pointer messages reach
            // DefWindowProc. Returning 1 here swallowed the message and made
            // the whole ImGui panel unclickable.
            return DefWindowProc(hwnd, message, wParam, lParam);
        }
    }

    if (message == WM_POINTERDOWN && app && acceptsRawWindowsInkPointer(*app))
    {
        if (!app) return 0;
        auto packets = app->input.packetsFromPointer(hwnd, wParam);
        if (packets.empty()) return 0;
        const auto& packet = packets.back();
        app->activePointerId = GET_POINTERID_WPARAM(wParam);
        if (packet.isEraser)
        {
            app->document->toolParams().tool = ToolKind::RegularEraser;
        }
        app->document->beginStroke(packet);
        app->visibleInputTrace.clear();
        app->visibleInputTrace.push_back(packet);
        app->diagnostics.record(packet, "begin");
        SetCapture(hwnd);
        updateTitle(hwnd);
        return 0;
    }

    if (message == WM_POINTERUPDATE && app && acceptsRawWindowsInkPointer(*app))
    {
        if (!app || !app->document->drawing() || app->activePointerId != GET_POINTERID_WPARAM(wParam)) return 0;
        processTabletPackets(hwnd, app->input.packetsFromPointer(hwnd, wParam));
        return 0;
    }

    if ((message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) && app && acceptsRawWindowsInkPointer(*app))
    {
        if (app)
        {
            app->document->endStroke();
            app->activePointerId = 0;
        }
        ReleaseCapture();
        return 0;
    }

    switch (message)
    {
    case WM_CREATE:
        return 0;
    case WM_COMMAND:
    {
        auto* app = state(hwnd);
        if (!app) break;
        applyDebugCommand(hwnd, app->debugPanel.commandFromControl(wParam));
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        auto* app = state(hwnd);
        if (!app || !acceptsMouse(app->tabletMode)) break;
        SetCapture(hwnd);
        if (app->document->toolParams().tool == ToolKind::RegularEraser)
        {
            app->document->toolParams().tool = ToolKind::Pencil;
        }
        if (auto packet = app->input.packetFromMouse(hwnd, lParam, 0.65f, false))
        {
            app->document->beginStroke(*packet);
            app->visibleInputTrace.clear();
            app->visibleInputTrace.push_back(*packet);
            app->diagnostics.record(*packet, "begin");
        }
        return 0;
    }
    case WM_RBUTTONDOWN:
    {
        auto* app = state(hwnd);
        if (!app || !acceptsMouse(app->tabletMode)) break;
        SetCapture(hwnd);
        app->document->toolParams().tool = ToolKind::RegularEraser;
        if (auto packet = app->input.packetFromMouse(hwnd, lParam, 0.85f, true))
        {
            app->document->beginStroke(*packet);
            app->visibleInputTrace.clear();
            app->visibleInputTrace.push_back(*packet);
            app->diagnostics.record(*packet, "begin");
        }
        updateTitle(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        auto* app = state(hwnd);
        if (!app || !acceptsMouse(app->tabletMode) || !app->document->drawing()) break;
        const bool eraser = app->document->toolParams().tool == ToolKind::RegularEraser;
        if (auto packet = app->input.packetFromMouse(hwnd, lParam, eraser ? 0.85f : 0.65f, eraser))
        {
            drawSegment(hwnd, *packet);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_CANCELMODE:
    {
        auto* app = state(hwnd);
        if (app)
        {
            app->document->endStroke();
        }
        ReleaseCapture();
        return 0;
    }
    case WM_KEYDOWN:
    {
        break;
    }
    case WM_DESTROY:
    {
        auto* app = state(hwnd);
        if (app)
        {
            app->diagnostics.stop();
            app->realTimeStylus.shutdown();
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    EnableMouseInPointer(TRUE);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(comResult);
    useRepoRootForUiAssets();

    const wchar_t* className = L"GraphiteEngineSliceWindow";
    WNDCLASS wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc);

    const RECT workRect = primaryMonitorWorkRect();
    const int windowX = workRect.left;
    const int windowY = workRect.top;
    const int windowWidth = std::max<LONG>(1024, workRect.right - workRect.left);
    const int windowHeight = std::max<LONG>(720, workRect.bottom - workRect.top);

    HWND hwnd = CreateWindowEx(
        0,
        className,
        L"Graphite Native App",
        WS_OVERLAPPEDWINDOW,
        windowX,
        windowY,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!hwnd) return 1;

    auto app = std::make_unique<AppState>();
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app.get()));

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    app->clientWidth = static_cast<std::uint32_t>(std::max<LONG>(1, clientRect.right - clientRect.left));
    app->clientHeight = static_cast<std::uint32_t>(std::max<LONG>(1, clientRect.bottom - clientRect.top));
    app->canvasWidth = app->clientWidth;
    app->canvasHeight = app->clientHeight;
    app->displayPixelsPerMm = displayPixelsPerMm(hwnd);
    app->input.setCanvasSize(app->canvasWidth, app->canvasHeight);
    app->realTimeStylus.setCanvasSize(app->canvasWidth, app->canvasHeight);

    if (!app->renderer.initialize(hwnd, app->canvasWidth, app->canvasHeight)) return 2;
    app->pencil4H = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-4h-canvas.png");
    app->pencil3H = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-3h-canvas.png");
    app->pencil2H = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-2h-canvas.png");
    app->pencilH = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-h-canvas.png");
    app->pencilHB = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-hb-canvas.png");
    app->pencilB = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-b-canvas.png");
    app->pencil2B = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-2b-canvas.png");
    app->pencil4B = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-4b-canvas.png");
    app->pencil6B = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-6b-canvas.png");
    app->pencil8B = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\pencil-8b-canvas.png");
    app->trayBackground = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\imagegen-roll-case-base-no-pocket-fronts.png");
    app->vinylEraser = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\vinyl-eraser-rot90.png");
    app->kneadedEraser = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\kneaded-eraser.png");
    app->electricEraser = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\electric-eraser-horizontal.png");
    app->tortillon = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\tortillon-horizontal.png");
    app->fanBrush = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\fan-brush-upright.png");
    app->powderBrush = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\powder-brush-upright.png");
    app->graphitePowder = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\graphite-powder.png");
    app->pencilStrap = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\kit-elastic-band.png");
    app->rolledLeatherOutside = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\rolled-leather-outside.png");
    app->tortillonStrapLeft = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\strap-tortillon-left.png");
    app->tortillonStrapRight = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\strap-tortillon-right.png");
    app->vinylEraserStrap = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\strap-vinyl-eraser.png");
    app->electricEraserStrap = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\strap-electric-eraser.png");
    app->fanBrushStrap = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\strap-fan-brush.png");
    app->powderBrushStrap = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\strap-powder-brush.png");
    app->leatherSideTab = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\leather-side-tab.png");
    app->leatherSideTabLeft = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\leather-side-tab-left.png");
    app->leatherSideTabTop = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\leather-side-tab-top.png");
    app->leatherSideTabBottom = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\leather-side-tab-bottom.png");
    app->leatherPencilPocket = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\imagegen-pencil-pocket-row-front.png");
    app->leatherEraserPocket = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\imagegen-eraser-pocket-front.png");
    app->leatherLongToolPocket = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\leather-long-tool-pocket.png");
    app->imagegenToolOptionsPanel = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\imagegen-tool-options-panel-trimmed.png");
    app->imagegenPaperPickerPanel = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\imagegen-paper-picker-panel-trimmed.png");
    app->imagegenOptionTag = app->renderer.loadUiTexture(L"native\\Graphite.EngineSlice\\assets\\ui\\imagegen-option-tag-trimmed.png");
    app->whitePixel = app->renderer.createSolidUiTexture(255, 255, 255, 255);
    if (!app->backend.initialize(GraphiteBackendInit{app->canvasWidth, app->canvasHeight, app->renderer.sharedCanvasHandle(), app->renderer.sharedCanvasBytes(), app->renderer.cudaFenceHandle()})) return 3;
    app->document = std::make_unique<GraphiteDocument>(app->backend);
    app->document->toolParams().radiusPx = kSharpenedPencilContactRadiusMm * app->displayPixelsPerMm;
    if (std::getenv("GRAPHITE_SHOW_LEGACY_PANEL") && !app->debugPanel.create(hwnd)) return 4;

    if (std::getenv("GRAPHITE_ENABLE_RTS_INPUT"))
    {
        app->realTimeStylus.initialize(hwnd);
    }
    if (std::getenv("GRAPHITE_AUTO_RECORD_INPUT"))
    {
        app->diagnostics.start(kDiagnosticsPath);
    }
    presentFrame(hwnd);
    updateTitle(hwnd);

    ShowWindow(hwnd, showCommand);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && handleGlobalKeyDown(hwnd, msg.wParam))
        {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    app->renderer.shutdown();
    app->backend.shutdown();
    app->realTimeStylus.shutdown();
    if (comInitialized)
    {
        CoUninitialize();
    }
    return 0;
}
