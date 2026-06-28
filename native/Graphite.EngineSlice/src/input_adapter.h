#pragma once

#include "graphite_types.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <vector>

class InputAdapter
{
public:
    void setCanvasSize(std::uint32_t width, std::uint32_t height);
    std::vector<StrokePacket> packetsFromPointer(HWND hwnd, WPARAM wParam) const;
    std::optional<StrokePacket> packetFromMouse(HWND hwnd, LPARAM lParam, float pressure, bool isEraser) const;

private:
    StrokePacket packetFromClient(HWND hwnd, float clientX, float clientY, float screenX, float screenY, float pressure, bool isEraser, InputSource source, std::uint64_t timestampUs = 0) const;
    std::uint32_t canvasWidth_ = 960;
    std::uint32_t canvasHeight_ = 640;
};
