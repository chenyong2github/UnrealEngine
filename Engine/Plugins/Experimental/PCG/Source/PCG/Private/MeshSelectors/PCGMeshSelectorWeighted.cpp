// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSelectors/PCGMeshSelectorWeighted.h"

#include "Data/PCGPointData.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGBlueprintHelpers.h"

#include "Math/RandomStream.h"
#include "MeshSelectors/PCGMeshSelectorBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMeshSelectorWeighted)

namespace PCGMeshSelectorWeighted
{
	FPCGMeshInstanceList& GetInstanceList(
		TArray<FPCGMeshInstanceList>& InstanceLists,
		bool bUseAttributeMaterialOverrides,
		const TArray<TSoftObjectPtr<UMaterialInterface>>& InMaterialOverrides,
		bool bInIsLocalToWorldDeterminantNegative)
	{
		check(InstanceLists.Num() > 0);
		const bool bOverrideMaterials = (bUseAttributeMaterialOverrides || InstanceLists[0].bOverrideMaterials);

		// First look through previously existing values - note that we scope this to prevent issues with the 0 index access which might become invalid below
		{
			const TArray<TSoftObjectPtr<UMaterialInterface>>& MaterialOverrides = (bUseAttributeMaterialOverrides ? InMaterialOverrides : InstanceLists[0].MaterialOverrides);

			for (int Index = 0; Index < InstanceLists.Num(); ++Index)
			{
				if (InstanceLists[Index].bOverrideMaterials != bOverrideMaterials)
				{
					continue;
				}

				if (InstanceLists[Index].bIsLocalToWorldDeterminantNegative != bInIsLocalToWorldDeterminantNegative)
				{
					continue;
				}

				if (bOverrideMaterials && InstanceLists[Index].MaterialOverrides != MaterialOverrides)
				{
					continue;
				}

				return InstanceLists[Index];
			}
		}

		// If not found, then copy first entry which is our "clean" version and apply the new values
		{
			FPCGMeshInstanceList& NewInstanceList = InstanceLists.Emplace_GetRef();
			const FPCGMeshInstanceList& SourceInstanceList = InstanceLists[0];
			NewInstanceList.Mesh = SourceInstanceList.Mesh;
			NewInstanceList.bOverrideCollisionProfile = SourceInstanceList.bOverrideCollisionProfile;
			NewInstanceList.CollisionProfile = SourceInstanceList.CollisionProfile;
			NewInstanceList.CullStartDistance = SourceInstanceList.CullStartDistance;
			NewInstanceList.CullEndDistance = SourceInstanceList.CullEndDistance;

			NewInstanceList.bOverrideMaterials = bOverrideMaterials;

			const TArray<TSoftObjectPtr<UMaterialInterface>>& MaterialOverrides = (bUseAttributeMaterialOverrides ? InMaterialOverrides : SourceInstanceList.MaterialOverrides);
			NewInstanceList.MaterialOverrides = MaterialOverrides;
			NewInstanceList.bIsLocalToWorldDeterminantNegative = bInIsLocalToWorldDeterminantNegative;

			return NewInstanceList;
		}
	}
}

