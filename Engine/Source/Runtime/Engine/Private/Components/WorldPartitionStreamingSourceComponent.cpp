// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "Engine/World.h"

UWorldPartitionStreamingSourceComponent::UWorldPartitionStreamingSourceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, DefaultVisualizerLoadingRange(10000.f)
#endif
	, TargetGrid(NAME_None)
	, DebugColor(ForceInit)
	, Priority(EStreamingSourcePriority::Low)
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
		OutStreamingSource.DebugColor = DebugColor;
		OutStreamingSource.TargetGrid = TargetGrid;
		OutStreamingSource.TargetHLODLayer = TargetHLODLayer;
		OutStreamingSource.Shapes = Shapes;
		OutStreamingSource.Priority = Priority;
		return true;
	}
	return false;
}

bool UWorldPartitionStreamingSourceComponent::IsStreamingCompleted() const
{
	UWorld* World = GetWorld();
	if (!bStreamingSourceEnabled || !World->IsGameWorld())
	{
		return false;
	}
	
	UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
	UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>();
	if (!WorldPartitionSubsystem || !DataLayerSubsystem)	
	{
		return false;
	}

	// Build a query source
	AActor* Actor = GetOwner();
	TArray<FWorldPartitionStreamingQuerySource> QuerySources;
	FWorldPartitionStreamingQuerySource& QuerySource = QuerySources.Emplace_GetRef();
	QuerySource.bSpatialQuery = true;
	QuerySource.Location = Actor->GetActorLocation();
	QuerySource.Rotation = Actor->GetActorRotation();
	QuerySource.TargetGrid = TargetGrid;
	QuerySource.Shapes = Shapes;
	QuerySource.bUseGridLoadingRange = true;
	QuerySource.Radius = 0.f;
	QuerySource.bDataLayersOnly = false;
	QuerySource.DataLayers = (TargetState == EStreamingSourceTargetState::Loaded) ? DataLayerSubsystem->GetEffectiveLoadedDataLayerNames().Array() : DataLayerSubsystem->GetEffectiveActiveDataLayerNames().Array();

	// Execute query
	const EWorldPartitionRuntimeCellState QueryState = (TargetState == EStreamingSourceTargetState::Loaded) ? EWorldPartitionRuntimeCellState::Loaded : EWorldPartitionRuntimeCellState::Activated;
	return WorldPartitionSubsystem->IsStreamingCompleted(QueryState, QuerySources, /*bExactState*/ true);
}

void UWorldPartitionStreamingSourceComponent::DrawVisualization(const FSceneView* View, FPrimitiveDrawInterface* PDI) const
{
#if WITH_EDITOR
	AActor* Actor = GetOwner();
	FStreamingSourceShapeHelper::ForEachShape(DefaultVisualizerLoadingRange, DefaultVisualizerLoadingRange, /*bInProjectIn2D*/ false, Actor->GetActorLocation(), Actor->GetActorRotation(), Shapes, [&PDI](const FSphericalSector& Shape)
	{
		if (Shape.IsSphere())
		{
			DrawWireSphere(PDI, Shape.GetCenter(), FColor::White, Shape.GetRadius(), 32, SDPG_World, 1.0, 0, true);
		}
		else
		{
			TArray<TPair<FVector, FVector>> Lines = Shape.BuildDebugMesh();
			for (const auto& Line : Lines)
			{
				PDI->DrawLine(Line.Key, Line.Value, FColor::White, SDPG_World, 1.0, 0, true);
			};
		}
	});
#endif
}

#if WITH_EDITOR
bool UWorldPartitionStreamingSourceComponent::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty && InProperty->GetName() == GET_MEMBER_NAME_STRING_CHECKED(UWorldPartitionStreamingSourceComponent, TargetGrid))
	{
		return TargetHLODLayer == nullptr;
	}
	return Super::CanEditChange(InProperty);
}
#endif
