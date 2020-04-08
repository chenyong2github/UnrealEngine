// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleDampVelocity.h"

namespace Chaos
{
	template<class T, int d>
	class TPerGroupDampVelocity : public TPerParticleDampVelocity<T, d>
	{
	public:
		TPerGroupDampVelocity(const TArray<uint32>& ParticleGroupIds, const TArray<T>& PerGroupDamping)
			: MParticleGroupIds(ParticleGroupIds)
			, MPerGroupDamping(PerGroupDamping)
		{
		}
		virtual ~TPerGroupDampVelocity() override {}

		inline void Apply(TDynamicParticles<T, d>& InParticles, const T Dt, const int32 Index) const override
		{
			ApplyHelper(InParticles, Dt, Index);
		}

		inline void Apply(TRigidParticles<T, d>& InParticles, const T Dt, const int32 Index) const override
		{
			ApplyHelper(InParticles, Dt, Index);
		}

	private:
		template<class T_PARTICLES>
		inline void ApplyHelper(T_PARTICLES& InParticles, const T Dt, const int32 Index) const
		{
			if (InParticles.InvM(Index) != (T)0.)  // Only damp dynamic/rigid particles
			{
				TPerParticleDampVelocity<T, d>::MCoefficient = MPerGroupDamping[MParticleGroupIds[Index]];
				TPerParticleDampVelocity<T, d>::ApplyHelper(InParticles, Dt, Index);
			}
		}

	private:
		const TArray<uint32>& MParticleGroupIds;
		const TArray<T>& MPerGroupDamping;
	};
}
