// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ParticleRule.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Spherical Constraints"), STAT_PBD_Spherical, STATGROUP_Chaos);

namespace Chaos
{

template<typename T, int d>
class PBDSphericalConstraint : public TParticleRule<T, d>
{
public:
	PBDSphericalConstraint(
		const uint32 InFirstParticleIndex
		, const uint32 InParticleCount
		, bool bInside
		, const TArray<TVector<T, d>>* const InSpherePositions
		, const TArray<T>* const InSphereRadii
		, const TArray <T>* const InSphereOffsetDistances = nullptr
		, const TArray<TVector<T, d>>* const InSphereOffsetDirections = nullptr
	)
		: FirstParticleIndex(InFirstParticleIndex), ParticleCount(InParticleCount)
		, bConstraintInsideOfSphere(bInside)
		, SpherePositions(InSpherePositions)
		, SphereRadii(InSphereRadii)
		, SphereOffsetDistances(InSphereOffsetDistances)
		, SphereOffsetDirections(InSphereOffsetDirections)
	{
		check(SphereRadii);
		check(SpherePositions);
	}
	virtual ~PBDSphericalConstraint() {}

	inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const override
	{
		SCOPE_CYCLE_COUNTER(STAT_PBD_Spherical);
		const T CenterDeadZoneRadius = KINDA_SMALL_NUMBER; // We will not push the particle away in the dead zone

		PhysicsParallelFor(ParticleCount, [&](int32 Index)
		{
			if (InParticles.InvM(FirstParticleIndex + Index) == 0)
			{
				return;
			}
			
			const T Radius = (*SphereRadii)[Index];
			TVector<T, d> Centre = (*SpherePositions)[Index + FirstParticleIndex];
			if (SphereOffsetDistances && SphereOffsetDirections)
			{
				Centre -= (*SphereOffsetDistances)[Index] * (*SphereOffsetDirections)[Index + FirstParticleIndex];
			}

			const TVector<T, d> CenterToParticle = InParticles.P(FirstParticleIndex + Index) - Centre;
			const T DistanceSquared = CenterToParticle.SizeSquared();
			const bool bPushParticleToShpericalBoundery = ((DistanceSquared < Radius * Radius) ^ bConstraintInsideOfSphere) && DistanceSquared > CenterDeadZoneRadius * CenterDeadZoneRadius;

			if (bPushParticleToShpericalBoundery)
			{
				const T Distance = sqrt(DistanceSquared);
				const TVector<T, d> PositionOnSphere = (Radius / Distance) * (CenterToParticle);
				InParticles.P(FirstParticleIndex + Index) = Centre + PositionOnSphere;
			}
		});
	}

private:	
	const uint32 FirstParticleIndex; // TODO: Get rid of this member variable (this is specific to a temporary cloth instancing implementation)
	const uint32 ParticleCount;
	const bool bConstraintInsideOfSphere; // Should the points be constrained to the inside of their respective spheres or the outsides

	const TArray<TVector<T, d>>* const SpherePositions;  // Positions of spheres without adding an offset vector // Start at index FirstParticleIndex
	const TArray<T>* const SphereRadii; // Start at index 0

	const TArray<T>* const SphereOffsetDistances;  // Optional Sphere position offsets // start at index 0
	const TArray<TVector<T, d>>* const SphereOffsetDirections; //	Optional Sphere offset directions (TBD: This is really implementation specific to backstop constraints, consider creating a specialized constraint instead).
};

}
