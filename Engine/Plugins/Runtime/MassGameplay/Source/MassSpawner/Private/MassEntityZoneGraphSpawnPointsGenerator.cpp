// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityZoneGraphSpawnPointsGenerator.h"
#include "ZoneGraphData.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphQuery.h"
#include "MassSpawnerTypes.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"

void UMassEntityZoneGraphSpawnPointsGenerator::GenerateSpawnPoints(UObject& QueryOwner, int32 Count, FFinishedGeneratingSpawnPointsSignature& FinishedGeneratingSpawnPointsDelegate) const
{
	TArray<FVector> Locations;
	if (const UWorld* World = QueryOwner.GetWorld())
	{
		if (const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(World))
		{
			const FRandomStream RandomStream(GFrameNumber);
			const TConstArrayView<FRegisteredZoneGraphData> RegisteredZoneGraphs = ZoneGraph->GetRegisteredZoneGraphData();
			for (const FRegisteredZoneGraphData& Registered : RegisteredZoneGraphs)
			{
				if (Registered.bInUse && Registered.ZoneGraphData)
				{
					GeneratePointsForZoneGraphData(*Registered.ZoneGraphData, Locations, RandomStream);
				}
			}

			// Randomize them
			for (int32 I = 0; I < Locations.Num(); ++I)
			{
				const int32 J = RandomStream.RandHelper(Locations.Num());
				Locations.Swap(I, J);
			}

			// If we generated too many, shrink it.
			if (Locations.Num() > Count)
			{
				Locations.SetNum(Count);
			}
		}
		else
		{
			UE_VLOG_UELOG(&QueryOwner, LogMassSpawner, Error, TEXT("No zone graph found in world"));
		}
	}
	else
	{
		UE_VLOG_UELOG(&QueryOwner, LogMassSpawner, Error, TEXT("Unable to retrieve world from QueryOwner"));
	}

	FinishedGeneratingSpawnPointsDelegate.Execute(Locations);
}

void UMassEntityZoneGraphSpawnPointsGenerator::GeneratePointsForZoneGraphData(const ::AZoneGraphData& ZoneGraphData, TArray<FVector>& Locations, const FRandomStream& RandomStream) const
{
	const FZoneGraphStorage& ZoneGraphStorage = ZoneGraphData.GetStorage();

	// Loop through all lanes
	for (int32 LaneIndex = 0; LaneIndex < ZoneGraphStorage.Lanes.Num(); ++LaneIndex)
	{
		const FZoneLaneData& Lane = ZoneGraphStorage.Lanes[LaneIndex];
		const float LaneHalfWidth = Lane.Width / 2.0f;
		if (TagFilter.Pass(Lane.Tags))
		{
			float LaneLength = 0.0f;
			UE::ZoneGraph::Query::GetLaneLength(ZoneGraphStorage, LaneIndex, LaneLength);

			float Distance = RandomStream.FRandRange(MinGap, MaxGap); // ..initially
			while (Distance <= LaneLength)
			{
				// Add location at the center of this space.
				FZoneGraphLaneLocation LaneLocation;
				UE::ZoneGraph::Query::CalculateLocationAlongLane(ZoneGraphStorage, LaneIndex, Distance, LaneLocation);
				const FVector Perp = LaneLocation.Direction ^ LaneLocation.Up;
				Locations.Add(LaneLocation.Position + Perp * RandomStream.FRandRange(-LaneHalfWidth, LaneHalfWidth));

				// Advance ahead past the space we just consumed, plus a random gap.
				Distance += RandomStream.FRandRange(MinGap, MaxGap);
			}
		}
	}
}