void UPCGMeshSelectorWeighted::SelectInstances_Implementation(
	FPCGContext& Context, 
	const UPCGStaticMeshSpawnerSettings* Settings, 
	const UPCGSpatialData* InSpatialData,
	TArray<FPCGMeshInstanceList>& OutMeshInstances,
	UPCGPointData* OutPointData) const
{
	TArray<TArray<FPCGMeshInstanceList>> MeshInstances;
	TArray<int> CumulativeWeights;

	int TotalWeight = 0;

	// Prepare common mesh setups which we will use as a kind of map
	for (const FPCGMeshSelectorWeightedEntry& Entry : MeshEntries)
	{
		if (Entry.Weight <= 0)
		{
			PCGE_LOG_C(Verbose, &Context, "Entry found with weight <= 0");
			continue;
		}

		TArray<FPCGMeshInstanceList>& PickEntry = MeshInstances.Emplace_GetRef();
		FindOrAddInstanceList(PickEntry, Entry.Mesh, Entry.bOverrideCollisionProfile, Entry.CollisionProfile, Entry.bOverrideMaterials, Entry.MaterialOverrides, Entry.CullStartDistance, Entry.CullEndDistance, /*bReverseCulling=*/false);

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

	TArray<FPCGPoint>* OutPoints = nullptr;
	FPCGMetadataAttribute<FString>* OutAttribute = nullptr;
	TMap<TSoftObjectPtr<UStaticMesh>, PCGMetadataValueKey> MeshToValueKey;

	FPCGMeshMaterialOverrideHelper MaterialOverrideHelper(Context, bUseAttributeMaterialOverrides, MaterialOverrideAttributes, PointData->Metadata);

	if (!MaterialOverrideHelper.IsValid())
	{
		return;
	}

	if (OutPointData)
	{
		check(OutPointData->Metadata);

		if (!OutPointData->Metadata->HasAttribute(Settings->OutAttributeName)) 
		{
			PCGE_LOG_C(Error, &Context, "Out attribute %s is not in the metadata", *Settings->OutAttributeName.ToString());
		}

		FPCGMetadataAttributeBase* OutAttributeBase = OutPointData->Metadata->GetMutableAttribute(Settings->OutAttributeName);

		if (OutAttributeBase)
		{
			if (OutAttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FString>::Id)
			{
				OutAttribute = static_cast<FPCGMetadataAttribute<FString>*>(OutAttributeBase);
				OutPoints = &OutPointData->GetMutablePoints();
			}
			else
			{
				PCGE_LOG_C(Error, &Context, "Out attribute is not of valid type FString");
			}
		}
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

			FRandomStream RandomSource = UPCGBlueprintHelpers::GetRandomStream(Point, Settings, Context.SourceComponent.Get());
			int RandomWeightedPick = RandomSource.RandRange(0, TotalWeight - 1);

			int RandomPick = 0;
			while(RandomPick < MeshInstances.Num() && CumulativeWeights[RandomPick] <= RandomWeightedPick)
			{
				++RandomPick;
			}

			if(RandomPick < MeshInstances.Num())
			{
				const bool bNeedsReverseCulling = (Point.Transform.GetDeterminant() < 0);
				FPCGMeshInstanceList& InstanceList = PCGMeshSelectorWeighted::GetInstanceList(MeshInstances[RandomPick], bUseAttributeMaterialOverrides, MaterialOverrideHelper.GetMaterialOverrides(Point.MetadataEntry), bNeedsReverseCulling);
				InstanceList.Instances.Emplace(Point);

				const TSoftObjectPtr<UStaticMesh>& Mesh = InstanceList.Mesh;

				if (OutPointData && OutAttribute)
				{
					PCGMetadataValueKey* OutValueKey = MeshToValueKey.Find(Mesh);
					if(!OutValueKey)
					{
						PCGMetadataValueKey ValueKey = OutAttribute->AddValue(Mesh.ToSoftObjectPath().ToString());
						OutValueKey = &MeshToValueKey.Add(Mesh, ValueKey);
					}
					
					check(OutValueKey);
					
					FPCGPoint& OutPoint = OutPoints->Add_GetRef(Point);
					OutPointData->Metadata->InitializeOnSet(OutPoint.MetadataEntry);
					OutAttribute->SetValueFromValueKey(OutPoint.MetadataEntry, *OutValueKey);
				}
			}
		}
	}

	// Collapse to OutMeshInstances
	for (TArray<FPCGMeshInstanceList>& PickedMeshInstances : MeshInstances)
	{
		for (FPCGMeshInstanceList& PickedMeshInstanceEntry : PickedMeshInstances)
		{
			OutMeshInstances.Emplace(MoveTemp(PickedMeshInstanceEntry));
		}
	}
}

