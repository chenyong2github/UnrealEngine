// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/Foundation/FoundationActorDesc.h"

#if WITH_EDITOR
#include "Engine/Level.h"
#include "Foundation/FoundationActor.h"

FFoundationActorDesc::FFoundationActorDesc(const FWorldPartitionActorDescData& DescData, FName InLevelPackage)
	: FWorldPartitionActorDesc(DescData)
	, LevelPackage(InLevelPackage)
{}

FFoundationActorDesc::FFoundationActorDesc(AActor* InActor)
	: FWorldPartitionActorDesc(InActor)
{
	if (AFoundationActor* FoundationActor = CastChecked<AFoundationActor>(InActor))
	{
		LevelPackage = *FoundationActor->GetFoundationPackage();
	}
}

void FFoundationActorDesc::BuildHash(FHashBuilder& HashBuilder)
{
	FWorldPartitionActorDesc::BuildHash(HashBuilder);
	HashBuilder << LevelPackage;
}

#endif
