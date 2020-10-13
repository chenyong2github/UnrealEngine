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

template<class T, int d>
void TPBDLongRangeConstraints<T, d>::Apply(TPBDParticles<T, d>& InParticles, const T Dt, const TArray<int32>& ConstraintIndices) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_LongRange);
	switch (GetMode())
	{
	case EMode::FastTetherFastLength:
	case EMode::AccurateTetherFastLength:
		for (int32 i : ConstraintIndices)
		{
			Apply(MEuclideanConstraints[i], InParticles, Dt, MDists[i]);
		}
		break;
	case EMode::AccurateTetherAccurateLength:
		for (int32 i : ConstraintIndices)
		{
			Apply(MGeodesicConstraints[i], InParticles, Dt, MDists[i]);
		}
		break;
	default:
		unimplemented();
		break;
	}
}

template<class T, int d>
void TPBDLongRangeConstraints<T, d>::Apply(TPBDParticles<T, d>& InParticles, const T Dt) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_LongRange);
	switch (GetMode())
	{
	case EMode::FastTetherFastLength:
	case EMode::AccurateTetherFastLength:
		for (int32 i = 0; i < MEuclideanConstraints.Num(); ++i)
		{
			Apply(MEuclideanConstraints[i], InParticles, Dt, MDists[i]);
		}
		break;
	case EMode::AccurateTetherAccurateLength:
		for (int32 i = 0; i < MGeodesicConstraints.Num(); ++i)
		{
			Apply(MGeodesicConstraints[i], InParticles, Dt, MDists[i]);
		}
		break;
	default:
		unimplemented();
		break;
	}
}

template<>
void TPBDLongRangeConstraints<float, 3>::Apply(TPBDParticles<float, 3>& InParticles, const float Dt) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_LongRange);

	switch (GetMode())
	{
	case EMode::FastTetherFastLength:
	case EMode::AccurateTetherFastLength:
		if (bChaos_LongRange_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::ApplyLongRangeConstraints(
				(ispc::FVector*)InParticles.GetP().GetData(),
				(ispc::FUIntVector2*)MEuclideanConstraints.GetData(),
				MDists.GetData(),
				MStiffness,
				MEuclideanConstraints.Num());
#endif
		}
		else
		{
			for (int32 i = 0; i < MEuclideanConstraints.Num(); ++i)
			{
				Apply(MEuclideanConstraints[i], InParticles, Dt, MDists[i]);
			}
		}
		break;

	case EMode::AccurateTetherAccurateLength:
		for (int32 i = 0; i < MGeodesicConstraints.Num(); ++i)
		{
			Apply(MGeodesicConstraints[i], InParticles, Dt, MDists[i]);
		}
		break;

	default:
		unimplemented();
		break;
	}
}

template class Chaos::TPBDLongRangeConstraints<float, 3>;
