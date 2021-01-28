// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditorEditMode.h"
#include "IPersonaPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "ControlRigGizmoActor.h"
#include "SkeletalDebugRendering.h"

FName FControlRigEditorEditMode::ModeName("EditMode.ControlRigEditor");

void FControlRigEditorEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FControlRigEditMode::Render(View, Viewport, PDI);

	if (ConfigOption == nullptr)
	{
		ConfigOption = UPersonaOptions::StaticClass()->GetDefaultObject<UPersonaOptions>();
	}
	EBoneDrawMode::Type BoneDrawMode = (EBoneDrawMode::Type)ConfigOption->DefaultBoneDrawSelection;

	if (bDrawHierarchyBones && BoneDrawMode != EBoneDrawMode::None)
	{
		if (UControlRig* ControlRig = GetControlRig(false))
		{
			const FRigBoneHierarchy& Hierarchy = ControlRig->GetBoneHierarchy();
			const FRigBoneHierarchy* HierarchyForSelection = &Hierarchy;

			if (UClass* ControlRigClass = ControlRig->GetClass())
			{
				if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(ControlRigClass->ClassGeneratedBy))
				{
					HierarchyForSelection = &RigBlueprint->HierarchyContainer.BoneHierarchy;
				}
			}

			if (BoneDrawMode == EBoneDrawMode::SelectedAndParents)
			{
				if (BoneHasSelectedChild.Num() < Hierarchy.Num())
				{
					BoneHasSelectedChild.SetNumZeroed(Hierarchy.Num());
				}
				for (const FRigBone& Bone : Hierarchy)
				{
					BoneHasSelectedChild[Bone.Index] = false;
					if (!HierarchyForSelection->IsSelected(Bone.Name))
					{
						continue;
					}

					int32 ParentIndex = Bone.ParentIndex;
					while (ParentIndex != INDEX_NONE)
					{
						BoneHasSelectedChild[ParentIndex] = true;
						ParentIndex = Hierarchy[ParentIndex].ParentIndex;
					}
				}
			}
			
			for (const FRigBone& Bone : Hierarchy)
			{
				const int32 ParentIndex = Bone.ParentIndex;
				const bool bSelected = HierarchyForSelection->IsSelected(Bone.Name);

				if (!bSelected)
				{
					if (BoneDrawMode == EBoneDrawMode::Selected)
					{
						continue;
					}
					else if (BoneDrawMode == EBoneDrawMode::SelectedAndParents)
					{
						if (!BoneHasSelectedChild[Bone.Index])
						{
							continue;
						}
					}
				}

				const FLinearColor LineColor = bSelected ? FLinearColor(1.0f, 0.34f, 0.0f, 1.0f) : FLinearColor::White;

				FVector Start, End;
				if (ParentIndex >= 0)
				{
					Start = Hierarchy[ParentIndex].GlobalTransform.GetLocation();
					End = Bone.GlobalTransform.GetLocation();
				}
				else
				{
					Start = FVector::ZeroVector;
					End = Bone.GlobalTransform.GetLocation();
				}

				const float BoneLength = (End - Start).Size();
				const float Radius = FMath::Clamp(BoneLength * 0.05f, 0.1f, 10000.f);

				//Render Sphere for bone end point and a cone between it and its parent.
				SkeletalDebugRendering::DrawWireBone(PDI, Start, End, LineColor, SDPG_Foreground, Radius);
			}
		}
	}
}

bool FControlRigEditorEditMode::GetCameraTarget(FSphere& OutTarget) const
{
	FBox Box(ForceInit);

	for (int32 Index = 0; Index < SelectedRigElements.Num(); ++Index)
	{
		static const float Radius = 20.f;
		if (SelectedRigElements[Index].Type == ERigElementType::Bone || SelectedRigElements[Index].Type == ERigElementType::Space)
		{
			FTransform Transform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false, true);
			Box += Transform.TransformPosition(FVector::OneVector * Radius);
			Box += Transform.TransformPosition(FVector::OneVector * -Radius);
		}
		else if (SelectedRigElements[Index].Type == ERigElementType::Control)
		{
			FTransform Transform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false, true);
			Box += Transform.TransformPosition(FVector::OneVector * Radius);
			Box += Transform.TransformPosition(FVector::OneVector * -Radius);
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