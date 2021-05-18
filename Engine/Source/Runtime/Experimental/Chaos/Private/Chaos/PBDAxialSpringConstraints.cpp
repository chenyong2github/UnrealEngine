// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDAxialSpringConstraints.h"
#include "Chaos/GraphColoring.h"
#include "ChaosStats.h"
#include "ChaosLog.h"
#if INTEL_ISPC
#include "PBDAxialSpringConstraints.ispc.generated.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Axial Spring Constraint"), STAT_PBD_AxialSpring, STATGROUP_Chaos);

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_AxialSpring_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosAxialSpringISPCEnabled(TEXT("p.Chaos.AxialSpring.ISPC"), bChaos_AxialSpring_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in AxialSpring constraints"));
#endif

using namespace Chaos;

// @todo(chaos): the parallel threshold (or decision to run parallel) should probably be owned by the solver and passed to the constraint container
int32 Chaos_AxialSpring_ParallelConstraintCount = 100;
FAutoConsoleVariableRef CVarChaosAxialSpringParallelConstraintCount(TEXT("p.Chaos.AxialSpring.ParallelConstraintCount"), Chaos_AxialSpring_ParallelConstraintCount, TEXT("If we have more constraints than this, use parallel-for in Apply."));

void FPBDAxialSpringConstraints::InitColor(const FPBDParticles& InParticles)
{
	// In dev builds we always color so we can tune the system without restarting. See Apply()
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	if (Constraints.Num() > Chaos_AxialSpring_ParallelConstraintCount)
#endif
	{
		ConstraintsPerColor = FGraphColoring::ComputeGraphColoring(Constraints, InParticles);
	}
}

void FPBDAxialSpringConstraints::ApplyHelper(FPBDParticles& Particles, const FReal Dt, const int32 ConstraintIndex, const FReal ExpStiffnessValue) const
{
		const auto& constraint = Constraints[ConstraintIndex];
		const int32 i1 = constraint[0];
		const int32 i2 = constraint[1];
		const int32 i3 = constraint[2];
		const FVec3 Delta = Base::GetDelta(Particles, ConstraintIndex, ExpStiffnessValue);
		const FReal Multiplier = 2 / (FMath::Max(Barys[ConstraintIndex], 1 - Barys[ConstraintIndex]) + 1);
		if (Particles.InvM(i1) > (FReal)0.)
		{
			Particles.P(i1) -= Multiplier * Particles.InvM(i1) * Delta;
		}
		if (Particles.InvM(i2) > (FReal)0.)
		{
			Particles.P(i2) += Multiplier * Particles.InvM(i2) * Barys[ConstraintIndex] * Delta;
		}
		if (Particles.InvM(i3) > (FReal)0.)
		{
			Particles.P(i3) += Multiplier * Particles.InvM(i3) * ((FReal)1. - Barys[ConstraintIndex]) * Delta;
		}
}

void FPBDAxialSpringConstraints::Apply(FPBDParticles& Particles, const FReal Dt) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_AxialSpring);
	if (ConstraintsPerColor.Num() > 0 && Constraints.Num() > Chaos_AxialSpring_ParallelConstraintCount)
	{
		if (!Stiffness.HasWeightMap())
		{
			const FReal ExpStiffnessValue = (FReal)Stiffness;

#if INTEL_ISPC
			if (bRealTypeCompatibleWithISPC && bChaos_AxialSpring_ISPC_Enabled)
			{
				for (const TArray<int32>& ConstraintBatch : ConstraintsPerColor)
				{
					ispc::ApplyAxialSpringConstraints(
						(ispc::FVector*)&Particles.GetP()[0],
						(ispc::FIntVector*)&Constraints.GetData()[0],
						&ConstraintBatch.GetData()[0],
						&Particles.GetInvM().GetData()[0],
						&Barys.GetData()[0],
						&Dists.GetData()[0],
						ExpStiffnessValue,
						ConstraintBatch.Num());
				}
			}
			else
#endif
			{
				for (const TArray<int32>& ConstraintBatch : ConstraintsPerColor)
				{
					PhysicsParallelFor(ConstraintBatch.Num(), [&](const int32 Index)
					{
						const int32 ConstraintIndex = ConstraintBatch[Index];
						ApplyHelper(Particles, Dt, ConstraintIndex, ExpStiffnessValue);
					});
				}
			}
		}
		else  // Has weight maps
		{
#if INTEL_ISPC
			if (bRealTypeCompatibleWithISPC && bChaos_AxialSpring_ISPC_Enabled)
			{
				for (const TArray<int32>& ConstraintBatch : ConstraintsPerColor)
				{
					ispc::ApplyAxialSpringConstraintsWithWeightMaps(
						(ispc::FVector*)&Particles.GetP()[0],
						(ispc::FIntVector*)&Constraints.GetData()[0],
						&ConstraintBatch.GetData()[0],
						&Particles.GetInvM().GetData()[0],
						&Barys.GetData()[0],
						&Dists.GetData()[0],
						&Stiffness.GetIndices().GetData()[0],
						&Stiffness.GetTable().GetData()[0],
						ConstraintBatch.Num());
				}
			}
			else
#endif
			{
				for (const TArray<int32>& ConstraintBatch : ConstraintsPerColor)
				{
					PhysicsParallelFor(ConstraintBatch.Num(), [&](const int32 Index)
					{
						const int32 ConstraintIndex = ConstraintBatch[Index];
						const FReal ExpStiffnessValue = Stiffness[ConstraintIndex];
						ApplyHelper(Particles, Dt, ConstraintIndex, ExpStiffnessValue);
					});
				}
			}
		}
	}
	else
	{
		if (!Stiffness.HasWeightMap())
		{
			const FReal ExpStiffnessValue = (FReal)Stiffness;
			for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
			{
				ApplyHelper(Particles, Dt, ConstraintIndex, ExpStiffnessValue);
			}
		}
		else
		{
			for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
			{
				const FReal ExpStiffnessValue = Stiffness[ConstraintIndex];
				ApplyHelper(Particles, Dt, ConstraintIndex, ExpStiffnessValue);
			}
		}
	}

}
