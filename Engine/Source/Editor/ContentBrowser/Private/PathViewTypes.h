// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserItem.h"

/** A folder item shown in the asset tree */
class FTreeItem : public TSharedFromThis<FTreeItem>
{
public:
	FTreeItem() = default;

	explicit FTreeItem(FContentBrowserItem&& InItem);
	explicit FTreeItem(const FContentBrowserItem& InItem);

	explicit FTreeItem(FContentBrowserItemData&& InItemData);
	explicit FTreeItem(const FContentBrowserItemData& InItemData);

	void AppendItemData(const FContentBrowserItem& InItem);

	void AppendItemData(const FContentBrowserItemData& InItemData);

	void RemoveItemData(const FContentBrowserItem& InItem);

	void RemoveItemData(const FContentBrowserItemData& InItemData);

	/** Get the underlying Content Browser item */
	const FContentBrowserItem& GetItem() const;

	/** Get the event fired whenever a rename is requested */
	FSimpleMulticastDelegate& OnRenameRequested();

	/** True if this folder is in the process of being named */
	bool IsNamingFolder() const;

	/** Set whether this folder is in the process of being named */
	void SetNamingFolder(const bool InNamingFolder);

	/** Returns true if this item is a child of the specified item */
	bool IsChildOf(const FTreeItem& InParent);

	/** Returns the child item by name or NULL if the child does not exist */
	TSharedPtr<FTreeItem> GetChild(const FName InChildFolderName) const;

	/** Finds the child who's path matches the one specified */
	TSharedPtr<FTreeItem> FindItemRecursive(const FName InFullPath);

	/** Request that the children be sorted the next time someone calls SortChildrenIfNeeded */
	void RequestSortChildren();

	/** Sort the children by name (but only if they need to be) */
	void SortChildrenIfNeeded();

	/** Represents a folder that does not correspond to a mounted location */
	bool IsDisplayOnlyFolder() const;

	/** Follows tree until it finds folders that are not display only*/
	void ExpandToNonDisplayOnlyFolders(TArray<TSharedPtr<FTreeItem>>& OutTreeItems);

public:
	/** The children of this tree item */
	TArray<TSharedPtr<FTreeItem>> Children;

	/** The parent folder for this item */
	TWeakPtr<FTreeItem> Parent;

private:
	/** Underlying Content Browser item data */
	FContentBrowserItem Item;

	/** Broadcasts whenever a rename is requested */
	FSimpleMulticastDelegate RenameRequestedEvent;

	/** If true, this folder is in the process of being named */
	bool bNamingFolder = false;

	/** If true, the children of this item need sorting */
	bool bChildrenRequireSort = false;
};
