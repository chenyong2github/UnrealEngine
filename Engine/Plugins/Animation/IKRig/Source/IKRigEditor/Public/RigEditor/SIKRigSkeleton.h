// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "SIKRigRetargetChainList.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

#include "SIKRigSolverStack.h"

struct FIKRigSkeletonChain;
class FIKRigEditorController;
class SIKRigSkeleton;
class FIKRigEditorToolkit;
class USkeletalMesh;

enum class IKRigTreeElementType { BONE, GOAL, SOLVERGOAL, BONE_SETTINGS };

class FIKRigTreeElement : public TSharedFromThis<FIKRigTreeElement>
{
public:
	
	FIKRigTreeElement(const FText& InKey, IKRigTreeElementType InType);

	TSharedRef<ITableRow> MakeTreeRowWidget(
		TSharedRef<FIKRigEditorController> InEditorController,
		const TSharedRef<STableViewBase>& InOwnerTable,
		TSharedRef<FIKRigTreeElement> InRigTreeElement,
		TSharedRef<FUICommandList> InCommandList,
		TSharedPtr<SIKRigSkeleton> InHierarchy);

	FText Key;
	IKRigTreeElementType ElementType;
	TSharedPtr<FIKRigTreeElement> Parent;
	TArray<TSharedPtr<FIKRigTreeElement>> Children;

	/** effector meta-data (if it is an effector) */
	FName SolverGoalName = NAME_None;
	int32 SolverGoalIndex = INDEX_NONE;

	/** bone setting meta-data (if it is a bone setting) */
	FName BoneSettingBoneName = NAME_None;
	int32 BoneSettingsSolverIndex = INDEX_NONE;

	/** name of bone if it is one */
	FName BoneName = NAME_None;
	
	/** name of goal if it is one */
	FName GoalName = NAME_None;

	/** delegate for when the context menu requests a rename */
	void RequestRename();
	DECLARE_DELEGATE(FOnRenameRequested);
	FOnRenameRequested OnRenameRequested;	
};

class SIKRigSkeletonItem : public STableRow<TSharedPtr<FIKRigTreeElement>>
{
public:
	
	void Construct(
		const FArguments& InArgs,
		TSharedRef<FIKRigEditorController> InEditorController,
		const TSharedRef<STableViewBase>& OwnerTable,
		TSharedRef<FIKRigTreeElement> InRigTreeElement,
		TSharedRef<FUICommandList> InCommandList,
		TSharedPtr<SIKRigSkeleton> InHierarchy);

private:
	
	void OnNameCommitted(const FText& InText, ETextCommit::Type InCommitType) const;
	FText GetName() const;

	TWeakPtr<FIKRigTreeElement> WeakRigTreeElement;
	TWeakPtr<FIKRigEditorController> EditorController;
	TWeakPtr<SIKRigSkeleton> SkeletonView;
};

class FIKRigSkeletonDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FIKRigSkeletonDragDropOp, FDecoratedDragDropOp)
    static TSharedRef<FIKRigSkeletonDragDropOp> New(TWeakPtr<FIKRigTreeElement> InElement);
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
	TWeakPtr<FIKRigTreeElement> Element;
};

class SIKRigSkeletonTreeView : public STreeView<TSharedPtr<FIKRigTreeElement>>
{
public:

	virtual ~SIKRigSkeletonTreeView() {}

	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override
	{
		FReply Reply = STreeView<TSharedPtr<FIKRigTreeElement>>::OnFocusReceived(MyGeometry, InFocusEvent);
		LastClickCycles = FPlatformTime::Cycles();
		return Reply;
	}

	/** Save a snapshot of items expansion and selection state */
	void SaveAndClearState()
	{
		SaveAndClearSparseItemInfos();
		SaveAndClearSelection();
	}
	
	/** Restore items expansion and selection state from the saved snapshot after tree reconstruction */
	void RestoreState(const TSharedPtr<FIKRigTreeElement>& ItemPtr)
	{
		RestoreSparseItemInfos(ItemPtr);
		RestoreSelection(ItemPtr);
	}

