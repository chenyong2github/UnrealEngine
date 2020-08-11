// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDescFactory.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "ActorRegistry.h"
#include "Algo/Transform.h"
#include "Algo/RemoveIf.h"

bool FWorldPartitionActorDescFactory::ReadMetaData(const FWorldPartitionActorDescInitData& ActorDescInitData, FWorldPartitionActorDescData& OutData)
{
	OutData.ActorPackage = ActorDescInitData.PackageName;
	OutData.ActorPath = ActorDescInitData.ActorPath;
	OutData.Class = ActorDescInitData.NativeClass->GetFName();

	static FName NAME_ActorGuid(TEXT("ActorGuid"));
	if (!FActorRegistry::ReadActorMetaData(NAME_ActorGuid, OutData.Guid, ActorDescInitData.AssetData))
	{
		return false;
	}
		
	static FName NAME_BoundsLocation(TEXT("BoundsLocation"));
	if (!FActorRegistry::ReadActorMetaData(NAME_BoundsLocation, OutData.BoundsLocation, ActorDescInitData.AssetData))
	{
		return false;
	}

	static FName NAME_BoundsExtent(TEXT("BoundsExtent"));
	if (!FActorRegistry::ReadActorMetaData(NAME_BoundsExtent, OutData.BoundsExtent, ActorDescInitData.AssetData))
	{
		return false;
	}

	// Transform BoundsLocation and BoundsExtent if necessary
	if (!ActorDescInitData.Transform.Equals(FTransform::Identity))
	{
		//@todo_ow: This will result in a new BoundsExtent that is larger than it should. To fix this, we would need the Object Oriented BoundingBox of the actor (the BV of the actor without rotation)
		const FVector BoundsMin = OutData.BoundsLocation - OutData.BoundsExtent;
		const FVector BoundsMax = OutData.BoundsLocation + OutData.BoundsExtent;
		const FBox NewBounds = FBox(BoundsMin, BoundsMax).TransformBy(ActorDescInitData.Transform);
		NewBounds.GetCenterAndExtents(OutData.BoundsLocation, OutData.BoundsExtent);
	}

	const EActorGridPlacement DefaultGridPlacement = ActorDescInitData.NativeClass->GetDefaultObject<AActor>()->GetDefaultGridPlacement();
	if (DefaultGridPlacement != EActorGridPlacement::None)
	{
		OutData.GridPlacement = DefaultGridPlacement;
	}
	else
	{
		int32 GridPlacementValue = (int32)EActorGridPlacement::AlwaysLoaded;
		static const FName NAME_GridPlacement(TEXT("GridPlacement"));
		FActorRegistry::ReadActorMetaData(NAME_GridPlacement, GridPlacementValue, ActorDescInitData.AssetData);
		OutData.GridPlacement = (EActorGridPlacement)GridPlacementValue;
	}

	static const FName NAME_RuntimeGrid(TEXT("RuntimeGrid"));
	FActorRegistry::ReadActorMetaData(NAME_RuntimeGrid, OutData.RuntimeGrid, ActorDescInitData.AssetData);

	static const FName NAME_IsEditorOnly(TEXT("IsEditorOnly"));
	FActorRegistry::ReadActorMetaData(NAME_IsEditorOnly, OutData.bActorIsEditorOnly, ActorDescInitData.AssetData);

	static const FName NAME_IsLevelBoundsRelevant(TEXT("IsLevelBoundsRelevant"));
	if (!FActorRegistry::ReadActorMetaData(NAME_IsLevelBoundsRelevant, OutData.bLevelBoundsRelevant, ActorDescInitData.AssetData))
	{
		OutData.bLevelBoundsRelevant = true;
	}
	
	FString LayersStr;
	static FName NAME_Layers(TEXT("Layers"));
	if (FActorRegistry::ReadActorMetaData(NAME_Layers, LayersStr, ActorDescInitData.AssetData))
	{
		TArray<FString> Layers;
		if (LayersStr.ParseIntoArray(Layers, TEXT(";")))
		{
			Algo::Transform(Layers, OutData.Layers, [&](const FString& Layer) { return FName(*Layer); });
		}
	}

	FString ActorsRefsStr;
	static const FName NAME_ActorReferences(TEXT("ActorReferences"));
	if (FActorRegistry::ReadActorMetaData(NAME_ActorReferences, ActorsRefsStr, ActorDescInitData.AssetData))
	{
		TArray<FString> ActorsRefs;
		if (ActorsRefsStr.ParseIntoArray(ActorsRefs, TEXT(";")))
		{
			Algo::Transform(ActorsRefs, OutData.References, [](const FString& ActorGuidStr)
			{
				FGuid ActorGuid;
				if (!FGuid::Parse(ActorGuidStr, ActorGuid))
				{
					ActorGuid.Invalidate();
				}
				return ActorGuid;
			});

			Algo::RemoveIf(OutData.References, [](const FGuid& Guid)
			{
				return !Guid.IsValid();
			});
		}
	}

	return true;
}

FWorldPartitionActorDesc* FWorldPartitionActorDescFactory::Create(const FWorldPartitionActorDescInitData& ActorDescInitData)
{
	if (FWorldPartitionActorDesc* NewDesc = CreateInstance(ActorDescInitData))
	{
		PostCreate(NewDesc);
		return NewDesc;
	}

	return nullptr;
}

FWorldPartitionActorDesc* FWorldPartitionActorDescFactory::Create(AActor* InActor)
{
	if (FWorldPartitionActorDesc* NewDesc = CreateInstance(InActor))
	{
		PostCreate(NewDesc);
		return NewDesc;
	}

	return nullptr;
}

void FWorldPartitionActorDescFactory::PostCreate(FWorldPartitionActorDesc* ActorDesc)
{
	ActorDesc->UpdateHash();
}

FWorldPartitionActorDesc* FWorldPartitionActorDescFactory::CreateInstance(const FWorldPartitionActorDescInitData& ActorDescInitData)
{
	FWorldPartitionActorDescData OutData;
	if (!ReadMetaData(ActorDescInitData, OutData))
	{
		return nullptr;
	}

	return new FWorldPartitionActorDesc(OutData);
}

FWorldPartitionActorDesc* FWorldPartitionActorDescFactory::CreateInstance(AActor* InActor)
{
	return new FWorldPartitionActorDesc(InActor);
}

#endif