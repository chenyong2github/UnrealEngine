// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyRiverActor.h"
#include "WaterSplineComponent.h"
#include "WaterSubsystem.h"
#include "WaterUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMesh.h"
#include "Algo/ForEach.h"

#if WITH_CHAOS
// for working around Chaos issue
#include "Chaos/Particles.h"
#include "Chaos/Convex.h"
#endif

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

AWaterBodyRiver::AWaterBodyRiver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyType = GetWaterBodyType();

#if WITH_EDITOR
	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterBodyRiverSprite"));
#endif

	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(!IsFlatSurface());
	check(!IsWaterSplineClosedLoop());
	check(!IsHeightOffsetSupported());
}

void AWaterBodyRiver::InitializeBody()
{
	if (!RiverGenerator)
	{
		RiverGenerator = NewObject<URiverGenerator>(this, TEXT("RiverGenerator"));
	}
}

TArray<UPrimitiveComponent*> AWaterBodyRiver::GetCollisionComponents() const
{
	if (RiverGenerator)
	{
		return RiverGenerator->GetCollisionComponents();
	}
	return Super::GetCollisionComponents();
}

void AWaterBodyRiver::UpdateWaterBody(bool bWithExclusionVolumes)
{
	if (RiverGenerator)
	{
		RiverGenerator->UpdateBody(bWithExclusionVolumes);
	}
}

void AWaterBodyRiver::UpdateMaterialInstances()
{
	Super::UpdateMaterialInstances();

	CreateOrUpdateLakeTransitionMID();
	CreateOrUpdateOceanTransitionMID();
}

void AWaterBodyRiver::SetLakeTransitionMaterial(UMaterialInterface* InMaterial)
{
	LakeTransitionMaterial = InMaterial;
	UpdateMaterialInstances();
}

void AWaterBodyRiver::SetOceanTransitionMaterial(UMaterialInterface* InMaterial)
{
	OceanTransitionMaterial = InMaterial;
	UpdateMaterialInstances();
}

UMaterialInstanceDynamic* AWaterBodyRiver::GetRiverToLakeTransitionMaterialInstance()
{
	CreateOrUpdateLakeTransitionMID();
	return LakeTransitionMID;
}

UMaterialInstanceDynamic* AWaterBodyRiver::GetRiverToOceanTransitionMaterialInstance()
{
	CreateOrUpdateOceanTransitionMID();
	return OceanTransitionMID;
}

#if WITH_EDITOR
TArray<UPrimitiveComponent*> AWaterBodyRiver::GetBrushRenderableComponents() const
{
	TArray<UPrimitiveComponent*> BrushRenderableComponents;
	if (RiverGenerator)
	{
		const TArray<USplineMeshComponent*>& SplineMeshComponents = RiverGenerator->GetSplineMeshComponents();
		BrushRenderableComponents.Reserve(SplineMeshComponents.Num());
		Algo::ForEach(SplineMeshComponents, [&BrushRenderableComponents](USplineMeshComponent* Comp)
		{
			if (Comp != nullptr)
			{
				BrushRenderableComponents.Add(StaticCast<UPrimitiveComponent*>(Comp));
			}
		});
	}
	return BrushRenderableComponents;
}
#endif //WITH_EDITOR

void AWaterBodyRiver::CreateOrUpdateLakeTransitionMID()
{
	if (GetWorld())
	{
		LakeTransitionMID = FWaterUtils::GetOrCreateTransientMID(LakeTransitionMID, TEXT("LakeTransitionMID"), LakeTransitionMaterial, GetTransientMIDFlags());

		SetDynamicParametersOnMID(LakeTransitionMID);
	}
}

void AWaterBodyRiver::CreateOrUpdateOceanTransitionMID()
{
	if (GetWorld())
	{
		OceanTransitionMID = FWaterUtils::GetOrCreateTransientMID(OceanTransitionMID, TEXT("OceanTransitionMID"), OceanTransitionMaterial, GetTransientMIDFlags());

		SetDynamicParametersOnMID(OceanTransitionMID);
	}
}

