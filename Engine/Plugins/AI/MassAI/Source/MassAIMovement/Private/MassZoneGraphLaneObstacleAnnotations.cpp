// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassZoneGraphLaneObstacleAnnotations.h"

#include "MassCommonTypes.h"
#include "MassMovementTypes.h"
#include "NavigationProcessor.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphRenderingUtilities.h"
#include "VisualLogger/VisualLogger.h"

void UZoneGraphLaneObstacleAnnotations::PostSubsystemsInitialized()
{
	Super::PostSubsystemsInitialized();

	ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	checkf(ZoneGraphSubsystem, TEXT("Expecting ZoneGraphSubsystem to be present."));
}

FZoneGraphTagMask UZoneGraphLaneObstacleAnnotations::GetAnnotationTags() const
{
	return FZoneGraphTagMask(LaneObstacleTag);
}

void UZoneGraphLaneObstacleAnnotations::HandleEvents(TConstArrayView<const UScriptStruct*> AllEventStructs, const FInstancedStructStream& Events)
{
	Events.ForEach([this](FStructView View)
	{
		if (const FZoneGraphLaneObstacleChangeEvent* LaneObstacleEvent = View.GetMutablePtr<FZoneGraphLaneObstacleChangeEvent>())
		{
			LaneObstacleChangeEvents.Add(*LaneObstacleEvent);
		}
	});
}

void UZoneGraphLaneObstacleAnnotations::TickAnnotation(const float DeltaTime, FZoneGraphAnnotationTagContainer& AnnotationTagContainer)
{
	// Process events
	if (!LaneObstacleTag.IsValid())
	{
		return;
	}

	for (const FZoneGraphLaneObstacleChangeEvent& Event : LaneObstacleChangeEvents)
	{
		const FMassLaneObstacle& LaneObstacle = Event.LaneObstacle;

		// Handle tag and update lane obstacle data.
		if (Event.EventAction == EMassLaneObstacleEventAction::Add)
		{
			// Add tag.
			const FZoneGraphLaneHandle LaneHandle = LaneObstacle.LaneSection.LaneHandle;
			TArrayView<FZoneGraphTagMask> LaneTags = AnnotationTagContainer.GetMutableAnnotationTagsForData(LaneHandle.DataHandle);
			LaneTags[LaneHandle.Index].Add(LaneObstacleTag);

			// Add to obstacle container.
			const FZoneGraphDataHandle ZoneGraphDataHandle = LaneHandle.DataHandle;
			FMassLaneObstacleContainer& LaneObstaclesContainer = RegisteredLaneData[ZoneGraphDataHandle.Index].LaneObstacles;
			LaneObstaclesContainer.Add(LaneObstacle);
		}
		else if (Event.EventAction == EMassLaneObstacleEventAction::Remove)
		{
			for (FMassRegisteredMovementLaneData& LaneData : RegisteredLaneData)
			{
				FMassLaneObstacleContainer& LaneObstaclesContainer = LaneData.LaneObstacles;
				const FMassLaneObstacle* Obstacle = LaneObstaclesContainer.Find(LaneObstacle.GetID());
				if (Obstacle)
				{
					// Remove from obstacle container.
					const bool WasLast = LaneObstaclesContainer.Remove(*Obstacle);
					if (WasLast)
					{
						// Remove tag.
						const FZoneGraphLaneHandle LaneHandle = Obstacle->LaneSection.LaneHandle;
						TArrayView<FZoneGraphTagMask> LaneTags = AnnotationTagContainer.GetMutableAnnotationTagsForData(LaneHandle.DataHandle);
						LaneTags[LaneHandle.Index].Remove(LaneObstacleTag);
					}
				}
			}
		}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
		MarkRenderStateDirty();
#endif

	}
	LaneObstacleChangeEvents.Reset();

#if WITH_MASS_DEBUG && 0
	for (const FMassRegisteredMovementLaneData& LaneData : RegisteredLaneData)
	{
		const FMassLaneObstacleContainer& LaneObstaclesContainer = LaneData.LaneObstacles;
		for (const auto& Pair : LaneObstaclesContainer.LaneObstaclesMap)
		{
			for(const FMassLaneObstacle& Obstacle : Pair.Value)
			{
				FZoneGraphLaneLocation StartLaneLocation;
				FZoneGraphLaneLocation EndLaneLocation;
				FZoneGraphLaneSection Section = Obstacle.LaneSection;
				if (ZoneGraphSubsystem->CalculateLocationAlongLane(Section.LaneHandle, Section.StartDistanceAlongLane, StartLaneLocation) &&
					ZoneGraphSubsystem->CalculateLocationAlongLane(Section.LaneHandle, Section.EndDistanceAlongLane, EndLaneLocation))
				{
					UE_VLOG_LOCATION(this, LogMassDynamicObstacle, Log, StartLaneLocation.Position, 20.f, FColor::Red, TEXT(""));
					UE_VLOG_LOCATION(this, LogMassDynamicObstacle, Log, EndLaneLocation.Position, 20.f, FColor::Red, TEXT(""));
				}
			}
		}
	}
#endif // WITH_MASS_DEBUG 
}

