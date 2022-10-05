// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnsavedAssetsTracker.h"

#include "UnsavedAssetsTrackerModule.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "Logging/LogMacros.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "SourceControlFileStatusMonitor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Editor/TransBuffer.h"
#include "Engine/World.h"
#include "UnrealEdGlobals.h"
#include "Misc/PackageName.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "UnsavedAssetsTracker"

DEFINE_LOG_CATEGORY_STATIC(LogUnsavedAssetsTracker, Log, All);

namespace
{

bool ShouldTrackDirtyPackage(const UPackage* Package)
{
	// Ignore the packages that aren't meant to be persistent.
	if (Package->HasAnyFlags(RF_Transient) ||
		Package->HasAnyPackageFlags(PKG_CompiledIn) ||
		Package->HasAnyPackageFlags(PKG_PlayInEditor) ||
		Package == GetTransientPackage() ||
		FPackageName::IsMemoryPackage(Package->GetPathName()))
	{
		return false;
	}
	return true;
}

FString GetHumanFriendlyAssetName(const UPackage* Package)
{
	FName AssetName;
	FName OwnerName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	// Lookup for the first asset in the package
	UObject* FoundAsset = nullptr;
	ForEachObjectWithPackage(Package, [&FoundAsset](UObject* InnerObject)
	{
		if (InnerObject->IsAsset())
		{
			if (FAssetData::IsUAsset(InnerObject))
			{
				// If we found the primary asset, use it
				FoundAsset = InnerObject;
				return false;
			}
			// Otherwise, keep the first found asset but keep looking for a primary asset
			if (!FoundAsset)
			{
				FoundAsset = InnerObject;
			}
		}
		return true;
	}, /*bIncludeNestedObjects*/ false);

	if (FoundAsset)
	{
		TWeakPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(FoundAsset->GetClass());
		if (AssetTypeActions.IsValid())
		{
			AssetName = *AssetTypeActions.Pin()->GetObjectDisplayName(FoundAsset);
		}
		else
		{
			AssetName = FoundAsset->GetFName();
		}

		OwnerName = FoundAsset->GetOutermostObject()->GetFName();
	}

	// Last resort, display the package name
	if (AssetName == NAME_None)
	{
		AssetName = *FPackageName::GetShortName(Package->GetFName());
	}

	return AssetName.ToString();
}

bool HasPackageWritePermissions(const UPackage* Package)
{
	// if we do not have write permission under the mount point for this package log an error in the message log to link to.
	FString PackageName = Package->GetName();
	return GUnrealEd->HasMountWritePermissionForPackage(PackageName);
}

// NOTE: This function is very similar to USourceControlHelpers::PackageFilename() but it doesn't call FindPackage() which can
//       crash the engine when auto-saving packages.
FString GetPackagePathname(const UPackage* InPackage)
{
	auto GetPathnameInternal = [](const UPackage* Package)
	{
		FString PackageName = Package->GetName();
		FString Filename = Package->GetName();

		// Get the filename by finding it on disk first
		if (!FPackageName::DoesPackageExist(PackageName, &Filename))
		{
			// The package does not exist on disk, see if we can find it in memory and predict the file extension
			// Only do this if the supplied package name is valid
			const bool bIncludeReadOnlyRoots = false;
			if (FPackageName::IsValidLongPackageName(PackageName, bIncludeReadOnlyRoots))
			{
				// This is a package in memory that has not yet been saved. Determine the extension and convert to a filename, if we do have the package, just assume normal asset extension
				const FString PackageExtension = Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
				Filename = FPackageName::LongPackageNameToFilename(PackageName, PackageExtension);
			}
		}

		return Filename;
	};

	return FPaths::ConvertRelativePathToFull(GetPathnameInternal(InPackage));
}


} // Anonoymous namespace


FUnsavedAssetsTracker::FUnsavedAssetsTracker()
{
	// Register for the package dirty state updated callback to catch packages that have been cleaned without being saved
	UPackage::PackageDirtyStateChangedEvent.AddRaw(this, &FUnsavedAssetsTracker::OnPackageDirtyStateUpdated);

	// Register for the "MarkPackageDirty" callback to catch packages that have been modified and need to be saved
	UPackage::PackageMarkedDirtyEvent.AddRaw(this, &FUnsavedAssetsTracker::OnPackageMarkedDirty);

	// Register for the package modified callback to catch packages that have been saved
	UPackage::PackageSavedWithContextEvent.AddRaw(this, &FUnsavedAssetsTracker::OnPackageSaved);

	// Hook to detect when a map is changed to refresh to catch when a temporary map is discarded.
	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditor.OnMapChanged().AddRaw(this, &FUnsavedAssetsTracker::OnMapChanged);

	// Hook to detect when a a world is renamed to to catch when a temporary map is saved with a new name.
	FWorldDelegates::OnPostWorldRename.AddRaw(this, &FUnsavedAssetsTracker::OnWorldPostRename);

	// Hook to detect when an Undo/Redo change the dirty state of a package.
	if (GEditor != nullptr && GEditor->Trans != nullptr)
	{
		UTransBuffer* TransBuffer = CastChecked<UTransBuffer>(GEditor->Trans);
		TransBuffer->OnUndo().AddRaw(this, &FUnsavedAssetsTracker::OnUndo);
		TransBuffer->OnRedo().AddRaw(this, &FUnsavedAssetsTracker::OnRedo);
	}
}

