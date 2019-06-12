// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BonePose.h"
#include "AnimationRuntime.h"
#include "AnimEncoding.h"
#include "HAL/ThreadSingleton.h"

void FMeshPose::ResetToRefPose()
{
	FAnimationRuntime::FillWithRefPose(Bones, *BoneContainer);
}

void FMeshPose::ResetToIdentity()
{
	FAnimationRuntime::InitializeTransform(*BoneContainer, Bones);
}


bool FMeshPose::ContainsNaN() const
{
	const TArray<FBoneIndexType> & RequiredBoneIndices = BoneContainer->GetBoneIndicesArray();
	for (int32 Iter = 0; Iter < RequiredBoneIndices.Num(); ++Iter)
	{
		const int32 BoneIndex = RequiredBoneIndices[Iter];
		if (Bones[BoneIndex].ContainsNaN())
		{
			return true;
		}
	}

	return false;
}

bool FMeshPose::IsNormalized() const
{
	const TArray<FBoneIndexType> & RequiredBoneIndices = BoneContainer->GetBoneIndicesArray();
	for (int32 Iter = 0; Iter < RequiredBoneIndices.Num(); ++Iter)
	{
		int32 BoneIndex = RequiredBoneIndices[Iter];
		const FTransform& Trans = Bones[BoneIndex];
		if (!Bones[BoneIndex].IsRotationNormalized())
		{
			return false;
		}
	}

	return true;
}

struct FRetargetTracking
{
	const FCompactPoseBoneIndex PoseBoneIndex;
	const int32 SkeletonBoneIndex;

	FRetargetTracking(const FCompactPoseBoneIndex InPoseBoneIndex, const int32 InSkeletonBoneIndex)
		: PoseBoneIndex(InPoseBoneIndex), SkeletonBoneIndex(InSkeletonBoneIndex)
	{
	}
};

FTransform ExtractTransformForKey(int32 Key, const FRawAnimSequenceTrack &TrackToExtract)
{
	static const FVector DefaultScale3D = FVector(1.f);
	const bool bHasScaleKey = TrackToExtract.ScaleKeys.Num() > 0;

	const int32 PosKeyIndex = FMath::Min(Key, TrackToExtract.PosKeys.Num() - 1);
	const int32 RotKeyIndex = FMath::Min(Key, TrackToExtract.RotKeys.Num() - 1);
	if (bHasScaleKey)
	{
		const int32 ScaleKeyIndex = FMath::Min(Key, TrackToExtract.ScaleKeys.Num() - 1);
		return FTransform(TrackToExtract.RotKeys[RotKeyIndex], TrackToExtract.PosKeys[PosKeyIndex], TrackToExtract.ScaleKeys[ScaleKeyIndex]);
	}
	else
	{
		return FTransform(TrackToExtract.RotKeys[RotKeyIndex], TrackToExtract.PosKeys[PosKeyIndex], DefaultScale3D);
	}
}

struct FBuildRawPoseScratchArea : public TThreadSingleton<FBuildRawPoseScratchArea>
{
	TArray<FRetargetTracking> RetargetTracking;
	TArray<FVirtualBoneCompactPoseData> VirtualBoneCompactPoseData;
};


