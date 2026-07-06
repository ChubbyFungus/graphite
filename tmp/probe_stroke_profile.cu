// Scratch diagnostic probe - NOT part of the build. Drives the real
// CudaGraphiteBackend headlessly with synthetic strokes and prints the
// deposit profile along the stroke so periodic beading can be measured
// numerically instead of by eye.
//
// Build (from repo root, vcvars + nvcc on PATH):
//   nvcc -std=c++17 -O2 -arch=native -I native/Graphite.EngineSlice/src \
//        tmp/probe_stroke_profile.cu -o tmp/probe_stroke_profile.exe
//
// The private->public define is a probe-only hack to read material buffers.
#define private public
#include "../native/Graphite.EngineSlice/src/cuda_graphite_backend.cu"
#undef private

#include <cstdio>
#include <cmath>
#include <vector>

static StrokePacket makePacket(float x, float y, float pressure, float strokeDist)
{
    StrokePacket p{};
    p.x = x;
    p.y = y;
    p.pressure = pressure;
    p.hasPressure = true;
    p.isTip = true;
    p.tiltX = 26.0f;   // match Zac's real pen posture from the CSV
    p.tiltY = -11.0f;
    p.strokeDistancePx = strokeDist;
    return p;
}

static void runStroke(CudaGraphiteBackend& backend, ToolParams& params,
                      float y, float step, float lengthPx, float pressure)
{
    float dist = 0.0f;
    StrokePacket prev = makePacket(20.0f, y, pressure, dist);
    backend.beginFrame();
    for (float x = 20.0f + step; x <= 20.0f + lengthPx; x += step)
    {
        dist += step;
        StrokePacket next = makePacket(x, y, pressure, dist);
        backend.submitStrokeSegment(prev, next, params);
        prev = next;
    }
    backend.endFrame();
}

// Read one material float at pixel (x,y) from a page-pooled device buffer.
static float readMaterial(CudaGraphiteBackend& b, const float* devBuffer, int x, int y)
{
    const std::uint32_t tileX = static_cast<std::uint32_t>(x) / b.tileSize_;
    const std::uint32_t tileY = static_cast<std::uint32_t>(y) / b.tileSize_;
    const std::uint32_t tileIndex = tileY * b.tileColumns_ + tileX;
    const std::uint32_t page = b.tilePageTable_[tileIndex];
    if (page == kUnallocatedTilePage) return -1.0f;
    const std::uint32_t localX = static_cast<std::uint32_t>(x) % b.tileSize_;
    const std::uint32_t localY = static_cast<std::uint32_t>(y) % b.tileSize_;
    const std::size_t idx = (static_cast<std::size_t>(page) * b.tileSize_ * b.tileSize_) + localY * b.tileSize_ + localX;
    float v = 0.0f;
    cudaMemcpy(&v, devBuffer + idx, sizeof(float), cudaMemcpyDeviceToHost);
    return v;
}

static void profile(CudaGraphiteBackend& b, const char* label, float y, float lengthPx)
{
    std::vector<float> tone;
    for (int x = 24; x < static_cast<int>(20.0f + lengthPx) - 4; ++x)
    {
        const float loose = readMaterial(b, b.looseGraphite_, x, static_cast<int>(y));
        const float bound = readMaterial(b, b.boundGraphite_, x, static_cast<int>(y));
        tone.push_back(bound * 1.2f + loose * 0.85f);
    }
    float mn = 1e9f, mx = -1e9f, sum = 0.0f;
    for (float v : tone) { mn = fminf(mn, v); mx = fmaxf(mx, v); sum += v; }
    const float mean = sum / tone.size();
    // crude period detection via autocorrelation of (v - mean)
    int bestLag = 0; float bestCorr = 0.0f;
    for (int lag = 2; lag < 40 && lag < static_cast<int>(tone.size()) / 2; ++lag)
    {
        float c = 0.0f;
        for (std::size_t i = 0; i + lag < tone.size(); ++i)
            c += (tone[i] - mean) * (tone[i + lag] - mean);
        if (c > bestCorr) { bestCorr = c; bestLag = lag; }
    }
    printf("%s: n=%zu mean=%.4f min=%.4f max=%.4f ripple=%.1f%% period~%dpx\n",
           label, tone.size(), mean, mn, mx, mean > 0 ? 100.0f * (mx - mn) / mean : 0.0f, bestLag);
    printf("  profile[0..79]: ");
    for (int i = 0; i < 80 && i < static_cast<int>(tone.size()); ++i)
        printf("%c", tone[i] < 0 ? '?' : "0123456789"[std::min(9, static_cast<int>(tone[i] * 30))]);
    printf("\n");
}

int main()
{
    CudaGraphiteBackend backend;
    GraphiteBackendInit init{};
    init.width = 512;
    init.height = 256;
    if (!backend.initialize(init)) { printf("init failed\n"); return 1; }
    backend.setPaperPreset(PaperPreset::ColdPress);

    ToolParams params{};
    params.tool = ToolKind::Pencil;
    params.grade = PencilGrade::HB;
    params.radiusPx = 3.0f;

    runStroke(backend, params, 40.0f, 1.5f, 400.0f, 0.35f);   // slow
    runStroke(backend, params, 90.0f, 10.0f, 400.0f, 0.35f);  // fast
    runStroke(backend, params, 140.0f, 1.5f, 400.0f, 0.75f);  // slow heavy

    profile(backend, "slow  step=1.5 p=0.35", 40.0f, 400.0f);
    profile(backend, "fast  step=10  p=0.35", 90.0f, 400.0f);
    profile(backend, "slow  step=1.5 p=0.75", 140.0f, 400.0f);
    backend.shutdown();
    return 0;
}
