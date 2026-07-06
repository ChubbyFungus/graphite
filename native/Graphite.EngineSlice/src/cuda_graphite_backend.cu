#include "cuda_graphite_backend.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
constexpr int kThreads = 256;
constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kUnallocatedTilePage = 0xFFFFFFFFu;

__device__ float clamp01(float value)
{
    return fminf(1.0f, fmaxf(0.0f, value));
}

__device__ float smooth01(float value)
{
    const float t = clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

__device__ float gradePosition(PencilGrade grade)
{
    if (grade == PencilGrade::FourH) return 0.0f;
    if (grade == PencilGrade::ThreeH) return 0.10f;
    if (grade == PencilGrade::TwoH) return 0.22f;
    if (grade == PencilGrade::H) return 0.36f;
    if (grade == PencilGrade::B) return 0.62f;
    if (grade == PencilGrade::TwoB) return 0.72f;
    if (grade == PencilGrade::FourB) return 0.84f;
    if (grade == PencilGrade::SixB) return 0.92f;
    if (grade == PencilGrade::EightB) return 1.0f;
    return 0.50f;
}

__device__ float anchoredGradeValue(PencilGrade grade, float hardAnchor, float middleAnchor, float softAnchor)
{
    const float position = gradePosition(grade);
    if (position <= 0.50f)
    {
        return hardAnchor + (middleAnchor - hardAnchor) * smooth01(position / 0.50f);
    }
    return middleAnchor + (softAnchor - middleAnchor) * smooth01((position - 0.50f) / 0.50f);
}

__device__ float gradeSoftness(PencilGrade grade)
{
    return anchoredGradeValue(grade, 0.25f, 0.58f, 1.0f);
}

__device__ float gradeDepositStrength(PencilGrade grade)
{
    return anchoredGradeValue(grade, 0.56f, 1.0f, 1.72f);
}

__device__ float gradeToneCapacity(PencilGrade grade, float pressure)
{
    // Capacity ceilings re-anchored 2026-07-05 (probe: 16-pass saturation,
    // display value targets 4H ~0.62, HB ~0.45, 2B ~0.30, 8B ~0.05).
    // Grade separation lives in the MAX darkness a grade can build to:
    // hard grades run out of depositable graphite at light values; only
    // the softest B grades can reach true black.
    const float base = anchoredGradeValue(grade, 0.16f, 0.28f, 0.60f);
    const float pressureRoom = anchoredGradeValue(grade, 0.04f, 0.07f, 0.16f);
    return base + pressure * pressureRoom;
}

__device__ float gradeLooseStrength(PencilGrade grade)
{
    return anchoredGradeValue(grade, 0.70f, 1.0f, 1.32f);
}

__device__ float gradeBoundStrength(PencilGrade grade)
{
    return anchoredGradeValue(grade, 0.88f, 1.0f, 1.16f);
}

__device__ float gradePressureVariance(PencilGrade grade, float pressure)
{
    const float heavyPressure = smooth01((pressure - 0.30f) / 0.70f);
    return 1.0f + heavyPressure * anchoredGradeValue(grade, 0.10f, 0.28f, 0.90f);
}

int toolLoadIndexForTool(ToolKind tool)
{
    if (tool == ToolKind::Tortillon) return 0;
    if (tool == ToolKind::FanBrush) return 1;
    if (tool == ToolKind::PowderBrush) return 2;
    if (tool == ToolKind::KneadedEraser) return 3;
    return -1;
}

__host__ __device__ float effectiveToolRadiusPx(ToolParams params)
{
    const float pencilRadius = fmaxf(0.45f, params.radiusPx);
    if (params.tool == ToolKind::RegularEraser) return fmaxf(pencilRadius * 7.0f, 7.0f);
    if (params.tool == ToolKind::KneadedEraser)
    {
        if (params.kneadedShape == KneadedEraserShape::Point) return fmaxf(pencilRadius * 2.4f, 2.4f);
        if (params.kneadedShape == KneadedEraserShape::Edge) return fmaxf(pencilRadius * 5.2f, 5.2f);
        if (params.kneadedShape == KneadedEraserShape::Flat) return fmaxf(pencilRadius * 9.0f, 9.0f);
        return fmaxf(pencilRadius * 6.5f, 6.5f);
    }
    if (params.tool == ToolKind::ElectricEraser) return fmaxf(pencilRadius * 3.0f, 3.0f);
    if (params.tool == ToolKind::Tortillon) return fmaxf(pencilRadius * 7.0f, 7.0f);
    if (params.tool == ToolKind::FanBrush) return fmaxf(pencilRadius * 40.0f, 24.0f);
    if (params.tool == ToolKind::PowderBrush) return fmaxf(pencilRadius * 34.0f, 22.0f);
    if (params.tool == ToolKind::GraphitePowder) return fmaxf(pencilRadius * 36.0f, 24.0f);
    return pencilRadius;
}

__device__ float hashNoise(int x, int y)
{
    return fabsf(fmodf(sinf(x * 12.9898f + y * 78.233f) * 43758.547f, 1.0f));
}

__device__ float smoothNoise(int x, int y, int scale)
{
    const int sx = x / scale;
    const int sy = y / scale;
    const float fx = static_cast<float>(x - sx * scale) / static_cast<float>(scale);
    const float fy = static_cast<float>(y - sy * scale) / static_cast<float>(scale);
    const float a = hashNoise(sx, sy);
    const float b = hashNoise(sx + 1, sy);
    const float c = hashNoise(sx, sy + 1);
    const float d = hashNoise(sx + 1, sy + 1);
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);
    return a + (b - a) * ux + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}

__device__ std::uint32_t materialIndexFor(int x, int y, const std::uint32_t* tilePageTable, std::uint32_t tileSize, std::uint32_t tileColumns)
{
    const std::uint32_t tileX = static_cast<std::uint32_t>(x) / tileSize;
    const std::uint32_t tileY = static_cast<std::uint32_t>(y) / tileSize;
    const std::uint32_t inTileX = static_cast<std::uint32_t>(x) % tileSize;
    const std::uint32_t inTileY = static_cast<std::uint32_t>(y) % tileSize;
    const std::uint32_t tileIndex = tileY * tileColumns + tileX;
    const std::uint32_t pageIndex = tilePageTable[tileIndex];
    return pageIndex * tileSize * tileSize + inTileY * tileSize + inTileX;
}

__device__ bool materialTileAllocatedAt(
    const unsigned char* allocatedTiles,
    int x,
    int y,
    int width,
    int height,
    std::uint32_t tileSize,
    std::uint32_t tileColumns)
{
    if (!allocatedTiles || x < 0 || y < 0 || x >= width || y >= height) return false;
    const std::uint32_t tileIndex = static_cast<std::uint32_t>(y / tileSize) * tileColumns + static_cast<std::uint32_t>(x / tileSize);
    return allocatedTiles[tileIndex] != 0;
}

__device__ bool materialPageAllocatedAt(
    const std::uint32_t* tilePageTable,
    int x,
    int y,
    int width,
    int height,
    std::uint32_t tileSize,
    std::uint32_t tileColumns)
{
    if (!tilePageTable || x < 0 || y < 0 || x >= width || y >= height) return false;
    const std::uint32_t tileIndex = static_cast<std::uint32_t>(y / tileSize) * tileColumns + static_cast<std::uint32_t>(x / tileSize);
    return tilePageTable[tileIndex] != kUnallocatedTilePage;
}

struct PaperDefaults
{
    float height = 0.0f;
    float roughness = 0.0f;
    float binding = 0.0f;
};

__device__ PaperDefaults paperDefaultsAt(int x, int y, PaperPreset preset)
{
    float baseTooth = 0.45f;
    float toothNoise = 0.09f;
    float fiberNoise = 0.018f;
    float baseRoughness = 0.55f;
    float roughnessNoise = 0.09f;
    float baseBinding = 0.72f;
    float bindingNoise = 0.08f;
    if (preset == PaperPreset::SmoothBristol)
    {
        baseTooth = 0.34f;
        toothNoise = 0.035f;
        fiberNoise = 0.006f;
        baseRoughness = 0.28f;
        roughnessNoise = 0.035f;
        baseBinding = 0.83f;
        bindingNoise = 0.035f;
    }
    else if (preset == PaperPreset::RoughSketch)
    {
        baseTooth = 0.52f;
        toothNoise = 0.16f;
        fiberNoise = 0.035f;
        baseRoughness = 0.72f;
        roughnessNoise = 0.14f;
        baseBinding = 0.58f;
        bindingNoise = 0.12f;
    }
    return PaperDefaults{
        baseTooth + smoothNoise(x, y, 9) * toothNoise + smoothNoise(x, y, 31) * fiberNoise,
        baseRoughness + smoothNoise(x + 37, y - 19, 15) * roughnessNoise,
        baseBinding + smoothNoise(x - 11, y + 23, 23) * bindingNoise,
    };
}

__global__ void initializeTouchedMaterialTilesKernel(
    float* paperHeight,
    float* paperRoughness,
    float* paperBinding,
    float* looseGraphite,
    float* boundGraphite,
    float* compaction,
    float* damage,
    const std::uint32_t* tilePageTable,
    const std::uint32_t* touchedTileIndices,
    std::uint32_t touchedTileCount,
    std::uint32_t tileSize,
    std::uint32_t tileColumns,
    int width,
    int height,
    PaperPreset preset)
{
    const std::uint32_t pixelsPerTile = tileSize * tileSize;
    const std::uint32_t n = blockIdx.x * blockDim.x + threadIdx.x;
    if (n >= touchedTileCount * pixelsPerTile) return;
    const std::uint32_t tileIndex = touchedTileIndices[n / pixelsPerTile];
    if (tilePageTable[tileIndex] == kUnallocatedTilePage) return;
    const std::uint32_t inTile = n % pixelsPerTile;
    const std::uint32_t tileX = tileIndex % tileColumns;
    const std::uint32_t tileY = tileIndex / tileColumns;
    const int x = static_cast<int>(tileX * tileSize + inTile % tileSize);
    const int y = static_cast<int>(tileY * tileSize + inTile / tileSize);
    if (x >= width || y >= height) return;

    const int i = static_cast<int>(materialIndexFor(x, y, tilePageTable, tileSize, tileColumns));
    const PaperDefaults defaults = paperDefaultsAt(x, y, preset);
    paperHeight[i] = defaults.height;
    paperRoughness[i] = defaults.roughness;
    paperBinding[i] = defaults.binding;
    looseGraphite[i] = 0.0f;
    boundGraphite[i] = 0.0f;
    compaction[i] = 0.0f;
    damage[i] = 0.0f;
}

__global__ void clearToolLoadsKernel(float* toolLoads)
{
    const int i = threadIdx.x;
    if (i < 4) toolLoads[i] = 0.0f;
}

__global__ void clearToolLoadKernel(float* toolLoads, int index)
{
    if (threadIdx.x == 0 && index >= 0 && index < 4) toolLoads[index] = 0.0f;
}

// Non-pencil tools are mass-transfer tools: they may spend only graphite
// previously captured from the page, and they capture at most what they lift.
__device__ void captureToolLoad(float* toolLoads, int index, float amount)
{
    if (amount > 0.0f) atomicAdd(&toolLoads[index], amount);
}

__device__ float spendToolLoad(float* toolLoads, int index, float requested)
{
    requested = fmaxf(0.0f, requested);
    if (requested <= 0.0f) return 0.0f;

    const float old = atomicAdd(&toolLoads[index], -requested);
    if (old <= 0.0f)
    {
        atomicAdd(&toolLoads[index], requested);
        return 0.0f;
    }

    if (old < requested)
    {
        atomicAdd(&toolLoads[index], requested - old);
        return old;
    }

    return requested;
}

__device__ float graphiteToneAt(
    const float* looseGraphite,
    const float* boundGraphite,
    const std::uint32_t* tilePageTable,
    int width,
    int height,
    std::uint32_t tileSize,
    std::uint32_t tileColumns,
    int sampleX,
    int sampleY)
{
    const int sx = min(width - 1, max(0, sampleX));
    const int sy = min(height - 1, max(0, sampleY));
    if (!materialPageAllocatedAt(tilePageTable, sx, sy, width, height, tileSize, tileColumns)) return 0.0f;
    const int sampleIndex = static_cast<int>(materialIndexFor(sx, sy, tilePageTable, tileSize, tileColumns));
    return looseGraphite[sampleIndex] * 0.75f + boundGraphite[sampleIndex] * 0.25f;
}

