// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDAxialSpringConstraints.h"
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

void FPBDAxialSpringConstraints::InitColor(const FDynamicParticles& InParticles)
{
	// In dev builds we always color so we can tune the system without restarting. See Apply()
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	if (MConstraints.Num() > Chaos_AxialSpring_ParallelConstraintCount)
#endif
	{
		MConstraintsPerColor = FGraphColoring::ComputeGraphColoring(MConstraints, InParticles);
	}
}

void FPBDAxialSpringConstraints::ApplyImp(FPBDParticles& InParticles, const FReal Dt, const int32 i) const
{
		const auto& constraint = MConstraints[i];
		int32 i1 = constraint[0];
		int32 i2 = constraint[1];
		int32 i3 = constraint[2];
		auto Delta = Base::GetDelta(InParticles, i);
		FReal Multiplier = 2 / (FMath::Max(MBarys[i], 1 - MBarys[i]) + 1);
		if (InParticles.InvM(i1) > 0)
		{
			InParticles.P(i1) -= Multiplier * InParticles.InvM(i1) * Delta;
		}
		if (InParticles.InvM(i2))
		{
			InParticles.P(i2) += Multiplier * InParticles.InvM(i2) * MBarys[i] * Delta;
		}
		if (InParticles.InvM(i3))
		{
			InParticles.P(i3) += Multiplier * InParticles.InvM(i3) * (1 - MBarys[i]) * Delta;
		}
}

void FPBDAxialSpringConstraints::Apply(FPBDParticles& InParticles, const FReal Dt) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_AxialSpring);
	if ((MConstraintsPerColor.Num() > 0) && (MConstraints.Num() > Chaos_AxialSpring_ParallelConstraintCount))
	{
		for (const auto& Constraints : MConstraintsPerColor)
		{
			if (bRealTypeCompatibleWithISPC && bChaos_AxialSpring_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::ApplyAxialSpringConstraints(
				    (ispc::FVector*)&InParticles.GetP()[0],
				    (ispc::FIntVector*)&MConstraints.GetData()[0],
				    &Constraints.GetData()[0],
				    &InParticles.GetInvM().GetData()[0],
				    &MBarys.GetData()[0],
				    &MDists.GetData()[0],
				    MStiffness,
				    Constraints.Num());
#endif
			}
			else
			{
				PhysicsParallelFor(Constraints.Num(), [&](const int32 Index) {
					ApplyImp(InParticles, Dt, Constraints[Index]);
				});
			}
		}
	}
	else
	{
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			ApplyImp(InParticles, Dt, i);
		}
	}

}
