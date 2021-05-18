// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/GraphColoring.h"
#include "Chaos/Framework/Parallel.h"
#include "ChaosStats.h"
#include "ChaosLog.h"
#include "HAL/IConsoleManager.h"

#if INTEL_ISPC
#include "PBDSpringConstraints.ispc.generated.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Spring Constraint"), STAT_PBD_Spring, STATGROUP_Chaos);

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_Spring_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosSpringISPCEnabled(TEXT("p.Chaos.Spring.ISPC"), bChaos_Spring_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in Spring constraints"));
#endif

using namespace Chaos;

// @todo(chaos): the parallel threshold (or decision to run parallel) should probably be owned by the solver and passed to the constraint container
static int32 Chaos_Spring_ParallelConstraintCount = 100;
#if !UE_BUILD_SHIPPING
FAutoConsoleVariableRef CVarChaosSpringParallelConstraintCount(TEXT("p.Chaos.Spring.ParallelConstraintCount"), Chaos_Spring_ParallelConstraintCount, TEXT("If we have more constraints than this, use parallel-for in Apply."));
#endif

void FPBDSpringConstraints::InitColor(const FPBDParticles& Particles)
{
	// In dev builds we always color so we can tune the system without restarting. See Apply()
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	if (Constraints.Num() > Chaos_Spring_ParallelConstraintCount)
#endif
	{
		ConstraintsPerColor = FGraphColoring::ComputeGraphColoring(Constraints, Particles);
	}
}

void FPBDSpringConstraints::ApplyHelper(FPBDParticles& Particles, const FReal Dt, const int32 ConstraintIndex, const FReal ExpStiffnessValue) const
{
	const TVec2<int32>& Constraint = Constraints[ConstraintIndex];
	const int32 i1 = Constraint[0];
	const int32 i2 = Constraint[1];
	const FVec3 Delta =  Base::GetDelta(Particles, ConstraintIndex, ExpStiffnessValue);
	if (Particles.InvM(i1) > (FReal)0.)
	{
		Particles.P(i1) -= Particles.InvM(i1) * Delta;
	}
	if (Particles.InvM(i2) > (FReal)0.)
	{
		Particles.P(i2) += Particles.InvM(i2) * Delta;
	}
}

void FPBDSpringConstraints::Apply(FPBDParticles& Particles, const FReal Dt) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_Spring);
	if ((ConstraintsPerColor.Num() > 0) && (Constraints.Num() > Chaos_Spring_ParallelConstraintCount))
	{
		if (!Stiffness.HasWeightMap())
		{
			const FReal ExpStiffnessValue = (FReal)Stiffness;

#if INTEL_ISPC
			if (bRealTypeCompatibleWithISPC && bChaos_Spring_ISPC_Enabled)
			{
				for (const TArray<int32>& ConstraintBatch : ConstraintsPerColor)
				{
					ispc::ApplySpringConstraints(
						(ispc::FVector*) & Particles.GetP()[0],
						(ispc::FIntVector2*) & Constraints.GetData()[0],
						&ConstraintBatch.GetData()[0],
						&Particles.GetInvM().GetData()[0],
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
			if (bRealTypeCompatibleWithISPC && bChaos_Spring_ISPC_Enabled)
			{
				for (const TArray<int32>& ConstraintBatch : ConstraintsPerColor)
				{
					ispc::ApplySpringConstraintsWithWeightMaps(
						(ispc::FVector*) & Particles.GetP()[0],
						(ispc::FIntVector2*) & Constraints.GetData()[0],
						&ConstraintBatch.GetData()[0],
						&Particles.GetInvM().GetData()[0],
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
