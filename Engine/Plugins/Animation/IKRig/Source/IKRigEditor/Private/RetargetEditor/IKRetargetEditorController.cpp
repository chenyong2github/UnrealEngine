// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditorController.h"

#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/SIKRetargetChainMapList.h"
#include "Retargeter/IKRetargeter.h"

#define LOCTEXT_NAMESPACE "IKRetargetEditorController"

USkeletalMesh* FIKRetargetEditorController::GetSourceSkeletalMesh() const
{
	if (!(Asset && Asset->SourceIKRigAsset))
	{
		return nullptr;
	}

	return Asset->SourceIKRigAsset->PreviewSkeletalMesh;
}

USkeletalMesh* FIKRetargetEditorController::GetTargetSkeletalMesh() const
{
	if (!(Asset && Asset->TargetIKRigAsset))
	{
		return nullptr;
	}

	return Asset->TargetIKRigAsset->PreviewSkeletalMesh;
}

FTransform FIKRetargetEditorController::GetTargetBoneTransform(const FName& BoneName) const
{
	UIKRetargetAnimInstance* AnimInstance = TargetAnimInstance.Get();
	check(AnimInstance);

	UIKRetargeter* CurrentRetargeter = AnimInstance->GetCurrentlyUsedRetargeter();
	check(CurrentRetargeter)

	// get transform of root of chain
	FTransform BoneTransform = CurrentRetargeter->GetTargetBoneRetargetPoseGlobalTransform(BoneName);

	BoneTransform.ScaleTranslation(Asset->TargetActorScale);

	// add the target translation offset
	const FVector TargetOffset(Asset->TargetActorOffset, 0.f, 0.f);
	BoneTransform.AddToTranslation(TargetOffset);

	return BoneTransform;
}

void FIKRetargetEditorController::GetTargetBoneStartAndEnd(const FName& BoneName, FVector& Start, FVector& End) const
{
	UIKRetargeter* CurrentRetargeter = GetCurrentlyRunningRetargeter();
	check(CurrentRetargeter)

	// get line of chain
	CurrentRetargeter->GetTargetBoneStartAndEnd(BoneName, Start, End);

	// add the target translation offset
	const FVector TargetOffset(Asset->TargetActorOffset, 0.f, 0.f);

	Start *= Asset->TargetActorScale;
	End *= Asset->TargetActorScale;
	
	Start += TargetOffset;
	End += TargetOffset;
}

UIKRetargeter* FIKRetargetEditorController::GetCurrentlyRunningRetargeter() const
{
	UIKRetargetAnimInstance* AnimInstance = TargetAnimInstance.Get();
	if(!AnimInstance)
	{
		return nullptr;
	}

	UIKRetargeter* CurrentRetargeter = AnimInstance->GetCurrentlyUsedRetargeter();
	if (!CurrentRetargeter)
	{
		return nullptr;
	}

	return CurrentRetargeter;
}

void FIKRetargetEditorController::RefreshAllViews()
{
	ChainsView.Get()->RefreshView();
}

#undef LOCTEXT_NAMESPACE
