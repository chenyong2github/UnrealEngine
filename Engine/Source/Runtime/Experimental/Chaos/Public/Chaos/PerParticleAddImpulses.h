// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleRule.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
	template<class T, int d>
	class TPerParticleAddImpulses : public TPerParticleRule<T, d>
	{
	public:
		TPerParticleAddImpulses() {}
		virtual ~TPerParticleAddImpulses() {}

		template<class T_PARTICLES>
		inline void ApplyHelper(T_PARTICLES& InParticles, const T Dt, const int32 Index) const
		{
			InParticles.V(Index) += InParticles.LinearImpulse(Index) * InParticles.InvM(Index);
		}

		inline void Apply(TDynamicParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
		{
			// @todo(mlentine): Is this something we want to support?
			ensure(false);
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
#if CHAOS_PARTICLE_ACTORTRANSFORM
			const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(InParticles.R(Index) * InParticles.RotationOfMass(Index), InParticles.InvI(Index));
#else
			const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(InParticles.R(Index), InParticles.InvI(Index));
#endif

			InParticles.W(Index) += WorldInvI * InParticles.AngularImpulse(Index);
			InParticles.LinearImpulse(Index) = TVector<T, 3>(0);
			InParticles.AngularImpulse(Index) = TVector<T, 3>(0);
		}

		inline void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const override //-V762
		{
			Particle.V() += Particle.LinearImpulse() * Particle.InvM();
#if CHAOS_PARTICLE_ACTORTRANSFORM
			const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(Particle.R() * Particle.RotationOfMass(), Particle.InvI());
#else
			const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(Particle.R(), Particle.InvI());
#endif
			Particle.W() += WorldInvI * Particle.AngularImpulse();
			Particle.LinearImpulse() = TVector<T, 3>(0);
			Particle.AngularImpulse() = TVector<T, 3>(0);
		}
	};
}
