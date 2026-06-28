#pragma once

#include "graphite_types.h"

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

struct WinTabStatus
{
    bool libraryLoaded = false;
    bool functionsLoaded = false;
    bool defaultContextRead = false;
    bool contextOpened = false;
    UINT packetMessage = 0;
    std::uint64_t messageCount = 0;
    int lastPacketBatch = 0;
    std::string failure;
};

class WinTabAdapter
{
public:
    void setCanvasSize(std::uint32_t width, std::uint32_t height);
    bool initialize(HWND hwnd);
    void shutdown();
    bool available() const;
    WinTabStatus status() const;
    UINT packetMessage() const;
    std::vector<StrokePacket> packetsFromMessage(HWND hwnd, WPARAM wParam, LPARAM lParam) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::uint32_t canvasWidth_ = 960;
    std::uint32_t canvasHeight_ = 640;
};
