// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FCompactPose;
struct FBlendedCurve;
struct FStackCustomAttributes;
struct FPoseContext;
struct FSlotEvaluationPose;

/** Structure used for passing around animation pose related data throughout the Animation Runtime */
struct ENGINE_API FAnimationPoseData
{
	FAnimationPoseData(FPoseContext& InPoseContext);
	FAnimationPoseData(FSlotEvaluationPose& InSlotPoseContext);
	FAnimationPoseData(FCompactPose& InPose, FBlendedCurve& InCurve, FStackCustomAttributes& InAttributes);
	
	/** No default constructor, or assignment */
	FAnimationPoseData() = delete;
	FAnimationPoseData& operator=(FAnimationPoseData&& Other) = delete;

	/** Getters for the wrapped structures */
	const FCompactPose& GetPose() const;
	FCompactPose& GetPose();
	const FBlendedCurve& GetCurve() const;
	FBlendedCurve& GetCurve();
	const FStackCustomAttributes& GetAttributes() const;
	FStackCustomAttributes& GetAttributes();

protected:
	FCompactPose& Pose;
	FBlendedCurve& Curve;
	FStackCustomAttributes& Attributes;
};