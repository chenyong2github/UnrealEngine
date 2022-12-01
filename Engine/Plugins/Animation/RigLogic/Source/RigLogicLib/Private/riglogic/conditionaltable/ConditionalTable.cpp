// Copyright Epic Games, Inc. All Rights Reserved.

#include "riglogic/conditionaltable/ConditionalTable.h"

#include "riglogic/TypeDefs.h"
#include "riglogic/utils/Extd.h"

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace rl4 {

namespace {

const float clampMin = 0.0f;
const float clampMax = 1.0f;

Vector<std::uint16_t> buildIntervalSkipMap(const Vector<std::uint16_t>& inputIndices,
                                           const Vector<std::uint16_t>& outputIndices,
                                           MemoryResource* memRes) {
    assert(inputIndices.size() == outputIndices.size());
    Vector<std::uint16_t> intervalsRemaining{inputIndices.size(), {}, memRes};
    for (std::size_t i = {}; i < inputIndices.size();) {
        std::uint16_t intervalCount = 1u;
        const std::uint16_t currentInputIndex = inputIndices[i];
        const std::uint16_t currentOutputIndex = outputIndices[i];
        for (std::size_t j = i + 1ul;
             (j < inputIndices.size()) && (currentInputIndex == inputIndices[j]) && (currentOutputIndex == outputIndices[j]);
             ++j) {
            intervalsRemaining[j] = intervalCount++;
        }
        auto start = intervalsRemaining.data() + i;
        std::reverse(start, start + intervalCount);
        i += intervalCount;
    }
    return intervalsRemaining;
}

}  // namespace

ConditionalTable::ConditionalTable(MemoryResource* memRes) :
    intervalsRemaining{memRes},
    inputIndices{memRes},
    outputIndices{memRes},
    fromValues{memRes},
    toValues{memRes},
    slopeValues{memRes},
    cutValues{memRes},
    inputCount{},
    outputCount{} {
}

ConditionalTable::ConditionalTable(Vector<std::uint16_t>&& inputIndices_,
                                   Vector<std::uint16_t>&& outputIndices_,
                                   Vector<float>&& fromValues_,
                                   Vector<float>&& toValues_,
                                   Vector<float>&& slopeValues_,
                                   Vector<float>&& cutValues_,
                                   std::uint16_t inputCount_,
                                   std::uint16_t outputCount_,
                                   MemoryResource* memRes) :
    intervalsRemaining{buildIntervalSkipMap(inputIndices_, outputIndices_, memRes)},
    inputIndices{std::move(inputIndices_)},
    outputIndices{std::move(outputIndices_)},
    fromValues{std::move(fromValues_)},
    toValues{std::move(toValues_)},
    slopeValues{std::move(slopeValues_)},
    cutValues{std::move(cutValues_)},
    inputCount{inputCount_},
    outputCount{outputCount_} {
}

std::uint16_t ConditionalTable::getInputCount() const {
    return inputCount;
}

std::uint16_t ConditionalTable::getOutputCount() const {
    return outputCount;
}

void ConditionalTable::calculate(const float* inputs, float* outputs, std::uint16_t chunkSize) const {
    std::fill_n(outputs, outputCount, 0.0f);

    for (std::uint16_t i = {}; i < chunkSize; ++i) {
        const float inValue = inputs[inputIndices[i]];
        const float from = fromValues[i];
        const float to = toValues[i];
        if ((from <= inValue) && (inValue <= to)) {
            const std::uint16_t outIndex = outputIndices[i];
            const float slope = slopeValues[i];
            const float cut = cutValues[i];
            outputs[outIndex] += (slope * inValue + cut);
            i = static_cast<std::uint16_t>(i + intervalsRemaining[i]);
        }
    }

    for (std::size_t i = 0ul; i < outputCount; ++i) {
        outputs[i] = extd::clamp(outputs[i], clampMin, clampMax);
    }
}

void ConditionalTable::calculate(const float* inputs, float* outputs) const {
    calculate(inputs, outputs, static_cast<std::uint16_t>(outputIndices.size()));
}

}  // namespace rl4
