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
			const FRigBoneHierarchy& BaseHiearchy = ControlRig->GetBoneHierarchy();

			DebugDrawSkeleton.Empty();
			DebugDrawBones.Reset();

			// create ref modifier
			FReferenceSkeletonModifier RefSkelModifier(DebugDrawSkeleton, nullptr);

			for (int32 Index = 0; Index < BaseHiearchy.Num(); Index++)
			{
				FMeshBoneInfo NewMeshBoneInfo;
				NewMeshBoneInfo.Name = BaseHiearchy.GetName(Index);
				NewMeshBoneInfo.ParentIndex = BaseHiearchy[Index].ParentIndex;
				// give ref pose here
				RefSkelModifier.Add(NewMeshBoneInfo, BaseHiearchy.GetInitialGlobalTransform(Index));

				DebugDrawBones.Add(Index);
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
		if (ControlRig)
		{
			// just copy it because this is not thread safe
			const FRigBoneHierarchy& BaseHiearchy = ControlRig->GetBoneHierarchy();
			return BaseHiearchy.GetGlobalTransform(BoneIndex);
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
