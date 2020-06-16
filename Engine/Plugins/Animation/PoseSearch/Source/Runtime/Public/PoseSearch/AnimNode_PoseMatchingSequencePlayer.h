// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNode_SequencePlayer.h"
#include "PoseSearch/PoseSearch.h"

#include "AnimNode_PoseMatchingSequencePlayer.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct POSESEARCH_API FAnimNode_PoseMatchingSequencePlayer : public FAnimNode_SequencePlayer
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	bool StartFromNearestPose;

public:
	FAnimNode_PoseMatchingSequencePlayer()
	    : StartFromNearestPose(true)
	{}

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual bool NeedsOnInitializeAnimInstance() const { return true; }
	// End of FAnimNode_Base interface

	// FAnimNode_SequencePlayerBase interface
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	// End of FAnimNode_SequencePlayerBase interface

protected:
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance);

private:
	FPoseSearchBuildQueryScratch Scratch;
	TArray<float> SearchQuery;
	float InitDelta;
};