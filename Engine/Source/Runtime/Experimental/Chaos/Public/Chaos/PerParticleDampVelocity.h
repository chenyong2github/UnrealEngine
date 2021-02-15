// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Matrix.h"
#include "Chaos/PerParticleRule.h"
#include "GenericPlatform/GenericPlatformMath.h"

namespace Chaos
{
	class FPerParticleDampVelocity : public FPerParticleRule
	{
	public:
		FPerParticleDampVelocity(const FReal Coefficient = (FReal)0.01)
		    : MCoefficient(Coefficient)
		{
		}
		virtual ~FPerParticleDampVelocity() {}

		void UpdatePositionBasedState(const FPBDParticles& Particles, const int32 Offset, const int32 Range);

		// Apply damping without first checking for kinematic particles
		inline void ApplyFast(FPBDParticles& Particles, const FReal /*Dt*/, const int32 Index) const
		{
			const FVec3 R = Particles.X(Index) - MXcm;
			const FVec3 Dv = MVcm - Particles.V(Index) + FVec3::CrossProduct(R, MOmega);
			Particles.V(Index) += MCoefficient * Dv;
		}

	private:
		FReal MCoefficient;
		FVec3 MXcm, MVcm, MOmega;
		TArray<int32> ActiveIndices;
	};
}

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_DampVelocity_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_DampVelocity_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_DampVelocity_ISPC_Enabled;
#endif
