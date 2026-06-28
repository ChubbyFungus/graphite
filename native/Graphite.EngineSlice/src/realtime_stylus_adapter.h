#pragma once

#include "graphite_types.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

constexpr UINT kRealTimeStylusPacketMessage = WM_APP + 0x31;

struct RealTimeStylusStatus
{
    bool comInitialized = false;
    bool objectCreated = false;
    bool hwndAssigned = false;
    bool desiredPacketDescriptionSet = false;
    bool pluginAdded = false;
    bool enabled = false;
    std::uint64_t packetCount = 0;
    std::uint64_t pressurePayloadPackets = 0;
    std::uint32_t packetPropertyCount = 0;
    std::int32_t xPropertyIndex = -1;
    std::int32_t yPropertyIndex = -1;
    std::int32_t pressurePropertyIndex = -1;
    LONG xLogicalMin = 0;
    LONG xLogicalMax = 0;
    LONG yLogicalMin = 0;
    LONG yLogicalMax = 0;
    LONG pressureLogicalMin = 0;
    LONG pressureLogicalMax = 0;
    float inkToDeviceScaleX = 1.0f;
    float inkToDeviceScaleY = 1.0f;
    LONG lastRawX = 0;
    LONG lastRawY = 0;
    LONG lastRawPressure = 0;
    std::string failure;
};

class RealTimeStylusAdapter
{
public:
    struct Impl;

    void setCanvasSize(std::uint32_t width, std::uint32_t height);
    bool initialize(HWND hwnd);
    void shutdown();
    bool available() const;
    RealTimeStylusStatus status() const;
    std::vector<StrokePacket> drainPackets(HWND hwnd);

private:
    Impl* impl_ = nullptr;
    std::uint32_t canvasWidth_ = 960;
    std::uint32_t canvasHeight_ = 640;
};
