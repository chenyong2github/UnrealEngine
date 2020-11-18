// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/Array.h"
#include "Chaos/PBDCollisionSpringConstraintsBase.h"
#include "Chaos/PBDParticles.h"

namespace Chaos
{
template<class T, int d>
class TPBDCollisionSpringConstraints : public TPBDCollisionSpringConstraintsBase<T, d>
{
	typedef TPBDCollisionSpringConstraintsBase<T, d> Base;
	using Base::Barys;
	using Base::Constraints;

public:
	TPBDCollisionSpringConstraints(
		const int32 InOffset,
		const int32 InNumParticles,
		const TArray<TVector<int32, 3>>& InElements,
		TSet<TVector<int32, 2>>&& InDisabledCollisionElements,
		const T InThickness = (T)1.0,
		const T InStiffness = (T)1.0)
	    : Base(InOffset, InNumParticles, InElements, MoveTemp(InDisabledCollisionElements), InThickness, InStiffness)
	{}

	virtual ~TPBDCollisionSpringConstraints() {}

	using Base::Init;

	void Apply(TPBDParticles<T, d>& Particles, const T Dt, const int32 ConstraintIndex) const
	{
		const int32 i = ConstraintIndex;
		const TVector<int32, 4>& Constraint = Constraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];
		const int32 i4 = Constraint[3];
		const TVector<T, d> Delta = Base::GetDelta(Particles, i);
		static const T Multiplier = 1;  // TODO(mlentine): Figure out what the best multiplier here is
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

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const
	{
		for (int32 i = 0; i < Constraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		for (int32 i : InConstraintIndices)
		{
			Apply(InParticles, Dt, i);
		}
	}

};
}
#endif
