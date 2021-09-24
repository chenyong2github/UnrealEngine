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

FTransform FIKRetargetEditorController::GetTargetBoneTransform(const int32& TargetBoneIndex) const
{
	UIKRetargetAnimInstance* AnimInstance = TargetAnimInstance.Get();
	check(AnimInstance);

	UIKRetargeter* CurrentRetargeter = AnimInstance->GetCurrentlyUsedRetargeter();
	check(CurrentRetargeter)

	// get transform of root of chain
	FTransform BoneTransform = CurrentRetargeter->GetTargetBoneRetargetPoseGlobalTransform(TargetBoneIndex);

	// scale and offset
	BoneTransform.ScaleTranslation(Asset->TargetActorScale);
	BoneTransform.AddToTranslation(FVector(Asset->TargetActorOffset, 0.f, 0.f));

	return BoneTransform;
}

bool FIKRetargetEditorController::GetTargetBoneLineSegments(
	const int32& TargetBoneIndex,
	FVector& OutStart,
	TArray<FVector>& OutChildren) const
{
	UIKRetargeter* CurrentRetargeter = GetCurrentlyRunningRetargeter();
	check(CurrentRetargeter && CurrentRetargeter->bIsLoadedAndValid)
	check(CurrentRetargeter->TargetSkeleton.BoneNames.IsValidIndex(TargetBoneIndex))
	
	// get the target skeleton we want to draw
	const FTargetSkeleton& TargetSkeleton = CurrentRetargeter->TargetSkeleton;

	// get the origin of the bone chain
	OutStart = TargetSkeleton.RetargetGlobalPose[TargetBoneIndex].GetTranslation();

	// get children
	TArray<int32> ChildIndices;
	TargetSkeleton.GetChildrenIndices(TargetBoneIndex, ChildIndices);
	for (const int32& ChildIndex : ChildIndices)
	{
		OutChildren.Emplace(TargetSkeleton.RetargetGlobalPose[ChildIndex].GetTranslation());
	}

	// add the target translation offset and scale
	const FVector TargetOffset(Asset->TargetActorOffset, 0.f, 0.f);
	OutStart *= Asset->TargetActorScale;
	OutStart += TargetOffset;
	for (FVector& ChildPoint : OutChildren)
	{
		ChildPoint *= Asset->TargetActorScale;
		ChildPoint += TargetOffset;
	}
	
	return true;
}

bool FIKRetargetEditorController::IsTargetBoneRetargeted(const int32& TargetBoneIndex)
{
	UIKRetargeter* CurrentRetargeter = GetCurrentlyRunningRetargeter();
	check(CurrentRetargeter && CurrentRetargeter->bIsLoadedAndValid)
	check(CurrentRetargeter->TargetSkeleton.BoneNames.IsValidIndex(TargetBoneIndex))

	return CurrentRetargeter->TargetSkeleton.IsBoneRetargeted[TargetBoneIndex];
}

UIKRetargeter* FIKRetargetEditorController::GetCurrentlyRunningRetargeter() const
{	
	if(UIKRetargetAnimInstance* AnimInstance = TargetAnimInstance.Get())
	{
		return AnimInstance->GetCurrentlyUsedRetargeter();
	}

	return nullptr;	
}

void FIKRetargetEditorController::RefreshAllViews()
{
	ChainsView.Get()->RefreshView();
}

void FIKRetargetEditorController::PlayAnimationAsset(UAnimationAsset* AssetToPlay)
{
	if (AssetToPlay && SourceAnimInstance)
	{
		SourceAnimInstance->SetAnimationAsset(AssetToPlay);
		PreviousAsset = AssetToPlay;
	}
}

void FIKRetargetEditorController::PlayPreviousAnimationAsset()
{
	if (PreviousAsset)
	{
		SourceAnimInstance->SetAnimationAsset(PreviousAsset);
	}
}

#undef LOCTEXT_NAMESPACE
