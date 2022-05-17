// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorLoaderInterface.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Misc/ScopedSlowTask.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartitionActorDescViewProxy.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#endif

#define LOCTEXT_NAMESPACE "WorldPartition"

UWorldPartitionActorLoaderInterface::UWorldPartitionActorLoaderInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

#if WITH_EDITOR
IWorldPartitionActorLoaderInterface::ILoaderAdapter::ILoaderAdapter(UWorld* InWorld)
	: World(InWorld)
	, bLoaded(false)
	, bUserCreated(false)
{
	check(!World->IsGameWorld());
}

IWorldPartitionActorLoaderInterface::ILoaderAdapter::~ILoaderAdapter()
{
	UnregisterDelegates();
}

bool IWorldPartitionActorLoaderInterface::ILoaderAdapter::Load()
{
	if (!bLoaded)
	{
		bLoaded = true;
		RefreshLoadedState();
		RegisterDelegates();
	}

	return true;
}

bool IWorldPartitionActorLoaderInterface::ILoaderAdapter::Unload()
{
	if (bLoaded && !IsEngineExitRequested())
	{
		if (UWorldPartition* WorldPartition = World->GetWorldPartition())
		{
			TArray<FWorldPartitionHandle> ActorsToUnload;
			for (const TPair<FGuid, TMap<FGuid, FWorldPartitionReference>>& ActorRefToUnload : ActorReferences)
			{
				ActorsToUnload.Emplace(WorldPartition, ActorRefToUnload.Key);
			}
			
			if (!AllowUnloadingActors(ActorsToUnload))
			{
				return false;
			}

			FScopedSlowTask SlowTask(1, LOCTEXT("UpdatingLoading", "Updating loading..."));
			SlowTask.MakeDialog();

			UnregisterDelegates();

			ActorReferences.Empty();
			bLoaded = false;

			PostLoadedStateChanged(true);

			SlowTask.EnterProgressFrame(1);
		}
	}

	return true;
}

bool IWorldPartitionActorLoaderInterface::ILoaderAdapter::IsLoaded() const
{
	return bLoaded;
}

void IWorldPartitionActorLoaderInterface::ILoaderAdapter::RegisterDelegates()
{
	FDataLayersEditorBroadcast::Get().OnActorDataLayersEditorLoadingStateChanged().AddRaw(this, &IWorldPartitionActorLoaderInterface::ILoaderAdapter::OnActorDataLayersEditorLoadingStateChanged);
}

void IWorldPartitionActorLoaderInterface::ILoaderAdapter::UnregisterDelegates()
{
	FDataLayersEditorBroadcast::Get().OnActorDataLayersEditorLoadingStateChanged().RemoveAll(this);
}

bool IWorldPartitionActorLoaderInterface::ILoaderAdapter::RefreshLoadedState()
{
	if (bLoaded)
	{
		if (UWorldPartition* WorldPartition = World->GetWorldPartition())
		{
			TArray<FWorldPartitionHandle> ActorsToLoad;
			TArray<FWorldPartitionHandle> ActorsToUnload;			
			ForEachActor([this, &ActorsToLoad, &ActorsToUnload](const FWorldPartitionHandle& Actor)
			{
				if (ShouldActorBeLoaded(Actor))
				{
					if (!ActorReferences.Contains(Actor->GetGuid()))
					{
						ActorsToLoad.Add(Actor);
					}
				}
				else if (ActorReferences.Contains(Actor->GetGuid()))
				{
					ActorsToUnload.Add(Actor);
				}
			});

			if (!AllowUnloadingActors(ActorsToUnload))
			{
				return false;
			}

			FScopedSlowTask SlowTask(ActorsToLoad.Num() + ActorsToUnload.Num(), LOCTEXT("UpdatingLoading", "Updating loading..."));
			SlowTask.MakeDialog();

			for (FWorldPartitionHandle& ActorToLoad : ActorsToLoad)
			{
				SlowTask.EnterProgressFrame(1);
				FWorldPartitionHandle ActorHandle(WorldPartition, ActorToLoad->GetGuid());
				AddReferenceToActor(ActorHandle);
			}

			for (FWorldPartitionHandle& ActorToUnload : ActorsToUnload)
			{
				SlowTask.EnterProgressFrame(1);
				ActorReferences.Remove(ActorToUnload->GetGuid());
			}

			PostLoadedStateChanged(ActorsToUnload.Num() > 0);
		}
	}

	return true;
}

