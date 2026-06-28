#include "wintab_adapter.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <memory>
#include <string>

namespace
{
using HCTX = HANDLE;
constexpr UINT kPacketMessage = WM_USER + 0x7FF;
constexpr UINT kWtiDefSysCtx = 4;
constexpr UINT kCtxOptions = 0;
constexpr UINT kCxoMessages = 0x0004;
constexpr UINT kCxoCsrMessages = 0x0008;
constexpr UINT kPkX = 0x0080;
constexpr UINT kPkY = 0x0100;
constexpr UINT kPkButtons = 0x0040;
constexpr UINT kPkNormalPressure = 0x0400;
constexpr UINT kPkTime = 0x0020;
constexpr UINT kPkCursor = 0x8000;
constexpr UINT kPkOrientation = 0x1000;
constexpr float kPi = 3.14159265358979323846f;

struct Axis
{
    LONG axMin = 0;
    LONG axMax = 0;
    UINT axUnits = 0;
    DWORD axResolution = 0;
};

struct LogContextW
{
    wchar_t lcName[40]{};
    UINT lcOptions = 0;
    UINT lcStatus = 0;
    UINT lcLocks = 0;
    UINT lcMsgBase = 0;
    UINT lcDevice = 0;
    UINT lcPktRate = 0;
    DWORD lcPktData = 0;
    DWORD lcPktMode = 0;
    DWORD lcMoveMask = 0;
    DWORD lcBtnDnMask = 0;
    DWORD lcBtnUpMask = 0;
    LONG lcInOrgX = 0;
    LONG lcInOrgY = 0;
    LONG lcInOrgZ = 0;
    LONG lcInExtX = 0;
    LONG lcInExtY = 0;
    LONG lcInExtZ = 0;
    LONG lcOutOrgX = 0;
    LONG lcOutOrgY = 0;
    LONG lcOutOrgZ = 0;
    LONG lcOutExtX = 0;
    LONG lcOutExtY = 0;
    LONG lcOutExtZ = 0;
    DWORD lcSensX = 0;
    DWORD lcSensY = 0;
    DWORD lcSensZ = 0;
    BOOL lcSysMode = 0;
    int lcSysOrgX = 0;
    int lcSysOrgY = 0;
    int lcSysExtX = 0;
    int lcSysExtY = 0;
    DWORD lcSysSensX = 0;
    DWORD lcSysSensY = 0;
};

struct Packet
{
    DWORD pkTime = 0;
    UINT pkCursor = 0;
    DWORD pkButtons = 0;
    LONG pkX = 0;
    LONG pkY = 0;
    UINT pkNormalPressure = 0;
    int pkOrientationAzimuth = 0;
    int pkOrientationAltitude = 0;
    int pkOrientationTwist = 0;
};

using WTInfoWFn = UINT(APIENTRY*)(UINT, UINT, LPVOID);
using WTOpenWFn = HCTX(APIENTRY*)(HWND, LogContextW*, BOOL);
using WTCloseFn = BOOL(APIENTRY*)(HCTX);
using WTEnableFn = BOOL(APIENTRY*)(HCTX, BOOL);
using WTQueueSizeGetFn = int(APIENTRY*)(HCTX);
using WTQueueSizeSetFn = BOOL(APIENTRY*)(HCTX, int);
using WTPacketsGetFn = int(APIENTRY*)(HCTX, int, Packet*);

std::uint64_t nowUs()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();
}

float pressure01(UINT pressure)
{
    return std::clamp(static_cast<float>(pressure) / 1024.0f, 0.0f, 1.0f);
}

void applyOrientation(const Packet& raw, StrokePacket& packet)
{
    if (raw.pkOrientationAltitude != 0)
    {
        const float azimuthRadians = static_cast<float>(raw.pkOrientationAzimuth) * kPi / 1800.0f;
        const float altitudeDegrees = std::clamp(static_cast<float>(raw.pkOrientationAltitude) / 10.0f, -90.0f, 90.0f);
        const float tiltDegrees = std::clamp(90.0f - std::abs(altitudeDegrees), 0.0f, 90.0f);
        packet.tiltX = std::cos(azimuthRadians) * tiltDegrees;
        packet.tiltY = std::sin(azimuthRadians) * tiltDegrees;
    }
    if (raw.pkOrientationTwist != 0 || raw.pkOrientationAzimuth != 0)
    {
        const int rotationTenths = raw.pkOrientationTwist != 0 ? raw.pkOrientationTwist : raw.pkOrientationAzimuth;
        packet.rotation = static_cast<float>(rotationTenths) * kPi / 1800.0f;
        packet.hasRotation = true;
    }
}

