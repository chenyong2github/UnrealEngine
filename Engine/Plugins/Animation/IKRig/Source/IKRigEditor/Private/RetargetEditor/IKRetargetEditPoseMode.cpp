// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditPoseMode.h"

#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "IKRigDebugRendering.h"
#include "IKRigProcessor.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Preferences/PersonaOptions.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetHitProxies.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "RigEditor/IKRigEditMode.h"


#define LOCTEXT_NAMESPACE "IKRetargeterEditMode"

FName FIKRetargetEditPoseMode::ModeName("IKRetargetAssetEditMode");

bool FIKRetargetEditPoseMode::GetCameraTarget(FSphere& OutTarget) const
{
	// target skeletal mesh
	if (const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin())
	{
		if (Controller->TargetSkelMeshComponent)
		{
			OutTarget = Controller->TargetSkelMeshComponent->Bounds.GetSphere();
			return true;
		}
	}
	
	return false;
}

IPersonaPreviewScene& FIKRetargetEditPoseMode::GetAnimPreviewScene() const
{
	return *static_cast<IPersonaPreviewScene*>(static_cast<FAssetEditorModeManager*>(Owner)->GetPreviewScene());
}

void FIKRetargetEditPoseMode::GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const
{
	// todo: provide warnings
}

void FIKRetargetEditPoseMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	const FIKRetargetEditorController* Controller = EditorController.Pin().Get();
	if (!Controller)
	{
		return;
	}

	const UIKRetargetProcessor* Processor = EditorController.Pin()->GetRetargetProcessor();
	if (!(Processor && Processor->IsInitialized()))
	{
		return;
	}

	// render the skeleton
	RenderSkeleton(PDI, Controller, Processor);
}

void FIKRetargetEditPoseMode::RenderSkeleton(
	FPrimitiveDrawInterface* PDI,
	const FIKRetargetEditorController* Controller,
	const UIKRetargetProcessor* Processor)
{
	check(Processor && Controller && Processor->IsInitialized())
	
	const FRetargetSkeleton& Skeleton = GetCurrentlyEditedSkeleton(*Processor);
	
	// get selected and affected bones in this skeleton
	TSet<int32> OutAffectedBones;
	TSet<int32> OutSelectedBones;
	GetSelectedAndAffectedBones(Controller, Skeleton, OutAffectedBones, OutSelectedBones);

	const UIKRetargeter* Asset = Controller->AssetController->GetAsset();
	float Scale;
	FVector Offset;
	GetEditedComponentScaleAndOffset(Scale, Offset);
	const float BoneDrawSize = Asset->BoneDrawSize;
	const float MaxDrawRadius = Controller->TargetSkelMeshComponent->Bounds.SphereRadius * 0.01f;

	// use colors from user preferences
	const UPersonaOptions* PersonaOptions = GetDefault<UPersonaOptions>();

	// loop over whole skeleton and draw each bone
	for (int32 BoneIndex = 0; BoneIndex<Skeleton.BoneNames.Num(); ++BoneIndex)
	{
		// selected bones are drawn with different color, affected bones are highlighted
		const bool bIsSelected = OutSelectedBones.Contains(BoneIndex);
		const bool bIsAffected = OutAffectedBones.Contains(BoneIndex);
		const bool bIsRetargeted = Processor->IsBoneRetargeted(BoneIndex, (int8)SkeletonMode);
		FLinearColor LineColor = bIsAffected ? PersonaOptions->AffectedBoneColor : PersonaOptions->DefaultBoneColor;
		LineColor = bIsSelected ? PersonaOptions->SelectedBoneColor : LineColor;
		LineColor = bIsRetargeted ? LineColor : FLinearColor::LerpUsingHSV(LineColor, PersonaOptions->DisabledBoneColor, 0.35f);
		
		const float BoneRadius = FMath::Min(1.0f, MaxDrawRadius) * BoneDrawSize;
		
		// get the location of the bone on the currently initialized target skeletal mesh
		// along with array of child positions
		const FTransform BoneTransform = Controller->GetGlobalRetargetPoseOfBone(Skeleton, BoneIndex, Scale, Offset);
		TArray<FVector> ChildrenPositions;
		TArray<int32> ChildrenIndices;
		Controller->GetGlobalRetargetPoseOfImmediateChildren(
			Skeleton,
			BoneIndex,
			Scale,
			Offset,
			ChildrenIndices,
			ChildrenPositions);

		// draw cone to parent with different color if this bone is NOT selected, but the child IS selected
		TArray<FLinearColor> ChildColors;
		for (int32 ChildIndex : ChildrenIndices)
		{
			FLinearColor ChildLineColor = LineColor;
			if (!bIsSelected && OutSelectedBones.Contains(ChildIndex))
			{
				ChildLineColor = PersonaOptions->ParentOfSelectedBoneColor;
			}
			ChildColors.Add(ChildLineColor);
		}
		
		// draw the bone proxy
		PDI->SetHitProxy(new HIKRetargetEditorBoneProxy(Skeleton.BoneNames[BoneIndex]));
		SkeletalDebugRendering::DrawWireBoneAdvanced(
			PDI,
			BoneTransform,
			ChildrenPositions,
			ChildColors,
			LineColor,
			SDPG_Foreground,
			BoneRadius,
			bIsSelected || bIsAffected);
		PDI->SetHitProxy(NULL);
	}
}

