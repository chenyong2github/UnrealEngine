// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/VelocityField.h"
#include "HAL/IConsoleManager.h"
#if INTEL_ISPC
#include "VelocityField.ispc.generated.h"

static_assert(sizeof(ispc::FVector3f) == sizeof(Chaos::Softs::FSolverVec3), "sizeof(ispc::FVector3f) != sizeof(Chaos::Softs::FSolverVec3)");
static_assert(sizeof(ispc::FIntVector) == sizeof(Chaos::TVec3<int32>), "sizeof(ispc::FIntVector) != sizeof(Chaos::TVec3<int32>)");
static_assert(sizeof(ispc::FVector2f) == sizeof(Chaos::Softs::FSolverVec2), "sizeof(ispc::FVector2f) != sizeof(Chaos::Softs::FSolverVec2)");
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_VelocityField_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosVelocityFieldISPCEnabled(TEXT("p.Chaos.VelocityField.ISPC"), bChaos_VelocityField_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in velocity field calculations"));
#endif

namespace Chaos::Softs {

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
			constexpr FSolverReal OneThird = (FSolverReal)1. / (FSolverReal)3.;

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
					Multipliers[ElementIndex] = FSolverVec2((FSolverReal)(DragMultipliers[I0] + DragMultipliers[I1] + DragMultipliers[I2]) * OneThird, (FSolverReal)0.); break;
					break;
				case EWeighMaps::LiftMultipliers:
					Multipliers[ElementIndex] = FSolverVec2((FSolverReal)0., (FSolverReal)(LiftMultipliers[I0] + LiftMultipliers[I1] + LiftMultipliers[I2]) * OneThird); break;
					break;
				default:
					Multipliers[ElementIndex] = FSolverVec2(
						(FSolverReal)(DragMultipliers[I0] + DragMultipliers[I1] + DragMultipliers[I2]) * OneThird,
						(FSolverReal)(LiftMultipliers[I0] + LiftMultipliers[I1] + LiftMultipliers[I2]) * OneThird);
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
		SetProperties(FSolverVec2(0.), FSolverVec2(0.), (FSolverReal)0.);
	}
}



void FVelocityField::UpdateForces(const FSolverParticles& InParticles, const FSolverReal /*Dt*/)
{
	if (!Multipliers.Num())
	{
#if INTEL_ISPC
		if (bRealTypeCompatibleWithISPC && bChaos_VelocityField_ISPC_Enabled)
		{
			ispc::UpdateField(
				(ispc::FVector3f*)Forces.GetData(),
				(const ispc::FIntVector*)Elements.GetData(),
				(const ispc::FVector3f*)InParticles.GetV().GetData(),
				(const ispc::FVector3f*)InParticles.XArray().GetData(),
				(const ispc::FVector3f&)Velocity,
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
				(ispc::FVector3f*)Forces.GetData(),
				(const ispc::FIntVector*)Elements.GetData(),
				(const ispc::FVector3f*)InParticles.GetV().GetData(),
				(const ispc::FVector3f*)InParticles.XArray().GetData(),
				(const ispc::FVector2f*)Multipliers.GetData(),
				(const ispc::FVector3f&)Velocity,
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
				const FSolverVec2& Multiplier = Multipliers[ElementIndex];
				const FSolverReal Cd = DragBase + DragRange * Multiplier[0];
				const FSolverReal Cl = LiftBase + LiftRange * Multiplier[1];

				UpdateField(InParticles, ElementIndex, Velocity, Cd, Cl);
			}
		}
	}
}

}  // End namespace Chaos::Softs
