// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertClientPackageManager.h"
#include "IConcertSession.h"
#include "IConcertFileSharingService.h"
#include "ConcertSyncClientLiveSession.h"
#include "ConcertSyncSessionDatabase.h"
#include "ConcertLogGlobal.h"
#include "ConcertWorkspaceData.h"
#include "ConcertWorkspaceMessages.h"
#include "ConcertSandboxPlatformFile.h"
#include "ConcertSyncClientUtil.h"
#include "ConcertUtil.h"

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
	#include "FileHelpers.h"
#endif

#define LOCTEXT_NAMESPACE "ConcertClientPackageManager"

FConcertClientPackageManager::FConcertClientPackageManager(TSharedRef<FConcertSyncClientLiveSession> InLiveSession, IConcertClientPackageBridge* InPackageBridge, TSharedPtr<IConcertFileSharingService> InFileSharingService)
	: LiveSession(MoveTemp(InLiveSession))
	, PackageBridge(InPackageBridge)
	, bIgnorePackageDirtyEvent(false)
	, FileSharingService(MoveTemp(InFileSharingService))
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

	if (!IsEngineExitRequested())
	{
		// Hot reload after unregistering from most delegates to prevent events triggered by hot-reloading (such as asset deleted) to be recorded as transaction.
		SynchronizeInMemoryPackages();
	}
}

