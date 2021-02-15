// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDRigidParticles.h"
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

void FPBDSpringConstraints::InitColor(const FDynamicParticles& InParticles)
{
	// In dev builds we always color so we can tune the system without restarting. See Apply()
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	if (MConstraints.Num() > Chaos_Spring_ParallelConstraintCount)
#endif
	{
		MConstraintsPerColor = FGraphColoring::ComputeGraphColoring(MConstraints, InParticles);
	}
}

template<class T_PARTICLES>
void FPBDSpringConstraints::Apply(T_PARTICLES& InParticles, const FReal Dt, const int32 InConstraintIndex) const
{
	const int32 i = InConstraintIndex;
	{
		const auto& Constraint = MConstraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		auto Delta = Base::GetDelta(InParticles, i);
		if (InParticles.InvM(i1) > 0)
		{
			InParticles.P(i1) -= InParticles.InvM(i1) * Delta;
		}
		if (InParticles.InvM(i2) > 0)
		{
			InParticles.P(i2) += InParticles.InvM(i2) * Delta;
		}
	}
}

void FPBDSpringConstraints::Apply(FPBDParticles& InParticles, const FReal Dt) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_Spring);
	if ((MConstraintsPerColor.Num() > 0) && (MConstraints.Num() > Chaos_Spring_ParallelConstraintCount))
	{
		for (const auto& Constraints : MConstraintsPerColor)
		{
			if (bRealTypeCompatibleWithISPC && bChaos_Spring_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::ApplySpringConstraints(
				    (ispc::FVector*)&InParticles.GetP()[0],
				    (ispc::FIntVector2*)&MConstraints.GetData()[0],
				    &Constraints.GetData()[0],
				    &InParticles.GetInvM().GetData()[0],
				    &MDists.GetData()[0],
				    MStiffness,
				    Constraints.Num());
#endif
			}
			else
			{
				PhysicsParallelFor(Constraints.Num(), [&](const int32 Index) {
					Apply(InParticles, Dt, Constraints[Index]);
				});
			}
		}
	}
	else
	{
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}
}

void FPBDSpringConstraints::Apply(TPBDRigidParticles<FReal, 3>& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_Spring);
	for (int32 i : InConstraintIndices)
	{
		const auto& Constraint = MConstraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		check(InParticles.Island(i1) == InParticles.Island(i2) || InParticles.Island(i1) == INDEX_NONE || InParticles.Island(i2) == INDEX_NONE);
		Apply(InParticles, Dt, i);
	}
}