__device__ float transferFromGraphiteSample(
    float* looseGraphite,
    float* boundGraphite,
    const std::uint32_t* tilePageTable,
    int width,
    int height,
    std::uint32_t tileSize,
    std::uint32_t tileColumns,
    int sampleX,
    int sampleY,
    int targetIndex,
    float localTone,
    float looseScale,
    float boundScale,
    float looseDepositRatio,
    float boundDepositRatio,
    float& captured)
{
    const int sx = min(width - 1, max(0, sampleX));
    const int sy = min(height - 1, max(0, sampleY));
    if (!materialPageAllocatedAt(tilePageTable, sx, sy, width, height, tileSize, tileColumns)) return 0.0f;

    const int sourceIndex = static_cast<int>(materialIndexFor(sx, sy, tilePageTable, tileSize, tileColumns));
    if (sourceIndex == targetIndex) return 0.0f;

    const float sourceLoose = looseGraphite[sourceIndex];
    const float sourceBound = boundGraphite[sourceIndex];
    const float sourceTone = sourceLoose * 0.75f + sourceBound * 0.25f;
    const float toneDelta = fmaxf(0.0f, sourceTone - localTone);
    if (toneDelta <= 0.0f) return 0.0f;

    const float liftedLoose = fminf(sourceLoose, toneDelta * looseScale);
    const float liftedBound = fminf(sourceBound, toneDelta * boundScale);
    looseGraphite[sourceIndex] = fmaxf(0.0f, sourceLoose - liftedLoose);
    boundGraphite[sourceIndex] = fmaxf(0.0f, sourceBound - liftedBound);

    const float deposited = liftedLoose * looseDepositRatio + liftedBound * boundDepositRatio;
    captured += fmaxf(0.0f, liftedLoose + liftedBound - deposited);
    return deposited;
}


__global__ void markTouchedTilesKernel(
    unsigned char* activeTiles,
    unsigned char* touchedTiles,
    std::uint32_t tileColumns,
    std::uint32_t tileRows,
    std::uint32_t minTileX,
    std::uint32_t maxTileX,
    std::uint32_t minTileY,
    std::uint32_t maxTileY)
{
    const std::uint32_t regionColumns = maxTileX - minTileX + 1;
    const std::uint32_t regionRows = maxTileY - minTileY + 1;
    const std::uint32_t regionCount = regionColumns * regionRows;
    const std::uint32_t n = blockIdx.x * blockDim.x + threadIdx.x;
    if (n >= regionCount) return;

    const std::uint32_t tileX = minTileX + n % regionColumns;
    const std::uint32_t tileY = minTileY + n / regionColumns;
    if (tileX >= tileColumns || tileY >= tileRows) return;
    const std::uint32_t tileIndex = tileY * tileColumns + tileX;
    activeTiles[tileIndex] = 1;
    touchedTiles[tileIndex] = 1;
}

