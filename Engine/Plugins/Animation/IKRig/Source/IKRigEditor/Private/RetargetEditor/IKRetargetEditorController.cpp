// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditorController.h"

#include "EditorModeManager.h"
#include "Widgets/Input/SButton.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetDefaultMode.h"
#include "RetargetEditor/IKRetargetEditPoseMode.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/SIKRetargetChainMapList.h"
#include "Retargeter/IKRetargeter.h"
#include "RigEditor/SIKRigOutputLog.h"
#include "RigEditor/IKRigController.h"

#define LOCTEXT_NAMESPACE "IKRetargetEditorController"

void FIKRetargetEditorController::Initialize(TSharedPtr<FIKRetargetEditor> InEditor, UIKRetargeter* InAsset)
{
	Editor = InEditor;
	AssetController = UIKRetargeterController::GetController(InAsset);
	AssetController->SetEditorController(this);

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
	ClearOutputLog();
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
	// clear the output log
	ClearOutputLog();
	// force edit pose mode to be off
	Editor.Pin()->GetEditorModeManager().DeactivateMode(FIKRetargetEditPoseMode::ModeName);
	// force reinit the runtime retarget processor
	TargetAnimInstance->SetProcessorNeedsInitialized();
	// refresh all the UI views
	RefreshAllViews();
}

void FIKRetargetEditorController::AddOffsetAndUpdatePreviewMeshPosition(
	const FVector& Offset,
	USceneComponent* Component) const
{
	UIKRetargeter* Asset = AssetController->GetAsset();
	FVector Position;
	float Scale;
	if (Component == TargetSkelMeshComponent)
	{
		Asset->TargetMeshOffset += Offset;
		Position = Asset->TargetMeshOffset;
		Scale = Asset->TargetMeshScale;
	}
	else
	{
		Asset->SourceMeshOffset += Offset;
		Position = Asset->SourceMeshOffset;
		Scale = 1.0f;
	}

	constexpr bool bSweep = false;
	constexpr FHitResult* OutSweepHitResult = nullptr;
	constexpr ETeleportType Teleport = ETeleportType::ResetPhysics;
	Component->SetWorldLocation(Position, bSweep, OutSweepHitResult, Teleport);
	Component->SetWorldScale3D(FVector(Scale,Scale,Scale));
}

USkeletalMesh* FIKRetargetEditorController::GetSourceSkeletalMesh() const
{
	return AssetController ? AssetController->GetSourcePreviewMesh() : nullptr;
}

USkeletalMesh* FIKRetargetEditorController::GetTargetSkeletalMesh() const
{
	return AssetController ? AssetController->GetTargetPreviewMesh() : nullptr;
}

FTransform FIKRetargetEditorController::GetTargetBoneGlobalTransform(
	const UIKRetargetProcessor* RetargetProcessor,
	const int32& TargetBoneIndex) const
{
	check(RetargetProcessor && RetargetProcessor->IsInitialized())

	// get transform of bone
	FTransform BoneTransform = RetargetProcessor->GetTargetBoneRetargetPoseGlobalTransform(TargetBoneIndex);

	// scale and offset
	BoneTransform.ScaleTranslation(AssetController->GetAsset()->TargetMeshScale);
	BoneTransform.AddToTranslation(AssetController->GetAsset()->TargetMeshOffset);

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
	const UIKRetargeter* Asset = AssetController->GetAsset();
	OutStart *= Asset->TargetMeshScale;
	OutStart += Asset->TargetMeshOffset;
	for (FVector& ChildPoint : OutChildren)
	{
		ChildPoint *= Asset->TargetMeshScale;
		ChildPoint += Asset->TargetMeshOffset;
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

void FIKRetargetEditorController::ClearOutputLog() const
{
	if (OutputLogView.IsValid())
	{
		OutputLogView.Get()->ClearLog();
	}
}

void FIKRetargetEditorController::RefreshAllViews() const
{
	Editor.Pin()->RegenerateMenusAndToolbars();
	DetailsView->ForceRefresh();

	// cannot assume chains view is always available
	if (ChainsView.IsValid())
	{
		ChainsView.Get()->RefreshView();
	}

	// refresh the asset browser to ensure it shows compatible sequences
	if (AssetBrowserView.IsValid())
	{
		AssetBrowserView.Get()->RefreshView();
	}
}

void FIKRetargetEditorController::PlayAnimationAsset(UAnimationAsset* AssetToPlay)
{
	if (AssetToPlay && SourceAnimInstance.IsValid())
	{
		SourceAnimInstance->SetAnimationAsset(AssetToPlay);
		PreviousAsset = AssetToPlay;
		// tell asset to output the retargeted pose
		AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::RunRetarget);
	}
}

void FIKRetargetEditorController::PlayPreviousAnimationAsset() const
{
	if (PreviousAsset)
	{
		SourceAnimInstance->SetAnimationAsset(PreviousAsset);
		// tell asset to output the retarget
		AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::RunRetarget);
	}
}