FUnsavedAssetsTracker::~FUnsavedAssetsTracker()
{
	UPackage::PackageDirtyStateChangedEvent.RemoveAll(this);
	UPackage::PackageMarkedDirtyEvent.RemoveAll(this);
	UPackage::PackageSavedWithContextEvent.RemoveAll(this);

	if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		LevelEditor->OnMapChanged().RemoveAll(this);
	}

	FWorldDelegates::OnPostWorldRename.RemoveAll(this);

	if (GEditor != nullptr && GEditor->Trans != nullptr)
	{
		UTransBuffer* TransBuffer = CastChecked<UTransBuffer>(GEditor->Trans);
		TransBuffer->OnUndo().RemoveAll(this);
		TransBuffer->OnRedo().RemoveAll(this);
	}
}

int32 FUnsavedAssetsTracker::GetUnsavedAssetNum() const
{
	return UnsavedFiles.Num();
}

TArray<FString> FUnsavedAssetsTracker::GetUnsavedAssets() const
{
	TArray<FString> Pathnames;
	UnsavedFiles.GetKeys(Pathnames);
	return Pathnames;
}

int32 FUnsavedAssetsTracker::GetWarningNum() const
{
	return WarningFiles.Num();
}

TMap<FString, FString> FUnsavedAssetsTracker::GetWarnings() const
{
	TMap<FString, FString> Warnings;
	for (const FString& Pathname: WarningFiles)
	{
		if (const FStatus* Status = UnsavedFiles.Find(Pathname))
		{
			Warnings.Add(Pathname, Status->WarningText.ToString());
		}
		else
		{
			checkNoEntry(); // The lists are out of sync.
		}
	}

	return Warnings;
}

void FUnsavedAssetsTracker::OnPackageMarkedDirty(UPackage* Package, bool bWasDirty)
{
	if (ShouldTrackDirtyPackage(Package))
	{
		StartTrackingDirtyPackage(Package);
	}
}

void FUnsavedAssetsTracker::OnPackageDirtyStateUpdated(UPackage* Package)
{
	if (!ShouldTrackDirtyPackage(Package))
	{
		return;
	}

	if (Package->IsDirty())
	{
		StartTrackingDirtyPackage(Package);
	}
	else
	{
		StopTrackingDirtyPackage(Package);
	}
}

void FUnsavedAssetsTracker::OnPackageSaved(const FString& PackagePathname, UPackage* Package, FObjectPostSaveContext ObjectSaveContext)
{
	if (ObjectSaveContext.IsProceduralSave() || (ObjectSaveContext.GetSaveFlags() & SAVE_FromAutosave) != 0)
	{
		return; // Don't track procedural save (during cooking) nor package auto-saved as backup in case of crash
	}

	if (ShouldTrackDirtyPackage(Package))
	{
		StopTrackingDirtyPackage(Package);
	}
}

void FUnsavedAssetsTracker::SyncWithDirtyPackageList()
{
	// The the list of dirty packages tracked by the engine (considered source of truth)
	TArray<UPackage*> DirtyPackages;
	FEditorFileUtils::GetDirtyPackages(DirtyPackages);

	TArray<FString> ToRemove;

	// Remove packages that used to be dirty but aren't dirty anymore. (usually because the package was saved/renamed at the same time)
	for (const TPair<FString, FStatus>& PathnameStatusPair : UnsavedFiles)
	{
		if (!DirtyPackages.ContainsByPredicate([&PathnameStatusPair](const UPackage* Pkg) { return GetPackagePathname(Pkg) == PathnameStatusPair.Key; }))
		{
			ToRemove.Emplace(PathnameStatusPair.Key);
		}
	}
	for (const FString& PathnameToRemove : ToRemove)
	{
		StopTrackingDirtyPackage(PathnameToRemove);
	}

	// Add packages that aren't tracked yet
	for (UPackage* Package : DirtyPackages)
	{
		if (ShouldTrackDirtyPackage(Package))
		{
			StartTrackingDirtyPackage(Package); // This early out if the package is already tracked.
		}
	}
}

