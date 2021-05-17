// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSpringConstraintsBase.h"

namespace Chaos
{

class CHAOS_API FPBDSpringConstraints : public FPBDSpringConstraintsBase
{
	typedef FPBDSpringConstraintsBase Base;
	using Base::Constraints;
	using Base::Stiffness;

public:
	template<int32 Valence>
	FPBDSpringConstraints(
		const FPBDParticles& Particles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const TArray<TVector<int32, Valence>>& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const FVec2& InStiffness,
		bool bTrimKinematicConstraints = false,
		typename TEnableIf<Valence >= 2 && Valence <= 4>::Type* = nullptr)
		: Base(Particles, ParticleOffset, ParticleCount, InConstraints, StiffnessMultipliers, InStiffness, bTrimKinematicConstraints)
	{
		InitColor(Particles);
	}

	virtual ~FPBDSpringConstraints() {}

	void Apply(FPBDParticles& Particles, const FReal Dt) const;

private:
	void InitColor(const FPBDParticles& InParticles);
	void ApplyHelper(FPBDParticles& Particles, const FReal Dt, const int32 ConstraintIndex, const FReal ExpStiffnessValue) const;

private:
	TArray<TArray<int32>> ConstraintsPerColor;
};

}

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_Spring_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_Spring_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_Spring_ISPC_Enabled;
#endif