void FIKRetargetEditPoseMode::GetSelectedAndAffectedBones(
	const FIKRetargetEditorController* Controller,
	const FRetargetSkeleton& Skeleton,
	TSet<int32>& OutAffectedBones,
	TSet<int32>& OutSelectedBones) const
{
	// record selected bone indices
	const TArray<FName>& SelectedBones = Controller->GetSelectedBones();
	for (const FName& SelectedBone : SelectedBones)
	{
		OutSelectedBones.Add(Skeleton.FindBoneIndexByName(SelectedBone));
	}

	// "affected bones" are the selected bones AND their children, recursively
	for (int32 SelectedBone : OutSelectedBones)
	{
		const int32 EndOfBranch = Skeleton.GetCachedEndOfBranchIndex(SelectedBone);
		OutAffectedBones.Add(SelectedBone);
		for (int32 BoneIndex=SelectedBone; BoneIndex<=EndOfBranch; ++BoneIndex)
		{
			OutAffectedBones.Add(BoneIndex);
		}
	}
}

bool FIKRetargetEditPoseMode::AllowWidgetMove()
{
	return false;
}

bool FIKRetargetEditPoseMode::ShouldDrawWidget() const
{
	return UsesTransformWidget(CurrentWidgetMode);
}

bool FIKRetargetEditPoseMode::UsesTransformWidget() const
{
	return UsesTransformWidget(CurrentWidgetMode);
}

bool FIKRetargetEditPoseMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	const bool bIsOnlyRootSelected = IsOnlyRootSelected();
	const bool bIsAnyBoneSelected = !Controller->GetSelectedBones().IsEmpty();

	if (!bIsAnyBoneSelected)
	{
		return false; // no bones selected, can't transform anything
	}
	
	if (bIsOnlyRootSelected && CheckMode == UE::Widget::EWidgetMode::WM_Translate)
	{
		return true; // can translate only the root
	}

	if (bIsAnyBoneSelected && CheckMode == UE::Widget::EWidgetMode::WM_Rotate)
	{
		return true; // can rotate any bone
	}
	
	return false;
}

FVector FIKRetargetEditPoseMode::GetWidgetLocation() const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FVector::ZeroVector; 
	}

	const TArray<FName>& SelectedBones = Controller->GetSelectedBones();
	if (SelectedBones.IsEmpty())
	{
		return FVector::ZeroVector; 
	}

	const UIKRetargetProcessor* RetargetProcessor = Controller->GetRetargetProcessor();
	if (!(RetargetProcessor && RetargetProcessor->IsInitialized()))
	{
		return FVector::ZeroVector;
	}

	const bool bIsSource = Controller->GetSkeletonMode() == EIKRetargetSkeletonMode::Source;
	
	const FRetargetSkeleton& Skeleton = bIsSource ? RetargetProcessor->GetSourceSkeleton() : RetargetProcessor->GetTargetSkeleton();
	const int32 BoneIndex = Skeleton.FindBoneIndexByName(SelectedBones.Last());
	if (BoneIndex == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	
	const UIKRetargeter* Asset = Controller->AssetController->GetAsset();
	const float Scale = bIsSource ? 1.0f : Asset->TargetMeshScale;
	const FVector Offset = bIsSource ? Asset->SourceMeshOffset : Asset->TargetMeshOffset;
	
	return Controller->GetGlobalRetargetPoseOfBone(Skeleton, BoneIndex, Scale, Offset).GetTranslation();
}

bool FIKRetargetEditPoseMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	
	if (Click.GetKey() != EKeys::LeftMouseButton)
	{
		return false;
	}
	
	// selected bone ?
	if (HitProxy && HitProxy->IsA(HIKRetargetEditorBoneProxy::StaticGetType()))
	{
		HIKRetargetEditorBoneProxy* BoneProxy = static_cast<HIKRetargetEditorBoneProxy*>(HitProxy);
		const TArray<FName> BoneNames{BoneProxy->BoneName};
		constexpr bool bFromHierarchy = false;
		const EBoneSelectionEdit EditMode = Click.IsControlDown() || Click.IsShiftDown() ? EBoneSelectionEdit::Add : EBoneSelectionEdit::Replace;
		Controller->EditBoneSelection(BoneNames, EditMode, bFromHierarchy);
		return true;
	}

	// clicking in empty space clears selection
	Controller->ClearSelection();
	return true;
}

bool FIKRetargetEditPoseMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	TrackingState = FIKRetargetTrackingState::None;

	// not manipulating any widget axes, so stop tracking
	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	if (CurrentAxis == EAxisList::None)
	{
		return false; 
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; // invalid editor state
	}

	// get state of viewport
	const bool bTranslating = InViewportClient->GetWidgetMode() == UE::Widget::EWidgetMode::WM_Translate;
	const bool bRotating = InViewportClient->GetWidgetMode() == UE::Widget::EWidgetMode::WM_Rotate;
	const bool bAnyBoneSelected = !Controller->GetSelectedBones().IsEmpty();
	const bool bOnlyRootSelected = IsOnlyRootSelected();

	// is any bone being rotated?
	if (bRotating && bAnyBoneSelected)
	{
		// Start a rotation transaction
		GEditor->BeginTransaction(LOCTEXT("RotateRetargetPoseBone", "Rotate Retarget Pose Bone"));
		Controller->AssetController->GetAsset()->Modify();
		TrackingState = FIKRetargetTrackingState::RotatingBone;
		UpdateWidgetTransform();
		return true;
	}

	// is the root being translated?
	if (bTranslating && bOnlyRootSelected)
	{
		// Start a translation transaction
		GEditor->BeginTransaction(LOCTEXT("TranslateRetargetPoseBone", "Translate Retarget Pose Bone"));
		Controller->AssetController->GetAsset()->Modify();
		TrackingState = FIKRetargetTrackingState::TranslatingRoot;
		UpdateWidgetTransform();
		return true;
	}

	return false;
}

bool FIKRetargetEditPoseMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{	
	if (TrackingState == FIKRetargetTrackingState::None)
	{
		const bool bIsRootSelected = IsRootSelected();
		const bool bTranslating = InViewportClient->GetWidgetMode() == UE::Widget::EWidgetMode::WM_Translate;
		// forcibly prevent translation of anything but the root
		if (!bIsRootSelected && bTranslating)
		{
			InViewportClient->SetWidgetMode(UE::Widget::EWidgetMode::WM_Rotate);
		}
		
		return true; // not handled
	}

	GEditor->EndTransaction();
	TrackingState = FIKRetargetTrackingState::None;
	return true;
}