void FUnsavedAssetsTracker::OnUndo(const FTransactionContext& TransactionContext, bool Succeeded)
{
	SyncWithDirtyPackageList();
}

void FUnsavedAssetsTracker::OnRedo(const FTransactionContext& TransactionContext, bool Succeeded)
{
	SyncWithDirtyPackageList();
}

void FUnsavedAssetsTracker::OnWorldPostRename(UWorld* InWorld)
{
	// Saving the temporary 'Untitled' map into a package is a save/rename operation. It is simpler to
	// sync the list of dirty package rather than implementing the rename logic, but a bit less efficient.
	SyncWithDirtyPackageList();
}

void FUnsavedAssetsTracker::OnMapChanged(UWorld* InWorld, EMapChangeType InMapChangeType)
{
	// Changing map sometimes drop changes to the temporary 'Untitled' map. It is simpler to sync the
	// list of dirty package rather than implementing the map tear down logic, but a bit less efficient.
	SyncWithDirtyPackageList();
}

void FUnsavedAssetsTracker::StartTrackingDirtyPackage(UPackage* Package)
{
	checkSlow(ShouldTrackDirtyPackage(Package)); // This check should be done prior calling this function.

	FString PackagePathname = GetPackagePathname(Package);
	if (PackagePathname.IsEmpty())
	{
		return;
	}

	FString HumanFriendlyAssetName = GetHumanFriendlyAssetName(Package);

	int32 UnsavedNumBefore = UnsavedFiles.Num();
	FStatus& Status = UnsavedFiles.FindOrAdd(PackagePathname, FStatus(HumanFriendlyAssetName));
	if (UnsavedFiles.Num() > UnsavedNumBefore) // Detect if a new asset was added.
	{
		if (!HasPackageWritePermissions(Package))
		{
			Status.WarningType = EWarningTypes::PackageWritePermission;
			Status.WarningText = FText::Format(LOCTEXT("Write_Permission_Warning", "Insufficient writing permission to save {0}"), FText::FromString(Package->GetName()));
			WarningFiles.Add(PackagePathname);
			ShowWarningNotificationIfNotAlreadyShown(EWarningTypes::PackageWritePermission, Status.WarningText);
		}

		ISourceControlModule::Get().GetSourceControlFileStatusMonitor().StartMonitoringFile(
			reinterpret_cast<uintptr_t>(this),
			PackagePathname,
			FSourceControlFileStatusMonitor::FOnSourceControlFileStatus::CreateSP(this, &FUnsavedAssetsTracker::OnSourceControlFileStatusUpdate));

		FUnsavedAssetsTrackerModule::Get().OnUnsavedAssetAdded.Broadcast(PackagePathname);

		UE_LOG(LogUnsavedAssetsTracker, Verbose, TEXT("Added file to the unsaved asset list: %s (%s)"), *HumanFriendlyAssetName, *PackagePathname);
	}
}

void FUnsavedAssetsTracker::StopTrackingDirtyPackage(UPackage* Package)
{
	checkSlow(ShouldTrackDirtyPackage(Package)); // This check should be done prior calling this function.

	FString PackagePathname = GetPackagePathname(Package);
	if (PackagePathname.IsEmpty())
	{
		return;
	}

	StopTrackingDirtyPackage(PackagePathname);
}

void FUnsavedAssetsTracker::StopTrackingDirtyPackage(const FString& PackagePathname)
{
	if (FStatus* Status = UnsavedFiles.Find(PackagePathname))
	{
		FString HumanFriendlyAssetName = MoveTemp(Status->HumanFriendlyAssetName); // Keep it for logging.

		UnsavedFiles.Remove(PackagePathname);
		ISourceControlModule::Get().GetSourceControlFileStatusMonitor().StopMonitoringFile(reinterpret_cast<uintptr_t>(this), PackagePathname);

		// Remove warnings this asset was generating (if any).
		WarningFiles.Remove(PackagePathname);
		FUnsavedAssetsTrackerModule::Get().OnUnsavedAssetRemoved.Broadcast(PackagePathname);

		if (WarningFiles.IsEmpty())
		{
			ShownWarnings.Reset();
		}

		UE_LOG(LogUnsavedAssetsTracker, Verbose, TEXT("Removed file from the unsaved asset list: %s (%s)"), *HumanFriendlyAssetName, *PackagePathname);
	}
}

