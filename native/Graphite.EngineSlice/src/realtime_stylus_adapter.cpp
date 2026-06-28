#include "realtime_stylus_adapter.h"

#include <objbase.h>
#include <msinkaut.h>
#include <RTSCOM.h>
#include <RTSCOM_i.c>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <sstream>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr LONG kFallbackPressureMax = 1024;

std::uint64_t nowUs()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();
}

std::string hrText(const char* label, HRESULT hr)
{
    std::ostringstream stream;
    stream << label << " failed 0x" << std::hex << static_cast<unsigned long>(hr);
    return stream.str();
}

struct QueuedPacket
{
    LONG x = 0;
    LONG y = 0;
    LONG pressure = 0;
    LONG tiltX = 0;
    LONG tiltY = 0;
    LONG twist = 0;
    bool hasPressure = false;
    bool hasTiltX = false;
    bool hasTiltY = false;
    bool hasTwist = false;
    bool isTip = true;
    bool isEnd = false;
    std::uint64_t timestampUs = 0;
};

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

struct RealTimeStylusAdapter::Impl
{
    HWND hwnd = nullptr;
    IRealTimeStylus* stylus = nullptr;
    IStylusSyncPlugin* plugin = nullptr;
    bool coInitializedHere = false;
    RealTimeStylusStatus status;
    std::mutex mutex;
    std::vector<QueuedPacket> queuedPackets;
};

class StylusPlugin final : public IStylusSyncPlugin
{
public:
    explicit StylusPlugin(RealTimeStylusAdapter::Impl* impl)
        : impl_(impl)
    {
        CoCreateFreeThreadedMarshaler(static_cast<IUnknown*>(this), &freeThreadedMarshaler_);
    }

