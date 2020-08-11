// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/Foundation/FoundationActorDescFactory.h"

#if WITH_EDITOR
#include "WorldPartition/Foundation/FoundationActorDesc.h"
#include "Foundation/FoundationActor.h"
#include "WorldPartition/WorldPartition.h"
#include "ActorRegistry.h"
#include "Editor.h"

FWorldPartitionActorDesc* FFoundationActorDescFactory::CreateInstance(const FWorldPartitionActorDescInitData& ActorDescInitData)
{
	FWorldPartitionActorDescData Data;
	if (!ReadMetaData(ActorDescInitData, Data))
	{
		return nullptr;
	}

	FString LevelPackageNameStr;
	static const FName NAME_FoundationPackage(TEXT("FoundationPackage"));
	if (!FActorRegistry::ReadActorMetaData(NAME_FoundationPackage, LevelPackageNameStr, ActorDescInitData.AssetData))
	{
		return nullptr;
	}

	FTransform FoundationTransform;
	static const FName NAME_FoundationTransform(TEXT("FoundationTransform"));
	if (!FActorRegistry::ReadActorMetaData(NAME_FoundationTransform, FoundationTransform, ActorDescInitData.AssetData))
	{
		return nullptr;
	}
	
	FName LevelPackage = FName(*LevelPackageNameStr);

	if (!LevelPackage.IsNone())
	{
		FBox LevelBounds;
		if (ULevel::GetLevelBoundsFromPackage(LevelPackage, LevelBounds))
		{
			FVector LevelBoundsLocation;
			LevelBounds.GetCenterAndExtents(Data.BoundsLocation, Data.BoundsExtent);

			//@todo_ow: This will result in a new BoundsExtent that is larger than it should. To fix this, we would need the Object Oriented BoundingBox of the actor (the BV of the actor without rotation)
			const FVector BoundsMin = Data.BoundsLocation - Data.BoundsExtent;
			const FVector BoundsMax = Data.BoundsLocation + Data.BoundsExtent;
			const FBox NewBounds = FBox(BoundsMin, BoundsMax).TransformBy(FoundationTransform);
			NewBounds.GetCenterAndExtents(Data.BoundsLocation, Data.BoundsExtent);
		}
	}

	return new FFoundationActorDesc(Data, LevelPackage);
}

FWorldPartitionActorDesc* FFoundationActorDescFactory::CreateInstance(AActor* InActor)
{
	return new FFoundationActorDesc(InActor);
}
#endif