// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertClientPackageManager.h"
#include "IConcertSession.h"
#include "ConcertSyncClientLiveSession.h"
#include "ConcertSyncSessionDatabase.h"
#include "ConcertLogGlobal.h"
#include "ConcertWorkspaceData.h"
#include "ConcertWorkspaceMessages.h"
#include "ConcertSandboxPlatformFile.h"
#include "ConcertSyncClientUtil.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/Package.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"

#if WITH_EDITOR
	#include "Editor.h"
	#include "Editor/EditorEngine.h"
#endif

#define LOCTEXT_NAMESPACE "ConcertClientPackageManager"

FConcertClientPackageManager::FConcertClientPackageManager(TSharedRef<FConcertSyncClientLiveSession> InLiveSession, IConcertClientPackageBridge* InPackageBridge)
	: LiveSession(InLiveSession)
	, PackageBridge(InPackageBridge)
	, bIgnorePackageDirtyEvent(false)
{
	check(LiveSession->IsValidSession());
	check(EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnablePackages));
	check(PackageBridge);

#if WITH_EDITOR
	if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldUsePackageSandbox))
	{
		// Create Sandbox
		SandboxPlatformFile = MakeUnique<FConcertSandboxPlatformFile>(LiveSession->GetSession().GetSessionWorkingDirectory() / TEXT("Sandbox"));
		SandboxPlatformFile->Initialize(&FPlatformFileManager::Get().GetPlatformFile(), TEXT(""));
	}

	if (GIsEditor)
	{
		// Register Package Events
		UPackage::PackageDirtyStateChangedEvent.AddRaw(this, &FConcertClientPackageManager::HandlePackageDirtyStateChanged);
		PackageBridge->OnLocalPackageEvent().AddRaw(this, &FConcertClientPackageManager::HandleLocalPackageEvent);
	}
#endif	// WITH_EDITOR

	LiveSession->GetSession().RegisterCustomEventHandler<FConcertPackageRejectedEvent>(this, &FConcertClientPackageManager::HandlePackageRejectedEvent);
}

FConcertClientPackageManager::~FConcertClientPackageManager()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		// Unregister Package Events
		UPackage::PackageDirtyStateChangedEvent.RemoveAll(this);
		PackageBridge->OnLocalPackageEvent().RemoveAll(this);
	}

	if (SandboxPlatformFile)
	{
		// Discard Sandbox and gather packages to be reloaded/purged
		SandboxPlatformFile->DiscardSandbox(PackagesPendingHotReload, PackagesPendingPurge);
		SandboxPlatformFile.Reset();
	}
#endif	// WITH_EDITOR

	LiveSession->GetSession().UnregisterCustomEventHandler<FConcertPackageRejectedEvent>(this);

	// Add dirty packages that aren't for purging to the list of hot reload, overlaps with the sandbox are filtered directly in ReloadPackages
	if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldUsePackageSandbox))
	{
		for (const FName DirtyPackageName : DirtyPackages)
		{
			if (!PackagesPendingPurge.Contains(DirtyPackageName))
			{
				PackagesPendingHotReload.Add(DirtyPackageName);
			}
		}
	}

	if (!GIsRequestingExit)
	{
		// Hot reload after unregistering from most delegates to prevent events triggered by hot-reloading (such as asset deleted) to be recorded as transaction.
		SynchronizeInMemoryPackages();
	}
}

bool FConcertClientPackageManager::ShouldIgnorePackageDirtyEvent(class UPackage* InPackage) const
{
	return InPackage == GetTransientPackage()
		|| InPackage->HasAnyFlags(RF_Transient)
		|| InPackage->HasAnyPackageFlags(PKG_PlayInEditor)
		|| bIgnorePackageDirtyEvent;
}

TMap<FString, int64> FConcertClientPackageManager::GetPersistedFiles() const
{
	TMap<FString, int64> PersistedFiles;
#if WITH_EDITOR
	if (SandboxPlatformFile)
	{
		FString PackagePath;
		for (const auto& PersistedFilePair : SandboxPlatformFile->GetPersistedFiles())
		{
			if (FPackageName::TryConvertFilenameToLongPackageName(PersistedFilePair.Key, PackagePath))
			{
				int64 PackageRevision = 0;
				if (LiveSession->GetSessionDatabase().GetPackageHeadRevision(*PackagePath, PackageRevision))
				{
					PersistedFiles.Add(PackagePath, PackageRevision);
				}
			}
		}
	}
#endif
	return PersistedFiles;
}