    ~StylusPlugin()
    {
        if (freeThreadedMarshaler_) freeThreadedMarshaler_->Release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;
        if (IsEqualIID(riid, IID_IMarshal) && freeThreadedMarshaler_)
        {
            return freeThreadedMarshaler_->QueryInterface(riid, ppvObject);
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IStylusPlugin) || IsEqualIID(riid, IID_IStylusSyncPlugin))
        {
            *ppvObject = static_cast<IStylusSyncPlugin*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG count = --refCount_;
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE RealTimeStylusEnabled(IRealTimeStylus* piRtsSrc, ULONG cTcidCount, const TABLET_CONTEXT_ID* pTcids) override
    {
        updatePacketDescription(piRtsSrc, cTcidCount, pTcids);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RealTimeStylusDisabled(IRealTimeStylus*, ULONG, const TABLET_CONTEXT_ID*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE StylusInRange(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE StylusOutOfRange(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE StylusDown(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG cPropCountPerPkt, LONG* pPacket, LONG**) override
    {
        (void)piRtsSrc;
        (void)pStylusInfo;
        enqueueOne(cPropCountPerPkt, pPacket, false);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE StylusUp(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG cPropCountPerPkt, LONG* pPacket, LONG**) override
    {
        (void)piRtsSrc;
        (void)pStylusInfo;
        enqueueOne(cPropCountPerPkt, pPacket, true);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE StylusButtonDown(IRealTimeStylus*, STYLUS_ID, const GUID*, POINT*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE StylusButtonUp(IRealTimeStylus*, STYLUS_ID, const GUID*, POINT*) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE InAirPackets(IRealTimeStylus*, const StylusInfo*, ULONG, ULONG, LONG*, ULONG*, LONG**) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Packets(IRealTimeStylus* piRtsSrc, const StylusInfo* pStylusInfo, ULONG cPktCount, ULONG cPktBuffLength, LONG* pPackets, ULONG*, LONG**) override
    {
        if (!impl_ || !pPackets || cPktCount == 0) return S_OK;
        (void)piRtsSrc;
        (void)pStylusInfo;
        const ULONG propsPerPacket = cPktCount > 0 ? cPktBuffLength / cPktCount : 0;
        if (propsPerPacket == 0) return S_OK;
        std::vector<QueuedPacket> packets;
        packets.reserve(cPktCount);
        for (ULONG packetIndex = 0; packetIndex < cPktCount; ++packetIndex)
        {
            packets.push_back(fromRaw(propsPerPacket, pPackets + packetIndex * propsPerPacket, false));
        }
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->queuedPackets.insert(impl_->queuedPackets.end(), packets.begin(), packets.end());
        }
        PostMessage(impl_->hwnd, kRealTimeStylusPacketMessage, 0, 0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CustomStylusDataAdded(IRealTimeStylus*, const GUID*, ULONG, const BYTE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SystemEvent(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID, SYSTEM_EVENT, SYSTEM_EVENT_DATA) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE TabletAdded(IRealTimeStylus*, IInkTablet*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE TabletRemoved(IRealTimeStylus*, LONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Error(IRealTimeStylus*, IStylusPlugin*, RealTimeStylusDataInterest, HRESULT hrErrorCode, LONG_PTR*) override
    {
        if (impl_) impl_->status.failure = hrText("RealTimeStylus plugin callback", hrErrorCode);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateMapping(IRealTimeStylus*) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE DataInterest(RealTimeStylusDataInterest* pDataInterest) override
    {
        if (!pDataInterest) return E_POINTER;
        *pDataInterest = RTSDI_DefaultEvents;
        return S_OK;
    }

private:
    struct PacketIndexes
    {
        int x = 0;
        int y = 1;
        int pressure = 2;
    };

    void updatePacketDescription(IRealTimeStylus* stylus, ULONG cTcidCount, const TABLET_CONTEXT_ID* tcids)
    {
        if (!impl_ || !stylus || cTcidCount == 0 || !tcids) return;

        FLOAT scaleX = 1.0f;
        FLOAT scaleY = 1.0f;
        ULONG propertyCount = 0;
        PACKET_PROPERTY* properties = nullptr;
        const HRESULT hr = stylus->GetPacketDescriptionData(tcids[0], &scaleX, &scaleY, &propertyCount, &properties);
        if (FAILED(hr))
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->status.failure = hrText("IRealTimeStylus::GetPacketDescriptionData", hr);
            return;
        }

        RealTimeStylusStatus next{};
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            next = impl_->status;
        }
        next.packetPropertyCount = propertyCount;
        next.inkToDeviceScaleX = scaleX;
        next.inkToDeviceScaleY = scaleY;
        next.xPropertyIndex = -1;
        next.yPropertyIndex = -1;
        next.pressurePropertyIndex = -1;

        for (ULONG i = 0; properties && i < propertyCount; ++i)
        {
            const auto& property = properties[i];
            if (IsEqualGUID(property.guid, GUID_PACKETPROPERTY_GUID_X))
            {
                next.xPropertyIndex = static_cast<std::int32_t>(i);
                next.xLogicalMin = property.PropertyMetrics.nLogicalMin;
                next.xLogicalMax = property.PropertyMetrics.nLogicalMax;
            }
            else if (IsEqualGUID(property.guid, GUID_PACKETPROPERTY_GUID_Y))
            {
                next.yPropertyIndex = static_cast<std::int32_t>(i);
                next.yLogicalMin = property.PropertyMetrics.nLogicalMin;
                next.yLogicalMax = property.PropertyMetrics.nLogicalMax;
            }
            else if (IsEqualGUID(property.guid, GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE))
            {
                next.pressurePropertyIndex = static_cast<std::int32_t>(i);
                next.pressureLogicalMin = property.PropertyMetrics.nLogicalMin;
                next.pressureLogicalMax = property.PropertyMetrics.nLogicalMax;
            }
        }
        if (properties) CoTaskMemFree(properties);

        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->status = next;
    }

    PacketIndexes packetIndexes() const
    {
        PacketIndexes indexes{};
        if (!impl_) return indexes;
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->status.xPropertyIndex >= 0) indexes.x = impl_->status.xPropertyIndex;
        if (impl_->status.yPropertyIndex >= 0) indexes.y = impl_->status.yPropertyIndex;
        indexes.pressure = impl_->status.pressurePropertyIndex;
        return indexes;
    }

    QueuedPacket fromRaw(ULONG propCount, const LONG* packet, bool isEnd) const
    {
        QueuedPacket queued{};
        const auto indexes = packetIndexes();
        if (indexes.x >= 0 && static_cast<ULONG>(indexes.x) < propCount) queued.x = packet[indexes.x];
        if (indexes.y >= 0 && static_cast<ULONG>(indexes.y) < propCount) queued.y = packet[indexes.y];
        if (indexes.pressure >= 0 && static_cast<ULONG>(indexes.pressure) < propCount)
        {
            queued.pressure = packet[indexes.pressure];
            queued.hasPressure = true;
        }
        if (propCount > 3)
        {
            queued.tiltX = packet[3];
            queued.hasTiltX = true;
        }
        if (propCount > 4)
        {
            queued.tiltY = packet[4];
            queued.hasTiltY = true;
        }
        if (propCount > 5)
        {
            queued.twist = packet[5];
            queued.hasTwist = true;
        }
        queued.isEnd = isEnd;
        queued.isTip = !isEnd;
        queued.timestampUs = nowUs();
        return queued;
    }

    void enqueueOne(ULONG propCount, LONG* packet, bool isEnd)
    {
        if (!impl_ || !packet || propCount == 0) return;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->queuedPackets.push_back(fromRaw(propCount, packet, isEnd));
        }
        PostMessage(impl_->hwnd, kRealTimeStylusPacketMessage, 0, 0);
    }

    std::atomic<ULONG> refCount_{1};
    RealTimeStylusAdapter::Impl* impl_ = nullptr;
    IUnknown* freeThreadedMarshaler_ = nullptr;
};

bool RealTimeStylusAdapter::initialize(HWND hwnd)
{
    shutdown();
    impl_ = new Impl();
    impl_->hwnd = hwnd;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr))
    {
        impl_->status.comInitialized = true;
        impl_->coInitializedHere = hr == S_OK;
    }
    else if (hr == RPC_E_CHANGED_MODE)
    {
        impl_->status.comInitialized = true;
    }
    else
    {
        impl_->status.failure = hrText("CoInitializeEx", hr);
        return false;
    }

    hr = CoCreateInstance(CLSID_RealTimeStylus, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->stylus));
    if (FAILED(hr))
    {
        impl_->status.failure = hrText("CoCreateInstance(CLSID_RealTimeStylus)", hr);
        return false;
    }
    impl_->status.objectCreated = true;

    hr = impl_->stylus->put_HWND(reinterpret_cast<HANDLE_PTR>(hwnd));
    if (FAILED(hr))
    {
        impl_->status.failure = hrText("IRealTimeStylus::put_HWND", hr);
        return false;
    }
    impl_->status.hwndAssigned = true;

    GUID properties[] = {
        GUID_PACKETPROPERTY_GUID_X,
        GUID_PACKETPROPERTY_GUID_Y,
        GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE,
        GUID_PACKETPROPERTY_GUID_X_TILT_ORIENTATION,
        GUID_PACKETPROPERTY_GUID_Y_TILT_ORIENTATION,
        GUID_PACKETPROPERTY_GUID_TWIST_ORIENTATION,
    };
    hr = impl_->stylus->SetDesiredPacketDescription(static_cast<ULONG>(std::size(properties)), properties);
    if (FAILED(hr))
    {
        impl_->status.failure = hrText("IRealTimeStylus::SetDesiredPacketDescription", hr);
        return false;
    }
    impl_->status.desiredPacketDescriptionSet = true;

    auto* plugin = new StylusPlugin(impl_);
    impl_->plugin = plugin;
    hr = impl_->stylus->AddStylusSyncPlugin(0, impl_->plugin);
    if (FAILED(hr))
    {
        impl_->plugin->Release();
        impl_->plugin = nullptr;
        impl_->status.failure = hrText("IRealTimeStylus::AddStylusSyncPlugin", hr);
        return false;
    }
    impl_->status.pluginAdded = true;

    hr = impl_->stylus->put_Enabled(TRUE);
    if (FAILED(hr))
    {
        impl_->status.failure = hrText("IRealTimeStylus::put_Enabled(TRUE)", hr);
        return false;
    }
    impl_->status.enabled = true;
    return true;
}

void RealTimeStylusAdapter::shutdown()
{
    if (!impl_) return;
    if (impl_->stylus)
    {
        impl_->stylus->put_Enabled(FALSE);
        impl_->stylus->Release();
        impl_->stylus = nullptr;
    }
    if (impl_->plugin)
    {
        impl_->plugin->Release();
        impl_->plugin = nullptr;
    }
    if (impl_->coInitializedHere)
    {
        CoUninitialize();
    }
    delete impl_;
    impl_ = nullptr;
}

bool RealTimeStylusAdapter::available() const
{
    return impl_ && impl_->status.enabled;
}

void RealTimeStylusAdapter::setCanvasSize(std::uint32_t width, std::uint32_t height)
{
    canvasWidth_ = std::max<std::uint32_t>(1, width);
    canvasHeight_ = std::max<std::uint32_t>(1, height);
}

RealTimeStylusStatus RealTimeStylusAdapter::status() const
{
    if (!impl_) return {};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->status;
}

std::vector<StrokePacket> RealTimeStylusAdapter::drainPackets(HWND hwnd)
{
    std::vector<QueuedPacket> queued;
    if (!impl_) return {};
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        queued.swap(impl_->queuedPackets);
    }

    RECT rect{};
    GetClientRect(hwnd, &rect);
    RealTimeStylusStatus currentStatus{};
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        currentStatus = impl_->status;
    }
    const float xRange = static_cast<float>(std::max<LONG>(1, currentStatus.xLogicalMax - currentStatus.xLogicalMin));
    const float yRange = static_cast<float>(std::max<LONG>(1, currentStatus.yLogicalMax - currentStatus.yLogicalMin));
    const float pressureRange = static_cast<float>(std::max<LONG>(1, currentStatus.pressureLogicalMax - currentStatus.pressureLogicalMin));
    const float canvasWidth = static_cast<float>(canvasWidth_);
    const float canvasHeight = static_cast<float>(canvasHeight_);

    std::vector<StrokePacket> packets;
    packets.reserve(queued.size());
    for (const auto& raw : queued)
    {
        StrokePacket packet{};
        packet.x = std::clamp((static_cast<float>(raw.x - currentStatus.xLogicalMin) / xRange) * canvasWidth, 0.0f, canvasWidth);
        packet.y = std::clamp((static_cast<float>(raw.y - currentStatus.yLogicalMin) / yRange) * canvasHeight, 0.0f, canvasHeight);
        packet.rawScreenX = static_cast<float>(raw.x);
        packet.rawScreenY = static_cast<float>(raw.y);
        packet.pressure = raw.hasPressure ? std::clamp(static_cast<float>(raw.pressure - currentStatus.pressureLogicalMin) / pressureRange, 0.0f, 1.0f) : 0.65f;
        packet.hasPressure = raw.hasPressure;
        packet.rawPressure = raw.hasPressure ? static_cast<std::uint32_t>(std::max<LONG>(0, raw.pressure)) : 0;
        packet.rawPressureMax = raw.hasPressure ? static_cast<std::uint32_t>(std::max<LONG>(1, currentStatus.pressureLogicalMax)) : 0;
        packet.tiltX = raw.hasTiltX ? static_cast<float>(raw.tiltX) : 0.0f;
        packet.tiltY = raw.hasTiltY ? static_cast<float>(raw.tiltY) : 0.0f;
        packet.rotation = raw.hasTwist ? static_cast<float>(raw.twist) * kPi / 180.0f : 0.0f;
        packet.hasRotation = raw.hasTwist;
        packet.timestampUs = raw.timestampUs;
        packet.isTip = raw.isTip;
        packet.source = InputSource::RealTimeStylus;
        packets.push_back(packet);

        impl_->status.packetCount++;
        if (raw.hasPressure) impl_->status.pressurePayloadPackets++;
        impl_->status.lastRawX = raw.x;
        impl_->status.lastRawY = raw.y;
        impl_->status.lastRawPressure = raw.pressure;
    }
    enrichMotion(packets);
    return packets;
}
