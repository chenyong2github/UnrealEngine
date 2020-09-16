// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LevelInstance/LevelInstanceActorDesc.h"

#if WITH_EDITOR
#include "Engine/Level.h"
#include "LevelInstance/LevelInstanceActor.h"

bool FLevelInstanceActorDesc::Init(const AActor* InActor)
{
	if (FWorldPartitionActorDesc::Init(InActor))
	{
		const ALevelInstance* LevelInstanceActor = CastChecked<ALevelInstance>(InActor);
		LevelPackage = *LevelInstanceActor->GetWorldAssetPackage();
		LevelInstanceTransform = FTransform(InActor->GetActorRotation(), InActor->GetActorLocation());
		UpdateHash();
		return true;
	}
	return false;
}

void FLevelInstanceActorDesc::SerializeMetaData(FActorMetaDataSerializer* Serializer)
{
	FWorldPartitionActorDesc::SerializeMetaData(Serializer);

	Serializer->Serialize(TEXT("LevelInstancePackage"), LevelPackage);
	Serializer->Serialize(TEXT("LevelInstanceTransform"), LevelInstanceTransform);

	if (Serializer->IsReading())
	{
		if (!LevelPackage.IsNone())
		{
			FBox LevelBounds;
			if (ULevel::GetLevelBoundsFromPackage(LevelPackage, LevelBounds))
			{
				FVector LevelBoundsLocation;
				LevelBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);

				//@todo_ow: This will result in a new BoundsExtent that is larger than it should. To fix this, we would need the Object Oriented BoundingBox of the actor (the BV of the actor without rotation)
				const FVector BoundsMin = BoundsLocation - BoundsExtent;
				const FVector BoundsMax = BoundsLocation + BoundsExtent;
				const FBox NewBounds = FBox(BoundsMin, BoundsMax).TransformBy(LevelInstanceTransform);
				NewBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);
			}
		}
	}
}

void FLevelInstanceActorDesc::BuildHash(FHashBuilder& HashBuilder)
{
	FWorldPartitionActorDesc::BuildHash(HashBuilder);
	HashBuilder << LevelPackage << LevelInstanceTransform;
}
#endif
