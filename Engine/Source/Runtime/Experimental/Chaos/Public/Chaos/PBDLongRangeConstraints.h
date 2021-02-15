// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDLongRangeConstraintsBase.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDConstraintContainer.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Long Range Constraint"), STAT_PBD_LongRange, STATGROUP_Chaos);

namespace Chaos
{
class CHAOS_API FPBDLongRangeConstraints : public FPBDLongRangeConstraintsBase, public FPBDConstraintContainer
{
public:
	typedef FPBDLongRangeConstraintsBase Base;
	typedef typename Base::EMode EMode;

	using Base::GetMode;

	FPBDLongRangeConstraints(
		const FDynamicParticles& InParticles,
		const TMap<int32, TSet<uint32>>& PointToNeighbors,
		const int32 NumberOfAttachments = 1,
		const FReal Stiffness = (FReal)1.,
		const FReal LimitScale = (FReal)1.,
		const EMode Mode = EMode::AccurateTetherFastLength)
		: FPBDLongRangeConstraintsBase(InParticles, PointToNeighbors, NumberOfAttachments, Stiffness, LimitScale, Mode) {}
	virtual ~FPBDLongRangeConstraints() {}

	void Apply(FPBDParticles& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices) const;
	void Apply(FPBDParticles& InParticles, const FReal Dt) const;

private:
	using Base::MEuclideanConstraints;
	using Base::MGeodesicConstraints;
	using Base::MDists;
	using Base::GetDelta;

	template<class TConstraintType>
	void Apply(const TConstraintType& Constraint, FPBDParticles& InParticles, const FReal Dt, const FReal RefDist) const
	{
		const int32 i2 = Constraint[Constraint.Num() - 1];
		checkSlow(InParticles.InvM(i2) > (FReal)0.);
		InParticles.P(i2) += GetDelta(Constraint, InParticles, RefDist);
	}

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
