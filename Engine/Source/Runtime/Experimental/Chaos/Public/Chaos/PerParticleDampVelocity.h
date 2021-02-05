// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Matrix.h"
#include "Chaos/PerParticleRule.h"
#include "GenericPlatform/GenericPlatformMath.h"

namespace Chaos
{
	class FPerParticleDampVelocity : public TPerParticleRule<FReal, 3>
	{
	public:
		FPerParticleDampVelocity(const FReal Coefficient = (FReal)0.01)
		    : MCoefficient(Coefficient)
		{
		}
		virtual ~FPerParticleDampVelocity() {}

		template<class T_PARTICLES>
		void UpdatePositionBasedState(const T_PARTICLES& Particles, const int32 Offset, const int32 Range);

		template<class T_PARTICLES>
		UE_DEPRECATED(4.26, "Use offset range version instead")
		inline void UpdatePositionBasedState(const T_PARTICLES& InParticles, const TArray<int32>& InActiveIndices)
		{
			MXcm = FVec3(0, 0, 0);
			MVcm = FVec3(0, 0, 0);
			FReal Mcm = (FReal)0;
			for (const int32 Index : InActiveIndices)
			{
				if (!InParticles.InvM(Index))
				{
					continue;
				}
				MXcm += InParticles.X(Index) * InParticles.M(Index);
				MVcm += InParticles.V(Index) * InParticles.M(Index);
				Mcm += InParticles.M(Index);
			}

			if (Mcm != 0.0f)
			{
				MXcm /= Mcm;
				MVcm /= Mcm;
			}

			FVec3 L = FVec3(0, 0, 0);
			FMatrix33 I(0);
			for (const int32 Index : InActiveIndices)
			{
				if (!InParticles.InvM(Index))
				{
					continue;
				}
				FVec3 V = InParticles.X(Index) - MXcm;
				L += FVec3::CrossProduct(V, InParticles.M(Index) * InParticles.V(Index));
				FMatrix33 M(0, V[2], -V[1], -V[2], 0, V[0], V[1], -V[0], 0);
				I += M.GetTransposed() * M * InParticles.M(Index);
			}
#if COMPILE_WITHOUT_UNREAL_SUPPORT
			MOmega = I.Determinant() > 1e-7 ? FRigidTransform3(I).InverseTransformVector(L) : FVec3(0);
#else
			const FReal Det = I.Determinant();
			MOmega = Det < SMALL_NUMBER || !FGenericPlatformMath::IsFinite(Det) ?
			    FVec3(0) :
			    I.InverseTransformVector(L); // Calls FMatrix::InverseFast(), which tests against SMALL_NUMBER
#endif
		}

		template<class T_PARTICLES>
		inline void UpdatePositionBasedState(const T_PARTICLES& InParticles)
		{
			if (ActiveIndices.Num() != InParticles.Size())
			{
				if ((uint32)ActiveIndices.Num() < InParticles.Size())
				{
					uint32 CurrNum = ActiveIndices.Num();
					ActiveIndices.AddUninitialized(InParticles.Size() - CurrNum);
					for (; CurrNum < InParticles.Size(); ++CurrNum)
					{
						ActiveIndices[CurrNum] = CurrNum;
					}
				}
				else if ((uint32)ActiveIndices.Num() > InParticles.Size())
				{
					ActiveIndices.SetNum(InParticles.Size());
				}
			}

			UpdatePositionBasedState(InParticles, ActiveIndices);
		}
		inline void Apply(TDynamicParticles<FReal, 3>& InParticles, const FReal Dt, const int32 Index) const override //-V762
		{
			if (InParticles.InvM(Index) == 0)
			{
				return; // Do not damp kinematic particles
			}
			ApplyFast(InParticles, Dt, Index);
		}

		inline void Apply(TRigidParticles<FReal, 3>& InParticles, const FReal Dt, const int32 Index) const override //-V762
		{
			if (InParticles.InvM(Index) == 0)
			{
				return; // Do not damp kinematic rigid bodies
			}
			ApplyFast(InParticles, Dt, Index);
		}

		// Apply damping without first checking for kinematic particles
		template<class T_PARTICLES>
		inline void ApplyFast(T_PARTICLES& Particles, const FReal /*Dt*/, const int32 Index) const
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
