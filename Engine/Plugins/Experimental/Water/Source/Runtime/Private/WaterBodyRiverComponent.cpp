// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyRiverComponent.h"
#include "WaterSplineComponent.h"
#include "WaterSubsystem.h"
#include "WaterUtils.h"
#include "Algo/ForEach.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMesh.h"
#include "Algo/Transform.h"

#if WITH_CHAOS
// for working around Chaos issue
#include "Chaos/Particles.h"
#include "Chaos/Convex.h"
#endif

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

UWaterBodyRiverComponent::UWaterBodyRiverComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(!IsFlatSurface());
	check(!IsWaterSplineClosedLoop());
	check(!IsHeightOffsetSupported());
}


TArray<UPrimitiveComponent*> UWaterBodyRiverComponent::GetCollisionComponents() const
{
	TArray<UPrimitiveComponent*> Result;
	Result.Reserve(SplineMeshComponents.Num());
	Algo::TransformIf(SplineMeshComponents, Result, [](USplineMeshComponent* SplineComp) { return ((SplineComp != nullptr) && (SplineComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision)); }, [](USplineMeshComponent* SplineComp) { return SplineComp; });
	return Result;
}

TArray<UPrimitiveComponent*> UWaterBodyRiverComponent::GetStandardRenderableComponents() const
{
	TArray<UPrimitiveComponent*> Result;
	Result.Reserve(SplineMeshComponents.Num());
	Algo::TransformIf(SplineMeshComponents, Result, [](USplineMeshComponent* SplineComp) { return (SplineComp != nullptr); }, [](USplineMeshComponent* SplineComp) { return SplineComp; });
	return Result;
}

