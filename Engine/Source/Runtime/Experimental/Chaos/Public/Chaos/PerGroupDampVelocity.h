// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PerParticleDampVelocity.h"
#include "Chaos/PBDActiveView.h"

namespace Chaos
{
	template<class T, int d>
	class TPerGroupDampVelocity : private TPerParticleDampVelocity<T, d>
	{
	public:
		TPerGroupDampVelocity(
			const TArray<uint32>& InParticleGroupIds,
			const TArray<T>& InGroupDampings,
			TArray<TVector<T, d>>& InGroupCenterOfMass,
			TArray<TVector<T, d>>& InGroupVelocity,
			TArray<TVector<T, d>>& InGroupAngularVelocity)
			: ParticleGroupIds(InParticleGroupIds)
			, GroupDampings(InGroupDampings)
			, GroupCenterOfMass(InGroupCenterOfMass)
			, GroupVelocity(InGroupVelocity)
			, GroupAngularVelocity(InGroupAngularVelocity)
		{
		}
		virtual ~TPerGroupDampVelocity() override {}

		template<class T_PARTICLES>
		inline void UpdateGroupPositionBasedState(const TPBDActiveView<T_PARTICLES>& ParticlesActiveView)
		{
			ParticlesActiveView.RangeFor(
				[this](T_PARTICLES& Particles, int32 Offset, int32 Range)
				{
					UpdatePositionBasedState(Particles, Offset, Range);

					const uint32 ParticleGroupId = ParticleGroupIds[Offset];  // Assume range and group id correlate
					GroupCenterOfMass[ParticleGroupId] = MXcm;
					GroupVelocity[ParticleGroupId] = MVcm;
					GroupAngularVelocity[ParticleGroupId] = MOmega;
				});
		}

		inline void Apply(TDynamicParticles<T, d>& Particles, const T Dt, const int32 Index) const override
		{
			ApplyHelper(Particles, Dt, Index);
		}

		inline void Apply(TRigidParticles<T, d>& Particles, const T Dt, const int32 Index) const override
		{
			ApplyHelper(Particles, Dt, Index);
		}

		// Apply damping without checking for kinematic particles
		template<class T_PARTICLES>
		inline void ApplyFast(T_PARTICLES& Particles, const int32 Index, const TVector<T, d>& CenterOfMass, const TVector<T, d>& Velocity, const TVector<T, d>& AngularVelocity, const T Damping) const
		{
			const TVector<T, d> R = Particles.X(Index) - CenterOfMass;
			const TVector<T, d> Dv = Velocity - Particles.V(Index) + TVector<T, d>::CrossProduct(R, AngularVelocity);
			Particles.V(Index) += Damping * Dv;
		}

	private:
		using TPerParticleDampVelocity<T, d>::UpdatePositionBasedState;

		template<class T_PARTICLES>
		inline void ApplyHelper(T_PARTICLES& Particles, const T Dt, const int32 Index) const
		{
			if (Particles.InvM(Index) != (T)0.)  // Only damp dynamic/rigid particles
			{
				const uint32 ParticleGroupId = ParticleGroupIds[Index];
				ApplyFast(Particles, Index,
					GroupCenterOfMass[ParticleGroupId],
					GroupVelocity[ParticleGroupId],
					GroupAngularVelocity[ParticleGroupId],
					GroupDampings[ParticleGroupId]);
			}
		}

	private:
		using TPerParticleDampVelocity<T, d>::MXcm;
		using TPerParticleDampVelocity<T, d>::MVcm;
		using TPerParticleDampVelocity<T, d>::MOmega;

		const TArray<uint32>& ParticleGroupIds;
		const TArray<T>& GroupDampings;
		TArray<TVector<T, d>>& GroupCenterOfMass;
		TArray<TVector<T, d>>& GroupVelocity;
		TArray<TVector<T, d>>& GroupAngularVelocity;
	};
}
