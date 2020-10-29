// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/VelocityField.h"
#include "HAL/IConsoleManager.h"
#if INTEL_ISPC
#include "VelocityField.ispc.generated.h"
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_VelocityField_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosVelocityFieldISPCEnabled(TEXT("p.Chaos.VelocityField.ISPC"), bChaos_VelocityField_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in velocity field calculations"));
#endif

using namespace Chaos;

template<class T, int d>
void TVelocityField<T, d>::UpdateForces(const TPBDParticles<T, d>& InParticles, const T /*Dt*/)
{
	if (!GetVelocity)
	{
		for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
		{
			UpdateField(InParticles, ElementIndex, Velocity);
		}
	}
	else
	{
		for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
		{
			const TVector<int32, 3>& Element = Elements[ElementIndex];

			// Get the triangle's position
			const TVector<T, d>& SurfacePosition = (T)(1. / 3.) * (
				InParticles.X(Element[0]) +
				InParticles.X(Element[1]) +
				InParticles.X(Element[2]));

			UpdateField(InParticles, ElementIndex, GetVelocity(SurfacePosition));
		}
	}
}

template<>
void TVelocityField<float, 3>::UpdateForces(const TPBDParticles<float, 3>& InParticles, const float /*Dt*/)
{
	if (!GetVelocity)
	{
		if (bChaos_VelocityField_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::UpdateField(
				(ispc::FVector*)Forces.GetData(),
				(const ispc::FIntVector*)Elements.GetData(),
				(const ispc::FVector*)InParticles.GetV().GetData(),
				(const ispc::FVector*)InParticles.XArray().GetData(),
				(const ispc::FVector&)Velocity,
				QuarterRho,
				Cd,
				Cl,
				Elements.Num());
#endif
		}
		else
		{
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				UpdateField(InParticles, ElementIndex, Velocity);
			}
		}
	}
	else
	{
		for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
		{
			const TVector<int32, 3>& Element = Elements[ElementIndex];

			// Get the triangle's position
			const TVector<float, 3>& SurfacePosition = (1.f / 3.f) * (
				InParticles.X(Element[0]) +
				InParticles.X(Element[1]) +
				InParticles.X(Element[2]));

			UpdateField(InParticles, ElementIndex, GetVelocity(SurfacePosition));
		}
	}
}

template class Chaos::TVelocityField<float, 3>;
