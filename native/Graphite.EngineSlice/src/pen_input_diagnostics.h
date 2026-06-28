#pragma once

#include "graphite_types.h"

#include <cstdint>
#include <fstream>
#include <string>

class PenInputDiagnostics
{
public:
    bool start(const std::string& path);
    void stop();
    void toggle(const std::string& path);
    void record(const StrokePacket& packet, const char* phase);

    bool recording() const { return recording_; }
    const std::string& path() const { return path_; }
    std::uint64_t packetCount() const { return packetCount_; }
    std::uint64_t wmPointerPackets() const { return wmPointerPackets_; }
    std::uint64_t winTabPackets() const { return winTabPackets_; }
    std::uint64_t realTimeStylusPackets() const { return realTimeStylusPackets_; }
    std::uint64_t mousePackets() const { return mousePackets_; }
    float minPressure() const { return packetCount_ ? minPressure_ : 0.0f; }
    float maxPressure() const { return packetCount_ ? maxPressure_ : 0.0f; }
    bool sawTilt() const { return sawTilt_; }
    bool sawRotation() const { return sawRotation_; }
    bool sawEraser() const { return sawEraser_; }
    bool sawBarrel() const { return sawBarrel_; }

private:
    std::ofstream stream_;
    std::string path_;
    bool recording_ = false;
    std::uint64_t packetCount_ = 0;
    std::uint64_t recordsSinceFlush_ = 0;
    std::uint64_t wmPointerPackets_ = 0;
    std::uint64_t winTabPackets_ = 0;
    std::uint64_t realTimeStylusPackets_ = 0;
    std::uint64_t mousePackets_ = 0;
    float minPressure_ = 1.0f;
    float maxPressure_ = 0.0f;
    bool sawTilt_ = false;
    bool sawRotation_ = false;
    bool sawEraser_ = false;
    bool sawBarrel_ = false;
};
