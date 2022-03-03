// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditorController.h"

#include "EditorModeManager.h"
#include "Widgets/Input/SButton.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetEditMode.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/SIKRetargetChainMapList.h"
#include "Retargeter/IKRetargeter.h"
#include "RigEditor/IKRigController.h"

#define LOCTEXT_NAMESPACE "IKRetargetEditorController"

void FIKRetargetEditorController::Initialize(TSharedPtr<FIKRetargetEditor> InEditor, UIKRetargeter* InAsset)
{
	Editor = InEditor;
	AssetController = UIKRetargeterController::GetController(InAsset);

	// bind callbacks when SOURCE or TARGET IK Rigs are modified
	BindToIKRigAsset(AssetController->GetAsset()->GetSourceIKRigWriteable());
	BindToIKRigAsset(AssetController->GetAsset()->GetTargetIKRigWriteable());

	// bind callback when retargeter needs reinitialized
	AssetController->OnRetargeterNeedsInitialized().AddSP(this, &FIKRetargetEditorController::OnRetargeterNeedsInitialized);
}

void FIKRetargetEditorController::BindToIKRigAsset(UIKRigDefinition* InIKRig) const
{
	if (!InIKRig)
	{
		return;
	}

	UIKRigController* Controller = UIKRigController::GetIKRigController(InIKRig);
	if (!Controller->OnIKRigNeedsInitialized().IsBoundToObject(this))
	{
		Controller->OnIKRigNeedsInitialized().AddSP(this, &FIKRetargetEditorController::OnIKRigNeedsInitialized);
		Controller->OnRetargetChainRenamed().AddSP(this, &FIKRetargetEditorController::OnRetargetChainRenamed);
		Controller->OnRetargetChainRemoved().AddSP(this, &FIKRetargetEditorController::OnRetargetChainRemoved);
	}
}

void FIKRetargetEditorController::OnIKRigNeedsInitialized(UIKRigDefinition* ModifiedIKRig) const
{
	const UIKRetargeter* Retargeter = AssetController->GetAsset();
	
	check(ModifiedIKRig && Retargeter)
	 
	const bool bIsSource = ModifiedIKRig == Retargeter->GetSourceIKRig();
	const bool bIsTarget = ModifiedIKRig == Retargeter->GetTargetIKRig();
	if (!(bIsSource || bIsTarget))
	{
		return;
	}

	// the target anim instance has the IK RetargetPoseFromMesh node which needs reinitialized with new asset version
	TargetAnimInstance->SetProcessorNeedsInitialized();
	RefreshAllViews();
}

void FIKRetargetEditorController::OnRetargetChainRenamed(UIKRigDefinition* ModifiedIKRig, FName OldName, FName NewName) const
{
	check(ModifiedIKRig)
	
	AssetController->OnRetargetChainRenamed(ModifiedIKRig, OldName, NewName);
}

void FIKRetargetEditorController::OnRetargetChainRemoved(UIKRigDefinition* ModifiedIKRig, const FName& InChainRemoved) const
{
	check(ModifiedIKRig)
	AssetController->OnRetargetChainRemoved(ModifiedIKRig, InChainRemoved);
	RefreshAllViews();
}

void FIKRetargetEditorController::OnRetargeterNeedsInitialized(const UIKRetargeter* Retargeter) const
{
	// force edit pose mode to be off
	AssetController->SetEditRetargetPoseMode(false, false); // turn off and don't reinitialize (avoid infinite loop)
	Editor.Pin()->GetEditorModeManager().DeactivateMode(FIKRetargetEditMode::ModeName);
	// force reinit the runtime retarget processor
	TargetAnimInstance->SetProcessorNeedsInitialized();
	// refresh all the UI views
	RefreshAllViews();
}

USkeletalMesh* FIKRetargetEditorController::GetSourceSkeletalMesh() const
{
	if (!(AssetController && AssetController->GetAsset()->GetSourceIKRig()))
	{
		return nullptr;
	}

	return AssetController->GetAsset()->GetSourceIKRig()->PreviewSkeletalMesh.Get();
}

USkeletalMesh* FIKRetargetEditorController::GetTargetSkeletalMesh() const
{
	if (!(AssetController && AssetController->GetAsset()->GetTargetIKRig()))
	{
		return nullptr;
	}

	return AssetController->GetTargetPreviewMesh();
}

FTransform FIKRetargetEditorController::GetTargetBoneGlobalTransform(
	const UIKRetargetProcessor* RetargetProcessor,
	const int32& TargetBoneIndex) const
{
	check(RetargetProcessor && RetargetProcessor->IsInitialized())

	// get transform of bone
	FTransform BoneTransform = RetargetProcessor->GetTargetBoneRetargetPoseGlobalTransform(TargetBoneIndex);

	// scale and offset
	BoneTransform.ScaleTranslation(AssetController->GetAsset()->TargetActorScale);
	BoneTransform.AddToTranslation(FVector(AssetController->GetAsset()->TargetActorOffset, 0.f, 0.f));

	return BoneTransform;
}

FTransform FIKRetargetEditorController::GetTargetBoneLocalTransform(
	const UIKRetargetProcessor* RetargetProcessor,
	const int32& TargetBoneIndex) const
{
	check(RetargetProcessor && RetargetProcessor->IsInitialized())

	return RetargetProcessor->GetTargetBoneRetargetPoseLocalTransform(TargetBoneIndex);
}

