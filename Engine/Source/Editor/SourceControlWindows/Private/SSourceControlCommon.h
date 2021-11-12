// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "SourceControlAssetDataCache.h"
#include "ISourceControlProvider.h"
#include "UncontrolledChangelistState.h"

struct FAssetData;
struct IChangelistTreeItem;
typedef TSharedPtr<IChangelistTreeItem> FChangelistTreeItemPtr;
typedef TSharedRef<IChangelistTreeItem> FChangelistTreeItemRef;

struct IChangelistTreeItem : TSharedFromThis<IChangelistTreeItem>
{
	enum TreeItemType
	{
		Invalid,
		Changelist,
		UncontrolledChangelist,
		File,
		OfflineFile,
		ShelvedChangelist, // container for shelved files
		ShelvedFile
	};

	/** Get this item's parent. Can be nullptr. */
	FChangelistTreeItemPtr GetParent() const;

	/** Get this item's children, if any. Although we store as weak pointers, they are guaranteed to be valid. */
	const TArray<FChangelistTreeItemPtr>& GetChildren() const;

	/** Returns the TreeItem's type */
	const TreeItemType GetTreeItemType() const
	{
		return Type;
	}

	/** Add a child to this item */
	void AddChild(FChangelistTreeItemRef Child);

	/** Remove a child from this item */
	void RemoveChild(const FChangelistTreeItemRef& Child);

protected:
	/** This item's parent, if any. */
	FChangelistTreeItemPtr Parent;

	/** Array of children contained underneath this item */
	TArray<FChangelistTreeItemPtr> Children;

	TreeItemType Type;
};

struct FChangelistTreeItem : public IChangelistTreeItem
{
	FChangelistTreeItem(FSourceControlChangelistStateRef InChangelistState)
		: ChangelistState(InChangelistState)
	{
		Type = IChangelistTreeItem::Changelist;
	}

	FText GetDisplayText() const
	{
		return ChangelistState->GetDisplayText();
	}

	FText GetDescriptionText() const
	{
		return ChangelistState->GetDescriptionText();
	}

	FSourceControlChangelistStateRef ChangelistState;
};

struct FUncontrolledChangelistTreeItem : public IChangelistTreeItem
{
	FUncontrolledChangelistTreeItem(FUncontrolledChangelistStateRef InUncontrolledChangelistState)
		: UncontrolledChangelistState(InUncontrolledChangelistState)
	{
		Type = IChangelistTreeItem::UncontrolledChangelist;
	}

	FText GetDisplayText() const
	{
		return UncontrolledChangelistState->GetDisplayText();
	}

	FText GetDescriptionText() const
	{
		return UncontrolledChangelistState->GetDescriptionText();
	}

	FUncontrolledChangelistStateRef UncontrolledChangelistState;
};

typedef TSharedPtr<FUncontrolledChangelistTreeItem> FUncontrolledChangelistTreeItemPtr;
typedef TSharedRef<FUncontrolledChangelistTreeItem> FUncontrolledChangelistTreeItemRef;

struct FShelvedChangelistTreeItem : public IChangelistTreeItem
{
	FShelvedChangelistTreeItem()
	{
		Type = IChangelistTreeItem::ShelvedChangelist;
	}

	FText GetDisplayText() const;
};

struct FFileTreeItem : public IChangelistTreeItem
{
	explicit FFileTreeItem(FSourceControlStateRef InFileState, bool bBeautifyPaths = true, bool bIsShelvedFile = false);

	/** Updates informations based on AssetData */
	void RefreshAssetInformation();

	/** Returns the asset name of the item */
	FText GetAssetName();

	/** Returns the asset path of the item */
	FText GetAssetPath() const { return AssetPath; }

	/** Returns the asset type of the item */
	FText GetAssetType() const { return AssetType; }

	/** Returns the asset type color of the item */
	FSlateColor GetAssetTypeColor() const { return FSlateColor(AssetTypeColor); }

	/** Returns the package name of the item to display */
	FText GetPackageName() const { return PackageName; }

	/** Returns the file name of the item in source control */
	FText GetFileName() const { return FText::FromString(FileState->GetFilename()); }

