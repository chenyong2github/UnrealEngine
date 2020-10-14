// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDSphericalConstraint.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Framework/Parallel.h"
#include "ChaosStats.h"
#include "ChaosLog.h"
#if INTEL_ISPC
#include "PBDSphericalConstraint.ispc.generated.h"
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_Spherical_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosSphericalISPCEnabled(TEXT("p.Chaos.Spherical.ISPC"), bChaos_Spherical_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in spherical constraints"));
#endif

using namespace Chaos;

template<typename T, int d>
void TPBDSphericalConstraint<T, d>::Apply(TPBDParticles<T, d>& Particles, const T Dt) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_Spherical);

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

template<>
void TPBDSphericalConstraint<float, 3>::Apply(TPBDParticles<FReal, 3>& Particles, const float Dt) const
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_Spherical);

	const int32 ParticleCount = SphereRadii.Num();

	if (bChaos_Spherical_ISPC_Enabled)
	{
#if INTEL_ISPC
		ispc::ApplySphericalConstraints(
			(ispc::FVector*)Particles.GetP().GetData(),
			(const ispc::FVector*)AnimationPositions.GetData(),
			Particles.GetInvM().GetData(),
			SphereRadii.GetData(),
			SphereRadiiMultiplier,
			ParticleOffset,
			ParticleCount);
#endif
	}
	else
	{
		PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile need for parallel loop based on particle count
		{
			const int32 ParticleIndex = ParticleOffset + Index;

			if (Particles.InvM(ParticleIndex) == 0)
			{
				return;
			}

			const float Radius = SphereRadii[Index] * SphereRadiiMultiplier;
			const TVector<float, 3>& Center = AnimationPositions[ParticleIndex];

			const TVector<float, 3> CenterToParticle = Particles.P(ParticleIndex) - Center;
			const float DistanceSquared = CenterToParticle.SizeSquared();

			static const float DeadZoneSquareRadius = SMALL_NUMBER; // We will not push the particle away in the dead zone
			if (DistanceSquared > FMath::Square(Radius) + DeadZoneSquareRadius)
			{
				const float Distance = sqrt(DistanceSquared);
				const TVector<float, 3> PositionOnSphere = (Radius / Distance) * CenterToParticle;
				Particles.P(ParticleIndex) = Center + PositionOnSphere;
			}
		});
	}
}

template<typename T, int d>
void TPBDSphericalBackstopConstraint<T, d>::ApplyLegacyHelperISPC(TPBDParticles<T, d>& Particles, const T Dt) const
{
	return ApplyLegacyHelper(Particles, Dt);
}

template<>
void TPBDSphericalBackstopConstraint<float, 3>::ApplyLegacyHelperISPC(TPBDParticles<float, 3>& Particles, const float Dt) const
{
	if (bChaos_Spherical_ISPC_Enabled)
	{
		const int32 ParticleCount = SphereRadii.Num();

#if INTEL_ISPC
		ispc::ApplyLegacySphericalBackstopConstraints(
			(ispc::FVector*)Particles.GetP().GetData(),
			(const ispc::FVector*)AnimationPositions.GetData(),
			(const ispc::FVector*)AnimationNormals.GetData(),
			Particles.GetInvM().GetData(),
			SphereOffsetDistances.GetData(),
			SphereRadii.GetData(),
			SphereRadiiMultiplier,
			ParticleOffset,
			ParticleCount);
#endif
	}
	else
	{
		return ApplyLegacyHelper(Particles, Dt);
	}
}

template class Chaos::TPBDSphericalConstraint<float, 3>;
template class Chaos::TPBDSphericalBackstopConstraint<float, 3>;
