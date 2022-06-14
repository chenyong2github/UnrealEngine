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

struct FPCGMeshesAndWeights
{
	TArray<TSoftObjectPtr<UStaticMesh>> Meshes;
	TArray<int> CumulativeWeights;
};

void UPCGMeshSelectorWeightedByCategory::SelectInstances_Implementation(
	FPCGContext& Context, 
	const UPCGStaticMeshSpawnerSettings* Settings, 
	const UPCGSpatialData* InSpatialData,
	TMap<TSoftObjectPtr<UStaticMesh>, FPCGMeshInstanceList>& OutMeshInstances) const
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
	TMap<PCGMetadataValueKey, FPCGMeshesAndWeights> CategoryEntryToMeshesAndWeights;

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

		FPCGMeshesAndWeights* MeshesAndWeights = CategoryEntryToMeshesAndWeights.Find(ValueKey);

		if (MeshesAndWeights)
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

		MeshesAndWeights = &CategoryEntryToMeshesAndWeights.Add(ValueKey, FPCGMeshesAndWeights());

		int TotalWeight = 0;
		for (const FPCGMeshSelectorWeightedEntry& WeightedEntry : Entry.WeightedMeshEntries)
		{
			if (WeightedEntry.Weight <= 0)
			{
				PCGE_LOG_C(Verbose, &Context, "Entry found with weight <= 0 in category %s", *Entry.CategoryEntry);
				continue;
			}

			// create or overwrite the entry for this mesh
			FPCGMeshInstanceList& InstanceList = OutMeshInstances.Emplace(WeightedEntry.Mesh);
			InstanceList.bOverrideCollisionProfile = WeightedEntry.bOverrideCollisionProfile;
			InstanceList.CollisionProfile = WeightedEntry.CollisionProfile;

			// precompute the weights
			TotalWeight += WeightedEntry.Weight;
			MeshesAndWeights->CumulativeWeights.Add(TotalWeight);
			MeshesAndWeights->Meshes.Add(WeightedEntry.Mesh);
		}
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute::SelectEntries);

	for (const FPCGPoint& Point : PointData->GetPoints())
	{
		if (Point.Density <= 0.0f)
		{
			continue;
		}

		PCGMetadataValueKey ValueKey = Attribute->GetValueKey(Point.MetadataEntry);

		// if no mesh list was processed for this attribute value, fallback to the default mesh list
		FPCGMeshesAndWeights* MeshesAndWeights = CategoryEntryToMeshesAndWeights.Find(ValueKey);
		if (!MeshesAndWeights)
		{
			if (DefaultValueKey != PCGDefaultValueKey)
			{
				MeshesAndWeights = CategoryEntryToMeshesAndWeights.Find(DefaultValueKey);
				check(MeshesAndWeights);
			}
			else
			{
				continue;
			}
		}

		const int TotalWeight = MeshesAndWeights->CumulativeWeights.Last();

		FRandomStream RandomSource(PCGHelpers::ComputeSeed(Point.Seed, Settings->Seed));
		int RandomWeightedPick = RandomSource.RandRange(0, TotalWeight - 1);

		int RandomPick = 0;
		while (RandomPick < MeshesAndWeights->Meshes.Num() && MeshesAndWeights->CumulativeWeights[RandomPick] <= RandomWeightedPick)
		{
			++RandomPick;
		}

		if (RandomPick < MeshesAndWeights->Meshes.Num())
		{
			OutMeshInstances[MeshesAndWeights->Meshes[RandomPick]].Instances.Emplace(Point.Transform);
		}
	}
}