__global__ void strokeKernel(
    float* paperHeight,
    float* paperRoughness,
    float* paperBinding,
    float* looseGraphite,
    float* boundGraphite,
    float* compaction,
    float* damage,
    float* toolLoads,
    const std::uint32_t* tilePageTable,
    const std::uint32_t* touchedTileIndices,
    std::uint32_t touchedTileCount,
    std::uint32_t tileSize,
    std::uint32_t tileColumns,
    int width,
    int height,
    StrokePacket from,
    StrokePacket to,
    ToolParams params)
{
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.01f) return;

    const float softness = gradeSoftness(params.grade);
    const float pressure = clamp01((from.pressure + to.pressure) * 0.5f);
    const float speed = fmaxf(from.speed, to.speed);
    const float speedFactor = clamp01(speed / 1800.0f);
    const float radius = effectiveToolRadiusPx(params);
    // With half-open capsule ownership (each pixel deposits from exactly one
    // segment; see tRaw cull below) deposit per pixel must NOT scale with
    // segment length - every stroke pixel is visited exactly once regardless
    // of how finely the stroke is packetized.
    const float segmentDepositScale = 1.0f;
    const std::uint32_t pixelsPerTile = tileSize * tileSize;
    const std::uint32_t n = blockIdx.x * blockDim.x + threadIdx.x;
    if (n >= touchedTileCount * pixelsPerTile) return;
    const std::uint32_t tileIndex = touchedTileIndices[n / pixelsPerTile];
    if (tilePageTable[tileIndex] == kUnallocatedTilePage) return;
    const std::uint32_t tileX = tileIndex % tileColumns;
    const std::uint32_t tileY = tileIndex / tileColumns;
    const int x = static_cast<int>(tileX * tileSize + n % pixelsPerTile % tileSize);
    const int y = static_cast<int>(tileY * tileSize + n % pixelsPerTile / tileSize);
    if (x >= width || y >= height) return;
    const float px = static_cast<float>(x);
    const float py = static_cast<float>(y);
    const float tRaw = ((px - from.x) * dx + (py - from.y) * dy) / fmaxf(length * length, 0.001f);
    // Half-open capsule ownership: each pencil pixel deposits from exactly
    // one segment (its projection interval t in [0,1)). Without the culls,
    // adjacent capsules' round caps both deposit around every packet joint,
    // producing dark beads at packet spacing. The first segment of a stroke
    // (strokeDistancePx == 0) keeps its start cap so marks begin rounded.
    // Pencil only: deposit tools want exactly-once pixel ownership.
    // Material-moving tools (tortillon/fan) keep overlapping applications -
    // overlap IS the smear - but get per-segment dose normalization below
    // so total effect per pass is packetization-independent.
    if (params.tool == ToolKind::Pencil)
    {
        if (tRaw < 0.0f && from.strokeDistancePx > 0.0f) return;
        if (tRaw >= 1.0f && length > 0.01f) return;
    }
    const float t = clamp01(tRaw);
    const float cx = from.x + dx * t;
    const float cy = from.y + dy * t;
    const float localX = px - cx;
    const float localY = py - cy;
    float distancePx = sqrtf(localX * localX + localY * localY);
    if (params.tool == ToolKind::Pencil)
    {
        const float tiltMagnitude = clamp01((fabsf(from.tiltX) + fabsf(to.tiltX) + fabsf(from.tiltY) + fabsf(to.tiltY)) / 180.0f);
        const float angle = to.hasRotation ? to.rotation : (to.orientation + kPi * 0.5f);
        const float ca = cosf(angle);
        const float sa = sinf(angle);
        const float majorScale = 1.0f + tiltMagnitude * 0.95f;
        const float minorScale = fmaxf(0.55f, 1.0f - tiltMagnitude * 0.30f);
        const float aligned = (localX * ca + localY * sa) / majorScale;
        const float cross = (-localX * sa + localY * ca) / minorScale;
        distancePx = sqrtf(aligned * aligned + cross * cross);
    }
    else if (params.tool == ToolKind::KneadedEraser && params.kneadedShape == KneadedEraserShape::Edge)
    {
        const float angle = length > 0.01f ? atan2f(dy, dx) : to.orientation;
        const float ca = cosf(angle);
        const float sa = sinf(angle);
        const float along = (localX * ca + localY * sa) / 1.18f;
        const float cross = (-localX * sa + localY * ca) / 0.32f;
        distancePx = sqrtf(along * along + cross * cross);
    }
    else if (params.tool == ToolKind::KneadedEraser && params.kneadedShape == KneadedEraserShape::Flat)
    {
        const float angle = length > 0.01f ? atan2f(dy, dx) : to.orientation;
        const float ca = cosf(angle);
        const float sa = sinf(angle);
        const float along = (localX * ca + localY * sa) / 1.35f;
        const float cross = (-localX * sa + localY * ca) / 0.72f;
        distancePx = sqrtf(along * along + cross * cross);
    }
    else if (params.tool == ToolKind::FanBrush)
    {
        const float angle = atan2f(dy, dx) + kPi * 0.5f;
        const float ca = cosf(angle);
        const float sa = sinf(angle);
        const float fanWidth = localX * ca + localY * sa;
        const float fanDepth = (-localX * sa + localY * ca) / 0.45f;
        distancePx = sqrtf(fanWidth * fanWidth + fanDepth * fanDepth);
    }
    const float rasterFootprintPx = params.tool == ToolKind::Pencil ? 0.38f : 0.20f;
    float dist = fmaxf(0.0f, distancePx - rasterFootprintPx) / radius;
    if (dist > 1.0f) return;

    const int idx = static_cast<int>(materialIndexFor(x, y, tilePageTable, tileSize, tileColumns));
    const float heightTooth = paperHeight[idx] * (1.0f - compaction[idx] * 0.55f);
    const float roughness = clamp01(paperRoughness[idx] - compaction[idx] * 0.38f + damage[idx] * 0.12f);
    const float catchTooth = clamp01(heightTooth * 0.35f + roughness * 0.65f);
    const float binding = clamp01(paperBinding[idx] - damage[idx] * 0.38f);
    float edge = powf(1.0f - dist, params.tool == ToolKind::Pencil ? 0.82f : 1.35f);
    if (params.tool == ToolKind::Pencil)
    {
        // Taper by TRUE stroke arc length (strokeDistancePx accumulated by
        // GraphiteDocument), not per-segment endpoint distance. Per-segment
        // feathering suppressed deposit at every interior packet joint,
        // beading strokes at packet spacing (worst on slow strokes where
        // every pixel of a ~1px segment sat inside the feather). With stroke
        // distance, only the first ~feather px of the whole mark taper.
        const float strokeDistanceAtPixel = from.strokeDistancePx + t * length;
        const float endpointFeather = fmaxf(radius * 1.25f, 0.85f);
        const float endpointFloor = 0.08f;
        edge *= endpointFloor + (1.0f - endpointFloor) * smooth01(strokeDistanceAtPixel / endpointFeather);
    }
    if (params.tool == ToolKind::Tortillon || params.tool == ToolKind::FanBrush)
    {
        // Dose normalization for material-moving tools: at high packet rates
        // a pixel sits inside ~2*radius/length overlapping segment capsules
        // per pass, multiplying the applied smear by packet density. Scale
        // each application by length/(2*radius) so the accumulated dose per
        // pass is the same regardless of how finely the stroke is sliced.
        edge *= clamp01(length / fmaxf(radius * 2.0f, 0.5f));
    }

    if (params.tool == ToolKind::ElectricEraser)
    {
        const float lift = edge * (0.72f + pressure * 0.32f);
        const float boundGhostFloor = 0.0035f + (1.0f - binding) * 0.0025f;
        looseGraphite[idx] = fmaxf(0.0f, looseGraphite[idx] - lift * 1.08f);
        boundGraphite[idx] = boundGraphite[idx] > boundGhostFloor
            ? fmaxf(boundGhostFloor, boundGraphite[idx] - lift * (0.30f + (1.0f - binding) * 0.35f))
            : boundGraphite[idx];
        compaction[idx] = clamp01(compaction[idx] + edge * pressure * 0.018f);
        damage[idx] = clamp01(damage[idx] + edge * pressure * pressure * 0.009f);
        paperBinding[idx] = clamp01(paperBinding[idx] - edge * pressure * pressure * 0.0028f);
        return;
    }

    if (params.tool == ToolKind::RegularEraser || to.isEraser)
    {
        const float pressureCurve = powf(pressure, 1.65f);
        const float lift = edge * (0.08f + pressureCurve * 0.82f);
        const float previousLoose = looseGraphite[idx];
        const float previousBound = boundGraphite[idx];
        const float looseResidueFloor = 0.030f - pressureCurve * 0.016f;
        const float boundResidueFloor = 0.020f + (1.0f - binding) * 0.014f - pressureCurve * 0.010f;
        looseGraphite[idx] = previousLoose > looseResidueFloor
            ? fmaxf(looseResidueFloor, previousLoose - lift * (0.72f + pressureCurve * 0.30f))
            : previousLoose;
        boundGraphite[idx] = previousBound > boundResidueFloor
            ? fmaxf(boundResidueFloor, previousBound - lift * (0.05f + pressureCurve * 0.12f + (1.0f - binding) * 0.20f))
            : previousBound;
        compaction[idx] = clamp01(compaction[idx] + edge * pressure * 0.011f);
        damage[idx] = clamp01(damage[idx] + edge * pressure * pressure * 0.0055f);
        paperBinding[idx] = clamp01(paperBinding[idx] - edge * pressure * pressure * 0.0016f);
        return;
    }

    if (params.tool == ToolKind::KneadedEraser)
    {
        const float shapeLift =
            params.kneadedShape == KneadedEraserShape::Point ? 1.45f :
            params.kneadedShape == KneadedEraserShape::Edge ? 1.18f :
            params.kneadedShape == KneadedEraserShape::Flat ? 0.72f :
            1.0f;
        const float shapeTexture =
            params.kneadedShape == KneadedEraserShape::Flat ? 0.86f :
            params.kneadedShape == KneadedEraserShape::Point ? 1.08f :
            1.0f;
        const float motionDab = clamp01(1.0f - length / fmaxf(radius * 0.65f, 0.1f)) * clamp01(1.0f - speed / 420.0f);
        const float motionRoll = clamp01(length / fmaxf(radius * 0.80f, 0.1f)) * clamp01(1.0f - speed / 1300.0f);
        const float motionDrag = clamp01(speed / 1600.0f);
        const float pressureCurve = powf(pressure, 1.25f);
        const float mottled = (0.50f + smoothNoise(x + 7, y - 13, 4) * 0.34f + hashNoise(x * 5, y * 7) * 0.16f) * shapeTexture;
        const float eraserLoad = clamp01(toolLoads[3]);
        const float saturation = 1.0f - eraserLoad * 0.68f;
        const float lift = edge * mottled * (0.025f + pressureCurve * 0.34f) * saturation * shapeLift * (0.72f + motionDab * 0.70f + motionRoll * 0.28f - motionDrag * 0.18f);
        const float looseResidueFloor =
            params.kneadedShape == KneadedEraserShape::Point ? 0.030f :
            params.kneadedShape == KneadedEraserShape::Edge ? 0.034f :
            params.kneadedShape == KneadedEraserShape::Flat ? 0.060f :
            0.045f;
        const float boundResidueFloor =
            (params.kneadedShape == KneadedEraserShape::Point ? 0.020f :
                params.kneadedShape == KneadedEraserShape::Edge ? 0.024f :
                params.kneadedShape == KneadedEraserShape::Flat ? 0.038f :
                0.030f) + (1.0f - binding) * 0.018f;
        const float liftedLoose = looseGraphite[idx] > looseResidueFloor
            ? fminf(looseGraphite[idx] - looseResidueFloor, lift * (0.62f + motionDab * 0.22f + motionRoll * 0.10f))
            : 0.0f;
        const float liftedBound = boundGraphite[idx] > boundResidueFloor
            ? fminf(boundGraphite[idx] - boundResidueFloor, lift * (0.018f + pressureCurve * 0.035f + (1.0f - binding) * 0.075f))
            : 0.0f;
        const float dragSoftening = motionDrag * edge * fminf(looseGraphite[idx], looseGraphite[idx] * (0.012f + pressureCurve * 0.025f));
        const float redeposit = spendToolLoad(toolLoads, 3, edge * eraserLoad * (0.0015f + motionDrag * 0.0065f + pressureCurve * 0.0035f));
        looseGraphite[idx] = fmaxf(0.0f, looseGraphite[idx] - liftedLoose);
        boundGraphite[idx] = fmaxf(0.0f, boundGraphite[idx] - liftedBound);
        looseGraphite[idx] = fminf(1.0f, looseGraphite[idx] - dragSoftening * 0.22f + redeposit);
        captureToolLoad(toolLoads, 3, (liftedLoose + liftedBound + dragSoftening * 0.45f) * 0.58f);
        compaction[idx] = clamp01(compaction[idx] + edge * pressure * 0.0010f);
        damage[idx] = clamp01(damage[idx] + edge * pressure * 0.0004f);
        return;
    }

    if (params.tool == ToolKind::Tortillon)
    {
        const float invLength = rsqrtf(fmaxf(length * length, 0.001f));
        const float dirX = dx * invLength;
        const float dirY = dy * invLength;
        const float sideX = -dirY;
        const float sideY = dirX;
        const float sampleDistance = radius * 0.42f;
        const int sx = min(width - 1, max(0, static_cast<int>(roundf(px - dirX * sampleDistance))));
        const int sy = min(height - 1, max(0, static_cast<int>(roundf(py - dirY * sampleDistance))));
        const bool sourceAllocated = materialPageAllocatedAt(tilePageTable, sx, sy, width, height, tileSize, tileColumns);
        const int srcIdx = sourceAllocated ? static_cast<int>(materialIndexFor(sx, sy, tilePageTable, tileSize, tileColumns)) : 0;
        const bool separateSource = sourceAllocated && srcIdx != idx;
        const float blendPressure = 0.24f + 0.76f * sqrtf(pressure);
        const float transport = edge * (0.12f + blendPressure * 0.46f) * (0.75f + speedFactor * 0.45f);
        const int loadIndex = 0;
        const float toolLoad = clamp01(toolLoads[loadIndex]);
        const float liftedFromSource = separateSource ? fminf(looseGraphite[srcIdx], looseGraphite[srcIdx] * transport * 0.38f) : 0.0f;
        const float redepositLoad = spendToolLoad(toolLoads, loadIndex, edge * toolLoad * 0.012f);
        const float localLoose = looseGraphite[idx];
        const float localBound = boundGraphite[idx];
        const float localTone = localLoose * 0.75f + localBound * 0.25f;
        const float neighborTone =
            (graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + dirX * radius * 0.22f)), static_cast<int>(roundf(py + dirY * radius * 0.22f))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - dirX * radius * 0.22f)), static_cast<int>(roundf(py - dirY * radius * 0.22f))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + sideX * radius * 0.18f)), static_cast<int>(roundf(py + sideY * radius * 0.18f))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - sideX * radius * 0.18f)), static_cast<int>(roundf(py - sideY * radius * 0.18f)))) * 0.25f;
        const float lineExcess = fmaxf(0.0f, localTone - neighborTone);
        const float liftedLocalLoose = fminf(localLoose, edge * lineExcess * (0.030f + blendPressure * 0.115f));
        const float softenedBound = fminf(localBound, edge * lineExcess * (0.0020f + blendPressure * 0.010f));
        // ATOMIC cross-pixel writes: the drag scatter-writes a NEIGHBOR pixel
        // while that pixel's own thread writes itself in the same launch.
        // Plain stores lose one of the two updates along warp-coherent runs,
        // which rendered as fine streak bands. Atomic adds make concurrent
        // updates sum instead of clobbering. (Values are re-clamped by every
        // consumer; transient sub-epsilon overdraft is visually harmless.)
        if (separateSource)
        {
            atomicAdd(&looseGraphite[srcIdx], -liftedFromSource);
        }
        atomicAdd(&looseGraphite[idx], -liftedLocalLoose + liftedFromSource * 0.72f + redepositLoad);
        atomicAdd(&boundGraphite[idx], -softenedBound + liftedFromSource * 0.07f);
        captureToolLoad(toolLoads, loadIndex, liftedFromSource * 0.21f + liftedLocalLoose * 0.45f + softenedBound * 0.65f);
        compaction[idx] = clamp01(compaction[idx] + edge * pressure * 0.009f);
        return;
    }

    if (params.tool == ToolKind::FanBrush)
    {
        const float brushLoad = clamp01(toolLoads[1]);
        const float smoothGrain = clamp01(0.30f + smoothNoise(x + 3, y - 5, 5) * 0.36f + smoothNoise(x - 23, y + 17, 17) * 0.34f);
        const float toothHold = clamp01(catchTooth * 0.72f + roughness * 0.28f);
        const float textureBreak = clamp01(0.36f + smoothGrain * 0.34f + (1.0f - toothHold) * 0.30f);
        const float brushContact = edge * textureBreak * (0.70f + speedFactor * 0.20f);
        const float blendPressure = 0.34f + 0.66f * sqrtf(pressure);
        const float localLoose = looseGraphite[idx];
        const float localBound = boundGraphite[idx];
        const float localTone = localLoose * 0.75f + localBound * 0.25f;
        const float ringA = radius * (0.10f + hashNoise(x + 19, y - 31) * 0.035f);
        const float ringB = radius * (0.22f + hashNoise(x - 43, y + 11) * 0.045f);
        const float diagA = ringA * 0.70710678f;
        const float diagB = ringB * 0.70710678f;
        const float neighborTone =
            (graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + ringA)), static_cast<int>(roundf(py))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - ringA)), static_cast<int>(roundf(py))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px)), static_cast<int>(roundf(py + ringA))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px)), static_cast<int>(roundf(py - ringA))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + diagB)), static_cast<int>(roundf(py + diagB))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - diagB)), static_cast<int>(roundf(py + diagB))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + diagB)), static_cast<int>(roundf(py - diagB))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - diagB)), static_cast<int>(roundf(py - diagB)))) * 0.125f;
        const float lineExcess = fmaxf(0.0f, localTone - neighborTone);
        float captured = 0.0f;
        float blendedIn = 0.0f;
        const float blendSlip = clamp01(1.0f - toothHold * 0.62f - compaction[idx] * 0.10f);
        const float transferLoose = brushContact * blendSlip * (0.060f + blendPressure * 0.180f);
        const float transferBound = brushContact * blendSlip * (0.0016f + blendPressure * 0.0060f);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + ringA)), static_cast<int>(roundf(py)), idx, localTone, transferLoose, transferBound, 0.90f, 0.62f, captured);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - ringA)), static_cast<int>(roundf(py)), idx, localTone, transferLoose, transferBound, 0.90f, 0.62f, captured);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px)), static_cast<int>(roundf(py + ringA)), idx, localTone, transferLoose, transferBound, 0.90f, 0.62f, captured);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px)), static_cast<int>(roundf(py - ringA)), idx, localTone, transferLoose, transferBound, 0.90f, 0.62f, captured);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + diagA)), static_cast<int>(roundf(py + diagA)), idx, localTone, transferLoose * 0.62f, transferBound * 0.62f, 0.86f, 0.54f, captured);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - diagA)), static_cast<int>(roundf(py + diagA)), idx, localTone, transferLoose * 0.62f, transferBound * 0.62f, 0.86f, 0.54f, captured);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + diagA)), static_cast<int>(roundf(py - diagA)), idx, localTone, transferLoose * 0.62f, transferBound * 0.62f, 0.86f, 0.54f, captured);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - diagA)), static_cast<int>(roundf(py - diagA)), idx, localTone, transferLoose * 0.62f, transferBound * 0.62f, 0.86f, 0.54f, captured);

        const float peakLift = fminf(localLoose, brushContact * blendSlip * localLoose * (0.006f + blendPressure * 0.018f) + brushContact * lineExcess * (0.018f + blendPressure * 0.070f));
        const float boundDust = fminf(localBound, brushContact * blendSlip * (0.00012f + blendPressure * 0.00055f) + brushContact * blendSlip * lineExcess * (0.0060f + blendPressure * 0.0280f));
        const float redeposit = spendToolLoad(toolLoads, 1, brushContact * brushLoad * (0.000001f + pressure * 0.000004f) * (0.35f + clamp01(localTone) * 0.65f));
        looseGraphite[idx] = clamp01(localLoose - peakLift * 0.18f + boundDust * 0.82f + blendedIn + redeposit);
        boundGraphite[idx] = fmaxf(0.0f, localBound - boundDust);
        captureToolLoad(toolLoads, 1, captured * 0.18f + peakLift * 0.08f + boundDust * 0.05f);
        compaction[idx] = clamp01(compaction[idx] + brushContact * pressure * 0.00055f);
        return;
    }

    if (params.tool == ToolKind::PowderBrush)
    {
        const float toothCatch = clamp01(0.18f + catchTooth * 0.82f + binding * 0.10f - compaction[idx] * 0.30f - damage[idx] * 0.24f);
        const float toothHold = clamp01(catchTooth * 0.68f + roughness * 0.32f);
        const float blendSlip = clamp01(1.0f - toothHold * 0.58f - compaction[idx] * 0.10f);
        const float brushLoad = clamp01(toolLoads[2]);
        const float localLoose = looseGraphite[idx];
        const float localBound = boundGraphite[idx];
        const float localTone = localLoose * 0.75f + localBound * 0.25f;
        const float invLength = rsqrtf(fmaxf(length * length, 0.001f));
        const float dirX = dx * invLength;
        const float dirY = dy * invLength;
        const float sideX = -dirY;
        const float sideY = dirX;
        const float blendPressure = 0.38f + 0.62f * sqrtf(pressure);
        const float neighborTone =
            (graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + dirX * radius * 0.22f)), static_cast<int>(roundf(py + dirY * radius * 0.22f))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - dirX * radius * 0.22f)), static_cast<int>(roundf(py - dirY * radius * 0.22f))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + sideX * radius * 0.22f)), static_cast<int>(roundf(py + sideY * radius * 0.22f))) +
                graphiteToneAt(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - sideX * radius * 0.22f)), static_cast<int>(roundf(py - sideY * radius * 0.22f)))) * 0.25f;
        const float lineExcess = fmaxf(0.0f, localTone - neighborTone);
        float captured = 0.0f;
        float blendedIn = 0.0f;
        const float textureBreak = clamp01(0.42f + smoothNoise(x - 41, y + 9, 7) * 0.28f + (1.0f - toothHold) * 0.30f);
        const float transferLoose = edge * textureBreak * blendSlip * (0.050f + blendPressure * 0.145f);
        const float transferBound = edge * textureBreak * blendSlip * (0.0010f + blendPressure * 0.0044f);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - dirX * radius * 0.20f)), static_cast<int>(roundf(py - dirY * radius * 0.20f)), idx, localTone, transferLoose, transferBound, 0.70f, 0.12f, captured);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px + sideX * radius * 0.20f)), static_cast<int>(roundf(py + sideY * radius * 0.20f)), idx, localTone, transferLoose * 0.45f, transferBound * 0.45f, 0.66f, 0.10f, captured);
        blendedIn += transferFromGraphiteSample(looseGraphite, boundGraphite, tilePageTable, width, height, tileSize, tileColumns, static_cast<int>(roundf(px - sideX * radius * 0.20f)), static_cast<int>(roundf(py - sideY * radius * 0.20f)), idx, localTone, transferLoose * 0.45f, transferBound * 0.45f, 0.66f, 0.10f, captured);
        const float lift = fminf(localLoose, edge * textureBreak * blendSlip * localLoose * (0.007f + blendPressure * 0.024f) + edge * textureBreak * blendSlip * lineExcess * (0.014f + blendPressure * 0.055f));
        const float boundDust = fminf(localBound, edge * textureBreak * blendSlip * (0.00018f + blendPressure * 0.00070f) + edge * textureBreak * blendSlip * lineExcess * (0.0048f + blendPressure * 0.0220f));
        const float powder = spendToolLoad(toolLoads, 2, edge * toothCatch * textureBreak * brushLoad * (0.00018f + pressure * 0.00075f) * (1.0f - speedFactor * 0.38f));
        looseGraphite[idx] = clamp01(localLoose - lift * 0.14f + boundDust * 0.86f + blendedIn + powder);
        boundGraphite[idx] = fmaxf(0.0f, localBound - boundDust);
        captureToolLoad(toolLoads, 2, captured * 0.12f + lift * 0.06f + boundDust * 0.04f);
        compaction[idx] = clamp01(compaction[idx] + edge * pressure * 0.001f);
        return;
    }

    if (params.tool == ToolKind::GraphitePowder)
    {
        const float toothCatch = clamp01(0.20f + catchTooth * 0.76f + roughness * 0.18f + binding * 0.06f - compaction[idx] * 0.22f - damage[idx] * 0.18f);
        const float textureBreak = clamp01(0.44f + smoothNoise(x - 29, y + 37, 9) * 0.32f + hashNoise(x * 3, y * 5) * 0.10f + (1.0f - toothCatch) * 0.14f);
        const float powderPressure = 0.35f + 0.65f * sqrtf(pressure);
        const float existingFilm = clamp01(looseGraphite[idx] * 0.88f + boundGraphite[idx] * 0.30f);
        const float powder = edge * toothCatch * textureBreak * powderPressure * (0.0018f + pressure * 0.0054f) * (1.0f - speedFactor * 0.42f) * (1.0f - existingFilm * 0.64f);
        const float settled = powder * (0.035f + pressure * 0.040f + binding * 0.025f);
        looseGraphite[idx] = clamp01(looseGraphite[idx] + powder);
        boundGraphite[idx] = clamp01(boundGraphite[idx] + settled);
        compaction[idx] = clamp01(compaction[idx] + edge * pressure * 0.00035f);
        return;
    }

    const float graphiteFill = clamp01(boundGraphite[idx] * 0.58f + looseGraphite[idx] * 0.24f);
    const float graphiteToneLoad = clamp01(boundGraphite[idx] * 1.20f + looseGraphite[idx] * 0.85f);
    const float gradeStrength = gradeDepositStrength(params.grade);
    const float looseStrength = gradeLooseStrength(params.grade);
    const float boundStrength = gradeBoundStrength(params.grade);
    const float pressureVariance = gradePressureVariance(params.grade, pressure);
    const float toneCapacity = clamp01(gradeToneCapacity(params.grade, pressure));
    const float toneRoom = clamp01((toneCapacity - graphiteToneLoad) / fmaxf(toneCapacity, 0.001f));
    const float openTooth = clamp01(1.0f - graphiteFill * (0.68f + softness * 0.12f) - compaction[idx] * 0.14f + damage[idx] * 0.05f);
    const float catchFactor = clamp01(0.25f + catchTooth * 0.70f + binding * 0.12f + graphiteFill * 0.05f - compaction[idx] * 0.24f + damage[idx] * 0.08f);
    const float fineTooth = smoothNoise(x + 13, y - 29, 2);
    const float fiberTooth = smoothNoise(x + 5, y - 7, 6);
    const float broadTooth = smoothNoise(x - 17, y + 11, 19);
    const float paperGrain = clamp01(0.48f + fineTooth * 0.10f + fiberTooth * 0.08f + broadTooth * 0.05f + catchTooth * 0.12f - compaction[idx] * 0.06f);
    const float saturationGate = smooth01(toneRoom) * (0.16f + openTooth * 0.84f);
    // Rate re-anchored after exactly-once capsule ownership (2026-07-05):
    // constants below were tuned when overlapping capsules multi-hit every
    // pixel ~3-5x per pass. Single-coverage targets (tone = b*1.2 + l*0.85,
    // one pass, p=0.5): 2H ~0.05, HB ~0.15, 2B ~0.25, 8B ~0.45. True
    // physical anchoring comes from the material calibration protocol.
    const float pressureDeposit = (0.075f + powf(pressure, 1.45f) * 0.56f) * gradeStrength * pressureVariance;
    const float deposit = edge * segmentDepositScale * saturationGate * pressureDeposit * (0.28f + softness * 0.62f) * (1.0f - speedFactor * 0.24f);
    const float contactCore = powf(edge, 2.4f) * segmentDepositScale * (0.72f + fineTooth * 0.28f) * saturationGate;
    const float loose = deposit * looseStrength * (0.84f + softness * 0.92f) * catchFactor * paperGrain * (0.22f + openTooth * 0.78f);
    const float bound = deposit * boundStrength * (0.045f + pressure * 0.16f) * (0.34f + catchTooth * 0.62f + binding * 0.20f) * (0.42f + paperGrain * 0.58f) * (0.30f + openTooth * 0.70f)
        + contactCore * pressureDeposit * (0.018f + pressure * 0.026f) * (0.25f + openTooth * 0.75f);
    looseGraphite[idx] = fminf(1.0f, looseGraphite[idx] + loose);
    boundGraphite[idx] = fminf(1.0f, boundGraphite[idx] + bound);
    compaction[idx] = clamp01(compaction[idx] + edge * segmentDepositScale * pressure * (0.0025f + (1.0f - softness) * 0.006f) * (1.0f - speedFactor * 0.25f));
}

