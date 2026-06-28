#include <cuda_runtime.h>

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace
{
struct MaterialState
{
    float paperHeight;
    float paperRoughness;
    float paperBinding;
    float looseGraphite;
    float boundGraphite;
    float compaction;
    float damage;
    float toolLoad;
};

__device__ float clamp01(float value)
{
    return fminf(1.0f, fmaxf(0.0f, value));
}

__global__ void pencilKernel(MaterialState* state, float softness, float pressure)
{
    MaterialState s = *state;
    const float heightTooth = s.paperHeight * (1.0f - s.compaction * 0.55f);
    const float roughness = clamp01(s.paperRoughness - s.compaction * 0.38f + s.damage * 0.12f);
    const float catchTooth = clamp01(heightTooth * 0.35f + roughness * 0.65f);
    const float binding = clamp01(s.paperBinding - s.damage * 0.38f);
    const float graphiteFill = clamp01(s.boundGraphite * 0.58f + s.looseGraphite * 0.24f);
    const float catchFactor = clamp01(0.32f + catchTooth * 0.62f + binding * 0.16f + graphiteFill * 0.22f - s.compaction * 0.22f + s.damage * 0.08f);
    const float deposit = (0.014f + pressure * pressure * 0.075f) * (0.34f + softness * 0.90f);
    const float loose = deposit * (0.56f + softness * 0.62f) * catchFactor;
    const float bound = deposit * (0.18f + pressure * 0.48f) * (0.40f + catchTooth + binding * 0.44f);
    s.looseGraphite = fminf(1.0f, s.looseGraphite + loose);
    s.boundGraphite = fminf(1.0f, s.boundGraphite + bound);
    s.compaction = clamp01(s.compaction + pressure * (0.0025f + (1.0f - softness) * 0.006f));
    *state = s;
}

__global__ void regularEraserKernel(MaterialState* state, float pressure)
{
    MaterialState s = *state;
    const float binding = clamp01(s.paperBinding - s.damage * 0.38f);
    const float lift = 0.35f + pressure * 0.55f;
    s.looseGraphite = fmaxf(0.0f, s.looseGraphite - lift * 0.95f);
    s.boundGraphite = fmaxf(0.0f, s.boundGraphite - lift * (0.12f + (1.0f - binding) * 0.28f));
    s.compaction = clamp01(s.compaction + pressure * 0.012f);
    s.damage = clamp01(s.damage + pressure * pressure * 0.006f);
    s.paperBinding = clamp01(s.paperBinding - pressure * pressure * 0.0018f);
    *state = s;
}

__global__ void powderKernel(MaterialState* state, float pressure)
{
    MaterialState s = *state;
    const float heightTooth = s.paperHeight * (1.0f - s.compaction * 0.55f);
    const float roughness = clamp01(s.paperRoughness - s.compaction * 0.38f + s.damage * 0.12f);
    const float catchTooth = clamp01(heightTooth * 0.35f + roughness * 0.65f);
    const float binding = clamp01(s.paperBinding - s.damage * 0.38f);
    const float toothCatch = clamp01(0.18f + catchTooth * 0.82f + binding * 0.10f - s.compaction * 0.30f - s.damage * 0.24f);
    const float lift = fminf(s.looseGraphite, s.looseGraphite * (0.003f + pressure * 0.012f) * (1.05f - toothCatch * 0.42f));
    const float requested = toothCatch * fminf(1.0f, fmaxf(0.0f, s.toolLoad)) * (0.0008f + pressure * 0.0030f);
    const float powder = fminf(s.toolLoad, requested);
    s.toolLoad = fmaxf(0.0f, s.toolLoad - powder) + lift;
    s.looseGraphite = fminf(1.0f, s.looseGraphite - lift + powder);
    s.compaction = clamp01(s.compaction + pressure * 0.001f);
    *state = s;
}

__global__ void graphitePowderKernel(MaterialState* state, float pressure)
{
    MaterialState s = *state;
    const float heightTooth = s.paperHeight * (1.0f - s.compaction * 0.55f);
    const float roughness = clamp01(s.paperRoughness - s.compaction * 0.38f + s.damage * 0.12f);
    const float catchTooth = clamp01(heightTooth * 0.35f + roughness * 0.65f);
    const float binding = clamp01(s.paperBinding - s.damage * 0.38f);
    const float toothCatch = clamp01(0.20f + catchTooth * 0.76f + roughness * 0.18f + binding * 0.06f - s.compaction * 0.22f - s.damage * 0.18f);
    const float powder = toothCatch * (0.0018f + pressure * 0.0054f);
    s.looseGraphite = fminf(1.0f, s.looseGraphite + powder);
    s.boundGraphite = fminf(1.0f, s.boundGraphite + powder * (0.035f + pressure * 0.040f + binding * 0.025f));
    s.compaction = clamp01(s.compaction + pressure * 0.00035f);
    *state = s;
}

void check(cudaError_t error, const char* label)
{
    if (error == cudaSuccess) return;
    std::fprintf(stderr, "%s failed: %s\n", label, cudaGetErrorString(error));
    std::abort();
}

MaterialState runPencil(float softness)
{
    MaterialState initial{0.55f, 0.62f, 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    MaterialState* device = nullptr;
    check(cudaMalloc(&device, sizeof(MaterialState)), "cudaMalloc");
    check(cudaMemcpy(device, &initial, sizeof(MaterialState), cudaMemcpyHostToDevice), "cudaMemcpy H2D");
    pencilKernel<<<1, 1>>>(device, softness, 0.7f);
    check(cudaDeviceSynchronize(), "pencil kernel");
    check(cudaMemcpy(&initial, device, sizeof(MaterialState), cudaMemcpyDeviceToHost), "cudaMemcpy D2H");
    cudaFree(device);
    return initial;
}

void pencilCudaDepositsGraphite()
{
    const MaterialState hard = runPencil(0.25f);
    const MaterialState soft = runPencil(1.0f);
    assert(hard.looseGraphite > 0.0f);
    assert(hard.boundGraphite > 0.0f);
    assert(soft.looseGraphite > hard.looseGraphite);
    assert(soft.looseGraphite + soft.boundGraphite > hard.looseGraphite + hard.boundGraphite);
}

void eraserCudaRemovesAndDamages()
{
    MaterialState state{0.55f, 0.62f, 0.72f, 0.7f, 0.4f, 0.0f, 0.0f, 0.0f};
    MaterialState* device = nullptr;
    check(cudaMalloc(&device, sizeof(MaterialState)), "cudaMalloc");
    check(cudaMemcpy(device, &state, sizeof(MaterialState), cudaMemcpyHostToDevice), "cudaMemcpy H2D");
    regularEraserKernel<<<1, 1>>>(device, 0.8f);
    check(cudaDeviceSynchronize(), "eraser kernel");
    check(cudaMemcpy(&state, device, sizeof(MaterialState), cudaMemcpyDeviceToHost), "cudaMemcpy D2H");
    cudaFree(device);
    assert(state.looseGraphite < 0.7f);
    assert(state.boundGraphite < 0.4f);
    assert(state.damage > 0.0f);
}

void cleanPowderCudaDoesNotCreateGraphite()
{
    MaterialState blank{0.55f, 0.62f, 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    MaterialState* device = nullptr;
    check(cudaMalloc(&device, sizeof(MaterialState)), "cudaMalloc");
    check(cudaMemcpy(device, &blank, sizeof(MaterialState), cudaMemcpyHostToDevice), "blank H2D");
    powderKernel<<<1, 1>>>(device, 0.7f);
    check(cudaDeviceSynchronize(), "blank powder");
    check(cudaMemcpy(&blank, device, sizeof(MaterialState), cudaMemcpyDeviceToHost), "blank D2H");
    cudaFree(device);
    assert(blank.looseGraphite == 0.0f);
    assert(blank.boundGraphite == 0.0f);
    assert(blank.toolLoad == 0.0f);
}

void dirtyPowderCudaRespondsToToothWithoutCreatingGraphite()
{
    MaterialState smooth{0.30f, 0.25f, 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.02f};
    MaterialState rough{0.70f, 0.78f, 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.02f};
    MaterialState* device = nullptr;
    check(cudaMalloc(&device, sizeof(MaterialState)), "cudaMalloc");
    check(cudaMemcpy(device, &smooth, sizeof(MaterialState), cudaMemcpyHostToDevice), "smooth H2D");
    powderKernel<<<1, 1>>>(device, 0.7f);
    check(cudaDeviceSynchronize(), "smooth powder");
    check(cudaMemcpy(&smooth, device, sizeof(MaterialState), cudaMemcpyDeviceToHost), "smooth D2H");
    check(cudaMemcpy(device, &rough, sizeof(MaterialState), cudaMemcpyHostToDevice), "rough H2D");
    powderKernel<<<1, 1>>>(device, 0.7f);
    check(cudaDeviceSynchronize(), "rough powder");
    check(cudaMemcpy(&rough, device, sizeof(MaterialState), cudaMemcpyDeviceToHost), "rough D2H");
    cudaFree(device);
    assert(rough.looseGraphite > smooth.looseGraphite);
    assert(smooth.looseGraphite + smooth.boundGraphite + smooth.toolLoad <= 0.020001f);
    assert(rough.looseGraphite + rough.boundGraphite + rough.toolLoad <= 0.020001f);
}

void graphitePowderCudaCreatesLooseGraphite()
{
    MaterialState state{0.55f, 0.62f, 0.72f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    MaterialState* device = nullptr;
    check(cudaMalloc(&device, sizeof(MaterialState)), "cudaMalloc");
    check(cudaMemcpy(device, &state, sizeof(MaterialState), cudaMemcpyHostToDevice), "graphite powder H2D");
    graphitePowderKernel<<<1, 1>>>(device, 0.7f);
    check(cudaDeviceSynchronize(), "graphite powder");
    check(cudaMemcpy(&state, device, sizeof(MaterialState), cudaMemcpyDeviceToHost), "graphite powder D2H");
    cudaFree(device);
    assert(state.looseGraphite > 0.0f);
    assert(state.boundGraphite > 0.0f);
    assert(state.looseGraphite > state.boundGraphite * 8.0f);
}
}

int main()
{
    check(cudaSetDevice(0), "cudaSetDevice");
    pencilCudaDepositsGraphite();
    eraserCudaRemovesAndDamages();
    cleanPowderCudaDoesNotCreateGraphite();
    dirtyPowderCudaRespondsToToothWithoutCreatingGraphite();
    graphitePowderCudaCreatesLooseGraphite();
    return 0;
}