	/** slow double-click rename state*/
	uint32 LastClickCycles = 0;
	TWeakPtr<FIKRigTreeElement> LastSelected;

private:

	/** Save a snapshot of the internal map that tracks item expansion before tree reconstruction */
	void SaveAndClearSparseItemInfos()
	{
		OldSparseItemInfos = SparseItemInfos;
		ClearExpandedItems();
	}

	/** Restore the expansion infos map from the saved snapshot after tree reconstruction */
	void RestoreSparseItemInfos(const TSharedPtr<FIKRigTreeElement>& ItemPtr)
	{
		for (const TTuple<TSharedPtr<FIKRigTreeElement>, FSparseItemInfo>& Pair : OldSparseItemInfos)
		{
			if (Pair.Key->Key.EqualTo(ItemPtr->Key))
			{
				// the SparseItemInfos now reference the new element, but keep the same expansion state
				SparseItemInfos.Add(ItemPtr, Pair.Value);
				return;
			}
		}

		// set default state as expanded if not found
		SparseItemInfos.Add(ItemPtr, FSparseItemInfo(true, false));
	}
	
	/** Save a snapshot of the internal set that tracks item selection before tree reconstruction */
	void SaveAndClearSelection()
	{
		OldSelectedItems = SelectedItems;
		ClearSelection();
	}
	
	/** Restore the selection from the saved snapshot after tree reconstruction */
	void RestoreSelection(const TSharedPtr<FIKRigTreeElement>& ItemPtr)
	{
		for (const TSharedPtr<FIKRigTreeElement>& OldItem : OldSelectedItems)
		{
			if (OldItem->Key.EqualTo(ItemPtr->Key))
			{
				// select the new element
				SetItemSelection(ItemPtr, true, ESelectInfo::Direct);
				return;
			}
		}
	}
	
	/** A temporary snapshot of the SparseItemInfos in STreeView, used during SIKRigSkeleton::RefreshTreeView() */
	TSparseItemMap OldSparseItemInfos;

	/** A temporary snapshot of the SelectedItems in SListView, used during SIKRigSkeleton::RefreshTreeView() */
	TItemSet OldSelectedItems;
};

class SIKRigSkeleton : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SIKRigSkeleton) {}
	SLATE_END_ARGS()

    ~SIKRigSkeleton() {};

	void Construct(const FArguments& InArgs, TSharedRef<FIKRigEditorController> InEditorController);

	/** selection state queries */
	static bool IsBoneInSelection(TArray<TSharedPtr<FIKRigTreeElement>>& SelectedBoneItems, const FName& BoneName);
	void GetSelectedBones(TArray<TSharedPtr<FIKRigTreeElement>>& OutBoneItems) const;
	void GetSelectedBoneNames(TArray<FName>& OutSelectedBoneNames) const;
	void GetSelectedGoals(TArray<TSharedPtr<FIKRigTreeElement>>& OutSelectedGoals) const;
	int32 GetNumSelectedGoals();
	void GetSelectedGoalNames(TArray<FName>& OutSelectedGoalNames) const;
	bool IsGoalSelected(const FName& GoalName);
	void AddSelectedItemFromViewport(
		const FName& ItemName,
		IKRigTreeElementType ItemType,
		const bool bReplace);
	void GetSelectedBoneChains(TArray<FIKRigSkeletonChain>& OutChains);
	bool HasSelectedItems() const;
	/** END selection state queries */

