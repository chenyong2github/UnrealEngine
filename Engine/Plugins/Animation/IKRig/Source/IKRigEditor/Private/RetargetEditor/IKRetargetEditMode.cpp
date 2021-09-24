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

FName FIKRetargetEditMode::ModeName("IKRetargetAssetEditMode");

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
	
	UIKRetargeter* Retargeter = Controller->Asset;
	
	for (int32 BoneIndex = 0; BoneIndex<Retargeter->TargetIKRigAsset->Skeleton.BoneNames.Num(); ++BoneIndex)
	{
		// filter out bones that are not part of a retargeted chain
		if (!Controller->IsTargetBoneRetargeted(BoneIndex))
		{
			continue;
		}
		
		const FName BoneName = Retargeter->TargetIKRigAsset->Skeleton.BoneNames[BoneIndex];

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
	return !SelectedBones.IsEmpty();
}

bool FIKRetargetEditMode::ShouldDrawWidget() const
{
	return !SelectedBones.IsEmpty();
}

bool FIKRetargetEditMode::UsesTransformWidget() const
{
	return !SelectedBones.IsEmpty();
}

bool FIKRetargetEditMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	return !SelectedBones.IsEmpty() && CheckMode == UE::Widget::EWidgetMode::WM_Rotate;
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

	const int32 BoneIndex = Controller->GetCurrentlyRunningRetargeter()->TargetSkeleton.FindBoneIndexByName(SelectedBones.Last());
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
		// selected goal
		if (HitProxy && HitProxy->IsA(HIKRetargetEditorBoneProxy::StaticGetType()))
		{
			HIKRetargetEditorBoneProxy* BoneProxy = static_cast<HIKRetargetEditorBoneProxy*>(HitProxy);
			const bool bReplaceSelection = !(InViewportClient->IsCtrlPressed() || InViewportClient->IsShiftPressed());
			HandleBoneSelectedInViewport(BoneProxy->BoneName, bReplaceSelection);
			return true;
		}
		else
		{
			// clicking in empty space clears selected goals
			const bool bReplaceSelection = true;
			HandleBoneSelectedInViewport(NAME_None, bReplaceSelection);
		}
	}
	
	return false;
}

bool FIKRetargetEditMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (SelectedBones.IsEmpty())
	{
		return false; // no goals selected to manipulate
	}

	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	if (CurrentAxis == EAxisList::None)
	{
		return false; // not manipulating a required axis
	}
	
	bRotatingBones = true;
	return true;
}

bool FIKRetargetEditMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (!bRotatingBones)
	{
		return false; // not handled
	}

	bRotatingBones = false;
	return true;
}

bool FIKRetargetEditMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (!bRotatingBones)
	{
		return false; // not handled
	}
	
	if(InViewportClient->GetWidgetMode() != UE::Widget::WM_Rotate)
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	// rotate chains
	for (const FName& BoneName : SelectedBones)
	{
		Controller->Asset->AddRotationOffsetToRetargetPoseBone(BoneName, InRot.Quaternion());
	}
	
	return true;
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

	const int32 BoneIndex = Controller->Asset->TargetIKRigAsset->Skeleton.GetBoneIndexFromName(SelectedBones[0]);
	const FTransform CurrentTransform = Controller->GetTargetBoneTransform(BoneIndex);
	InMatrix = CurrentTransform.ToMatrixNoScale().RemoveTranslation();
	return true;
}

bool FIKRetargetEditMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
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
