// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDSpringConstraintsBase.h"
#include "Chaos/PerParticleRule.h"

namespace Chaos
{
class FPerParticlePBDSpringConstraints : public FPerParticleRule, public FPBDSpringConstraintsBase
{
	typedef FPBDSpringConstraintsBase Base;
	using Base::MConstraints;

  public:
	FPerParticlePBDSpringConstraints(const FDynamicParticles& InParticles, TArray<TVec2<int32>>&& Constraints, const FReal Stiffness = (FReal)1.)
	    : Base(InParticles, MoveTemp(Constraints), Stiffness)
	{
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			const auto& Constraint = MConstraints[i];
			int32 i1 = Constraint[0];
			int32 i2 = Constraint[1];
			if (i1 >= MParticleToConstraints.Num())
			{
				MParticleToConstraints.SetNum(i1 + 1);
			}
			if (i2 >= MParticleToConstraints.Num())
			{
				MParticleToConstraints.SetNum(i2 + 1);
			}
			MParticleToConstraints[i1].Add(i);
			MParticleToConstraints[i2].Add(i);
		}
	}
	virtual ~FPerParticlePBDSpringConstraints() {}

	// TODO(mlentine): We likely need to use time n positions here
	void Apply(FPBDParticles& InParticles, const FReal Dt, const int32 Index) const override //-V762
	{
		for (int32 i = 0; i < MParticleToConstraints[Index].Num(); ++i)
		{
			int32 CIndex = MParticleToConstraints[Index][i];
			const auto& Constraint = MConstraints[CIndex];
			int32 i1 = Constraint[0];
			int32 i2 = Constraint[1];
			if (Index == i1 && InParticles.InvM(i1) > 0)
			{
				InParticles.P(i1) -= InParticles.InvM(i1) * Base::GetDelta(InParticles, CIndex);
			}
			else if (InParticles.InvM(i2) > 0)
			{
				check(Index == i2);
				InParticles.P(i2) += InParticles.InvM(i2) * Base::GetDelta(InParticles, CIndex);
			}
		}
	}

  private:
	TArray<TArray<int32>> MParticleToConstraints;
};
}
