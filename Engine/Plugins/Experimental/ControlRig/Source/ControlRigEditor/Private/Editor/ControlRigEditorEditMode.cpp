// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditorEditMode.h"
#include "IPersonaPreviewScene.h"
#include "AssetEditorModeManager.h"

FName FControlRigEditorEditMode::ModeName("EditMode.ControlRigEditor");

bool FControlRigEditorEditMode::GetCameraTarget(FSphere& OutTarget) const
{
	FBox Box(ForceInit);

	for(const FControlUnitProxy& UnitProxy : ControlUnits)
	{
		if(UnitProxy.IsSelected() && UnitProxy.Control)
		{
			FBox ActorBox = UnitProxy.Control->GetComponentsBoundingBox(true);
			Box += ActorBox;
		}
	}

	if (AreBoneSelected())
	{
		for (int32 Index = 0; Index < SelectedBones.Num(); ++Index)
		{
			FTransform Transform = OnGetBoneTransformDelegate.Execute(SelectedBones[Index], false);
			Box += Transform.GetLocation();
		}
	}

	if(Box.IsValid)
	{
		OutTarget.Center = Box.GetCenter();
		OutTarget.W = Box.GetExtent().GetAbsMax();
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