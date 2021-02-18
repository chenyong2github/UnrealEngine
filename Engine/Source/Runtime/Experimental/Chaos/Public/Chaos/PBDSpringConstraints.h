// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDSpringConstraintsBase.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/GraphColoring.h"

namespace Chaos
{

template<class T, int d>
class TPBDParticles;

template<class T, int d>
class TRigidParticles;

template<class T, int d>
class TPBDRigidParticles;

class CHAOS_API FPBDSpringConstraints : public FPBDSpringConstraintsBase, public FPBDConstraintContainer
{
	typedef FPBDSpringConstraintsBase Base;
	using Base::MConstraints;

  public:
	FPBDSpringConstraints(const FReal Stiffness = (FReal)1)
	    : FPBDSpringConstraintsBase(Stiffness)
	{}

	FPBDSpringConstraints(const FDynamicParticles& InParticles, TArray<TVec2<int32>>&& Constraints, const FReal Stiffness = (FReal)1, bool bStripKinematicConstraints = false)
	    : FPBDSpringConstraintsBase(InParticles, MoveTemp(Constraints), Stiffness, bStripKinematicConstraints)
	{
		InitColor(InParticles);
	}
	FPBDSpringConstraints(const TRigidParticles<FReal, 3>& InParticles, TArray<TVec2<int32>>&& Constraints, const FReal Stiffness = (FReal)1, bool bStripKinematicConstraints = false)
	    : FPBDSpringConstraintsBase(InParticles, MoveTemp(Constraints), Stiffness, bStripKinematicConstraints) 
	{}

	FPBDSpringConstraints(const FDynamicParticles& InParticles, const TArray<TVec3<int32>>& Constraints, const FReal Stiffness = (FReal)1, bool bStripKinematicConstraints = false)
	    : FPBDSpringConstraintsBase(InParticles, Constraints, Stiffness, bStripKinematicConstraints)
	{
		InitColor(InParticles);
	}

	FPBDSpringConstraints(const FDynamicParticles& InParticles, const TArray<TVec4<int32>>& Constraints, const FReal Stiffness = (FReal)1, bool bStripKinematicConstraints = false)
	    : FPBDSpringConstraintsBase(InParticles, Constraints, Stiffness, bStripKinematicConstraints)
	{
		InitColor(InParticles);
	}

	virtual ~FPBDSpringConstraints() {}

	const TArray<TVec2<int32>>& GetConstraints() const { return MConstraints; }
	TArray<TVec2<int32>>& GetConstraints() { return MConstraints; }
	TArray<TVec2<int32>>& Constraints() { return MConstraints; }

	template<class T_PARTICLES>
	void Apply(T_PARTICLES& InParticles, const FReal Dt, const int32 InConstraintIndex) const;
	void Apply(TPBDParticles<FReal, 3>& InParticles, const FReal Dt) const;

	void Apply(TPBDRigidParticles<FReal, 3>& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices) const;

private:
	void InitColor(const FDynamicParticles& InParticles);

	TArray<TArray<int32>> MConstraintsPerColor;
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