__device__ float importedGraphiteAt(const float* graphite, std::uint32_t sourceWidth, std::uint32_t sourceHeight, float sourceX, float sourceY)
{
    if (!graphite || sourceWidth == 0 || sourceHeight == 0) return 0.0f;
    sourceX = fminf(static_cast<float>(sourceWidth - 1), fmaxf(0.0f, sourceX));
    sourceY = fminf(static_cast<float>(sourceHeight - 1), fmaxf(0.0f, sourceY));
    const int x0 = static_cast<int>(floorf(sourceX));
    const int y0 = static_cast<int>(floorf(sourceY));
    const int x1 = min(static_cast<int>(sourceWidth - 1), x0 + 1);
    const int y1 = min(static_cast<int>(sourceHeight - 1), y0 + 1);
    const float tx = sourceX - static_cast<float>(x0);
    const float ty = sourceY - static_cast<float>(y0);
    const float a = graphite[y0 * sourceWidth + x0];
    const float b = graphite[y0 * sourceWidth + x1];
    const float c = graphite[y1 * sourceWidth + x0];
    const float d = graphite[y1 * sourceWidth + x1];
    return clamp01(a + (b - a) * tx + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty);
}

__global__ void importSketchMaterialKernel(
    float* paperHeight,
    float* paperRoughness,
    float* paperBinding,
    float* looseGraphite,
    float* boundGraphite,
    float* compaction,
    float* damage,
    const std::uint32_t* tilePageTable,
    const std::uint32_t* touchedTileIndices,
    std::uint32_t touchedTileCount,
    std::uint32_t tileSize,
    std::uint32_t tileColumns,
    int width,
    int height,
    const float* importedGraphite,
    std::uint32_t sourceWidth,
    std::uint32_t sourceHeight,
    float targetX,
    float targetY,
    float targetWidth,
    float targetHeight)
{
    const std::uint32_t pixelsPerTile = tileSize * tileSize;
    const std::uint32_t n = blockIdx.x * blockDim.x + threadIdx.x;
    if (n >= touchedTileCount * pixelsPerTile) return;
    const std::uint32_t tileIndex = touchedTileIndices[n / pixelsPerTile];
    if (tilePageTable[tileIndex] == kUnallocatedTilePage) return;
    const std::uint32_t inTile = n % pixelsPerTile;
    const std::uint32_t tileX = tileIndex % tileColumns;
    const std::uint32_t tileY = tileIndex / tileColumns;
    const int x = static_cast<int>(tileX * tileSize + inTile % tileSize);
    const int y = static_cast<int>(tileY * tileSize + inTile / tileSize);
    if (x >= width || y >= height) return;

    const float localX = (static_cast<float>(x) + 0.5f - targetX) / fmaxf(targetWidth, 0.001f);
    const float localY = (static_cast<float>(y) + 0.5f - targetY) / fmaxf(targetHeight, 0.001f);
    if (localX < 0.0f || localY < 0.0f || localX > 1.0f || localY > 1.0f) return;

    float graphite = importedGraphiteAt(
        importedGraphite,
        sourceWidth,
        sourceHeight,
        localX * static_cast<float>(sourceWidth - 1),
        localY * static_cast<float>(sourceHeight - 1));
    if (graphite <= 0.001f) return;

    const int idx = static_cast<int>(materialIndexFor(x, y, tilePageTable, tileSize, tileColumns));
    const float tooth = clamp01(paperHeight[idx] * (1.0f - compaction[idx] * 0.42f));
    const float roughness = clamp01(paperRoughness[idx] - compaction[idx] * 0.24f + damage[idx] * 0.08f);
    const float binding = clamp01(paperBinding[idx] - damage[idx] * 0.18f);
    const float catchTooth = clamp01(tooth * 0.34f + roughness * 0.66f);
    const float paperGrain = clamp01(0.72f + smoothNoise(x + 31, y - 17, 3) * 0.18f + smoothNoise(x - 13, y + 41, 13) * 0.10f);
    graphite = powf(graphite, 1.08f) * paperGrain;

    const float existingFilm = clamp01(looseGraphite[idx] * 0.86f + boundGraphite[idx] * 1.18f);
    const float room = clamp01(1.0f - existingFilm * 0.68f);
    const float loose = graphite * room * (0.25f + catchTooth * 0.35f + roughness * 0.12f);
    const float bound = graphite * room * (0.18f + binding * 0.13f + catchTooth * 0.16f);
    looseGraphite[idx] = clamp01(looseGraphite[idx] + loose);
    boundGraphite[idx] = clamp01(boundGraphite[idx] + bound);
    compaction[idx] = clamp01(compaction[idx] + graphite * (0.018f + binding * 0.018f));
    damage[idx] = clamp01(damage[idx] + graphite * 0.0015f);
}

__device__ uchar4 grayscale(float value)
{
    const unsigned char v = static_cast<unsigned char>(clamp01(value) * 255.0f);
    return make_uchar4(v, v, v, 255);
}

__device__ float paperHeightOrDefault(
    const float* paperHeight,
    const unsigned char* allocatedTiles,
    const std::uint32_t* tilePageTable,
    int width,
    int height,
    std::uint32_t tileSize,
    std::uint32_t tileColumns,
    PaperPreset preset,
    int x,
    int y)
{
    const int sx = min(width - 1, max(0, x));
    const int sy = min(height - 1, max(0, y));
    if (!materialTileAllocatedAt(allocatedTiles, sx, sy, width, height, tileSize, tileColumns))
    {
        return paperDefaultsAt(sx, sy, preset).height;
    }
    return paperHeight[materialIndexFor(sx, sy, tilePageTable, tileSize, tileColumns)];
}

__device__ float surfaceSheenAt(
    const float* paperHeight,
    const float* looseGraphite,
    const float* boundGraphite,
    const float* compaction,
    const float* damage,
    const unsigned char* allocatedTiles,
    const std::uint32_t* tilePageTable,
    int width,
    int height,
    std::uint32_t tileSize,
    std::uint32_t tileColumns,
    PaperPreset preset,
    int x,
    int y)
{
    if (!materialTileAllocatedAt(allocatedTiles, x, y, width, height, tileSize, tileColumns)) return 0.0f;
    const int i = static_cast<int>(materialIndexFor(x, y, tilePageTable, tileSize, tileColumns));
    const float dx = paperHeightOrDefault(paperHeight, allocatedTiles, tilePageTable, width, height, tileSize, tileColumns, preset, x + 1, y) -
        paperHeightOrDefault(paperHeight, allocatedTiles, tilePageTable, width, height, tileSize, tileColumns, preset, x - 1, y);
    const float dy = paperHeightOrDefault(paperHeight, allocatedTiles, tilePageTable, width, height, tileSize, tileColumns, preset, x, y + 1) -
        paperHeightOrDefault(paperHeight, allocatedTiles, tilePageTable, width, height, tileSize, tileColumns, preset, x, y - 1);
    const float normalFacing = clamp01(1.0f - sqrtf(dx * dx + dy * dy) * 2.4f);
    const float graphiteFilm = clamp01(boundGraphite[i] * 0.75f + looseGraphite[i] * 0.22f);
    const float burnish = clamp01(compaction[i] * 1.35f - damage[i] * 0.55f);
    return clamp01(normalFacing * graphiteFilm * (0.18f + burnish * 0.82f));
}

__device__ uchar4 displayTonePixel(
    int x,
    int y,
    float tooth,
    float roughness,
    float loose,
    float bound,
    float compacted,
    float damaged,
    float sheen)
{
    const float graphite = clamp01(bound * 1.38f + loose * 1.08f);
    const float fiber = smoothNoise(x + 101, y - 53, 29) * 0.0035f + smoothNoise(x - 17, y + 71, 7) * 0.0020f;
    const float paperValue = clamp01(0.952f + tooth * 0.010f - roughness * 0.004f + fiber - compacted * 0.026f - damaged * 0.036f);
    const float graphiteShade = graphite * (1.18f + compacted * 0.18f);
    const float r = clamp01(paperValue + 0.018f - graphiteShade * 0.96f + sheen * 0.10f);
    const float g = clamp01(paperValue + 0.008f - graphiteShade * 1.00f + sheen * 0.08f);
    const float b = clamp01(paperValue - 0.018f - graphiteShade * 1.03f + sheen * 0.06f);
    return make_uchar4(
        static_cast<unsigned char>(r * 255.0f),
        static_cast<unsigned char>(g * 255.0f),
        static_cast<unsigned char>(b * 255.0f),
        255);
}

