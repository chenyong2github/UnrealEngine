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

static int32 GLevelInstanceDebugForceLevelStreaming = 0;
static FAutoConsoleVariableRef CVarForceLevelStreaming(
	TEXT("levelinstance.debug.forcelevelstreaming"),
	GLevelInstanceDebugForceLevelStreaming,
	TEXT("Set to 1 to force Level Instance to be streamed instead of embedded in World Partition grid."));

struct FActorDescContainerInstance
{
	FActorDescContainerInstance()
	: Container(nullptr)
	, RefCount(0)
	{}

	UActorDescContainer* Container;
	uint32 RefCount;
};

static TMap<FName, FActorDescContainerInstance> ActorDescContainers;

FLevelInstanceActorDesc::FLevelInstanceActorDesc()
	: DesiredRuntimeBehavior(ELevelInstanceRuntimeBehavior::Partitioned)
	, LevelInstanceContainer(nullptr)
{}

FLevelInstanceActorDesc::~FLevelInstanceActorDesc()
{
	check(!LevelInstanceContainer);
}

void FLevelInstanceActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	const ALevelInstance* LevelInstanceActor = CastChecked<ALevelInstance>(InActor);
	LevelPackage = *LevelInstanceActor->GetWorldAssetPackage();
	LevelInstanceTransform = LevelInstanceActor->GetActorTransform();
	DesiredRuntimeBehavior = LevelInstanceActor->GetDesiredRuntimeBehavior();
}

void FLevelInstanceActorDesc::Init(const FWorldPartitionActorDescInitData& DescData)
{
	ALevelInstance* CDO = DescData.NativeClass->GetDefaultObject<ALevelInstance>();
	DesiredRuntimeBehavior = CDO->GetDefaultRuntimeBehavior();

	FWorldPartitionActorDesc::Init(DescData);
}

bool FLevelInstanceActorDesc::Equals(const FWorldPartitionActorDesc* Other) const
{
	if (FWorldPartitionActorDesc::Equals(Other))
	{
		const FLevelInstanceActorDesc* LevelInstanceActorDesc = (FLevelInstanceActorDesc*)Other;

		return
			LevelPackage == LevelInstanceActorDesc->LevelPackage &&
			LevelInstanceTransform.Equals(LevelInstanceActorDesc->LevelInstanceTransform, 0.1f) &&
			DesiredRuntimeBehavior == LevelInstanceActorDesc->DesiredRuntimeBehavior;
	}

	return false;
}

void FLevelInstanceActorDesc::SetContainer(UActorDescContainer* InContainer)
{
	FWorldPartitionActorDesc::SetContainer(InContainer);

	if (Container)
	{
		check(!LevelInstanceContainer);

		if (DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Partitioned && !GLevelInstanceDebugForceLevelStreaming)
		{
			if (!LevelPackage.IsNone() && ULevel::GetIsLevelUsingExternalActorsFromPackage(LevelPackage) && !ULevel::GetIsLevelPartitionedFromPackage(LevelPackage))
			{
				LevelInstanceContainer = RegisterActorDescContainer(LevelPackage, Container->GetWorld());
				check(LevelInstanceContainer);
			}
		}
	}
	else
	{
		if (LevelInstanceContainer)
		{
			UnregisterActorDescContainer(LevelInstanceContainer);
			LevelInstanceContainer = nullptr;
		}
	}
}

bool FLevelInstanceActorDesc::GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const
{
	if (LevelInstanceContainer)
	{
		OutLevelContainer = LevelInstanceContainer;
		OutLevelTransform = LevelInstanceTransform;
		OutClusterMode = EContainerClusterMode::Partitioned;
		return true;
	}

	return false;
}

void FLevelInstanceActorDesc::TransferFrom(const FWorldPartitionActorDesc* From)
{
	FWorldPartitionActorDesc::TransferFrom(From);

	FLevelInstanceActorDesc* FromLevelInstanceActorDesc = (FLevelInstanceActorDesc*)From;
	LevelInstanceContainer = FromLevelInstanceActorDesc->LevelInstanceContainer;
	FromLevelInstanceActorDesc->LevelInstanceContainer = nullptr;
}

void FLevelInstanceActorDesc::Serialize(FArchive& Ar)
{
	FWorldPartitionActorDesc::Serialize(Ar);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	Ar << LevelPackage;

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::LargeWorldCoordinates)
	{
		FTransform3f LevelInstanceTransformFlt;
		Ar << LevelInstanceTransformFlt;
		LevelInstanceTransform = FTransform(LevelInstanceTransformFlt);
	}
	else
	{
		Ar << LevelInstanceTransform;
	}

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

UActorDescContainer* FLevelInstanceActorDesc::RegisterActorDescContainer(FName PackageName, UWorld* InWorld)
{
	FActorDescContainerInstance& ExistingContainerInstance = ActorDescContainers.FindOrAdd(PackageName);
	UActorDescContainer* ActorDescContainer = ExistingContainerInstance.Container;
	
	if (ExistingContainerInstance.RefCount++ == 0)
	{
		ActorDescContainer = NewObject<UActorDescContainer>(GetTransientPackage());
		ExistingContainerInstance.Container = ActorDescContainer;
		
		// This will potentially invalidate ExistingContainerInstance due to ActorDescContainers reallocation
		ActorDescContainer->Initialize(InWorld, PackageName);
	}

	return ActorDescContainer;
}

void FLevelInstanceActorDesc::UnregisterActorDescContainer(UActorDescContainer* Container)
{
	FName PackageName = Container->GetContainerPackage();
	FActorDescContainerInstance& ExistingContainerInstance = ActorDescContainers.FindChecked(PackageName);

	if (ExistingContainerInstance.RefCount-- == 1)
	{
		ExistingContainerInstance.Container->Uninitialize();
		ActorDescContainers.FindAndRemoveChecked(PackageName);
	}
}
#endif