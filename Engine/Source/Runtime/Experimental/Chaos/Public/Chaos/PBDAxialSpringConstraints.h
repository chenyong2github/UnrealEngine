// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDAxialSpringConstraintsBase.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PerParticleRule.h"
#include "Chaos/GraphColoring.h"
#include "Chaos/Core.h"
#include "ChaosStats.h"

namespace Chaos
{

class CHAOS_API FPBDAxialSpringConstraints : public FParticleRule, public FPBDAxialSpringConstraintsBase
{
	typedef FPBDAxialSpringConstraintsBase Base;
	using Base::MBarys;
	using Base::MConstraints;

  public:
	FPBDAxialSpringConstraints(const FDynamicParticles& InParticles, TArray<TVector<int32, 3>>&& Constraints, const FReal Stiffness = (FReal)1.)
		: FPBDAxialSpringConstraintsBase(InParticles, MoveTemp(Constraints), Stiffness)
	{
		InitColor(InParticles);
	}
	virtual ~FPBDAxialSpringConstraints() {}

  private:
	void InitColor(const FDynamicParticles& InParticles);
	void ApplyImp(FPBDParticles& InParticles, const FReal Dt, const int32 i) const;
  public:
	void Apply(FPBDParticles& InParticles, const FReal Dt) const override; //-V762

	TArray<TArray<int32>> MConstraintsPerColor;
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
