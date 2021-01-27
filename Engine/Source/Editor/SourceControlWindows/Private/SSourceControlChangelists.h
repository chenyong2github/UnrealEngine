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
		File,
		ShelvedChangelist, // container for shelved files
		ShelvedFile
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
	FSourceControlChangelistStatePtr GetChangelistStateFromSelection();
	FSourceControlChangelistPtr GetChangelistFromSelection();

	/** Returns list of currently selected files */
	TArray<FString> GetSelectedFiles();

	/** Returns list of currently selected shelved files */
	TArray<FString> GetSelectedShelvedFiles();

	/** Changelist operations */
	void OnNewChangelist();
	void OnDeleteChangelist();
	bool CanDeleteChangelist();
	void OnEditChangelist();
	void OnSubmitChangelist();
	bool CanSubmitChangelist();

	/** Changelist & File operations */
	void OnRevertUnchanged();
	bool CanRevertUnchanged();
	void OnRevert();
	bool CanRevert();
	void OnShelve();

	/** Changelist & shelved files operations */
	void OnUnshelve();
	void OnDeleteShelvedFiles();

	/** Files operations */
	void OnLocateFile();
	bool CanLocateFile();
	void OnShowHistory();
	void OnDiffAgainstDepot();
	bool CanDiffAgainstDepot();

	/** Shelved files operations */
	void OnDiffAgainstWorkspace();
	bool CanDiffAgainstWorkspace();

	void OnSourceControlStateChanged();
	void OnSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider);
	void OnChangelistsStatusUpdated(const FSourceControlOperationRef& InOperation, ECommandResult::Type InType);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	struct ExpandedState
	{
		bool bChangelistExpanded;
		bool bShelveExpanded;
	};

	void SaveExpandedState(TMap<FSourceControlChangelistStateRef, ExpandedState>& ExpandedStates) const;
	void RestoreExpandedState(const TMap<FSourceControlChangelistStateRef, ExpandedState>& ExpandedStates);

	TSharedRef<SWidget> MakeToolBar();

private:
	/** Changelists (root nodes) */
	TArray<FChangelistTreeItemPtr> ChangelistsNodes;

	/** Changelist treeview widget */
	TSharedPtr<SChangelistTree> TreeView;

	/** Source control state changed delegate handle */
	FDelegateHandle SourceControlStateChangedDelegateHandle;

	bool bShouldRefresh;
};
