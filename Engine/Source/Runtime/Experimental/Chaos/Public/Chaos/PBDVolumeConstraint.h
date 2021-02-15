// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDVolumeConstraintBase.h"
#include "Chaos/ParticleRule.h"

namespace Chaos
{
class FPBDVolumeConstraint : public FParticleRule, public FPBDVolumeConstraintBase
{
	typedef FPBDVolumeConstraintBase Base;

  public:
	  FPBDVolumeConstraint(const FDynamicParticles& InParticles, TArray<TVec3<int32>>&& InConstraints, const FReal InStiffness = (FReal)1.)
	    : Base(InParticles, MoveTemp(InConstraints), InStiffness) {}
	virtual ~FPBDVolumeConstraint() {}

	void Apply(FPBDParticles& InParticles, const FReal dt) const override //-V762
	{
		auto W = Base::GetWeights(InParticles, (FReal)1.);
		auto Grads = Base::GetGradients(InParticles);
		auto S = Base::GetScalingFactor(InParticles, Grads, W);
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			InParticles.P(i) -= S * W[i] * Grads[i];
		}
	}
};

template<class T>
using TPBDVolumeConstraint UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDVolumeConstraint instead") = FPBDVolumeConstraint;

}
