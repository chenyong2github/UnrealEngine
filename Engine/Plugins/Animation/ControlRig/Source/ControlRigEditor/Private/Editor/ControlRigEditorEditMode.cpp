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
			URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
			URigHierarchy* HierarchyForSelection = Hierarchy;

			if (UClass* ControlRigClass = ControlRig->GetClass())
			{
				if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(ControlRigClass->ClassGeneratedBy))
				{
					HierarchyForSelection = RigBlueprint->Hierarchy;
				}
			}

			if (BoneDrawMode == EBoneDrawMode::SelectedAndParents)
			{
				if (BoneHasSelectedChild.Num() != Hierarchy->Num())
				{
					BoneHasSelectedChild.SetNumZeroed(Hierarchy->Num());
				}

				Hierarchy->ForEach<FRigBoneElement>([this, Hierarchy, HierarchyForSelection](FRigBoneElement* BoneElement) -> bool
				{
					BoneHasSelectedChild[BoneElement->GetIndex()] = false;
					if (!HierarchyForSelection->IsSelected(BoneElement->GetKey()))
					{
						return true;
					}

					int32 ParentIndex = Hierarchy->GetFirstParent(BoneElement->GetIndex());
					while (ParentIndex != INDEX_NONE)
					{
						BoneHasSelectedChild[ParentIndex] = true;
						ParentIndex = Hierarchy->GetFirstParent(ParentIndex);
					}

					return true;
				});
			}
			
			Hierarchy->ForEach<FRigBoneElement>([this, PDI, Hierarchy, HierarchyForSelection, &BoneDrawMode](FRigBoneElement* BoneElement) -> bool
			{
				const int32 ParentIndex = Hierarchy->GetFirstParent(BoneElement->GetIndex());
				const bool bSelected = HierarchyForSelection->IsSelected(BoneElement->GetKey());

				if (!bSelected)
				{
					if (BoneDrawMode == EBoneDrawMode::Selected)
					{
						return true;
					}
					else if (BoneDrawMode == EBoneDrawMode::SelectedAndParents)
					{
						if (!BoneHasSelectedChild[BoneElement->GetIndex()])
						{
							return true;
						}
					}
				}

				const FLinearColor LineColor = bSelected ? FLinearColor(1.0f, 0.34f, 0.0f, 1.0f) : FLinearColor::White;

				FVector Start, End;
				if (ParentIndex != INDEX_NONE)
				{
					Start = Hierarchy->GetGlobalTransform(ParentIndex).GetLocation();
					End = Hierarchy->GetGlobalTransform(BoneElement->GetIndex()).GetLocation();
				}
				else
				{
					Start = FVector::ZeroVector;
					End = Hierarchy->GetGlobalTransform(BoneElement->GetIndex()).GetLocation();
				}

				const float BoneLength = (End - Start).Size();
				const float Radius = FMath::Clamp(BoneLength * 0.05f, 0.1f, 10000.f);

				//Render Sphere for bone end point and a cone between it and its parent.
				PDI->SetHitProxy(new HPersonaBoneHitProxy(BoneElement->GetIndex(), BoneElement->GetName()));
				SkeletalDebugRendering::DrawWireBone(PDI, Start, End, LineColor, SDPG_Foreground, Radius);
				PDI->SetHitProxy(nullptr);
				return true;
			});
		}
	}
}

bool FControlRigEditorEditMode::GetCameraTarget(FSphere& OutTarget) const
{
	FTransform ComponentToWorld = FTransform::Identity;
	if(const USceneComponent* SceneComponent = GetHostingSceneComponent())
	{
		ComponentToWorld = SceneComponent->GetComponentToWorld();
	}

	FBox Box(ForceInit);
	TArray<FRigElementKey> SelectedRigElements = GetSelectedRigElements();
	for (int32 Index = 0; Index < SelectedRigElements.Num(); ++Index)
	{
		static const float Radius = 20.f;
		if (SelectedRigElements[Index].Type == ERigElementType::Bone || SelectedRigElements[Index].Type == ERigElementType::Null)
		{
			FTransform Transform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false, true);
			Transform = Transform * ComponentToWorld;
			Box += Transform.TransformPosition(FVector::OneVector * Radius);
			Box += Transform.TransformPosition(FVector::OneVector * -Radius);
		}
		else if (SelectedRigElements[Index].Type == ERigElementType::Control)
		{
			FTransform Transform = OnGetRigElementTransformDelegate.Execute(SelectedRigElements[Index], false, true);
			Transform = Transform * ComponentToWorld;
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