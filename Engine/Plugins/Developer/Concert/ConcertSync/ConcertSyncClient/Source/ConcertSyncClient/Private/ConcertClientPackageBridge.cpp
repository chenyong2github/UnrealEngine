// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertClientPackageBridge.h"
#include "ConcertLogGlobal.h"
#include "ConcertWorkspaceData.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/FeedbackContext.h"
#include "HAL/FileManager.h"

#include "AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
	#include "LevelEditor.h"
#endif

#define LOCTEXT_NAMESPACE "ConcertClientPackageBridge"

namespace ConcertClientPackageBridgeUtil
{

void FillPackageInfo(UPackage* InPackage, const EConcertPackageUpdateType InPackageUpdateType, FConcertPackageInfo& OutPackageInfo)
{
	OutPackageInfo.PackageName = InPackage->GetFName();
	OutPackageInfo.PackageFileExtension = UWorld::FindWorldInPackage(InPackage) ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
	OutPackageInfo.PackageUpdateType = InPackageUpdateType;
}

} // namespace ConcertClientPackageBridgeUtil

FConcertClientPackageBridge::FConcertClientPackageBridge()
	: bIgnoreLocalSave(false)
	, bIgnoreLocalDiscard(false)
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		// Register Package Events
		UPackage::PreSavePackageEvent.AddRaw(this, &FConcertClientPackageBridge::HandlePackagePreSave);
		UPackage::PackageSavedEvent.AddRaw(this, &FConcertClientPackageBridge::HandlePackageSaved);
		FCoreUObjectDelegates::OnPackageReloaded.AddRaw(this, &FConcertClientPackageBridge::HandleAssetReload);

		// Register Asset Registry Events
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().OnInMemoryAssetCreated().AddRaw(this, &FConcertClientPackageBridge::HandleAssetAdded);
		AssetRegistryModule.Get().OnInMemoryAssetDeleted().AddRaw(this, &FConcertClientPackageBridge::HandleAssetDeleted);
		AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FConcertClientPackageBridge::HandleAssetRenamed);

		// Register Map Change Events
		FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditor.OnMapChanged().AddRaw(this, &FConcertClientPackageBridge::HandleMapChanged);
	}
#endif	// WITH_EDITOR
}

FConcertClientPackageBridge::~FConcertClientPackageBridge()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		// Unregister Package Events
		UPackage::PreSavePackageEvent.RemoveAll(this);
		UPackage::PackageSavedEvent.RemoveAll(this);
		FCoreUObjectDelegates::OnPackageReloaded.RemoveAll(this);

		// Unregister Asset Registry Events
		if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
		{
			AssetRegistryModule->Get().OnInMemoryAssetCreated().RemoveAll(this);
			AssetRegistryModule->Get().OnInMemoryAssetDeleted().RemoveAll(this);
			AssetRegistryModule->Get().OnAssetRenamed().RemoveAll(this);
		}

		// Unregister Map Change Events
		if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
		{
			LevelEditor->OnMapChanged().RemoveAll(this);
		}
	}
#endif	// WITH_EDITOR
}

FOnConcertClientLocalPackageEvent& FConcertClientPackageBridge::OnLocalPackageEvent()
{
	return OnLocalPackageEventDelegate;
}

FOnConcertClientLocalPackageDiscarded& FConcertClientPackageBridge::OnLocalPackageDiscarded()
{
	return OnLocalPackageDiscardedDelegate;
}

bool& FConcertClientPackageBridge::GetIgnoreLocalSaveRef()
{
	return bIgnoreLocalSave;
}

bool& FConcertClientPackageBridge::GetIgnoreLocalDiscardRef()
{
	return bIgnoreLocalDiscard;
}

void FConcertClientPackageBridge::HandlePackagePreSave(UPackage* Package)
{
	// Ignore package operations fired by the cooker (cook on the fly).
	if (GIsCookerLoadingPackage)
	{
		check(IsInGameThread()); // We expect the cooker to call us on the game thread otherwise, we can have concurrency issues.
		return;
	}

	// Ignore unwanted saves
	if (bIgnoreLocalSave)
	{
		return;
	}

	// Early out if the delegate is unbound
	if (!OnLocalPackageEventDelegate.IsBound())
	{
		return;
	}

	UWorld* World = UWorld::FindWorldInPackage(Package);

	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetFName().ToString(), PackageFilename, World ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension()))
	{
		FConcertPackage Event;
		ConcertClientPackageBridgeUtil::FillPackageInfo(Package, EConcertPackageUpdateType::Saved, Event.Info);
		Event.Info.bPreSave = true;
		Event.Info.bAutoSave = GEngine->IsAutosaving();

		if (FFileHelper::LoadFileToArray(Event.PackageData, *PackageFilename))
		{
			OnLocalPackageEventDelegate.Broadcast(Event);
		}
	}

	UE_LOG(LogConcert, Verbose, TEXT("Asset Pre-Saved: %s"), *Package->GetName());
}