__global__ void renderKernel(const float* paperHeight, const float* paperRoughness, const float* paperBinding, const float* looseGraphite, const float* boundGraphite, const float* compaction, const float* damage, const unsigned char* allocatedTiles, const std::uint32_t* tilePageTable, cudaSurfaceObject_t display, int width, int height, std::uint32_t tileSize, std::uint32_t tileColumns, DebugView view, PaperPreset preset)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int count = width * height;
    if (i >= count) return;
    const int x = i % width;
    const int y = i / width;
    const std::uint32_t tileIndex = static_cast<std::uint32_t>(y / tileSize) * tileColumns + static_cast<std::uint32_t>(x / tileSize);
    const bool allocated = allocatedTiles && allocatedTiles[tileIndex];
    const int materialIndex = allocated ? static_cast<int>(materialIndexFor(x, y, tilePageTable, tileSize, tileColumns)) : 0;
    const PaperDefaults defaults = paperDefaultsAt(x, y, preset);
    const float tooth = allocated ? paperHeight[materialIndex] : defaults.height;
    const float roughness = allocated ? paperRoughness[materialIndex] : defaults.roughness;
    const float binding = allocated ? paperBinding[materialIndex] : defaults.binding;
    const float loose = allocated ? looseGraphite[materialIndex] : 0.0f;
    const float bound = allocated ? boundGraphite[materialIndex] : 0.0f;
    const float compacted = allocated ? compaction[materialIndex] : 0.0f;
    const float damaged = allocated ? damage[materialIndex] : 0.0f;
    uchar4 pixel{};
    if (view == DebugView::LooseGraphite)
    {
        pixel = grayscale(loose);
    }
    else if (view == DebugView::BoundGraphite)
    {
        pixel = grayscale(bound);
    }
    else if (view == DebugView::PaperHeight)
    {
        pixel = grayscale(tooth);
    }
    else if (view == DebugView::Compaction)
    {
        pixel = grayscale(compacted);
    }
    else if (view == DebugView::Damage)
    {
        pixel = grayscale(damaged);
    }
    else if (view == DebugView::PaperBinding)
    {
        pixel = grayscale(binding);
    }
    else if (view == DebugView::PaperRoughness)
    {
        pixel = grayscale(roughness);
    }
    else if (view == DebugView::SurfaceSheen)
    {
        pixel = grayscale(allocated ? surfaceSheenAt(paperHeight, looseGraphite, boundGraphite, compaction, damage, allocatedTiles, tilePageTable, width, height, tileSize, tileColumns, preset, x, y) : 0.0f);
    }
    else
    {
        const float sheen = allocated ? surfaceSheenAt(paperHeight, looseGraphite, boundGraphite, compaction, damage, allocatedTiles, tilePageTable, width, height, tileSize, tileColumns, preset, x, y) : 0.0f;
        pixel = displayTonePixel(x, y, tooth, roughness, loose, bound, compacted, damaged, sheen);
    }
    surf2Dwrite(pixel, display, x * static_cast<int>(sizeof(uchar4)), y);
}

__global__ void renderTouchedTileListKernel(
    const float* paperHeight,
    const float* paperRoughness,
    const float* paperBinding,
    const float* looseGraphite,
    const float* boundGraphite,
    const float* compaction,
    const float* damage,
    const unsigned char* allocatedTiles,
    const std::uint32_t* tilePageTable,
    const std::uint32_t* touchedTileIndices,
    std::uint32_t touchedTileCount,
    cudaSurfaceObject_t display,
    int width,
    int height,
    std::uint32_t tileSize,
    std::uint32_t tileColumns,
    DebugView view,
    PaperPreset preset)
{
    const std::uint32_t n = blockIdx.x * blockDim.x + threadIdx.x;
    const std::uint32_t pixelsPerTile = tileSize * tileSize;
    if (n >= touchedTileCount * pixelsPerTile) return;

    const std::uint32_t inTile = n % pixelsPerTile;
    const std::uint32_t tileIndex = touchedTileIndices[n / pixelsPerTile];
    const std::uint32_t tileX = tileIndex % tileColumns;
    const std::uint32_t tileY = tileIndex / tileColumns;

    const int x = static_cast<int>(tileX * tileSize + inTile % tileSize);
    const int y = static_cast<int>(tileY * tileSize + inTile / tileSize);
    if (x >= width || y >= height) return;

    const bool allocated = allocatedTiles && allocatedTiles[tileIndex];
    const int i = allocated ? static_cast<int>(materialIndexFor(x, y, tilePageTable, tileSize, tileColumns)) : 0;
    const PaperDefaults defaults = paperDefaultsAt(x, y, preset);
    const float tooth = allocated ? paperHeight[i] : defaults.height;
    const float roughness = allocated ? paperRoughness[i] : defaults.roughness;
    const float binding = allocated ? paperBinding[i] : defaults.binding;
    const float loose = allocated ? looseGraphite[i] : 0.0f;
    const float bound = allocated ? boundGraphite[i] : 0.0f;
    const float compacted = allocated ? compaction[i] : 0.0f;
    const float damaged = allocated ? damage[i] : 0.0f;
    uchar4 pixel{};
    if (view == DebugView::LooseGraphite)
    {
        pixel = grayscale(loose);
    }
    else if (view == DebugView::BoundGraphite)
    {
        pixel = grayscale(bound);
    }
    else if (view == DebugView::PaperHeight)
    {
        pixel = grayscale(tooth);
    }
    else if (view == DebugView::Compaction)
    {
        pixel = grayscale(compacted);
    }
    else if (view == DebugView::Damage)
    {
        pixel = grayscale(damaged);
    }
    else if (view == DebugView::PaperBinding)
    {
        pixel = grayscale(binding);
    }
    else if (view == DebugView::PaperRoughness)
    {
        pixel = grayscale(roughness);
    }
    else if (view == DebugView::SurfaceSheen)
    {
        pixel = grayscale(allocated ? surfaceSheenAt(paperHeight, looseGraphite, boundGraphite, compaction, damage, allocatedTiles, tilePageTable, width, height, tileSize, tileColumns, preset, x, y) : 0.0f);
    }
    else
    {
        const float sheen = allocated ? surfaceSheenAt(paperHeight, looseGraphite, boundGraphite, compaction, damage, allocatedTiles, tilePageTable, width, height, tileSize, tileColumns, preset, x, y) : 0.0f;
        pixel = displayTonePixel(x, y, tooth, roughness, loose, bound, compacted, damaged, sheen);
    }
    surf2Dwrite(pixel, display, x * static_cast<int>(sizeof(uchar4)), y);
}

__global__ void materialStatsReduceKernel(
    const float* paperHeight,
    const float* paperRoughness,
    const float* paperBinding,
    const float* looseGraphite,
    const float* boundGraphite,
    const float* compaction,
    const float* damage,
    const unsigned char* allocatedTiles,
    const std::uint32_t* tilePageTable,
    float* damageSum,
    float* bindingSum,
    float* roughnessSum,
    float* sheenSum,
    float* looseGraphiteSum,
    float* boundGraphiteSum,
    int width,
    int height,
    std::uint32_t tileSize,
    std::uint32_t tileColumns,
    PaperPreset preset)
{
    __shared__ float damageShared[256];
    __shared__ float bindingShared[256];
    __shared__ float roughnessShared[256];
    __shared__ float sheenShared[256];
    __shared__ float looseShared[256];
    __shared__ float boundShared[256];
    const int tid = threadIdx.x;
    const int i = blockIdx.x * blockDim.x + tid;
    const int count = width * height;
    if (i < count)
    {
        const int x = i % width;
        const int y = i / width;
        const std::uint32_t tileIndex = static_cast<std::uint32_t>(y / tileSize) * tileColumns + static_cast<std::uint32_t>(x / tileSize);
        const bool allocated = allocatedTiles && allocatedTiles[tileIndex];
        const int materialIndex = allocated ? static_cast<int>(materialIndexFor(x, y, tilePageTable, tileSize, tileColumns)) : 0;
        const PaperDefaults defaults = paperDefaultsAt(x, y, preset);
        damageShared[tid] = allocated ? damage[materialIndex] : 0.0f;
        bindingShared[tid] = allocated ? paperBinding[materialIndex] : defaults.binding;
        roughnessShared[tid] = allocated ? paperRoughness[materialIndex] : defaults.roughness;
        sheenShared[tid] = allocated ? surfaceSheenAt(paperHeight, looseGraphite, boundGraphite, compaction, damage, allocatedTiles, tilePageTable, width, height, tileSize, tileColumns, preset, x, y) : 0.0f;
        looseShared[tid] = allocated ? looseGraphite[materialIndex] : 0.0f;
        boundShared[tid] = allocated ? boundGraphite[materialIndex] : 0.0f;
    }
    else
    {
        damageShared[tid] = 0.0f;
        bindingShared[tid] = 0.0f;
        roughnessShared[tid] = 0.0f;
        sheenShared[tid] = 0.0f;
        looseShared[tid] = 0.0f;
        boundShared[tid] = 0.0f;
    }
    __syncthreads();
    for (int stride = blockDim.x / 2; stride > 0; stride >>= 1)
    {
        if (tid < stride)
        {
            damageShared[tid] += damageShared[tid + stride];
            bindingShared[tid] += bindingShared[tid + stride];
            roughnessShared[tid] += roughnessShared[tid + stride];
            sheenShared[tid] += sheenShared[tid + stride];
            looseShared[tid] += looseShared[tid + stride];
            boundShared[tid] += boundShared[tid + stride];
        }
        __syncthreads();
    }
    if (tid == 0)
    {
        atomicAdd(damageSum, damageShared[0]);
        atomicAdd(bindingSum, bindingShared[0]);
        atomicAdd(roughnessSum, roughnessShared[0]);
        atomicAdd(sheenSum, sheenShared[0]);
        atomicAdd(looseGraphiteSum, looseShared[0]);
        atomicAdd(boundGraphiteSum, boundShared[0]);
    }
}

bool check(cudaError_t error, const char* op)
{
    if (error == cudaSuccess) return true;
    std::fprintf(stderr, "CUDA %s failed: %s\n", op, cudaGetErrorString(error));
    return false;
}
}

CudaGraphiteSnapshot::CudaGraphiteSnapshot(std::uint32_t snapshotWidth, std::uint32_t snapshotHeight, std::uint32_t snapshotPageCapacity)
    : width(snapshotWidth), height(snapshotHeight), pageCapacity(snapshotPageCapacity)
{
    constexpr std::uint32_t tileSize = 128;
    const std::uint32_t tileColumns = (width + tileSize - 1) / tileSize;
    const std::uint32_t tileRows = (height + tileSize - 1) / tileSize;
    const std::size_t cells = static_cast<std::size_t>(pageCapacity) * tileSize * tileSize;
    const std::size_t bytes = cells * sizeof(float);
    cudaMalloc(&paperHeight, bytes);
    cudaMalloc(&paperRoughness, bytes);
    cudaMalloc(&paperBinding, bytes);
    cudaMalloc(&looseGraphite, bytes);
    cudaMalloc(&boundGraphite, bytes);
    cudaMalloc(&compaction, bytes);
    cudaMalloc(&damage, bytes);
    const std::size_t tileCount = static_cast<std::size_t>(tileColumns) * tileRows;
    cudaMalloc(&tileAllocated, tileCount);
    cudaMalloc(&tilePageTable, tileCount * sizeof(std::uint32_t));
}

CudaGraphiteSnapshot::CudaGraphiteSnapshot(CudaGraphiteSnapshot&& other) noexcept
{
    *this = std::move(other);
}

CudaGraphiteSnapshot& CudaGraphiteSnapshot::operator=(CudaGraphiteSnapshot&& other) noexcept
{
    if (this == &other) return *this;
    this->~CudaGraphiteSnapshot();
    width = other.width;
    height = other.height;
    paperHeight = other.paperHeight;
    paperRoughness = other.paperRoughness;
    paperBinding = other.paperBinding;
    looseGraphite = other.looseGraphite;
    boundGraphite = other.boundGraphite;
    compaction = other.compaction;
    damage = other.damage;
    tileAllocated = other.tileAllocated;
    tilePageTable = other.tilePageTable;
    pageCapacity = other.pageCapacity;
    allocatedPageCount = other.allocatedPageCount;
    other.width = 0;
    other.height = 0;
    other.paperHeight = nullptr;
    other.paperRoughness = nullptr;
    other.paperBinding = nullptr;
    other.looseGraphite = nullptr;
    other.boundGraphite = nullptr;
    other.compaction = nullptr;
    other.damage = nullptr;
    other.tileAllocated = nullptr;
    other.tilePageTable = nullptr;
    other.pageCapacity = 0;
    other.allocatedPageCount = 0;
    return *this;
}

CudaGraphiteSnapshot::~CudaGraphiteSnapshot()
{
    if (paperHeight) cudaFree(paperHeight);
    if (paperRoughness) cudaFree(paperRoughness);
    if (paperBinding) cudaFree(paperBinding);
    if (looseGraphite) cudaFree(looseGraphite);
    if (boundGraphite) cudaFree(boundGraphite);
    if (compaction) cudaFree(compaction);
    if (damage) cudaFree(damage);
    if (tileAllocated) cudaFree(tileAllocated);
    if (tilePageTable) cudaFree(tilePageTable);
}

CudaGraphiteBackend::~CudaGraphiteBackend()
{
    shutdown();
}

