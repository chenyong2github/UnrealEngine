// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ParticleRule.h"
#include "ChaosStats.h"
#include "Containers/ArrayView.h"
#include "Containers/ContainersFwd.h"
#include "Chaos/Framework/Parallel.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Spherical Constraints"), STAT_PBD_Spherical, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Spherical Backstop Constraints"), STAT_PBD_SphericalBackstop, STATGROUP_Chaos);

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_Spherical_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_Spherical_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_Spherical_ISPC_Enabled;
#endif

namespace Chaos
{
	template<typename T, int d>
	class CHAOS_API TPBDSphericalConstraint : public TParticleRule<T, d>
	{
	public:
		TPBDSphericalConstraint(
			const uint32 InParticleOffset,
			const uint32 InParticleCount,
			const TArray<TVector<T, d>>& InAnimationPositions,  // Use global indexation (will need adding ParticleOffset)
			const TConstArrayView<T>& InSphereRadii  // Use local indexation
		)
			: AnimationPositions(InAnimationPositions)
			, SphereRadii(InSphereRadii)
			, ParticleOffset(InParticleOffset)
			, SphereRadiiMultiplier((T)1.)
		{
			check(InSphereRadii.Num() == InParticleCount);
		}
		virtual ~TPBDSphericalConstraint() {}

		inline virtual void Apply(TPBDParticles<T, d>& Particles, const T Dt) const override
		{
			SCOPE_CYCLE_COUNTER(STAT_PBD_Spherical);

			if (bChaos_Spherical_ISPC_Enabled)
			{
				ApplyHelperISPC(Particles, Dt);
			}
			else
			{
				ApplyHelper(Particles, Dt);
			}
		}

		inline void SetSphereRadiiMultiplier(const T InSphereRadiiMultiplier)
		{
			SphereRadiiMultiplier = FMath::Max((T)0., InSphereRadiiMultiplier);
		}

	private:
		inline void ApplyHelper(TPBDParticles<T, d>& Particles, const T Dt) const
		{
			const int32 ParticleCount = SphereRadii.Num();

			PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile need for parallel loop based on particle count
			{
				const int32 ParticleIndex = ParticleOffset + Index;

				if (Particles.InvM(ParticleIndex) == 0)
				{
					return;
				}

				const T Radius = SphereRadii[Index] * SphereRadiiMultiplier;
				const TVector<T, d>& Center = AnimationPositions[ParticleIndex];

				const TVector<T, d> CenterToParticle = Particles.P(ParticleIndex) - Center;
				const T DistanceSquared = CenterToParticle.SizeSquared();

				static const T DeadZoneSquareRadius = SMALL_NUMBER; // We will not push the particle away in the dead zone
				if (DistanceSquared > FMath::Square(Radius) + DeadZoneSquareRadius)
				{
					const T Distance = sqrt(DistanceSquared);
					const TVector<T, d> PositionOnSphere = (Radius / Distance) * CenterToParticle;
					Particles.P(ParticleIndex) = Center + PositionOnSphere;
				}
			});
		}

		void ApplyHelperISPC(TPBDParticles<T, d>& Particles, const T Dt) const;

