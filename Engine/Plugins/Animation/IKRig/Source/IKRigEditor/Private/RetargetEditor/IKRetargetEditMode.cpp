// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditMode.h"

#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "EditorScriptingHelpers.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RetargetEditor/IKRetargetHitProxies.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "Retargeter/IKRetargetProcessor.h"

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

	if (!Controller->GetRetargetProcessor()->IsInitialized())
	{
		return;
	}

	const UIKRetargeter* Retargeter = Controller->AssetController->GetAsset();
	const UIKRetargetProcessor* RetargetProcessor = Controller->GetRetargetProcessor();
	const FTargetSkeleton& TargetSkeleton = RetargetProcessor->GetTargetSkeleton();

	for (int32 BoneIndex = 0; BoneIndex<TargetSkeleton.BoneNames.Num(); ++BoneIndex)
	{		
		// filter out bones that are not part of a retargeted chain
		if (!TargetSkeleton.IsBoneInAnyTargetChain[BoneIndex])
		{
			continue;
		}
		
		const FName& BoneName = TargetSkeleton.BoneNames[BoneIndex];

		// is this chain currently selected?
		const bool bIsSelected = IsBoneSelected(BoneName);

		// get the location of the bone on the currently initialized target skeletal mesh
		const FTransform BoneTransform = Controller->GetTargetBoneTransform(BoneIndex);

		FVector Start;
		TArray<FVector> End;
		if (Controller->GetTargetBoneLineSegments(BoneIndex, Start, End))
		{
			// draw the chain rotation gizmo
			PDI->SetHitProxy(new HIKRetargetEditorBoneProxy(BoneName));
			DrawBoneProxy(
				PDI,
				BoneTransform,
				Start,
				End,
				Retargeter->BoneDrawSize,
				Retargeter->BoneDrawThickness,
				bIsSelected);
			PDI->SetHitProxy(NULL);
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
	const bool bIsAnyBoneSelected = !SelectedBones.IsEmpty();

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
	if (SelectedBones.IsEmpty())
	{
		return FVector::ZeroVector; 
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FVector::ZeroVector; 
	}

	const FTargetSkeleton& TargetSkeleton = Controller->GetRetargetProcessor()->GetTargetSkeleton();
	const int32 BoneIndex = TargetSkeleton.FindBoneIndexByName(SelectedBones.Last());
	if (BoneIndex == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	
	return Controller->GetTargetBoneTransform(BoneIndex).GetTranslation();
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
		}
	}
	
	return false;
}

bool FIKRetargetEditMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	TrackingState = None;
	
	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	if (CurrentAxis == EAxisList::None)
	{
		return false; // not manipulating a required axis
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; // invalid editor state
	}

	// get state of viewport
	const bool bTranslating = InViewportClient->GetWidgetMode() == UE::Widget::EWidgetMode::WM_Translate;
	const bool bRotating = InViewportClient->GetWidgetMode() == UE::Widget::EWidgetMode::WM_Rotate;
	const bool bAnyBoneSelected = !SelectedBones.IsEmpty();
	const bool bOnlyRootSelected = IsOnlyRootSelected();

	// is any bone being rotated?
	if (bRotating && bAnyBoneSelected)
	{
		// Start a rotation transaction
		GEditor->BeginTransaction(LOCTEXT("RotateRetargetPoseBone", "Rotate Retarget Pose Bone"));
		Controller->AssetController->GetAsset()->Modify();
		TrackingState = FIKRetargetTrackingState::RotatingBone;
		return true;
	}

	// is the root being translated?
	if (bTranslating && bOnlyRootSelected)
	{
		// Start a translation transaction
		GEditor->BeginTransaction(LOCTEXT("TranslateRetargetPoseBone", "Translate Retarget Pose Bone"));
		Controller->AssetController->GetAsset()->Modify();
		TrackingState = FIKRetargetTrackingState::TranslatingRoot;
		return true;
	}

	return false;
}

bool FIKRetargetEditMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{	
	if (TrackingState == FIKRetargetTrackingState::None)
	{
		return false; // not handled
	}

	GEditor->EndTransaction();
	TrackingState = FIKRetargetTrackingState::None;
	return true;
}

bool FIKRetargetEditMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
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

		// apply rotation delta to all selected bones
		for (const FName& BoneName : SelectedBones)
		{
			Controller->AssetController->AddRotationOffsetToRetargetPoseBone(BoneName, InRot.Quaternion());
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
	if (SelectedBones.IsEmpty())
	{
		return false; // nothing selected to manipulate
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	const int32 BoneIndex = Controller->AssetController->GetAsset()->GetTargetIKRig()->Skeleton.GetBoneIndexFromName(SelectedBones[0]);
	const FTransform CurrentTransform = Controller->GetTargetBoneTransform(BoneIndex);
	InMatrix = CurrentTransform.ToMatrixNoScale().RemoveTranslation();
	return true;
}

bool FIKRetargetEditMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

bool FIKRetargetEditMode::IsOnlyRootSelected() const
{
	if (SelectedBones.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}

	return Controller->AssetController->GetTargetRootBone() == SelectedBones[0];
}

void FIKRetargetEditMode::DrawBoneProxy(
	FPrimitiveDrawInterface* PDI,
	const FTransform& BoneTransform,
	const FVector& Start,
	const TArray<FVector>& ChildPoints,
	float Size,
	float Thickness,
	bool bIsSelected) const
{
	const FLinearColor Color = bIsSelected ? FLinearColor::Green : FLinearColor::Yellow;
	Size = FMath::Clamp(Size, 0.1f, 1000.0f);
	Thickness = FMath::Clamp(Thickness, 0.1f, 1000.0f);

	// draw a line to each child
	for (const FVector& ChildPosition : ChildPoints)
	{
		PDI->DrawLine(Start, ChildPosition, Color, SDPG_Foreground, Thickness * 1.0f);
	}
	
	// draw a coordinate frame at the bone
	DrawCoordinateSystem(PDI, Start, BoneTransform.GetRotation().Rotator(), Size, SDPG_Foreground, Thickness);
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
	return SelectedBones.Contains(BoneName);
}

void FIKRetargetEditMode::HandleBoneSelectedInViewport(const FName& BoneName, bool bReplace)
{
	if (bReplace)
	{
		SelectedBones.Reset();
		if (BoneName != NAME_None)
		{
			SelectedBones.Add(BoneName);
		}
	}
	else
	{
		if (BoneName != NAME_None)
		{
			const bool bAlreadySelected = SelectedBones.Contains(BoneName);
			if (bAlreadySelected)
			{
				SelectedBones.Remove(BoneName);	
			}
			else
			{
				SelectedBones.Add(BoneName);
			}
		}	
	}
}

#undef LOCTEXT_NAMESPACE
