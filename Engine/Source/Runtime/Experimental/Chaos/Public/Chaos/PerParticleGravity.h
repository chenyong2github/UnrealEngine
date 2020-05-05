// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/DynamicParticles.h"
#include "Chaos/PerParticleRule.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	template<class T, int d>
	class TPerParticleGravity : public TPerParticleRule<T, d>
	{
	public:
		TPerParticleGravity()
			: MAcceleration(TVector<T, d>(0, 0, (T)-980.665)) {}
		TPerParticleGravity(const TVector<T, d>& G)
			: MAcceleration(G) {}
		TPerParticleGravity(const TVector<T, d>& Direction, const T Magnitude)
			: MAcceleration(Direction * Magnitude) {}
		virtual ~TPerParticleGravity() {}

		// TODO: Remove this - we should no longer be using indices directly.
		//       This has been kept around for cloth, which uses it in
		//       PBDEvolution.
		template<class T_PARTICLES>
		inline void ApplyHelper(T_PARTICLES& InParticles, const T Dt, const int Index) const
		{
			InParticles.F(Index) += MAcceleration * InParticles.M(Index);
		}
		inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int Index) const override //-V762
		{
			ApplyHelper(InParticles, Dt, Index);
		}

		void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const override //-V762
		{
			if(Particle.GravityEnabled())
			{
				Particle.F() += MAcceleration * Particle.M();
			}
		}

		void SetAcceleration(const TVector<T, d>& Acceleration)
		{ MAcceleration = Acceleration; }

		const TVector<T, d>& GetAcceleration() const
		{ return MAcceleration; }

	private:
		TVector<T, d> MAcceleration;
	};
}