void FUnsavedAssetsTracker::OnSourceControlFileStatusUpdate(const FString& Pathname, const ISourceControlState* State)
{
	auto DiscardWarning = [this](FStatus* InStatus, const FString& Pathname)
	{
		// Source control status update cannot clear the package write permission warning.
		if (InStatus->WarningType != EWarningTypes::PackageWritePermission)
		{
			WarningFiles.Remove(Pathname);
			InStatus->WarningText = FText::GetEmpty();
			InStatus->WarningType = EWarningTypes::None;
		}

		if (WarningFiles.IsEmpty()) // All warning were cleared.
		{
			ShownWarnings.Reset(); // Reeactivate the notification next time a warning happens.
		}
	};

	if (FStatus* Status = UnsavedFiles.Find(Pathname))
	{
		if (Status->WarningType == EWarningTypes::PackageWritePermission)
		{
			return; // Write permission issue has more weight than source control issue.
		}
		else if (State == nullptr) // Source control state was reset. (Changing source control provider/disabling source control)
		{
			DiscardWarning(Status, Pathname);
		}
		else if (TOptional<FText> WarningText = State->GetWarningText())
		{
			Status->WarningText = *WarningText;
			OnSourceControlWarningNotification(*State, *Status);
			WarningFiles.Add(Pathname);
		}
		else
		{
			DiscardWarning(Status, Pathname);
		}
	}
}

void FUnsavedAssetsTracker::PrompToSavePackages()
{
	if (GetUnsavedAssetNum() > 0)
	{
		const bool bPromptUserToSave = true;
		const bool bSaveMapPackages = true;
		const bool bSaveContentPackages = true;
		const bool bFastSave = false;
		const bool bClosingEditor = false;
		const bool bNotifyNoPackagesSaved = true;
		const bool bCanBeDeclined = false;
		if (FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave, bNotifyNoPackagesSaved, bCanBeDeclined))
		{
			// User likely saved something, reset the warnings. We could scan the list of unsaved asset that weren't saved (if any) and check if some warning
			// types remain, but that looks overkill in this context.
			ShownWarnings.Reset();

			// Stay in sync with what the package the engine thinks is dirty.
			SyncWithDirtyPackageList();
		}
	}
}

void FUnsavedAssetsTracker::OnSourceControlWarningNotification(const ISourceControlState& State, FStatus& InOutStatus)
{
	auto UpdateAndShowWarningIfNotAlreadyShown = [this](EWarningTypes WarningType, const FText& Msg, FStatus& InOutStatus)
	{
		// Update the warning type.
		InOutStatus.WarningType = WarningType;
		ShowWarningNotificationIfNotAlreadyShown(WarningType, Msg);
	};

	if (State.IsConflicted())
	{
		UpdateAndShowWarningIfNotAlreadyShown(EWarningTypes::Conflicted, LOCTEXT("Conflicted_Warning", "Warning: Assets you have edited have conflict(s)."), InOutStatus);
	}
	else if (!State.IsCurrent())
	{
		UpdateAndShowWarningIfNotAlreadyShown(EWarningTypes::OutOfDate, LOCTEXT("Out_of_Date_Warning", "Warning: Assets you have edited are out of date."), InOutStatus);
	}
	else if (State.IsCheckedOutOther())
	{
		UpdateAndShowWarningIfNotAlreadyShown(EWarningTypes::CheckedOutByOther, LOCTEXT("Locked_by_Other_Warning", "Warning: Assets you have edited are locked by another user."), InOutStatus);
	}
	else if (!State.IsCheckedOut())
	{
		if (State.IsCheckedOutInOtherBranch())
		{
			UpdateAndShowWarningIfNotAlreadyShown(EWarningTypes::CheckedOutInOtherBranch, LOCTEXT("Checked_Out_In_Other_Branch_Warning", "Warning: Assets you have edited are checked out in another branch."), InOutStatus);
		}
		else if (State.IsModifiedInOtherBranch())
		{
			UpdateAndShowWarningIfNotAlreadyShown(EWarningTypes::ModifiedInOtherBranch, LOCTEXT("Modified_In_Other_Branch_Warning", "Warning: Assets you have edited are modified in another branch."), InOutStatus);
		}
	}
	else if (State.GetWarningText().IsSet())
	{
		UpdateAndShowWarningIfNotAlreadyShown(EWarningTypes::Other, LOCTEXT("Generic_Warning", "Warning: Assets you have edited have warnings."), InOutStatus);
	}
}

void FUnsavedAssetsTracker::ShowWarningNotificationIfNotAlreadyShown(EWarningTypes WarningType, const FText& Msg)
{
	// Show the notification if it hasn't been shown since the last reset/save.
	if (bWarningNotificationEnabled && !ShownWarnings.Contains(WarningType))
	{
		// Setup the notification for operation feedback
		FNotificationInfo Info(Msg);
		Info.Image = FAppStyle::GetBrush("Icons.WarningWithColor");
		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		Notification->SetCompletionState(SNotificationItem::CS_None);
		ShownWarnings.Add(WarningType);
	}
}

#undef LOCTEXT_NAMESPACE
