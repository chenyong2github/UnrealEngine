// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigSkeletalMeshComponent.h"
#include "Sequencer/ControlRigSequencerAnimInstance.h"
#include "SkeletalDebugRendering.h"
#include "ControlRig.h"

UControlRigSkeletalMeshComponent::UControlRigSkeletalMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DebugDrawSkeleton(false)
{
	SetDisablePostProcessBlueprint(true);
}

void UControlRigSkeletalMeshComponent::InitAnim(bool bForceReinit)
{
	// skip preview init entirely, just init the super class
	USkeletalMeshComponent::InitAnim(bForceReinit);

	RebuildDebugDrawSkeleton();
}


void UControlRigSkeletalMeshComponent::SetCustomDefaultPose()
{
	ShowReferencePose(false);
}

void UControlRigSkeletalMeshComponent::RebuildDebugDrawSkeleton()
{
	UControlRigSequencerAnimInstance* ControlRigInstance = Cast<UControlRigSequencerAnimInstance>(GetAnimInstance());

	if (ControlRigInstance && ControlRigInstance->CachedControlRig.IsValid())
	{
		UControlRig* ControlRig = ControlRigInstance->CachedControlRig.Get();
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
			RefSkelModifier.Add(NewMeshBoneInfo, BaseHiearchy.GetInitialTransform(Index));

			DebugDrawBones.Add(Index);
		}
	}
}

FTransform UControlRigSkeletalMeshComponent::GetDrawTransform(int32 BoneIndex) const
{
	UControlRigSequencerAnimInstance* ControlRigInstance = Cast<UControlRigSequencerAnimInstance>(GetAnimInstance());

	if (ControlRigInstance && ControlRigInstance->CachedControlRig.IsValid())
	{
		UControlRig* ControlRig = ControlRigInstance->CachedControlRig.Get();
		// just copy it because this is not thread safe
		const FRigBoneHierarchy& BaseHiearchy = ControlRig->GetBoneHierarchy();
		return BaseHiearchy.GetGlobalTransform(BoneIndex);
	}

	return FTransform::Identity;
}

void UControlRigSkeletalMeshComponent::EnablePreview(bool bEnable, UAnimationAsset* PreviewAsset)
{
	UControlRigSequencerAnimInstance* ControlRigInstance = Cast<UControlRigSequencerAnimInstance>(GetAnimInstance());

	if (ControlRigInstance && ControlRigInstance->CachedControlRig.IsValid())
	{
		ControlRigInstance->SetAnimationAsset(PreviewAsset);
	}
}
