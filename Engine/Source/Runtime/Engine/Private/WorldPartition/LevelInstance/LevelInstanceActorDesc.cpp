// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LevelInstance/LevelInstanceActorDesc.h"

#if WITH_EDITOR
#include "Engine/Level.h"
#include "LevelInstance/LevelInstanceActor.h"

FLevelInstanceActorDesc::FLevelInstanceActorDesc(const FWorldPartitionActorDescData& DescData, FName InLevelPackage)
	: FWorldPartitionActorDesc(DescData)
	, LevelPackage(InLevelPackage)
{}

FLevelInstanceActorDesc::FLevelInstanceActorDesc(AActor* InActor)
	: FWorldPartitionActorDesc(InActor)
{
	if (ALevelInstance* LevelInstanceActor = CastChecked<ALevelInstance>(InActor))
	{
		LevelPackage = *LevelInstanceActor->GetWorldAssetPackage();
	}
}

void FLevelInstanceActorDesc::BuildHash(FHashBuilder& HashBuilder)
{
	FWorldPartitionActorDesc::BuildHash(HashBuilder);
	HashBuilder << LevelPackage;
}

#endif
