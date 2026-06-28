#include "debug_panel.h"

#include <cwchar>

namespace
{
constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr int kClearId = 5001;
constexpr int kPencil4HId = 5002;
constexpr int kPencilHBId = 5003;
constexpr int kPencil8BId = 5004;
constexpr int kRegularEraserId = 5005;
constexpr int kKneadedEraserId = 5006;
constexpr int kTortillonId = 5007;
constexpr int kFanBrushId = 5008;
constexpr int kPowderBrushId = 5009;
constexpr int kGraphitePowderId = 5026;
constexpr int kElectricEraserId = 5025;
constexpr int kCleanToolId = 5024;
constexpr int kPaperSmoothId = 5010;
constexpr int kPaperColdId = 5011;
constexpr int kPaperRoughId = 5012;
constexpr int kUndoId = 5013;
constexpr int kRedoId = 5014;
constexpr int kViewDisplayId = 5015;
constexpr int kViewLooseId = 5016;
constexpr int kViewBoundId = 5017;
constexpr int kViewPaperId = 5018;
constexpr int kViewCompactionId = 5019;
constexpr int kViewDamageId = 5020;
constexpr int kViewBindingId = 5021;
constexpr int kViewSheenId = 5022;
constexpr int kViewRoughnessId = 5023;

const wchar_t* toolName(ToolKind tool, PencilGrade grade)
{
    if (tool == ToolKind::RegularEraser) return L"Regular eraser";
    if (tool == ToolKind::KneadedEraser) return L"Kneaded eraser";
    if (tool == ToolKind::ElectricEraser) return L"Electric eraser";
    if (tool == ToolKind::Tortillon) return L"Tortillon";
    if (tool == ToolKind::FanBrush) return L"Fan brush";
    if (tool == ToolKind::PowderBrush) return L"Powder brush";
    if (tool == ToolKind::GraphitePowder) return L"Graphite powder";
    if (grade == PencilGrade::FourH) return L"Pencil 4H";
    if (grade == PencilGrade::EightB) return L"Pencil 8B";
    return L"Pencil HB";
}

const wchar_t* sourceName(InputSource source)
{
    if (source == InputSource::WmPointer) return L"WM_POINTER";
    if (source == InputSource::RealTimeStylus) return L"RealTimeStylus";
    return L"Mouse fallback";
}

const wchar_t* paperName(PaperPreset preset)
{
    if (preset == PaperPreset::SmoothBristol) return L"Smooth bristol";
    if (preset == PaperPreset::RoughSketch) return L"Rough sketch paper";
    return L"Vellum drawing paper";
}

const wchar_t* viewName(DebugView view)
{
    if (view == DebugView::LooseGraphite) return L"Loose graphite";
    if (view == DebugView::BoundGraphite) return L"Bound graphite";
    if (view == DebugView::PaperHeight) return L"Paper height";
    if (view == DebugView::Compaction) return L"Compaction";
    if (view == DebugView::Damage) return L"Damage";
    if (view == DebugView::PaperBinding) return L"Paper binding";
    if (view == DebugView::SurfaceSheen) return L"Surface sheen";
    if (view == DebugView::PaperRoughness) return L"Paper roughness";
    return L"Display tone";
}
}