void CudaGraphiteBackend::releaseMaterialBuffers()
{
    if (paperHeight_) cudaFree(paperHeight_);
    if (paperRoughness_) cudaFree(paperRoughness_);
    if (paperBinding_) cudaFree(paperBinding_);
    if (looseGraphite_) cudaFree(looseGraphite_);
    if (boundGraphite_) cudaFree(boundGraphite_);
    if (compaction_) cudaFree(compaction_);
    if (damage_) cudaFree(damage_);
    paperHeight_ = nullptr;
    paperRoughness_ = nullptr;
    paperBinding_ = nullptr;
    looseGraphite_ = nullptr;
    boundGraphite_ = nullptr;
    compaction_ = nullptr;
    damage_ = nullptr;
    materialStorageCells_ = 0;
    materialPageCapacity_ = 0;
}

bool CudaGraphiteBackend::ensureMaterialPageCapacity(std::uint32_t requiredPages)
{
    if (requiredPages <= materialPageCapacity_) return true;
    if (requiredPages > maxMaterialPages_) return false;
    std::uint32_t newCapacity = std::max<std::uint32_t>(4, materialPageCapacity_ == 0 ? 4 : materialPageCapacity_ * 2);
    while (newCapacity < requiredPages) newCapacity *= 2;
    newCapacity = std::min(newCapacity, maxMaterialPages_);

    const std::size_t oldBytes = static_cast<std::size_t>(materialPageCapacity_) * tileSize_ * tileSize_ * sizeof(float);
    const std::size_t newCells = static_cast<std::size_t>(newCapacity) * tileSize_ * tileSize_;
    const std::size_t newBytes = newCells * sizeof(float);

    float* newPaperHeight = nullptr;
    float* newPaperRoughness = nullptr;
    float* newPaperBinding = nullptr;
    float* newLooseGraphite = nullptr;
    float* newBoundGraphite = nullptr;
    float* newCompaction = nullptr;
    float* newDamage = nullptr;
    if (!check(cudaMalloc(&newPaperHeight, newBytes), "grow paperHeight") ||
        !check(cudaMalloc(&newPaperRoughness, newBytes), "grow paperRoughness") ||
        !check(cudaMalloc(&newPaperBinding, newBytes), "grow paperBinding") ||
        !check(cudaMalloc(&newLooseGraphite, newBytes), "grow looseGraphite") ||
        !check(cudaMalloc(&newBoundGraphite, newBytes), "grow boundGraphite") ||
        !check(cudaMalloc(&newCompaction, newBytes), "grow compaction") ||
        !check(cudaMalloc(&newDamage, newBytes), "grow damage"))
    {
        if (newPaperHeight) cudaFree(newPaperHeight);
        if (newPaperRoughness) cudaFree(newPaperRoughness);
        if (newPaperBinding) cudaFree(newPaperBinding);
        if (newLooseGraphite) cudaFree(newLooseGraphite);
        if (newBoundGraphite) cudaFree(newBoundGraphite);
        if (newCompaction) cudaFree(newCompaction);
        if (newDamage) cudaFree(newDamage);
        return false;
    }

    if (oldBytes > 0)
    {
        cudaMemcpy(newPaperHeight, paperHeight_, oldBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newPaperRoughness, paperRoughness_, oldBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newPaperBinding, paperBinding_, oldBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newLooseGraphite, looseGraphite_, oldBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newBoundGraphite, boundGraphite_, oldBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newCompaction, compaction_, oldBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newDamage, damage_, oldBytes, cudaMemcpyDeviceToDevice);
    }

    releaseMaterialBuffers();
    paperHeight_ = newPaperHeight;
    paperRoughness_ = newPaperRoughness;
    paperBinding_ = newPaperBinding;
    looseGraphite_ = newLooseGraphite;
    boundGraphite_ = newBoundGraphite;
    compaction_ = newCompaction;
    damage_ = newDamage;
    materialPageCapacity_ = newCapacity;
    materialStorageCells_ = newCells;
    return true;
}

bool CudaGraphiteBackend::compactMaterialPagePool()
{
    const std::uint32_t targetCapacity = std::max<std::uint32_t>(4, allocatedPageCount_);
    if (targetCapacity == materialPageCapacity_) return true;

    const std::size_t copyBytes = static_cast<std::size_t>(allocatedPageCount_) * tileSize_ * tileSize_ * sizeof(float);
    const std::size_t newCells = static_cast<std::size_t>(targetCapacity) * tileSize_ * tileSize_;
    const std::size_t newBytes = newCells * sizeof(float);

    float* newPaperHeight = nullptr;
    float* newPaperRoughness = nullptr;
    float* newPaperBinding = nullptr;
    float* newLooseGraphite = nullptr;
    float* newBoundGraphite = nullptr;
    float* newCompaction = nullptr;
    float* newDamage = nullptr;
    if (!check(cudaMalloc(&newPaperHeight, newBytes), "compact paperHeight") ||
        !check(cudaMalloc(&newPaperRoughness, newBytes), "compact paperRoughness") ||
        !check(cudaMalloc(&newPaperBinding, newBytes), "compact paperBinding") ||
        !check(cudaMalloc(&newLooseGraphite, newBytes), "compact looseGraphite") ||
        !check(cudaMalloc(&newBoundGraphite, newBytes), "compact boundGraphite") ||
        !check(cudaMalloc(&newCompaction, newBytes), "compact compaction") ||
        !check(cudaMalloc(&newDamage, newBytes), "compact damage"))
    {
        if (newPaperHeight) cudaFree(newPaperHeight);
        if (newPaperRoughness) cudaFree(newPaperRoughness);
        if (newPaperBinding) cudaFree(newPaperBinding);
        if (newLooseGraphite) cudaFree(newLooseGraphite);
        if (newBoundGraphite) cudaFree(newBoundGraphite);
        if (newCompaction) cudaFree(newCompaction);
        if (newDamage) cudaFree(newDamage);
        return false;
    }

    if (copyBytes > 0)
    {
        cudaMemcpy(newPaperHeight, paperHeight_, copyBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newPaperRoughness, paperRoughness_, copyBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newPaperBinding, paperBinding_, copyBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newLooseGraphite, looseGraphite_, copyBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newBoundGraphite, boundGraphite_, copyBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newCompaction, compaction_, copyBytes, cudaMemcpyDeviceToDevice);
        cudaMemcpy(newDamage, damage_, copyBytes, cudaMemcpyDeviceToDevice);
    }

    releaseMaterialBuffers();
    paperHeight_ = newPaperHeight;
    paperRoughness_ = newPaperRoughness;
    paperBinding_ = newPaperBinding;
    looseGraphite_ = newLooseGraphite;
    boundGraphite_ = newBoundGraphite;
    compaction_ = newCompaction;
    damage_ = newDamage;
    materialPageCapacity_ = targetCapacity;
    materialStorageCells_ = newCells;
    return true;
}

bool CudaGraphiteBackend::initialize(const GraphiteBackendInit& init)
{
    width_ = init.width;
    height_ = init.height;
    tileColumns_ = (width_ + tileSize_ - 1) / tileSize_;
    tileRows_ = (height_ + tileSize_ - 1) / tileSize_;
    maxMaterialPages_ = tileColumns_ * tileRows_;
    tileActive_.assign(static_cast<std::size_t>(tileColumns_) * tileRows_, 0);
    tileTouched_.assign(static_cast<std::size_t>(tileColumns_) * tileRows_, 0);
    tileAllocated_.assign(static_cast<std::size_t>(tileColumns_) * tileRows_, 0);
    tilePageTable_.assign(static_cast<std::size_t>(tileColumns_) * tileRows_, kUnallocatedTilePage);
    tileReplayFilter_.assign(static_cast<std::size_t>(tileColumns_) * tileRows_, 0);
    tileReplayFilterEnabled_ = false;
    touchedTileIndices_.reserve(static_cast<std::size_t>(tileColumns_) * tileRows_);
    newlyAllocatedTileIndices_.reserve(static_cast<std::size_t>(tileColumns_) * tileRows_);

    if (!check(cudaSetDevice(0), "set device")) return false;
    if (!ensureMaterialPageCapacity(4)) return false;
    if (!check(cudaMalloc(&toolLoads_, sizeof(float) * 4), "allocate tool loads")) return false;
    if (!check(cudaMalloc(&tileActiveDevice_, tileActive_.size()), "allocate active tile mask")) return false;
    if (!check(cudaMalloc(&tileTouchedDevice_, tileTouched_.size()), "allocate touched tile mask")) return false;
    if (!check(cudaMalloc(&tileAllocatedDevice_, tileAllocated_.size()), "allocate allocated tile mask")) return false;
    if (!check(cudaMalloc(&touchedTileIndicesDevice_, tileActive_.size() * sizeof(std::uint32_t)), "allocate touched tile index list")) return false;
    if (!check(cudaMalloc(&newlyAllocatedTileIndicesDevice_, tileActive_.size() * sizeof(std::uint32_t)), "allocate new tile index list")) return false;
    if (!check(cudaMalloc(&tilePageTableDevice_, tilePageTable_.size() * sizeof(std::uint32_t)), "allocate tile page table")) return false;
    if (!check(cudaMalloc(&damageSum_, sizeof(float)), "allocate damage sum")) return false;
    if (!check(cudaMalloc(&bindingSum_, sizeof(float)), "allocate binding sum")) return false;
    if (!check(cudaMalloc(&sheenSum_, sizeof(float)), "allocate sheen sum")) return false;
    if (!check(cudaMalloc(&roughnessSum_, sizeof(float)), "allocate roughness sum")) return false;
    if (!check(cudaMalloc(&looseGraphiteSum_, sizeof(float)), "allocate loose graphite sum")) return false;
    if (!check(cudaMalloc(&boundGraphiteSum_, sizeof(float)), "allocate bound graphite sum")) return false;

    // Headless mode (tests/probes): no D3D12 display handle means material
    // simulation only, no display surface. Display writes are skipped.
    if (init.d3d12SharedDisplayHandle)
    {
        cudaExternalMemoryHandleDesc externalDesc{};
        externalDesc.type = cudaExternalMemoryHandleTypeD3D12Resource;
        externalDesc.handle.win32.handle = init.d3d12SharedDisplayHandle;
        externalDesc.size = init.d3d12SharedDisplayBytes;
        externalDesc.flags = cudaExternalMemoryDedicated;
        if (!check(cudaImportExternalMemory(reinterpret_cast<cudaExternalMemory_t*>(&displayExternal_), &externalDesc), "import D3D12 display texture")) return false;

        cudaExternalMemoryMipmappedArrayDesc mipDesc{};
        mipDesc.offset = 0;
        mipDesc.formatDesc = cudaCreateChannelDesc<uchar4>();
        mipDesc.extent = make_cudaExtent(width_, height_, 0);
        mipDesc.flags = cudaArraySurfaceLoadStore;
        mipDesc.numLevels = 1;
        if (!check(cudaExternalMemoryGetMappedMipmappedArray(reinterpret_cast<cudaMipmappedArray_t*>(&displayMipmappedArray_), reinterpret_cast<cudaExternalMemory_t>(displayExternal_), &mipDesc), "map D3D12 display texture")) return false;

        cudaArray_t levelArray{};
        if (!check(cudaGetMipmappedArrayLevel(&levelArray, reinterpret_cast<cudaMipmappedArray_t>(displayMipmappedArray_), 0), "get display texture mip level")) return false;

        cudaResourceDesc surfaceDesc{};
        surfaceDesc.resType = cudaResourceTypeArray;
        surfaceDesc.res.array.array = levelArray;
        if (!check(cudaCreateSurfaceObject(reinterpret_cast<cudaSurfaceObject_t*>(&displaySurface_), &surfaceDesc), "create display surface")) return false;
    }

    if (init.d3d12CudaFenceHandle)
    {
        cudaExternalSemaphoreHandleDesc semaphoreDesc{};
        semaphoreDesc.type = cudaExternalSemaphoreHandleTypeD3D12Fence;
        semaphoreDesc.handle.win32.handle = init.d3d12CudaFenceHandle;
        cudaExternalSemaphore_t semaphore{};
        if (!check(cudaImportExternalSemaphore(&semaphore, &semaphoreDesc), "import D3D12 fence")) return false;
        d3d12FenceExternal_ = reinterpret_cast<void*>(semaphore);
    }

    clear();
    return true;
}