void FConcertClientPackageBridge::HandlePackageSaved(const FString& PackageFilename, UObject* Outer)
{
	UPackage* Package = CastChecked<UPackage>(Outer);

	// Ignore package operations fired by the cooker (cook on the fly).
	if (GIsCookerLoadingPackage)
	{
		check(IsInGameThread()); // We expect the cooker to call us on the game thread otherwise, we can have concurrency issues.
		return;
	}

	// Ignore unwanted saves
	if (bIgnoreLocalSave)
	{
		return;
	}

	// Early out if the delegate is unbound
	if (!OnLocalPackageEventDelegate.IsBound())
	{
		return;
	}

	// if we end up here, the package should be either unlocked or locked by this client, the server will resend the latest revision if it wasn't the case.
	FName NewPackageName;
	PackagesBeingRenamed.RemoveAndCopyValue(Package->GetFName(), NewPackageName);

	FConcertPackage Event;
	ConcertClientPackageBridgeUtil::FillPackageInfo(Package, NewPackageName.IsNone() ? EConcertPackageUpdateType::Saved : EConcertPackageUpdateType::Renamed, Event.Info);
	Event.Info.NewPackageName = NewPackageName;
	Event.Info.bPreSave = false;
	Event.Info.bAutoSave = GEngine->IsAutosaving();

	if (FFileHelper::LoadFileToArray(Event.PackageData, *PackageFilename))
	{
		OnLocalPackageEventDelegate.Broadcast(Event);
	}

	UE_LOG(LogConcert, Verbose, TEXT("Asset Saved: %s"), *Package->GetName());
}

void FConcertClientPackageBridge::HandleAssetAdded(UObject *Object)
{
	// Early out if the delegate is unbound
	if (!OnLocalPackageEventDelegate.IsBound())
	{
		return;
	}

	UPackage* Package = Object->GetOutermost();

	// Skip packages that are in the process of being renamed as they are always saved after being added
	if (PackagesBeingRenamed.Contains(Package->GetFName()))
	{
		return;
	}

	// Save this package to disk so that we can send its contents immediately
	{
		FScopedIgnoreLocalSave IgnorePackageSaveScope(*this);
		UWorld* World = UWorld::FindWorldInPackage(Package);

		const FString PackageFilename = FPaths::ProjectIntermediateDir() / TEXT("Concert") / TEXT("Temp") / FGuid::NewGuid().ToString() + (World ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
		if (UPackage::SavePackage(Package, World, RF_Standalone, *PackageFilename, GWarn, nullptr, false, false, SAVE_NoError | SAVE_KeepDirty))
		{
			FConcertPackage Event;
			ConcertClientPackageBridgeUtil::FillPackageInfo(Package, EConcertPackageUpdateType::Added, Event.Info);

			if (FFileHelper::LoadFileToArray(Event.PackageData, *PackageFilename))
			{
				OnLocalPackageEventDelegate.Broadcast(Event);
			}

			IFileManager::Get().Delete(*PackageFilename);
		}
	}

	UE_LOG(LogConcert, Verbose, TEXT("Asset Added: %s"), *Package->GetName());
}

void FConcertClientPackageBridge::HandleAssetDeleted(UObject *Object)
{
	// Early out if the delegate is unbound
	if (!OnLocalPackageEventDelegate.IsBound())
	{
		return;
	}

	UPackage* Package = Object->GetOutermost();

	FConcertPackage Event;
	ConcertClientPackageBridgeUtil::FillPackageInfo(Package, EConcertPackageUpdateType::Deleted, Event.Info);
	OnLocalPackageEventDelegate.Broadcast(Event);

	UE_LOG(LogConcert, Verbose, TEXT("Asset Deleted: %s"), *Package->GetName());
}

void FConcertClientPackageBridge::HandleAssetRenamed(const FAssetData& Data, const FString& OldName)
{
	// A rename operation comes through as:
	//	1) Asset renamed (this notification)
	//	2) Asset added (old asset, which we'll ignore)
	//	3) Asset saved (new asset)
	//	4) Asset saved (old asset, as a redirector)
	const FName OldPackageName = *FPackageName::ObjectPathToPackageName(OldName);
	PackagesBeingRenamed.Add(OldPackageName, Data.PackageName);

	UE_LOG(LogConcert, Verbose, TEXT("Asset Renamed: %s -> %s"), *OldPackageName.ToString(), *Data.PackageName.ToString());
}

void FConcertClientPackageBridge::HandleAssetReload(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent)
{
	// Early out if the delegate is unbound
	if (!OnLocalPackageDiscardedDelegate.IsBound())
	{
		return;
	}

	if (InPackageReloadPhase == EPackageReloadPhase::PrePackageLoad)
	{
		UPackage* Package = const_cast<UPackage*>(InPackageReloadedEvent->GetOldPackage());
		OnLocalPackageDiscardedDelegate.Broadcast(Package);

		UE_LOG(LogConcert, Verbose, TEXT("Asset Discarded: %s"), *Package->GetName());
	}
}

void FConcertClientPackageBridge::HandleMapChanged(UWorld* InWorld, EMapChangeType InMapChangeType)
{
	// Early out if the delegate is unbound
	if (!OnLocalPackageDiscardedDelegate.IsBound())
	{
		return;
	}

	if (InMapChangeType == EMapChangeType::TearDownWorld)
	{
		UPackage* Package = InWorld->GetOutermost();
		OnLocalPackageDiscardedDelegate.Broadcast(Package);

		UE_LOG(LogConcert, Verbose, TEXT("Asset Discarded: %s"), *Package->GetName());
	}
}

#undef LOCTEXT_NAMESPACE
