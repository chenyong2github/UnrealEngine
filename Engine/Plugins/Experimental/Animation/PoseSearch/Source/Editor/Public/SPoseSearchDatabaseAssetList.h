// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch/PoseSearch.h"
#include "Widgets/Views/STreeView.h"
#include "EditorUndoClient.h"

class FPoseSearchDatabaseViewModel;

namespace UE::PoseSearch
{
	class SDatabaseAssetTree;

	class FDatabaseAssetTreeNode : public TSharedFromThis<FDatabaseAssetTreeNode>
	{

	public:
		FDatabaseAssetTreeNode(
			int32 InSourceAssetIdx,
			ESearchIndexAssetType InSourceAssetType,
			const TSharedRef<FPoseSearchDatabaseViewModel>& InEditorViewModel);

		TSharedRef<ITableRow> MakeTreeRowWidget(
			const TSharedRef<STableViewBase>& InOwnerTable,
			TSharedRef<FDatabaseAssetTreeNode> InDatabaseAssetNode,
			TSharedRef<FUICommandList> InCommandList,
			TSharedPtr<SDatabaseAssetTree> InHierarchy);

		int32 SourceAssetIdx;
		ESearchIndexAssetType SourceAssetType;
		TSharedPtr<FDatabaseAssetTreeNode> Parent;
		TArray<TSharedPtr<FDatabaseAssetTreeNode>> Children;

	protected:

		TWeakPtr<FPoseSearchDatabaseViewModel> EditorViewModel;

	};

	class SDatabaseAssetListItem : public STableRow<TSharedPtr<FDatabaseAssetTreeNode>>
	{
	public:
		void Construct(
			const FArguments& InArgs,
			const TSharedRef<FPoseSearchDatabaseViewModel>& InEditorViewModel,
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

		TWeakPtr<FDatabaseAssetTreeNode> WeakAssetTreeNode;
		TWeakPtr<FPoseSearchDatabaseViewModel> EditorViewModel;
		TWeakPtr<SDatabaseAssetTree> SkeletonView;
	};

	class SDatabaseAssetTree : public SCompoundWidget, public FEditorUndoClient
	{
	public:
		SLATE_BEGIN_ARGS(SDatabaseAssetTree) {}
		SLATE_END_ARGS()

		virtual ~SDatabaseAssetTree();

		void Construct(const FArguments& InArgs, TSharedRef<FPoseSearchDatabaseViewModel> InEditorViewModel);

		// SWidget interface
		virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
		virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
		// End SWidget interface
	
		void RefreshTreeView(bool IsInitialSetup = false);

	protected:
		TWeakPtr<FPoseSearchDatabaseViewModel> EditorViewModel;

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
		
		void OnAddGroup();
		void OnAddSequence();
		void OnAddBlendSpace();

		void OnDeleteAsset(TSharedPtr<FDatabaseAssetTreeNode> Node);
		void OnRemoveFromGroup(TSharedPtr<FDatabaseAssetTreeNode> Node);
		void OnDeleteGroup(TSharedPtr<FDatabaseAssetTreeNode> Node);

		friend SDatabaseAssetListItem;
	};
}

