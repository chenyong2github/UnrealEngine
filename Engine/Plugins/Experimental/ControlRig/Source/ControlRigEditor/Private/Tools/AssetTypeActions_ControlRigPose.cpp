// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_ControlRigPose.h"
#include "EditorFramework/AssetImportData.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "ToolMenus.h"

#include "ControlRig.h"
#include "UnrealEdGlobals.h"
#include "ControlRigEditMode.h"
#include "Tools/ControlRigPose.h"
#include "EditorModeManager.h"
#include "SControlRigRenamePoseControls.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_ControlRigPose"

FText FAssetTypeActions_ControlRigPose::GetName() const
{
	return LOCTEXT("AssetTypeActions_ControlRigPose", "Control Rig Pose");
}

FColor FAssetTypeActions_ControlRigPose::GetTypeColor() const
{
	return FColor(222, 128, 64);
}

UClass* FAssetTypeActions_ControlRigPose::GetSupportedClass() const
{
	return UControlRigPoseAsset::StaticClass();
}

bool FAssetTypeActions_ControlRigPose::HasActions(const TArray<UObject*>& InObjects) const
{
	return true;
}

void FAssetTypeActions_ControlRigPose::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor /*= TSharedPtr<IToolkitHost>()*/)
{
	FAssetTypeActions_Base::OpenAssetEditor(InObjects, EditWithinLevelEditor);
}

uint32 FAssetTypeActions_ControlRigPose::GetCategories()
{
	return EAssetTypeCategories::Animation;
}

class UThumbnailInfo* FAssetTypeActions_ControlRigPose::GetThumbnailInfo(UObject* Asset) const
{
	return nullptr; //mz todo it looks like this is not needed if we use the default. Will remove based on feedback.
	/*
	UControlRigPoseAsset* PoseAsset = CastChecked<UControlRigPoseAsset>(Asset);
	UThumbnailInfo* ThumbnailInfo = PoseAsset->ThumbnailInfo;
	if (ThumbnailInfo == NULL)
	{
		ThumbnailInfo = NewObject<USceneThumbnailInfo>(PoseAsset, NAME_None, RF_Transactional);
		PoseAsset->ThumbnailInfo = ThumbnailInfo;
	}
	
	return ThumbnailInfo;
	*/
}

bool FAssetTypeActions_ControlRigPose::IsImportedAsset() const
{
	return false;
}

void FAssetTypeActions_ControlRigPose::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	if (InObjects.Num() == 1)
	{
		UObject* SelectedAsset = InObjects[0];
		if (SelectedAsset == nullptr)
		{
			return;
		}

		UControlRigPoseAsset* PoseAsset = Cast<UControlRigPoseAsset>(SelectedAsset);
		if (PoseAsset)
		{
			FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
			if (ControlRigEditMode && ControlRigEditMode->GetControlRig(true))
			{ 
				UControlRig* ControlRig = ControlRigEditMode->GetControlRig(true);

				Section.AddDynamicEntry("Control Rig Pose Actions", FNewToolMenuSectionDelegate::CreateLambda([ControlRig,PoseAsset](FToolMenuSection& InSection)
					{
						{
							const FText Label = LOCTEXT("PastePoseButton", "Paste Pose");
							const FText ToolTipText = LOCTEXT("PastePoseButtonTooltip", "Paste the selected pose");
							InSection.AddMenuEntry(
								"PastePose",
								Label,
								ToolTipText,
								FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([ControlRig,PoseAsset]()
										{
											PoseAsset->PastePose(ControlRig, false, false);
										})));
						}
						{
							const FText Label = LOCTEXT("SelectControls", "Select Controls");
							const FText ToolTipText = LOCTEXT("SelectControlsTooltip", "Select controls in this pose on active control rig");
							InSection.AddMenuEntry(
								"SelectControls",
								Label,
								ToolTipText,
								FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([ControlRig, PoseAsset]()
										{
											PoseAsset->SelectControls(ControlRig);
										})));
						}
						{
							const FText Label = LOCTEXT("UpdatePose", "Update Pose");
							const FText ToolTipText = LOCTEXT("UpdatePoseTooltip", "Update the pose based upon current control rig pose and selected controls");
							InSection.AddMenuEntry(
								"UpdatePose",
								Label,
								ToolTipText,
								FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([ControlRig,PoseAsset]()
										{
											PoseAsset->SavePose(ControlRig,false);
										})));
						}
						{

							const FText Label = LOCTEXT("RenameControls", "Rename Controls");
							const FText ToolTipText = LOCTEXT("RenameControlsTooltip", "Rename controls on selected poses");
							InSection.AddMenuEntry(
								"RenameControls",
								Label,
								ToolTipText,
								FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([PoseAsset]()
										{
											TArray<UControlRigPoseAsset*> PoseAssets;
											PoseAssets.Add(PoseAsset);
											FControlRigRenameControlsDialog::RenameControls(PoseAssets);
										})));
						}

					}));
			}
			else
			{
				Section.AddDynamicEntry("Control Rig Pose Actions", FNewToolMenuSectionDelegate::CreateLambda([PoseAsset](FToolMenuSection& InSection)
				{
					{

						const FText Label = LOCTEXT("RenameControls", "Rename Controls");
						const FText ToolTipText = LOCTEXT("RenameControlsTooltip", "Rename controls on selected poses");
						InSection.AddMenuEntry(
							"RenameControls",
							Label,
							ToolTipText,
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([PoseAsset]()
									{
										TArray<UControlRigPoseAsset*> PoseAssets;
										PoseAssets.Add(PoseAsset);
										FControlRigRenameControlsDialog::RenameControls(PoseAssets);
									})));
					}
				}));
			}
		}
	}
	else if (InObjects.Num() > 1)
	{
		TArray<UControlRigPoseAsset*> PoseAssets;
		for (UObject* Object : InObjects)
		{
			if (Object && Object->IsA<UControlRigPoseAsset>())
			{
				UControlRigPoseAsset* PoseAsset = Cast<UControlRigPoseAsset>(Object);
				PoseAssets.Add(PoseAsset);
			}
		}
		Section.AddDynamicEntry("Control Rig Pose Actions", FNewToolMenuSectionDelegate::CreateLambda([PoseAssets](FToolMenuSection& InSection)
			{
				{

					const FText Label = LOCTEXT("RenameControls", "Rename Controls");
					const FText ToolTipText = LOCTEXT("RenameControlsTooltip", "Rename controls on selected poses");
					InSection.AddMenuEntry(
						"RenameControls",
						Label,
						ToolTipText,
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([PoseAssets]()
								{
									FControlRigRenameControlsDialog::RenameControls(PoseAssets);
								})));
				}
			}));

	}
}

#undef LOCTEXT_NAMESPACE
