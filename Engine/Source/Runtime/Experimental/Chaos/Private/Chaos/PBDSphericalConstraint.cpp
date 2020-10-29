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
void TPBDSphericalConstraint<T, d>::ApplyHelperISPC(TPBDParticles<T, d>& Particles, const T Dt) const
{
	ApplyHelper(Particles, Dt);
}

template<>
void TPBDSphericalConstraint<float, 3>::ApplyHelperISPC(TPBDParticles<FReal, 3>& Particles, const float Dt) const
{
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

template<typename T, int d>
void TPBDSphericalBackstopConstraint<T, d>::ApplyLegacyHelperISPC(TPBDParticles<T, d>& Particles, const T Dt) const
{
	ApplyLegacyHelper(Particles, Dt);
}

template<>
void TPBDSphericalBackstopConstraint<float, 3>::ApplyLegacyHelperISPC(TPBDParticles<float, 3>& Particles, const float Dt) const
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

template<typename T, int d>
void TPBDSphericalBackstopConstraint<T, d>::ApplyHelperISPC(TPBDParticles<T, d>& Particles, const T Dt) const
{
	ApplyHelper(Particles, Dt);
}

template<>
void TPBDSphericalBackstopConstraint<float, 3>::ApplyHelperISPC(TPBDParticles<float, 3>& Particles, const float Dt) const
{
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

template class Chaos::TPBDSphericalConstraint<float, 3>;
template class Chaos::TPBDSphericalBackstopConstraint<float, 3>;
