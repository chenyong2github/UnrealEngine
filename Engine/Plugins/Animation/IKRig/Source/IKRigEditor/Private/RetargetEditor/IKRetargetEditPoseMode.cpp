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

	// render the skeleton
	RenderSkeleton(PDI, Controller);
}

void FIKRetargetEditPoseMode::RenderSkeleton(
	FPrimitiveDrawInterface* PDI,
	const FIKRetargetEditorController* Controller)
{
	const UDebugSkelMeshComponent* MeshComponent = Controller->GetSkeletalMeshComponent(SourceOrTarget);
	FTransform ComponentTransform = MeshComponent->GetComponentTransform();
	const FReferenceSkeleton& RefSkeleton = MeshComponent->GetReferenceSkeleton();
	const int32 NumBones = RefSkeleton.GetNum();

	// get world transforms of bones
	TArray<FBoneIndexType> RequiredBones;
	RequiredBones.AddUninitialized(NumBones);
	TArray<FTransform> WorldTransforms;
	WorldTransforms.AddUninitialized(NumBones);
	for (int32 Index=0; Index<NumBones; ++Index)
	{
		RequiredBones[Index] = Index;
		WorldTransforms[Index] = MeshComponent->GetBoneTransform(Index, ComponentTransform);
	}
	
	const UIKRetargeter* Asset = Controller->AssetController->GetAsset();
	const float BoneDrawSize = Asset->BoneDrawSize;
	const float MaxDrawRadius = Controller->TargetSkelMeshComponent->Bounds.SphereRadius * 0.01f;
	const float BoneRadius = FMath::Min(1.0f, MaxDrawRadius) * BoneDrawSize;
	
	UPersonaOptions* ConfigOption = UPersonaOptions::StaticClass()->GetDefaultObject<UPersonaOptions>();
	
	FSkelDebugDrawConfig DrawConfig;
	DrawConfig.BoneDrawMode = (EBoneDrawMode::Type)ConfigOption->DefaultBoneDrawSelection;
	DrawConfig.BoneDrawSize = BoneRadius;
	DrawConfig.bAddHitProxy = true;
	DrawConfig.bForceDraw = true;
	DrawConfig.DefaultBoneColor = GetMutableDefault<UPersonaOptions>()->DefaultBoneColor;
	DrawConfig.AffectedBoneColor = GetMutableDefault<UPersonaOptions>()->AffectedBoneColor;
	DrawConfig.SelectedBoneColor = GetMutableDefault<UPersonaOptions>()->SelectedBoneColor;
	DrawConfig.ParentOfSelectedBoneColor = GetMutableDefault<UPersonaOptions>()->ParentOfSelectedBoneColor;

	TArray<HHitProxy*> HitProxies;
	HitProxies.AddUninitialized(NumBones);
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		HitProxies[Index] = new HIKRetargetEditorBoneProxy(RefSkeleton.GetBoneName(Index));
	}

	// record selected bone indices
	const TArray<FName>& SelectedBoneNames = Controller->GetSelectedBones();
	TArray<int32> SelectedBones;
	for (const FName& SelectedBoneName : SelectedBoneNames)
	{
		int32 SelectedBoneIndex = RefSkeleton.FindBoneIndex(SelectedBoneName);
		SelectedBones.Add(SelectedBoneIndex);
	}

	SkeletalDebugRendering::DrawBones(
		PDI,
		ComponentTransform.GetLocation(),
		RequiredBones,
		RefSkeleton,
		WorldTransforms,
		SelectedBones,
		TArray<FLinearColor>(),
		HitProxies,
		DrawConfig
	);
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

	const USkeletalMesh* SkeletalMesh = Controller->GetSkeletalMesh(SourceOrTarget);
	if (!SkeletalMesh)
	{
		return FVector::ZeroVector; 
	}
	
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(SelectedBones.Last());
	if (BoneIndex == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	
	float Scale;
	FVector Offset;
	GetEditedComponentScaleAndOffset(Scale,Offset);
	
	return Controller->GetGlobalRetargetPoseOfBone(SourceOrTarget, BoneIndex, Scale, Offset).GetTranslation();
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
			Controller->AssetController->SetRotationOffsetForRetargetPoseBone(BoneName, TotalDeltaRotation, Controller->GetSourceOrTarget());
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
		Controller->AssetController->AddTranslationOffsetToRetargetRootBone(InDrag, Controller->GetSourceOrTarget());
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
	
	// clear bone edit
	BoneEdit.Reset();

	// deselect everything except bones
	constexpr bool bKeepBoneSelection = true;
	Controller->ClearSelection(bKeepBoneSelection);
	
	// record skeleton to edit (must be constant between enter/exit)
	SourceOrTarget = Controller->GetSourceOrTarget();
	// hide skeleton we're editing so that we can draw our own editable version of it
	const bool bEditingSource = SourceOrTarget == ERetargetSourceOrTarget::Source;
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

void FIKRetargetEditPoseMode::GetEditedComponentScaleAndOffset(float& OutScale, FVector& OutOffset) const
{
	OutScale = 1.0f;
	OutOffset = FVector::Zero();

	const bool bIsSource = EditorController.Pin()->GetSourceOrTarget() == ERetargetSourceOrTarget::Source;
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

	const USkeletalMesh* SkeletalMesh = Controller->GetSkeletalMesh(SourceOrTarget);
	if (!SkeletalMesh)
	{
		BoneEdit.GlobalTransform = FTransform::Identity;
		return;
	}
	
	float Scale;
	FVector Offset;
	GetEditedComponentScaleAndOffset(Scale,Offset);

	const UIKRetargeterController* AssetController = Controller->AssetController;
	const FIKRetargetPose& RetargetPose = AssetController->GetCurrentRetargetPose(SourceOrTarget);
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();

	BoneEdit.Name = SelectedBones.Last();
	BoneEdit.Index = RefSkeleton.FindBoneIndex(BoneEdit.Name);
	BoneEdit.GlobalTransform = Controller->GetGlobalRetargetPoseOfBone(SourceOrTarget, BoneEdit.Index, Scale, Offset);
	BoneEdit.AccumulatedGlobalOffset = FQuat::Identity;

	BoneEdit.PreviousDeltaRotation.Reset();
	for (int32 SelectionIndex=0; SelectionIndex<SelectedBones.Num(); ++SelectionIndex)
	{
		FQuat PrevDeltaRotation = RetargetPose.GetDeltaRotationForBone(SelectedBones[SelectionIndex]);
		BoneEdit.PreviousDeltaRotation.Add(PrevDeltaRotation);
	}
	
	const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneEdit.Index);
	BoneEdit.ParentGlobalTransform = Controller->GetGlobalRetargetPoseOfBone(SourceOrTarget, ParentIndex, Scale, Offset);
}

bool FIKRetargetEditPoseMode::IsRootSelected() const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	const TArray<FName>& SelectedBones = Controller->GetSelectedBones();
	const FName& RootName = Controller->AssetController->GetRetargetRootBone(SourceOrTarget);
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

	return Controller->AssetController->GetRetargetRootBone(SourceOrTarget) == SelectedBones[0];
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
