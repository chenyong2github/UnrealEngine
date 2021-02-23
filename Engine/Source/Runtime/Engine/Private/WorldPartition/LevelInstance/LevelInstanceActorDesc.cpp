// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LevelInstance/LevelInstanceActorDesc.h"
#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "Engine/Level.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "WorldPartition/WorldPartition.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

FLevelInstanceActorDesc::FLevelInstanceActorDesc()
	: DesiredRuntimeBehavior(ELevelInstanceRuntimeBehavior::Embedded)
{

}

void FLevelInstanceActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	const ALevelInstance* LevelInstanceActor = CastChecked<ALevelInstance>(InActor);
	LevelPackage = *LevelInstanceActor->GetWorldAssetPackage();
	LevelInstanceTransform = LevelInstanceActor->GetActorTransform();
	DesiredRuntimeBehavior = LevelInstanceActor->GetDesiredRuntimeBehavior();

	RegisterContainer();
}

void FLevelInstanceActorDesc::Init(UActorDescContainer* InContainer, const FWorldPartitionActorDescInitData& DescData)
{
	FWorldPartitionActorDesc::Init(InContainer, DescData);

	RegisterContainer();
}

void FLevelInstanceActorDesc::RegisterContainer()
{
	check(!LevelInstanceContainer);
	if (Container && (DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Embedded || DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Partitioned))
	{
		if (!LevelPackage.IsNone() && ULevel::GetIsLevelUsingExternalActorsFromPackage(LevelPackage))
		{
			UWorldPartition* WorldPartition = Container->GetWorld()->GetWorldPartition();
			LevelInstanceContainer = WorldPartition->RegisterActorDescContainer(LevelPackage);
		}
	}
}

bool FLevelInstanceActorDesc::GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const
{
	if (LevelInstanceContainer)
	{
		OutLevelContainer = LevelInstanceContainer;
		OutLevelTransform = LevelInstanceTransform;
		check(DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Embedded || DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Partitioned)
		OutClusterMode = DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Embedded ? EContainerClusterMode::Embedded : EContainerClusterMode::Partitioned;
		return true;
	}

	return false;
}

void FLevelInstanceActorDesc::Serialize(FArchive& Ar)
{
	FWorldPartitionActorDesc::Serialize(Ar);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);

	Ar << LevelPackage << LevelInstanceTransform;

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::LevelInstanceSerializeRuntimeBehavior)
	{
		Ar << DesiredRuntimeBehavior;
	}

	if (Ar.IsLoading())
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

void FLevelInstanceActorDesc::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(LevelInstanceContainer);
}

#endif
