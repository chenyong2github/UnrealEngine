// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BonePose.h"

struct FAnimationPoseData;
struct FAnimExtractContext;
class UAnimationAsset;
class UAnimNotifyState_PoseSearchBase;

namespace UE::PoseSearch
{

/**
 * Helper for sampling data from animation assets
 */
struct POSESEARCH_API FAnimationAssetSampler
{
	FAnimationAssetSampler(TWeakObjectPtr<const UAnimationAsset> InAnimationAsset = nullptr, const FVector& InBlendParameters = FVector::ZeroVector, const FBoneContainer& InBoneContainer = FBoneContainer(), int32 InRootTransformSamplingRate = 30);
	void Init(TWeakObjectPtr<const UAnimationAsset> InAnimationAsset, const FVector& InBlendParameters = FVector::ZeroVector, const FBoneContainer& InBoneContainer = FBoneContainer(), int32 InRootTransformSamplingRate = 30);

	bool IsInitialized() const;
	float GetPlayLength() const;
	float GetScaledTime(float Time) const;
	bool IsLoopable() const;

	// Gets the final root transformation at the end of the asset's playback time
	FTransform GetTotalRootTransform() const;

	// Extracts pose for this asset for a given context
	void ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const;

	// Extracts root transform at the given time, using the extremities of the sequence to extrapolate beyond the 
	// sequence limits when Time is less than zero or greater than the sequence length.
	FTransform ExtractRootTransform(float Time) const;

	// Extracts notify states inheriting from UAnimNotifyState_PoseSearchBase present in the sequence at Time.
	// The function does not empty NotifyStates before adding new notifies!
	void ExtractPoseSearchNotifyStates(float Time, TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const;

	const UAnimationAsset* GetAsset() const;

	void Process();

protected:
	TWeakObjectPtr<const UAnimationAsset> AnimationAsset;

	// members used to sample blend spaces only!
	FVector BlendParameters = FVector::ZeroVector;
	FBoneContainer BoneContainer;
	int32 RootTransformSamplingRate = 30;
	float CachedPlayLength = -1.f;
	TArray<FTransform> AccumulatedRootTransform;

	const float ExtrapolationSampleTime = 1.f / 30.f;
	const float ExtractionInterval = 1.0f / 120.0f;
};

} // namespace UE::PoseSearch

