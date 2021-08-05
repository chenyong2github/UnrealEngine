// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "Engine/World.h"

UWorldPartitionStreamingSourceComponent::UWorldPartitionStreamingSourceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bStreamingSourceEnabled(true)
	, TargetState(EStreamingSourceTargetState::Activated)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWorldPartitionStreamingSourceComponent::OnRegister()
{
	Super::OnRegister();

	UWorld* World = GetWorld();

#if WITH_EDITOR
	if (!World->IsGameWorld())
	{
		return;
	}
#endif

	if (UWorldPartition* WorldPartition = World->GetWorldPartition())
	{
		WorldPartition->RegisterStreamingSourceProvider(this);
	}
}

void UWorldPartitionStreamingSourceComponent::OnUnregister()
{
	Super::OnUnregister();

	UWorld* World = GetWorld();

#if WITH_EDITOR
	if (!World->IsGameWorld())
	{
		return;
	}
#endif

	if (UWorldPartition* WorldPartition = World->GetWorldPartition())
	{
		verify(WorldPartition->UnregisterStreamingSourceProvider(this));
	}
}

bool UWorldPartitionStreamingSourceComponent::GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource)
{
	if (bStreamingSourceEnabled)
	{
		AActor* Actor = GetOwner();

		OutStreamingSource.Name = *Actor->GetActorNameOrLabel();
		OutStreamingSource.Location = Actor->GetActorLocation();
		OutStreamingSource.Rotation = Actor->GetActorRotation();
		OutStreamingSource.TargetState = TargetState;
		return true;
	}
	return false;
}