// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDLongRangeConstraints.h"
#include "ChaosLog.h"
#if INTEL_ISPC
#include "PBDLongRangeConstraints.ispc.generated.h"
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_LongRange_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosLongRangeISPCEnabled(TEXT("p.Chaos.LongRange.ISPC"), bChaos_LongRange_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in long range constraints"));
#endif

using namespace Chaos;

void FPBDLongRangeConstraints::Apply(FPBDParticles& Particles, const FReal /*Dt*/, const TArray<int32>& ConstraintIndices) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_LongRange);
	for (const int32 ConstraintIndex : ConstraintIndices)
	{
		const FTether& Tether = Tethers[ConstraintIndex];
		Particles.P(Tether.End) += Stiffness * Tether.GetDelta(Particles);
	}
}

void FPBDLongRangeConstraints::Apply(FPBDParticles& Particles, const FReal /*Dt*/) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_LongRange);

	if (bRealTypeCompatibleWithISPC && bChaos_LongRange_ISPC_Enabled)
	{
#if INTEL_ISPC
		// Run particles in parallel, and ranges in sequence to avoid a race condition when updating the same particle from different tethers
		TethersView.RangeFor([this, &Particles](TArray<FTether>& InTethers, int32 Offset, int32 Range)
			{
				ispc::ApplyLongRangeConstraints(
					(ispc::FVector*)Particles.GetP().GetData(),
					(ispc::FTether*)(InTethers.GetData() + Offset),
					Stiffness,
					Range - Offset);
			});
#endif
	}
	else
	{
		// Run particles in parallel, and rangesin sequence to avoid a race condition when updating the same particle from different tethers
		static const int32 MinParallelSize = 500;
		TethersView.ParallelFor([this, &Particles](TArray<FTether>& InTethers, int32 Index)
			{
				const FTether& Tether = InTethers[Index];
				Particles.P(Tether.End) += Stiffness * Tether.GetDelta(Particles);
			}, MinParallelSize);
	}
}