bool FIKRetargetEditorController::GetTargetBoneLineSegments(
	const UIKRetargetProcessor* RetargetProcessor,
	const int32& TargetBoneIndex,
	FVector& OutStart,
	TArray<FVector>& OutChildren) const
{
	// get the runtime processor
	check(RetargetProcessor && RetargetProcessor->IsInitialized())
	
	// get the target skeleton we want to draw
	const FTargetSkeleton& TargetSkeleton = RetargetProcessor->GetTargetSkeleton();
	check(TargetSkeleton.BoneNames.IsValidIndex(TargetBoneIndex))

	// get the origin of the bone chain
	OutStart = TargetSkeleton.RetargetGlobalPose[TargetBoneIndex].GetTranslation();

	// get children
	TArray<int32> ChildIndices;
	TargetSkeleton.GetChildrenIndices(TargetBoneIndex, ChildIndices);
	for (const int32& ChildIndex : ChildIndices)
	{
		OutChildren.Emplace(TargetSkeleton.RetargetGlobalPose[ChildIndex].GetTranslation());
	}

	// add the target translation offset and scale
	const FVector TargetOffset(AssetController->GetAsset()->TargetActorOffset, 0.f, 0.f);
	OutStart *= AssetController->GetAsset()->TargetActorScale;
	OutStart += TargetOffset;
	for (FVector& ChildPoint : OutChildren)
	{
		ChildPoint *= AssetController->GetAsset()->TargetActorScale;
		ChildPoint += TargetOffset;
	}
	
	return true;
}

const UIKRetargetProcessor* FIKRetargetEditorController::GetRetargetProcessor() const
{	
	if(UIKRetargetAnimInstance* AnimInstance = TargetAnimInstance.Get())
	{
		return AnimInstance->GetRetargetProcessor();
	}

	return nullptr;	
}

void FIKRetargetEditorController::RefreshAllViews() const
{
	Editor.Pin()->RegenerateMenusAndToolbars();

	// cannot assume chains view is always available
	if (ChainsView.IsValid())
	{
		ChainsView.Get()->RefreshView();
	}
}

void FIKRetargetEditorController::PlayAnimationAsset(UAnimationAsset* AssetToPlay)
{
	if (AssetToPlay && SourceAnimInstance.IsValid())
	{
		SourceAnimInstance->SetAnimationAsset(AssetToPlay);
		PreviousAsset = AssetToPlay;
	}
}

void FIKRetargetEditorController::PlayPreviousAnimationAsset() const
{
	if (PreviousAsset)
	{
		SourceAnimInstance->SetAnimationAsset(PreviousAsset);
	}
}

void FIKRetargetEditorController::HandleEditPose() const
{
	const bool bEditPoseMode = !AssetController->GetEditRetargetPoseMode();
	AssetController->SetEditRetargetPoseMode(bEditPoseMode);
	if (bEditPoseMode)
	{
		Editor.Pin()->GetEditorModeManager().ActivateMode(FIKRetargetEditMode::ModeName);
		SourceSkelMeshComponent->ShowReferencePose(true);
	}
	else
	{
		Editor.Pin()->GetEditorModeManager().DeactivateMode(FIKRetargetEditMode::ModeName);
		PlayPreviousAnimationAsset();
	}
}

bool FIKRetargetEditorController::CanEditPose() const
{
	const UIKRetargetProcessor* Processor = GetRetargetProcessor();
	if (!Processor)
	{
		return false;
	}

	return Processor->IsInitialized();
}

bool FIKRetargetEditorController::IsEditingPose() const
{
	return AssetController->GetEditRetargetPoseMode();
}

void FIKRetargetEditorController::HandleNewPose()
{
	// get a unique pose name to use as suggestion
	const FName DefaultNewPoseName = FName(LOCTEXT("NewRetargetPoseName", "CustomRetargetPose").ToString());
	const FName UniqueNewPoseName = AssetController->MakePoseNameUnique(DefaultNewPoseName);
	
	SAssignNew(NewPoseWindow, SWindow)
	.Title(LOCTEXT("NewRetargetPoseOptions", "Create New Retarget Pose"))
	.ClientSize(FVector2D(250, 80))
	.HasCloseButton(true)
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FEditorStyle::GetBrush("Menu.Background") )
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SAssignNew(NewPoseEditableText, SEditableTextBox)
				.Text(FText::FromName(UniqueNewPoseName))
			]

			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("OkButtonLabel", "Ok"))
					.OnClicked(this, &FIKRetargetEditorController::CreateNewPose)
				]
				
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
					.OnClicked_Lambda( [=]()
					{
						NewPoseWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]	
		]
	];

	GEditor->EditorAddModalWindow(NewPoseWindow.ToSharedRef());
	NewPoseWindow.Reset();
}

FReply FIKRetargetEditorController::CreateNewPose() const
{
	const FName NewPoseName = FName(NewPoseEditableText.Get()->GetText().ToString());
	AssetController->AddRetargetPose(NewPoseName);
	NewPoseWindow->RequestDestroyWindow();
	Editor.Pin()->RegenerateMenusAndToolbars();
	return FReply::Handled();
}

void FIKRetargetEditorController::HandleDeletePose() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName();
	AssetController->RemoveRetargetPose(CurrentPose);
	Editor.Pin()->RegenerateMenusAndToolbars();
}

bool FIKRetargetEditorController::CanDeletePose() const
{	
	// cannot delete default pose
	return AssetController->GetCurrentRetargetPoseName() != UIKRetargeter::GetDefaultPoseName();
}

void FIKRetargetEditorController::HandleResetPose() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName();
	AssetController->ResetRetargetPose(CurrentPose);
}

FText FIKRetargetEditorController::GetCurrentPoseName() const
{
	return FText::FromName(AssetController->GetCurrentRetargetPoseName());
}

void FIKRetargetEditorController::OnPoseSelected(TSharedPtr<FName> InPosePose, ESelectInfo::Type SelectInfo) const
{
	AssetController->SetCurrentRetargetPose(*InPosePose.Get());
}

#undef LOCTEXT_NAMESPACE
