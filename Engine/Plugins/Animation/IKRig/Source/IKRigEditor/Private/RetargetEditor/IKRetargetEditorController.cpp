// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditorController.h"

#include "AnimPose.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "EditorModeManager.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/Input/SButton.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"
#include "Factories/PoseAssetFactory.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetDefaultMode.h"
#include "RetargetEditor/IKRetargetEditPoseMode.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/SIKRetargetChainMapList.h"
#include "RetargetEditor/SIKRetargetHierarchy.h"
#include "Retargeter/IKRetargeter.h"
#include "RigEditor/SIKRigOutputLog.h"
#include "RigEditor/IKRigController.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "IKRetargetEditorController"


void FIKRetargetEditorController::Initialize(TSharedPtr<FIKRetargetEditor> InEditor, UIKRetargeter* InAsset)
{
	Editor = InEditor;
	AssetController = UIKRetargeterController::GetController(InAsset);
	CurrentlyEditingSourceOrTarget = ERetargetSourceOrTarget::Target;
	OutputMode = ERetargeterOutputMode::ShowRetargetPose;
	PreviousMode = OutputMode;
	PoseExporter = MakeShared<FIKRetargetPoseExporter>();
	PoseExporter->Initialize(SharedThis(this));

	// clean the asset before editing
	const bool bForceReinitialization = false;
	AssetController->CleanChainMapping(bForceReinitialization);
	AssetController->CleanPoseLists(bForceReinitialization);

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
	UIKRetargeter* Retargeter = AssetController->GetAsset();
	
	check(ModifiedIKRig && Retargeter)
	 
	const bool bIsSource = ModifiedIKRig == Retargeter->GetSourceIKRig();
	const bool bIsTarget = ModifiedIKRig == Retargeter->GetTargetIKRig();
	if (!(bIsSource || bIsTarget))
	{
		return;
	}

	// the target anim instance has the RetargetPoseFromMesh node which needs reinitialized with new asset version
	OnRetargeterNeedsInitialized(Retargeter);
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

void FIKRetargetEditorController::OnRetargeterNeedsInitialized(UIKRetargeter* Retargeter) const
{
	// clear the output log
	ClearOutputLog();
	
	// force reinit the retarget processor (also inits the target IK Rig processor)
	if (UIKRetargetProcessor* Processor = GetRetargetProcessor())
	{
		constexpr bool bSuppressWarnings = false;
		Processor->Initialize(
			GetSkeletalMesh(ERetargetSourceOrTarget::Source),
			GetSkeletalMesh(ERetargetSourceOrTarget::Target),
			Retargeter,
			bSuppressWarnings);
	}
	
	// refresh all the UI views
	RefreshAllViews();
}

UDebugSkelMeshComponent* FIKRetargetEditorController::GetSkeletalMeshComponent(
	const ERetargetSourceOrTarget& SourceOrTarget) const
{
	return SourceOrTarget == ERetargetSourceOrTarget::Source ? SourceSkelMeshComponent : TargetSkelMeshComponent;
}

UIKRetargetAnimInstance* FIKRetargetEditorController::GetAnimInstance(
	const ERetargetSourceOrTarget& SourceOrTarget) const
{
	return SourceOrTarget == ERetargetSourceOrTarget::Source ? SourceAnimInstance.Get() : TargetAnimInstance.Get();
}

void FIKRetargetEditorController::AddOffsetToMeshComponent(const FVector& Offset, USceneComponent* MeshComponent) const
{
	UIKRetargeter* Asset = AssetController->GetAsset();
	FVector Position;
	float Scale;
	if (MeshComponent == TargetSkelMeshComponent)
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
	MeshComponent->SetWorldLocation(Position, bSweep, OutSweepHitResult, Teleport);
	MeshComponent->SetWorldScale3D(FVector(Scale,Scale,Scale));
}

bool FIKRetargetEditorController::IsBoneRetargeted(const FName& BoneName, ERetargetSourceOrTarget SourceOrTarget) const
{
	// get an initialized processor
	const UIKRetargetProcessor* Processor = GetRetargetProcessor();
	if (!(Processor && Processor->IsInitialized()))
	{
		return false;
	}

	// get the bone index
	const FRetargetSkeleton& Skeleton = SourceOrTarget == ERetargetSourceOrTarget::Source ? Processor->GetSourceSkeleton() : Processor->GetTargetSkeleton();
	const int32 BoneIndex = Skeleton.FindBoneIndexByName(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	// return if it's a retargeted bone
	return Processor->IsBoneRetargeted(BoneIndex, (int8)SourceOrTarget);
}

FName FIKRetargetEditorController::GetChainNameFromBone(const FName& BoneName, ERetargetSourceOrTarget SourceOrTarget) const
{
	// get an initialized processor
	const UIKRetargetProcessor* Processor = GetRetargetProcessor();
	if (!(Processor && Processor->IsInitialized()))
	{
		return NAME_None;
	}

	// get the bone index
	const FRetargetSkeleton& Skeleton = SourceOrTarget == ERetargetSourceOrTarget::Source ? Processor->GetSourceSkeleton() : Processor->GetTargetSkeleton();
	const int32 BoneIndex = Skeleton.FindBoneIndexByName(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return NAME_None;
	}

	return Processor->GetChainNameForBone(BoneIndex, (int8)SourceOrTarget);
}

TObjectPtr<UIKRetargetBoneDetails> FIKRetargetEditorController::GetDetailsObjectForBone(const FName& BoneName)
{
	if (AllBoneDetails.Contains(BoneName))
	{
		return AllBoneDetails[BoneName];
	}

	return CreateBoneDetails(BoneName);
}

TObjectPtr<UIKRetargetBoneDetails> FIKRetargetEditorController::CreateBoneDetails(const FName& BoneName)
{
	// create and store a new one
	UIKRetargetBoneDetails* NewBoneDetails = NewObject<UIKRetargetBoneDetails>(AssetController->GetAsset(), FName(BoneName), RF_Standalone | RF_Transient );
	NewBoneDetails->SelectedBone = BoneName;
	NewBoneDetails->EditorController = SharedThis(this);

	// store it in the map
	AllBoneDetails.Add(BoneName, NewBoneDetails);
	
	return NewBoneDetails;
}

USkeletalMesh* FIKRetargetEditorController::GetSkeletalMesh(const ERetargetSourceOrTarget SourceOrTarget) const
{
	return AssetController ? AssetController->GetPreviewMesh(SourceOrTarget) : nullptr;
}

const USkeleton* FIKRetargetEditorController::GetSkeleton(const ERetargetSourceOrTarget SourceOrTarget) const
{
	if (const USkeletalMesh* Mesh = GetSkeletalMesh(SourceOrTarget))
	{
		return Mesh->GetSkeleton();
	}
	
	return nullptr;
}

UDebugSkelMeshComponent* FIKRetargetEditorController::GetEditedSkeletalMesh() const
{
	return CurrentlyEditingSourceOrTarget == ERetargetSourceOrTarget::Source ? SourceSkelMeshComponent : TargetSkelMeshComponent;
}

const FRetargetSkeleton& FIKRetargetEditorController::GetCurrentlyEditedSkeleton(const UIKRetargetProcessor& Processor) const
{
	return CurrentlyEditingSourceOrTarget == ERetargetSourceOrTarget::Source ? Processor.GetSourceSkeleton() : Processor.GetTargetSkeleton();
}

FTransform FIKRetargetEditorController::GetGlobalRetargetPoseOfBone(
	const ERetargetSourceOrTarget SourceOrTarget,
	const int32& BoneIndex,
	const float& Scale,
	const FVector& Offset) const
{
	if (BoneIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}
	
	const UIKRetargetAnimInstance* AnimInstance = GetAnimInstance(SourceOrTarget);
	const TArray<FTransform>& GlobalRetargetPose = AnimInstance->GetGlobalRetargetPose();
	
	// get transform of bone
	FTransform BoneTransform = GlobalRetargetPose[BoneIndex];

	// scale and offset
	BoneTransform.ScaleTranslation(Scale);
	BoneTransform.AddToTranslation(Offset);
	BoneTransform.NormalizeRotation();

	return BoneTransform;
}

FTransform FIKRetargetEditorController::GetTargetBoneLocalTransform(
	const UIKRetargetProcessor* RetargetProcessor,
	const int32& TargetBoneIndex) const
{
	check(RetargetProcessor && RetargetProcessor->IsInitialized())

	return RetargetProcessor->GetTargetBoneRetargetPoseLocalTransform(TargetBoneIndex);
}

void FIKRetargetEditorController::GetGlobalRetargetPoseOfImmediateChildren(
	const FRetargetSkeleton& RetargetSkeleton,
	const int32& BoneIndex,
	const float& Scale,
	const FVector& Offset,
	TArray<int32>& OutChildrenIndices,
	TArray<FVector>& OutChildrenPositions)
{
	OutChildrenIndices.Reset();
	OutChildrenPositions.Reset();
	
	check(RetargetSkeleton.BoneNames.IsValidIndex(BoneIndex))

	// get indices of immediate children
	RetargetSkeleton.GetChildrenIndices(BoneIndex, OutChildrenIndices);

	// get the positions of the immediate children
	for (const int32& ChildIndex : OutChildrenIndices)
	{
		OutChildrenPositions.Emplace(RetargetSkeleton.RetargetGlobalPose[ChildIndex].GetTranslation());
	}

	// apply scale and offset to positions
	for (FVector& ChildPosition : OutChildrenPositions)
	{
		ChildPosition *= Scale;
		ChildPosition += Offset;
	}
}

UIKRetargetProcessor* FIKRetargetEditorController::GetRetargetProcessor() const
{	
	if(UIKRetargetAnimInstance* AnimInstance = TargetAnimInstance.Get())
	{
		return AnimInstance->GetRetargetProcessor();
	}

	return nullptr;	
}

void FIKRetargetEditorController::ResetIKPlantingState() const
{
	if (UIKRetargetProcessor* Processor = GetRetargetProcessor())
	{
		Processor->ResetPlanting();
	}
}

void FIKRetargetEditorController::ClearOutputLog() const
{
	if (OutputLogView.IsValid())
	{
		OutputLogView.Get()->ClearLog();
		if (const UIKRetargetProcessor* Processor = GetRetargetProcessor())
		{
			Processor->Log.Clear();
		}
	}
}

void FIKRetargetEditorController::RefreshAllViews() const
{
	Editor.Pin()->RegenerateMenusAndToolbars();
	RefreshDetailsView();
	RefreshChainsView();
	RefreshAssetBrowserView();
	RefreshHierarchyView();
}

void FIKRetargetEditorController::RefreshDetailsView() const
{
	// refresh the details panel, cannot assume tab is not closed
	if (DetailsView.IsValid())
	{
		DetailsView->ForceRefresh();
	}
}

void FIKRetargetEditorController::RefreshChainsView() const
{
	// refesh chains view, cannot assume tab is not closed
	if (ChainsView.IsValid())
	{
		ChainsView.Get()->RefreshView();
	}
}

void FIKRetargetEditorController::RefreshAssetBrowserView() const
{
	// refresh the asset browser to ensure it shows compatible sequences
	if (AssetBrowserView.IsValid())
	{
		AssetBrowserView.Get()->RefreshView();
	}
}

void FIKRetargetEditorController::RefreshHierarchyView() const
{
	if (HierarchyView.IsValid())
	{
		HierarchyView.Get()->RefreshPoseList();
		HierarchyView.Get()->RefreshTreeView();
	}
}

void FIKRetargetEditorController::RefreshPoseList() const
{
	if (HierarchyView.IsValid())
	{
		HierarchyView.Get()->RefreshPoseList();
	}
}

void FIKRetargetEditorController::SetDetailsObject(UObject* DetailsObject) const
{
	if (DetailsView.IsValid())
	{
		DetailsView->SetObject(DetailsObject);
	}
}

void FIKRetargetEditorController::SetDetailsObjects(const TArray<UObject*>& DetailsObjects) const
{
	if (DetailsView.IsValid())
	{
		DetailsView->SetObjects(DetailsObjects);
	}
}

void FIKRetargetEditorController::PlayAnimationAsset(UAnimationAsset* AssetToPlay)
{
	if (AssetToPlay && SourceAnimInstance.IsValid())
	{
		SourceAnimInstance->SetAnimationAsset(AssetToPlay);
		SourceAnimInstance->SetPlaying(true);
		AnimThatWasPlaying = AssetToPlay;
		// ensure we are running the retargeter so you can see the animation
		SetRetargeterMode(ERetargeterOutputMode::RunRetarget);
	}
}

void FIKRetargetEditorController::PausePlayback()
{
	if (UAnimationAsset* CurrentAnim = SourceAnimInstance->GetAnimationAsset())
	{
		AnimThatWasPlaying = CurrentAnim;
		TimeWhenPaused = SourceAnimInstance->GetCurrentTime();
	}
	
	SourceAnimInstance->SetPlaying(false);
	SourceAnimInstance->SetAnimationAsset(nullptr);
}

void FIKRetargetEditorController::ResumePlayback()
{
	SourceAnimInstance->SetAnimationAsset(AnimThatWasPlaying);
	SourceAnimInstance->SetPlaying(true);
	SourceAnimInstance->SetPosition(TimeWhenPaused);
}

float FIKRetargetEditorController::GetRetargetPoseAmount() const
{
	return RetargetPosePreviewBlend;
}

void FIKRetargetEditorController::SetRetargetPoseAmount(float InValue)
{
	if (OutputMode==ERetargeterOutputMode::RunRetarget)
	{
		SetRetargeterMode(ERetargeterOutputMode::ShowRetargetPose);
	}
	
	RetargetPosePreviewBlend = InValue;
	SourceAnimInstance->SetRetargetPoseBlend(RetargetPosePreviewBlend);
	TargetAnimInstance->SetRetargetPoseBlend(RetargetPosePreviewBlend);
}

void FIKRetargetEditorController::SetSourceOrTargetMode(ERetargetSourceOrTarget NewMode)
{
	// already in this mode, so do nothing
	if (NewMode == CurrentlyEditingSourceOrTarget)
	{
		return;
	}

	// clear the selection on old skeleton
	ClearSelection();
	
	// store the new skeleton mode
	CurrentlyEditingSourceOrTarget = NewMode;

	switch (GetRetargeterMode())
	{
		case ERetargeterOutputMode::EditRetargetPose:
		{
			FEditorModeTools& EditorModeManager = Editor.Pin()->GetEditorModeManager();
			FIKRetargetEditPoseMode* EditMode = EditorModeManager.GetActiveModeTyped<FIKRetargetEditPoseMode>(FIKRetargetEditPoseMode::ModeName);
			if (EditMode)
			{
				// FIKRetargetEditPoseMode::Enter() is reentrant and written so we can switch between editing
				// source / target skeleton without having to enter/exit the mode; just call Enter() again
				EditMode->Enter();
			}
			break;
		}
		case ERetargeterOutputMode::ShowRetargetPose:
		case ERetargeterOutputMode::RunRetarget:
		{
			// toggle visibility of currently active skeleton
			const bool bEditingSource = NewMode == ERetargetSourceOrTarget::Source;
			SourceSkelMeshComponent->SkeletonDrawMode = bEditingSource ? ESkeletonDrawMode::Default : ESkeletonDrawMode::GreyedOut;
			TargetSkelMeshComponent->SkeletonDrawMode = bEditingSource ? ESkeletonDrawMode::GreyedOut : ESkeletonDrawMode::Default;
			break;
		}
		default:
			checkNoEntry();
	}
	
	RefreshAllViews();
}

void FIKRetargetEditorController::SetSelectedMesh(UPrimitiveComponent* InMeshComponent)
{
	SelectedMesh = InMeshComponent;
	SourceSkelMeshComponent->PushSelectionToProxy();
	TargetSkelMeshComponent->PushSelectionToProxy();
	SourceSkelMeshComponent->MarkRenderStateDirty();
	TargetSkelMeshComponent->MarkRenderStateDirty();
}

UPrimitiveComponent* FIKRetargetEditorController::GetSelectedMesh()
{
	return SelectedMesh;
}

void FIKRetargetEditorController::EditBoneSelection(
	const TArray<FName>& InBoneNames,
	EBoneSelectionEdit EditMode,
	const bool bFromHierarchyView)
{
	// must have a skeletal mesh
	UDebugSkelMeshComponent* DebugComponent = GetEditedSkeletalMesh();
	if (!DebugComponent->GetSkeletalMeshAsset())
	{
		return;
	}
	
	switch (EditMode)
	{
		case EBoneSelectionEdit::Add:
		{
			for (const FName& BoneName : InBoneNames)
			{
				SelectedBones.AddUnique(BoneName);
			}
			
			break;
		}
		case EBoneSelectionEdit::Remove:
		{
			for (const FName& BoneName : InBoneNames)
			{
				SelectedBones.Remove(BoneName);
			}
			break;
		}
		case EBoneSelectionEdit::Replace:
		{
			SelectedBones = InBoneNames;
			break;
		}
		default:
			checkNoEntry();
	}

	// convert to bone indices
	const FReferenceSkeleton& RefSkeleton = DebugComponent->GetSkeletalMeshAsset()->GetRefSkeleton();
	TArray<int32> SelectedBoneIndices;
	for (const FName& Bone : SelectedBones)
	{
		int32 BoneIndex = RefSkeleton.FindBoneIndex(Bone);
		SelectedBoneIndices.Add(BoneIndex);
		
		if (BoneIndex == INDEX_NONE)
		{
			ensureMsgf(false, TEXT("Incoming selection list is not compatible with the currently edited skeleton."));
			SelectedBoneIndices.Reset();
			break;
		}
	}

	// deselect mesh
	SetSelectedMesh(nullptr);
	
	// apply selection to debug mesh component so rendering knows
	DebugComponent->BonesOfInterest = SelectedBoneIndices;

	// update hierarchy view
	if (!bFromHierarchyView)
	{
		RefreshHierarchyView();
	}

	// update details
	if (SelectedBones.IsEmpty())
	{
		SetDetailsObject(AssetController->GetAsset());
	}
	else
	{
		SelectedBoneDetails.Reset();
		for (const FName& SelectedBone : SelectedBones)
		{
			TObjectPtr<UIKRetargetBoneDetails> BoneDetails = GetDetailsObjectForBone(SelectedBone);
			SelectedBoneDetails.Add(BoneDetails);
		}
		SetDetailsObjects(SelectedBoneDetails);
	}
}

void FIKRetargetEditorController::ClearSelection(const bool bKeepBoneSelection)
{
	// clear mesh selection
	SetSelectedMesh(nullptr);
	
	// deselect all chains
	if (ChainsView.IsValid())
	{
		ChainsView->ClearSelection();
	}

	// clear bone selection
	if (!bKeepBoneSelection)
	{
		const TArray<FName> Empty;
		constexpr bool bFromHierarchy = false;
		EditBoneSelection(Empty, EBoneSelectionEdit::Replace, bFromHierarchy);

		// show global details
		SetDetailsObject(AssetController->GetAsset());
	}

	RefreshDetailsView();
}

void FIKRetargetEditorController::SetRetargeterMode(ERetargeterOutputMode Mode)
{
	if (OutputMode == Mode)
	{
		return;
	}
		
	PreviousMode = OutputMode;
	
	FEditorModeTools& EditorModeManager = Editor.Pin()->GetEditorModeManager();
	
	switch (Mode)
	{
		case ERetargeterOutputMode::EditRetargetPose:
			// enter edit mode
			EditorModeManager.DeactivateMode(FIKRetargetDefaultMode::ModeName);
			EditorModeManager.ActivateMode(FIKRetargetEditPoseMode::ModeName);
			OutputMode = ERetargeterOutputMode::EditRetargetPose;
			SourceAnimInstance->SetRetargetMode(ERetargeterOutputMode::EditRetargetPose);
			TargetAnimInstance->SetRetargetMode(ERetargeterOutputMode::EditRetargetPose);
			PausePlayback();
			SetRetargetPoseAmount(1.0f);
			break;
		
		case ERetargeterOutputMode::RunRetarget:
			EditorModeManager.DeactivateMode(FIKRetargetEditPoseMode::ModeName);
			EditorModeManager.ActivateMode(FIKRetargetDefaultMode::ModeName);
			OutputMode = ERetargeterOutputMode::RunRetarget;
			SourceAnimInstance->SetRetargetMode(ERetargeterOutputMode::RunRetarget);
			TargetAnimInstance->SetRetargetMode(ERetargeterOutputMode::RunRetarget);
			// must reinitialize after editing the retarget pose
			AssetController->BroadcastNeedsReinitialized();
			ResumePlayback();
			break;

		case ERetargeterOutputMode::ShowRetargetPose:
			EditorModeManager.DeactivateMode(FIKRetargetEditPoseMode::ModeName);
			EditorModeManager.ActivateMode(FIKRetargetDefaultMode::ModeName);
			OutputMode = ERetargeterOutputMode::ShowRetargetPose;
			// show retarget pose
			SourceAnimInstance->SetRetargetMode(ERetargeterOutputMode::ShowRetargetPose);
			TargetAnimInstance->SetRetargetMode(ERetargeterOutputMode::ShowRetargetPose);
			PausePlayback();
			SetRetargetPoseAmount(1.0f);
			break;

		default:
			checkNoEntry();
	}

	// details view displays differently depending on output mode
	RefreshDetailsView();
}

FReply FIKRetargetEditorController::HandleShowRetargetPose()
{
	const ERetargeterOutputMode CurrentMode = GetRetargeterMode();
	if (CurrentMode == ERetargeterOutputMode::ShowRetargetPose || CurrentMode == ERetargeterOutputMode::EditRetargetPose)
	{
		SetRetargeterMode(ERetargeterOutputMode::RunRetarget);
	}
	else
	{
		SetRetargeterMode(ERetargeterOutputMode::ShowRetargetPose);
	}
	
	return FReply::Handled();
}

bool FIKRetargetEditorController::CanShowRetargetPose() const
{
	return GetRetargeterMode() != ERetargeterOutputMode::ShowRetargetPose;
}

bool FIKRetargetEditorController::IsShowingRetargetPose() const
{
	return GetRetargeterMode() == ERetargeterOutputMode::ShowRetargetPose;
}

void FIKRetargetEditorController::HandleEditPose()
{
	if (IsEditingPose())
	{
		// stop pose editing
		SetRetargeterMode(PreviousMode);
	}
	else
	{
		// start pose editing
		SetRetargeterMode(ERetargeterOutputMode::EditRetargetPose);
	}
}

bool FIKRetargetEditorController::CanEditPose() const
{
	return GetSkeletalMesh(GetSourceOrTarget()) != nullptr;
}

bool FIKRetargetEditorController::IsEditingPose() const
{
	return GetRetargeterMode() == ERetargeterOutputMode::EditRetargetPose;
}

void FIKRetargetEditorController::HandleNewPose()
{
	SetRetargeterMode(ERetargeterOutputMode::ShowRetargetPose);
	
	// get a unique pose name to use as suggestion
	const FString DefaultNewPoseName = LOCTEXT("NewRetargetPoseName", "CustomRetargetPose").ToString();
	const FName UniqueNewPoseName = AssetController->MakePoseNameUnique(DefaultNewPoseName, GetSourceOrTarget());
	
	SAssignNew(NewPoseWindow, SWindow)
	.Title(LOCTEXT("NewRetargetPoseOptions", "Create New Retarget Pose"))
	.ClientSize(FVector2D(300, 80))
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
				.MinDesiredWidth(275)
				.Text(FText::FromName(UniqueNewPoseName))
			]

			+ SVerticalBox::Slot()
			.Padding(4)
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.Text(LOCTEXT("OkButtonLabel", "Ok"))
					.OnClicked(this, &FIKRetargetEditorController::CreateNewPose)
				]
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
					.OnClicked_Lambda( [this]()
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

bool FIKRetargetEditorController::CanCreatePose() const
{
	return !IsEditingPose();
}

FReply FIKRetargetEditorController::CreateNewPose() const
{
	const FName NewPoseName = FName(NewPoseEditableText.Get()->GetText().ToString());
	AssetController->AddRetargetPose(NewPoseName, nullptr, GetSourceOrTarget());
	NewPoseWindow->RequestDestroyWindow();
	RefreshPoseList();
	return FReply::Handled();
}

void FIKRetargetEditorController::HandleDuplicatePose()
{
	SetRetargeterMode(ERetargeterOutputMode::ShowRetargetPose);
	
	// get a unique pose name to use as suggestion for duplicate
	const FString DuplicateSuffix = LOCTEXT("DuplicateSuffix", "_Copy").ToString();
	FString CurrentPoseName = GetCurrentPoseName().ToString();
	const FString DefaultDuplicatePoseName = CurrentPoseName.Append(*DuplicateSuffix);
	const FName UniqueNewPoseName = AssetController->MakePoseNameUnique(DefaultDuplicatePoseName, GetSourceOrTarget());
	
	SAssignNew(NewPoseWindow, SWindow)
	.Title(LOCTEXT("DuplicateRetargetPoseOptions", "Duplicate Retarget Pose"))
	.ClientSize(FVector2D(300, 80))
	.HasCloseButton(true)
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FAppStyle::GetBrush("Menu.Background") )
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(4)
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				SAssignNew(NewPoseEditableText, SEditableTextBox)
				.MinDesiredWidth(275)
				.Text(FText::FromName(UniqueNewPoseName))
			]

			+ SVerticalBox::Slot()
			.Padding(4)
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.Text(LOCTEXT("OkButtonLabel", "Ok"))
					.OnClicked(this, &FIKRetargetEditorController::CreateDuplicatePose)
				]
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
					.OnClicked_Lambda( [this]()
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

FReply FIKRetargetEditorController::CreateDuplicatePose() const
{
	const FIKRetargetPose& PoseToDuplicate = AssetController->GetCurrentRetargetPose(CurrentlyEditingSourceOrTarget);
	const FName NewPoseName = FName(NewPoseEditableText.Get()->GetText().ToString());
	AssetController->AddRetargetPose(NewPoseName, &PoseToDuplicate, GetSourceOrTarget());
	NewPoseWindow->RequestDestroyWindow();
	RefreshPoseList();
	return FReply::Handled();
}

void FIKRetargetEditorController::HandleDeletePose()
{
	SetRetargeterMode(ERetargeterOutputMode::ShowRetargetPose);
	
	const ERetargetSourceOrTarget SourceOrTarget = GetSourceOrTarget();
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName(SourceOrTarget);
	AssetController->RemoveRetargetPose(CurrentPose, SourceOrTarget);
	RefreshPoseList();
}

bool FIKRetargetEditorController::CanDeletePose() const
{	
	// cannot delete default pose
	return AssetController->GetCurrentRetargetPoseName(GetSourceOrTarget()) != UIKRetargeter::GetDefaultPoseName();
}

void FIKRetargetEditorController::HandleResetAllBones() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName(CurrentlyEditingSourceOrTarget);
	static TArray<FName> Empty; // empty list will reset all bones
	AssetController->ResetRetargetPose(CurrentPose, Empty, GetSourceOrTarget());
}

