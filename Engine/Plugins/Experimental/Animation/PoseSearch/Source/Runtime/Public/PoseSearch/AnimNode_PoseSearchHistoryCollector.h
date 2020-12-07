// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearch.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_PoseSearchHistoryCollector.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct POSESEARCH_API FAnimNode_PoseSearchHistoryCollector : public FAnimNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink Source;

public:
	FAnimNode_PoseSearchHistoryCollector()
	{}

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context);
	virtual void GatherDebugData(FNodeDebugData& DebugData);
	// End of FAnimNode_Base interface

	UE::PoseSearch::FPoseHistory& GetPoseHistory() { return PoseHistory; }
	const UE::PoseSearch::FPoseHistory& GetPoseHistory() const { return PoseHistory; }

protected:

	UE::PoseSearch::FPoseHistory PoseHistory;
	TArray<float> Query;
	float EvalDeltaTime = 0.0f;
};