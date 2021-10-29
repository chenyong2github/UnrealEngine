// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter/IKRetargeter.h"

const FName UIKRetargeter::DefaultPoseName = "Default Pose";
const FName UIKRetargeter::GetSourceIKRigPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, SourceIKRigAsset); };
const FName UIKRetargeter::GetTargetIKRigPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, TargetIKRigAsset); };
#if WITH_EDITOR
const FName UIKRetargeter::GetTargetPreviewMeshPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, TargetPreviewMesh); };
#endif
const FName UIKRetargeter::GetDefaultPoseName() { return DefaultPoseName; }

#if WITH_EDITOR
void UIKRetargeter::PostEditUndo()
{
	Super::PostEditUndo();
	IKRigEditUndo.Broadcast();
};
#endif

void FIKRetargetPose::AddRotationDeltaToBone(FName BoneName, FQuat RotationDelta)
{
	FQuat* RotOffset = BoneRotationOffsets.Find(BoneName);
	if (RotOffset == nullptr)
	{
		// first time this bone has been modified in this pose
		BoneRotationOffsets.Emplace(BoneName, RotationDelta);
		return;
	}

	// accumulate delta rotation
	*RotOffset = RotationDelta * (*RotOffset);
}

void FIKRetargetPose::AddTranslationDeltaToRoot(FVector TranslateDelta)
{
	RootTranslationOffset += TranslateDelta;
}