void UWaterBodyRiverComponent::OnUpdateBody(bool bWithExclusionVolumes)
{
	if (const USplineComponent* WaterSpline = GetWaterSpline())
	{
		const int32 NumSplinePoints = WaterSpline->GetNumberOfSplinePoints();
		const int32 NumberOfMeshComponentsNeeded = WaterSpline->IsClosedLoop() ? NumSplinePoints : NumSplinePoints - 1;
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

void UWaterBodyRiverComponent::UpdateMaterialInstances()
{
	Super::UpdateMaterialInstances();

	CreateOrUpdateLakeTransitionMID();
	CreateOrUpdateOceanTransitionMID();
}

void UWaterBodyRiverComponent::SetLakeTransitionMaterial(UMaterialInterface* InMaterial)
{
	LakeTransitionMaterial = InMaterial;
	UpdateMaterialInstances();
}

void UWaterBodyRiverComponent::SetOceanTransitionMaterial(UMaterialInterface* InMaterial)
{
	OceanTransitionMaterial = InMaterial;
	UpdateMaterialInstances();
}

void UWaterBodyRiverComponent::Reset()
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

UMaterialInstanceDynamic* UWaterBodyRiverComponent::GetRiverToLakeTransitionMaterialInstance()
{
	CreateOrUpdateLakeTransitionMID();
	return LakeTransitionMID;
}

UMaterialInstanceDynamic* UWaterBodyRiverComponent::GetRiverToOceanTransitionMaterialInstance()
{
	CreateOrUpdateOceanTransitionMID();
	return OceanTransitionMID;
}

#if WITH_EDITOR
TArray<UPrimitiveComponent*> UWaterBodyRiverComponent::GetBrushRenderableComponents() const
{
	TArray<UPrimitiveComponent*> BrushRenderableComponents;
	
	BrushRenderableComponents.Reserve(SplineMeshComponents.Num());
	Algo::ForEach(SplineMeshComponents, [&BrushRenderableComponents](USplineMeshComponent* Comp)
	{
		if (Comp != nullptr)
		{
			BrushRenderableComponents.Add(StaticCast<UPrimitiveComponent*>(Comp));
		}
	});
	
	return BrushRenderableComponents;
}
#endif //WITH_EDITOR

void UWaterBodyRiverComponent::CreateOrUpdateLakeTransitionMID()
{
	if (GetWorld())
	{
		LakeTransitionMID = FWaterUtils::GetOrCreateTransientMID(LakeTransitionMID, TEXT("LakeTransitionMID"), LakeTransitionMaterial, GetTransientMIDFlags());

		SetDynamicParametersOnMID(LakeTransitionMID);
	}
}

void UWaterBodyRiverComponent::CreateOrUpdateOceanTransitionMID()
{
	if (GetWorld())
	{
		OceanTransitionMID = FWaterUtils::GetOrCreateTransientMID(OceanTransitionMID, TEXT("OceanTransitionMID"), OceanTransitionMaterial, GetTransientMIDFlags());

		SetDynamicParametersOnMID(OceanTransitionMID);
	}
}

void UWaterBodyRiverComponent::GenerateMeshes()
{
	Reset();

	AActor* Owner = GetOwner();
	check(Owner);
	
	if (const USplineComponent* WaterSpline = GetWaterSpline())
	{
		const int32 NumSplinePoints = WaterSpline->GetNumberOfSplinePoints();

		const int32 NumberOfMeshComponentsNeeded = WaterSpline->IsClosedLoop() ? NumSplinePoints : NumSplinePoints - 1;
		for (int32 SplinePtIndex = 0; SplinePtIndex < NumberOfMeshComponentsNeeded; ++SplinePtIndex)
		{
			FString Name = FString::Printf(TEXT("SplineMeshComponent_%d"), SplinePtIndex);
			USplineMeshComponent* MeshComp = NewObject<USplineMeshComponent>(Owner, *Name, RF_Transactional);
			MeshComp->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
			SplineMeshComponents.Add(MeshComp);
			MeshComp->SetMobility(Owner->GetRootComponent()->Mobility);
			MeshComp->SetupAttachment(this);
			if (GetWorld() && GetWorld()->bIsWorldInitialized)
			{
				MeshComp->RegisterComponent();
			}

			// Call UpdateSplineMesh after RegisterComponent so that physics state creation can happen (needs the component to be registered)
			UpdateSplineMesh(MeshComp, SplinePtIndex);
		}
	}
}

void UWaterBodyRiverComponent::UpdateSplineMesh(USplineMeshComponent* MeshComp, int32 SplinePointIndex)
{
	if (const USplineComponent* WaterSpline = GetWaterSpline())
	{
		const int32 NumSplinePoints = WaterSpline->GetNumberOfSplinePoints();

		const int32 StartSplinePointIndex = SplinePointIndex;
		const int32 StopSplinePointIndex = WaterSpline->IsClosedLoop() && StartSplinePointIndex == NumSplinePoints - 1 ? 0 : StartSplinePointIndex + 1;

		UStaticMesh* StaticMesh = GetWaterMeshOverride();
		if (StaticMesh == nullptr)
		{
			StaticMesh = UWaterSubsystem::StaticClass()->GetDefaultObject<UWaterSubsystem>()->DefaultRiverMesh;
		}
		check(StaticMesh != nullptr);
		MeshComp->SetStaticMesh(StaticMesh);
		MeshComp->SetMaterial(0, GetWaterMaterialInstance());
		MeshComp->SetCollisionProfileName(GetCollisionProfileName());
		// In the case of rivers, the USplineMeshComponent acts as both collision and visual component so we simply disable collision on them : 
		MeshComp->SetGenerateOverlapEvents(bGenerateCollisions);
		if (!bGenerateCollisions)
		{
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		MeshComp->SetCastShadow(false);

		MeshComp->bFillCollisionUnderneathForNavmesh = bGenerateCollisions && bFillCollisionUnderWaterBodiesForNavmesh;

		const bool bUpdateMesh = false;
		const FVector StartScale = WaterSpline->GetScaleAtSplinePoint(StartSplinePointIndex);
		const FVector EndScale = WaterSpline->GetScaleAtSplinePoint(StopSplinePointIndex);

		// Scale the water mesh so that it is the size of the bounds
		FVector StaticMeshExtent = 2.0f * StaticMesh->GetBounds().BoxExtent;
		StaticMeshExtent.X = FMath::Max(StaticMeshExtent.X, 0.0001f);
		StaticMeshExtent.Y = FMath::Max(StaticMeshExtent.Y, 0.0001f);
		StaticMeshExtent.Z = 1.0f;

		MeshComp->SetStartScale(FVector2D(StartScale / StaticMeshExtent), bUpdateMesh);
		MeshComp->SetEndScale(FVector2D(EndScale / StaticMeshExtent), bUpdateMesh);

		FVector StartPos, StartTangent;
		WaterSpline->GetLocationAndTangentAtSplinePoint(StartSplinePointIndex, StartPos, StartTangent, ESplineCoordinateSpace::Local);

		FVector EndPos, EndTangent;
		WaterSpline->GetLocationAndTangentAtSplinePoint(StopSplinePointIndex, EndPos, EndTangent, ESplineCoordinateSpace::Local);

		MeshComp->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent, bUpdateMesh);

		MeshComp->UpdateMesh();

#if WITH_CHAOS
		//
		// GROSS HACK to work around the issue above that CreatePhysicsMeshes() doesn't currently work at Runtime.
		// The "cook" for a FKConvexElem just creates and caches a Chaos::FConvex instance, and the restore from cooked
		// data just restores that and passes it to the FKConvexElem. So we're just going to do that ourselves.
		// Code below is taken from FChaosDerivedDataCooker::BuildConvexMeshes() 
		//
		if (MeshComp->GetBodySetup())
		{
			for (FKConvexElem& Elem : MeshComp->GetBodySetup()->AggGeom.ConvexElems)
			{
				const int32 NumHullVerts = Elem.VertexData.Num();
				TArray<Chaos::FConvex::FVec3Type> ConvexVertices;
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

#if WITH_EDITOR
void UWaterBodyRiverComponent::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged)
{
	Super::OnPostEditChangeProperty(PropertyChangedEvent, bShapeOrPositionChanged, bWeightmapSettingsChanged);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UWaterBodyRiverComponent, LakeTransitionMaterial) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UWaterBodyRiverComponent, OceanTransitionMaterial))
	{
		UpdateMaterialInstances();
	}
}
#endif