// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_PoseAsset.h"
#include "ToolMenuSection.h"
#include "Animation/PoseAsset.h"
#include "Animation/AnimSequence.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

void FAssetTypeActions_PoseAsset::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	auto PoseAssets = GetTypedWeakObjectPtrs<UPoseAsset>(InObjects);

	Section.AddMenuEntry(
		"PoseAsset_UpdateSource",
		LOCTEXT("PoseAsset_UpdateSource", "Update Source Animation"),
		LOCTEXT("PoseAsset_UpdateSourceTooltip", "Updates the source animation for this pose"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_PoseAsset::ExecuteUpdateSource, PoseAssets),
			FCanExecuteAction()
		)
	);
}
void FAssetTypeActions_PoseAsset::ExecuteUpdateSource(TArray<TWeakObjectPtr<UPoseAsset>> Objects)
{
	FScopedTransaction Transaction(LOCTEXT("PoseUpdateSource", "Updating Source Animation for Pose"));
	for (const TWeakObjectPtr<UPoseAsset>& WeakPose : Objects)
	{
		if (UPoseAsset* PoseAsset = WeakPose.Get())
		{
			if (UAnimSequence* SourceAnimation = PoseAsset->SourceAnimation)
			{
				if (!PoseAsset->SourceAnimationRawDataGUID.IsValid() || PoseAsset->SourceAnimationRawDataGUID != SourceAnimation->GetRawDataGuid())
				{
					if (PoseAsset->GetSkeleton()->IsCompatibleForEditor(SourceAnimation->GetSkeleton()))
					{
						PoseAsset->Modify();
						PoseAsset->UpdatePoseFromAnimation(SourceAnimation);
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
