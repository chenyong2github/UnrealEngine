// Copyright Epic Games, Inc. All Rights Reserved.

#include "Instances/InstancedPlacementPartitionActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

AInstancedPlacementPartitionActor::AInstancedPlacementPartitionActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(true);
}

#if WITH_EDITOR	
uint32 AInstancedPlacementPartitionActor::GetDefaultGridSize(UWorld* InWorld) const
{
	return InWorld->GetWorldSettings()->DefaultPlacementGridSize;
}

FGuid AInstancedPlacementPartitionActor::GetGridGuid() const
{
	return PlacementGridGuid;
}

void AInstancedPlacementPartitionActor::SetGridGuid(const FGuid& InGuid)
{
	PlacementGridGuid = InGuid;
}
#endif

ISMInstanceManager* AInstancedPlacementPartitionActor::GetSMInstanceManager(const FSMInstanceId& InstanceId)
{
	if (ISMInstanceManager* ParentSMInstanceManager = Super::GetSMInstanceManager(InstanceId))
	{
		return ParentSMInstanceManager;
	}

#if WITH_EDITOR
	// For now, assume that if we didn't have some manager registered, it is safe to edit the ISM directly.
	// This should be removed after palette items are set up properly in the placement API
	if (IsISMComponent(InstanceId.ISMComponent))
	{
		return InstanceId.ISMComponent;
	}
#endif

	return nullptr;
}