	/** Returns the name of the icon to be used in the list item widget */
	FName GetIconName() const { return FileState->GetIcon().GetStyleName(); }

	/** Returns the tooltip text for the icon */
	FText GetIconTooltip() const { return FileState->GetDisplayTooltip(); }

	/** Returns the checkbox state of this item */
	ECheckBoxState GetCheckBoxState() const { return CheckBoxState; }

	/** Sets the checkbox state of this item */
	void SetCheckBoxState(ECheckBoxState NewState) { CheckBoxState = NewState; }

	/** true if the item is not in source control and needs to be added prior to checkin */
	bool NeedsAdding() const { return !FileState->IsSourceControlled(); }

	/** true if the item is in source control and is able to be checked in */
	bool CanCheckIn() const { return FileState->CanCheckIn() || FileState->IsDeleted(); }

	/** true if the item is enabled in the list */
	bool IsEnabled() const { return !FileState->IsConflicted() && FileState->IsCurrent(); }

	/** true if the item is source controlled and not marked for add nor for delete */
	bool CanDiff() const { return FileState->IsSourceControlled() && !FileState->IsAdded() && !FileState->IsDeleted(); }

	const FAssetDataArrayPtr& GetAssetData() const
	{
		return Assets;
	}

	bool IsShelved() const { return GetTreeItemType() == IChangelistTreeItem::ShelvedFile; }

private:
	/** Returns a string representing the name of the asset represented by the given AssetData */
	FString RetrieveAssetName(const FAssetData& InAssetData) const;

	/** Returns a string representing the path of the asset represented by the given AssetData */
	FString RetrieveAssetPath(const FAssetData& InAssetData) const;

public:
	/** Shared pointer to the source control state object itself */
	FSourceControlStateRef FileState;

private:
	/** Checkbox state, used only in the Submit dialog */
	ECheckBoxState CheckBoxState;

	/** Cached asset name to display */
	FText AssetName;

	/** Cached asset path to display */
	FText AssetPath;

	/** Cached asset type to display */
	FText AssetType;

	/** Cached asset type related color to display */
	FColor AssetTypeColor;

	/** Cached package name to display */
	FText PackageName;

	/** Matching asset(s) to facilitate Locate in content browser */
	FAssetDataArrayPtr Assets;

	/** Represents the minimum amount of time between to attempt to refresh AssetData */
	const FTimespan MinTimeBetweenUpdate;

	/** Timestamp representing the time at which the last information update was made */
	FTimespan LastUpdateTime;

	/** True if informations returned from the cache are up to date */
	bool bAssetsUpToDate;
};

struct FOfflineFileTreeItem : public IChangelistTreeItem
{
	explicit FOfflineFileTreeItem(const FString& InFilename);

public:
	const FText& GetPackageName() const { return PackageName; }

	const FText& GetDisplayName() const { return AssetName; }

	const FText& GetDisplayPath() const { return AssetPath; }

	const FText& GetDisplayType() const { return AssetType; }

	const FSlateColor& GetDisplayColor() const { return AssetTypeColor; }

private:
	TArray<FAssetData> Assets;
	FText PackageName;
	FText AssetName;
	FText AssetPath;
	FText AssetType;
	FSlateColor AssetTypeColor;
};

typedef TSharedPtr<FFileTreeItem> FFileTreeItemPtr;
typedef TSharedRef<FFileTreeItem> FFileTreeItemRef;

struct FShelvedFileTreeItem : public FFileTreeItem
{
	explicit FShelvedFileTreeItem(FSourceControlStateRef InFileState, bool bBeautifyPaths = true)
		: FFileTreeItem(InFileState, bBeautifyPaths,/*bIsShelved=*/true)
	{}
};

namespace SSourceControlCommon
{
	TSharedRef<SWidget> GetSCCFileWidget(FSourceControlStateRef InFileState, bool bIsShelvedFile = false);
	FText GetDefaultAssetName();
	FText GetDefaultAssetType();
	FText GetDefaultUnknownAssetType();
	FText GetDefaultMultipleAsset();
}