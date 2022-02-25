// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditMode.h"

#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "EditorScriptingHelpers.h"
#include "IKRigDebugRendering.h"
#include "IKRigProcessor.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetHitProxies.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "RigEditor/IKRigEditMode.h"

FName FIKRetargetEditMode::ModeName("IKRetargetAssetEditMode");

#define LOCTEXT_NAMESPACE "IKRetargeterEditMode"

bool FIKRetargetEditMode::GetCameraTarget(FSphere& OutTarget) const
{
	// target skeletal mesh
	if (const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin())
	{
		if (Controller->SourceSkelMeshComponent)
		{
			OutTarget = Controller->SourceSkelMeshComponent->Bounds.GetSphere();
			return true;
		}
	}
	
	return false;
}

IPersonaPreviewScene& FIKRetargetEditMode::GetAnimPreviewScene() const
{
	return *static_cast<IPersonaPreviewScene*>(static_cast<FAssetEditorModeManager*>(Owner)->GetPreviewScene());
}

void FIKRetargetEditMode::GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const
{
	// todo: provide warnings
}

void FIKRetargetEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

	const UIKRetargetProcessor* RetargetProcessor = Controller->GetRetargetProcessor();
	if (!(RetargetProcessor && RetargetProcessor->IsInitialized()))
	{
		return;
	}

	const UIKRetargeter* Asset = Controller->AssetController->GetAsset();
	const FTargetSkeleton& TargetSkeleton = RetargetProcessor->GetTargetSkeleton();
	const int32 RootBoneIndex = RetargetProcessor->GetTargetSkeletonRootBone();

	TSet<int32> OutAffectedBones;
	TSet<int32> OutSelectedBones;
	UIKRigProcessor* IKRigProcessor = RetargetProcessor->GetTargetIKRigProcessor();
	GetAffectedBones(Controller.Get(), IKRigProcessor, OutAffectedBones, OutSelectedBones);

	const float MaxDrawRadius = Controller->TargetSkelMeshComponent->Bounds.SphereRadius * 0.01f;
	for (int32 BoneIndex = 0; BoneIndex<TargetSkeleton.BoneNames.Num(); ++BoneIndex)
	{		
		// filter out bones that cannot be edited as part of the retarget pose
		const bool bIsRootBone = BoneIndex==RootBoneIndex;
		const bool bIsInTargetChain = TargetSkeleton.IsBoneInAnyTargetChain[BoneIndex];
		if (!(bIsRootBone || bIsInTargetChain))
		{
			continue;
		}

		// selected bones are drawn with different color, affected bones are highlighted
		const bool bIsSelected = OutSelectedBones.Contains(BoneIndex);
		const bool bIsAffected = OutAffectedBones.Contains(BoneIndex);
		FLinearColor LineColor = bIsAffected ? IKRigDebugRendering::AFFECTED_BONE_COLOR : IKRigDebugRendering::DESELECTED_BONE_COLOR;
		LineColor = bIsSelected ? IKRigDebugRendering::SELECTED_BONE_COLOR : LineColor;

		const float BoneRadiusSetting = Asset->BoneDrawSize;
		const float BoneRadius = FMath::Min(1.0f, MaxDrawRadius) * BoneRadiusSetting;
		
		// get the location of the bone on the currently initialized target skeletal mesh
		// along with array of child positions
		const FTransform BoneTransform = Controller->GetTargetBoneGlobalTransform(RetargetProcessor, BoneIndex);
		FVector Start;
		TArray<FVector> ChildLocations;
		Controller->GetTargetBoneLineSegments(RetargetProcessor, BoneIndex, Start, ChildLocations);
		
		// draw the bone proxy
		PDI->SetHitProxy(new HIKRetargetEditorBoneProxy(TargetSkeleton.BoneNames[BoneIndex]));
		IKRigDebugRendering::DrawWireBone(
			PDI,
			BoneTransform,
			ChildLocations,
			LineColor,
			SDPG_Foreground,
			BoneRadius,
			bIsSelected || bIsAffected);
		PDI->SetHitProxy(NULL);
	}
}

