// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseAssetTreeNode.h"
#include "PoseSearchDatabaseViewModel.h"

#include "PoseSearch/PoseSearch.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimComposite.h"
#include "Animation/BlendSpace.h"

#include "Misc/TransactionObjectEvent.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Misc/FeedbackContext.h"
#include "AssetSelection.h"
#include "ClassIconFinder.h"
#include "DetailColumnSizeData.h"
#include "PoseSearchDatabaseAssetListItem.h"

#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/SListView.h"

#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SPositiveActionButton.h"
#include "Styling/AppStyle.h"

#include "ScopedTransaction.h"
#include "Styling/StyleColors.h"

namespace UE::PoseSearch
{
	FDatabaseAssetTreeNode::FDatabaseAssetTreeNode(
		int32 InSourceAssetIdx,
		ESearchIndexAssetType InSourceAssetType,
		const TSharedRef<FDatabaseViewModel>& InEditorViewModel) 
		: SourceAssetIdx(InSourceAssetIdx)
		, SourceAssetType(InSourceAssetType)
		, EditorViewModel(InEditorViewModel)
	{ }

	TSharedRef<ITableRow> FDatabaseAssetTreeNode::MakeTreeRowWidget(
		const TSharedRef<STableViewBase>& InOwnerTable,
		TSharedRef<FDatabaseAssetTreeNode> InDatabaseAssetNode,
		TSharedRef<FUICommandList> InCommandList,
		TSharedPtr<SDatabaseAssetTree> InHierarchy)
	{
		return SNew(
			SDatabaseAssetListItem, 
			EditorViewModel.Pin().ToSharedRef(), 
			InOwnerTable, 
			InDatabaseAssetNode, 
			InCommandList, 
			InHierarchy);
	}

	bool FDatabaseAssetTreeNode::IsRootMotionEnabled() const
	{
		if (const UPoseSearchDatabase* Database = EditorViewModel.Pin()->GetPoseSearchDatabase())
		{
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetAnimationAssetBase(SourceAssetIdx))
			{
				return DatabaseAnimationAsset->IsRootMotionEnabled();
			}
		}

		return false;
	}

	bool FDatabaseAssetTreeNode::IsLooping() const
	{
		if (const UPoseSearchDatabase* Database = EditorViewModel.Pin()->GetPoseSearchDatabase())
		{
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetAnimationAssetBase(SourceAssetIdx))
			{
				return DatabaseAnimationAsset->IsLooping();
			}
		}

		return false;
	}

	EPoseSearchMirrorOption FDatabaseAssetTreeNode::GetMirrorOption() const
	{
		if (const UPoseSearchDatabase* Database = EditorViewModel.Pin()->GetPoseSearchDatabase())
		{
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetAnimationAssetBase(SourceAssetIdx))
			{
				return DatabaseAnimationAsset->GetMirrorOption();
			}
		}

		return EPoseSearchMirrorOption::Invalid;
	}
}
