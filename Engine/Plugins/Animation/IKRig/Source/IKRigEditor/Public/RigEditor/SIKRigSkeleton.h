// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

#include "SIKRigSolverStack.h"

#include "SIKRigSkeleton.generated.h"

class FIKRigEditorController;
class SIKRigSkeleton;
class FIKRigEditorToolkit;
class USkeletalMesh;

enum class IKRigTreeElementType { BONE, GOAL, EFFECTOR, BONE_SETTINGS };

class FIKRigTreeElement : public TSharedFromThis<FIKRigTreeElement>
{
public:
	
	FIKRigTreeElement(
		const FName& InKey,
		IKRigTreeElementType InType);

	TSharedRef<ITableRow> MakeTreeRowWidget(
		TSharedRef<FIKRigEditorController> InEditorController,
		const TSharedRef<STableViewBase>& InOwnerTable,
		TSharedRef<FIKRigTreeElement> InRigTreeElement,
		TSharedRef<FUICommandList> InCommandList,
		TSharedPtr<SIKRigSkeleton> InHierarchy);

	FName Key;
	IKRigTreeElementType ElementType;
	TSharedPtr<FIKRigTreeElement> Parent;
	TArray<TSharedPtr<FIKRigTreeElement>> Children;

	/** effector meta-data */
	FName EffectorGoalName = NAME_None;
	int32 EffectorSolverIndex = INDEX_NONE;

	/** bone setting meta-data */
	FName BoneSettingBoneName = NAME_None;
	int32 BoneSettingsSolverIndex = INDEX_NONE;

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

	/** Save a snapshot of the internal map that tracks item expansion before tree reconstruction */
	void SaveAndClearSparseItemInfos()
	{
		OldSparseItemInfos = SparseItemInfos;
		ClearExpandedItems();
	}

	/** Restore the expansion infos map from the saved snapshot after tree reconstruction */
	void RestoreSparseItemInfos(TSharedPtr<FIKRigTreeElement> ItemPtr)
	{
		for (const auto& Pair : OldSparseItemInfos)
		{
			if (Pair.Key->Key == ItemPtr->Key)
			{
				// the SparseItemInfos now reference the new element, but keep the same expansion state
				SparseItemInfos.Add(ItemPtr, Pair.Value);
				break;
			}
		}
	}

	/** slow double-click rename state*/
	uint32 LastClickCycles = 0;
	TWeakPtr<FIKRigTreeElement> LastSelected;

private:
	
	/** A temporary snapshot of the SparseItemInfos in STreeView, used during SIKRigSkeleton::RefreshTreeView() */
	TSparseItemMap OldSparseItemInfos;
};

USTRUCT()
struct FIKRigSkeletonImportSettings
{
	GENERATED_BODY()

	FIKRigSkeletonImportSettings() : Mesh(nullptr) {}

	UPROPERTY(EditAnywhere, Category = "Skeleton Import")
	USkeletalMesh* Mesh;
};

class SIKRigSkeleton : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SIKRigSkeleton) {}
	SLATE_END_ARGS()

    ~SIKRigSkeleton() {};

	void Construct(const FArguments& InArgs, TSharedRef<FIKRigEditorController> InEditorController);

private:
	/** SWidget interface */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
	/** END SWidget interface */
	
	/** Bind commands that this widget handles */
	void BindCommands();

	/** creating / renaming / deleting goals */
	void HandleNewGoal();
	bool CanAddNewGoal();
	void HandleRenameElement() const;
	void HandleDeleteItem();
	bool CanDeleteItem() const;
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

	/** per-bone settings */
	void HandleAddBoneSettings();
	bool CanAddBoneSettings();
	void HandleRemoveBoneSettings();
	bool CanRemoveBoneSettings();
	/** END per-bone settings */

	/** selection state queries */
	void GetSelectedBones(TArray<TSharedPtr<FIKRigTreeElement>>& OutBoneItems);
	void GetSelectedGoals(TArray<TSharedPtr<FIKRigTreeElement>>& OutSelectedGoals) const;
	void SetSelectedGoalsFromViewport(const TArray<FName>& GoalNames);
	/** END selection state queries */

	/** skeleton import */
	FReply OnImportSkeletonClicked();
	void ImportSkeleton(const FAssetData& InAssetData);
	EVisibility IsImportButtonVisible() const;
	/** END skeleton import */

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
};
