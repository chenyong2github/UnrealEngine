// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/PBDCollisionSpringConstraintsBase.h"

namespace Chaos::Softs
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
		const FTriangleMesh& InTriangleMesh,
		const TArray<FSolverVec3>* InRestPositions,
		TSet<TVec2<int32>>&& InDisabledCollisionElements,
		const FSolverReal InThickness = (FSolverReal)1.,
		const FSolverReal InStiffness = (FSolverReal)1.)
		: Base(InOffset, InNumParticles, InTriangleMesh, InRestPositions, MoveTemp(InDisabledCollisionElements), InThickness, InStiffness)
	{}

	virtual ~FPBDCollisionSpringConstraints() override {}

	using Base::Init;

	void Apply(FSolverParticles& Particles, const FSolverReal Dt, const int32 ConstraintIndex) const
	{
		const int32 i = ConstraintIndex;
		const TVector<int32, 4>& Constraint = Constraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];
		const int32 i4 = Constraint[3];
		const FSolverVec3 Delta = Base::GetDelta(Particles, i);
		static const FSolverReal Multiplier = (FSolverReal)0.5;  // TODO(mlentine): Figure out what the best multiplier here is
		if (Particles.InvM(i1) > 0)
		{
			Particles.P(i1) += Multiplier * Particles.InvM(i1) * Delta;
		}
		if (Particles.InvM(i2) > (FSolverReal)0.)
		{
			Particles.P(i2) -= Multiplier * Particles.InvM(i2) * Barys[i][0] * Delta;
		}
		if (Particles.InvM(i3) > (FSolverReal)0.)
		{
			Particles.P(i3) -= Multiplier * Particles.InvM(i3) * Barys[i][1] * Delta;
		}
		if (Particles.InvM(i4) > (FSolverReal)0.)
		{
			Particles.P(i4) -= Multiplier * Particles.InvM(i4) * Barys[i][2] * Delta;
		}
	}

	void Apply(FSolverParticles& InParticles, const FSolverReal Dt) const
	{
		for (int32 i = 0; i < Constraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

	void Apply(FSolverParticles& InParticles, const FSolverReal Dt, const TArray<int32>& InConstraintIndices) const
	{
		for (int32 i : InConstraintIndices)
		{
			Apply(InParticles, Dt, i);
		}
	}

};

}  // End namespace Chaos::Softs

#endif