bool IWorldPartitionActorLoaderInterface::ILoaderAdapter::AllowUnloadingActors(const TArray<FWorldPartitionHandle>& ActorsToUnload) const
{
	if (IsRunningCommandlet())
	{
		return true;
	}

	TSet<UPackage*> ModifiedPackages;
	TMap<FWorldPartitionHandle, int32> UnloadCount;

	// Count the number of times we are referencing each actor to unload
	for (const FWorldPartitionHandle& ActorToUnload : ActorsToUnload)
	{
		const TMap<FGuid, FWorldPartitionReference>& ActorToUnloadRefs = ActorReferences.FindChecked(ActorToUnload->GetGuid());

		for (const TPair<FGuid, FWorldPartitionReference>& ActorToUnloadRef : ActorToUnloadRefs)
		{
			UnloadCount.FindOrAdd(ActorToUnloadRef.Value, 0)++;
		}
	}

	for (const TPair<FWorldPartitionHandle, int32>& Pair : UnloadCount)
	{
		const FWorldPartitionHandle& ActorHandle = Pair.Key;

		// Only prompt if the actor will get unloaded by the unloading cells
		if (ActorHandle->GetHardRefCount() == Pair.Value)
		{
			if (AActor* LoadedActor = ActorHandle->GetActor())
			{
				if (UPackage* ActorPackage = LoadedActor->GetExternalPackage(); ActorPackage && ActorPackage->IsDirty())
				{
					ModifiedPackages.Add(ActorPackage);
				}
			}
		}
	}

	if (ModifiedPackages.Num())
	{
		const FText Title = LOCTEXT("SaveActorsTitle", "Save Actor(s)");
		const FText Message = LOCTEXT("SaveActorsMessage", "Save Actor(s) before unloading them.");

		FEditorFileUtils::EPromptReturnCode RetCode = FEditorFileUtils::PromptForCheckoutAndSave(ModifiedPackages.Array(), false, true, Title, Message, nullptr, false, true);
		check(RetCode != FEditorFileUtils::PR_Failure);

		if (RetCode == FEditorFileUtils::PR_Cancelled)
		{
			return false;
		}
	}

	return true;
}

bool IWorldPartitionActorLoaderInterface::ILoaderAdapter::ShouldActorBeLoaded(const FWorldPartitionHandle& Actor) const
{
	check(Actor.IsValid());

	if (!Actor->ShouldBeLoadedByEditor())
	{
		return false;
	}

	if (UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(World))
	{
		FWorldPartitionActorViewProxy ActorDescProxy(*Actor);

		if (IsRunningCookCommandlet())
		{
			// When running cook commandlet, dont allow loading of actors with runtime loaded data layers
			for (const FName& DataLayerInstanceName : ActorDescProxy.GetDataLayers())
			{
				const UDataLayerInstance* DataLayerInstance = DataLayerSubsystem->GetDataLayerInstance(DataLayerInstanceName);
				if (DataLayerInstance && DataLayerInstance->IsRuntime())
				{
					return false;
				}
			}
		}
		else
		{
			uint32 NumValidLayers = 0;
			for (const FName& DataLayerInstanceName : ActorDescProxy.GetDataLayers())
			{
				if (const UDataLayerInstance* DataLayerInstance = DataLayerSubsystem->GetDataLayerInstance(DataLayerInstanceName))
				{
					if (DataLayerInstance->IsEffectiveLoadedInEditor())
					{
						return true;
					}
					NumValidLayers++;
				}
			}
			return !NumValidLayers;
		}
	}

	return true;
};