private:
	/** SWidget interface */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
	/** END SWidget interface */
	
	/** Bind commands that this widget handles */
	void BindCommands();

	/** directly, programmatically add item to selection */
	void AddItemToSelection(const TSharedPtr<FIKRigTreeElement>& InItem);
	/** directly, programmatically add item to selection */
	void RemoveItemFromSelection(const TSharedPtr<FIKRigTreeElement>& InItem);
	/** directly, programmatically replace item in selection */
	void ReplaceItemInSelection(const FText& OldName, const FText& NewName);

	/** creating / renaming / deleting goals */
	void HandleNewGoal() const;
	bool CanAddNewGoal() const;
	void HandleRenameGoal() const;
	bool CanRenameGoal() const;
	void HandleDeleteElement();
	bool CanDeleteElement() const;
	/** END creating / renaming / deleting goals */

	/** connecting/disconnecting goals to solvers */
	void HandleConnectGoalToSolver();
	void HandleDisconnectGoalFromSolver();
	bool CanConnectGoalToSolvers() const;
	bool CanDisconnectGoalFromSolvers() const;
	void ConnectSelectedGoalsToSelectedSolvers(bool bConnect);
	int32 GetNumSelectedGoalToSolverConnections(bool bCountOnlyConnected) const;
	/** END connecting/disconnecting goals to solvers */

	/** setting root bone */
	void HandleSetRootBoneOnSolvers();
	bool CanSetRootBoneOnSolvers();
	/** END setting root bone */

	/** setting end bone */
	void HandleSetEndBoneOnSolvers();
	bool CanSetEndBoneOnSolvers() const;
	bool HasEndBoneCompatibleSolverSelected() const;
	/** END setting end bone */

	/** per-bone settings */
	void HandleAddBoneSettings();
	bool CanAddBoneSettings();
	void HandleRemoveBoneSettings();
	bool CanRemoveBoneSettings();
	/** END per-bone settings */
	
	/** exclude/include bones */
	void HandleExcludeBone();
	bool CanExcludeBone();
	void HandleIncludeBone();
	bool CanIncludeBone();
	/** END exclude/include bones */

	/** retarget chains */
	void HandleNewRetargetChain();
	bool CanAddNewRetargetChain();
	void HandleSetRetargetRoot();
	bool CanSetRetargetRoot();
	void HandleClearRetargetRoot();
	bool CanClearRetargetRoot();
	/** END retarget chains */

	/** centralized editor controls (facilitate cross-communication between multiple UI elements)*/
	TWeakPtr<FIKRigEditorController> EditorController;
	
	/** command list we bind to */
	TSharedPtr<FUICommandList> CommandList;
	
	/** tree view widget */
	TSharedPtr<SIKRigSkeletonTreeView> TreeView;
	TArray<TSharedPtr<FIKRigTreeElement>> RootElements;
	TArray<TSharedPtr<FIKRigTreeElement>> AllElements;
	
	/** tree view callbacks */
	void RefreshTreeView(bool IsInitialSetup=false);
	TSharedRef<ITableRow> MakeTableRowWidget(TSharedPtr<FIKRigTreeElement> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleGetChildrenForTree(TSharedPtr<FIKRigTreeElement> InItem, TArray<TSharedPtr<FIKRigTreeElement>>& OutChildren);
	void OnSelectionChanged(TSharedPtr<FIKRigTreeElement> Selection, ESelectInfo::Type SelectInfo);
	TSharedPtr< SWidget > CreateContextMenu();
	void OnItemClicked(TSharedPtr<FIKRigTreeElement> InItem);
	void OnItemDoubleClicked(TSharedPtr<FIKRigTreeElement> InItem);
	void OnSetExpansionRecursive(TSharedPtr<FIKRigTreeElement> InItem, bool bShouldBeExpanded);
	void SetExpansionRecursive(TSharedPtr<FIKRigTreeElement> InElement, bool bTowardsParent, bool bShouldBeExpanded);
	void FillContextMenu(FMenuBuilder& MenuBuilder);
	/** END tree view callbacks */

	/** drag and drop */
	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	TOptional<EItemDropZone> OnCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FIKRigTreeElement> TargetItem);
	FReply OnAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FIKRigTreeElement> TargetItem);
	/** END drag and drop */

	friend SIKRigSkeletonItem;
	friend FIKRigEditorController;
	friend SIKRigSolverStack;
	friend SIKRigRetargetChainList;
};