bool FIKRetargetEditPoseMode::InputDelta(
	FEditorViewportClient* InViewportClient,
	FViewport* InViewport,
	FVector& InDrag,
	FRotator& InRot,
	FVector& InScale)
{
	if (TrackingState == FIKRetargetTrackingState::None)
	{
		return false; // not handled
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	const UIKRetargetProcessor* Processor = Controller->GetRetargetProcessor();
	if (!(Processor && Processor->IsInitialized()))
	{
		return false;
	}
	const FRetargetSkeleton& Skeleton = GetCurrentlyEditedSkeleton(*Processor);
	
	// rotating any bone
	if (TrackingState == FIKRetargetTrackingState::RotatingBone)
	{
		if(InViewportClient->GetWidgetMode() != UE::Widget::WM_Rotate)
		{
			return false;
		}

		// accumulate the rotation from the viewport gizmo (amount of rotation since tracking started)
		BoneEdit.AccumulatedGlobalOffset = InRot.Quaternion() * BoneEdit.AccumulatedGlobalOffset;

		// convert world space delta quaternion to bone-space
		const FVector RotationAxis = BoneEdit.AccumulatedGlobalOffset.GetRotationAxis();
		const FVector UnRotatedAxis = BoneEdit.GlobalTransform.InverseTransformVector(RotationAxis);
		const FQuat BoneLocalDelta = FQuat(UnRotatedAxis, BoneEdit.AccumulatedGlobalOffset.GetAngle());
		
		// apply rotation delta to all selected bones
		const TArray<FName>& SelectedBones = Controller->GetSelectedBones();
		for (int32 SelectionIndex=0; SelectionIndex<SelectedBones.Num(); ++SelectionIndex)
		{
			const FName& BoneName = SelectedBones[SelectionIndex];

			// apply the new delta to the retarget pose
			const FQuat TotalDeltaRotation = BoneEdit.PreviousDeltaRotation[SelectionIndex] * BoneLocalDelta;
			Controller->AssetController->SetRotationOffsetForRetargetPoseBone(BoneName, TotalDeltaRotation);
		}

		return true;
	}
	
	// translating root
	if (TrackingState == FIKRetargetTrackingState::TranslatingRoot)
	{
		if(InViewportClient->GetWidgetMode() != UE::Widget::WM_Translate)
		{
			return false;
		}

		// apply translation delta to root
		Controller->AssetController->AddTranslationOffsetToRetargetRootBone(InDrag);
		return true;
	}
	
	return false;
}

bool FIKRetargetEditPoseMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	if (Controller->GetSelectedBones().IsEmpty())
	{
		return false; // nothing selected to manipulate
	}

	if (TrackingState == FIKRetargetTrackingState::None)
	{
		UpdateWidgetTransform();
	}

	InMatrix = BoneEdit.GlobalTransform.ToMatrixNoScale().RemoveTranslation();
	return true;
}

bool FIKRetargetEditPoseMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

void FIKRetargetEditPoseMode::Enter()
{
	IPersonaEditMode::Enter();
	
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

	const UIKRetargetProcessor* Processor = Controller->GetRetargetProcessor();
	if (!Processor)
	{
		return;
	}

	// put source mesh in reference pose
	Controller->SourceSkelMeshComponent->ShowReferencePose(true);
	// have to move component back to offset position because ShowReferencePose() sets it back to origin
	Controller->AddOffsetToMeshComponent(FVector::ZeroVector, Controller->SourceSkelMeshComponent);
	// put asset in mode where target mesh will output retarget pose (for preview purposes)
	Controller->AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::EditRetargetPose);

	// clear bone edit
	BoneEdit.Reset();

	// deselect everything except bones
	constexpr bool bKeepBoneSelection = true;
	Controller->ClearSelection(bKeepBoneSelection);
	
	// record skeleton to edit (must be constant between enter/exit)
	SkeletonMode = Controller->GetSkeletonMode();
	// hide skeleton we're editing so that we can draw our own editable version of it
	const bool bEditingSource = SkeletonMode == EIKRetargetSkeletonMode::Source;
	Controller->SourceSkelMeshComponent->SkeletonDrawMode = bEditingSource ? ESkeletonDrawMode::Hidden : ESkeletonDrawMode::GreyedOut;
	Controller->TargetSkelMeshComponent->SkeletonDrawMode = !bEditingSource ? ESkeletonDrawMode::Hidden : ESkeletonDrawMode::GreyedOut;
}