bool FConcertClientPackageManager::ShouldIgnorePackageDirtyEvent(class UPackage* InPackage) const
{
	return InPackage == GetTransientPackage()
		|| InPackage->HasAnyFlags(RF_Transient)
		|| InPackage->HasAnyPackageFlags(PKG_PlayInEditor | PKG_CompiledIn) // CompiledIn packages are not considered content for MU. (ex when changing some plugin settings like /Script/DisasterRecoveryClient)
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
			if (LiveSession->GetSessionDatabase().GetPackageInfoForRevision(*PackageName, PackageInfo, &PackageRevision))
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

void FConcertClientPackageManager::QueueDirtyPackagesForReload()
{
	TArray<UPackage*> DirtyPkgs;
#if WITH_EDITOR
	{
		UEditorLoadingAndSavingUtils::GetDirtyMapPackages(DirtyPkgs);
		// strip the current world from the dirty list if it doesn't have a file on disk counterpart
		UWorld* CurrentWorld = ConcertSyncClientUtil::GetCurrentWorld();
		UPackage* WorldPackage = CurrentWorld ? CurrentWorld->GetOutermost() : nullptr;
		if (WorldPackage && WorldPackage->IsDirty() &&
			(WorldPackage->HasAnyPackageFlags(PKG_PlayInEditor | PKG_InMemoryOnly) ||
			WorldPackage->HasAnyFlags(RF_Transient) ||
			WorldPackage->FileName != WorldPackage->GetFName()))
		{
			DirtyPkgs.Remove(WorldPackage);
		}
		UEditorLoadingAndSavingUtils::GetDirtyContentPackages(DirtyPkgs);
	}
#endif
	for (UPackage* DirtyPkg : DirtyPkgs)
	{
		FName PackageName = DirtyPkg->GetFName();
		PackagesPendingHotReload.Add(PackageName);
		PackagesPendingPurge.Remove(PackageName);
	}
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

	LiveSession->GetSessionDatabase().GetPackageEvent(InPackageEventId, [this](FConcertSyncPackageEventData& PackageEvent)
	{
		ApplyPackageUpdate(PackageEvent.MetaData.PackageInfo, PackageEvent.PackageDataStream);
	});
}

void FConcertClientPackageManager::ApplyAllHeadPackageData()
{
	LiveSession->GetSessionDatabase().EnumerateHeadRevisionPackageData([this](const FConcertPackageInfo& InPackageInfo, FConcertPackageDataStream& InPackageDataStream)
	{
		ApplyPackageUpdate(InPackageInfo, InPackageDataStream);
		return true;
	});
}

bool FConcertClientPackageManager::HasSessionChanges() const
{
	bool bHasSessionChanges = false;
	LiveSession->GetSessionDatabase().EnumeratePackageNamesWithHeadRevision([&bHasSessionChanges](const FName PackageName)
	{
		bHasSessionChanges = true;
		return false; // Stop enumeration
	}, /*IgnorePersisted*/true);
	return bHasSessionChanges;
}

bool FConcertClientPackageManager::PersistSessionChanges(TArrayView<const FName> InPackagesToPersist, ISourceControlProvider* SourceControlProvider, TArray<FText>* OutFailureReasons)
{
#if WITH_EDITOR
	if (SandboxPlatformFile)
	{
		// Transform all the package names into actual filenames
		TArray<FString, TInlineAllocator<8>> FilesToPersist;
		FString Filename;
		for (const FName& PackageName : InPackagesToPersist)
		{
			if (FPackageName::DoesPackageExist(PackageName.ToString(), nullptr, &Filename))
			{
				FilesToPersist.Add(MoveTemp(Filename));
			}
		}
		return SandboxPlatformFile->PersistSandbox(FilesToPersist, SourceControlProvider, OutFailureReasons);
	}
#endif
	return false;
}

void FConcertClientPackageManager::ApplyPackageUpdate(const FConcertPackageInfo& InPackageInfo, FConcertPackageDataStream& InPackageDataStream)
{
	switch (InPackageInfo.PackageUpdateType)
	{
	case EConcertPackageUpdateType::Dummy:
	case EConcertPackageUpdateType::Added:
	case EConcertPackageUpdateType::Saved:
		SavePackageFile(InPackageInfo, InPackageDataStream);
		break;

	case EConcertPackageUpdateType::Renamed:
		DeletePackageFile(InPackageInfo);
		SavePackageFile(InPackageInfo, InPackageDataStream);
		break;

	case EConcertPackageUpdateType::Deleted:
		DeletePackageFile(InPackageInfo);
		break;

	default:
		break;
	}
}

void FConcertClientPackageManager::HandlePackageRejectedEvent(const FConcertSessionContext& InEventContext, const FConcertPackageRejectedEvent& InEvent)
{
	// Package update was rejected, restore the head-revision of the package
	LiveSession->GetSessionDatabase().GetPackageDataForRevision(InEvent.PackageName, [this](const FConcertPackageInfo& InPackageInfo, FConcertPackageDataStream& InPackageDataStream)
	{
		ApplyPackageUpdate(InPackageInfo, InPackageDataStream);
	});
}

void FConcertClientPackageManager::HandlePackageDirtyStateChanged(UPackage* InPackage)
{
	check(!InPackage->HasAnyFlags(RF_Transient) || InPackage != GetTransientPackage());
	if (InPackage->IsDirty() && !InPackage->HasAnyPackageFlags(PKG_CompiledIn | PKG_InMemoryOnly)) // Dirty packages are tracked for purge/reload, but 'compiled in' and 'in memory' cannot be hot purged/reloaded.
	{
		DirtyPackages.Add(InPackage->GetFName());
	}
	else
	{
		DirtyPackages.Remove(InPackage->GetFName());
	}
}

void FConcertClientPackageManager::HandleLocalPackageEvent(const FConcertPackageInfo& PackageInfo, const FString& PackagePathname)
{
	// Ignore unwanted saves
	if (PackageInfo.PackageUpdateType == EConcertPackageUpdateType::Saved)
	{
		if (!FPackageName::IsValidLongPackageName(PackageInfo.PackageName.ToString())) // Auto-Save might save the template in /Temp/... which is an invalid long package name.
		{
			return;
		}
		else if (PackageInfo.bAutoSave && !EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldSendPackageAutoSaves))
		{
			return;
		}
		else if (PackageInfo.bPreSave)
		{
			// Pre-save events are used to send the pristine package state of a package (if enabled), so make sure we don't already have a history for this package
			FConcertPackageInfo ExistingPackageInfo;
			if (!EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldSendPackagePristineState) || LiveSession->GetSessionDatabase().GetPackageInfoForRevision(PackageInfo.PackageName, ExistingPackageInfo))
			{
				return;
			}
			// Without live sync feature, the local database is not maintainted after the original 'sync on join'. Package that were not in the original sync don't have a revision from the client point of view.
			else if (!EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableLiveSync) && EmittedPristinePackages.Contains(PackageInfo.PackageName))
			{
				return; // Prevent capturing the original package state at every pre-save.
			}
		}
	}

	if (PackageInfo.PackageUpdateType == EConcertPackageUpdateType::Added && EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldUsePackageSandbox))
	{
		// If this package was locally added and we're using a sandbox, also write it to the correct location on disk (which will be placed into the sandbox directory)
		FString DstPackagePathname;
		if (FPackageName::TryConvertLongPackageNameToFilename(PackageInfo.PackageName.ToString(), DstPackagePathname, PackageInfo.PackageFileExtension))
		{
			if (IFileManager::Get().Copy(*DstPackagePathname, *PackagePathname) != ECopyResult::COPY_OK)
			{
				UE_LOG(LogConcert, Error, TEXT("Failed to copy package file '%s' to the sandbox"), *PackagePathname);
			}
		}
	}

	FConcertPackageUpdateEvent Event;

	// Copy or link the packge data to the Concert event.
	int64 PackageFileSize = PackagePathname.IsEmpty() ? -1 : IFileManager::Get().FileSize(*PackagePathname); // EConcertPackageUpdateType::Delete is emitted with an empty pathname.
	if (PackageFileSize > 0)
	{
		if (CanExchangePackageDataAsByteArray(static_cast<uint64>(PackageFileSize)))
		{
			// Embed the package data directly in the event.
			if (!FFileHelper::LoadFileToArray(Event.Package.PackageData, *PackagePathname))
			{
				UE_LOG(LogConcert, Error, TEXT("Failed to load file data '%s' in memory"), *PackagePathname);
				return;
			}
		}
		else if (FileSharingService && EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableFileSharing))
		{
			// Publish a copy of the package data in the sharing service and set the corresponding file ID in the event.
			if (!FileSharingService->Publish(PackagePathname, Event.Package.FileId))
			{
				UE_LOG(LogConcert, Error, TEXT("Failed to share a copy of package file '%s'"), *PackagePathname);
				return;
			}
		}
		else
		{
			// Notify the client about the file being too large to be emitted.
			UE_LOG(LogConcert, Error, TEXT("Failed to handle local package file '%s'. The file is too big to be sent over the network."), *PackagePathname);
			OnConcertClientPackageTooLargeError().Broadcast(Event.Package.Info, PackageFileSize, FConcertPackage::GetMaxPackageDataSizeEmbeddableAsByteArray());
			return;
		}
	}

	Event.Package.Info = PackageInfo;
	LiveSession->GetSessionDatabase().GetTransactionMaxEventId(Event.Package.Info.TransactionEventIdAtSave);
	LiveSession->GetSession().SendCustomEvent(Event, LiveSession->GetSession().GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);

	if (PackageInfo.bPreSave && EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldSendPackagePristineState) && !EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableLiveSync))
	{
		EmittedPristinePackages.Add(PackageInfo.PackageName); // Prevent sending the original package state at every pre-save.
	}
}

