// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDSpringConstraints.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Spring Constraint"), STAT_PBD_Spring, STATGROUP_Chaos);

using namespace Chaos;

void FPBDSpringConstriants::Apply(TPBDParticles<FReal, 3>& InParticles, const FReal Dt, const int32 InConstraintIndex)
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

void FPBDSpringConstriants::Apply(TPBDParticles<FReal, 3>& InParticles, const FReal Dt)
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_Spring);
	if (MConstraintsPerColor.Num())
	{
		PhysicsParallelFor(MConstraintsPerColor.Num(), [&](const int32 Color){
			for (int32 i = 0; i < MConstraintsPerColor[Color].Num(); ++i)
			{
				Apply(InParticles, Dt, MConstraintsPerColor[Color][i]);
			}
		});
	}
	else
	{
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}
}

void FPBDSpringConstriants::Apply(TPBDRigidParticles<FReal, 3>& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices)
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
