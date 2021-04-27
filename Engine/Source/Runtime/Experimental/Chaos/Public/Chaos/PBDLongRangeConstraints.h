// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDLongRangeConstraintsBase.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Long Range Constraint"), STAT_PBD_LongRange, STATGROUP_Chaos);

namespace Chaos
{
class CHAOS_API FPBDLongRangeConstraints final : public FPBDLongRangeConstraintsBase
{
public:
	typedef FPBDLongRangeConstraintsBase Base;
	typedef typename Base::FTether FTether;
	typedef typename Base::EMode EMode;

	FPBDLongRangeConstraints(
		const FPBDParticles& Particles,
		const int32 InParticleOffset,
		const int32 InParticleCount,
		const TMap<int32, TSet<int32>>& PointToNeighbors,
		const TConstArrayView<FReal>& StiffnessMultipliers,
		const int32 MaxNumTetherIslands = 4,
		const FVec2& InStiffness = FVec2((FReal)1., (FReal)1.),
		const FReal LimitScale = (FReal)1.,
		const EMode InMode = EMode::Geodesic)
		: FPBDLongRangeConstraintsBase(Particles, InParticleOffset, InParticleCount, PointToNeighbors, StiffnessMultipliers, MaxNumTetherIslands, InStiffness, LimitScale, InMode) {}
	virtual ~FPBDLongRangeConstraints() {}

	void Apply(FPBDParticles& Particles, const FReal Dt, const TArray<int32>& ConstraintIndices) const;
	void Apply(FPBDParticles& Particles, const FReal Dt) const;

private:
	using Base::Tethers;
	using Base::TethersView;
	using Base::Stiffness;
	using Base::ParticleOffset;
};
}

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_LongRange_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_LongRange_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_LongRange_ISPC_Enabled;
#endif