bool DebugPanel::create(HWND parent)
{
    panel_ = CreateWindowEx(
        0,
        L"STATIC",
        nullptr,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        980,
        16,
        300,
        940,
        parent,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr);
    if (!panel_) return false;

    addLabel(panel_, 0, 0, 280, 24, L"GRAPHITE ENGINE");
    status_ = addLabel(panel_, 0, 34, 280, 28, L"Status");
    tool_ = addLabel(panel_, 0, 66, 280, 28, L"Tool");
    input_ = addLabel(panel_, 0, 98, 280, 28, L"Input");
    backend_ = addLabel(panel_, 0, 130, 280, 28, L"Backend");
    history_ = addLabel(panel_, 0, 162, 280, 28, L"History");
    view_ = addLabel(panel_, 0, 194, 280, 28, L"View");
    tiles_ = addLabel(panel_, 0, 226, 280, 28, L"Tiles");
    diagnostics_ = addLabel(panel_, 0, 258, 280, 28, L"Input diag");
    loads_ = addLabel(panel_, 0, 290, 280, 28, L"Tool loads");
    damage_ = addLabel(panel_, 0, 322, 280, 28, L"Damage");

    addLabel(panel_, 0, 364, 280, 22, L"Pencil grades");
    addButton(panel_, kPencil4HId, 0, 392, 86, 32, L"4H");
    addButton(panel_, kPencilHBId, 96, 392, 86, 32, L"HB");
    addButton(panel_, kPencil8BId, 192, 392, 86, 32, L"8B");

    addLabel(panel_, 0, 446, 280, 22, L"Material tools");
    addButton(panel_, kRegularEraserId, 0, 474, 132, 34, L"Regular erase");
    addButton(panel_, kKneadedEraserId, 146, 474, 132, 34, L"Kneaded");
    addButton(panel_, kElectricEraserId, 0, 518, 132, 34, L"Electric erase");
    addButton(panel_, kTortillonId, 146, 518, 132, 34, L"Tortillon");
    addButton(panel_, kFanBrushId, 0, 562, 132, 34, L"Fan brush");
    addButton(panel_, kPowderBrushId, 146, 562, 132, 34, L"Powder");
    addButton(panel_, kGraphitePowderId, 0, 606, 132, 34, L"Graphite powder");
    addButton(panel_, kCleanToolId, 146, 606, 132, 34, L"Clean tool");
    addButton(panel_, kClearId, 0, 650, 62, 34, L"Clear");
    addButton(panel_, kUndoId, 72, 650, 62, 34, L"Undo");
    addButton(panel_, kRedoId, 144, 650, 62, 34, L"Redo");

    addLabel(panel_, 0, 698, 280, 22, L"Paper preset");
    addButton(panel_, kPaperSmoothId, 0, 724, 86, 30, L"Smooth");
    addButton(panel_, kPaperColdId, 96, 724, 86, 30, L"Cold");
    addButton(panel_, kPaperRoughId, 192, 724, 86, 30, L"Rough");

    addLabel(panel_, 0, 762, 280, 22, L"Buffer view");
    addButton(panel_, kViewDisplayId, 0, 786, 132, 28, L"Display");
    addButton(panel_, kViewLooseId, 146, 786, 132, 28, L"Loose");
    addButton(panel_, kViewBoundId, 0, 820, 132, 28, L"Bound");
    addButton(panel_, kViewPaperId, 146, 820, 132, 28, L"Paper");
    addButton(panel_, kViewCompactionId, 0, 854, 132, 28, L"Compaction");
    addButton(panel_, kViewDamageId, 146, 854, 132, 28, L"Damage");
    addButton(panel_, kViewBindingId, 0, 888, 86, 28, L"Binding");
    addButton(panel_, kViewSheenId, 96, 888, 86, 28, L"Sheen");
    addButton(panel_, kViewRoughnessId, 192, 888, 86, 28, L"Rough");
    return true;
}

