// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDParticles.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ParticleRule.h"

namespace Chaos
{

/**
 * A Particle Rule that applies some effect to all particles in parallel.
 * This should only be used if the effect on any particle is independent of
 * all others (i.e., the implementation of ApplySingle only reads/writes to
 * the one particle).
 */
template<class T, int d>
class TPerParticleRule : public TParticleRule<T, d>
{
  public:
	void Apply(TParticles<T, d>& InParticles, const T Dt) const override
	{
		ApplyPerParticle(InParticles, Dt);
	}

	void Apply(TDynamicParticles<T, d>& InParticles, const T Dt) const override
	{
		ApplyPerParticle(InParticles, Dt);
	}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const override
	{
		ApplyPerParticle(InParticles, Dt);
	}

	template<class T_PARTICLES>
	void ApplyPerParticle(T_PARTICLES& InParticles, const T Dt) const
	{
		PhysicsParallelFor(InParticles.Size(), [&](int32 Index) {
			Apply(InParticles, Dt, Index);
		});
	}

	virtual void Apply(TParticles<T, d>& InParticles, const T Dt, const int Index) const { check(0); };
	virtual void Apply(TDynamicParticles<T, d>& InParticles, const T Dt, const int Index) const { Apply(static_cast<TParticles<T, d>&>(InParticles), Dt, Index); };
	virtual void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int Index) const { Apply(static_cast<TDynamicParticles<T, d>&>(InParticles), Dt, Index); };
	virtual void Apply(TRigidParticles<T, d>& InParticles, const T Dt, const int Index) const { Apply(static_cast<TParticles<T, d>&>(InParticles), Dt, Index); };
	virtual void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const int Index) const { Apply(static_cast<TRigidParticles<T, d>&>(InParticles), Dt, Index); };

	virtual void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const { check(0); }
};
}
