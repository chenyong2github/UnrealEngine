// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFBoneUtility.h"
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 26)
#include "Animation/AnimationPoseData.h"
#endif

FTransform FGLTFBoneUtility::GetBindTransform(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex)
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

void FGLTFBoneUtility::GetFrameTimestamps(const UAnimSequence* AnimSequence, TArray<float>& OutFrameTimestamps)
{
	const int32 FrameCount = AnimSequence->GetRawNumberOfFrames();
	OutFrameTimestamps.AddUninitialized(FrameCount);

	const float SequenceLength = AnimSequence->SequenceLength;
	const float FrameLength = FrameCount > 1 ? SequenceLength / (FrameCount - 1) : 0;

	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		OutFrameTimestamps[FrameIndex] = FMath::Clamp(FrameIndex * FrameLength, 0.0f, SequenceLength);
	}
}

void FGLTFBoneUtility::GetBoneIndices(const USkeleton* Skeleton, TArray<FBoneIndexType>& OutBoneIndices)
{
	const int32 BoneCount = Skeleton->GetReferenceSkeleton().GetNum();
	OutBoneIndices.AddUninitialized(BoneCount);

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		OutBoneIndices[BoneIndex] = BoneIndex;
	}
}

void FGLTFBoneUtility::GetBoneTransformsByFrame(const UAnimSequence* AnimSequence, const TArray<float>& FrameTimestamps, const TArray<FBoneIndexType>& BoneIndices, TArray<TArray<FTransform>>& OutBoneTransformsByFrame)
{
	FMemMark Mark(FMemStack::Get()); // Make sure to free stack allocations made by FCompactPose, FBlendedCurve, and FStackCustomAttributes when end of scope

	FBoneContainer BoneContainer;
	BoneContainer.SetUseRAWData(true);
	BoneContainer.InitializeTo(BoneIndices, FCurveEvaluationOption(true), *AnimSequence->GetSkeleton());

	const int32 FrameCount = FrameTimestamps.Num();
	OutBoneTransformsByFrame.AddDefaulted(FrameCount);

	FCompactPose Pose;
	Pose.SetBoneContainer(&BoneContainer);

	FBlendedCurve Curve;
	Curve.InitFrom(BoneContainer);

#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 26)
	FStackCustomAttributes Attributes;
	FAnimationPoseData PoseData(Pose, Curve, Attributes);
#endif

	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		const FAnimExtractContext ExtractionContext(FrameTimestamps[Frame]); // TODO: set bExtractRootMotion?
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 26)
		AnimSequence->GetBonePose(PoseData, ExtractionContext);
#else
		AnimSequence->GetBonePose(Pose, Curve, ExtractionContext);
#endif
		Pose.CopyBonesTo(OutBoneTransformsByFrame[Frame]);
	}

	// Clear all stack allocations to allow FMemMark to free them
	Pose.Empty();
	Curve.Empty();

#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 26)
	Attributes.Empty();
#endif
}
