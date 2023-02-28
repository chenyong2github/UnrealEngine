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
		bool bUseMaterialOverrides,
		const TArray<TSoftObjectPtr<UMaterialInterface>>& InMaterialOverrides,
		bool bInReverseCulling)
	{
		check(InstanceLists.Num() > 0);

		// First look through previously existing values - note that we scope this to prevent issues with the 0 index access which might become invalid below
		{
			const TArray<TSoftObjectPtr<UMaterialInterface>>& MaterialOverrides = (bUseMaterialOverrides ? InMaterialOverrides : InstanceLists[0].Descriptor.OverrideMaterials);
			for (FPCGMeshInstanceList& InstanceList : InstanceLists)
			{
				if (InstanceList.Descriptor.bReverseCulling == bInReverseCulling &&
					InstanceList.Descriptor.OverrideMaterials == MaterialOverrides)
				{
					return InstanceList;
				}
			}
		}

		FPCGMeshInstanceList& NewInstanceList = InstanceLists.Emplace_GetRef();
		NewInstanceList.Descriptor = InstanceLists[0].Descriptor;
		NewInstanceList.Descriptor.bReverseCulling = bInReverseCulling;

		if (bUseMaterialOverrides)
		{
			NewInstanceList.Descriptor.OverrideMaterials = InMaterialOverrides;
		}

		return NewInstanceList;
	}
}

FPCGMeshSelectorWeightedEntry::FPCGMeshSelectorWeightedEntry(TSoftObjectPtr<UStaticMesh> InMesh, int InWeight)
	: Weight(InWeight)
{
	Descriptor.StaticMesh = InMesh;
}

#if WITH_EDITOR
void FPCGMeshSelectorWeightedEntry::ApplyDeprecation()
{
	if (Mesh_DEPRECATED.IsValid() ||
		bOverrideCollisionProfile_DEPRECATED ||
		CollisionProfile_DEPRECATED.Name != UCollisionProfile::NoCollision_ProfileName ||
		bOverrideMaterials_DEPRECATED ||
		MaterialOverrides_DEPRECATED.Num() > 0 ||
		CullStartDistance_DEPRECATED != 0 ||
		CullEndDistance_DEPRECATED != 0 ||
		WorldPositionOffsetDisableDistance_DEPRECATED != 0)
	{
		Descriptor.StaticMesh = Mesh_DEPRECATED;
		
		if (bOverrideCollisionProfile_DEPRECATED)
		{
			Descriptor.bUseDefaultCollision = false;
			Descriptor.BodyInstance.SetCollisionProfileName(CollisionProfile_DEPRECATED.Name);
		}
		else
		{
			Descriptor.bUseDefaultCollision = true;
		}

		Descriptor.InstanceStartCullDistance = CullStartDistance_DEPRECATED;
		Descriptor.InstanceEndCullDistance = CullEndDistance_DEPRECATED;
		Descriptor.WorldPositionOffsetDisableDistance = WorldPositionOffsetDisableDistance_DEPRECATED;

		if (bOverrideMaterials_DEPRECATED)
		{
			Descriptor.OverrideMaterials = MaterialOverrides_DEPRECATED;
		}

		Mesh_DEPRECATED.Reset();
		bOverrideCollisionProfile_DEPRECATED = false;
		CollisionProfile_DEPRECATED = UCollisionProfile::NoCollision_ProfileName;
		bOverrideMaterials_DEPRECATED = false;
		MaterialOverrides_DEPRECATED.Reset();
		CullStartDistance_DEPRECATED = 0;
		CullEndDistance_DEPRECATED = 0;		
		WorldPositionOffsetDisableDistance_DEPRECATED = 0;
	}
}
#endif

void UPCGMeshSelectorWeighted::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	for (FPCGMeshSelectorWeightedEntry& Entry : MeshEntries)
	{
		Entry.ApplyDeprecation();
	}
#endif
}

void UPCGMeshSelectorWeighted::SelectInstances_Implementation(
	FPCGContext& Context, 
	const UPCGStaticMeshSpawnerSettings* Settings, 
	const UPCGPointData* InPointData,
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
		PickEntry.Emplace_GetRef(Entry.Descriptor);

		TotalWeight += Entry.Weight;
		CumulativeWeights.Add(TotalWeight);
	}

	if (TotalWeight <= 0)
	{
		return;
	}

	if (!InPointData)
	{
		PCGE_LOG_C(Error, &Context, "Missing input data");
		return;
	}

	TArray<FPCGPoint>* OutPoints = nullptr;
	FPCGMetadataAttribute<FString>* OutAttribute = nullptr;
	TMap<TSoftObjectPtr<UStaticMesh>, PCGMetadataValueKey> MeshToValueKey;

	FPCGMeshMaterialOverrideHelper MaterialOverrideHelper(Context, bUseAttributeMaterialOverrides, MaterialOverrideAttributes, InPointData->Metadata);

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

		for (const FPCGPoint& Point : InPointData->GetPoints())
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
				InstanceList.Instances.Emplace(Point.Transform);
				InstanceList.InstancesMetadataEntry.Emplace(Point.MetadataEntry);

				const TSoftObjectPtr<UStaticMesh>& Mesh = InstanceList.Descriptor.StaticMesh;

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

