#include "pen_input_diagnostics.h"

#include <algorithm>

namespace
{
const char* sourceName(InputSource source)
{
    if (source == InputSource::WmPointer) return "WM_POINTER";
    if (source == InputSource::WinTab) return "WinTab";
    if (source == InputSource::RealTimeStylus) return "RealTimeStylus";
    return "MouseFallback";
}
}

bool PenInputDiagnostics::start(const std::string& path)
{
    stop();
    path_ = path;
    stream_.open(path_, std::ios::out | std::ios::trunc);
    if (!stream_.is_open())
    {
        recording_ = false;
        return false;
    }
    packetCount_ = 0;
    recordsSinceFlush_ = 0;
    wmPointerPackets_ = 0;
    winTabPackets_ = 0;
    realTimeStylusPackets_ = 0;
    mousePackets_ = 0;
    minPressure_ = 1.0f;
    maxPressure_ = 0.0f;
    sawTilt_ = false;
    sawRotation_ = false;
    sawEraser_ = false;
    sawBarrel_ = false;
    recording_ = true;
    stream_ << "packet,phase,source,x,y,rawScreenX,rawScreenY,pressure,hasPressure,rawPressure,rawPressureMax,pointerType,pointerFlags,penInfoAvailable,penMask,penFlags,tiltX,tiltY,rotation,hasRotation,orientation,velocityX,velocityY,speed,timestampUs,isTip,isEraser,barrelButton\n";
    return true;
}

void PenInputDiagnostics::stop()
{
    if (stream_.is_open())
    {
        stream_.flush();
        stream_.close();
    }
    recording_ = false;
}

void PenInputDiagnostics::toggle(const std::string& path)
{
    if (recording_) stop();
    else start(path);
}

void PenInputDiagnostics::record(const StrokePacket& packet, const char* phase)
{
    if (!recording_ || !stream_.is_open()) return;
    packetCount_++;
    if (packet.source == InputSource::WmPointer) wmPointerPackets_++;
    else if (packet.source == InputSource::WinTab) winTabPackets_++;
    else if (packet.source == InputSource::RealTimeStylus) realTimeStylusPackets_++;
    else mousePackets_++;
    minPressure_ = std::min(minPressure_, packet.pressure);
    maxPressure_ = std::max(maxPressure_, packet.pressure);
    sawTilt_ = sawTilt_ || packet.tiltX != 0.0f || packet.tiltY != 0.0f;
    sawRotation_ = sawRotation_ || packet.hasRotation;
    sawEraser_ = sawEraser_ || packet.isEraser;
    sawBarrel_ = sawBarrel_ || packet.barrelButton;

    stream_ << packetCount_ << ','
            << phase << ','
            << sourceName(packet.source) << ','
            << packet.x << ','
            << packet.y << ','
            << packet.rawScreenX << ','
            << packet.rawScreenY << ','
            << packet.pressure << ','
            << (packet.hasPressure ? 1 : 0) << ','
            << packet.rawPressure << ','
            << packet.rawPressureMax << ','
            << packet.pointerType << ','
            << packet.pointerFlags << ','
            << (packet.penInfoAvailable ? 1 : 0) << ','
            << packet.penMask << ','
            << packet.penFlags << ','
            << packet.tiltX << ','
            << packet.tiltY << ','
            << packet.rotation << ','
            << (packet.hasRotation ? 1 : 0) << ','
            << packet.orientation << ','
            << packet.velocityX << ','
            << packet.velocityY << ','
            << packet.speed << ','
            << packet.timestampUs << ','
            << (packet.isTip ? 1 : 0) << ','
            << (packet.isEraser ? 1 : 0) << ','
            << (packet.barrelButton ? 1 : 0) << '\n';
    recordsSinceFlush_++;
    if (recordsSinceFlush_ >= 32)
    {
        stream_.flush();
        recordsSinceFlush_ = 0;
    }
}