void IWorldPartitionActorLoaderInterface::ILoaderAdapter::PostLoadedStateChanged(bool bUnloadedActors)
{
	if (!IsRunningCommandlet())
	{
		if (bUnloadedActors)
		{
			GEditor->SelectNone(true, true);
		}

		GEngine->BroadcastLevelActorListChanged();
		GEditor->NoteSelectionChange();

		GEditor->ResetTransaction(LOCTEXT("LoadingEditorActorResetTrans", "Editor Actors Loading State Changed"));

		if (bUnloadedActors)
		{
			GEngine->ForceGarbageCollection(true);
		}
	}
}

void IWorldPartitionActorLoaderInterface::ILoaderAdapter::AddReferenceToActor(FWorldPartitionHandle& ActorHandle)
{
	TFunction<void(const FWorldPartitionHandle&, TMap<FGuid, FWorldPartitionReference>&)> AddReferences = [this, &ActorHandle, &AddReferences](const FWorldPartitionHandle& Handle, TMap<FGuid, FWorldPartitionReference>& ReferenceMap)
	{
		if (!ReferenceMap.Contains(Handle->GetGuid()))
		{
			ReferenceMap.Emplace(Handle->GetGuid(), Handle);
			
			for (const FGuid& ReferencedActorGuid : ActorHandle->GetReferences())
			{
				FWorldPartitionHandle ReferenceActorHandle(ActorHandle->GetContainer(), ReferencedActorGuid);

				if (ReferenceActorHandle.IsValid())
				{
					AddReferences(ReferenceActorHandle, ReferenceMap);
				}
			}
		}
	};

	AddReferences(ActorHandle, ActorReferences.Emplace(ActorHandle->GetGuid()));
}

void IWorldPartitionActorLoaderInterface::ILoaderAdapter::OnActorDataLayersEditorLoadingStateChanged(bool bFromUserOperation)
{
	if (!RefreshLoadedState())
	{
		// Temporary measure: reset transactions if the user cancels saving to avoid inconsistencies. The next step is to change that
		// behavior in order to keep dirty actors in memory instead of asking to save them when unloading, in preparation of in editor
		// camera loading.
		GEditor->ResetTransaction(LOCTEXT("LoadingEditorActorResetTrans", "Editor Actors Loading State Changed"));
	}
}

IWorldPartitionActorLoaderInterface::FLoaderAdapterList::FLoaderAdapterList(UWorld* InWorld)
	: ILoaderAdapter(InWorld)
{}

void IWorldPartitionActorLoaderInterface::FLoaderAdapterList::ForEachActor(TFunctionRef<void(const FWorldPartitionHandle&)> InOperation) const
{
	for (const FWorldPartitionHandle& Actor : Actors)
	{
		InOperation(Actor);
	}
}

IWorldPartitionActorLoaderInterface::ILoaderAdapterSpatial::ILoaderAdapterSpatial(UWorld* InWorld)
	: ILoaderAdapter(InWorld)
	, bIncludeSpatiallyLoadedActors(true)
	, bIncludeNonSpatiallyLoadedActors(false)
{}

void IWorldPartitionActorLoaderInterface::ILoaderAdapterSpatial::ForEachActor(TFunctionRef<void(const FWorldPartitionHandle&)> InOperation) const
{
	if (UWorldPartition* WorldPartition = World->GetWorldPartition())
	{
		WorldPartition->EditorHash->ForEachIntersectingActor(*GetBoundingBox(), [this, WorldPartition, &InOperation](FWorldPartitionActorDesc* ActorDesc)
		{
			if (Intersect(ActorDesc->GetBounds()))
			{
				FWorldPartitionHandle ActorHandle(WorldPartition, ActorDesc->GetGuid());
				InOperation(ActorHandle);
			}
		}, bIncludeSpatiallyLoadedActors, bIncludeNonSpatiallyLoadedActors);
	}
}
#endif

#undef LOCTEXT_NAMESPACE