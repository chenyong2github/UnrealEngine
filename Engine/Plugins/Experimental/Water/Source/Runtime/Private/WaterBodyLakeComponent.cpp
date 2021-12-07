// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyLakeComponent.h"
#include "WaterSplineComponent.h"
#include "WaterSubsystem.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "Engine/StaticMesh.h"
#include "LakeCollisionComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

UWaterBodyLakeComponent::UWaterBodyLakeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(IsFlatSurface());
	check(IsWaterSplineClosedLoop());
	check(!IsHeightOffsetSupported());
}

TArray<UPrimitiveComponent*> UWaterBodyLakeComponent::GetCollisionComponents() const
{
	TArray<UPrimitiveComponent*> Result;
	if (LakeCollision != nullptr)
	{
		Result.Add(LakeCollision);
	}
	return Result;
}

TArray<UPrimitiveComponent*> UWaterBodyLakeComponent::GetStandardRenderableComponents() const 
{
	TArray<UPrimitiveComponent*> Result;
	if (LakeMeshComp != nullptr)
	{
		Result.Add(LakeMeshComp);
	}
	return Result;
}

void UWaterBodyLakeComponent::Reset()
{
	AActor* Owner = GetOwner();
	check(Owner);

	TArray<UStaticMeshComponent*> MeshComponents;
	Owner->GetComponents(MeshComponents);

	for (UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		MeshComponent->DestroyComponent();
	}

	if (LakeCollision)
	{
		LakeCollision->DestroyComponent();
	}

	LakeCollision = nullptr;
	LakeMeshComp = nullptr;
}

void UWaterBodyLakeComponent::OnUpdateBody(bool bWithExclusionVolumes)
{
	AActor* OwnerActor = GetOwner();
	check(OwnerActor);
	
	if (!LakeMeshComp)
	{
		LakeMeshComp = NewObject<UStaticMeshComponent>(OwnerActor, TEXT("LakeMeshComponent"), RF_Transactional);
		LakeMeshComp->SetupAttachment(this);
		LakeMeshComp->RegisterComponent();
	}

	if (bGenerateCollisions)
	{
		if (!LakeCollision)
		{
			LakeCollision = NewObject<ULakeCollisionComponent>(OwnerActor, TEXT("LakeCollisionComponent"), RF_Transactional);
			LakeCollision->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
			LakeCollision->SetupAttachment(this);
			LakeCollision->RegisterComponent();
		}
	}
	else
	{
		if (LakeCollision)
		{
			LakeCollision->DestroyComponent();
		}
		LakeCollision = nullptr;
	}

	if (UWaterSplineComponent* WaterSpline = GetWaterSpline())
	{
		UStaticMesh* WaterMesh = GetWaterMeshOverride() ? GetWaterMeshOverride() : UWaterSubsystem::StaticClass()->GetDefaultObject<UWaterSubsystem>()->DefaultLakeMesh;

		const FVector SplineExtent = WaterSpline->Bounds.BoxExtent;

		FVector WorldLoc(WaterSpline->Bounds.Origin);
		WorldLoc.Z = GetComponentLocation().Z;

		if (WaterMesh)
		{
			FTransform MeshCompToWorld = WaterSpline->GetComponentToWorld();
			// Scale the water mesh so that it is the size of the bounds
			FVector MeshExtent = WaterMesh->GetBounds().BoxExtent;
			MeshExtent.Z = 1.0f;

			const FVector LocalSplineExtent = WaterSpline->Bounds.TransformBy(MeshCompToWorld.Inverse()).BoxExtent;

			const FVector ScaleRatio = SplineExtent / MeshExtent;
			LakeMeshComp->SetWorldScale3D(FVector(ScaleRatio.X, ScaleRatio.Y, 1));
			LakeMeshComp->SetWorldLocation(WorldLoc);
			LakeMeshComp->SetWorldRotation(FQuat::Identity);
			LakeMeshComp->SetAbsolute(false, false, true);
			LakeMeshComp->SetStaticMesh(WaterMesh);
			LakeMeshComp->SetMaterial(0, GetWaterMaterialInstance());
			LakeMeshComp->SetCastShadow(false);
			LakeMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		LakeMeshComp->SetMobility(Mobility);

		if (LakeCollision)
		{
			check(bGenerateCollisions);
			LakeCollision->bFillCollisionUnderneathForNavmesh = bFillCollisionUnderWaterBodiesForNavmesh;
			LakeCollision->SetMobility(Mobility);
			LakeCollision->SetCollisionProfileName(GetCollisionProfileName());
			LakeCollision->SetGenerateOverlapEvents(true);

			const float Depth = GetChannelDepth() / 2;
			FVector LakeCollisionExtent = FVector(SplineExtent.X, SplineExtent.Y, 0.f) / GetComponentScale();
			LakeCollisionExtent.Z = Depth + CollisionHeightOffset / 2;
			LakeCollision->SetWorldLocation(WorldLoc + FVector(0, 0, -Depth + CollisionHeightOffset / 2));
			LakeCollision->UpdateCollision(LakeCollisionExtent, true);
		}
	}
}