// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PerParticleDampVelocity.h"

#if INTEL_ISPC
#include "PerParticleDampVelocity.ispc.generated.h"
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_DampVelocity_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosDampVelocityISPCEnabled(TEXT("p.Chaos.DampVelocity.ISPC"), bChaos_DampVelocity_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in per particle damp velocity calculation"));
#endif

using namespace Chaos;

void FPerParticleDampVelocity::UpdatePositionBasedState(const FPBDParticles& Particles, const int32 Offset, const int32 Range)
{
	if (bRealTypeCompatibleWithISPC && bChaos_DampVelocity_ISPC_Enabled)
	{
#if INTEL_ISPC
		ispc::UpdatePositionBasedState(
			(ispc::FVector&)MXcm,
			(ispc::FVector&)MVcm,
			(ispc::FVector&)MOmega,
			(const ispc::FVector*)Particles.XArray().GetData(),
			(const ispc::FVector*)Particles.GetV().GetData(),
			Particles.GetM().GetData(),
			Particles.GetInvM().GetData(),
			Offset,
			Range);
#endif
	}
	else
	{
		MXcm = FVec3(0);
		MVcm = FVec3(0);
		FReal Mcm = (FReal)0;

		for (int32 Index = Offset; Index < Range; ++Index)
		{
			if (!Particles.InvM(Index))
			{
				continue;
			}
			MXcm += Particles.X(Index) * Particles.M(Index);
			MVcm += Particles.V(Index) * Particles.M(Index);
			Mcm += Particles.M(Index);
		}

		if (Mcm != (FReal)0.0)
		{
			MXcm /= Mcm;
			MVcm /= Mcm;
		}

		FVec3 L = FVec3(0);
		FMatrix33 I(0);
		for (int32 Index = Offset; Index < Range; ++Index)
		{
			if (!Particles.InvM(Index))
			{
				continue;
			}
			const FVec3 V = Particles.X(Index) - MXcm;
			L += FVec3::CrossProduct(V, Particles.M(Index) * Particles.V(Index));
			const FMatrix33 M(0, V[2], -V[1], -V[2], 0, V[0], V[1], -V[0], 0);
			I += M.GetTransposed() * M * Particles.M(Index);
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
}