void enrichMotion(std::vector<StrokePacket>& packets)
{
    for (std::size_t i = 1; i < packets.size(); ++i)
    {
        auto& previous = packets[i - 1];
        auto& packet = packets[i];
        const float dx = packet.x - previous.x;
        const float dy = packet.y - previous.y;
        const auto dtUs = packet.timestampUs > previous.timestampUs ? packet.timestampUs - previous.timestampUs : 0;
        const float dt = dtUs > 0 ? static_cast<float>(dtUs) / 1000000.0f : 0.001f;
        packet.velocityX = dx / dt;
        packet.velocityY = dy / dt;
        packet.speed = std::sqrt(packet.velocityX * packet.velocityX + packet.velocityY * packet.velocityY);
        packet.orientation = std::atan2(dy, dx);
    }
    if (packets.size() > 1)
    {
        packets.front().velocityX = packets[1].velocityX;
        packets.front().velocityY = packets[1].velocityY;
        packets.front().speed = packets[1].speed;
        packets.front().orientation = packets[1].orientation;
    }
}
}

struct WinTabAdapter::Impl
{
    HMODULE library = nullptr;
    HCTX context = nullptr;
    WTInfoWFn WTInfoW = nullptr;
    WTOpenWFn WTOpenW = nullptr;
    WTCloseFn WTClose = nullptr;
    WTEnableFn WTEnable = nullptr;
    WTEnableFn WTOverlap = nullptr;
    WTQueueSizeGetFn WTQueueSizeGet = nullptr;
    WTQueueSizeSetFn WTQueueSizeSet = nullptr;
    WTPacketsGetFn WTPacketsGet = nullptr;
    WinTabStatus status{};
};

bool WinTabAdapter::initialize(HWND hwnd)
{
    shutdown();
    auto impl = std::make_unique<Impl>();
    impl->status.packetMessage = kPacketMessage;
    impl->library = LoadLibraryW(L"Wintab32.dll");
    if (!impl->library)
    {
        impl->status.failure = "LoadLibraryW(Wintab32.dll) failed";
        impl_ = impl.release();
        return false;
    }
    impl->status.libraryLoaded = true;

    impl->WTInfoW = reinterpret_cast<WTInfoWFn>(GetProcAddress(impl->library, "WTInfoW"));
    impl->WTOpenW = reinterpret_cast<WTOpenWFn>(GetProcAddress(impl->library, "WTOpenW"));
    impl->WTClose = reinterpret_cast<WTCloseFn>(GetProcAddress(impl->library, "WTClose"));
    impl->WTEnable = reinterpret_cast<WTEnableFn>(GetProcAddress(impl->library, "WTEnable"));
    impl->WTOverlap = reinterpret_cast<WTEnableFn>(GetProcAddress(impl->library, "WTOverlap"));
    impl->WTQueueSizeGet = reinterpret_cast<WTQueueSizeGetFn>(GetProcAddress(impl->library, "WTQueueSizeGet"));
    impl->WTQueueSizeSet = reinterpret_cast<WTQueueSizeSetFn>(GetProcAddress(impl->library, "WTQueueSizeSet"));
    impl->WTPacketsGet = reinterpret_cast<WTPacketsGetFn>(GetProcAddress(impl->library, "WTPacketsGet"));
    if (!impl->WTInfoW || !impl->WTOpenW || !impl->WTClose || !impl->WTEnable || !impl->WTOverlap || !impl->WTQueueSizeGet || !impl->WTQueueSizeSet || !impl->WTPacketsGet)
    {
        impl->status.failure = "Missing required WinTab exports";
        impl_ = impl.release();
        return false;
    }
    impl->status.functionsLoaded = true;

    LogContextW context{};
    if (!impl->WTInfoW(kWtiDefSysCtx, 0, &context))
    {
        impl->status.failure = "WTInfoW(WTI_DEFSYSCTX) failed";
        impl_ = impl.release();
        return false;
    }
    impl->status.defaultContextRead = true;

    context.lcOptions |= kCxoMessages | kCxoCsrMessages;
    context.lcMsgBase = kPacketMessage;
    context.lcPktData = kPkTime | kPkButtons | kPkX | kPkY | kPkNormalPressure | kPkOrientation | kPkCursor;
    context.lcPktMode = 0;
    context.lcMoveMask = context.lcPktData;
    context.lcBtnDnMask = 0xFFFFFFFF;
    context.lcBtnUpMask = 0xFFFFFFFF;

    impl->context = impl->WTOpenW(hwnd, &context, TRUE);
    if (!impl->context)
    {
        impl->status.failure = "WTOpenW failed";
        impl_ = impl.release();
        return false;
    }
    impl->status.contextOpened = true;
    const int queueSize = impl->WTQueueSizeGet(impl->context);
    impl->WTQueueSizeSet(impl->context, 128);
    impl->WTEnable(impl->context, TRUE);
    impl->WTOverlap(impl->context, TRUE);
    impl->status.failure = queueSize > 0 ? "" : "WTQueueSizeGet returned <= 0";

    impl_ = impl.release();
    return true;
}

