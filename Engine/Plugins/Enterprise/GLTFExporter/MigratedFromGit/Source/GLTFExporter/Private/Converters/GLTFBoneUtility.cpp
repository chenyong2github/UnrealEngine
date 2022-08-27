// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFBoneUtility.h"
#include "Engine.h"

FTransform FGLTFBoneUtility::GetBindTransform(const FReferenceSkeleton& RefSkeleton, const int32 BoneIndex)
{
	const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRefBoneInfo();
	const TArray<FTransform>& BonePoses = RefSkeleton.GetRefBonePose();

	int32 CurBoneIndex = BoneIndex;
	FTransform BindTransform = FTransform::Identity;

	do
	{
		BindTransform = BindTransform * BonePoses[CurBoneIndex];
		CurBoneIndex = BoneInfos[CurBoneIndex].ParentIndex;
	} while (CurBoneIndex != INDEX_NONE);

	return BindTransform;
}