void FIKRetargetEditorController::HandleResetSelectedBones() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName(CurrentlyEditingSourceOrTarget);
	AssetController->ResetRetargetPose(CurrentPose, GetSelectedBones(), GetSourceOrTarget());
}

void FIKRetargetEditorController::HandleResetSelectedAndChildrenBones() const
{
	// get the reference skeleton we're operating on
	const USkeletalMesh* SkeletalMesh = GetSkeletalMesh(GetSourceOrTarget());
	if (!SkeletalMesh)
	{
		return;
	}
	const FReferenceSkeleton RefSkeleton = SkeletalMesh->GetRefSkeleton();
	
	// get list of all children of selected bones
	TArray<int32> AllChildrenIndices;
	for (const FName& SelectedBone : SelectedBones)
	{
		const int32 SelectedBoneIndex = RefSkeleton.FindBoneIndex(SelectedBone);
		AllChildrenIndices.Add(SelectedBoneIndex);
		
		for (int32 ChildIndex = 0; ChildIndex < RefSkeleton.GetNum(); ++ChildIndex)
		{
			const int32 ParentIndex = RefSkeleton.GetParentIndex(ChildIndex);
			if (ParentIndex != INDEX_NONE && AllChildrenIndices.Contains(ParentIndex))
			{
				AllChildrenIndices.Add(ChildIndex);
			}
		}
	}

	// merge total list of all selected bones and their children
	TArray<FName> BonesToReset = SelectedBones;
	for (const int32 ChildIndex : AllChildrenIndices)
	{
		BonesToReset.AddUnique(RefSkeleton.GetBoneName(ChildIndex));
	}
	
	// reset the bones in the current pose
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName(CurrentlyEditingSourceOrTarget);
	AssetController->ResetRetargetPose(CurrentPose, BonesToReset, GetSourceOrTarget());
}