void FConcertClientPackageManager::SavePackageFile(const FConcertPackageInfo& PackageInfo, FConcertPackageDataStream& InPackageDataStream)
{
	// This path should only be taken for non-cooked targets for now
	check(!FPlatformProperties::RequiresCookedData());

	if (!InPackageDataStream.DataAr || InPackageDataStream.DataSize == 0)
	{
		// If we have no package data set, then this was from a meta-data
		// only package sync, so we have no new contents to write to disk
		return;
	}

	FString PackageName = PackageInfo.PackageName.ToString();
	ConcertSyncClientUtil::FlushPackageLoading(PackageName);

	// Convert long package name to filename
	FString PackageFilename;
	bool bSuccess = FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename, PackageInfo.PackageFileExtension);
	if (bSuccess)
	{
		// Overwrite the file on disk
		TUniquePtr<FArchive> DstAr(IFileManager::Get().CreateFileWriter(*PackageFilename, FILEWRITE_EvenIfReadOnly));
		bSuccess = DstAr && ConcertUtil::Copy(*DstAr, *InPackageDataStream.DataAr, InPackageDataStream.DataSize);
	}

	if (bSuccess)
	{
		PackagesPendingHotReload.Add(PackageInfo.PackageName);
		PackagesPendingPurge.Remove(PackageInfo.PackageName);
	}
}

void FConcertClientPackageManager::DeletePackageFile(const FConcertPackageInfo& PackageInfo)
{
	// This path should only be taken for non-cooked targets for now
	check(!FPlatformProperties::RequiresCookedData());

	FString PackageName = PackageInfo.PackageName.ToString();
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
		PackagesPendingPurge.Add(PackageInfo.PackageName);
		PackagesPendingHotReload.Remove(PackageInfo.PackageName);
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

bool FConcertClientPackageManager::CanExchangePackageDataAsByteArray(int64 PackageDataSize) const
{
	if (FileSharingService && EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableFileSharing))
	{
		return FConcertPackage::ShouldEmbedPackageDataAsByteArray(PackageDataSize); // Test the package data size against a preferred limit.
	}

	return FConcertPackage::CanEmbedPackageDataAsByteArray(PackageDataSize); // The the package data size against the maximum permitted.
}

#undef LOCTEXT_NAMESPACE
