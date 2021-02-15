// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDShapeConstraintsBase.h"
#include "Chaos/PerParticleRule.h"

namespace Chaos
{
class FPerParticlePBDShapeConstraints : public FPerParticleRule, public FPBDShapeConstraintsBase
{
	typedef FPBDShapeConstraintsBase Base;

  public:
	FPerParticlePBDShapeConstraints(const FReal Stiffness = (FReal)1.)
	    : Base(Stiffness)
	{
	}
	FPerParticlePBDShapeConstraints(const FDynamicParticles& InParticles, const TArray<FVec3>& TargetPositions, const FReal Stiffness = (FReal)1.)
	    : Base(InParticles, TargetPositions, Stiffness)
	{
	}
	virtual ~FPerParticlePBDShapeConstraints() {}

	// TODO(mlentine): We likely need to use time n positions here
	void Apply(FPBDParticles& InParticles, const FReal Dt, const int32 Index) const override //-V762
	{
		if (InParticles.InvM(Index) > 0)
		{
			InParticles.P(Index) -= InParticles.InvM(Index) * Base::GetDelta(InParticles, Index);
		}
	}

	void Apply(FPBDParticles& InParticles, const FReal Dt) const override //-V762
	{
		PhysicsParallelFor(InParticles.Size(), [&](int32 Index) {
			Apply(InParticles, Dt, Index);
		});
	}
};
}
