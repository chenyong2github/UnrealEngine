// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDParticles.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PerParticleRule.h"

namespace Chaos
{
template<class T, int d>
class TPerParticlePBDUpdateFromDeltaPosition : public TPerParticleRule<T, d>
{
  public:
	TPerParticlePBDUpdateFromDeltaPosition() {}
	virtual ~TPerParticlePBDUpdateFromDeltaPosition() {}

	template<class T_PARTICLES>
	inline void ApplyHelper(T_PARTICLES& InParticles, const T Dt, const int32 Index) const
	{
		InParticles.V(Index) = (InParticles.P(Index) - InParticles.X(Index)) / Dt;
		//InParticles.X(Index) = InParticles.P(Index);
	}

	inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
	{
		InParticles.V(Index) = (InParticles.P(Index) - InParticles.X(Index)) / Dt;
		InParticles.X(Index) = InParticles.P(Index);
	}

	inline void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
	{
		ApplyHelper(InParticles, Dt, Index);
		InParticles.W(Index) = TRotation<T, d>::CalculateAngularVelocity(InParticles.R(Index), InParticles.Q(Index), Dt);
	}

	inline void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const override //-V762
	{
		Particle.V() = (Particle.P() - Particle.X()) / Dt;
		Particle.W() = TRotation<T, d>::CalculateAngularVelocity(Particle.R(), Particle.Q(), Dt);
	}
};
}
