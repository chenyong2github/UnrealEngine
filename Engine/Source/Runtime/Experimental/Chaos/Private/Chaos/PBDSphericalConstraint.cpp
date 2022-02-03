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

static_assert(sizeof(ispc::FVector3f) == sizeof(Chaos::Softs::FSolverVec3), "sizeof(ispc::FVector3f) != sizeof(Chaos::Softs::FSolverVec3)");
#endif

namespace Chaos::Softs {

void FPBDSphericalConstraint::ApplyHelperISPC(FSolverParticles& Particles, const FSolverReal /*Dt*/) const
{
	check(bRealTypeCompatibleWithISPC);

	const int32 ParticleCount = SphereRadii.Num();

#if INTEL_ISPC
	ispc::ApplySphericalConstraints(
		(ispc::FVector3f*)Particles.GetP().GetData(),
		(const ispc::FVector3f*)AnimationPositions.GetData(),
		Particles.GetInvM().GetData(),
		SphereRadii.GetData(),
		SphereRadiiMultiplier,
		ParticleOffset,
		ParticleCount);
#endif
}
void FPBDSphericalBackstopConstraint::ApplyLegacyHelperISPC(FSolverParticles& Particles, const FSolverReal /*Dt*/) const
{
	check(bRealTypeCompatibleWithISPC);

	const int32 ParticleCount = SphereRadii.Num();

#if INTEL_ISPC
	ispc::ApplyLegacySphericalBackstopConstraints(
		(ispc::FVector3f*)Particles.GetP().GetData(),
		(const ispc::FVector3f*)AnimationPositions.GetData(),
		(const ispc::FVector3f*)AnimationNormals.GetData(),
		Particles.GetInvM().GetData(),
		SphereOffsetDistances.GetData(),
		SphereRadii.GetData(),
		SphereRadiiMultiplier,
		ParticleOffset,
		ParticleCount);
#endif
}

void FPBDSphericalBackstopConstraint::ApplyHelperISPC(FSolverParticles& Particles, const FSolverReal /*Dt*/) const
{
	check(bRealTypeCompatibleWithISPC);
	const int32 ParticleCount = SphereRadii.Num();

#if INTEL_ISPC
	ispc::ApplySphericalBackstopConstraints(
		(ispc::FVector3f*)Particles.GetP().GetData(),
		(const ispc::FVector3f*)AnimationPositions.GetData(),
		(const ispc::FVector3f*)AnimationNormals.GetData(),
		Particles.GetInvM().GetData(),
		SphereOffsetDistances.GetData(),
		SphereRadii.GetData(),
		SphereRadiiMultiplier,
		ParticleOffset,
		ParticleCount);
#endif
}

}  // End namespace Chaos::Softs