void FIKRetargetEditMode::GetAffectedBones(
	FIKRetargetEditorController* Controller,
	UIKRigProcessor* Processor,
	TSet<int32>& OutAffectedBones,
	TSet<int32>& OutSelectedBones) const
{
	if (!(Controller && Processor))
	{
		return;
	}

	const FIKRigSkeleton& Skeleton = Processor->GetSkeleton();

	// record selected bone indices
	for (const FName& SelectedBone : BoneEdit.SelectedBones)
	{
		OutSelectedBones.Add(Skeleton.GetBoneIndexFromName(SelectedBone));
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

bool FIKRetargetEditMode::AllowWidgetMove()
{
	return false;
}

bool FIKRetargetEditMode::ShouldDrawWidget() const
{
	return UsesTransformWidget(CurrentWidgetMode);
}

bool FIKRetargetEditMode::UsesTransformWidget() const
{
	return UsesTransformWidget(CurrentWidgetMode);
}

bool FIKRetargetEditMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	const bool bIsOnlyRootSelected = IsOnlyRootSelected();
	const bool bIsAnyBoneSelected = !BoneEdit.SelectedBones.IsEmpty();

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

FVector FIKRetargetEditMode::GetWidgetLocation() const
{
	if (BoneEdit.SelectedBones.IsEmpty())
	{
		return FVector::ZeroVector; 
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FVector::ZeroVector; 
	}

	const FTargetSkeleton& TargetSkeleton = Controller->GetRetargetProcessor()->GetTargetSkeleton();
	const int32 BoneIndex = TargetSkeleton.FindBoneIndexByName(BoneEdit.SelectedBones.Last());
	if (BoneIndex == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}

	const UIKRetargetProcessor* RetargetProcessor = Controller->GetRetargetProcessor();
	if (!(RetargetProcessor && RetargetProcessor->IsInitialized()))
	{
		return FVector::ZeroVector;
	}
	
	return Controller->GetTargetBoneGlobalTransform(RetargetProcessor, BoneIndex).GetTranslation();
}

bool FIKRetargetEditMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	// check for selections
	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		// selected bone ?
		if (HitProxy && HitProxy->IsA(HIKRetargetEditorBoneProxy::StaticGetType()))
		{
			HIKRetargetEditorBoneProxy* BoneProxy = static_cast<HIKRetargetEditorBoneProxy*>(HitProxy);
			const bool bReplaceSelection = !(InViewportClient->IsCtrlPressed() || InViewportClient->IsShiftPressed());
			HandleBoneSelectedInViewport(BoneProxy->BoneName, bReplaceSelection);
			return true;
		}
		else
		{
			// clicking in empty space clears selection
			const bool bReplaceSelection = true;
			HandleBoneSelectedInViewport(NAME_None, bReplaceSelection);

			// show asset in details view
			const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
			if (Controller.IsValid())
			{
				Controller->DetailsView->SetObject(Controller->AssetController->GetAsset());
			}
		}
	}
	
	return false;
}

bool FIKRetargetEditMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
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
	const bool bAnyBoneSelected = !BoneEdit.SelectedBones.IsEmpty();
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

bool FIKRetargetEditMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
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