void FConcertClientPackageManager::SynchronizePersistedFiles(const TMap<FString, int64>& PersistedFiles)
{
#if WITH_EDITOR
	if (SandboxPlatformFile)
	{
		auto GetPackageFilenameForRevision = [this](const FString& PackageName, const int64 PackageRevision, FString& OutPackageFilename) -> bool
		{
			FConcertPackageInfo PackageInfo;
			if (LiveSession->GetSessionDatabase().GetPackageDataForRevision(*PackageName, PackageInfo, nullptr, &PackageRevision))
			{
				if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, OutPackageFilename, PackageInfo.PackageFileExtension))
				{
					OutPackageFilename = FPaths::ConvertRelativePathToFull(MoveTemp(OutPackageFilename));
					return true;
				}
			}
			return false;
		};

		FString PackageFilename;
		TArray<FString> PersistedFilePaths;
		for (const auto& PersistedFilePair : PersistedFiles)
		{
			int64 PackageRevision = 0;
			if (LiveSession->GetSessionDatabase().GetPackageHeadRevision(*PersistedFilePair.Key, PackageRevision))
			{
				// If the current package ledger head revision match the persisted file revision, add the file as persisted
				if (PackageRevision == PersistedFilePair.Value && GetPackageFilenameForRevision(PersistedFilePair.Key, PackageRevision, PackageFilename))
				{
					PersistedFilePaths.Add(PackageFilename);
				}
			}
		}
		SandboxPlatformFile->AddFilesAsPersisted(PersistedFilePaths);
	}
#endif
}

void FConcertClientPackageManager::SynchronizeInMemoryPackages()
{
	// Purge pending packages first, since hot reloading can prompt on them before we clear their dirty flags
	TGuardValue<bool> IgnoreDirtyEventScope(bIgnorePackageDirtyEvent, true);
	IConcertClientPackageBridge::FScopedIgnoreLocalDiscard IgnorePackageDiscardScope(*PackageBridge);
	PurgePendingPackages();
	HotReloadPendingPackages();
}

void FConcertClientPackageManager::HandlePackageDiscarded(UPackage* InPackage)
{
	FConcertPackageUpdateEvent Event;
	Event.Package.Info.PackageName = InPackage->GetFName();
	Event.Package.Info.PackageFileExtension = UWorld::FindWorldInPackage(InPackage) ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
	Event.Package.Info.PackageUpdateType = EConcertPackageUpdateType::Dummy;
	LiveSession->GetSessionDatabase().GetTransactionMaxEventId(Event.Package.Info.TransactionEventIdAtSave);
	LiveSession->GetSession().SendCustomEvent(Event, LiveSession->GetSession().GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
}

void FConcertClientPackageManager::HandleRemotePackage(const FGuid& InSourceEndpointId, const int64 InPackageEventId, const bool bApply)
{
	// Ignore this package if we generated it
	if (InSourceEndpointId == LiveSession->GetSession().GetSessionClientEndpointId())
	{
		return;
	}

	if (!bApply)
	{
		return;
	}

	FConcertSyncPackageEvent PackageEvent;
	if (LiveSession->GetSessionDatabase().GetPackageEvent(InPackageEventId, PackageEvent))
	{
		ApplyPackageUpdate(PackageEvent.Package);
	}
}

void FConcertClientPackageManager::ApplyAllHeadPackageData()
{
	LiveSession->GetSessionDatabase().EnumerateHeadRevisionPackageData([this](FConcertPackage&& InPackage)
	{
		ApplyPackageUpdate(InPackage);
		return true;
	});
}

bool FConcertClientPackageManager::HasSessionChanges() const
{
#if WITH_EDITOR
	return SandboxPlatformFile && SandboxPlatformFile->GatherSandboxChangedFilenames().Num() > 0;
#else
	return false;
#endif	// WITH_EDITOR
}

TArray<FString> FConcertClientPackageManager::GatherSessionChanges()
{
#if WITH_EDITOR
	if (SandboxPlatformFile)
	{
		return SandboxPlatformFile->GatherSandboxChangedFilenames();
	}
#endif	// WITH_EDITOR
	return TArray<FString>();
}

bool FConcertClientPackageManager::PersistSessionChanges(TArrayView<const FString> InFilesToPersist, ISourceControlProvider* SourceControlProvider, TArray<FText>* OutFailureReasons)
{
#if WITH_EDITOR
	if (SandboxPlatformFile)
	{
		return SandboxPlatformFile->PersistSandbox(MoveTemp(InFilesToPersist), SourceControlProvider, OutFailureReasons);
	}
#endif
	return false;
}

void FConcertClientPackageManager::ApplyPackageUpdate(const FConcertPackage& InPackage)
{
	switch (InPackage.Info.PackageUpdateType)
	{
	case EConcertPackageUpdateType::Dummy:
	case EConcertPackageUpdateType::Added:
	case EConcertPackageUpdateType::Saved:
		SavePackageFile(InPackage);
		break;

	case EConcertPackageUpdateType::Renamed:
		DeletePackageFile(InPackage);
		SavePackageFile(InPackage);
		break;

	case EConcertPackageUpdateType::Deleted:
		DeletePackageFile(InPackage);
		break;

	default:
		break;
	}
}

void FConcertClientPackageManager::HandlePackageRejectedEvent(const FConcertSessionContext& InEventContext, const FConcertPackageRejectedEvent& InEvent)
{
	// Package update was rejected, restore the head-revision of the package
	FConcertPackage Package;
	if (LiveSession->GetSessionDatabase().GetPackageDataForRevision(InEvent.PackageName, Package))
	{
		ApplyPackageUpdate(Package);
	}
}

void FConcertClientPackageManager::HandlePackageDirtyStateChanged(UPackage* InPackage)
{
	check(!InPackage->HasAnyFlags(RF_Transient) || InPackage != GetTransientPackage());
	if (InPackage->IsDirty())
	{
		DirtyPackages.Add(InPackage->GetFName());
	}
	else
	{
		DirtyPackages.Remove(InPackage->GetFName());
	}
}

void FConcertClientPackageManager::HandleLocalPackageEvent(const FConcertPackage& Package)
{
	// Ignore unwanted saves
	if (Package.Info.PackageUpdateType == EConcertPackageUpdateType::Saved)
	{
		if (Package.Info.bPreSave)
		{
			// Pre-save events are used to send the pristine package state of a package (if enabled), so make sure we don't already have a history for this package
			FConcertPackageInfo ExistingPackageInfo;
			if (!EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldSendPackagePristineState) || LiveSession->GetSessionDatabase().GetPackageDataForRevision(Package.Info.PackageName, ExistingPackageInfo, nullptr))
			{
				return;
			}
		}
		else
		{
			// Save events may optionally exclude auto-saves
			if (Package.Info.bAutoSave && !EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldSendPackageAutoSaves))
			{
				return;
			}
		}

		if (!FPackageName::IsValidLongPackageName(Package.Info.PackageName.ToString())) // Auto-Save might save the template in /Temp/... which is an invalid long package name.
		{
			return;
		}
	}

	if (Package.Info.PackageUpdateType == EConcertPackageUpdateType::Added && EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldUsePackageSandbox))
	{
		// If this package was locally added and we're using a sandbox, also write it to the correct location on disk (which will be placed into the sandbox directory)
		FString PackageFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Package.Info.PackageName.ToString(), PackageFilename, Package.Info.PackageFileExtension))
		{
			FFileHelper::SaveArrayToFile(Package.PackageData, *PackageFilename);
		}
	}

	FConcertPackageUpdateEvent Event;
	Event.Package = Package;
	LiveSession->GetSessionDatabase().GetTransactionMaxEventId(Event.Package.Info.TransactionEventIdAtSave);
	LiveSession->GetSession().SendCustomEvent(Event, LiveSession->GetSession().GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
}

