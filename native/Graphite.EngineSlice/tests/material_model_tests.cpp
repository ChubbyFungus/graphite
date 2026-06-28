#include "graphite_types.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace
{
float clamp01(float value)
{
    return std::min(1.0f, std::max(0.0f, value));
}

float gradeSoftness(PencilGrade grade)
{
    if (grade == PencilGrade::FourH) return 0.25f;
    if (grade == PencilGrade::EightB) return 1.0f;
    return 0.58f;
}

struct MaterialState
{
    float paperHeight = 0.55f;
    float paperRoughness = 0.62f;
    float paperBinding = 0.72f;
    float looseGraphite = 0.0f;
    float boundGraphite = 0.0f;
    float compaction = 0.0f;
    float damage = 0.0f;
    float toolLoad[4]{};
};

float graphiteTotal(const MaterialState& state)
{
    return state.looseGraphite + state.boundGraphite + state.toolLoad[0] + state.toolLoad[1] + state.toolLoad[2] + state.toolLoad[3];
}

float spendToolLoad(MaterialState& state, int index, float requested)
{
    requested = std::max(0.0f, requested);
    const float spent = std::min(state.toolLoad[index], requested);
    state.toolLoad[index] -= spent;
    return spent;
}

void applyCenterStroke(MaterialState& state, ToolParams params, float pressure, float speed = 0.0f)
{
    const float softness = gradeSoftness(params.grade);
    const float speedFactor = clamp01(speed / 1800.0f);
    const float edge = 1.0f;
    const float heightTooth = state.paperHeight * (1.0f - state.compaction * 0.55f);
    const float roughness = clamp01(state.paperRoughness - state.compaction * 0.38f + state.damage * 0.12f);
    const float catchTooth = clamp01(heightTooth * 0.35f + roughness * 0.65f);
    const float binding = clamp01(state.paperBinding - state.damage * 0.38f);

    if (params.tool == ToolKind::RegularEraser)
    {
        const float lift = edge * (0.35f + pressure * 0.55f);
        state.looseGraphite = std::max(0.0f, state.looseGraphite - lift * 0.95f);
        state.boundGraphite = std::max(0.0f, state.boundGraphite - lift * (0.12f + (1.0f - binding) * 0.28f));
        state.compaction = clamp01(state.compaction + edge * pressure * 0.012f);
        state.damage = clamp01(state.damage + edge * pressure * pressure * 0.006f);
        state.paperBinding = clamp01(state.paperBinding - edge * pressure * pressure * 0.0018f);
        return;
    }

    if (params.tool == ToolKind::KneadedEraser)
    {
        const float eraserLoad = clamp01(state.toolLoad[3]);
        const float saturation = 1.0f - eraserLoad * 0.72f;
        const float lift = edge * (0.08f + pressure * 0.28f) * saturation;
        const float liftedLoose = std::min(state.looseGraphite, lift * 0.78f);
        const float liftedBound = std::min(state.boundGraphite, lift * (0.04f + (1.0f - binding) * 0.10f));
        state.looseGraphite = std::max(0.0f, state.looseGraphite - liftedLoose);
        state.boundGraphite = std::max(0.0f, state.boundGraphite - liftedBound);
        state.toolLoad[3] += (liftedLoose + liftedBound) * 0.55f;
        state.compaction = clamp01(state.compaction + edge * pressure * 0.002f);
        state.damage = clamp01(state.damage + edge * pressure * 0.0007f);
        return;
    }

    if (params.tool == ToolKind::PowderBrush)
    {
        const float toothCatch = clamp01(0.18f + catchTooth * 0.82f + binding * 0.10f - state.compaction * 0.30f - state.damage * 0.24f);
        const float lift = std::min(state.looseGraphite, edge * state.looseGraphite * (0.003f + pressure * 0.012f) * (1.05f - toothCatch * 0.42f));
        const float boundDust = std::min(state.boundGraphite, edge * (0.00008f + pressure * 0.00034f));
        const float powder = spendToolLoad(state, 2, edge * toothCatch * clamp01(state.toolLoad[2]) * (0.0008f + pressure * 0.0030f) * (1.0f - speedFactor * 0.38f));
        state.looseGraphite = clamp01(state.looseGraphite - lift + boundDust * 0.25f + powder);
        state.boundGraphite = std::max(0.0f, state.boundGraphite - boundDust);
        state.toolLoad[2] += lift + boundDust * 0.75f;
        state.compaction = clamp01(state.compaction + edge * pressure * 0.001f);
        return;
    }

    if (params.tool == ToolKind::GraphitePowder)
    {
        const float toothCatch = clamp01(0.20f + catchTooth * 0.76f + roughness * 0.18f + binding * 0.06f - state.compaction * 0.22f - state.damage * 0.18f);
        const float powder = edge * toothCatch * (0.0018f + pressure * 0.0054f) * (1.0f - speedFactor * 0.42f);
        state.looseGraphite = clamp01(state.looseGraphite + powder);
        state.boundGraphite = clamp01(state.boundGraphite + powder * (0.035f + pressure * 0.040f + binding * 0.025f));
        state.compaction = clamp01(state.compaction + edge * pressure * 0.00035f);
        return;
    }

    const float graphiteFill = clamp01(state.boundGraphite * 0.58f + state.looseGraphite * 0.24f);
    const float catchFactor = clamp01(0.32f + catchTooth * 0.62f + binding * 0.16f + graphiteFill * 0.22f - state.compaction * 0.22f + state.damage * 0.08f);
    const float deposit = edge * (0.014f + pressure * pressure * 0.075f) * (0.34f + softness * 0.90f) * (1.0f - speedFactor * 0.32f);
    const float loose = deposit * (0.56f + softness * 0.62f) * catchFactor;
    const float bound = deposit * (0.18f + pressure * 0.48f) * (0.40f + catchTooth + binding * 0.44f);
    state.looseGraphite = std::min(1.0f, state.looseGraphite + loose);
    state.boundGraphite = std::min(1.0f, state.boundGraphite + bound);
    state.compaction = clamp01(state.compaction + edge * pressure * (0.0025f + (1.0f - softness) * 0.006f) * (1.0f - speedFactor * 0.25f));
}

void softPencilDepositsMoreThanHardPencil()
{
    ToolParams hard{};
    hard.tool = ToolKind::Pencil;
    hard.grade = PencilGrade::FourH;
    ToolParams soft = hard;
    soft.grade = PencilGrade::EightB;
    MaterialState hardState;
    MaterialState softState;
    applyCenterStroke(hardState, hard, 0.7f);
    applyCenterStroke(softState, soft, 0.7f);
    assert(softState.looseGraphite > hardState.looseGraphite);
    assert(softState.looseGraphite + softState.boundGraphite > hardState.looseGraphite + hardState.boundGraphite);
}

void erasersHaveDifferentMaterialEffects()
{
    MaterialState regular;
    regular.looseGraphite = 0.6f;
    regular.boundGraphite = 0.4f;
    MaterialState kneaded = regular;
    ToolParams regularTool{};
    regularTool.tool = ToolKind::RegularEraser;
    ToolParams kneadedTool{};
    kneadedTool.tool = ToolKind::KneadedEraser;
    applyCenterStroke(regular, regularTool, 0.8f);
    applyCenterStroke(kneaded, kneadedTool, 0.8f);
    assert(regular.looseGraphite < kneaded.looseGraphite);
    assert(regular.damage > kneaded.damage);
    assert(kneaded.toolLoad[3] > 0.0f);
}

void cleanPowderBrushDoesNotCreateGraphite()
{
    ToolParams powder{};
    powder.tool = ToolKind::PowderBrush;
    MaterialState blank;
    const float before = graphiteTotal(blank);
    applyCenterStroke(blank, powder, 0.7f);
    assert(graphiteTotal(blank) <= before + 0.000001f);
}

void dirtyPowderBrushDependsOnPaperToothWithoutCreatingGraphite()
{
    ToolParams powder{};
    powder.tool = ToolKind::PowderBrush;
    MaterialState smooth;
    smooth.paperHeight = 0.30f;
    smooth.paperRoughness = 0.25f;
    smooth.toolLoad[2] = 0.02f;
    MaterialState rough;
    rough.paperHeight = 0.70f;
    rough.paperRoughness = 0.78f;
    rough.toolLoad[2] = 0.02f;
    const float smoothBefore = graphiteTotal(smooth);
    const float roughBefore = graphiteTotal(rough);
    applyCenterStroke(smooth, powder, 0.7f);
    applyCenterStroke(rough, powder, 0.7f);
    assert(rough.looseGraphite > smooth.looseGraphite);
    assert(graphiteTotal(smooth) <= smoothBefore + 0.000001f);
    assert(graphiteTotal(rough) <= roughBefore + 0.000001f);
}

void graphitePowderCreatesLooseGraphite()
{
    ToolParams powder{};
    powder.tool = ToolKind::GraphitePowder;
    MaterialState blank;
    applyCenterStroke(blank, powder, 0.7f);
    assert(blank.looseGraphite > 0.0f);
    assert(blank.boundGraphite > 0.0f);
    assert(blank.looseGraphite > blank.boundGraphite * 8.0f);
}
}

int main()
{
    softPencilDepositsMoreThanHardPencil();
    erasersHaveDifferentMaterialEffects();
    cleanPowderBrushDoesNotCreateGraphite();
    dirtyPowderBrushDependsOnPaperToothWithoutCreatingGraphite();
    graphitePowderCreatesLooseGraphite();
    return 0;
}
