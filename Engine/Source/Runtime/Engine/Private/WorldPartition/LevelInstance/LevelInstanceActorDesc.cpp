// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LevelInstance/LevelInstanceActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescView.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "Engine/Level.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "LevelInstance/LevelInstanceComponent.h"
#include "Misc/PackageName.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationErrorHandler.h"
#include "WorldPartition/WorldPartitionActorDescArchive.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

static int32 GLevelInstanceDebugForceLevelStreaming = 0;
static FAutoConsoleVariableRef CVarForceLevelStreaming(
	TEXT("levelinstance.debug.forcelevelstreaming"),
	GLevelInstanceDebugForceLevelStreaming,
	TEXT("Set to 1 to force Level Instance to be streamed instead of embedded in World Partition grid."));

FLevelInstanceActorDesc::FLevelInstanceActorDesc()
	: DesiredRuntimeBehavior(ELevelInstanceRuntimeBehavior::Partitioned)
	, LevelInstanceContainer(nullptr)
	, bIsContainerInstance(false)
{}

FLevelInstanceActorDesc::~FLevelInstanceActorDesc()
{
	UnregisterContainerInstance();
	check(!LevelInstanceContainer.IsValid());
	check(!LevelInstanceContainerWorldContext.IsValid());
}

void FLevelInstanceActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	const ILevelInstanceInterface* LevelInstance = CastChecked<ILevelInstanceInterface>(InActor);
	LevelPackage = *LevelInstance->GetWorldAssetPackage();
	LevelInstanceTransform = InActor->GetActorTransform();
	DesiredRuntimeBehavior = LevelInstance->GetDesiredRuntimeBehavior();
	Filter = LevelInstance->GetFilter();
	
	bIsContainerInstance = IsContainerInstanceInternal();
}

void FLevelInstanceActorDesc::Init(const FWorldPartitionActorDescInitData& DescData)
{
	AActor* CDO = DescData.NativeClass->GetDefaultObject<AActor>();
	ILevelInstanceInterface* LevelInstanceCDO = CastChecked<ILevelInstanceInterface>(CDO);
	DesiredRuntimeBehavior = LevelInstanceCDO->GetDefaultRuntimeBehavior();

	FWorldPartitionActorDesc::Init(DescData);
	
	bIsContainerInstance = IsContainerInstanceInternal();
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

void FLevelInstanceActorDesc::UpdateBounds()
{
	check(LevelInstanceContainerWorldContext.IsValid());

	UWorldPartitionSubsystem* WorldPartitionSubsystem = UWorld::GetSubsystem<UWorldPartitionSubsystem>(LevelInstanceContainerWorldContext.Get());
	check(WorldPartitionSubsystem);

	FTransform LevelInstancePivotOffsetTransform = FTransform(ULevel::GetLevelInstancePivotOffsetFromPackage(LevelPackage));
	FTransform FinalLevelTransform = LevelInstancePivotOffsetTransform * LevelInstanceTransform;
	FBox ContainerBounds = WorldPartitionSubsystem->GetContainerBounds(LevelPackage).TransformBy(FinalLevelTransform);

	ContainerBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);
}

void FLevelInstanceActorDesc::RegisterContainerInstance(UWorld* InWorldContext)
{
	if (InWorldContext)
	{
		check(!LevelInstanceContainer.IsValid());
		check(!LevelInstanceContainerWorldContext.IsValid());

		if (IsContainerInstance())
		{
			check(InWorldContext);
			LevelInstanceContainerWorldContext = InWorldContext;

			UWorldPartitionSubsystem* WorldPartitionSubsystem = UWorld::GetSubsystem<UWorldPartitionSubsystem>(InWorldContext);
			check(WorldPartitionSubsystem);

			LevelInstanceContainer = WorldPartitionSubsystem->RegisterContainer(LevelPackage);
			check(LevelInstanceContainer.IsValid());

			// Should only be called on RegisterContainerInstance before ActorDesc is hashed
			UpdateBounds();
		}
	}
}

void FLevelInstanceActorDesc::UnregisterContainerInstance()
{
	if (LevelInstanceContainer.IsValid())
	{
		check(LevelInstanceContainerWorldContext.IsValid());

		UWorldPartitionSubsystem* WorldPartitionSubsystem = UWorld::GetSubsystem<UWorldPartitionSubsystem>(LevelInstanceContainerWorldContext.Get());
		check(WorldPartitionSubsystem);

		WorldPartitionSubsystem->UnregisterContainer(LevelInstanceContainer.Get());
		LevelInstanceContainer.Reset();
	}

	LevelInstanceContainerWorldContext.Reset();
}

void FLevelInstanceActorDesc::SetContainer(UActorDescContainer* InContainer, UWorld* InWorldContext)
{
	FWorldPartitionActorDesc::SetContainer(InContainer, InWorldContext);

	if (Container)
	{
		RegisterContainerInstance(InWorldContext);
	}
	else
	{
		UnregisterContainerInstance();		
	}
}

bool FLevelInstanceActorDesc::IsContainerInstance() const
{
	return bIsContainerInstance;
}

bool FLevelInstanceActorDesc::IsContainerInstanceInternal() const
{
	if (DesiredRuntimeBehavior != ELevelInstanceRuntimeBehavior::Partitioned)
	{
		return false;
	}
	
	if (GLevelInstanceDebugForceLevelStreaming)
	{
		return false;
	}

	if (LevelPackage.IsNone())
	{
		return false;
	}
	
	if (!ULevel::GetIsLevelUsingExternalActorsFromPackage(LevelPackage))
	{
		return false;
	}

	return ULevelInstanceSubsystem::CanUsePackage(LevelPackage);
}

