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
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "UnsavedAssetsTracker"

DEFINE_LOG_CATEGORY_STATIC(LogUnsavedAssetsTracker, Log, All);

namespace
{

bool IsPackagePersistent(UPackage* Package)
{
	// Ignore the packages that cannot be saved to disk.
	if (Package->HasAnyFlags(RF_Transient) || Package->HasAnyPackageFlags(PKG_CompiledIn) || Package == GetTransientPackage())
	{
		return false;
	}
	return true;
}

FString GetHumanFriendlyAssetName(UPackage* Package)
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

bool HasPackageWritePermissions(UPackage* Package)
{
	// if we do not have write permission under the mount point for this package log an error in the message log to link to.
	FString PackageName = Package->GetName();
	return GUnrealEd->HasMountWritePermissionForPackage(PackageName);
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
}

FUnsavedAssetsTracker::~FUnsavedAssetsTracker()
{
	UPackage::PackageDirtyStateChangedEvent.RemoveAll(this);
	UPackage::PackageMarkedDirtyEvent.RemoveAll(this);
	UPackage::PackageSavedWithContextEvent.RemoveAll(this);
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
	AddPackage(Package);
}

void FUnsavedAssetsTracker::OnPackageDirtyStateUpdated(UPackage* Package)
{
	if (Package->IsDirty())
	{
		AddPackage(Package);
	}
	else
	{
		RemovePackage(Package);
	}
}

void FUnsavedAssetsTracker::OnPackageSaved(const FString& PackagePathname, UPackage* Package, FObjectPostSaveContext ObjectSaveContext)
{
	RemovePackage(Package);
}

void FUnsavedAssetsTracker::AddPackage(UPackage* Package)
{
	if (!IsPackagePersistent(Package))
	{
		return;
	}

	FString PackagePathname = SourceControlHelpers::PackageFilename(Package);
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

void FUnsavedAssetsTracker::RemovePackage(UPackage* Package)
{
	FString PackagePathname = SourceControlHelpers::PackageFilename(Package);
	if (PackagePathname.IsEmpty())
	{
		return;
	}

	if (FStatus* Status = UnsavedFiles.Find(PackagePathname))
	{
		FString HumanFriendlyAssetName = MoveTemp(Status->HumanFriendlyAssetName); // Keep it for logging.

		UnsavedFiles.Remove(PackagePathname);
		ISourceControlModule::Get().GetSourceControlFileStatusMonitor().StopMonitoringFile(reinterpret_cast<uintptr_t>(this), PackagePathname);

		// Remove warnings this asset was generating (if any).
		WarningFiles.Remove(PackagePathname);
		FUnsavedAssetsTrackerModule::Get().OnUnsavedAssetRemoved.Broadcast(PackagePathname);

		UE_LOG(LogUnsavedAssetsTracker, Log, TEXT("Removed file from the unsaved asset list: %s (%s)"), *HumanFriendlyAssetName, *PackagePathname);
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