void UZoneGraphLaneObstacleAnnotations::PostZoneGraphDataAdded(const AZoneGraphData& ZoneGraphData)
{
	const UWorld* World = GetWorld();

	// Only consider valid graph from our world
	if (ZoneGraphData.GetWorld() != World)
	{
		return;
	}

	const FString WorldName = World->GetName();
	const FZoneGraphStorage& Storage = ZoneGraphData.GetStorage();
	const int32 ZoneGraphDataIndex = Storage.DataHandle.Index;

	UE_VLOG_UELOG(this, LogMassDynamicObstacle, Verbose, TEXT("%s adding obstacle lane data for zone graph %d/%d"), *WorldName, Storage.DataHandle.Index, Storage.DataHandle.Generation);

	if (ZoneGraphDataIndex >= RegisteredLaneData.Num())
	{
		RegisteredLaneData.SetNum(ZoneGraphDataIndex + 1);
	}

	FMassRegisteredMovementLaneData& LaneData = RegisteredLaneData[ZoneGraphDataIndex];
	LaneData.DataHandle = Storage.DataHandle;
}

void UZoneGraphLaneObstacleAnnotations::PreZoneGraphDataRemoved(const AZoneGraphData& ZoneGraphData)
{
	// Only consider valid graph from our world
	if (ZoneGraphData.GetWorld() != GetWorld())
	{
		return;
	}

	const FZoneGraphStorage& Storage = ZoneGraphData.GetStorage();
	const int32 Index = Storage.DataHandle.Index;
	if (!RegisteredLaneData.IsValidIndex(Index))
	{
		return;
	}

	// Removing lane data.
	FMassRegisteredMovementLaneData& LaneData = RegisteredLaneData[Index];
	LaneData.Reset();

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	MarkRenderStateDirty();
#endif
}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
void UZoneGraphLaneObstacleAnnotations::DebugDraw(FZoneGraphAnnotationSceneProxy* DebugProxy)
{
	UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	if (!ZoneGraph)
	{
		return;
	}
	
	for (const FMassRegisteredMovementLaneData& LaneData : RegisteredLaneData)
	{
		const FZoneGraphStorage* ZoneStorage = ZoneGraph->GetZoneGraphStorage(LaneData.DataHandle);
		if (!ZoneStorage)
		{
			continue;
		}

		const FMassLaneObstacleContainer& LaneObstaclesContainer = LaneData.LaneObstacles;
		for (const auto& Pair : LaneObstaclesContainer.LaneObstaclesMap)
		{
			for(const FMassLaneObstacle& Obstacle : Pair.Value)
			{
				const FZoneGraphLaneSection& Section = Obstacle.LaneSection;
				const FVector ZOffset(0.f, 0.f, 1.f);
				UE::ZoneGraph::RenderingUtilities::AppendLaneSection(DebugProxy, *ZoneStorage, Section, FColor::Red, /*LineThickness*/5.0f, ZOffset);
			}
		}
	}
}
#endif // !UE_BUILD_SHIPPING && !UE_BUILD_TEST