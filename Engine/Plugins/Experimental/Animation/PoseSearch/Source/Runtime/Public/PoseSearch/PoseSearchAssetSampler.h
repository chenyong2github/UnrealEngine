// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BonePose.h"

struct FAnimationPoseData;
struct FAnimExtractContext;
class UAnimationAsset;
class UAnimInstance;
class UAnimNotifyState_PoseSearchBase;
class UBlendSpace;
class UMirrorDataTable;

namespace UE::PoseSearch
{

/**
 * Helper interface for sampling data from animation assets
 */
struct POSESEARCH_API FAssetSamplerBase : public TSharedFromThis<FAssetSamplerBase>
{
	virtual ~FAssetSamplerBase() {}

	virtual float GetPlayLength() const { return 0.f; }
	virtual float GetScaledTime(float Time) const { return Time; }
	virtual bool IsLoopable() const { return false; }

	// Gets the final root transformation at the end of the asset's playback time
	virtual FTransform GetTotalRootTransform() const { return FTransform::Identity; }

	// Extracts pose for this asset for a given context
	virtual void ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const { }

	// Extracts root transform at the given time, using the extremities of the sequence to extrapolate beyond the 
	// sequence limits when Time is less than zero or greater than the sequence length.
	virtual FTransform ExtractRootTransform(float Time) const { return FTransform::Identity; }

	// Extracts notify states inheriting from UAnimNotifyState_PoseSearchBase present in the sequence at Time.
	// The function does not empty NotifyStates before adding new notifies!
	virtual void ExtractPoseSearchNotifyStates(float Time, TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const { }

	virtual const UAnimationAsset* GetAsset() const { return nullptr; }

	virtual void Process() { }

	const float ExtrapolationSampleTime = 1.f / 30.f;
	const float ExtractionInterval = 1.0f / 120.0f;
};

// Sampler working with UAnimSequenceBase so it can be used for UAnimSequence as well as UAnimComposite.
struct POSESEARCH_API FSequenceBaseSampler : public FAssetSamplerBase
{
	struct FInput
	{
		TWeakObjectPtr<const UAnimSequenceBase> SequenceBase;
	} Input;

	void Init(const FInput& Input);
	virtual void Process() override;

	virtual float GetPlayLength() const override;
	virtual float GetScaledTime(float Time) const override;
	virtual bool IsLoopable() const override;

	virtual FTransform GetTotalRootTransform() const override;

	virtual void ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const override;
	virtual FTransform ExtractRootTransform(float Time) const override;
	virtual void ExtractPoseSearchNotifyStates(float Time, TArray<class UAnimNotifyState_PoseSearchBase*>& NotifyStates) const override;
	virtual const UAnimationAsset* GetAsset() const override;
};

struct POSESEARCH_API FBlendSpaceSampler : public FAssetSamplerBase
{
	struct FInput
	{
		FBoneContainer BoneContainer;
		TWeakObjectPtr<const UBlendSpace> BlendSpace;
		int32 RootTransformSamplingRate = 30;
		FVector BlendParameters;
	} Input;

	void Init(const FInput& Input);
	virtual void Process() override;

	virtual float GetPlayLength() const override { return PlayLength; }
	virtual float GetScaledTime(float Time) const override;
	virtual bool IsLoopable() const override;

	virtual FTransform GetTotalRootTransform() const override;

	virtual void ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const override;
	virtual FTransform ExtractRootTransform(float Time) const override;
	virtual void ExtractPoseSearchNotifyStates(float Time, TArray<class UAnimNotifyState_PoseSearchBase*>& NotifyStates) const override;

	virtual const UAnimationAsset* GetAsset() const override;

private:
	float PlayLength = 0.0f;
	TArray<FTransform> AccumulatedRootTransform;

	void ProcessPlayLength();
	void ProcessRootTransform();

	// Extracts the pre-computed blend space root transform. ProcessRootTransform must be run first.
	FTransform ExtractBlendSpaceRootTrackTransform(float Time) const;
	FTransform ExtractBlendSpaceRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const;
	FTransform ExtractBlendSpaceRootMotionFromRange(float StartTrackPosition, float EndTrackPosition) const;
};

struct POSESEARCH_API FAnimMontageSampler : public FAssetSamplerBase
{
	struct FInput
	{
		// @todo: add support for SlotName / multiple SlotAnimTracks
		TWeakObjectPtr<const UAnimMontage> AnimMontage;
	} Input;

	void Init(const FInput& Input);
	virtual void Process() override;

	virtual float GetPlayLength() const override;
	virtual float GetScaledTime(float Time) const override;
	virtual bool IsLoopable() const override;

	virtual FTransform GetTotalRootTransform() const override;

	virtual void ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const override;
	virtual FTransform ExtractRootTransform(float Time) const override;
	virtual void ExtractPoseSearchNotifyStates(float Time, TArray<class UAnimNotifyState_PoseSearchBase*>& NotifyStates) const override;
	virtual const UAnimationAsset* GetAsset() const override;

private:
	FTransform ExtractRootTransformInternal(float StartTime, float EndTime) const;
};

} // namespace UE::PoseSearch

