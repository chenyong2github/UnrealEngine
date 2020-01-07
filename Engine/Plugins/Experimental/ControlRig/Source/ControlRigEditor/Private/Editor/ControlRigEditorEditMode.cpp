// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditorEditMode.h"
#include "IPersonaPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "ControlRigGizmoActor.h"

FName FControlRigEditorEditMode::ModeName("EditMode.ControlRigEditor");

bool FControlRigEditorEditMode::GetCameraTarget(FSphere& OutTarget) const
{
	FBox Box(ForceInit);

	for (int32 Index = 0; Index < SelectedRigElements.Num(); ++Index)
	{
		if (SelectedRigElements[Index].Type == ERigElementType::Bone || SelectedRigElements[Index].Type == ERigElementType::Space)
		{
			FTransform Transform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false);
			Box += Transform.GetLocation();
		}
		else if (SelectedRigElements[Index].Type == ERigElementType::Control)
		{
			FTransform Transform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false);
			Box += Transform.TransformPosition(FVector::OneVector * 50.f); // we assume 100cm as the base size for a control
			Box += Transform.TransformPosition(FVector::OneVector * -50.f);
		}
	}

	if(Box.IsValid)
	{
		OutTarget.Center = Box.GetCenter();
		OutTarget.W = Box.GetExtent().GetAbsMax() * 1.25f;
		return true;
	}

	return false;
}

IPersonaPreviewScene& FControlRigEditorEditMode::GetAnimPreviewScene() const
{
	return *static_cast<IPersonaPreviewScene*>(static_cast<FAssetEditorModeManager*>(Owner)->GetPreviewScene());
}

void FControlRigEditorEditMode::GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const
{

}