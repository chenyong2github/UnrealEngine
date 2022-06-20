// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSelectors/PCGMeshSelectorWeightedByCategory.h"

#include "PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Elements/PCGPointFilter.h"
#include "Elements/PCGStaticMeshSpawner.h"

#include "Math/RandomStream.h"

struct FPCGInstancesAndWeights
{
	TArray<int> InstanceListIndices;
	TArray<int> CumulativeWeights;
};

void UPCGMeshSelectorWeightedByCategory::SelectInstances_Implementation(
	FPCGContext& Context,
	const UPCGStaticMeshSpawnerSettings* Settings,
	const UPCGSpatialData* InSpatialData,
	TArray<FPCGMeshInstanceList>& OutMeshInstances) const
{
	const UPCGPointData* PointData = InSpatialData->ToPointData(&Context);

	if (!PointData)
	{
		PCGE_LOG_C(Error, &Context, "Unable to get point data from input");
		return;
	}

	if (!PointData->Metadata)
	{
		PCGE_LOG_C(Error, &Context, "Unable to get metadata from input");
		return;
	}

	if (!PointData->Metadata->HasAttribute(CategoryAttribute))
	{
		PCGE_LOG_C(Error, &Context, "Attribute %s is not in the metadata", *CategoryAttribute.ToString());
		return;
	}

	const FPCGMetadataAttributeBase* AttributeBase = PointData->Metadata->GetConstAttribute(CategoryAttribute);
	check(AttributeBase);

	// TODO: support enum type as well
	if (AttributeBase->GetTypeId() != PCG::Private::MetadataTypes<FString>::Id)
	{
		PCGE_LOG_C(Error, &Context, "Attribute is not of valid type FString");
		return;
	}

	const FPCGMetadataAttribute<FString>* Attribute = static_cast<const FPCGMetadataAttribute<FString>*>(AttributeBase);

	// maps a CategoryEntry ValueKey to the meshes and precomputed weight data 
	TMap<PCGMetadataValueKey, FPCGInstancesAndWeights> CategoryEntryToInstancesAndWeights;

	// unmarked points will fallback to the MeshEntries associated with the DefaultValueKey
	PCGMetadataValueKey DefaultValueKey = PCGDefaultValueKey;

	for (const FPCGWeightedByCategoryEntryList& Entry : Entries)
	{
		if (Entry.WeightedMeshEntries.Num() == 0)
		{
			PCGE_LOG_C(Verbose, &Context, "Empty entry found in category %s", *Entry.CategoryEntry);
			continue;
		}

		PCGMetadataValueKey ValueKey = Attribute->FindValue<FString>(Entry.CategoryEntry);

		if (ValueKey == PCGDefaultValueKey)
		{
			PCGE_LOG_C(Verbose, &Context, "Invalid category %s", *Entry.CategoryEntry);
			continue;
		}

		FPCGInstancesAndWeights* InstancesAndWeights = CategoryEntryToInstancesAndWeights.Find(ValueKey);

		if (InstancesAndWeights)
		{
			PCGE_LOG_C(Warning, &Context, "Duplicate entry found in category %s. Subsequent entries are ignored.", *Entry.CategoryEntry);
			continue;
		}

		if (Entry.IsDefault)
		{
			if (DefaultValueKey == PCGDefaultValueKey)
			{
				DefaultValueKey = ValueKey;
			}
			else
			{
				PCGE_LOG_C(Warning, &Context, "Duplicate default entry found. Subsequent default entries are ignored.");
			}
		}

		InstancesAndWeights = &CategoryEntryToInstancesAndWeights.Add(ValueKey, FPCGInstancesAndWeights());

		int TotalWeight = 0;
		for (const FPCGMeshSelectorWeightedEntry& WeightedEntry : Entry.WeightedMeshEntries)
		{
			if (WeightedEntry.Weight <= 0)
			{
				PCGE_LOG_C(Verbose, &Context, "Entry found with weight <= 0 in category %s", *Entry.CategoryEntry);
				continue;
			}
			
			int32 Index = INDEX_NONE;
			FindOrAddInstanceList(OutMeshInstances, WeightedEntry.Mesh, WeightedEntry.bOverrideCollisionProfile, WeightedEntry.CollisionProfile, WeightedEntry.bOverrideMaterials, WeightedEntry.MaterialOverrides, Index);

			// precompute the weights
			TotalWeight += WeightedEntry.Weight;
			InstancesAndWeights->CumulativeWeights.Add(TotalWeight);
			InstancesAndWeights->InstanceListIndices.Add(Index);
		}
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute::SelectEntries);

	// Assign points to entries
	for (const FPCGPoint& Point : PointData->GetPoints())
	{
		if (Point.Density <= 0.0f)
		{
			continue;
		}

		PCGMetadataValueKey ValueKey = Attribute->GetValueKey(Point.MetadataEntry);

		// if no mesh list was processed for this attribute value, fallback to the default mesh list
		FPCGInstancesAndWeights* InstancesAndWeights = CategoryEntryToInstancesAndWeights.Find(ValueKey);
		if (!InstancesAndWeights)
		{
			if (DefaultValueKey != PCGDefaultValueKey)
			{
				InstancesAndWeights = CategoryEntryToInstancesAndWeights.Find(DefaultValueKey);
				check(InstancesAndWeights);
			}
			else
			{
				continue;
			}
		}

		const int TotalWeight = InstancesAndWeights->CumulativeWeights.Last();

		FRandomStream RandomSource(PCGHelpers::ComputeSeed(Point.Seed, Settings->Seed));
		int RandomWeightedPick = RandomSource.RandRange(0, TotalWeight - 1);

		int RandomPick = 0;
		while (RandomPick < InstancesAndWeights->InstanceListIndices.Num() && InstancesAndWeights->CumulativeWeights[RandomPick] <= RandomWeightedPick)
		{
			++RandomPick;
		}

		if (RandomPick < InstancesAndWeights->InstanceListIndices.Num())
		{
			const int32 Index = InstancesAndWeights->InstanceListIndices[RandomPick];
			OutMeshInstances[Index].Instances.Emplace(Point.Transform);
		}
	}
}