template<bool bInterpolateT>
void BuildPoseFromRawDataInternal(const TArray<FRawAnimSequenceTrack>& InAnimationData, const TArray<struct FTrackToSkeletonMap>& TrackToSkeletonMapTable, FCompactPose& InOutPose, int32 KeyIndex1, int32 KeyIndex2, float Alpha)
{
	const int32 NumTracks = InAnimationData.Num();
	const FBoneContainer& RequiredBones = InOutPose.GetBoneContainer();

	TArray<FRetargetTracking>& RetargetTracking = FBuildRawPoseScratchArea::Get().RetargetTracking;
	RetargetTracking.Reset(NumTracks);

	TArray<FVirtualBoneCompactPoseData>& VBCompactPoseData = FBuildRawPoseScratchArea::Get().VirtualBoneCompactPoseData;
	VBCompactPoseData = RequiredBones.GetVirtualBoneCompactPoseData();

	FCompactPose Key2Pose;
	if (bInterpolateT)
	{
		Key2Pose.CopyBonesFrom(InOutPose);
	}

	for (int32 TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
	{
		const int32 SkeletonBoneIndex = TrackToSkeletonMapTable[TrackIndex].BoneTreeIndex;
		// not sure it's safe to assume that SkeletonBoneIndex can never be INDEX_NONE
		if ((SkeletonBoneIndex != INDEX_NONE) && (SkeletonBoneIndex < MAX_BONES))
		{
			const FCompactPoseBoneIndex PoseBoneIndex = RequiredBones.GetCompactPoseIndexFromSkeletonIndex(SkeletonBoneIndex);
			if (PoseBoneIndex != INDEX_NONE)
			{
				for (int32 Idx = 0; Idx < VBCompactPoseData.Num(); ++Idx)
				{
					FVirtualBoneCompactPoseData& VB = VBCompactPoseData[Idx];
					if (PoseBoneIndex == VB.VBIndex)
					{
						// Remove this bone as we have written data for it (false so we dont resize allocation)
						VBCompactPoseData.RemoveAtSwap(Idx, 1, false);
						break; //Modified TArray so must break here
					}
				}
				// extract animation

				const FRawAnimSequenceTrack& TrackToExtract = InAnimationData[TrackIndex];

				// Bail out (with rather wacky data) if data is empty for some reason.
				if (TrackToExtract.PosKeys.Num() == 0 || TrackToExtract.RotKeys.Num() == 0)
				{
					InOutPose[PoseBoneIndex].SetIdentity();

					if (bInterpolateT)
					{
						Key2Pose[PoseBoneIndex].SetIdentity();
					}
				}
				else
				{
					InOutPose[PoseBoneIndex] = ExtractTransformForKey(KeyIndex1, TrackToExtract);

					if (bInterpolateT)
					{
						Key2Pose[PoseBoneIndex] = ExtractTransformForKey(KeyIndex2, TrackToExtract);
					}
				}

				RetargetTracking.Add(FRetargetTracking(PoseBoneIndex, SkeletonBoneIndex));
			}
		}
	}

	//Build Virtual Bones
	if (VBCompactPoseData.Num() > 0)
	{
		FCSPose<FCompactPose> CSPose1;
		CSPose1.InitPose(InOutPose);

		FCSPose<FCompactPose> CSPose2;
		if (bInterpolateT)
		{
			CSPose2.InitPose(Key2Pose);
		}

		for (FVirtualBoneCompactPoseData& VB : VBCompactPoseData)
		{
			FTransform Source = CSPose1.GetComponentSpaceTransform(VB.SourceIndex);
			FTransform Target = CSPose1.GetComponentSpaceTransform(VB.TargetIndex);
			InOutPose[VB.VBIndex] = Target.GetRelativeTransform(Source);

			if (bInterpolateT)
			{
				FTransform Source2 = CSPose2.GetComponentSpaceTransform(VB.SourceIndex);
				FTransform Target2 = CSPose2.GetComponentSpaceTransform(VB.TargetIndex);
				Key2Pose[VB.VBIndex] = Target2.GetRelativeTransform(Source2);
			}
		}
	}

	if (bInterpolateT)
	{
		for (FCompactPoseBoneIndex BoneIndex : InOutPose.ForEachBoneIndex())
		{
			InOutPose[BoneIndex].Blend(InOutPose[BoneIndex], Key2Pose[BoneIndex], Alpha);
		}
	}
}

void BuildPoseFromRawData(const TArray<FRawAnimSequenceTrack>& InAnimationData, const TArray<struct FTrackToSkeletonMap>& TrackToSkeletonMapTable, FCompactPose& InOutPose, float InTime, EAnimInterpolationType Interpolation, int32 NumFrames, float SequenceLength, FName RetargetSource)
{
	int32 KeyIndex1, KeyIndex2;
	float Alpha;
	FAnimationRuntime::GetKeyIndicesFromTime(KeyIndex1, KeyIndex2, Alpha, InTime, NumFrames, SequenceLength);

	if (Interpolation == EAnimInterpolationType::Step)
	{
		Alpha = 0.f;
	}

	bool bInterpolate = true;

	if (Alpha < KINDA_SMALL_NUMBER)
	{
		Alpha = 0.f;
		bInterpolate = false;
	}
	else if (Alpha > 1.f - KINDA_SMALL_NUMBER)
	{
		bInterpolate = false;
		KeyIndex1 = KeyIndex2;
	}

	if (bInterpolate)
	{
		BuildPoseFromRawDataInternal<true>(InAnimationData, TrackToSkeletonMapTable, InOutPose, KeyIndex1, KeyIndex2, Alpha);
	}
	else
	{
		BuildPoseFromRawDataInternal<false>(InAnimationData, TrackToSkeletonMapTable, InOutPose, KeyIndex1, KeyIndex2, Alpha);
	}

	const FBoneContainer& RequiredBones = InOutPose.GetBoneContainer();
	const bool bDisableRetargeting = RequiredBones.GetDisableRetargeting();

	if (!bDisableRetargeting)
	{
		const TArray<FRetargetTracking>& RetargetTracking = FBuildRawPoseScratchArea::Get().RetargetTracking;

		USkeleton* Skeleton = RequiredBones.GetSkeletonAsset();

		for (const FRetargetTracking& RT : RetargetTracking)
		{
			FAnimationRuntime::RetargetBoneTransform(Skeleton, RetargetSource, InOutPose[RT.PoseBoneIndex], RT.SkeletonBoneIndex, RT.PoseBoneIndex, RequiredBones, false);
		}
	}

}


