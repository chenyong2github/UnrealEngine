// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LevelInstance/LevelInstanceActorDescFactory.h"

#if WITH_EDITOR
#include "WorldPartition/LevelInstance/LevelInstanceActorDesc.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "WorldPartition/WorldPartition.h"
#include "ActorRegistry.h"
#include "Editor.h"

FWorldPartitionActorDesc* FLevelInstanceActorDescFactory::CreateInstance(const FWorldPartitionActorDescInitData& ActorDescInitData)
{
	FWorldPartitionActorDescData Data;
	if (!ReadMetaData(ActorDescInitData, Data))
	{
		return nullptr;
	}

	FString LevelPackageNameStr;
	static const FName NAME_LevelInstancePackage(TEXT("LevelInstancePackage"));
	if (!FActorRegistry::ReadActorMetaData(NAME_LevelInstancePackage, LevelPackageNameStr, ActorDescInitData.AssetData))
	{
		return nullptr;
	}

	FTransform LevelInstanceTransform;
	static const FName NAME_LevelInstanceTransform(TEXT("LevelInstanceTransform"));
	if (!FActorRegistry::ReadActorMetaData(NAME_LevelInstanceTransform, LevelInstanceTransform, ActorDescInitData.AssetData))
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
			const FBox NewBounds = FBox(BoundsMin, BoundsMax).TransformBy(LevelInstanceTransform);
			NewBounds.GetCenterAndExtents(Data.BoundsLocation, Data.BoundsExtent);
		}
	}

	return new FLevelInstanceActorDesc(Data, LevelPackage);
}

FWorldPartitionActorDesc* FLevelInstanceActorDescFactory::CreateInstance(AActor* InActor)
{
	return new FLevelInstanceActorDesc(InActor);
}
#endif