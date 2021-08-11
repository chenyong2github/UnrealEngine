// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

#include "ISourceControlProvider.h"
#include "SSourceControlCommon.h"

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
	FUncontrolledChangelistStatePtr GetCurrentUncontrolledChangelistState();
	FSourceControlChangelistPtr GetCurrentChangelist();
	FSourceControlChangelistStatePtr GetChangelistStateFromSelection();
	FSourceControlChangelistPtr GetChangelistFromSelection();

	/** Returns list of currently selected files */
	TArray<FString> GetSelectedFiles();

	/**
	 * Splits selected files between Controlled and Uncontrolled files.
	 * @param 	OutControlledFiles 		Selected source controlled files will be added to this array.
	 * @param 	OutUncontrolledFiles	Selected uncontrolled files will be added to this array.
	 */
	void GetSelectedFiles(TArray<FString>& OutControlledFiles, TArray<FString>& OutUncontrolledFiles);

	/**
	 * Splits selected files between Controlled and Uncontrolled files.
	 * @param 	OutControlledFileStates 	Selected source controlled file states will be added to this array.
	 * @param 	OutUncontrolledFileStates	Selected uncontrolled file states will be added to this array.
	 */
	void GetSelectedFileStates(TArray<FSourceControlStateRef>& OutControlledFileStates, TArray<FSourceControlStateRef>& OutUncontrolledFileStates);

	/** Returns list of currently selected shelved files */
	TArray<FString> GetSelectedShelvedFiles();

	/**
	 * Check if the type given as argument is a parent of selected items.
	 * @param 	ParentType 	The parent type to look for.
	 * @return 	True of ParentType is a parent of selected items.
	 */
	bool IsParentOfSelection(const IChangelistTreeItem::TreeItemType ParentType) const;

	/** Changelist operations */
	void OnNewChangelist();
	void OnDeleteChangelist();
	bool CanDeleteChangelist();
	void OnEditChangelist();
	void OnSubmitChangelist();
	bool CanSubmitChangelist();
	void OnValidateChangelist();
	bool CanValidateChangelist();

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
	void OnMoveFiles();
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

	/**
	 * Returns a new changelist description if needed, appending validation tag.
	 * 
	 * @param bInValidationResult	The result of the validation step
	 * @param InOriginalChangelistDescription	Description of the changelist before modification
	 * 
	 * @return The new changelist description
	 */
	FText UpdateChangelistDescriptionToSubmitIfNeeded(const bool bInValidationResult, const FText& InOriginalChangelistDescription) const;
	
	/** Returns true if the provided changelist description contains a validation tag. */
	bool HasValidationTag(const FText& InChangelistDescription) const;

	/** Executes an operation to updates the changelist description of the provided changelist with a new description. */
	void EditChangelistDescription(const FText& InNewChangelistDescription, const FSourceControlChangelistStatePtr& InChangelistState) const;

private:
	/** Tag to append to a changelist that passed validation */
	static const FText ChangelistValidatedTag;

	/** Changelists (root nodes) */
	TArray<FChangelistTreeItemPtr> ChangelistsNodes;

	/** Changelist treeview widget */
	TSharedPtr<SChangelistTree> TreeView;

	/** Source control state changed delegate handle */
	FDelegateHandle SourceControlStateChangedDelegateHandle;

	/** Uncontrolled Changelist changed delegate handle */
	FDelegateHandle UncontrolledChangelistChangedDelegateHandle;

	bool bShouldRefresh;

	void StartRefreshStatus();
	void TickRefreshStatus(double InDeltaTime);
	void EndRefreshStatus();

	FText RefreshStatus;
	bool bIsRefreshing;
	double RefreshStatusTimeElapsed;
};
