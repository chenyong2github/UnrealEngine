// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleRule.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
template<class T, int d>
class TPerParticleEulerStepVelocity : public TPerParticleRule<T, d>
{
  public:
	TPerParticleEulerStepVelocity() {}
	virtual ~TPerParticleEulerStepVelocity() {}

	template<class T_PARTICLES>
	inline void ApplyHelper(T_PARTICLES& InParticles, const T Dt, const int32 Index) const
	{
		InParticles.V(Index) += InParticles.F(Index) * InParticles.InvM(Index) * Dt;
	}

	inline void Apply(TDynamicParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
	{
		if (InParticles.InvM(Index) == 0)
			return;
		ApplyHelper(InParticles, Dt, Index);
	}

	inline void Apply(TRigidParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
	{
		if (InParticles.InvM(Index) == 0 || InParticles.Disabled(Index) || InParticles.Sleeping(Index))
			return;
		ApplyHelper(InParticles, Dt, Index);

		//
		// TODO: This is the first-order approximation.
		//       If needed, we might eventually want to do a second order Euler's Equation,
		//       but if we do that we'll need to do a transform into a rotating reference frame.
		//       Just using W += InvI * (Torque - W x (I * W)) * dt is not correct, since Torque
		//		 and W are in an inertial frame.
		//
		const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(InParticles.R(Index), InParticles.InvI(Index));
		InParticles.W(Index) += WorldInvI * InParticles.Torque(Index) * Dt;
	}
	
	inline void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const override //-V762
	{
		Particle.V() += Particle.F() * Particle.InvM() * Dt;
		const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(Particle.R(), Particle.InvI());
		Particle.W() += WorldInvI * Particle.Torque() * Dt;
	}
};
}
