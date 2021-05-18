// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDAxialSpringConstraintsBase.h"

namespace Chaos
{

class CHAOS_API FPBDAxialSpringConstraints : public FPBDAxialSpringConstraintsBase
{
	typedef FPBDAxialSpringConstraintsBase Base;
	using Base::Barys;
	using Base::Constraints;

public:
	FPBDAxialSpringConstraints(
		const FPBDParticles& Particles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const TArray<TVec3<int32>>& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const FVec2& InStiffness,
		bool bTrimKinematicConstraints)
		: Base(Particles, ParticleOffset, ParticleCount, InConstraints, StiffnessMultipliers, InStiffness, bTrimKinematicConstraints)
	{
		InitColor(Particles);
	}

	virtual ~FPBDAxialSpringConstraints() {}

	void Apply(FPBDParticles& InParticles, const FReal Dt) const;

private:
	void InitColor(const FPBDParticles& InParticles);
	void ApplyHelper(FPBDParticles& InParticles, const FReal Dt, const int32 ConstraintIndex, const FReal ExpStiffnessValue) const;

private:
	TArray<TArray<int32>> ConstraintsPerColor;
};

}

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_AxialSpring_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_AxialSpring_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_AxialSpring_ISPC_Enabled;
#endif
