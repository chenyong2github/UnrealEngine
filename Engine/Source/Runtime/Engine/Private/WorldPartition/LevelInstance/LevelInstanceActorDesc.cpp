// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LevelInstance/LevelInstanceActorDesc.h"
#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "Engine/Level.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"

FLevelInstanceActorDesc::FLevelInstanceActorDesc()
	: DesiredRuntimeBehavior(ELevelInstanceRuntimeBehavior::Partitioned)
	, LevelInstanceContainer(nullptr)
{

}

void FLevelInstanceActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	const ALevelInstance* LevelInstanceActor = CastChecked<ALevelInstance>(InActor);
	LevelPackage = *LevelInstanceActor->GetWorldAssetPackage();
	LevelInstanceTransform = LevelInstanceActor->GetActorTransform();
	DesiredRuntimeBehavior = LevelInstanceActor->GetDesiredRuntimeBehavior();
}

void FLevelInstanceActorDesc::Init(UActorDescContainer* InContainer, const FWorldPartitionActorDescInitData& DescData)
{
	ALevelInstance* CDO = DescData.NativeClass->GetDefaultObject<ALevelInstance>();
	DesiredRuntimeBehavior = CDO->GetDefaultRuntimeBehavior();

	FWorldPartitionActorDesc::Init(InContainer, DescData);
}

void FLevelInstanceActorDesc::OnUnregister()
{
	FWorldPartitionActorDesc::OnUnregister();
	LevelInstanceContainer = nullptr;
}

bool FLevelInstanceActorDesc::GetContainerInstance(UWorldPartition* InMainPartition, const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const
{
	// Lazy register
	if (!LevelInstanceContainer)
	{
		if (InMainPartition && DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Partitioned)
		{
			if (!LevelPackage.IsNone() && ULevel::GetIsLevelUsingExternalActorsFromPackage(LevelPackage) && !ULevel::GetIsLevelPartitionedFromPackage(LevelPackage))
			{
				LevelInstanceContainer = InMainPartition->RegisterActorDescContainer(LevelPackage);
			}
		}
	}

	if (LevelInstanceContainer)
	{
		OutLevelContainer = LevelInstanceContainer;
		OutLevelTransform = LevelInstanceTransform;
		OutClusterMode = EContainerClusterMode::Partitioned;
		return true;
	}

	return false;
}

void FLevelInstanceActorDesc::Serialize(FArchive& Ar)
{
	FWorldPartitionActorDesc::Serialize(Ar);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	Ar << LevelPackage << LevelInstanceTransform;

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::LevelInstanceSerializeRuntimeBehavior)
	{
		Ar << DesiredRuntimeBehavior;

		if (Ar.IsLoading() && DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Embedded_Deprecated)
		{
			DesiredRuntimeBehavior = ELevelInstanceRuntimeBehavior::Partitioned;
		}
	}

	if (Ar.IsLoading())
	{
		const bool bFixupOldVersion = (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::PackedLevelInstanceBoundsFix) && 
									  (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::PackedLevelInstanceBoundsFix);

		if (!LevelPackage.IsNone() && (GetActorClass()->GetDefaultObject<ALevelInstance>()->SupportsLoading() || bFixupOldVersion))
		{
			FBox OutBounds;
			if (ULevelInstanceSubsystem::GetLevelInstanceBoundsFromPackage(LevelInstanceTransform, LevelPackage, OutBounds))
			{
				OutBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);
			}
		}
	}
}

void FLevelInstanceActorDesc::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(LevelInstanceContainer);
}

#endif