#if WITH_EDITOR
void AWaterBodyRiver::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged)
{
	Super::OnPostEditChangeProperty(PropertyChangedEvent, bShapeOrPositionChanged, bWeightmapSettingsChanged);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBodyRiver, LakeTransitionMaterial) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBodyRiver, OceanTransitionMaterial))
	{
		UpdateMaterialInstances();
	}
}
#endif

// ----------------------------------------------------------------------------------

URiverGenerator::URiverGenerator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URiverGenerator::Reset()
{
	for (USplineMeshComponent* Comp : SplineMeshComponents)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	SplineMeshComponents.Empty();
}

void URiverGenerator::OnUpdateBody(bool bWithExclusionVolumes)
{
	AWaterBody* OwnerBody = GetOuterAWaterBody();
	UWaterSplineComponent* WaterSplineComp = OwnerBody->GetWaterSpline();
	if (WaterSplineComp)
	{
		const int32 NumSplinePoints = WaterSplineComp->GetNumberOfSplinePoints();
		const int32 NumberOfMeshComponentsNeeded = WaterSplineComp->IsClosedLoop() ? NumSplinePoints : NumSplinePoints - 1;
		if (SplineMeshComponents.Num() != NumberOfMeshComponentsNeeded)
		{
			GenerateMeshes();
		}
		else
		{
			for (int32 SplinePtIndex = 0; SplinePtIndex < NumberOfMeshComponentsNeeded; ++SplinePtIndex)
			{
				USplineMeshComponent* MeshComp = SplineMeshComponents[SplinePtIndex];

				// Validate all mesh components.  Blueprints can null these out somehow
				if (MeshComp)
				{
					UpdateSplineMesh(MeshComp, SplinePtIndex);
					MeshComp->MarkRenderStateDirty();
				}
				else
				{
					GenerateMeshes();
					break;
				}
			}
		}
	}
}

TArray<UPrimitiveComponent*> URiverGenerator::GetCollisionComponents() const
{
	TArray<UPrimitiveComponent*> Result;
	Result.Reserve(SplineMeshComponents.Num());
	for (USplineMeshComponent* Comp : SplineMeshComponents)
	{
		if ((Comp != nullptr) && (Comp->GetCollisionEnabled() != ECollisionEnabled::NoCollision))
		{
			Result.Add(Comp);
		}
	}
	return Result;
}

void URiverGenerator::GenerateMeshes()
{
	Reset();

	AWaterBody* OwnerBody = GetOuterAWaterBody();
	UWaterSplineComponent* WaterSplineComp = OwnerBody->GetWaterSpline();
	if (WaterSplineComp)
	{
		const int32 NumSplinePoints = WaterSplineComp->GetNumberOfSplinePoints();

		const int32 NumberOfMeshComponentsNeeded = WaterSplineComp->IsClosedLoop() ? NumSplinePoints : NumSplinePoints - 1;
		for (int32 SplinePtIndex = 0; SplinePtIndex < NumberOfMeshComponentsNeeded; ++SplinePtIndex)
		{
			FString Name = FString::Printf(TEXT("SplineMeshComponent_%d"), SplinePtIndex);
			USplineMeshComponent* MeshComp = NewObject<USplineMeshComponent>(OwnerBody, *Name);
			MeshComp->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
			SplineMeshComponents.Add(MeshComp);
			MeshComp->SetMobility(OwnerBody->GetRootComponent()->Mobility);
			MeshComp->SetupAttachment(OwnerBody->GetRootComponent());
			MeshComp->RegisterComponent();

			// Call UpdateSplineMesh after RegisterComponent so that physics state creation can happen (needs the component to be registered)
			UpdateSplineMesh(MeshComp, SplinePtIndex);
		}
	}
}

