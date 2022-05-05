// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LevelInstance/LevelInstanceActorDesc.h"
#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "Engine/Level.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"

static int32 GLevelInstanceDebugForceLevelStreaming = 0;
static FAutoConsoleVariableRef CVarForceLevelStreaming(
	TEXT("levelinstance.debug.forcelevelstreaming"),
	GLevelInstanceDebugForceLevelStreaming,
	TEXT("Set to 1 to force Level Instance to be streamed instead of embedded in World Partition grid."));

class FActorDescContainerInstanceManager : public FGCObject
{
	struct FActorDescContainerInstance
	{
		FActorDescContainerInstance()
		: Container(nullptr)
		, RefCount(0)
		{}

		void AddReferencedObjects(FReferenceCollector& Collector)
		{
			Collector.AddReferencedObject(Container);
		}

		UActorDescContainer* Container;
		uint32 RefCount;
	};

	//~ Begin FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		for (auto& It : ActorDescContainers)
		{
			It.Value.AddReferencedObjects(Collector);
		}
	}

	virtual FString GetReferencerName() const override
	{
		return TEXT("FActorDescContainerInstanceManager");
	}
	//~ End FGCObject Interface

public:
	static FActorDescContainerInstanceManager& Get()
	{
		static FActorDescContainerInstanceManager Instance;
		return Instance;
	}

	UActorDescContainer* RegisterContainer(FName PackageName, UWorld* InWorld)
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

		check(ActorDescContainer->GetWorld() == InWorld);
		return ActorDescContainer;
	}

	void UnregisterContainer(UActorDescContainer* Container)
	{
		FName PackageName = Container->GetContainerPackage();
		FActorDescContainerInstance& ExistingContainerInstance = ActorDescContainers.FindChecked(PackageName);

		if (ExistingContainerInstance.RefCount-- == 1)
		{
			ExistingContainerInstance.Container->Uninitialize();
			ActorDescContainers.FindAndRemoveChecked(PackageName);
		}
	}

private:
	TMap<FName, FActorDescContainerInstance> ActorDescContainers;
};

FLevelInstanceActorDesc::FLevelInstanceActorDesc()
	: DesiredRuntimeBehavior(ELevelInstanceRuntimeBehavior::Partitioned)
	, LevelInstanceContainer(nullptr)
{}

FLevelInstanceActorDesc::~FLevelInstanceActorDesc()
{
	UnregisterContainerInstance();
	check(!LevelInstanceContainer.IsValid());
}

void FLevelInstanceActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	const ILevelInstanceInterface* LevelInstance = CastChecked<ILevelInstanceInterface>(InActor);
	LevelPackage = *LevelInstance->GetWorldAssetPackage();
	LevelInstanceTransform = InActor->GetActorTransform();
	DesiredRuntimeBehavior = LevelInstance->GetDesiredRuntimeBehavior();
}

void FLevelInstanceActorDesc::Init(const FWorldPartitionActorDescInitData& DescData)
{
	AActor* CDO = DescData.NativeClass->GetDefaultObject<AActor>();
	ILevelInstanceInterface* LevelInstanceCDO = CastChecked<ILevelInstanceInterface>(CDO);
	DesiredRuntimeBehavior = LevelInstanceCDO->GetDefaultRuntimeBehavior();

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

void FLevelInstanceActorDesc::RegisterContainerInstance(UWorld* InWorld)
{
	check(InWorld);
	check(!LevelInstanceContainer.IsValid());
	if (DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Partitioned && !GLevelInstanceDebugForceLevelStreaming)
	{
		if (!LevelPackage.IsNone() && ULevel::GetIsLevelUsingExternalActorsFromPackage(LevelPackage) && ULevelInstanceSubsystem::CanUsePackage(LevelPackage))
		{
			LevelInstanceContainer = FActorDescContainerInstanceManager::Get().RegisterContainer(LevelPackage, InWorld);
			check(LevelInstanceContainer.IsValid());
		}
	}
}

void FLevelInstanceActorDesc::UnregisterContainerInstance()
{
	if (LevelInstanceContainer.IsValid())
	{
		FActorDescContainerInstanceManager::Get().UnregisterContainer(LevelInstanceContainer.Get());
		LevelInstanceContainer.Reset();
	}
}

void FLevelInstanceActorDesc::SetContainer(UActorDescContainer* InContainer)
{
	FWorldPartitionActorDesc::SetContainer(InContainer);

	if (Container)
	{
		RegisterContainerInstance(Container->GetWorld());
	}
	else
	{
		UnregisterContainerInstance();
	}
}

bool FLevelInstanceActorDesc::GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const
{
	if (!LevelInstanceContainer.IsValid() && IsLoaded())
	{
		// Lazy initialization of LevelInstanceContainer (used by ModifiedActorsDescList)
		const_cast<FLevelInstanceActorDesc*>(this)->RegisterContainerInstance(GetActor()->GetWorld());
	}
	if (LevelInstanceContainer.IsValid())
	{
		OutLevelContainer = LevelInstanceContainer.Get();
		// Apply level instance pivot offset
		FTransform LevelInstancePivotOffsetTransform = FTransform(ULevel::GetLevelInstancePivotOffsetFromPackage(LevelInstanceContainer->GetContainerPackage()));
		OutLevelTransform = LevelInstancePivotOffsetTransform * LevelInstanceTransform;
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

		const AActor* CDO = GetActorClass()->GetDefaultObject<AActor>();
		const ILevelInstanceInterface* LevelInstanceCDO = CastChecked<ILevelInstanceInterface>(CDO);
		if (!LevelPackage.IsNone() && (LevelInstanceCDO->IsLoadingEnabled() || bFixupOldVersion))
		{
			FBox OutBounds;
			if (ULevelInstanceSubsystem::GetLevelInstanceBoundsFromPackage(LevelInstanceTransform, LevelPackage, OutBounds))
			{
				OutBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);
			}
		}
	}
}

#endif