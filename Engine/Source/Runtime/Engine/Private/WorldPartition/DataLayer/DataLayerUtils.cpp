// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerUtils.h"

#if WITH_EDITOR
#include "WorldPartition/DataLayer/WorldDataLayersActorDesc.h"
#include "WorldPartition/DataLayer/DataLayerInstanceWithAsset.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerType.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"

TArray<FName> FDataLayerUtils::ResolvedDataLayerInstanceNames(const FWorldPartitionActorDesc* InActorDesc, const FWorldDataLayersActorDesc* InWorldDataLayersActorDesc, UWorld* InWorld, bool* bOutIsResultValid)
{
	bool bLocalIsSuccess = true;
	bool& bIsSuccess = bOutIsResultValid ? *bOutIsResultValid : bLocalIsSuccess;
	bIsSuccess = true;

	// Prioritize in-memory AWorldDataLayers
	UWorld* World = InWorld;
	if (!World)
	{
		World = InActorDesc->GetContainer() ? InActorDesc->GetContainer()->GetWorld() : nullptr;
	}
	const UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(World);

	// DataLayers not using DataLayer Assets represent DataLayerInstanceNames
	if (!InActorDesc->IsUsingDataLayerAsset())
	{
		if (DataLayerSubsystem && DataLayerSubsystem->CanResolveDataLayers())
		{
			TArray<FName> Result;
			for (const FName& DataLayerInstanceName : InActorDesc->GetDataLayers())
			{
				if (const UDataLayerInstance* DataLayerInstance = DataLayerSubsystem->GetDataLayerInstance(DataLayerInstanceName))
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
		if (DataLayerSubsystem && DataLayerSubsystem->CanResolveDataLayers())
		{
			TArray<FName> Result;
			for (const FName& DataLayerAssetPath : InActorDesc->GetDataLayers())
			{
				DataLayerSubsystem->ForEachDataLayer([DataLayerAssetPath, &Result](UDataLayerInstance* DataLayerInstance)
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
bool FDataLayerUtils::ResolveRuntimeDataLayerInstanceNames(const FWorldPartitionActorDescView& InActorDescView, const FActorDescViewMap& ActorDescViewMap, TArray<FName>& OutRuntimeDataLayerInstanceNames)
{
	UWorld* World = InActorDescView.GetActorDesc()->GetContainer()->GetWorld();
	const UDataLayerSubsystem* DataLayerSubsystem = World ? World->GetSubsystem<UDataLayerSubsystem>() : nullptr;
	if (DataLayerSubsystem && DataLayerSubsystem->CanResolveDataLayers())
	{
		for (FName DataLayerInstanceName : InActorDescView.GetDataLayerInstanceNames())
		{
			const UDataLayerInstance* DataLayerInstance = DataLayerSubsystem->GetDataLayerInstance(DataLayerInstanceName);
			if (DataLayerInstance && DataLayerInstance->IsRuntime())
			{
				OutRuntimeDataLayerInstanceNames.Add(DataLayerInstanceName);
			}
		}

		return true;
	}
	else
	{
		// Fallback on FWorldDataLayersActorDesc
		TArray<const FWorldPartitionActorDescView*> WorldDataLayerViews = ActorDescViewMap.FindByExactNativeClass<AWorldDataLayers>();
	
		if (WorldDataLayerViews.Num())
		{
			check(WorldDataLayerViews.Num() == 1);
			const FWorldPartitionActorDescView* WorldDataLayersActorDescView = WorldDataLayerViews[0];
	
			FWorldDataLayersActorDesc* WorldDataLayersActorDesc = (FWorldDataLayersActorDesc*)WorldDataLayersActorDescView->GetActorDesc();

			for (FName DataLayerInstanceName : InActorDescView.GetDataLayerInstanceNames())
			{
				if (const FDataLayerInstanceDesc* DataLayerInstanceDesc = WorldDataLayersActorDesc->GetDataLayerInstanceFromInstanceName(DataLayerInstanceName))
				{
					if (DataLayerInstanceDesc->GetDataLayerType() == EDataLayerType::Runtime)
					{
						OutRuntimeDataLayerInstanceNames.Add(DataLayerInstanceName);
					}
				}
			}

			return true;
		}
	}

	return false;
}

#endif