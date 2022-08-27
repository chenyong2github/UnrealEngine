// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLTFBoneUtility
{
	static FTransform GetBindTransform(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex);

	static void InitializeToSkeleton(FBoneContainer& BoneContainer, const USkeleton* Skeleton);
};
