// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleRule.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
	template<class T, int d>
	class TPerParticleEtherDrag : public TPerParticleRule<T, d>
	{
	public:
		TPerParticleEtherDrag() {}
		virtual ~TPerParticleEtherDrag() {}

		inline void ApplyHelper(FVec3& V, FVec3& W, FReal LinearDamp, FReal AngularDamp, T Dt) const
		{
			const FReal LinearMultiplier = FMath::Max(FReal(0), FReal(1) - (LinearDamp * Dt));
			V *= LinearMultiplier;

			const FReal AngularMultiplier = FMath::Max(FReal(0), FReal(1) - (AngularDamp * Dt));
			W *= AngularMultiplier;
		}

		inline void Apply(TDynamicParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
		{
			ensure(false);
		}

		inline void Apply(TRigidParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
		{
			ApplyHelper(InParticles.V(Index), InParticles.W(Index), InParticles.LinearEtherDrag(Index), InParticles.AngularEtherDrag(Index), Dt);
		}

		inline void Apply(TTransientPBDRigidParticleHandle<T, d>& Particle, const T Dt) const override //-V762
		{
			ApplyHelper(Particle.V(), Particle.W(), Particle.LinearEtherDrag(), Particle.AngularEtherDrag(), Dt);
		}
	};
}
