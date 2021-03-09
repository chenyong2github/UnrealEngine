// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyCustomActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#include "WaterSubsystem.h"
#endif

// ----------------------------------------------------------------------------------

AWaterBodyCustom::AWaterBodyCustom(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyType = GetWaterBodyType();
	bAffectsLandscape = false;

#if WITH_EDITOR
	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterBodyCustomSprite"));
#endif

	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(!IsFlatSurface());
	check(!IsWaterSplineClosedLoop());
	check(!IsHeightOffsetSupported());
}

void AWaterBodyCustom::InitializeBody()
{
	if (!CustomGenerator)
	{
		CustomGenerator = NewObject<UCustomMeshGenerator>(this, TEXT("CustomGenerator"));
	}
}

TArray<UPrimitiveComponent*> AWaterBodyCustom::GetCollisionComponents() const
{
	if (CustomGenerator)
	{
		return CustomGenerator->GetCollisionComponents();
	}
	return Super::GetCollisionComponents();
}

void AWaterBodyCustom::BeginUpdateWaterBody()
{
	Super::BeginUpdateWaterBody();

	UMaterialInstanceDynamic* WaterMaterialInstance = GetWaterMaterialInstance();
	if (CustomGenerator && WaterMaterialInstance)
	{
		// We need to get(or create) the water MID at runtime and apply it to the static mesh component of the custom generator
		// The MID is transient so it will not make it through serialization, apply it here (at runtime)
		CustomGenerator->SetMaterial(WaterMaterialInstance);
	}
}

void AWaterBodyCustom::UpdateWaterBody(bool bWithExclusionVolumes)
{
	if (CustomGenerator)
	{
		CustomGenerator->UpdateBody(bWithExclusionVolumes);
	}
}


#if WITH_EDITOR
bool AWaterBodyCustom::IsIconVisible() const
{
	return (GetWaterMeshOverride() == nullptr);
}
#endif

// ----------------------------------------------------------------------------------

UCustomMeshGenerator::UCustomMeshGenerator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UCustomMeshGenerator::Reset()
{
	AWaterBody* OwnerBody = GetOuterAWaterBody();
	TArray<UStaticMeshComponent*> MeshComponents;
	OwnerBody->GetComponents(MeshComponents);

	MeshComp = nullptr;
	for (UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		MeshComponent->DestroyComponent();
	}
}

void UCustomMeshGenerator::SetMaterial(UMaterialInterface* Material)
{
	if (MeshComp)
	{
		MeshComp->SetMaterial(0, Material);
	}
}

void UCustomMeshGenerator::OnUpdateBody(bool bWithExclusionVolumes)
{
	AWaterBody* OwnerBody = GetOuterAWaterBody();
	if (!MeshComp)
	{
		MeshComp = NewObject<UStaticMeshComponent>(OwnerBody, TEXT("CustomMeshComponent"));
		MeshComp->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
		MeshComp->SetupAttachment(OwnerBody->GetRootComponent());
		MeshComp->SetCollisionProfileName(OwnerBody->GetCollisionProfileName());
		// In the case of custom meshes, the static mesh component acts as both collision and visual component so we simply disable collision on it: 
		MeshComp->SetGenerateOverlapEvents(OwnerBody->bGenerateCollisions);
		if (!OwnerBody->bGenerateCollisions)
		{
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		MeshComp->RegisterComponent();
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	OwnerBody->GetComponents(PrimitiveComponents);

	// Make no assumptions for custom meshes.  Add all components with collision to the list of collision components
	for (UPrimitiveComponent* Comp : PrimitiveComponents)
	{
		if (OwnerBody->bGenerateCollisions && (Comp->GetCollisionEnabled() != ECollisionEnabled::NoCollision))
		{
			// Use value of bFillCollisionUnderWaterBodiesForNavmesh for all components with collisions.
			Comp->bFillCollisionUnderneathForNavmesh = OwnerBody->bFillCollisionUnderWaterBodiesForNavmesh;
		}

		Comp->SetMobility(OwnerBody->GetRootComponent()->Mobility);
	}

	MeshComp->SetStaticMesh(OwnerBody->GetWaterMeshOverride());
	UMaterialInstanceDynamic* WaterMaterial = OwnerBody->GetWaterMaterialInstance();
	MeshComp->SetMaterial(0, WaterMaterial);
	MeshComp->SetCastShadow(false);
	MeshComp->MarkRenderStateDirty();
}

TArray<UPrimitiveComponent*> UCustomMeshGenerator::GetCollisionComponents() const
{
	AWaterBody* OwnerBody = GetOuterAWaterBody();
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	OwnerBody->GetComponents(PrimitiveComponents);
	TArray<UPrimitiveComponent*> Result;
	Result.Reserve(PrimitiveComponents.Num());
	for (UPrimitiveComponent* Comp : PrimitiveComponents)
	{
		if (Comp->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			Result.Add(Comp);
		}
	}
	return Result;
}
