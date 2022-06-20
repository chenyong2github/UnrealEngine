// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSelectors/PCGMeshSelectorWeighted.h"

#include "PCGHelpers.h"
#include "PCGPoint.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include "Math/RandomStream.h"

void UPCGMeshSelectorWeighted::SelectInstances_Implementation(
	FPCGContext& Context, 
	const UPCGStaticMeshSpawnerSettings* Settings, 
	const UPCGSpatialData* InSpatialData,
	TArray<FPCGMeshInstanceList>& OutMeshInstances) const
{
	TArray<int> CumulativeWeights;

	int TotalWeight = 0;

	for (const FPCGMeshSelectorWeightedEntry& Entry : MeshEntries)
	{
		if (Entry.Weight <= 0)
		{
			PCGE_LOG_C(Verbose, &Context, "Entry found with weight <= 0");
			continue;
		}

		int32 Index = INDEX_NONE;
		FindOrAddInstanceList(OutMeshInstances, Entry.Mesh, Entry.bOverrideCollisionProfile, Entry.CollisionProfile, Entry.bOverrideMaterials, Entry.MaterialOverrides, Index);
		TotalWeight += Entry.Weight;
		CumulativeWeights.Add(TotalWeight);
	}

	if (TotalWeight <= 0)
	{
		return;
	}

	const UPCGPointData* PointData = InSpatialData->ToPointData(&Context);

	if (!PointData)
	{
		PCGE_LOG_C(Error, &Context, "Unable to get point data from input");
		return;
	}

	// Assign points to entries
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute::SelectEntries);

		for (const FPCGPoint& Point : PointData->GetPoints()) 
		{
			if (Point.Density <= 0.0f)
			{
				continue;
			}

			FRandomStream RandomSource(PCGHelpers::ComputeSeed(Point.Seed, Settings->Seed));
			int RandomWeightedPick = RandomSource.RandRange(0, TotalWeight - 1);

			int RandomPick = 0;
			while (RandomPick < OutMeshInstances.Num() && CumulativeWeights[RandomPick] <= RandomWeightedPick)
			{
				++RandomPick;
			}

			if (RandomPick < OutMeshInstances.Num())
			{
				OutMeshInstances[RandomPick].Instances.Emplace(Point.Transform);
			}
		}
	}
}
