// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter/IKRetargeter.h"

const FName UIKRetargeter::DefaultPoseName = "Default Pose";


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