void FIKRetargetEditorController::HandleGoToRetargetPose() const
{
	Editor.Pin()->GetEditorModeManager().DeactivateMode(FIKRetargetEditPoseMode::ModeName);
	Editor.Pin()->GetEditorModeManager().ActivateMode(FIKRetargetDefaultMode::ModeName);

	// put source back in ref pose
	SourceSkelMeshComponent->ShowReferencePose(true);
	// have to move component back to offset position because ShowReferencePose() sets it back to origin
	AddOffsetAndUpdatePreviewMeshPosition(FVector::ZeroVector, SourceSkelMeshComponent);
	// tell asset to output the retarget pose
	AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::ShowRetargetPose);
}

void FIKRetargetEditorController::HandleEditPose() const
{
	if (IsEditingPose())
	{
		// stop pose editing
		Editor.Pin()->GetEditorModeManager().DeactivateMode(FIKRetargetEditPoseMode::ModeName);
		Editor.Pin()->GetEditorModeManager().ActivateMode(FIKRetargetDefaultMode::ModeName);
		
		// must reinitialize after editing the retarget pose
		AssetController->BroadcastNeedsReinitialized();
		// continue playing whatever animation asset was last used
		PlayPreviousAnimationAsset();
	}
	else
	{
		// start pose editing
		Editor.Pin()->GetEditorModeManager().DeactivateMode(FIKRetargetDefaultMode::ModeName);
		Editor.Pin()->GetEditorModeManager().ActivateMode(FIKRetargetEditPoseMode::ModeName);
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
	return AssetController->GetAsset()->GetOutputMode() == ERetargeterOutputMode::EditRetargetPose;
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
		.BorderImage( FAppStyle::GetBrush("Menu.Background") )
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

bool FIKRetargetEditorController::CanNewPose() const
{
	return !IsEditingPose();
}

FReply FIKRetargetEditorController::CreateNewPose() const
{
	const FName NewPoseName = FName(NewPoseEditableText.Get()->GetText().ToString());
	AssetController->AddRetargetPose(NewPoseName);
	NewPoseWindow->RequestDestroyWindow();
	DetailsView->ForceRefresh();
	return FReply::Handled();
}

void FIKRetargetEditorController::HandleDeletePose() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName();
	AssetController->RemoveRetargetPose(CurrentPose);
	DetailsView->ForceRefresh();
}

bool FIKRetargetEditorController::CanDeletePose() const
{	
	// cannot delete default pose
	const bool bNotUsingDefaultPose = AssetController->GetCurrentRetargetPoseName() != UIKRetargeter::GetDefaultPoseName();
	// cannot delete pose while editing
	return bNotUsingDefaultPose && !IsEditingPose();
}

void FIKRetargetEditorController::HandleResetPose() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName();
	AssetController->ResetRetargetPose(CurrentPose);
}

bool FIKRetargetEditorController::CanResetPose() const
{
	// only allow reseting pose while editing to avoid confusion
	return IsEditingPose();
}

void FIKRetargetEditorController::HandleRenamePose()
{
	SAssignNew(RenamePoseWindow, SWindow)
	.Title(LOCTEXT("RenameRetargetPoseOptions", "Rename Retarget Pose"))
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
				SAssignNew(NewNameEditableText, SEditableTextBox)
				.Text(GetCurrentPoseName())
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
					.IsEnabled_Lambda([this]()
					{
						return !GetCurrentPoseName().EqualTo(NewNameEditableText.Get()->GetText());
					})
					.OnClicked(this, &FIKRetargetEditorController::RenamePose)
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
						RenamePoseWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]	
		]
	];

	GEditor->EditorAddModalWindow(RenamePoseWindow.ToSharedRef());
	RenamePoseWindow.Reset();
}

FReply FIKRetargetEditorController::RenamePose() const
{
	const FName NewPoseName = FName(NewNameEditableText.Get()->GetText().ToString());
	RenamePoseWindow->RequestDestroyWindow();
	
	AssetController->RenameCurrentRetargetPose(NewPoseName);
	DetailsView->ForceRefresh();
	return FReply::Handled();
}

bool FIKRetargetEditorController::CanRenamePose() const
{
	// cannot rename default pose
	const bool bNotUsingDefaultPose = AssetController->GetCurrentRetargetPoseName() != UIKRetargeter::GetDefaultPoseName();
	// cannot rename pose while editing
	return bNotUsingDefaultPose && !IsEditingPose();
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
