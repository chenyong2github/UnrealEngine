// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/Array.h"
#include "Chaos/PBDCollisionSpringConstraintsBase.h"
#include "Chaos/PBDParticles.h"

namespace Chaos
{
class FPBDCollisionSpringConstraints : public FPBDCollisionSpringConstraintsBase
{
	typedef FPBDCollisionSpringConstraintsBase Base;
	using Base::Barys;
	using Base::Constraints;

public:
	FPBDCollisionSpringConstraints(
		const int32 InOffset,
		const int32 InNumParticles,
		const TArray<TVec3<int32>>& InElements,
		TSet<TVec2<int32>>&& InDisabledCollisionElements,
		const FReal InThickness = (FReal)1.,
		const FReal InStiffness = (FReal)1.)
	    : Base(InOffset, InNumParticles, InElements, MoveTemp(InDisabledCollisionElements), InThickness, InStiffness)
	{}

	virtual ~FPBDCollisionSpringConstraints() {}

	using Base::Init;

	void Apply(FPBDParticles& Particles, const FReal Dt, const int32 ConstraintIndex) const
	{
		const int32 i = ConstraintIndex;
		const TVector<int32, 4>& Constraint = Constraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];
		const int32 i4 = Constraint[3];
		const FVec3 Delta = Base::GetDelta(Particles, i);
		static const FReal Multiplier = (FReal)1.;  // TODO(mlentine): Figure out what the best multiplier here is
		if (Particles.InvM(i1) > 0)
		{
			Particles.P(i1) += Multiplier * Particles.InvM(i1) * Delta;
		}
		if (Particles.InvM(i2))
		{
			Particles.P(i2) -= Multiplier * Particles.InvM(i2) * Barys[i][0] * Delta;
		}
		if (Particles.InvM(i3))
		{
			Particles.P(i3) -= Multiplier * Particles.InvM(i3) * Barys[i][1] * Delta;
		}
		if (Particles.InvM(i4))
		{
			Particles.P(i4) -= Multiplier * Particles.InvM(i4) * Barys[i][2] * Delta;
		}
	}

	void Apply(FPBDParticles& InParticles, const FReal Dt) const
	{
		for (int32 i = 0; i < Constraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

	void Apply(FPBDParticles& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices) const
	{
		for (int32 i : InConstraintIndices)
		{
			Apply(InParticles, Dt, i);
		}
	}

};
}
#endif
