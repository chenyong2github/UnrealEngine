// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyCustomComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WaterSubsystem.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

UWaterBodyCustomComponent::UWaterBodyCustomComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAffectsLandscape = false;

	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(!IsFlatSurface());
	check(!IsWaterSplineClosedLoop());
	check(!IsHeightOffsetSupported());
}

TArray<UPrimitiveComponent*> UWaterBodyCustomComponent::GetCollisionComponents() const
{
	AActor* Owner = GetOwner();
	check(Owner);
	
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Owner->GetComponents(PrimitiveComponents);
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

void UWaterBodyCustomComponent::UpdateComponentVisibility()
{
	if (UWorld* World = GetWorld())
	{
		// If water rendering is enabled we dont need the components to do the rendering
		const bool bIsWaterRenderingEnabled = UWaterSubsystem::GetWaterSubsystem(World)->IsWaterRenderingEnabled();

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
		GetOwner()->GetComponents(MeshComponents);
		for (UStaticMeshComponent* Component : MeshComponents)
		{
			Component->SetVisibility(bIsWaterRenderingEnabled);
			Component->SetHiddenInGame(!bIsWaterRenderingEnabled);
		}
	}
}

void UWaterBodyCustomComponent::Reset()
{
	AActor* Owner = GetOwner();
	check(Owner);

	TArray<UStaticMeshComponent*> MeshComponents;
	Owner->GetComponents(MeshComponents);

	MeshComp = nullptr;
	for (UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		MeshComponent->DestroyComponent();
	}
}

void UWaterBodyCustomComponent::OnUpdateBody(bool bWithExclusionVolumes)
{
	AActor* OwnerActor = GetOwner();
	check(OwnerActor);

	if (!MeshComp)
	{
		MeshComp = NewObject<UStaticMeshComponent>(OwnerActor, TEXT("CustomMeshComponent"));
		MeshComp->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
		MeshComp->SetupAttachment(this);
		MeshComp->SetCollisionProfileName(GetCollisionProfileName());
		// In the case of custom meshes, the static mesh component acts as both collision and visual component so we simply disable collision on it: 
		MeshComp->SetGenerateOverlapEvents(bGenerateCollisions);
		if (!bGenerateCollisions)
		{
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		MeshComp->RegisterComponent();
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	OwnerActor->GetComponents(PrimitiveComponents);

	// Make no assumptions for custom meshes.  Add all components with collision to the list of collision components
	for (UPrimitiveComponent* Comp : PrimitiveComponents)
	{
		if (bGenerateCollisions && (Comp->GetCollisionEnabled() != ECollisionEnabled::NoCollision))
		{
			// Use value of bFillCollisionUnderWaterBodiesForNavmesh for all components with collisions.
			Comp->bFillCollisionUnderneathForNavmesh = bFillCollisionUnderWaterBodiesForNavmesh;
		}

		Comp->SetMobility(Mobility);
	}

	CreateOrUpdateWaterMID();
	MeshComp->SetStaticMesh(GetWaterMeshOverride());
	MeshComp->SetMaterial(0, WaterMID);
	MeshComp->SetCastShadow(false);
	MeshComp->MarkRenderStateDirty();
}

void UWaterBodyCustomComponent::BeginUpdateWaterBody()
{
	Super::BeginUpdateWaterBody();

	UMaterialInstanceDynamic* WaterMaterialInstance = GetWaterMaterialInstance();
	if (WaterMaterialInstance && MeshComp)
	{
		// We need to get(or create) the water MID at runtime and apply it to the static mesh component of the custom generator
		// The MID is transient so it will not make it through serialization, apply it here (at runtime)
		MeshComp->SetMaterial(0, WaterMaterialInstance);
	}
}