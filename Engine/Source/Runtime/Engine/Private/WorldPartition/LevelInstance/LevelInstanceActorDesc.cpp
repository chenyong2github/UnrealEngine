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

void FLevelInstanceActorDesc::UpdateBounds()
{
	check(LevelInstanceContainerWorldContext.IsValid());

	ULevelInstanceSubsystem* LevelInstanceSubsystem = UWorld::GetSubsystem<ULevelInstanceSubsystem>(LevelInstanceContainerWorldContext.Get());
	check(LevelInstanceSubsystem);

	FTransform LevelInstancePivotOffsetTransform = FTransform(ULevel::GetLevelInstancePivotOffsetFromPackage(LevelPackage));
	FTransform FinalLevelTransform = LevelInstancePivotOffsetTransform * LevelInstanceTransform;
	FBox ContainerBounds = LevelInstanceSubsystem->GetContainerBounds(LevelPackage).TransformBy(FinalLevelTransform);

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

			ULevelInstanceSubsystem* LevelInstanceSubsystem = UWorld::GetSubsystem<ULevelInstanceSubsystem>(InWorldContext);
			check(LevelInstanceSubsystem);

			LevelInstanceContainer = LevelInstanceSubsystem->RegisterContainer(LevelPackage);
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

		ULevelInstanceSubsystem* LevelInstanceSubsystem = UWorld::GetSubsystem<ULevelInstanceSubsystem>(LevelInstanceContainerWorldContext.Get());
		check(LevelInstanceSubsystem);

		LevelInstanceSubsystem->UnregisterContainer(LevelInstanceContainer.Get());
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

bool FLevelInstanceActorDesc::GetContainerInstance(FContainerInstance& OutContainerInstance, bool bInBuildFilter) const
{
	if (LevelInstanceContainer.IsValid())
	{
		OutContainerInstance.Container = LevelInstanceContainer.Get();
		OutContainerInstance.ClusterMode = EContainerClusterMode::Partitioned;

		// Apply level instance pivot offset
		FTransform LevelInstancePivotOffsetTransform = FTransform(ULevel::GetLevelInstancePivotOffsetFromPackage(LevelInstanceContainer->GetContainerPackage()));
		OutContainerInstance.Transform = LevelInstancePivotOffsetTransform * LevelInstanceTransform;

		if (bInBuildFilter)
		{
			// Fill Container Instance Filter
			ULevelInstanceSubsystem* LevelInstanceSubsystem = UWorld::GetSubsystem<ULevelInstanceSubsystem>(LevelInstanceContainerWorldContext.Get());
			check(LevelInstanceSubsystem);

			FWorldPartitionActorFilter ContainerFilter = LevelInstanceSubsystem->GetLevelInstanceFilter(LevelInstanceContainer->GetContainerPackage().ToString());
			ContainerFilter.Override(Filter);

			// Flatten Filter to FActorContainerID map
			TMap<FActorContainerID, TSet<FSoftObjectPath>> FilteredOutDataLayersPerContainer;
			TFunction<void(const FActorContainerID&, const FWorldPartitionActorFilter&)> ProcessFilter = [&FilteredOutDataLayersPerContainer, &ProcessFilter](const FActorContainerID& InContainerID, const FWorldPartitionActorFilter& InContainerFilter)
			{
				check(!FilteredOutDataLayersPerContainer.Contains(InContainerID));
				TSet<FSoftObjectPath>& Filtered = FilteredOutDataLayersPerContainer.Add(InContainerID);

				for (auto& [AssetPath, DataLayerFilter] : InContainerFilter.DataLayerFilters)
				{
					if (!DataLayerFilter.bIncluded)
					{
						Filtered.Add(AssetPath);
					}
				}

				for (auto& [ActorGuid, WorldPartitionActorFilter] : InContainerFilter.GetChildFilters())
				{
					ProcessFilter(FActorContainerID(InContainerID, ActorGuid), *WorldPartitionActorFilter);
				}
			};

			ProcessFilter(OutContainerInstance.GetID(), ContainerFilter);

			TFunction<void(const FActorContainerID&, const UActorDescContainer*)> ProcessContainers = [&FilteredOutDataLayersPerContainer, &OutContainerInstance, &ProcessContainers](const FActorContainerID& InContainerID, const UActorDescContainer* InContainer)
			{
				const TSet<FSoftObjectPath>& Filtered = FilteredOutDataLayersPerContainer.FindChecked(InContainerID);
				for (FActorDescList::TConstIterator<> ActorDescIt(InContainer); ActorDescIt; ++ActorDescIt)
				{
					if (ActorDescIt->GetDataLayers().Num() > 0 && ActorDescIt->IsUsingDataLayerAsset())
					{
						for (FName DataLayerName : ActorDescIt->GetDataLayers())
						{
							FSoftObjectPath DataLayerAsset(DataLayerName.ToString());
							if (Filtered.Contains(DataLayerAsset))
							{
								OutContainerInstance.FilteredActors.FindOrAdd(InContainerID).Add(ActorDescIt->GetGuid());
								break;
							}
						}
					}

					if (ActorDescIt->IsContainerInstance())
					{
						FWorldPartitionActorDesc::FContainerInstance ChildContainerInstance;
						const bool bBuildFilter = false;
						if (ActorDescIt->GetContainerInstance(ChildContainerInstance, bBuildFilter))
						{
							ProcessContainers(FActorContainerID(InContainerID, ActorDescIt->GetGuid()), ChildContainerInstance.Container);
						}
					}
				}
			};

			ProcessContainers(OutContainerInstance.GetID(), OutContainerInstance.Container);
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

	if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::WorldPartitionActorFilter)
	{
		Ar << Filter;
	}
}

#endif
