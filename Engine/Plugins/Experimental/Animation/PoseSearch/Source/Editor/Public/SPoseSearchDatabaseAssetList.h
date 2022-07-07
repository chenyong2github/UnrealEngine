// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch/PoseSearch.h"
#include "Widgets/Views/STreeView.h"
#include "EditorUndoClient.h"

class FUICommandList;

namespace UE::PoseSearch
{
	class FDatabaseViewModel;
	class SDatabaseAssetTree;

	class FDatabaseAssetTreeNode : public TSharedFromThis<FDatabaseAssetTreeNode>
	{

	public:
		FDatabaseAssetTreeNode(
			int32 InSourceAssetIdx,
			ESearchIndexAssetType InSourceAssetType,
			const TSharedRef<FDatabaseViewModel>& InEditorViewModel);

		TSharedRef<ITableRow> MakeTreeRowWidget(
			const TSharedRef<STableViewBase>& InOwnerTable,
			TSharedRef<FDatabaseAssetTreeNode> InDatabaseAssetNode,
			TSharedRef<FUICommandList> InCommandList,
			TSharedPtr<SDatabaseAssetTree> InHierarchy);

		int32 SourceAssetIdx;
		ESearchIndexAssetType SourceAssetType;
		TSharedPtr<FDatabaseAssetTreeNode> Parent;
		TArray<TSharedPtr<FDatabaseAssetTreeNode>> Children;
		TWeakPtr<FDatabaseViewModel> EditorViewModel;
	};

	class SDatabaseAssetListItem : public STableRow<TSharedPtr<FDatabaseAssetTreeNode>>
	{
	public:
		void Construct(
			const FArguments& InArgs,
			const TSharedRef<FDatabaseViewModel>& InEditorViewModel,
			const TSharedRef<STableViewBase>& OwnerTable,
			TSharedRef<FDatabaseAssetTreeNode> InAssetTreeNode,
			TSharedRef<FUICommandList> InCommandList,
			TSharedPtr<SDatabaseAssetTree> InHierarchy);


	protected:
		FText GetName() const;
		TSharedRef<SWidget> GenerateItemWidget();
		TSharedRef<SWidget> GenerateAddButtonWidget();

		const FSlateBrush* GetGroupBackgroundImage() const;
		void ConstructGroupItem(const TSharedRef<STableViewBase>& OwnerTable);
		void ConstructAssetItem(const TSharedRef<STableViewBase>& OwnerTable);

		void OnAddSequence();
		void OnAddBlendSpace();

		EVisibility GetSelectedActorIconVisbility() const;

		TWeakPtr<FDatabaseAssetTreeNode> WeakAssetTreeNode;
		TWeakPtr<FDatabaseViewModel> EditorViewModel;
		TWeakPtr<SDatabaseAssetTree> SkeletonView;
	};

	class SDatabaseAssetTree : public SCompoundWidget, public FEditorUndoClient
	{
	public:
		SLATE_BEGIN_ARGS(SDatabaseAssetTree) {}
		SLATE_END_ARGS()

		virtual ~SDatabaseAssetTree();

		void Construct(const FArguments& InArgs, TSharedRef<FDatabaseViewModel> InEditorViewModel);

		// SWidget interface
		virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
		virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
		virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
		// End SWidget interface
	
		void RefreshTreeView(bool bIsInitialSetup = false, bool bRecoverSelection = false);
		void FinalizeTreeChanges(bool bRecoverSelection = false);

	protected:
		TWeakPtr<FDatabaseViewModel> EditorViewModel;

		/** command list we bind to */
		TSharedPtr<FUICommandList> CommandList;

		/** tree view widget */
		TSharedPtr<STreeView<TSharedPtr<FDatabaseAssetTreeNode>>> TreeView;
		TArray<TSharedPtr<FDatabaseAssetTreeNode>> RootNodes;
		TArray<TSharedPtr<FDatabaseAssetTreeNode>> AllNodes;

		TSharedRef<ITableRow> MakeTableRowWidget(
			TSharedPtr<FDatabaseAssetTreeNode> InItem,
			const TSharedRef<STableViewBase>& OwnerTable);
		void HandleGetChildrenForTree(
			TSharedPtr<FDatabaseAssetTreeNode> InNode, 
			TArray<TSharedPtr<FDatabaseAssetTreeNode>>& OutChildren);

		TOptional<EItemDropZone> OnCanAcceptDrop(
			const FDragDropEvent& DragDropEvent, 
			EItemDropZone DropZone, 
			TSharedPtr<FDatabaseAssetTreeNode> TargetItem);

		FReply OnAcceptDrop(
			const FDragDropEvent& DragDropEvent,
			EItemDropZone DropZone,
			TSharedPtr<FDatabaseAssetTreeNode> TargetItem);

		int32 FindGroupIndex(TSharedPtr<FDatabaseAssetTreeNode> TargetItem);

		TSharedRef<SWidget> CreateAddNewMenuWidget();
		TSharedPtr<SWidget> CreateContextMenu();
		
		void OnAddGroup(bool bFinalizeChanges = true);
		void OnAddSequence(bool bFinalizeChanges = true);
		void OnAddBlendSpace(bool bFinalizeChanges = true);

		void OnDeleteAsset(TSharedPtr<FDatabaseAssetTreeNode> Node, bool bFinalizeChanges = true);
		void OnRemoveFromGroup(TSharedPtr<FDatabaseAssetTreeNode> Node, bool bFinalizeChanges = true);
		void OnDeleteGroup(TSharedPtr<FDatabaseAssetTreeNode> Node, bool bFinalizeChanges = true);

		void CreateCommandList();

		/** Removes existing selected component nodes from the tree*/
		bool CanDeleteNodes() const;
		void OnDeleteNodes();

		friend SDatabaseAssetListItem;

	protected:
		// Called when an item is selected/deselected
		DECLARE_MULTICAST_DELEGATE_TwoParams(
			FOnSelectionChangedMulticaster, 
			const TArrayView<TSharedPtr<FDatabaseAssetTreeNode>>& /* InSelectedItems */,
			ESelectInfo::Type /* SelectInfo */);

		FOnSelectionChangedMulticaster OnSelectionChanged;

	public:
		typedef FOnSelectionChangedMulticaster::FDelegate FOnSelectionChanged;
		void RegisterOnSelectionChanged(const FOnSelectionChanged& Delegate);
		void UnregisterOnSelectionChanged(void* Unregister);

		void RecoverSelection(const TArray<TSharedPtr<FDatabaseAssetTreeNode>>& PreviouslySelectedNodes);
	};
}

