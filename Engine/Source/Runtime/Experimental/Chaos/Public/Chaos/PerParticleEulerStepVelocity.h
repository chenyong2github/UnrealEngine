// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleRule.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
class FPerParticleEulerStepVelocity : public FPerParticleRule
{
  public:
	FPerParticleEulerStepVelocity() {}
	virtual ~FPerParticleEulerStepVelocity() {}

	template<class T_PARTICLES>
	inline void ApplyHelper(T_PARTICLES& InParticles, const FReal Dt, const int32 Index) const
	{
		InParticles.V(Index) += InParticles.F(Index) * InParticles.InvM(Index) * Dt;
	}

	inline void Apply(FDynamicParticles& InParticles, const FReal Dt, const int32 Index) const override //-V762
	{
		if (InParticles.InvM(Index) == 0)
			return;
		ApplyHelper(InParticles, Dt, Index);
	}

	inline void Apply(TRigidParticles<FReal, 3>& InParticles, const FReal Dt, const int32 Index) const override //-V762
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
#if CHAOS_PARTICLE_ACTORTRANSFORM
		const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(InParticles.R(Index) * InParticles.RotationOfMass(Index), InParticles.InvI(Index));
#else
		const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(InParticles.R(Index), InParticles.InvI(Index));
#endif
		InParticles.W(Index) += WorldInvI * InParticles.Torque(Index) * Dt;
	}
	
	inline void Apply(TTransientPBDRigidParticleHandle<FReal, 3>& Particle, const FReal Dt) const override //-V762
	{
		Particle.V() += Particle.F() * Particle.InvM() * Dt;
#if CHAOS_PARTICLE_ACTORTRANSFORM
		const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(Particle.R() * Particle.RotationOfMass(), Particle.InvI());
#else
		const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(Particle.R(Index), Particle.InvI());
#endif
		Particle.W() += WorldInvI * Particle.Torque() * Dt;
	}
};

template<class T, int d>
using TPerParticleEulerStepVelocity UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPerParticleEulerStepVelocity instead") = FPerParticleEulerStepVelocity;

}