void URiverGenerator::UpdateSplineMesh(USplineMeshComponent* MeshComp, int32 SplinePointIndex)
{
	AWaterBody* OwnerBody = GetOuterAWaterBody();
	check(OwnerBody);

	UWaterSplineComponent* WaterSplineComp = OwnerBody->GetWaterSpline();

	if (WaterSplineComp)
	{
		const int32 NumSplinePoints = WaterSplineComp->GetNumberOfSplinePoints();

		int32 StartSplinePointIndex = SplinePointIndex;
		int32 StopSplinePointIndex = WaterSplineComp->IsClosedLoop() && StartSplinePointIndex == NumSplinePoints - 1 ? 0 : StartSplinePointIndex + 1;

		UStaticMesh* WaterMeshOverride = OwnerBody->GetWaterMeshOverride();
		MeshComp->SetStaticMesh(WaterMeshOverride ? WaterMeshOverride : UWaterSubsystem::StaticClass()->GetDefaultObject<UWaterSubsystem>()->DefaultRiverMesh);
		MeshComp->SetMaterial(0, OwnerBody->GetWaterMaterial());
		MeshComp->SetCollisionProfileName(OwnerBody->GetCollisionProfileName());
		// In the case of rivers, the USplineMeshComponent acts as both collision and visual component so we simply disable collision on them : 
		MeshComp->SetGenerateOverlapEvents(OwnerBody->bGenerateCollisions);
		if (!OwnerBody->bGenerateCollisions)
		{
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		MeshComp->SetCastShadow(false);

		MeshComp->bFillCollisionUnderneathForNavmesh = OwnerBody->bGenerateCollisions && OwnerBody->bFillCollisionUnderWaterBodiesForNavmesh;

		bool bUpdateMesh = false;
		const FVector StartScale = WaterSplineComp->GetScaleAtSplinePoint(StartSplinePointIndex);
		const FVector EndScale = WaterSplineComp->GetScaleAtSplinePoint(StopSplinePointIndex);

		MeshComp->SetStartScale(FVector2D(StartScale), bUpdateMesh);
		MeshComp->SetEndScale(FVector2D(EndScale), bUpdateMesh);

		FVector StartPos, StartTangent;
		WaterSplineComp->GetLocationAndTangentAtSplinePoint(StartSplinePointIndex, StartPos, StartTangent, ESplineCoordinateSpace::Local);

		FVector EndPos, EndTangent;
		WaterSplineComp->GetLocationAndTangentAtSplinePoint(StopSplinePointIndex, EndPos, EndTangent, ESplineCoordinateSpace::Local);

		MeshComp->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent, bUpdateMesh);

		MeshComp->UpdateMesh();

#if WITH_CHAOS
		//
		// GROSS HACK to work around the issue above that CreatePhysicsMeshes() doesn't currently work at Runtime.
		// The "cook" for a FKConvexElem just creates and caches a Chaos::FConvex instance, and the restore from cooked
		// data just restores that and pases it to the FKConvexElem. So we're just going to do that ourselves.
		// Code below is taken from FChaosDerivedDataCooker::BuildConvexMeshes() 
		//
		if (MeshComp->GetBodySetup())
		{
			for (FKConvexElem& Elem : MeshComp->GetBodySetup()->AggGeom.ConvexElems)
			{
				int32 NumHullVerts = Elem.VertexData.Num();
				TArray<Chaos::FVec3> ConvexVertices;
				ConvexVertices.SetNum(NumHullVerts);
				for (int32 VertIndex = 0; VertIndex < NumHullVerts; ++VertIndex)
				{
					ConvexVertices[VertIndex] = Elem.VertexData[VertIndex];
				}

				TSharedPtr<Chaos::FConvex, ESPMode::ThreadSafe> ChaosConvex = MakeShared<Chaos::FConvex, ESPMode::ThreadSafe>(ConvexVertices, 0.0f);

				Elem.SetChaosConvexMesh(MoveTemp(ChaosConvex));
			}

			MeshComp->RecreatePhysicsState();
		}
#endif
	}
}