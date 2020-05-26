// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDAxialSpringConstraints.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Axial Spring Constraint"), STAT_PBD_AxialSpring, STATGROUP_Chaos);
	
using namespace Chaos;

void FPBDAxialSpringConstraints::ApplyImp(TPBDParticles<FReal, 3>& InParticles, const FReal Dt, const int32 i) const
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

void FPBDAxialSpringConstraints::Apply(TPBDParticles<FReal, 3>& InParticles, const FReal Dt) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_AxialSpring);
	if (MConstraintsPerColor.Num())
	{
		for (const auto& Constraints : MConstraintsPerColor)
		{
			PhysicsParallelFor(Constraints.Num(), [&](const int32 Index) {
				ApplyImp(InParticles, Dt, Constraints[Index]);
			});
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
