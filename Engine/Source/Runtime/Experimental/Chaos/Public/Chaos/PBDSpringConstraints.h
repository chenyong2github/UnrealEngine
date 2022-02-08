// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSpringConstraintsBase.h"

namespace Chaos::Softs
{

class CHAOS_API FPBDSpringConstraints : public FPBDSpringConstraintsBase
{
	typedef FPBDSpringConstraintsBase Base;
	using Base::Constraints;
	using Base::Stiffness;

public:
	template<int32 Valence>
	FPBDSpringConstraints(
		const FSolverParticles& Particles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const TArray<TVector<int32, Valence>>& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const FSolverVec2& InStiffness,
		bool bTrimKinematicConstraints = false,
		typename TEnableIf<Valence >= 2 && Valence <= 4>::Type* = nullptr)
		: Base(Particles, ParticleOffset, ParticleCount, InConstraints, StiffnessMultipliers, InStiffness, bTrimKinematicConstraints)
	{
		InitColor(Particles);
	}

	virtual ~FPBDSpringConstraints() override {}

	void Apply(FSolverParticles& Particles, const FSolverReal Dt) const;

private:
	void InitColor(const FSolverParticles& InParticles);
	void ApplyHelper(FSolverParticles& Particles, const FSolverReal Dt, const int32 ConstraintIndex, const FSolverReal ExpStiffnessValue) const;

private:
	TArray<int32> ConstraintsPerColorStartIndex; // Constraints are ordered so each batch is contiguous. This is ColorNum + 1 length so it can be used as start and end.
};

}  // End namespace Chaos::Softs

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_Spring_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_Spring_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_Spring_ISPC_Enabled;
#endif
