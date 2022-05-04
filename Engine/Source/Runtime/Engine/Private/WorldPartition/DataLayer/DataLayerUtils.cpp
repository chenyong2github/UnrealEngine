// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerUtils.h"

#if WITH_EDITOR
#include "WorldPartition/DataLayer/WorldDataLayersActorDesc.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerInstanceWithAsset.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerType.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/ActorDescContainer.h"

FWorldDataLayersActorDesc* FDataLayerUtils::GetWorldDataLayersActorDesc(const UActorDescContainer* InContainer, bool bInCheckValid)
{
	if (InContainer)
	{
		for (FActorDescList::TIterator<AWorldDataLayers> Iterator(const_cast<UActorDescContainer*>(InContainer)); Iterator; ++Iterator)
		{
			if (!bInCheckValid || Iterator->IsValid())
			{
				FWorldDataLayersActorDesc* WorldDataLayersActorDesc = *Iterator;
				return WorldDataLayersActorDesc;
			}
			// No need to iterate (we assume that there's only 1 AWorldDataLayer for now)
			return nullptr;
		}
	}
	return nullptr;
}

TArray<FName> FDataLayerUtils::ResolvedDataLayerInstanceNames(const FWorldPartitionActorDesc* InActorDesc, const AWorldDataLayers* InWorldDataLayers, const FWorldDataLayersActorDesc* InWorldDataLayersActorDesc, bool* bOutIsResultValid)
{
	bool bLocalIsSuccess = true;
	bool& bIsSuccess = bOutIsResultValid ? *bOutIsResultValid : bLocalIsSuccess;
	bIsSuccess = true;

	// Prioritize in-memory AWorldDataLayers
	const AWorldDataLayers* WorldDataLayers = InWorldDataLayers;
	if (!WorldDataLayers)
	{
		UWorld* World = InActorDesc->GetContainer() ? InActorDesc->GetContainer()->GetWorld() : nullptr;
		WorldDataLayers = World ? World->GetWorldDataLayers() : nullptr;
	}

	// DataLayers not using DataLayer Assets represent DataLayerInstanceNames
	if (!InActorDesc->IsUsingDataLayerAsset())
	{
		if (WorldDataLayers)
		{
			TArray<FName> Result;
			for (const FName& DataLayerInstanceName : InActorDesc->GetDataLayers())
			{
				if (const UDataLayerInstance* DataLayerInstance = WorldDataLayers->GetDataLayerInstance(DataLayerInstanceName))
				{
					Result.Add(DataLayerInstanceName);
				}
			}
			return Result;
		}
		// Fallback on FWorldDataLayersActorDesc
		else if (InWorldDataLayersActorDesc)
		{
			TArray<FName> Result;
			for (const FName& DataLayerInstanceName : InActorDesc->GetDataLayers())
			{
				if (const FDataLayerInstanceDesc* DataLayerInstanceDesc = InWorldDataLayersActorDesc->GetDataLayerInstanceFromInstanceName(DataLayerInstanceName))
				{
					Result.Add(DataLayerInstanceName);
				}
			}
			return Result;
		}
	}
	// ActorDesc DataLayers represents DataLayer Asset Paths
	else
	{
		if (WorldDataLayers)
		{
			TArray<FName> Result;
			for (const FName& DataLayerAssetPath : InActorDesc->GetDataLayers())
			{
				WorldDataLayers->ForEachDataLayer([DataLayerAssetPath, &Result](UDataLayerInstance* DataLayerInstance)
				{
					const UDataLayerInstanceWithAsset* DataLayerInstanceWithAsset = Cast<UDataLayerInstanceWithAsset>(DataLayerInstance);
					const UDataLayerAsset* DataLayerAsset = DataLayerInstanceWithAsset ? DataLayerInstanceWithAsset->GetAsset() : nullptr;
					if (DataLayerAsset && (FName(DataLayerAsset->GetPathName()) == DataLayerAssetPath))
					{
						Result.Add(DataLayerInstance->GetDataLayerFName());
						return false;
					}
					return true;
				});
			}
			return Result;
		}
		// Fallback on FWorldDataLayersActorDesc
		else if (InWorldDataLayersActorDesc)
		{
			TArray<FName> Result;
			for (const FName& DataLayerAssetPath : InActorDesc->GetDataLayers())
			{
				if (const FDataLayerInstanceDesc* DataLayerInstanceDesc = InWorldDataLayersActorDesc->GetDataLayerInstanceFromAssetPath(DataLayerAssetPath))
				{
					Result.Add(DataLayerInstanceDesc->GetName());
				}
			}
			return Result;
		}
	}

	bIsSuccess = false;
	return InActorDesc->GetDataLayers();
}

// For performance reasons, this function assumes that InActorDesc's DataLayerInstanceNames was already resolved.
TArray<FName> FDataLayerUtils::ResolveRuntimeDataLayerInstanceNames(const FWorldPartitionActorDesc* InActorDesc, const UActorDescContainer* InContainer, bool* bOutIsResultValid)
{
	bool bLocalIsSuccess = false;
	bool& bIsSuccess = bOutIsResultValid ? *bOutIsResultValid : bLocalIsSuccess;
	bIsSuccess = false;

	TArray<FName> Result;
	bIsSuccess = InActorDesc->GetDataLayerInstanceNames().IsEmpty();
	if (bIsSuccess)
	{
		return Result;
	}

	const UActorDescContainer* Container = InContainer ? InContainer : InActorDesc->GetContainer();
	if (Container)
	{
		UWorld* World = Container->GetWorld();
		if (AWorldDataLayers* WorldDataLayers = World ? World->GetWorldDataLayers() : nullptr)
		{
			bIsSuccess = true;
			for (FName DataLayerInstanceName : InActorDesc->GetDataLayerInstanceNames())
			{
				const UDataLayerInstance* DataLayerInstance = WorldDataLayers->GetDataLayerInstance(DataLayerInstanceName);
				if (DataLayerInstance && DataLayerInstance->IsRuntime())
				{
					Result.Add(DataLayerInstanceName);
				}
			}
		}
		// Fallback on FWorldDataLayersActorDesc
		else if (FWorldDataLayersActorDesc* WorldDataLayersActorDesc = FDataLayerUtils::GetWorldDataLayersActorDesc(Container))
		{
			bIsSuccess = true;
			for (FName DataLayerInstanceName : InActorDesc->GetDataLayerInstanceNames())
			{
				const FDataLayerInstanceDesc* DataLayerInstanceDesc = WorldDataLayersActorDesc->GetDataLayerInstanceFromInstanceName(DataLayerInstanceName);
				if (DataLayerInstanceDesc && (DataLayerInstanceDesc->GetDataLayerType() == EDataLayerType::Runtime))
				{
					Result.Add(DataLayerInstanceName);
				}
			}
		}
	}
	return Result;
}

#endif