bool FLevelInstanceActorDesc::GetContainerInstance(const FGetContainerInstanceParams& InParams, FContainerInstance& OutContainerInstance) const
{
	if (LevelInstanceContainer.IsValid())
	{
		const ILevelInstanceInterface* LevelInstance = ActorPtr.IsValid() ? CastChecked<const ILevelInstanceInterface>(ActorPtr.Get()) : nullptr;
		OutContainerInstance.Container = LevelInstanceContainer.Get();
		OutContainerInstance.LoadedLevel = LevelInstance ? LevelInstance->GetLoadedLevel() : nullptr;
		OutContainerInstance.bSupportsPartialEditorLoading = LevelInstance ? LevelInstance->SupportsPartialEditorLoading() : false;
		OutContainerInstance.ClusterMode = EContainerClusterMode::Partitioned;

		// Apply level instance pivot offset
		FTransform LevelInstancePivotOffsetTransform = FTransform(ULevel::GetLevelInstancePivotOffsetFromPackage(LevelInstanceContainer->GetContainerPackage()));
		OutContainerInstance.Transform = LevelInstancePivotOffsetTransform * LevelInstanceTransform;		

		if (InParams.bBuildFilter)
		{
			// Fill Container Instance Filter
			UWorldPartitionSubsystem* WorldPartitionSubsystem = UWorld::GetSubsystem<UWorldPartitionSubsystem>(LevelInstanceContainerWorldContext.Get());
			check(WorldPartitionSubsystem);

			OutContainerInstance.FilteredActors = WorldPartitionSubsystem->GetFilteredActorsPerContainer(InParams.ContainerID, LevelInstanceContainer->GetContainerPackage().ToString(), Filter);
		}
		return true;
	}

	return false;
}

void FLevelInstanceActorDesc::CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler) const
{
	FWorldPartitionActorDesc::CheckForErrors(ErrorHandler);

	FPackagePath WorldAssetPath;
	if (!FPackagePath::TryFromPackageName(LevelPackage, WorldAssetPath) || !FPackageName::DoesPackageExist(WorldAssetPath))
	{
		ErrorHandler->OnLevelInstanceInvalidWorldAsset(this, LevelPackage, IStreamingGenerationErrorHandler::ELevelInstanceInvalidReason::WorldAssetNotFound);
	}
	else if (!ULevel::GetIsLevelUsingExternalActorsFromPackage(LevelPackage))
	{
		if (DesiredRuntimeBehavior != ELevelInstanceRuntimeBehavior::LevelStreaming)
		{
			ErrorHandler->OnLevelInstanceInvalidWorldAsset(this, LevelPackage, IStreamingGenerationErrorHandler::ELevelInstanceInvalidReason::WorldAssetNotUsingExternalActors);
		}
	}
	else if (ULevel::GetIsLevelPartitionedFromPackage(LevelPackage))
	{
		if ((DesiredRuntimeBehavior != ELevelInstanceRuntimeBehavior::Partitioned) || !ULevel::GetPartitionedLevelCanBeUsedByLevelInstanceFromPackage(LevelPackage))
		{
			ErrorHandler->OnLevelInstanceInvalidWorldAsset(this, LevelPackage, IStreamingGenerationErrorHandler::ELevelInstanceInvalidReason::WorldAssetImcompatiblePartitioned);
		}
	}
}

void FLevelInstanceActorDesc::TransferFrom(const FWorldPartitionActorDesc* From)
{
	FWorldPartitionActorDesc::TransferFrom(From);

	FLevelInstanceActorDesc* FromLevelInstanceActorDesc = (FLevelInstanceActorDesc*)From;

	// Use the Register/Unregister so callbacks are added/removed
	if (FromLevelInstanceActorDesc->LevelInstanceContainer.IsValid())
	{
		check(FromLevelInstanceActorDesc->LevelInstanceContainerWorldContext.IsValid());
		RegisterContainerInstance(FromLevelInstanceActorDesc->LevelInstanceContainerWorldContext.Get());
		FromLevelInstanceActorDesc->UnregisterContainerInstance();
	}
}

void FLevelInstanceActorDesc::Serialize(FArchive& Ar)
{
	FWorldPartitionActorDesc::Serialize(Ar);

	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

	Ar << TDeltaSerialize<FName>(LevelPackage);

	if (!bIsDefaultActorDesc)
	{
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
	}

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::LevelInstanceSerializeRuntimeBehavior)
	{
		Ar << TDeltaSerialize<ELevelInstanceRuntimeBehavior>(DesiredRuntimeBehavior);

		if (Ar.IsLoading() && DesiredRuntimeBehavior == ELevelInstanceRuntimeBehavior::Embedded_Deprecated)
		{
			DesiredRuntimeBehavior = ELevelInstanceRuntimeBehavior::Partitioned;
		}
	}

	if (!bIsDefaultActorDesc)
	{
		if (Ar.IsLoading())
		{
			const bool bFixupOldVersion = (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::PackedLevelInstanceBoundsFix) && 
										  (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::PackedLevelInstanceBoundsFix);

			const AActor* CDO = GetActorNativeClass()->GetDefaultObject<AActor>();
			const ILevelInstanceInterface* LevelInstanceCDO = CastChecked<ILevelInstanceInterface>(CDO);
			if (!LevelPackage.IsNone() && (LevelInstanceCDO->IsLoadingEnabled() || bFixupOldVersion))
			{
				if (!IsContainerInstance())
				{
					FBox OutBounds;
					if (ULevelInstanceSubsystem::GetLevelInstanceBoundsFromPackage(LevelInstanceTransform, LevelPackage, OutBounds))
					{
						OutBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);
					}
				}
			}
		}
	}

	if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::WorldPartitionActorFilter)
	{
		Ar << Filter;
	}
}

#endif
