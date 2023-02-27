// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_CorrectivesSource.h"

#include "Animation/AnimInstanceProxy.h"
#include "PoseCorrectivesAsset.h"
#include "Animation/AnimCurveUtils.h"


void FAnimNode_CorrectivesSource::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	SourcePose.CacheBones(Context);

	if (!IsValid(PoseCorrectivesAsset))
	{
		return;
	}

	const FBoneContainer& BoneContainer = Context.AnimInstanceProxy->GetRequiredBones();

	BoneCompactIndices.Reset();

	for (const FName& BoneName : PoseCorrectivesAsset->GetBoneNames())
	{
		FBoneReference BoneRef(BoneName);
		BoneRef.Initialize(BoneContainer);
		BoneCompactIndices.Push(BoneRef.GetCompactPoseIndex(BoneContainer));
	}
}

void FAnimNode_CorrectivesSource::Evaluate_AnyThread(FPoseContext& Output)
{
	FPoseContext SourceData(Output);
	SourcePose.Evaluate(SourceData);
	Output = SourceData;

	if (!bUseCorrectiveSource || !PoseCorrectivesAsset)
	{
		return;
	}

	FPoseCorrective* PoseCorrective = PoseCorrectivesAsset->FindCorrective(CurrentCorrrective);
	if (PoseCorrective)
	{
		for (int32 PoseBoneIndex = 0; PoseBoneIndex < BoneCompactIndices.Num(); PoseBoneIndex++)
		{
			const FCompactPoseBoneIndex& BoneCompactIndex = BoneCompactIndices[PoseBoneIndex];
			if (BoneCompactIndex.IsValid())
			{
				Output.Pose[BoneCompactIndex] = bUseSourcePose ? PoseCorrective->PoseLocal[PoseBoneIndex] : PoseCorrective->CorrectivePoseLocal[PoseBoneIndex];
			}
		}

		FBlendedCurve Curve;
		auto GetNameFromIndex = [PoseCorrectivesAsset = PoseCorrectivesAsset](int32 InCurveIndex)
		{
			return PoseCorrectivesAsset->GetCurveNames()[InCurveIndex];
		};
		
		auto GetValueFromIndex = [PoseCorrective, bUseSourcePose = bUseSourcePose](int32 InCurveIndex)
		{
			float CurveValue = PoseCorrective->CurveData[InCurveIndex];
			if (!bUseSourcePose)
				CurveValue += PoseCorrective->CorrectiveCurvesDelta[InCurveIndex];
			return CurveValue;
		};

		UE::Anim::FCurveUtils::BuildUnsorted(Curve, PoseCorrectivesAsset->GetCurveNames().Num(), GetNameFromIndex, GetValueFromIndex);

		Output.Curve.Override(Curve);
	}
}
