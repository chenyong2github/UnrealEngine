// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
		TPerParticleGravity(const TVector<T, d>& Direction = TVector<T, d>(0, 0, -1), const T Magnitude = (T)980.665)
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
			if (!UsesGravity.Contains(Particle.Handle()))
			{
				Particle.F() += MAcceleration * Particle.M();
			}
		}

		void SetEnabled(const TGeometryParticleHandleImp<T, d, true>& Handle, bool bEnabled)
		{
			if (bEnabled)
			{
				if (UsesGravity.Contains(&Handle))
				{
					UsesGravity.Remove(&Handle);
				}
			}
			else
			{
				if (!UsesGravity.Contains(&Handle))
				{
					UsesGravity.Add(&Handle);
				}
			}
		}

		void SetAcceleration(const TVector<T, d>& Acceleration)
		{
			MAcceleration = Acceleration;
		}

	private:
		TVector<T, d> MAcceleration;
		TSet< const TGeometryParticleHandleImp<T, d, true>* > UsesGravity;
	};
}