	protected:
		const TArray<TVector<T, d>>& AnimationPositions;  // Use global indexation (will need adding ParticleOffset)
		const TConstArrayView<T> SphereRadii;  // Use local indexation
		const int32 ParticleOffset;
		T SphereRadiiMultiplier;
	};

	template<typename T, int d>
	class CHAOS_API TPBDSphericalBackstopConstraint : public TParticleRule<T, d>
	{
	public:
		TPBDSphericalBackstopConstraint(
			const int32 InParticleOffset,
			const int32 InParticleCount,
			const TArray<TVector<T, d>>& InAnimationPositions,  // Use global indexation (will need adding ParticleOffset)
			const TArray<TVector<T, d>>& InAnimationNormals,  // Use global indexation (will need adding ParticleOffset)
			const TConstArrayView<T>& InSphereRadii,  // Use local indexation
			const TConstArrayView<T>& InSphereOffsetDistances,  // Use local indexation
			const bool bInUseLegacyBackstop  // Do not include the sphere radius in the distance calculations when this is true
		)
			: AnimationPositions(InAnimationPositions)
			, AnimationNormals(InAnimationNormals)
			, SphereRadii(InSphereRadii)
			, SphereOffsetDistances(InSphereOffsetDistances)
			, ParticleOffset(InParticleOffset)
			, SphereRadiiMultiplier((T)1.)
			, bUseLegacyBackstop(bInUseLegacyBackstop)
		{
			check(InSphereRadii.Num() == InParticleCount);
			check(InSphereOffsetDistances.Num() == InParticleCount);
		}
		virtual ~TPBDSphericalBackstopConstraint() {}

		inline virtual void Apply(TPBDParticles<T, d>& Particles, const T Dt) const override
		{
			SCOPE_CYCLE_COUNTER(STAT_PBD_SphericalBackstop);

			if (bUseLegacyBackstop)
			{
				// SphereOffsetDistances includes the sphere radius
				// This is harder to author, and does not follow the NvCloth specs.
				// However, this is how it's been done in the Unreal Engine PhysX cloth implementation.
				if (bChaos_Spherical_ISPC_Enabled)
				{
					ApplyLegacyHelperISPC(Particles, Dt);
				}
				else
				{
					ApplyLegacyHelper(Particles, Dt);
				}
			}
			else
			{
				// SphereOffsetDistances doesn't include the sphere radius
				if (bChaos_Spherical_ISPC_Enabled)
				{
					ApplyHelperISPC(Particles, Dt);
				}
				else
				{
					ApplyHelper(Particles, Dt);
				}
			}
		}

		inline void SetSphereRadiiMultiplier(const T InSphereRadiiMultiplier)
		{
			SphereRadiiMultiplier = FMath::Max((T)0., InSphereRadiiMultiplier);
		}

		inline T GetSphereRadiiMultiplier() const
		{
			return SphereRadiiMultiplier;
		}

		inline bool UseLegacyBackstop() const
		{
			return bUseLegacyBackstop;
		}

	private:
		inline void ApplyHelper(TPBDParticles<T, d>& Particles, const T Dt) const
		{
			const int32 ParticleCount = SphereRadii.Num();

			PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile need for parallel loop based on particle count
			{
				const int32 ParticleIndex = ParticleOffset + Index;

				if (Particles.InvM(ParticleIndex) == 0)
				{
					return;
				}

				const TVector<T, d>& AnimationPosition = AnimationPositions[Index];
				const TVector<T, d>& AnimationNormal = AnimationNormals[Index];

				const T SphereOffsetDistance = SphereOffsetDistances[Index];
				const T Radius = SphereRadii[Index] * SphereRadiiMultiplier;

				const TVector<T, d> Center = AnimationPosition - (Radius + SphereOffsetDistance) * AnimationNormal;  // Non legacy version adds radius to the distance
				const TVector<T, d> CenterToParticle = Particles.P(ParticleIndex) - Center;
				const T DistanceSquared = CenterToParticle.SizeSquared();

				static const T DeadZoneSquareRadius = SMALL_NUMBER;
				if (DistanceSquared < DeadZoneSquareRadius)
				{
					Particles.P(ParticleIndex) = AnimationPosition - SphereOffsetDistance * AnimationNormal;  // Non legacy version adds radius to the distance
				}
				else if (DistanceSquared < FMath::Square(Radius))
				{
					const TVector<T, d> PositionOnSphere = (Radius / sqrt(DistanceSquared)) * CenterToParticle;
					Particles.P(ParticleIndex) = Center + PositionOnSphere;
				}
				// Else the particle is outside the sphere, and there is nothing to do
			});
		}

		inline void ApplyLegacyHelper(TPBDParticles<T, d>& Particles, const T Dt) const
		{
			const int32 ParticleCount = SphereRadii.Num();

			PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile need for parallel loop based on particle count
			{
				const int32 ParticleIndex = ParticleOffset + Index;

				if (Particles.InvM(ParticleIndex) == 0)
				{
					return;
				}

				const TVector<T, d>& AnimationPosition = AnimationPositions[Index];
				const TVector<T, d>& AnimationNormal = AnimationNormals[Index];

				const T SphereOffsetDistance = SphereOffsetDistances[Index];
				const T Radius = SphereRadii[Index] * SphereRadiiMultiplier;

				const TVector<T, d> Center = AnimationPosition - SphereOffsetDistance * AnimationNormal;  // Legacy version already includes the radius within the distance
				const TVector<T, d> CenterToParticle = Particles.P(ParticleIndex) - Center;
				const T DistanceSquared = CenterToParticle.SizeSquared();

				static const T DeadZoneSquareRadius = SMALL_NUMBER;
				if (DistanceSquared < DeadZoneSquareRadius)
				{
					Particles.P(ParticleIndex) = AnimationPosition - (SphereOffsetDistance - Radius) * AnimationNormal;  // Legacy version already includes the radius to the distance
				}
				else if (DistanceSquared < FMath::Square(Radius))
				{
					const TVector<T, d> PositionOnSphere = (Radius / sqrt(DistanceSquared)) * CenterToParticle;
					Particles.P(ParticleIndex) = Center + PositionOnSphere;
				}
				// Else the particle is outside the sphere, and there is nothing to do
			});
		}

		void ApplyLegacyHelperISPC(TPBDParticles<T, d>& Particles, const T Dt) const;
		void ApplyHelperISPC(TPBDParticles<T, d>& Particles, const T Dt) const;

	private:
		const TArray<TVector<T, d>>& AnimationPositions;  // Positions of spheres, use global indexation (will need adding ParticleOffset)
		const TArray<TVector<T, d>>& AnimationNormals; // Sphere offset directions, use global indexation (will need adding ParticleOffset)
		const TConstArrayView<T> SphereRadii; // Start at index 0, use local indexation
		const TConstArrayView<T> SphereOffsetDistances;  // Sphere position offsets, use local indexation
		const int32 ParticleOffset;
		T SphereRadiiMultiplier;
		bool bUseLegacyBackstop;
	};
}
