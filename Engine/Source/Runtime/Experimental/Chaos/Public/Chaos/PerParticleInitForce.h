// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleRule.h"

namespace Chaos
{
template<class T, int d>
class TPerParticleInitForce : public TPerParticleRule<T, d>
{
  public:
	TPerParticleInitForce() {}
	virtual ~TPerParticleInitForce() {}

	inline void Apply(TDynamicParticles<T, d>& InParticles, const T Dt, const int Index) const override //-V762
	{
		InParticles.F(Index) = TVector<T, d>(0);
	}

	inline void Apply(TRigidParticles<T, d>& InParticles, const T Dt, const int Index) const override //-V762
	{
		InParticles.F(Index) = TVector<T,d>(0);
		InParticles.Torque(Index) = TVector<T,d>(0);
	}

	inline void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const override //-V762
	{
		Particle.F() = TVector<T, d>(0);
		Particle.Torque() = TVector<T, d>(0);
	}
};
}
