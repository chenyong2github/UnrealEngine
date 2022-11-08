// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseEditorReflection.h"
#include "SPoseSearchDatabaseAssetList.h"
#include "PoseSearchDatabaseViewModel.h"

#define LOCTEXT_NAMESPACE "UPoseSearchDatabaseReflection"

#if WITH_EDITOR

void UPoseSearchDatabaseReflectionBase::SetSourceLink(
	const TWeakPtr<UE::PoseSearch::FDatabaseAssetTreeNode>& InWeakAssetTreeNode,
	const TSharedPtr<UE::PoseSearch::SDatabaseAssetTree>& InAssetTreeWidget)
{
	WeakAssetTreeNode = InWeakAssetTreeNode;
	AssetTreeWidget = InAssetTreeWidget;
}

void UPoseSearchDatabaseSequenceReflection::PostEditChangeProperty(
	struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	check(WeakAssetTreeNode.Pin()->SourceAssetType == ESearchIndexAssetType::Sequence);

	UPoseSearchDatabase* Database = WeakAssetTreeNode.Pin()->EditorViewModel.Pin()->GetPoseSearchDatabase();
	if (IsValid(Database))
	{
		Database->Sequences[WeakAssetTreeNode.Pin()->SourceAssetIdx] = Sequence;
		AssetTreeWidget->FinalizeTreeChanges(true);
	}
}

void UPoseSearchDatabaseBlendSpaceReflection::PostEditChangeProperty(
	struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	check(WeakAssetTreeNode.Pin()->SourceAssetType == ESearchIndexAssetType::BlendSpace);

	UPoseSearchDatabase* Database = WeakAssetTreeNode.Pin()->EditorViewModel.Pin()->GetPoseSearchDatabase();
	if (IsValid(Database))
	{
		Database->BlendSpaces[WeakAssetTreeNode.Pin()->SourceAssetIdx] = BlendSpace;
		AssetTreeWidget->FinalizeTreeChanges(true);
	}
}

#endif // WITH_EDITOR

void UPoseSearchDatabaseStatistics::Initialize(const UPoseSearchDatabase* PoseSearchDatabase)
{
	static FText TimeFormat = LOCTEXT("TimeFormat", "{0} {0}|plural(one=Second,other=Seconds)");
	
	if (PoseSearchDatabase)
	{
		if (const FPoseSearchIndex* SearchIndexSafe = PoseSearchDatabase->GetSearchIndexSafe())
		{
			// General information
	
			AnimationSequences = PoseSearchDatabase->Sequences.Num();
			
			TotalAnimationPosesInFrames = SearchIndexSafe->NumPoses;
			TotalAnimationPosesInTime = FText::Format(TimeFormat, static_cast<double>(SearchIndexSafe->NumPoses) / PoseSearchDatabase->Schema->SampleRate);
			
			{
				uint32 NumOfSearchablePoses = 0;
				for (const FPoseSearchPoseMetadata & PoseMetadata : SearchIndexSafe->PoseMetadata)
				{
					NumOfSearchablePoses += PoseMetadata.Flags != EPoseSearchPoseFlags::BlockTransition;
				}
				
				SearchableFrames = NumOfSearchablePoses;
				SearchableTime = FText::Format(TimeFormat, static_cast<double>(NumOfSearchablePoses) / PoseSearchDatabase->Schema->SampleRate);
			}
			
			// Velocity information
	
			// TODO: Set values once they can be queried from the PoseSearchIndex.
			
			// Principal Component Analysis
			
			ExplainedVariance = SearchIndexSafe->PCAExplainedVariance;
			
			// Memory information
			
			{
				const uint32 ValuesBytesSize = SearchIndexSafe->Values.GetAllocatedSize();
				const uint32 PCAValuesBytesSize = SearchIndexSafe->PCAValues.GetAllocatedSize();
				const uint32 KDTreeBytesSize = SearchIndexSafe->KDTree.GetAllocatedSize();
				const uint32 PoseMetadataBytesSize = SearchIndexSafe->PoseMetadata.GetAllocatedSize();
				const uint32 AssetsBytesSize = SearchIndexSafe->Assets.GetAllocatedSize();
				const uint32 OtherBytesSize = SearchIndexSafe->PCAProjectionMatrix.GetAllocatedSize() + SearchIndexSafe->Mean.GetAllocatedSize() + SearchIndexSafe->WeightsSqrt.GetAllocatedSize();
				const uint32 EstimatedDatabaseBytesSize = ValuesBytesSize + PCAValuesBytesSize + KDTreeBytesSize + PoseMetadataBytesSize + AssetsBytesSize + OtherBytesSize;
				
				ValuesSize = FText::AsMemory(ValuesBytesSize);
				PCAValuesSize = FText::AsMemory(PCAValuesBytesSize);
				KDTreeSize = FText::AsMemory(KDTreeBytesSize);
				PoseMetadataSize = FText::AsMemory(PoseMetadataBytesSize);
				AssetsSize = FText::AsMemory(AssetsBytesSize);
				EstimatedDatabaseSize = FText::AsMemory(EstimatedDatabaseBytesSize);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
