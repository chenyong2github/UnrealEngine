// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneIndices.h"

class UAnimSequence;
class USkeletalMesh;
class USkeleton;
struct FReferenceSkeleton;

struct FGLTFBoneUtilities
{
	static const FReferenceSkeleton& GetReferenceSkeleton(const USkeletalMesh* SkeletalMesh);

	static FTransform GetBindTransform(const FReferenceSkeleton& ReferenceSkeleton, int32 BoneIndex);

	static void GetFrameTimestamps(const UAnimSequence* AnimSequence, TArray<float>& OutFrameTimestamps);

	static void GetBoneIndices(const USkeleton* Skeleton, TArray<FBoneIndexType>& OutBoneIndices);

	static void GetBoneTransformsByFrame(const UAnimSequence* AnimSequence, const TArray<float>& FrameTimestamps, const TArray<FBoneIndexType>& BoneIndices, TArray<TArray<FTransform>>& OutBoneTransformsByFrame);
};
