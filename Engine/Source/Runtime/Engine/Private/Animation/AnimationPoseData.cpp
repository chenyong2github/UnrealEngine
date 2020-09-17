// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimationPoseData.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/CustomAttributesRuntime.h"

#include "BonePose.h"

FAnimationPoseData::FAnimationPoseData(FPoseContext& InPoseContext)
	: Pose(InPoseContext.Pose), Curve(InPoseContext.Curve), Attributes(InPoseContext.CustomAttributes)
{
}

FAnimationPoseData::FAnimationPoseData(FSlotEvaluationPose& InSlotPoseContext)
	: Pose(InSlotPoseContext.Pose), Curve(InSlotPoseContext.Curve), Attributes(InSlotPoseContext.Attributes)
{
}

FAnimationPoseData::FAnimationPoseData(FCompactPose& InPose, FBlendedCurve& InCurve, FStackCustomAttributes& InAttributes) : Pose(InPose), Curve(InCurve), Attributes(InAttributes)
{
}

FCompactPose& FAnimationPoseData::GetPose()
{
	return Pose;
}

const FCompactPose& FAnimationPoseData::GetPose() const
{
	return Pose;
}

FBlendedCurve& FAnimationPoseData::GetCurve()
{
	return Curve;
}

const FBlendedCurve& FAnimationPoseData::GetCurve() const
{
	return Curve;
}

FStackCustomAttributes& FAnimationPoseData::GetAttributes()
{
	return Attributes;
}

const FStackCustomAttributes& FAnimationPoseData::GetAttributes() const
{
	return Attributes;
}