void CudaGraphiteBackend::shutdown()
{
    releaseMaterialBuffers();
    if (toolLoads_) cudaFree(toolLoads_);
    if (tileActiveDevice_) cudaFree(tileActiveDevice_);
    if (tileTouchedDevice_) cudaFree(tileTouchedDevice_);
    if (tileAllocatedDevice_) cudaFree(tileAllocatedDevice_);
    if (touchedTileIndicesDevice_) cudaFree(touchedTileIndicesDevice_);
    if (newlyAllocatedTileIndicesDevice_) cudaFree(newlyAllocatedTileIndicesDevice_);
    if (tilePageTableDevice_) cudaFree(tilePageTableDevice_);
    if (damageSum_) cudaFree(damageSum_);
    if (bindingSum_) cudaFree(bindingSum_);
    if (sheenSum_) cudaFree(sheenSum_);
    if (roughnessSum_) cudaFree(roughnessSum_);
    if (looseGraphiteSum_) cudaFree(looseGraphiteSum_);
    if (boundGraphiteSum_) cudaFree(boundGraphiteSum_);
    if (displaySurface_) cudaDestroySurfaceObject(static_cast<cudaSurfaceObject_t>(displaySurface_));
    if (displayMipmappedArray_) cudaFreeMipmappedArray(reinterpret_cast<cudaMipmappedArray_t>(displayMipmappedArray_));
    if (displayExternal_) cudaDestroyExternalMemory(reinterpret_cast<cudaExternalMemory_t>(displayExternal_));
    if (d3d12FenceExternal_) cudaDestroyExternalSemaphore(reinterpret_cast<cudaExternalSemaphore_t>(d3d12FenceExternal_));
    toolLoads_ = nullptr;
    tileActiveDevice_ = nullptr;
    tileTouchedDevice_ = nullptr;
    tileAllocatedDevice_ = nullptr;
    touchedTileIndicesDevice_ = nullptr;
    newlyAllocatedTileIndicesDevice_ = nullptr;
    tilePageTableDevice_ = nullptr;
    damageSum_ = nullptr;
    bindingSum_ = nullptr;
    sheenSum_ = nullptr;
    roughnessSum_ = nullptr;
    looseGraphiteSum_ = nullptr;
    boundGraphiteSum_ = nullptr;
    displaySurface_ = 0;
    displayMipmappedArray_ = nullptr;
    displayExternal_ = nullptr;
    d3d12FenceExternal_ = nullptr;
}

void CudaGraphiteBackend::clear()
{
    std::fill(tileActive_.begin(), tileActive_.end(), 0);
    std::fill(tileTouched_.begin(), tileTouched_.end(), 0);
    std::fill(tileAllocated_.begin(), tileAllocated_.end(), 0);
    std::fill(tilePageTable_.begin(), tilePageTable_.end(), kUnallocatedTilePage);
    touchedTileIndices_.clear();
    newlyAllocatedTileIndices_.clear();
    activeTiles_ = 0;
    allocatedTiles_ = 0;
    allocatedPageCount_ = 0;
    lastTouchedTiles_ = 0;
    if (tileActiveDevice_) cudaMemset(tileActiveDevice_, 0, tileActive_.size());
    if (tileTouchedDevice_) cudaMemset(tileTouchedDevice_, 0, tileTouched_.size());
    if (tileAllocatedDevice_) cudaMemset(tileAllocatedDevice_, 0, tileAllocated_.size());
    if (tilePageTableDevice_) cudaMemcpy(tilePageTableDevice_, tilePageTable_.data(), tilePageTable_.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice);
    compactMaterialPagePool();
    clearToolLoadsKernel<<<1, 32>>>(toolLoads_);
    forceFullRender_ = true;
    cudaDeviceSynchronize();
    endFrame();
}

void CudaGraphiteBackend::clearTiles(const std::vector<std::uint32_t>& tileIndices)
{
    touchedTileIndices_.clear();
    for (std::uint32_t tileIndex : tileIndices)
    {
        if (tileIndex >= tileAllocated_.size()) continue;
        tileActive_[tileIndex] = 0;
        tileTouched_[tileIndex] = 1;
        tileAllocated_[tileIndex] = 0;
        tilePageTable_[tileIndex] = kUnallocatedTilePage;
        touchedTileIndices_.push_back(tileIndex);
    }
    if (touchedTileIndices_.empty()) return;

    activeTiles_ = 0;
    allocatedTiles_ = 0;
    lastTouchedTiles_ = static_cast<std::uint32_t>(touchedTileIndices_.size());
    for (std::uint8_t active : tileActive_)
    {
        if (active) activeTiles_++;
    }
    for (std::uint8_t allocated : tileAllocated_)
    {
        if (allocated) allocatedTiles_++;
    }
    cudaMemcpy(tileActiveDevice_, tileActive_.data(), tileActive_.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(tileTouchedDevice_, tileTouched_.data(), tileTouched_.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(tileAllocatedDevice_, tileAllocated_.data(), tileAllocated_.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(tilePageTableDevice_, tilePageTable_.data(), tilePageTable_.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice);
    cudaMemcpy(touchedTileIndicesDevice_, touchedTileIndices_.data(), touchedTileIndices_.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice);
    forceFullRender_ = false;
    endFrame();
}

void CudaGraphiteBackend::compactMaterialPages()
{
    if (compactMaterialPagePool())
    {
        forceFullRender_ = true;
        endFrame();
    }
}

void CudaGraphiteBackend::beginFrame()
{
}

void CudaGraphiteBackend::setTileReplayFilter(const std::vector<std::uint32_t>& tileIndices)
{
    std::fill(tileReplayFilter_.begin(), tileReplayFilter_.end(), 0);
    tileReplayFilterEnabled_ = !tileIndices.empty();
    for (std::uint32_t tileIndex : tileIndices)
    {
        if (tileIndex < tileReplayFilter_.size()) tileReplayFilter_[tileIndex] = 1;
    }
}

void CudaGraphiteBackend::clearTileReplayFilter()
{
    std::fill(tileReplayFilter_.begin(), tileReplayFilter_.end(), 0);
    tileReplayFilterEnabled_ = false;
}

void CudaGraphiteBackend::submitStrokeSegment(const StrokePacket& from, const StrokePacket& to, const ToolParams& params)
{
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float radius = std::max(2.0f, effectiveToolRadiusPx(params) * 1.8f);
    const int minX = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x) - radius)));
    const int maxX = std::min(static_cast<int>(width_) - 1, static_cast<int>(std::ceil(std::max(from.x, to.x) + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min(from.y, to.y) - radius)));
    const int maxY = std::min(static_cast<int>(height_) - 1, static_cast<int>(std::ceil(std::max(from.y, to.y) + radius)));
    const std::uint32_t minTileX = static_cast<std::uint32_t>(minX) / tileSize_;
    const std::uint32_t maxTileX = static_cast<std::uint32_t>(maxX) / tileSize_;
    const std::uint32_t minTileY = static_cast<std::uint32_t>(minY) / tileSize_;
    const std::uint32_t maxTileY = static_cast<std::uint32_t>(maxY) / tileSize_;
    if (tileTouchedDevice_) cudaMemset(tileTouchedDevice_, 0, tileTouched_.size());
    const std::uint32_t tileRegionCount = (maxTileX - minTileX + 1) * (maxTileY - minTileY + 1);
    markTouchedTilesKernel<<<(tileRegionCount + kThreads - 1) / kThreads, kThreads>>>(
        tileActiveDevice_,
        tileTouchedDevice_,
        tileColumns_,
        tileRows_,
        minTileX,
        maxTileX,
        minTileY,
        maxTileY);
    check(cudaGetLastError(), "mark touched tiles launch");
    cudaMemcpy(tileActive_.data(), tileActiveDevice_, tileActive_.size(), cudaMemcpyDeviceToHost);
    cudaMemcpy(tileTouched_.data(), tileTouchedDevice_, tileTouched_.size(), cudaMemcpyDeviceToHost);
    activeTiles_ = 0;
    allocatedTiles_ = 0;
    lastTouchedTiles_ = 0;
    for (std::uint8_t active : tileActive_)
    {
        if (active) activeTiles_++;
    }
    for (std::uint8_t touched : tileTouched_)
    {
        if (touched) lastTouchedTiles_++;
    }
    touchedTileIndices_.clear();
    for (std::uint32_t tileIndex = 0; tileIndex < tileTouched_.size(); ++tileIndex)
    {
        if (!tileTouched_[tileIndex]) continue;
        if (tileReplayFilterEnabled_ && !tileReplayFilter_[tileIndex])
        {
            tileTouched_[tileIndex] = 0;
            tileActive_[tileIndex] = tileAllocated_[tileIndex];
            continue;
        }
        touchedTileIndices_.push_back(tileIndex);
    }
    lastTouchedTiles_ = static_cast<std::uint32_t>(touchedTileIndices_.size());
    if (tileReplayFilterEnabled_)
    {
        activeTiles_ = 0;
        for (std::uint8_t active : tileActive_)
        {
            if (active) activeTiles_++;
        }
        cudaMemcpy(tileActiveDevice_, tileActive_.data(), tileActive_.size(), cudaMemcpyHostToDevice);
        cudaMemcpy(tileTouchedDevice_, tileTouched_.data(), tileTouched_.size(), cudaMemcpyHostToDevice);
    }
    if (!touchedTileIndices_.empty())
    {
        newlyAllocatedTileIndices_.clear();
        for (std::uint32_t tileIndex : touchedTileIndices_)
        {
            if (tilePageTable_[tileIndex] == kUnallocatedTilePage)
            {
                tilePageTable_[tileIndex] = allocatedPageCount_++;
                tileAllocated_[tileIndex] = 1;
                newlyAllocatedTileIndices_.push_back(tileIndex);
            }
        }
        if (!ensureMaterialPageCapacity(allocatedPageCount_)) return;
        cudaMemcpy(tilePageTableDevice_, tilePageTable_.data(), tilePageTable_.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice);
        cudaMemcpy(tileAllocatedDevice_, tileAllocated_.data(), tileAllocated_.size(), cudaMemcpyHostToDevice);
        cudaMemcpy(touchedTileIndicesDevice_, touchedTileIndices_.data(), touchedTileIndices_.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice);
        if (!newlyAllocatedTileIndices_.empty())
        {
            cudaMemcpy(newlyAllocatedTileIndicesDevice_, newlyAllocatedTileIndices_.data(), newlyAllocatedTileIndices_.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice);
            const std::uint32_t materialInitPixels = static_cast<std::uint32_t>(newlyAllocatedTileIndices_.size()) * tileSize_ * tileSize_;
            initializeTouchedMaterialTilesKernel<<<(materialInitPixels + kThreads - 1) / kThreads, kThreads>>>(
                paperHeight_,
                paperRoughness_,
                paperBinding_,
                looseGraphite_,
                boundGraphite_,
                compaction_,
                damage_,
                tilePageTableDevice_,
                newlyAllocatedTileIndicesDevice_,
                static_cast<std::uint32_t>(newlyAllocatedTileIndices_.size()),
                tileSize_,
                tileColumns_,
                width_,
                height_,
                paperPreset_);
            check(cudaGetLastError(), "initialize material tiles launch");
            check(cudaDeviceSynchronize(), "initialize material tiles");
        }
        allocatedTiles_ = allocatedPageCount_;
    }

    cudaEvent_t start{};
    cudaEvent_t stop{};
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
    cudaEventRecord(start);
    const std::uint32_t strokePixelCount = static_cast<std::uint32_t>(touchedTileIndices_.size()) * tileSize_ * tileSize_;
    if (strokePixelCount == 0)
    {
        lastKernelMs_ = 0.0f;
        cudaEventDestroy(start);
        cudaEventDestroy(stop);
        return;
    }
    strokeKernel<<<(strokePixelCount + kThreads - 1) / kThreads, kThreads>>>(paperHeight_, paperRoughness_, paperBinding_, looseGraphite_, boundGraphite_, compaction_, damage_, toolLoads_, tilePageTableDevice_, touchedTileIndicesDevice_, static_cast<std::uint32_t>(touchedTileIndices_.size()), tileSize_, tileColumns_, width_, height_, from, to, params);
    check(cudaGetLastError(), "stroke kernel launch");
    check(cudaEventRecord(stop), "stroke event record");
    check(cudaEventSynchronize(stop), "stroke kernel synchronize");
    check(cudaEventElapsedTime(&lastKernelMs_, start, stop), "stroke timing");
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    packets_++;
    forceFullRender_ = true;
    (void)dx;
    (void)dy;
}

