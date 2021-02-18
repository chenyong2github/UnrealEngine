// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ParticleRule.h"
#include "Chaos/PBDParticles.h"
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
	class CHAOS_API FPBDSphericalConstraint : public FParticleRule
	{
	public:
		FPBDSphericalConstraint(
			const uint32 InParticleOffset,
			const uint32 InParticleCount,
			const TArray<FVec3>& InAnimationPositions,  // Use global indexation (will need adding ParticleOffset)
			const TConstArrayView<FReal>& InSphereRadii  // Use local indexation
		)
			: AnimationPositions(InAnimationPositions)
			, SphereRadii(InSphereRadii)
			, ParticleOffset(InParticleOffset)
			, SphereRadiiMultiplier((FReal)1.)
		{
			check(InSphereRadii.Num() == InParticleCount);
		}
		virtual ~FPBDSphericalConstraint() {}

		inline virtual void Apply(FPBDParticles& Particles, const FReal Dt) const override
		{
			SCOPE_CYCLE_COUNTER(STAT_PBD_Spherical);

			if (bRealTypeCompatibleWithISPC && bChaos_Spherical_ISPC_Enabled)
			{
				ApplyHelperISPC(Particles, Dt);
			}
			else
			{
				ApplyHelper(Particles, Dt);
			}
		}

		inline void SetSphereRadiiMultiplier(const FReal InSphereRadiiMultiplier)
		{
			SphereRadiiMultiplier = FMath::Max((FReal)0., InSphereRadiiMultiplier);
		}

	private:
		inline void ApplyHelper(FPBDParticles& Particles, const FReal Dt) const
		{
			const int32 ParticleCount = SphereRadii.Num();

			PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile need for parallel loop based on particle count
			{
				const int32 ParticleIndex = ParticleOffset + Index;

				if (Particles.InvM(ParticleIndex) == 0)
				{
					return;
				}

				const FReal Radius = SphereRadii[Index] * SphereRadiiMultiplier;
				const FVec3& Center = AnimationPositions[ParticleIndex];

				const FVec3 CenterToParticle = Particles.P(ParticleIndex) - Center;
				const FReal DistanceSquared = CenterToParticle.SizeSquared();

				static const FReal DeadZoneSquareRadius = SMALL_NUMBER; // We will not push the particle away in the dead zone
				if (DistanceSquared > FMath::Square(Radius) + DeadZoneSquareRadius)
				{
					const FReal Distance = sqrt(DistanceSquared);
					const FVec3 PositionOnSphere = (Radius / Distance) * CenterToParticle;
					Particles.P(ParticleIndex) = Center + PositionOnSphere;
				}
			});
		}

		void ApplyHelperISPC(FPBDParticles& Particles, const FReal Dt) const;

	protected:
		const TArray<FVec3>& AnimationPositions;  // Use global indexation (will need adding ParticleOffset)
		const TConstArrayView<FReal> SphereRadii;  // Use local indexation
		const int32 ParticleOffset;
		FReal SphereRadiiMultiplier;
	};

	class CHAOS_API FPBDSphericalBackstopConstraint : public FParticleRule
	{
	public:
		FPBDSphericalBackstopConstraint(
			const int32 InParticleOffset,
			const int32 InParticleCount,
			const TArray<FVec3>& InAnimationPositions,  // Use global indexation (will need adding ParticleOffset)
			const TArray<FVec3>& InAnimationNormals,  // Use global indexation (will need adding ParticleOffset)
			const TConstArrayView<FReal>& InSphereRadii,  // Use local indexation
			const TConstArrayView<FReal>& InSphereOffsetDistances,  // Use local indexation
			const bool bInUseLegacyBackstop  // Do not include the sphere radius in the distance calculations when this is true
		)
			: AnimationPositions(InAnimationPositions)
			, AnimationNormals(InAnimationNormals)
			, SphereRadii(InSphereRadii)
			, SphereOffsetDistances(InSphereOffsetDistances)
			, ParticleOffset(InParticleOffset)
			, SphereRadiiMultiplier((FReal)1.)
			, bUseLegacyBackstop(bInUseLegacyBackstop)
		{
			check(InSphereRadii.Num() == InParticleCount);
			check(InSphereOffsetDistances.Num() == InParticleCount);
		}
		virtual ~FPBDSphericalBackstopConstraint() {}

		inline virtual void Apply(FPBDParticles& Particles, const FReal Dt) const override
		{
			SCOPE_CYCLE_COUNTER(STAT_PBD_SphericalBackstop);

			if (bUseLegacyBackstop)
			{
				// SphereOffsetDistances includes the sphere radius
				// This is harder to author, and does not follow the NvCloth specs.
				// However, this is how it's been done in the Unreal Engine PhysX cloth implementation.
				if (bRealTypeCompatibleWithISPC && bChaos_Spherical_ISPC_Enabled)
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
				if (bRealTypeCompatibleWithISPC && bChaos_Spherical_ISPC_Enabled)
				{
					ApplyHelperISPC(Particles, Dt);
				}
				else
				{
					ApplyHelper(Particles, Dt);
				}
			}
		}

		inline void SetSphereRadiiMultiplier(const FReal InSphereRadiiMultiplier)
		{
			SphereRadiiMultiplier = FMath::Max((FReal)0., InSphereRadiiMultiplier);
		}

		inline FReal GetSphereRadiiMultiplier() const
		{
			return SphereRadiiMultiplier;
		}

		inline bool UseLegacyBackstop() const
		{
			return bUseLegacyBackstop;
		}

	private:
		inline void ApplyHelper(FPBDParticles& Particles, const FReal Dt) const
		{
			const int32 ParticleCount = SphereRadii.Num();

			PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile need for parallel loop based on particle count
			{
				const int32 ParticleIndex = ParticleOffset + Index;

				if (Particles.InvM(ParticleIndex) == 0)
				{
					return;
				}

				const FVec3& AnimationPosition = AnimationPositions[Index];
				const FVec3& AnimationNormal = AnimationNormals[Index];

				const FReal SphereOffsetDistance = SphereOffsetDistances[Index];
				const FReal Radius = SphereRadii[Index] * SphereRadiiMultiplier;

				const FVec3 Center = AnimationPosition - (Radius + SphereOffsetDistance) * AnimationNormal;  // Non legacy version adds radius to the distance
				const FVec3 CenterToParticle = Particles.P(ParticleIndex) - Center;
				const FReal DistanceSquared = CenterToParticle.SizeSquared();

				static const FReal DeadZoneSquareRadius = SMALL_NUMBER;
				if (DistanceSquared < DeadZoneSquareRadius)
				{
					Particles.P(ParticleIndex) = AnimationPosition - SphereOffsetDistance * AnimationNormal;  // Non legacy version adds radius to the distance
				}
				else if (DistanceSquared < FMath::Square(Radius))
				{
					const FVec3 PositionOnSphere = (Radius / sqrt(DistanceSquared)) * CenterToParticle;
					Particles.P(ParticleIndex) = Center + PositionOnSphere;
				}
				// Else the particle is outside the sphere, and there is nothing to do
			});
		}

		inline void ApplyLegacyHelper(FPBDParticles& Particles, const FReal Dt) const
		{
			const int32 ParticleCount = SphereRadii.Num();

			PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile need for parallel loop based on particle count
			{
				const int32 ParticleIndex = ParticleOffset + Index;

				if (Particles.InvM(ParticleIndex) == 0)
				{
					return;
				}

				const FVec3& AnimationPosition = AnimationPositions[Index];
				const FVec3& AnimationNormal = AnimationNormals[Index];

				const FReal SphereOffsetDistance = SphereOffsetDistances[Index];
				const FReal Radius = SphereRadii[Index] * SphereRadiiMultiplier;

				const FVec3 Center = AnimationPosition - SphereOffsetDistance * AnimationNormal;  // Legacy version already includes the radius within the distance
				const FVec3 CenterToParticle = Particles.P(ParticleIndex) - Center;
				const FReal DistanceSquared = CenterToParticle.SizeSquared();

				static const FReal DeadZoneSquareRadius = SMALL_NUMBER;
				if (DistanceSquared < DeadZoneSquareRadius)
				{
					Particles.P(ParticleIndex) = AnimationPosition - (SphereOffsetDistance - Radius) * AnimationNormal;  // Legacy version already includes the radius to the distance
				}
				else if (DistanceSquared < FMath::Square(Radius))
				{
					const FVec3 PositionOnSphere = (Radius / sqrt(DistanceSquared)) * CenterToParticle;
					Particles.P(ParticleIndex) = Center + PositionOnSphere;
				}
				// Else the particle is outside the sphere, and there is nothing to do
			});
		}

		void ApplyLegacyHelperISPC(FPBDParticles& Particles, const FReal Dt) const;
		void ApplyHelperISPC(FPBDParticles& Particles, const FReal Dt) const;

	private:
		const TArray<FVec3>& AnimationPositions;  // Positions of spheres, use global indexation (will need adding ParticleOffset)
		const TArray<FVec3>& AnimationNormals; // Sphere offset directions, use global indexation (will need adding ParticleOffset)
		const TConstArrayView<FReal> SphereRadii; // Start at index 0, use local indexation
		const TConstArrayView<FReal> SphereOffsetDistances;  // Sphere position offsets, use local indexation
		const int32 ParticleOffset;
		FReal SphereRadiiMultiplier;
		bool bUseLegacyBackstop;
	};

	template<typename T, int d>
	using TPBDSphericalConstraint UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDSphericalConstraint instead") = FPBDSphericalConstraint;

	template<typename T, int d>
	using TPBDSphericalBackstopConstraint UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDSphericalBackstopConstraint instead") = FPBDSphericalBackstopConstraint;
}
