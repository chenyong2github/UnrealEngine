// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/LevelStreamingGCHelper.h"
#include "Engine/LevelStreaming.h"
#include "Engine/CoreSettings.h"
#include "Engine/NetDriver.h"
#include "Engine/Engine.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectGlobalsInternal.h"
#include "UObject/ReferenceChainSearch.h"
#include "UObject/UObjectIterator.h"
#include "CoreGlobals.h"

/************************************************************************/
/* FLevelStreamingGCHelper implementation                               */
/************************************************************************/
FLevelStreamingGCHelper::FOnGCStreamedOutLevelsEvent FLevelStreamingGCHelper::OnGCStreamedOutLevels;
TArray<TWeakObjectPtr<ULevel> > FLevelStreamingGCHelper::LevelsPendingUnload;
TArray<FName> FLevelStreamingGCHelper::LevelPackageNames;
bool FLevelStreamingGCHelper::bEnabledForCommandlet = false;

void FLevelStreamingGCHelper::AddGarbageCollectorCallback()
{
	// Only register for garbage collection once
	static bool GarbageCollectAdded = false;
	if ( GarbageCollectAdded == false )
	{
		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic( FLevelStreamingGCHelper::PrepareStreamedOutLevelsForGC );
		FCoreUObjectDelegates::GetPostGarbageCollect().AddStatic( FLevelStreamingGCHelper::VerifyLevelsGotRemovedByGC );
		GarbageCollectAdded = true;
	}
}

void FLevelStreamingGCHelper::EnableForCommandlet()
{
	check(IsRunningCommandlet());
	bEnabledForCommandlet = true;
}

void FLevelStreamingGCHelper::RequestUnload( ULevel* InLevel )
{
	if (!IsRunningCommandlet() || bEnabledForCommandlet)
	{
		check( InLevel );
		check( InLevel->bIsVisible == false );
		LevelsPendingUnload.AddUnique( InLevel );
	}
}

void FLevelStreamingGCHelper::CancelUnloadRequest( ULevel* InLevel )
{
	LevelsPendingUnload.Remove( InLevel );
}

void FLevelStreamingGCHelper::PrepareStreamedOutLevelsForGC()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLevelStreamingGCHelper::PrepareStreamedOutLevelsForGC);
	if (LevelsPendingUnload.Num() > 0)
	{
		OnGCStreamedOutLevels.Broadcast();
	}
	
	// Iterate over all level objects that want to be unloaded.
	for( int32 LevelIndex=0; LevelIndex < LevelsPendingUnload.Num(); LevelIndex++ )
	{
		ULevel*	Level = LevelsPendingUnload[LevelIndex].Get();

		if( Level && ((!GIsEditor || bEnabledForCommandlet) || Level->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor)))
		{
			UPackage* LevelPackage = Level->GetOutermost();
			UE_LOG(LogStreaming, Log, TEXT("PrepareStreamedOutLevelsForGC called on '%s'"), *LevelPackage->GetName() );
			
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World)
				{
					// This can never ever be called during tick; same goes for GC in general.
					check( !World->bInTick );

					FWorldContext& MutableContext = GEngine->GetWorldContextFromWorldChecked(World);
					for (FNamedNetDriver& Driver : MutableContext.ActiveNetDrivers)
					{
						if (Driver.NetDriver != nullptr)
						{
							// The net driver must remove this level and its actors from the packagemap, or else
							// the client package map will keep hard refs to them and prevent GC
							Driver.NetDriver->NotifyStreamingLevelUnload(Level);
						}
					}

					// Broadcast level unloaded event to blueprints through level streaming objects
					ULevelStreaming::BroadcastLevelLoadedStatus(World, LevelPackage->GetFName(), false);
				}
			}

			// Make sure that this package has been unloaded after GC pass.
			LevelPackageNames.Add( LevelPackage->GetFName() );

			Level->CleanupLevel();

			// Mark world and all other package subobjects as pending kill
			// This will destroy metadata objects and any other objects left behind
			TSet<UPackage*> Packages;
			ForEachObjectWithOuter(Level->GetOutermostObject(), [&Packages](UObject* Object)
			{
				bool bIsAlreadyInSet = false;
				UPackage* Package = Object->GetPackage();
				Packages.Add(Package, &bIsAlreadyInSet);
				if (!bIsAlreadyInSet)
				{
					ForEachObjectWithPackage(Package, [](UObject* PackageObject) { PackageObject->MarkAsGarbage(); return true; }, true, RF_NoFlags, EInternalObjectFlags::Garbage);
					Package->MarkAsGarbage();
				}
			}, true, RF_NoFlags, EInternalObjectFlags::Garbage);

			if (!UObjectBaseUtility::IsPendingKillEnabled())
			{
				// Rename the packages that we are streaming out so that we can possibly reload another copy of them
				for (UPackage* Package : Packages)
				{
					FCoreUObjectInternalDelegates::GetOnLeakedPackageRenameDelegate().Broadcast(Package);
					FName NewName = MakeUniqueObjectName(nullptr, UPackage::StaticClass(), Package->GetFName());
					Package->Rename(*NewName.ToString(), nullptr, REN_ForceNoResetLoaders | REN_DontCreateRedirectors | REN_NonTransactional);
				}

#if !WITH_EDITOR
				// Clear the level actors array to maximize the memory savings from GC if there are outstanding references to some actors.
				// Don't do this outside the editor now til we can validate it works properly with external packages.
				Level->Actors.Empty();
				Level->ActorsForGC.Empty();
				Level->ActorCluster = nullptr; // The actor cluster has been marked garbage so will be dissolved, also drop a reference to it here since it has an internal array of pointers
#endif
			}

			Level->CleanupReferences();
		}
	}

	LevelsPendingUnload.Empty();
}