void CudaGraphiteBackend::importSketchMaterial(const ImportedSketchMaterial& material)
{
    if (material.width == 0 || material.height == 0 || material.graphite.empty()) return;
    if (material.graphite.size() != static_cast<std::size_t>(material.width) * material.height) return;
    if (material.targetWidth <= 0.0f || material.targetHeight <= 0.0f) return;

    const int minX = std::max(0, static_cast<int>(std::floor(material.targetX)));
    const int maxX = std::min(static_cast<int>(width_) - 1, static_cast<int>(std::ceil(material.targetX + material.targetWidth)));
    const int minY = std::max(0, static_cast<int>(std::floor(material.targetY)));
    const int maxY = std::min(static_cast<int>(height_) - 1, static_cast<int>(std::ceil(material.targetY + material.targetHeight)));
    if (maxX < minX || maxY < minY) return;

    const std::uint32_t minTileX = static_cast<std::uint32_t>(minX) / tileSize_;
    const std::uint32_t maxTileX = static_cast<std::uint32_t>(maxX) / tileSize_;
    const std::uint32_t minTileY = static_cast<std::uint32_t>(minY) / tileSize_;
    const std::uint32_t maxTileY = static_cast<std::uint32_t>(maxY) / tileSize_;

    std::fill(tileTouched_.begin(), tileTouched_.end(), 0);
    touchedTileIndices_.clear();
    for (std::uint32_t tileY = minTileY; tileY <= maxTileY && tileY < tileRows_; ++tileY)
    {
        for (std::uint32_t tileX = minTileX; tileX <= maxTileX && tileX < tileColumns_; ++tileX)
        {
            const std::uint32_t tileIndex = tileY * tileColumns_ + tileX;
            if (tileReplayFilterEnabled_ && !tileReplayFilter_[tileIndex]) continue;
            tileActive_[tileIndex] = 1;
            tileTouched_[tileIndex] = 1;
            touchedTileIndices_.push_back(tileIndex);
        }
    }
    if (touchedTileIndices_.empty())
    {
        lastTouchedTiles_ = 0;
        lastKernelMs_ = 0.0f;
        return;
    }

    newlyAllocatedTileIndices_.clear();
    for (std::uint32_t tileIndex : touchedTileIndices_)
    {
        if (tilePageTable_[tileIndex] == kUnallocatedTilePage)
        {
            tilePageTable_[tileIndex] = allocatedPageCount_++;
            tileAllocated_[tileIndex] = 1;
            newlyAllocatedTileIndices_.push_back(tileIndex);
        }
    }
    if (!ensureMaterialPageCapacity(allocatedPageCount_)) return;

    activeTiles_ = 0;
    allocatedTiles_ = 0;
    for (std::uint8_t active : tileActive_)
    {
        if (active) activeTiles_++;
    }
    for (std::uint8_t allocated : tileAllocated_)
    {
        if (allocated) allocatedTiles_++;
    }
    lastTouchedTiles_ = static_cast<std::uint32_t>(touchedTileIndices_.size());

    cudaMemcpy(tileActiveDevice_, tileActive_.data(), tileActive_.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(tileTouchedDevice_, tileTouched_.data(), tileTouched_.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(tileAllocatedDevice_, tileAllocated_.data(), tileAllocated_.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(tilePageTableDevice_, tilePageTable_.data(), tilePageTable_.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice);
    cudaMemcpy(touchedTileIndicesDevice_, touchedTileIndices_.data(), touchedTileIndices_.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice);

    if (!newlyAllocatedTileIndices_.empty())
    {
        cudaMemcpy(newlyAllocatedTileIndicesDevice_, newlyAllocatedTileIndices_.data(), newlyAllocatedTileIndices_.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice);
        const std::uint32_t materialInitPixels = static_cast<std::uint32_t>(newlyAllocatedTileIndices_.size()) * tileSize_ * tileSize_;
        initializeTouchedMaterialTilesKernel<<<(materialInitPixels + kThreads - 1) / kThreads, kThreads>>>(
            paperHeight_,
            paperRoughness_,
            paperBinding_,
            looseGraphite_,
            boundGraphite_,
            compaction_,
            damage_,
            tilePageTableDevice_,
            newlyAllocatedTileIndicesDevice_,
            static_cast<std::uint32_t>(newlyAllocatedTileIndices_.size()),
            tileSize_,
            tileColumns_,
            width_,
            height_,
            paperPreset_);
        check(cudaGetLastError(), "initialize import material tiles launch");
        check(cudaDeviceSynchronize(), "initialize import material tiles");
    }

    float* importedGraphiteDevice = nullptr;
    const std::size_t importedBytes = material.graphite.size() * sizeof(float);
    if (!check(cudaMalloc(&importedGraphiteDevice, importedBytes), "allocate imported sketch graphite")) return;
    if (!check(cudaMemcpy(importedGraphiteDevice, material.graphite.data(), importedBytes, cudaMemcpyHostToDevice), "copy imported sketch graphite"))
    {
        cudaFree(importedGraphiteDevice);
        return;
    }

    cudaEvent_t start{};
    cudaEvent_t stop{};
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
    cudaEventRecord(start);
    const std::uint32_t importPixelCount = static_cast<std::uint32_t>(touchedTileIndices_.size()) * tileSize_ * tileSize_;
    importSketchMaterialKernel<<<(importPixelCount + kThreads - 1) / kThreads, kThreads>>>(
        paperHeight_,
        paperRoughness_,
        paperBinding_,
        looseGraphite_,
        boundGraphite_,
        compaction_,
        damage_,
        tilePageTableDevice_,
        touchedTileIndicesDevice_,
        static_cast<std::uint32_t>(touchedTileIndices_.size()),
        tileSize_,
        tileColumns_,
        width_,
        height_,
        importedGraphiteDevice,
        material.width,
        material.height,
        material.targetX,
        material.targetY,
        material.targetWidth,
        material.targetHeight);
    check(cudaGetLastError(), "import sketch material launch");
    check(cudaEventRecord(stop), "import sketch event record");
    check(cudaEventSynchronize(stop), "import sketch synchronize");
    check(cudaEventElapsedTime(&lastKernelMs_, start, stop), "import sketch timing");
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    cudaFree(importedGraphiteDevice);

    forceFullRender_ = true;
    endFrame();
}

void CudaGraphiteBackend::endFrame()
{
    if (!displaySurface_) return; // headless: material state only, no display
    const int count = static_cast<int>(width_ * height_);
    const cudaSurfaceObject_t surface = static_cast<cudaSurfaceObject_t>(displaySurface_);
    if (forceFullRender_ || !tileTouchedDevice_)
    {
        renderKernel<<<(count + kThreads - 1) / kThreads, kThreads>>>(paperHeight_, paperRoughness_, paperBinding_, looseGraphite_, boundGraphite_, compaction_, damage_, tileAllocatedDevice_, tilePageTableDevice_, surface, width_, height_, tileSize_, tileColumns_, debugView_, paperPreset_);
        check(cudaGetLastError(), "full render launch");
        forceFullRender_ = false;
        lastRenderUsedDirtyTiles_ = false;
        lastRenderTiles_ = tileColumns_ * tileRows_;
        lastRenderPixels_ = static_cast<std::uint32_t>(count);
    }
    else
    {
        const std::uint32_t touchedPixelCount = static_cast<std::uint32_t>(touchedTileIndices_.size()) * tileSize_ * tileSize_;
        if (touchedPixelCount > 0)
        {
            renderTouchedTileListKernel<<<(touchedPixelCount + kThreads - 1) / kThreads, kThreads>>>(
                paperHeight_,
                paperRoughness_,
                paperBinding_,
                looseGraphite_,
                boundGraphite_,
                compaction_,
                damage_,
                tileAllocatedDevice_,
                tilePageTableDevice_,
                touchedTileIndicesDevice_,
                static_cast<std::uint32_t>(touchedTileIndices_.size()),
                surface,
                width_,
                height_,
                tileSize_,
                tileColumns_,
                debugView_,
                paperPreset_);
            check(cudaGetLastError(), "dirty tile render launch");
        }
        lastRenderUsedDirtyTiles_ = true;
        lastRenderTiles_ = static_cast<std::uint32_t>(touchedTileIndices_.size());
        lastRenderPixels_ = std::min<std::uint32_t>(static_cast<std::uint32_t>(width_ * height_), touchedPixelCount);
    }
    if (d3d12FenceExternal_)
    {
        cudaExternalSemaphoreSignalParams signalParams{};
        const std::uint64_t fenceValue = signalFenceValue_++;
        signalParams.params.fence.value = fenceValue;
        auto semaphore = reinterpret_cast<cudaExternalSemaphore_t>(d3d12FenceExternal_);
        if (check(cudaSignalExternalSemaphoresAsync(&semaphore, &signalParams, 1, 0), "signal D3D12 fence"))
        {
            lastSignaledFenceValue_ = fenceValue;
        }
    }
    else
    {
        check(cudaDeviceSynchronize(), "render synchronize");
    }
}

void CudaGraphiteBackend::setDebugView(DebugView view)
{
    debugView_ = view;
    forceFullRender_ = true;
    endFrame();
}

void CudaGraphiteBackend::setPaperPreset(PaperPreset preset)
{
    paperPreset_ = preset;
}

void CudaGraphiteBackend::cleanTool(ToolKind tool)
{
    const int index = toolLoadIndexForTool(tool);
    if (index < 0 || !toolLoads_) return;
    clearToolLoadKernel<<<1, 1>>>(toolLoads_, index);
    check(cudaGetLastError(), "clean tool load launch");
    check(cudaDeviceSynchronize(), "clean tool load");
}

std::uint32_t CudaGraphiteBackend::width() const
{
    return width_;
}

std::uint32_t CudaGraphiteBackend::height() const
{
    return height_;
}

BackendStats CudaGraphiteBackend::stats() const
{
    cudaMemcpy(const_cast<float*>(toolLoadsHost_), toolLoads_, sizeof(float) * 4, cudaMemcpyDeviceToHost);
    float zero = 0.0f;
    float damageSum = 0.0f;
    float bindingSum = 0.0f;
    float sheenSum = 0.0f;
    float roughnessSum = 0.0f;
    float looseGraphiteSum = 0.0f;
    float boundGraphiteSum = 0.0f;
    const int count = static_cast<int>(width_ * height_);
    cudaMemcpy(damageSum_, &zero, sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(bindingSum_, &zero, sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(sheenSum_, &zero, sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(roughnessSum_, &zero, sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(looseGraphiteSum_, &zero, sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(boundGraphiteSum_, &zero, sizeof(float), cudaMemcpyHostToDevice);
    materialStatsReduceKernel<<<(count + kThreads - 1) / kThreads, kThreads>>>(
        paperHeight_,
        paperRoughness_,
        paperBinding_,
        looseGraphite_,
        boundGraphite_,
        compaction_,
        damage_,
        tileAllocatedDevice_,
        tilePageTableDevice_,
        damageSum_,
        bindingSum_,
        roughnessSum_,
        sheenSum_,
        looseGraphiteSum_,
        boundGraphiteSum_,
        width_,
        height_,
        tileSize_,
        tileColumns_,
        paperPreset_);
    check(cudaGetLastError(), "material stats launch");
    cudaMemcpy(&damageSum, damageSum_, sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(&bindingSum, bindingSum_, sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(&sheenSum, sheenSum_, sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(&roughnessSum, roughnessSum_, sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(&looseGraphiteSum, looseGraphiteSum_, sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(&boundGraphiteSum, boundGraphiteSum_, sizeof(float), cudaMemcpyDeviceToHost);
    return BackendStats{lastKernelMs_, packets_, tileSize_, tileColumns_, tileRows_, activeTiles_, allocatedTiles_, allocatedPageCount_, materialPageCapacity_, lastTouchedTiles_, lastRenderUsedDirtyTiles_, lastRenderTiles_, lastRenderPixels_, toolLoadsHost_[0], toolLoadsHost_[1], toolLoadsHost_[2], toolLoadsHost_[3], looseGraphiteSum / static_cast<float>(count), boundGraphiteSum / static_cast<float>(count), damageSum / static_cast<float>(count), bindingSum / static_cast<float>(count), sheenSum / static_cast<float>(count), roughnessSum / static_cast<float>(count), lastSignaledFenceValue_, paperPreset_};
}

std::unique_ptr<CudaGraphiteSnapshot> CudaGraphiteBackend::capture() const
{
    auto snapshot = std::make_unique<CudaGraphiteSnapshot>(width_, height_, materialPageCapacity_);
    snapshot->allocatedPageCount = allocatedPageCount_;
    const std::size_t bytes = static_cast<std::size_t>(snapshot->pageCapacity) * tileSize_ * tileSize_ * sizeof(float);
    cudaMemcpy(snapshot->paperHeight, paperHeight_, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(snapshot->paperRoughness, paperRoughness_, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(snapshot->paperBinding, paperBinding_, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(snapshot->looseGraphite, looseGraphite_, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(snapshot->boundGraphite, boundGraphite_, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(snapshot->compaction, compaction_, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(snapshot->damage, damage_, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(snapshot->tileAllocated, tileAllocatedDevice_, tileAllocated_.size(), cudaMemcpyDeviceToDevice);
    cudaMemcpy(snapshot->tilePageTable, tilePageTableDevice_, tilePageTable_.size() * sizeof(std::uint32_t), cudaMemcpyDeviceToDevice);
    return snapshot;
}

void CudaGraphiteBackend::restore(const CudaGraphiteSnapshot& snapshot)
{
    if (snapshot.width != width_ || snapshot.height != height_) return;
    if (!ensureMaterialPageCapacity(snapshot.pageCapacity)) return;
    allocatedPageCount_ = snapshot.allocatedPageCount;
    const std::size_t bytes = static_cast<std::size_t>(snapshot.pageCapacity) * tileSize_ * tileSize_ * sizeof(float);
    cudaMemcpy(paperHeight_, snapshot.paperHeight, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(paperRoughness_, snapshot.paperRoughness, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(paperBinding_, snapshot.paperBinding, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(looseGraphite_, snapshot.looseGraphite, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(boundGraphite_, snapshot.boundGraphite, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(compaction_, snapshot.compaction, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(damage_, snapshot.damage, bytes, cudaMemcpyDeviceToDevice);
    cudaMemcpy(tileAllocatedDevice_, snapshot.tileAllocated, tileAllocated_.size(), cudaMemcpyDeviceToDevice);
    cudaMemcpy(tilePageTableDevice_, snapshot.tilePageTable, tilePageTable_.size() * sizeof(std::uint32_t), cudaMemcpyDeviceToDevice);
    cudaMemcpy(tileAllocated_.data(), tileAllocatedDevice_, tileAllocated_.size(), cudaMemcpyDeviceToHost);
    cudaMemcpy(tilePageTable_.data(), tilePageTableDevice_, tilePageTable_.size() * sizeof(std::uint32_t), cudaMemcpyDeviceToHost);
    allocatedTiles_ = allocatedPageCount_;
    forceFullRender_ = true;
    endFrame();
}
