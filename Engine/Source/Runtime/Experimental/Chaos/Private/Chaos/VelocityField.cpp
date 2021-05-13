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

void FVelocityField::SetGeometry(const FTriangleMesh* TriangleMesh, const TConstArrayView<FRealSingle>& DragMultipliers, const TConstArrayView<FRealSingle>& LiftMultipliers)
{
	if (TriangleMesh)
	{
		PointToTriangleMap = TriangleMesh->GetPointToTriangleMap();
		Elements = TriangleMesh->GetElements();
				
		const TVec2<int32> Range = TriangleMesh->GetVertexRange();
		Offset = Range[0];
		NumParticles = 1 + Range[1] - Offset;

		Forces.SetNumUninitialized(Elements.Num());

		enum class EWeighMaps : uint8
		{
			None = 0,
			DragMultipliers = 1 << 0,
			LiftMultipliers = 1 << 1
		};

		const EWeighMaps WeighMaps = (EWeighMaps)(
			(uint8)(DragMultipliers.Num() == NumParticles ? EWeighMaps::DragMultipliers : EWeighMaps::None) |
			(uint8)(LiftMultipliers.Num() == NumParticles ? EWeighMaps::LiftMultipliers : EWeighMaps::None));

		if (WeighMaps == EWeighMaps::None)
		{
			Multipliers.Reset();
		}
		else
		{
			static const FReal OneThird = (FReal)1. / (FReal)3.;

			Multipliers.SetNumUninitialized(Elements.Num());

			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				const TVec3<int32>& Element = Elements[ElementIndex];
				const int32 I0 = Element[0] - Offset;
				const int32 I1 = Element[1] - Offset;
				const int32 I2 = Element[2] - Offset;

				switch (WeighMaps)
				{
				case EWeighMaps::DragMultipliers:
					Multipliers[ElementIndex] = FVec2((DragMultipliers[I0] + DragMultipliers[I1] + DragMultipliers[I2]) * OneThird, (FReal)0.); break;
					break;
				case EWeighMaps::LiftMultipliers:
					Multipliers[ElementIndex] = FVec2((FReal)0., (LiftMultipliers[I0] + LiftMultipliers[I1] + LiftMultipliers[I2]) * OneThird); break;
					break;
				default:
					Multipliers[ElementIndex] = FVec2(
						DragMultipliers[I0] + DragMultipliers[I1] + DragMultipliers[I2],
						LiftMultipliers[I0] + LiftMultipliers[I1] + LiftMultipliers[I2]) * OneThird;
					break;
				}
			}
		}
	}
	else
	{
		PointToTriangleMap = TArrayView<TArray<int32>>();
		Elements = TArrayView<TVector<int32, 3>>();
		Offset = 0;
		NumParticles = 0;
		Forces.Reset();
		Multipliers.Reset();
		SetProperties(FVec2::ZeroVector, FVec2::ZeroVector, (FReal)0.);
	}
}



void FVelocityField::UpdateForces(const FPBDParticles& InParticles, const FReal /*Dt*/)
{
	if (!Multipliers.Num())
	{
#if INTEL_ISPC
		if (bRealTypeCompatibleWithISPC && bChaos_VelocityField_ISPC_Enabled)
		{
			ispc::UpdateField(
				(ispc::FVector*)Forces.GetData(),
				(const ispc::FIntVector*)Elements.GetData(),
				(const ispc::FVector*)InParticles.GetV().GetData(),
				(const ispc::FVector*)InParticles.XArray().GetData(),
				(const ispc::FVector&)Velocity,
				QuarterRho,
				DragBase,
				LiftBase,
				Elements.Num());
		}
		else
#endif
		{
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				UpdateField(InParticles, ElementIndex, Velocity, DragBase, LiftBase);
			}
		}
	}
	else
	{
#if INTEL_ISPC
		if (bRealTypeCompatibleWithISPC && bChaos_VelocityField_ISPC_Enabled)
		{
			ispc::UpdateFieldWithWeightMaps(
				(ispc::FVector*)Forces.GetData(),
				(const ispc::FIntVector*)Elements.GetData(),
				(const ispc::FVector*)InParticles.GetV().GetData(),
				(const ispc::FVector*)InParticles.XArray().GetData(),
				(const ispc::FVector2*)Multipliers.GetData(),
				(const ispc::FVector&)Velocity,
				QuarterRho,
				DragBase,
				DragRange,
				LiftBase,
				LiftRange,
				Elements.Num());
		}
		else
#endif
		{
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				const FVec2& Multiplier = Multipliers[ElementIndex];
				const FReal Cd = DragBase + DragRange * Multiplier[0];
				const FReal Cl = LiftBase + LiftRange * Multiplier[1];

				UpdateField(InParticles, ElementIndex, Velocity, Cd, Cl);
			}
		}
	}
}
