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

class SChangelistTree : public STreeView<FChangelistTreeItemPtr>
{
private:
	virtual void Private_SetItemSelection(FChangelistTreeItemPtr TheItem, bool bShouldBeSelected, bool bWasUserDirected = false) override;
};

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

	FReply OnFilesDragged(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent);

	void RequestRefresh();
	void Refresh();
	void ClearChangelistsTree();

	TSharedPtr<SWidget> OnOpenContextMenu();

	/** Returns the currently selected changelist state ptr or null in invalid cases */
	FSourceControlChangelistStatePtr GetCurrentChangelistState();
	FSourceControlChangelistPtr GetCurrentChangelist();
	FSourceControlChangelistStatePtr GetChangelistStateFromFileSelection();
	FSourceControlChangelistPtr GetChangelistFromFileSelection();

	/** Returns list of currently selected files */
	TArray<FString> GetSelectedFiles();

	void OnNewChangelist();
	void OnDeleteChangelist();
	bool CanDeleteChangelist();
	void OnEditChangelist();
	void OnRevertUnchanged();
	void OnSubmitChangelist();
	bool CanSubmitChangelist();

	void OnSourceControlStateChanged();
	void OnSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider);
	void OnChangelistsStatusUpdated(const FSourceControlOperationRef& InOperation, ECommandResult::Type InType);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	/** Changelists (root nodes) */
	TArray<FChangelistTreeItemPtr> ChangelistsNodes;

	/** Changelist treeview widget */
	TSharedPtr<SChangelistTree> TreeView;

	/** Source control state changed delegate handle */
	FDelegateHandle SourceControlStateChangedDelegateHandle;

	bool bShouldRefresh;
};
