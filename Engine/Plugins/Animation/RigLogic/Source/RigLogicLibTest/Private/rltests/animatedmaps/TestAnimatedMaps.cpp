#include "rltests/Defs.h"
#include "rltests/animatedmaps/AnimatedMapFixtures.h"
#include "rltests/conditionaltable/ConditionalTableFixtures.h"

#include "riglogic/animatedmaps/AnimatedMaps.h"
#include "riglogic/types/Aliases.h"

#include <pma/resources/AlignedMemoryResource.h>

#include <array>

class AnimatedMapsTest : public ::testing::TestWithParam<std::uint16_t> {
};

TEST_P(AnimatedMapsTest, LODLimitsCondTableSize) {
    pma::AlignedMemoryResource amr;
    rl4::Vector<std::uint16_t> lods{2, 1};
    auto conditionals = ConditionalTableFactory::withMultipleIODefaults(&amr);
    rl4::AnimatedMaps animatedMaps{std::move(lods), std::move(conditionals)};

    const auto lod = GetParam();
    const float expected[2][2] = {
        {0.3f, 0.6f},  // LOD0
        {0.3f, 0.0f}  // LOD1
    };
    std::array<float, 2ul> outputs;
    animatedMaps.calculate(rl4::ConstArrayView<float>{conditionalTableInputs},
                           rl4::ArrayView<float>{outputs},
                           lod);
    ASSERT_ELEMENTS_EQ(outputs, expected[lod], 2ul);
}

INSTANTIATE_TEST_SUITE_P(AnimatedMapsTest, AnimatedMapsTest, ::testing::Values(0u, 1u));
