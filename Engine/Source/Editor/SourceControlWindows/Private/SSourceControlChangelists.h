// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

#include "ISourceControlProvider.h"

struct IChangelistTreeItem;
typedef TSharedPtr<IChangelistTreeItem> FChangelistTreeItemPtr;
typedef TSharedRef<IChangelistTreeItem> FChangelistTreeItemRef;

struct IChangelistTreeItem : TSharedFromThis<IChangelistTreeItem>
{
	enum TreeItemType
	{
		Invalid,
		Changelist,
		File
	};

	/** Get this item's parent. Can be nullptr. */
	FChangelistTreeItemPtr GetParent() const
	{
		return Parent;
	}

	/** Get this item's children, if any. Although we store as weak pointers, they are guaranteed to be valid. */
	const TArray<FChangelistTreeItemPtr>& GetChildren() const
	{
		return Children;
	}

	/** Returns the TreeItem's type */
	const TreeItemType GetTreeItemType() const
	{
		return Type;
	}

	/** Add a child to this item */
	void AddChild(FChangelistTreeItemRef Child)
	{
		Child->Parent = AsShared();
		Children.Add(MoveTemp(Child));
	}

	/** Remove a child from this item */
	void RemoveChild(const FChangelistTreeItemRef& Child)
	{
		if (Children.Remove(Child))
		{
			Child->Parent = nullptr;
		}
	}

protected:
	/** This item's parent, if any. */
	FChangelistTreeItemPtr Parent;

	/** Array of children contained underneath this item */
	TArray<FChangelistTreeItemPtr> Children;

	TreeItemType Type;
};

typedef STreeView<FChangelistTreeItemPtr> SChangelistTree;

class SSourceControlChangelistsWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSourceControlChangelistsWidget) {}
	SLATE_END_ARGS()

	/**
	* Constructor.
	*/
	SSourceControlChangelistsWidget();

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SChangelistTree> CreateTreeviewWidget();

	TSharedRef<ITableRow> OnGenerateRow(FChangelistTreeItemPtr InTreeItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(FChangelistTreeItemPtr InParent, TArray<FChangelistTreeItemPtr>& OutChildren);

	bool OnIsSelectableOrNavigable(FChangelistTreeItemPtr InItem) const;

	void Refresh();
	void OnChangelistsStatusUpdated(const FSourceControlOperationRef&, ECommandResult::Type);

private:

	/** Changelists (root nodes) */
	TArray<FChangelistTreeItemPtr> ChangelistsNodes;

	/** Changelist treeview widget */
	TSharedPtr<SChangelistTree> TreeView;

	TSharedPtr<class FUpdatePendingChangelistsStatus, ESPMode::ThreadSafe> UpdatePendingChangelistsOperation;
};