bool FIKRetargetEditorController::CanResetSelected() const
{
	return !GetSelectedBones().IsEmpty();
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
		.BorderImage( FAppStyle::GetBrush("Menu.Background") )
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
					.OnClicked_Lambda( [this]()
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
	
	AssetController->RenameCurrentRetargetPose(NewPoseName, GetSourceOrTarget());
	RefreshPoseList();
	return FReply::Handled();
}

bool FIKRetargetEditorController::CanRenamePose() const
{
	// cannot rename default pose
	const bool bNotUsingDefaultPose = AssetController->GetCurrentRetargetPoseName(GetSourceOrTarget()) != UIKRetargeter::GetDefaultPoseName();
	// cannot rename pose while editing
	return bNotUsingDefaultPose && !IsEditingPose();
}

void FIKRetargetEditorController::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (TTuple<FName, TObjectPtr<UIKRetargetBoneDetails>> Pair : AllBoneDetails)
	{
		Collector.AddReferencedObject(Pair.Value);
	}
};

FText FIKRetargetEditorController::GetCurrentPoseName() const
{
	return FText::FromName(AssetController->GetCurrentRetargetPoseName(GetSourceOrTarget()));
}

void FIKRetargetEditorController::OnPoseSelected(TSharedPtr<FName> InPose, ESelectInfo::Type SelectInfo) const
{
	if (InPose.IsValid())
	{
		AssetController->SetCurrentRetargetPose(*InPose.Get(), GetSourceOrTarget());
	}
}

#undef LOCTEXT_NAMESPACE