void WinTabAdapter::shutdown()
{
    if (!impl_) return;
    if (impl_->context && impl_->WTClose) impl_->WTClose(impl_->context);
    if (impl_->library) FreeLibrary(impl_->library);
    delete impl_;
    impl_ = nullptr;
}

bool WinTabAdapter::available() const
{
    return impl_ && impl_->context;
}

void WinTabAdapter::setCanvasSize(std::uint32_t width, std::uint32_t height)
{
    canvasWidth_ = std::max<std::uint32_t>(1, width);
    canvasHeight_ = std::max<std::uint32_t>(1, height);
}

WinTabStatus WinTabAdapter::status() const
{
    if (!impl_) return WinTabStatus{};
    return impl_->status;
}

UINT WinTabAdapter::packetMessage() const
{
    return kPacketMessage;
}

std::vector<StrokePacket> WinTabAdapter::packetsFromMessage(HWND hwnd, WPARAM, LPARAM) const
{
    std::vector<StrokePacket> packets;
    if (!available()) return packets;
    impl_->status.messageCount++;

    Packet rawPackets[32]{};
    const int count = impl_->WTPacketsGet(impl_->context, 32, rawPackets);
    impl_->status.lastPacketBatch = count;
    if (count <= 0) return packets;

    RECT rect{};
    GetClientRect(hwnd, &rect);
    const float clientWidth = static_cast<float>(std::max(1L, rect.right - rect.left));
    const float clientHeight = static_cast<float>(std::max(1L, rect.bottom - rect.top));
    const float canvasWidth = static_cast<float>(canvasWidth_);
    const float canvasHeight = static_cast<float>(canvasHeight_);

    packets.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        POINT screen{rawPackets[i].pkX, rawPackets[i].pkY};
        POINT client = screen;
        ScreenToClient(hwnd, &client);

        StrokePacket packet{};
        packet.x = static_cast<float>(client.x) * canvasWidth / clientWidth;
        packet.y = static_cast<float>(client.y) * canvasHeight / clientHeight;
        packet.rawScreenX = static_cast<float>(screen.x);
        packet.rawScreenY = static_cast<float>(screen.y);
        packet.pressure = pressure01(rawPackets[i].pkNormalPressure);
        packet.hasPressure = true;
        packet.timestampUs = nowUs();
        packet.isTip = packet.pressure > 0.0f;
        packet.barrelButton = (rawPackets[i].pkButtons & 0x02) != 0;
        packet.isEraser = (rawPackets[i].pkCursor & 0x02) != 0;
        packet.source = InputSource::WinTab;
        packet.rawPressure = rawPackets[i].pkNormalPressure;
        packet.rawPressureMax = 1024;
        packet.penInfoAvailable = true;
        applyOrientation(rawPackets[i], packet);
        packets.push_back(packet);
    }

    enrichMotion(packets);
    return packets;
}