void FConcertClientPackageManager::SavePackageFile(const FConcertPackage& Package)
{
	// This path should only be taken for non-cooked targets for now
	check(!FPlatformProperties::RequiresCookedData());

	if (Package.PackageData.Num() == 0)
	{
		// If we have no package data set, then this was from a meta-data 
		// only package sync, so we have no new contents to write to disk
		return;
	}

	FString PackageName = Package.Info.PackageName.ToString();
	ConcertSyncClientUtil::FlushPackageLoading(PackageName);

	// Convert long package name to filename
	FString PackageFilename;
	bool bSuccess = FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename, Package.Info.PackageFileExtension);
	if (bSuccess)
	{
		// Overwrite the file on disk
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*PackageFilename, false);
		bSuccess = FFileHelper::SaveArrayToFile(Package.PackageData, *PackageFilename);
	}

	if (bSuccess)
	{
		PackagesPendingHotReload.Add(Package.Info.PackageName);
		PackagesPendingPurge.Remove(Package.Info.PackageName);
	}
}

void FConcertClientPackageManager::DeletePackageFile(const FConcertPackage& Package)
{
	// This path should only be taken for non-cooked targets for now
	check(!FPlatformProperties::RequiresCookedData());

	FString PackageName = Package.Info.PackageName.ToString();
	ConcertSyncClientUtil::FlushPackageLoading(PackageName);

	// Convert long package name to filename
	FString PackageFilenameWildcard;
	bool bSuccess = FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilenameWildcard, TEXT(".*"));
	if (bSuccess)
	{
		// Delete the file on disk
		// We delete any files associated with this package as it may have changed extension type during the session
		TArray<FString> FoundPackageFilenames;
		IFileManager::Get().FindFiles(FoundPackageFilenames, *PackageFilenameWildcard, /*Files*/true, /*Directories*/false);
		const FString PackageDirectory = FPaths::GetPath(PackageFilenameWildcard);
		for (const FString& FoundPackageFilename : FoundPackageFilenames)
		{
			bSuccess |= IFileManager::Get().Delete(*(PackageDirectory / FoundPackageFilename), false, true, true);
		}
	}

	if (bSuccess)
	{
		PackagesPendingPurge.Add(Package.Info.PackageName);
		PackagesPendingHotReload.Remove(Package.Info.PackageName);
	}
}

bool FConcertClientPackageManager::CanHotReloadOrPurge() const
{
	return ConcertSyncClientUtil::CanPerformBlockingAction() && !LiveSession->GetSession().IsSuspended();
}

void FConcertClientPackageManager::HotReloadPendingPackages()
{
	if (CanHotReloadOrPurge())
	{
		ConcertSyncClientUtil::HotReloadPackages(PackagesPendingHotReload);
		PackagesPendingHotReload.Reset();
	}
}

void FConcertClientPackageManager::PurgePendingPackages()
{
	if (CanHotReloadOrPurge())
	{
		ConcertSyncClientUtil::PurgePackages(PackagesPendingPurge);
		PackagesPendingPurge.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
