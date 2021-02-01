// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "WorldPartition/WorldPartition.h"

UWorldPartitionStreamingSourceComponent::UWorldPartitionStreamingSourceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWorldPartitionStreamingSourceComponent::OnRegister()
{
	Super::OnRegister();

	UWorld* World = GetWorld();

#if WITH_EDITOR
	if (!World->IsPlayInEditor())
	{
		return;
	}
#endif

	if (UWorldPartition* WorldPartition = World->GetWorldPartition())
	{
		AActor* Actor = GetOwner();
		ULevel* ActorLevel = Actor->GetLevel();

		if (ActorLevel == World->PersistentLevel)
		{
			WorldPartition->RegisterStreamingSourceProvider(this);
		}
		else
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("Attaching a WorldPartitionStreamingSourceComponent to an actor that is in the grid will result in never unloading the affected cells (%s)"), *Actor->GetName());
		}
	}
}

void UWorldPartitionStreamingSourceComponent::OnUnregister()
{
	Super::OnUnregister();

	UWorld* World = GetWorld();

#if WITH_EDITOR
	if (!World->IsPlayInEditor())
	{
		return;
	}
#endif

	if (UWorldPartition* WorldPartition = World->GetWorldPartition())
	{
		AActor* Actor = GetOwner();
		ULevel* ActorLevel = Actor->GetLevel();

		verify(WorldPartition->UnregisterStreamingSourceProvider(this) || (ActorLevel != World->PersistentLevel));
	}
}

bool UWorldPartitionStreamingSourceComponent::GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource)
{
	AActor* Actor = GetOwner();
	OutStreamingSource.Location = Actor->GetActorLocation();
	OutStreamingSource.Rotation = Actor->GetActorRotation();
	return true;
}