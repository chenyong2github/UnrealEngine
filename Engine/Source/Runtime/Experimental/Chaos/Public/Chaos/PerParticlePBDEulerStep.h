// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PerParticleRule.h"

namespace Chaos
{
/**
 * Update position and rotation based on velocity and angular velocity.
 */
template<class T, int d>
class TPerParticlePBDEulerStep : public TPerParticleRule<T, d>
{
  public:
	TPerParticlePBDEulerStep() {}
	virtual ~TPerParticlePBDEulerStep() {}

	template<class T_PARTICLES>
	inline void ApplyHelper(T_PARTICLES& InParticles, const T Dt, const int32 Index) const
	{
		InParticles.P(Index) = InParticles.X(Index) + InParticles.V(Index) * Dt;
	}

	inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
	{
		ApplyHelper(InParticles, Dt, Index);
	}

	inline void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
	{
		FVec3 PCoM = FParticleUtilities::GetCoMWorldPosition(InParticles, Index);
		FRotation3 QCoM = FParticleUtilities::GetCoMWorldRotation(InParticles, Index);

		PCoM = PCoM + InParticles.V(Index) * Dt;
		QCoM = FRotation3::IntegrateRotationWithAngularVelocity(QCoM, InParticles.W(Index), Dt);

		FParticleUtilities::SetCoMWorldTransform(InParticles, Index, PCoM, QCoM);
	}

	inline void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const override
	{
		FVec3 PCoM = FParticleUtilities::GetCoMWorldPosition(&Particle);
		FRotation3 QCoM = FParticleUtilities::GetCoMWorldRotation(&Particle);

		PCoM = PCoM + Particle.V() * Dt;
		QCoM = FRotation3::IntegrateRotationWithAngularVelocity(QCoM, Particle.W(), Dt);

		FParticleUtilities::SetCoMWorldTransform(&Particle, PCoM, QCoM);
	}
};
}