bool FIKRetargetEditMode::InputDelta(
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
		BoneEdit.AccumulatedGlobalOffset = InRot.Quaternion().Inverse() * BoneEdit.AccumulatedGlobalOffset;

		// convert to a delta rotation in the local space of the last selected bone
		const FQuat BoneGlobalOrig = BoneEdit.GlobalTransform.GetRotation();
		const FQuat BoneGlobalPlusOffset = BoneEdit.AccumulatedGlobalOffset * BoneGlobalOrig;
		const FQuat ParentInv = BoneEdit.ParentGlobalTransform.GetRotation().Inverse();
		const FQuat BoneLocal = ParentInv * BoneGlobalOrig;
		const FQuat BoneLocalPlusOffset = ParentInv * BoneGlobalPlusOffset;
		const FQuat BoneLocalOffset = BoneLocal * BoneLocalPlusOffset.Inverse();
		
		// apply rotation delta to all selected bones
		for (int32 SelectionIndex=0; SelectionIndex<BoneEdit.SelectedBones.Num(); ++SelectionIndex)
		{
			const FQuat TotalOffsetLocal = BoneLocalOffset * BoneEdit.PrevLocalOffsets[SelectionIndex];
			const FName& BoneName = BoneEdit.SelectedBones[SelectionIndex];
			Controller->AssetController->SetRotationOffsetForRetargetPoseBone(BoneName, TotalOffsetLocal);
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

bool FIKRetargetEditMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (BoneEdit.SelectedBones.IsEmpty())
	{
		return false; // nothing selected to manipulate
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	if (TrackingState == FIKRetargetTrackingState::None)
	{
		UpdateWidgetTransform();
	}

	InMatrix = BoneEdit.GlobalTransform.ToMatrixNoScale().RemoveTranslation();
	return true;
}

bool FIKRetargetEditMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

void FIKRetargetEditMode::UpdateWidgetTransform()
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid() || BoneEdit.SelectedBones.IsEmpty())
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
	const FTargetSkeleton& TargetSkeleton = RetargetProcessor->GetTargetSkeleton();

	BoneEdit.Name = BoneEdit.SelectedBones.Last();
	BoneEdit.Index = TargetSkeleton.FindBoneIndexByName(BoneEdit.Name);
	BoneEdit.GlobalTransform = Controller->GetTargetBoneGlobalTransform(RetargetProcessor, BoneEdit.Index);
	BoneEdit.AccumulatedGlobalOffset = FQuat::Identity;

	BoneEdit.PrevLocalOffsets.Reset();
	for (int32 SelectionIndex=0; SelectionIndex<BoneEdit.SelectedBones.Num(); ++SelectionIndex)
	{
		FQuat PrevLocalOffset = AssetController->GetRotationOffsetForRetargetPoseBone(BoneEdit.SelectedBones[SelectionIndex]);
		BoneEdit.PrevLocalOffsets.Add(PrevLocalOffset);
	}
	
	const int32 ParentIndex = TargetSkeleton.GetParentIndex(BoneEdit.Index);
	if (ParentIndex != INDEX_NONE)
	{
		BoneEdit.ParentGlobalTransform = Controller->GetTargetBoneGlobalTransform(RetargetProcessor, ParentIndex);
	}else
	{
		BoneEdit.ParentGlobalTransform = FTransform::Identity;
	}
}

bool FIKRetargetEditMode::IsRootSelected() const
{
	if (BoneEdit.SelectedBones.IsEmpty())
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	const FName& RootName = Controller->AssetController->GetTargetRootBone();
	for (const FName& SelectedBone : BoneEdit.SelectedBones)
	{
		if (RootName == SelectedBone)
		{
			return true;
		}
	}

	return false;
}

bool FIKRetargetEditMode::IsOnlyRootSelected() const
{
	if (BoneEdit.SelectedBones.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	return Controller->AssetController->GetTargetRootBone() == BoneEdit.SelectedBones[0];
}

void FIKRetargetEditMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
	
	CurrentWidgetMode = ViewportClient->GetWidgetMode();
}

void FIKRetargetEditMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

bool FIKRetargetEditMode::IsBoneSelected(const FName& BoneName) const
{
	return BoneEdit.SelectedBones.Contains(BoneName);
}

void FIKRetargetEditMode::HandleBoneSelectedInViewport(const FName& BoneName, bool bReplace)
{
	if (bReplace)
	{
		BoneEdit.SelectedBones.Reset();
		if (BoneName != NAME_None)
		{
			BoneEdit.SelectedBones.Add(BoneName);
		}
	}
	else
	{
		if (BoneName != NAME_None)
		{
			const bool bAlreadySelected = BoneEdit.SelectedBones.Contains(BoneName);
			if (bAlreadySelected)
			{
				BoneEdit.SelectedBones.Remove(BoneName);	
			}
			else
			{
				BoneEdit.SelectedBones.Add(BoneName);
			}
		}	
	}
}

#undef LOCTEXT_NAMESPACE
