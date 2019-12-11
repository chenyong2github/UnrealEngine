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
		FVec3 PCoM = FParticleUtilitiesXR::GetCoMWorldPosition(InParticles, Index);
		FRotation3 QCoM = FParticleUtilitiesXR::GetCoMWorldRotation(InParticles, Index);

		PCoM = PCoM + InParticles.V(Index) * Dt;
		QCoM = FRotation3::IntegrateRotationWithAngularVelocity(QCoM, InParticles.W(Index), Dt);

		FParticleUtilitiesPQ::SetCoMWorldTransform(InParticles, Index, PCoM, QCoM);
	}

	inline void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const override
	{
		FVec3 PCoM = FParticleUtilitiesXR::GetCoMWorldPosition(&Particle);
		FRotation3 QCoM = FParticleUtilitiesXR::GetCoMWorldRotation(&Particle);

		PCoM = PCoM + Particle.V() * Dt;
		QCoM = FRotation3::IntegrateRotationWithAngularVelocity(QCoM, Particle.W(), Dt);

		FParticleUtilitiesPQ::SetCoMWorldTransform(&Particle, PCoM, QCoM);
	}
};
}
