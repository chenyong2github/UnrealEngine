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

class CHAOS_API FPBDAxialSpringConstraints : public TParticleRule<FReal, 3>, public TPBDAxialSpringConstraintsBase<FReal, 3>
{
	typedef TPBDAxialSpringConstraintsBase<FReal, 3> Base;
	using Base::MBarys;
	using Base::MConstraints;

  public:
	FPBDAxialSpringConstraints(const TDynamicParticles<FReal, 3>& InParticles, TArray<TVector<int32, 3>>&& Constraints, const FReal Stiffness = (FReal)1)
	    : TPBDAxialSpringConstraintsBase<FReal, 3>(InParticles, MoveTemp(Constraints), Stiffness)
	{
		InitColor(InParticles);
	}
	virtual ~FPBDAxialSpringConstraints() {}

  private:
	void InitColor(const TDynamicParticles<FReal, 3>& InParticles);
	void ApplyImp(TPBDParticles<FReal, 3>& InParticles, const FReal Dt, const int32 i) const;
  public:
	void Apply(TPBDParticles<FReal, 3>& InParticles, const FReal Dt) const override; //-V762

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