void FLevelStreamingGCHelper::VerifyLevelsGotRemovedByGC()
{
	if (LevelPackageNames.Num())
	{
#if DO_GUARD_SLOW
		const bool bShouldVerify = !GIsEditor;
#else
		const bool bShouldVerify = false;
#endif

#if !UE_BUILD_SHIPPING
		if (bShouldVerify || GLevelStreamingForceVerifyLevelsGotRemovedByGC)
		{
			int32 FailCount = 0;
			const bool bIsAsyncLoading = IsAsyncLoading();
			// Iterate over all objects and find out whether they reside in a GC'ed level package.
			for (FThreadSafeObjectIterator It; It; ++It)
			{
				UObject* Object = *It;
				// Check whether object's outermost is in the list.
				if (LevelPackageNames.Find(Object->GetOutermost()->GetFName()) != INDEX_NONE
					// But disregard package object itself.
					&& !Object->IsA(UPackage::StaticClass()))
				{
					if (bIsAsyncLoading && Object->HasAnyInternalFlags(EInternalObjectFlags::Async|EInternalObjectFlags::AsyncLoading))
					{
						UE_LOG(LogStreaming, Display, TEXT("Level object %s isn't released by async loading yet, "
								"it will get garbage collected next time instead."),
								*Object->GetFullName());
					}
					else
					{
						UE_LOG(LogStreaming, Warning, TEXT("Level object %s didn't get garbage collected! Trying to find culprit, though this might crash. Try increasing stack size if it does. Referenced by:"), *Object->GetFullName());
						FReferenceChainSearch RefChainSearch(Object, EReferenceChainSearchMode::Shortest | EReferenceChainSearchMode::PrintResults);
						FailCount++;
					}
				}
			}
			if (FailCount > 0)
			{
				UE_LOG(LogStreaming, Error, TEXT("Streamed out levels were not completely garbage collected! Please see previous log entries."));
				// If not forcing verify, consider errors as fatal
				check(GLevelStreamingForceVerifyLevelsGotRemovedByGC);
			}
		}
#endif
		LevelPackageNames.Empty();
	}
}

int32 FLevelStreamingGCHelper::GetNumLevelsPendingPurge()
{
	return LevelsPendingUnload.Num();
}