void DebugPanel::update(const DebugPanelState& state)
{
    if (!panel_) return;

    wchar_t buffer[256]{};
    std::swprintf(buffer, 256, L"Native CUDA + D3D12  Windows Ink input");
    SetWindowText(status_, buffer);

    std::swprintf(buffer, 256, L"Tool: %s  paper %s", toolName(state.tool, state.grade), paperName(state.paperPreset));
    SetWindowText(tool_, buffer);

    if (state.hasRotation)
    {
        std::swprintf(buffer, 256, L"Input: %s  pressure %.0f%%  speed %.0f  rot %.0f", sourceName(state.source), state.pressure * 100.0f, state.speed, state.rotation * kRadiansToDegrees);
    }
    else
    {
        std::swprintf(buffer, 256, L"Input: %s  pressure %.0f%%  speed %.0f", sourceName(state.source), state.pressure * 100.0f, state.speed);
    }
    SetWindowText(input_, buffer);

    std::swprintf(buffer, 256, L"CUDA: %.3f ms  packets %llu", state.lastKernelMs, static_cast<unsigned long long>(state.packets));
    SetWindowText(backend_, buffer);

    std::swprintf(buffer, 256, L"History: undo %u  redo %u  replay tiles %u", state.undoDepth, state.redoDepth, state.replayTrackedTiles);
    SetWindowText(history_, buffer);

    std::swprintf(buffer, 256, L"View: %s", viewName(state.debugView));
    SetWindowText(view_, buffer);

    const auto totalTiles = state.tileColumns * state.tileRows;
    std::swprintf(buffer, 256, L"Tiles: %u/%u active  pages %u/%u  %s %u", state.activeTiles, totalTiles, state.allocatedMaterialPages, state.materialPageCapacity, state.lastRenderUsedDirtyTiles ? L"dirty" : L"full", state.lastRenderTiles);
    SetWindowText(tiles_, buffer);

    std::swprintf(buffer, 256, L"Diag: %s %llu  WM %llu  P %.0f-%.0f%s%s%s%s",
        state.diagnosticsRecording ? L"rec" : L"off",
        static_cast<unsigned long long>(state.diagnosticsPackets),
        static_cast<unsigned long long>(state.diagnosticsWmPointerPackets),
        state.diagnosticsMinPressure * 100.0f,
        state.diagnosticsMaxPressure * 100.0f,
        state.diagnosticsSawTilt ? L" tilt" : L"",
        state.diagnosticsSawRotation ? L" rot" : L"",
        state.diagnosticsSawEraser ? L" era" : L"",
        state.diagnosticsSawBarrel ? L" btn" : L"");
    SetWindowText(diagnostics_, buffer);

    std::swprintf(buffer, 256, L"Loads: T %.2f  F %.2f  P %.2f  K %.2f", state.tortillonLoad, state.fanBrushLoad, state.powderBrushLoad, state.kneadedEraserLoad);
    SetWindowText(loads_, buffer);

    std::swprintf(buffer, 256, L"Graphite L %.4f B %.4f  damage %.4f", state.averageLooseGraphite, state.averageBoundGraphite, state.averageDamage);
    SetWindowText(damage_, buffer);
}

DebugCommand DebugPanel::commandFromControl(WPARAM wParam) const
{
    if (HIWORD(wParam) != BN_CLICKED) return DebugCommand::None;
    switch (LOWORD(wParam))
    {
    case kClearId: return DebugCommand::Clear;
    case kPencil4HId: return DebugCommand::Pencil4H;
    case kPencilHBId: return DebugCommand::PencilHB;
    case kPencil8BId: return DebugCommand::Pencil8B;
    case kRegularEraserId: return DebugCommand::RegularEraser;
    case kKneadedEraserId: return DebugCommand::KneadedEraser;
    case kElectricEraserId: return DebugCommand::ElectricEraser;
    case kTortillonId: return DebugCommand::Tortillon;
    case kFanBrushId: return DebugCommand::FanBrush;
    case kPowderBrushId: return DebugCommand::PowderBrush;
    case kGraphitePowderId: return DebugCommand::GraphitePowder;
    case kCleanToolId: return DebugCommand::CleanTool;
    case kPaperSmoothId: return DebugCommand::PaperSmooth;
    case kPaperColdId: return DebugCommand::PaperColdPress;
    case kPaperRoughId: return DebugCommand::PaperRough;
    case kUndoId: return DebugCommand::Undo;
    case kRedoId: return DebugCommand::Redo;
    case kViewDisplayId: return DebugCommand::ViewDisplayTone;
    case kViewLooseId: return DebugCommand::ViewLooseGraphite;
    case kViewBoundId: return DebugCommand::ViewBoundGraphite;
    case kViewPaperId: return DebugCommand::ViewPaperHeight;
    case kViewCompactionId: return DebugCommand::ViewCompaction;
    case kViewDamageId: return DebugCommand::ViewDamage;
    case kViewBindingId: return DebugCommand::ViewPaperBinding;
    case kViewSheenId: return DebugCommand::ViewSurfaceSheen;
    case kViewRoughnessId: return DebugCommand::ViewPaperRoughness;
    default: return DebugCommand::None;
    }
}

HWND DebugPanel::addLabel(HWND parent, int x, int y, int width, int height, const wchar_t* text)
{
    return CreateWindowEx(
        0,
        L"STATIC",
        text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x,
        y,
        width,
        height,
        parent,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr);
}

HWND DebugPanel::addButton(HWND parent, int id, int x, int y, int width, int height, const wchar_t* text)
{
    return CreateWindowEx(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr),
        nullptr);
}
