// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyLakeActor.h"
#include "WaterSplineComponent.h"
#include "WaterSubsystem.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "Engine/StaticMesh.h"
#include "LakeCollisionComponent.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

AWaterBodyLake::AWaterBodyLake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyType = GetWaterBodyType();

#if WITH_EDITOR
	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterBodyLakeSprite"));
#endif

	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(IsFlatSurface());
	check(IsWaterSplineClosedLoop());
	check(!IsHeightOffsetSupported());
}

void AWaterBodyLake::InitializeBody()
{
	if (!LakeGenerator)
	{
		LakeGenerator = NewObject<ULakeGenerator>(this, TEXT("LakeGenerator"));
	}
}

TArray<UPrimitiveComponent*> AWaterBodyLake::GetCollisionComponents() const
{
	if (LakeGenerator)
	{
		return LakeGenerator->GetCollisionComponents();
	}
	return Super::GetCollisionComponents();
}

void AWaterBodyLake::UpdateWaterBody(bool bWithExclusionVolumes)
{
	if (LakeGenerator)
	{
		LakeGenerator->UpdateBody(bWithExclusionVolumes);
	}
}

// ----------------------------------------------------------------------------------

ULakeGenerator::ULakeGenerator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void ULakeGenerator::Reset()
{
	AWaterBody* OwnerBody = GetOuterAWaterBody();
	TArray<UStaticMeshComponent*> MeshComponents;
	OwnerBody->GetComponents(MeshComponents);

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

void ULakeGenerator::OnUpdateBody(bool bWithExclusionVolumes)
{
	AWaterBody* OwnerBody = GetOuterAWaterBody();
	if (!LakeMeshComp)
	{
		LakeMeshComp = NewObject<UStaticMeshComponent>(OwnerBody, TEXT("LakeMeshComponent"));
		LakeMeshComp->SetupAttachment(OwnerBody->GetRootComponent());
		LakeMeshComp->RegisterComponent();
	}

	if (OwnerBody->bGenerateCollisions)
	{
		if (!LakeCollision)
		{
			LakeCollision = NewObject<ULakeCollisionComponent>(OwnerBody, TEXT("LakeCollisionComponent"));
			LakeCollision->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
			LakeCollision->SetupAttachment(OwnerBody->GetRootComponent());
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

	UWaterSplineComponent* WaterSpline = OwnerBody->GetWaterSpline();

	UStaticMesh* WaterMesh = OwnerBody->GetWaterMeshOverride() ? OwnerBody->GetWaterMeshOverride() : UWaterSubsystem::StaticClass()->GetDefaultObject<UWaterSubsystem>()->DefaultLakeMesh;

	const FVector SplineExtent = WaterSpline->Bounds.BoxExtent;

	FVector WorldLoc(WaterSpline->Bounds.Origin);
	WorldLoc.Z = OwnerBody->GetActorLocation().Z;

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
		LakeMeshComp->SetMaterial(0, OwnerBody->GetWaterMaterial());
		LakeMeshComp->SetCastShadow(false);
		LakeMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	LakeMeshComp->SetMobility(OwnerBody->GetRootComponent()->Mobility);

	if (LakeCollision)
	{
		check(OwnerBody->bGenerateCollisions);
		LakeCollision->bFillCollisionUnderneathForNavmesh = OwnerBody->bFillCollisionUnderWaterBodiesForNavmesh;
		LakeCollision->SetMobility(OwnerBody->GetRootComponent()->Mobility);
		LakeCollision->SetCollisionProfileName(OwnerBody->GetCollisionProfileName());
		LakeCollision->SetGenerateOverlapEvents(true);

		const float Depth = OwnerBody->GetChannelDepth() / 2;
		FVector LakeCollisionExtent = FVector(SplineExtent.X, SplineExtent.Y, Depth) / OwnerBody->GetActorScale();
		LakeCollision->SetWorldLocation(WorldLoc + FVector(0, 0, -Depth));
		LakeCollision->UpdateCollision(LakeCollisionExtent, true);
	}
}

void ULakeGenerator::PostLoad()
{
	Super::PostLoad();

	LakeCollisionComp_DEPRECATED = nullptr;
}

TArray<UPrimitiveComponent*> ULakeGenerator::GetCollisionComponents() const
{
	TArray<UPrimitiveComponent*> Result;
	if (LakeCollision != nullptr)
	{
		Result.Add(LakeCollision);
	}
	return Result;
}