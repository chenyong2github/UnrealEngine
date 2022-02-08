// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDAxialSpringConstraintsBase.h"

namespace Chaos::Softs
{

class CHAOS_API FPBDAxialSpringConstraints : public FPBDAxialSpringConstraintsBase
{
	typedef FPBDAxialSpringConstraintsBase Base;
	using Base::Barys;
	using Base::Constraints;

public:
	FPBDAxialSpringConstraints(
		const FSolverParticles& Particles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const TArray<TVec3<int32>>& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const FSolverVec2& InStiffness,
		bool bTrimKinematicConstraints)
		: Base(Particles, ParticleOffset, ParticleCount, InConstraints, StiffnessMultipliers, InStiffness, bTrimKinematicConstraints)
	{
		InitColor(Particles);
	}

	virtual ~FPBDAxialSpringConstraints() override {}

	void Apply(FSolverParticles& InParticles, const FSolverReal Dt) const;

private:
	void InitColor(const FSolverParticles& InParticles);
	void ApplyHelper(FSolverParticles& InParticles, const FSolverReal Dt, const int32 ConstraintIndex, const FSolverReal ExpStiffnessValue) const;

private:
	TArray<int32> ConstraintsPerColorStartIndex; // Constraints are ordered so each batch is contiguous. This is ColorNum + 1 length so it can be used as start and end.
};

}  // End namespace Chaos::Softs

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_AxialSpring_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_AxialSpring_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_AxialSpring_ISPC_Enabled;
#endif
