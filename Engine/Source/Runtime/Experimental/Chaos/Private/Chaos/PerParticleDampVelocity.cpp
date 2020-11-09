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

template<class T, int d>
template<class T_PARTICLES>
void TPerParticleDampVelocity<T, d>::UpdatePositionBasedState(const T_PARTICLES& Particles, const int32 Offset, const int32 Range)
{
	static_assert(d == 3, "Damp Velocities currently only supports 3D vectors.");

	MXcm = TVector<T, d>(0);
	MVcm = TVector<T, d>(0);
	T Mcm = (T)0;

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

	if (Mcm != 0.0f)
	{
		MXcm /= Mcm;
		MVcm /= Mcm;
	}

	TVector<T, d> L = TVector<T, d>(0);
	PMatrix<T, d, d> I(0);
	for (int32 Index = Offset; Index < Range; ++Index)
	{
		if (!Particles.InvM(Index))
		{
			continue;
		}
		const TVector<T, d> V = Particles.X(Index) - MXcm;
		L += TVector<T, d>::CrossProduct(V, Particles.M(Index) * Particles.V(Index));
		const PMatrix<T, d, d> M(0, V[2], -V[1], -V[2], 0, V[0], V[1], -V[0], 0);
		I += M.GetTransposed() * M * Particles.M(Index);
	}

#if COMPILE_WITHOUT_UNREAL_SUPPORT
	MOmega = I.Determinant() > 1e-7 ? TRigidTransform<T, d>(I).InverseTransformVector(L) : TVector<T, d>(0);
#else
	const T Det = I.Determinant();
	MOmega = Det < SMALL_NUMBER || !FGenericPlatformMath::IsFinite(Det) ?
		TVector<T, d>(0) :
		I.InverseTransformVector(L); // Calls FMatrix::InverseFast(), which tests against SMALL_NUMBER
#endif
}

template<>
template<>
void TPerParticleDampVelocity<float, 3>::UpdatePositionBasedState(const TPBDParticles<float, 3>& Particles, const int32 Offset, const int32 Range)
{
	if (bChaos_DampVelocity_ISPC_Enabled)
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
		MXcm = TVector<float, 3>(0.f);
		MVcm = TVector<float, 3>(0.f);
		float Mcm = 0.f;

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

		if (Mcm != 0.f)
		{
			MXcm /= Mcm;
			MVcm /= Mcm;
		}

		TVector<float, 3> L = TVector<float, 3>(0.f);
		PMatrix<float, 3, 3> I(0.f);
		for (int32 Index = Offset; Index < Range; ++Index)
		{
			if (!Particles.InvM(Index))
			{
				continue;
			}
			const TVector<float, 3> V = Particles.X(Index) - MXcm;
			L += TVector<float, 3>::CrossProduct(V, Particles.M(Index) * Particles.V(Index));
			const PMatrix<float, 3, 3> M(0.f, V[2], -V[1], -V[2], 0.f, V[0], V[1], -V[0], 0.f);
			I += M.GetTransposed() * M * Particles.M(Index);
		}

#if COMPILE_WITHOUT_UNREAL_SUPPORT
		MOmega = I.Determinant() > 1e-7 ? TRigidTransform<float, 3>(I).InverseTransformVector(L) : TVector<float, 3>(0);
#else
		const float Det = I.Determinant();
		MOmega = Det < SMALL_NUMBER || !FGenericPlatformMath::IsFinite(Det) ?
			TVector<float, 3>(0.f) :
			I.InverseTransformVector(L); // Calls FMatrix::InverseFast(), which tests against SMALL_NUMBER
#endif
	}
}

template class Chaos::TPerParticleDampVelocity<float, 3>;
