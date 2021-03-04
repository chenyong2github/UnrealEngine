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

void FPBDSphericalConstraint::ApplyHelperISPC(FPBDParticles& Particles, const FReal Dt) const
{
	check(bRealTypeCompatibleWithISPC);

	const int32 ParticleCount = SphereRadii.Num();

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
void FPBDSphericalBackstopConstraint::ApplyLegacyHelperISPC(FPBDParticles& Particles, const FReal Dt) const
{
	check(bRealTypeCompatibleWithISPC);

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

void FPBDSphericalBackstopConstraint::ApplyHelperISPC(FPBDParticles& Particles, const FReal Dt) const
{
	check(bRealTypeCompatibleWithISPC);
	const int32 ParticleCount = SphereRadii.Num();

#if INTEL_ISPC
	ispc::ApplySphericalBackstopConstraints(
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
