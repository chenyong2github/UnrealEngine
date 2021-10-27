// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/joints/bpcm/strategies/Block4.h"
#include "trimd/TRiMD.h"

namespace rl4 {

namespace bpcm {

template<typename T>
using SSEJointCalculationStrategy = Block4JointCalculationStrategy<trimd::sse::F128, T>;

}  // namespace bpcm

}  // namespace rl4