void FIKRetargetEditPoseMode::Exit()
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

	IPersonaEditMode::Exit();
}

const FRetargetSkeleton& FIKRetargetEditPoseMode::GetCurrentlyEditedSkeleton(const UIKRetargetProcessor& Processor) const
{
	return SkeletonMode == EIKRetargetSkeletonMode::Source ? Processor.GetSourceSkeleton() : Processor.GetTargetSkeleton();
}

void FIKRetargetEditPoseMode::GetEditedComponentScaleAndOffset(float& OutScale, FVector& OutOffset) const
{
	OutScale = 1.0f;
	OutOffset = FVector::Zero();

	const bool bIsSource = EditorController.Pin()->GetSkeletonMode() == EIKRetargetSkeletonMode::Source;
	const UIKRetargeter* Asset = EditorController.Pin()->AssetController->GetAsset();
	if (!Asset)
	{
		return;
	}
	
	OutScale = bIsSource ? 1.0f : Asset->TargetMeshScale;
	OutOffset = bIsSource ? Asset->SourceMeshOffset : Asset->TargetMeshOffset;
}

void FIKRetargetEditPoseMode::UpdateWidgetTransform()
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		BoneEdit.GlobalTransform = FTransform::Identity;
		return;
	}

	const TArray<FName>& SelectedBones = Controller->GetSelectedBones();
	if (SelectedBones.IsEmpty())
	{
		BoneEdit.GlobalTransform = FTransform::Identity;
		return;
	}

	const UIKRetargetProcessor* RetargetProcessor = Controller->GetRetargetProcessor();
	if (!(RetargetProcessor && RetargetProcessor->IsInitialized()))
	{
		return;
	}
	
	const UIKRetargeterController* AssetController = Controller->AssetController;
	const FRetargetSkeleton& Skeleton = GetCurrentlyEditedSkeleton(*RetargetProcessor);
	float Scale;
	FVector Offset;
	GetEditedComponentScaleAndOffset(Scale,Offset);

	const FIKRetargetPose& RetargetPose = AssetController->GetCurrentRetargetPose();

	BoneEdit.Name = SelectedBones.Last();
	BoneEdit.Index = Skeleton.FindBoneIndexByName(BoneEdit.Name);
	BoneEdit.GlobalTransform = Controller->GetGlobalRetargetPoseOfBone(Skeleton, BoneEdit.Index, Scale, Offset);
	BoneEdit.AccumulatedGlobalOffset = FQuat::Identity;

	BoneEdit.PreviousDeltaRotation.Reset();
	for (int32 SelectionIndex=0; SelectionIndex<SelectedBones.Num(); ++SelectionIndex)
	{
		FQuat PrevDeltaRotation = RetargetProcessor->GetTargetSkeleton().GetRetargetPoseDeltaRotation(SelectedBones[SelectionIndex], &RetargetPose);
		BoneEdit.PreviousDeltaRotation.Add(PrevDeltaRotation);
	}
	
	const int32 ParentIndex = Skeleton.GetParentIndex(BoneEdit.Index);
	if (ParentIndex != INDEX_NONE)
	{
		BoneEdit.ParentGlobalTransform = Controller->GetGlobalRetargetPoseOfBone(Skeleton, ParentIndex, Scale, Offset);
	}else
	{
		BoneEdit.ParentGlobalTransform = FTransform::Identity;
	}
}

bool FIKRetargetEditPoseMode::IsRootSelected() const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	const TArray<FName>& SelectedBones = Controller->GetSelectedBones();
	const FName& RootName = Controller->AssetController->GetTargetRootBone();
	return SelectedBones.Contains(RootName);
}

bool FIKRetargetEditPoseMode::IsOnlyRootSelected() const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	const TArray<FName>& SelectedBones = Controller->GetSelectedBones();
	if (SelectedBones.Num() != 1)
	{
		return false;
	}

	return Controller->AssetController->GetTargetRootBone() == SelectedBones[0];
}

void FIKRetargetEditPoseMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
	
	CurrentWidgetMode = ViewportClient->GetWidgetMode();
}

void FIKRetargetEditPoseMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

#undef LOCTEXT_NAMESPACE
