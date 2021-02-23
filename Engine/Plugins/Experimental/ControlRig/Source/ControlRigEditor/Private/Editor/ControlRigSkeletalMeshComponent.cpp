// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigSkeletalMeshComponent.h"
#include "Sequencer/ControlRigLayerInstance.h" 
#include "SkeletalDebugRendering.h"
#include "ControlRig.h"
#include "AnimPreviewInstance.h"

UControlRigSkeletalMeshComponent::UControlRigSkeletalMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DebugDrawSkeleton(false)
{
	SetDisablePostProcessBlueprint(true);
}

void UControlRigSkeletalMeshComponent::InitAnim(bool bForceReinit)
{
	// skip preview init entirely, just init the super class
	Super::InitAnim(bForceReinit);

	UControlRigLayerInstance* ControlRigInstance = Cast<UControlRigLayerInstance>(GetAnimInstance());
	if (ControlRigInstance)
	{
		ControlRigInstance->SetSourceAnimInstance(PreviewInstance);
	}

	RebuildDebugDrawSkeleton();
}

bool UControlRigSkeletalMeshComponent::IsPreviewOn() const
{
	return (PreviewInstance != nullptr);
}

void UControlRigSkeletalMeshComponent::SetCustomDefaultPose()
{
	ShowReferencePose(false);
}

void UControlRigSkeletalMeshComponent::RebuildDebugDrawSkeleton()
{
	UControlRigLayerInstance* ControlRigInstance = Cast<UControlRigLayerInstance>(GetAnimInstance());

	if (ControlRigInstance)
	{
		UControlRig* ControlRig = ControlRigInstance->GetFirstAvailableControlRig();
		if (ControlRig)
		{
			// just copy it because this is not thread safe
			URigHierarchy* Hierarchy = ControlRig->GetHierarchy();

			DebugDrawSkeleton.Empty();
			DebugDrawBones.Reset();
			DebugDrawBoneIndexInHierarchy.Reset();

			// create ref modifier
			FReferenceSkeletonModifier RefSkelModifier(DebugDrawSkeleton, nullptr);

			TMap<FName, int32> AddedBoneMap;
			TArray<FRigBoneElement*> BoneElements = Hierarchy->GetBones(true);
			for(FRigBoneElement* BoneElement : BoneElements)
			{
				const int32 Index = BoneElement->GetIndex();
				const FName ParentName = Hierarchy->GetFirstParent(BoneElement->GetKey()).Name;
				int32 ParentIndex = INDEX_NONE; 
				if(!ParentName.IsNone())
				{
					ParentIndex = AddedBoneMap.FindChecked(ParentName);
				}
					
				FMeshBoneInfo NewMeshBoneInfo;
				NewMeshBoneInfo.Name = BoneElement->GetName();
				NewMeshBoneInfo.ParentIndex = ParentIndex; 
				// give ref pose here
				RefSkelModifier.Add(NewMeshBoneInfo, Hierarchy->GetInitialGlobalTransform(Index));

				AddedBoneMap.FindOrAdd(BoneElement->GetName(), DebugDrawBones.Num());
				DebugDrawBones.Add(DebugDrawBones.Num());
				DebugDrawBoneIndexInHierarchy.Add(BoneElement->GetIndex());
			}
		}
	}
}

FTransform UControlRigSkeletalMeshComponent::GetDrawTransform(int32 BoneIndex) const
{
	UControlRigLayerInstance* ControlRigInstance = Cast<UControlRigLayerInstance>(GetAnimInstance());

	if (ControlRigInstance)
	{
		UControlRig* ControlRig = ControlRigInstance->GetFirstAvailableControlRig();
		if (ControlRig && DebugDrawBoneIndexInHierarchy.IsValidIndex(BoneIndex))
		{
			// just copy it because this is not thread safe
			URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
			return Hierarchy->GetGlobalTransform(DebugDrawBoneIndexInHierarchy[BoneIndex]);
		}
	}

	return FTransform::Identity;
}


void UControlRigSkeletalMeshComponent::EnablePreview(bool bEnable, UAnimationAsset* PreviewAsset)
{
	if (PreviewInstance)
	{
		PreviewInstance->SetAnimationAsset(PreviewAsset);
	}
}
