// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlState.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SkeinSourceControl.State"

int32 FSkeinSourceControlState::GetHistorySize() const
{
	return 0;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FSkeinSourceControlState::GetHistoryItem(int32 HistoryIndex) const
{
	return nullptr;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FSkeinSourceControlState::FindHistoryRevision(int32 RevisionNumber) const
{
	return nullptr;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FSkeinSourceControlState::FindHistoryRevision(const FString& InRevision) const
{
	return nullptr;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FSkeinSourceControlState::GetBaseRevForMerge() const
{
	return nullptr;
}

FSlateIcon FSkeinSourceControlState::GetIcon() const
{
	switch (State)
	{
	case ESkeinState::Modified:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Subversion.CheckedOut");
	case ESkeinState::Added:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Subversion.OpenForAdd");
	case ESkeinState::Renamed:
	case ESkeinState::Copied:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Subversion.Branched");
	case ESkeinState::Deleted: // Deleted & Missing files do not show in Content Browser
	case ESkeinState::Missing:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Subversion.MarkedForDelete");
	case ESkeinState::Conflicted:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Subversion.NotAtHeadRevision");
	case ESkeinState::NotControlled:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Subversion.NotInDepot");
	case ESkeinState::Unknown:
	case ESkeinState::Unchanged: // Unchanged is the same as "Pristine" (not checked out) for Perforce, ie no icon
	case ESkeinState::Ignored:
	default:
		return FSlateIcon();
	}

	return FSlateIcon();
}


FText FSkeinSourceControlState::GetDisplayName() const
{
	switch (State)
	{
	case ESkeinState::Unknown:
		return LOCTEXT("Unknown", "Unknown");
	case ESkeinState::Unchanged:
		return LOCTEXT("Unchanged", "Unchanged");
	case ESkeinState::Added:
		return LOCTEXT("Added", "Added");
	case ESkeinState::Deleted:
		return LOCTEXT("Deleted", "Deleted");
	case ESkeinState::Modified:
		return LOCTEXT("Modified", "Modified");
	case ESkeinState::Renamed:
		return LOCTEXT("Renamed", "Renamed");
	case ESkeinState::Copied:
		return LOCTEXT("Copied", "Copied");
	case ESkeinState::Conflicted:
		return LOCTEXT("ContentsConflict", "Contents Conflict");
	case ESkeinState::Ignored:
		return LOCTEXT("Ignored", "Ignored");
	case ESkeinState::NotControlled:
		return LOCTEXT("NotControlled", "Not Under Source Control");
	case ESkeinState::Missing:
		return LOCTEXT("Missing", "Missing");
	}

	return FText();
}

FText FSkeinSourceControlState::GetDisplayTooltip() const
{
	switch (State)
	{
	case ESkeinState::Unknown:
		return LOCTEXT("Unknown_Tooltip", "Unknown source control state");
	case ESkeinState::Unchanged:
		return LOCTEXT("Pristine_Tooltip", "There are no modifications");
	case ESkeinState::Added:
		return LOCTEXT("Added_Tooltip", "Item is scheduled for addition");
	case ESkeinState::Deleted:
		return LOCTEXT("Deleted_Tooltip", "Item is scheduled for deletion");
	case ESkeinState::Modified:
		return LOCTEXT("Modified_Tooltip", "Item has been modified");
	case ESkeinState::Renamed:
		return LOCTEXT("Renamed_Tooltip", "Item has been renamed");
	case ESkeinState::Copied:
		return LOCTEXT("Copied_Tooltip", "Item has been copied");
	case ESkeinState::Conflicted:
		return LOCTEXT("ContentsConflict_Tooltip", "The contents of the item conflict with updates received from the repository.");
	case ESkeinState::Ignored:
		return LOCTEXT("Ignored_Tooltip", "Item is being ignored.");
	case ESkeinState::NotControlled:
		return LOCTEXT("NotControlled_Tooltip", "Item is not under version control.");
	case ESkeinState::Missing:
		return LOCTEXT("Missing_Tooltip", "Item is missing (e.g., you moved or deleted it without using Skein). This also indicates that a directory is incomplete (a checkout or update was interrupted).");
	}

	return FText();
}

const FString& FSkeinSourceControlState::GetFilename() const
{
	return Filename;
}

const FDateTime& FSkeinSourceControlState::GetTimeStamp() const
{
	return TimeStamp;
}

// Deleted and Missing assets cannot appear in the Content Browser, but the do in the Submit files to Source Control window!
bool FSkeinSourceControlState::CanCheckIn() const
{
	return State == ESkeinState::Added
		|| State == ESkeinState::Deleted
		|| State == ESkeinState::Missing
		|| State == ESkeinState::Modified
		|| State == ESkeinState::Renamed;
}

bool FSkeinSourceControlState::CanCheckout() const
{
	return false; // With Skein all tracked files in the working copy are always already checked-out (as opposed to Perforce)
}

bool FSkeinSourceControlState::IsCheckedOut() const
{
	return IsSourceControlled(); // With Skein all tracked files in the working copy are always checked-out (as opposed to Perforce)
}

bool FSkeinSourceControlState::IsCheckedOutOther(FString* Who) const
{
	return false; // Skein does not lock checked-out files as Perforce does
}

bool FSkeinSourceControlState::IsCurrent() const
{
	return true; // Could check the state of the HEAD versus the state of tracked branch on remote
}

bool FSkeinSourceControlState::IsSourceControlled() const
{
	return State != ESkeinState::NotControlled && State != ESkeinState::Ignored && State != ESkeinState::Unknown;
}

bool FSkeinSourceControlState::IsAdded() const
{
	return State == ESkeinState::Added;
}

bool FSkeinSourceControlState::IsDeleted() const
{
	return State == ESkeinState::Deleted || State == ESkeinState::Missing;
}

bool FSkeinSourceControlState::IsIgnored() const
{
	return State == ESkeinState::Ignored;
}

bool FSkeinSourceControlState::CanEdit() const
{
	return true; // With Skein all files in the working copy are always editable (as opposed to Perforce)
}

bool FSkeinSourceControlState::CanDelete() const
{
	return IsSourceControlled() && IsCurrent();
}

bool FSkeinSourceControlState::IsUnknown() const
{
	return State == ESkeinState::Unknown;
}

bool FSkeinSourceControlState::IsModified() const
{
	// Warning: for Perforce, a checked-out file is locked for modification (whereas with Skein all tracked files are checked-out),
	// so for a clean "check-in" (commit) checked-out files unmodified should be removed from the changeset (the index)
	// http://stackoverflow.com/questions/12357971/what-does-revert-unchanged-files-mean-in-perforce
	//
	// Thus, before check-in UE Editor call RevertUnchangedFiles() in PromptForCheckin() and CheckinFiles().
	//
	// So here we must take care to enumerate all states that need to be commited,
	// all other will be discarded :
	//  - Unknown
	//  - Unchanged
	//  - NotControlled
	//  - Ignored
	return State == ESkeinState::Added
		|| State == ESkeinState::Deleted
		|| State == ESkeinState::Modified
		|| State == ESkeinState::Renamed
		|| State == ESkeinState::Copied
		|| State == ESkeinState::Conflicted
		|| State == ESkeinState::Missing;
}

bool FSkeinSourceControlState::CanAdd() const
{
	return State == ESkeinState::NotControlled;
}

bool FSkeinSourceControlState::IsConflicted() const
{
	return State == ESkeinState::Conflicted;
}

bool FSkeinSourceControlState::CanRevert() const
{
	return CanCheckIn();
}

#undef LOCTEXT_NAMESPACE
