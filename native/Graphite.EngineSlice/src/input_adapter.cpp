#include "input_adapter.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#ifndef PEN_MASK_ROTATION
#define PEN_MASK_ROTATION 0x00000002
#endif

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kNoPressurePenFallback = 0.42f;

std::uint64_t nowUs()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();
}

std::uint64_t timestampUsForPointer(const POINTER_INFO& info)
{
    if (info.PerformanceCount != 0)
    {
        LARGE_INTEGER frequency{};
        if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
        {
            const auto ticksPerSecond = static_cast<UINT64>(frequency.QuadPart);
            const auto seconds = info.PerformanceCount / ticksPerSecond;
            const auto remainder = info.PerformanceCount % ticksPerSecond;
            return static_cast<std::uint64_t>(seconds * 1000000ull + (remainder * 1000000ull) / ticksPerSecond);
        }
    }
    if (info.dwTime != 0) return static_cast<std::uint64_t>(info.dwTime) * 1000ull;
    return nowUs();
}

bool clientPointFromPointer(HWND hwnd, const POINTER_INFO& info, float& clientX, float& clientY, float& screenX, float& screenY)
{
    POINT client = info.ptPixelLocation;
    ScreenToClient(hwnd, &client);
    clientX = static_cast<float>(client.x);
    clientY = static_cast<float>(client.y);
    screenX = static_cast<float>(info.ptPixelLocation.x);
    screenY = static_cast<float>(info.ptPixelLocation.y);

    HDC dc = GetDC(hwnd);
    if (!dc) return false;
    const int widthPx = GetDeviceCaps(dc, HORZRES);
    const int heightPx = GetDeviceCaps(dc, VERTRES);
    const int widthMm = GetDeviceCaps(dc, HORZSIZE);
    const int heightMm = GetDeviceCaps(dc, VERTSIZE);
    ReleaseDC(hwnd, dc);
    if (widthPx <= 0 || heightPx <= 0 || widthMm <= 0 || heightMm <= 0) return false;

    const float pxPerHimetricX = static_cast<float>(widthPx) / (static_cast<float>(widthMm) * 100.0f);
    const float pxPerHimetricY = static_cast<float>(heightPx) / (static_cast<float>(heightMm) * 100.0f);
    const float refinedScreenX = static_cast<float>(info.ptHimetricLocation.x) * pxPerHimetricX;
    const float refinedScreenY = static_cast<float>(info.ptHimetricLocation.y) * pxPerHimetricY;
    if (std::abs(refinedScreenX - screenX) > 3.0f || std::abs(refinedScreenY - screenY) > 3.0f) return false;

    POINT clientOrigin{0, 0};
    ClientToScreen(hwnd, &clientOrigin);
    screenX = refinedScreenX;
    screenY = refinedScreenY;
    clientX = refinedScreenX - static_cast<float>(clientOrigin.x);
    clientY = refinedScreenY - static_cast<float>(clientOrigin.y);
    return true;
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

void InputAdapter::setCanvasSize(std::uint32_t width, std::uint32_t height)
{
    canvasWidth_ = std::max<std::uint32_t>(1, width);
    canvasHeight_ = std::max<std::uint32_t>(1, height);
}

StrokePacket InputAdapter::packetFromClient(HWND hwnd, float clientX, float clientY, float screenX, float screenY, float pressure, bool isEraser, InputSource source, std::uint64_t timestampUs) const
{
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const float width = static_cast<float>(std::max(1L, rect.right - rect.left));
    const float height = static_cast<float>(std::max(1L, rect.bottom - rect.top));

    StrokePacket packet{};
    packet.x = clientX * static_cast<float>(canvasWidth_) / width;
    packet.y = clientY * static_cast<float>(canvasHeight_) / height;
    packet.rawScreenX = screenX;
    packet.rawScreenY = screenY;
    packet.pressure = std::clamp(pressure, 0.0f, 1.0f);
    packet.hasPressure = source == InputSource::MouseFallback;
    packet.timestampUs = timestampUs != 0 ? timestampUs : nowUs();
    packet.isTip = true;
    packet.isEraser = isEraser;
    packet.source = source;
    return packet;
}

std::optional<StrokePacket> InputAdapter::packetFromMouse(HWND hwnd, LPARAM lParam, float pressure, bool isEraser) const
{
    POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    POINT screen = client;
    ClientToScreen(hwnd, &screen);
    return packetFromClient(hwnd, static_cast<float>(client.x), static_cast<float>(client.y), static_cast<float>(screen.x), static_cast<float>(screen.y), pressure, isEraser, InputSource::MouseFallback);
}

std::vector<StrokePacket> InputAdapter::packetsFromPointer(HWND hwnd, WPARAM wParam) const
{
    std::vector<StrokePacket> packets;
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);

    UINT32 historyCount = 0;
    if (GetPointerPenInfoHistory(pointerId, &historyCount, nullptr) && historyCount > 0)
    {
        std::vector<POINTER_PEN_INFO> history(historyCount);
        if (GetPointerPenInfoHistory(pointerId, &historyCount, history.data()))
        {
            packets.reserve(historyCount);
            for (auto it = history.rbegin(); it != history.rend(); ++it)
            {
                float clientX = 0.0f;
                float clientY = 0.0f;
                float screenX = 0.0f;
                float screenY = 0.0f;
                clientPointFromPointer(hwnd, it->pointerInfo, clientX, clientY, screenX, screenY);
                float pressure = kNoPressurePenFallback;
                if (it->penMask & PEN_MASK_PRESSURE)
                {
                    pressure = static_cast<float>(it->pressure) / 1024.0f;
                }
                auto packet = packetFromClient(hwnd, clientX, clientY, screenX, screenY, pressure, (it->penFlags & PEN_FLAG_ERASER) != 0, InputSource::WmPointer, timestampUsForPointer(it->pointerInfo));
                packet.hasPressure = (it->penMask & PEN_MASK_PRESSURE) != 0;
                packet.pointerType = static_cast<std::uint32_t>(it->pointerInfo.pointerType);
                packet.pointerFlags = static_cast<std::uint32_t>(it->pointerInfo.pointerFlags);
                packet.penInfoAvailable = true;
                packet.penMask = it->penMask;
                packet.penFlags = it->penFlags;
                packet.tiltX = (it->penMask & PEN_MASK_TILT_X) ? static_cast<float>(it->tiltX) : 0.0f;
                packet.tiltY = (it->penMask & PEN_MASK_TILT_Y) ? static_cast<float>(it->tiltY) : 0.0f;
                if (it->penMask & PEN_MASK_ROTATION)
                {
                    packet.rotation = static_cast<float>(it->rotation) * kPi / 180.0f;
                    packet.hasRotation = true;
                }
                packet.barrelButton = (it->penFlags & PEN_FLAG_BARREL) != 0;
                packet.rawPressure = (it->penMask & PEN_MASK_PRESSURE) ? it->pressure : 0;
                packet.rawPressureMax = (it->penMask & PEN_MASK_PRESSURE) ? 1024 : 0;
                packets.push_back(packet);
            }
            enrichMotion(packets);
            return packets;
        }
    }

    POINTER_PEN_INFO penInfo{};
    if (GetPointerPenInfo(pointerId, &penInfo))
    {
        float clientX = 0.0f;
        float clientY = 0.0f;
        float screenX = 0.0f;
        float screenY = 0.0f;
        clientPointFromPointer(hwnd, penInfo.pointerInfo, clientX, clientY, screenX, screenY);
        float pressure = kNoPressurePenFallback;
        if (penInfo.penMask & PEN_MASK_PRESSURE)
        {
            pressure = static_cast<float>(penInfo.pressure) / 1024.0f;
        }
        auto packet = packetFromClient(hwnd, clientX, clientY, screenX, screenY, pressure, (penInfo.penFlags & PEN_FLAG_ERASER) != 0, InputSource::WmPointer, timestampUsForPointer(penInfo.pointerInfo));
        packet.hasPressure = (penInfo.penMask & PEN_MASK_PRESSURE) != 0;
        packet.pointerType = static_cast<std::uint32_t>(penInfo.pointerInfo.pointerType);
        packet.pointerFlags = static_cast<std::uint32_t>(penInfo.pointerInfo.pointerFlags);
        packet.penInfoAvailable = true;
        packet.penMask = penInfo.penMask;
        packet.penFlags = penInfo.penFlags;
        packet.tiltX = (penInfo.penMask & PEN_MASK_TILT_X) ? static_cast<float>(penInfo.tiltX) : 0.0f;
        packet.tiltY = (penInfo.penMask & PEN_MASK_TILT_Y) ? static_cast<float>(penInfo.tiltY) : 0.0f;
        if (penInfo.penMask & PEN_MASK_ROTATION)
        {
            packet.rotation = static_cast<float>(penInfo.rotation) * kPi / 180.0f;
            packet.hasRotation = true;
        }
        packet.barrelButton = (penInfo.penFlags & PEN_FLAG_BARREL) != 0;
        packet.rawPressure = (penInfo.penMask & PEN_MASK_PRESSURE) ? penInfo.pressure : 0;
        packet.rawPressureMax = (penInfo.penMask & PEN_MASK_PRESSURE) ? 1024 : 0;
        packets.push_back(packet);
        enrichMotion(packets);
        return packets;
    }

    POINTER_INFO pointerInfo{};
    if (!GetPointerInfo(pointerId, &pointerInfo)) return packets;
    float clientX = 0.0f;
    float clientY = 0.0f;
    float screenX = 0.0f;
    float screenY = 0.0f;
    clientPointFromPointer(hwnd, pointerInfo, clientX, clientY, screenX, screenY);
    auto packet = packetFromClient(hwnd, clientX, clientY, screenX, screenY, kNoPressurePenFallback, false, InputSource::WmPointer, timestampUsForPointer(pointerInfo));
    packet.pointerType = static_cast<std::uint32_t>(pointerInfo.pointerType);
    packet.pointerFlags = static_cast<std::uint32_t>(pointerInfo.pointerFlags);
    packets.push_back(packet);
    enrichMotion(packets);
    return packets;
}
