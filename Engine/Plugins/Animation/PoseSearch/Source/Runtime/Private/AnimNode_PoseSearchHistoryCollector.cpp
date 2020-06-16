// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Animation/AnimInstanceProxy.h"

#define LOCTEXT_NAMESPACE "AnimNode_PoseSearchHistoryCollector"

/////////////////////////////////////////////////////
// FAnimNode_PoseSearchHistoryCollector

void FAnimNode_PoseSearchHistoryCollector::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);

	Super::Initialize_AnyThread(Context);

	// @@ need to do this once based on descendant node's search schema, not every node init
	PoseHistory.Init(32, 1.0f);

	FScopedAnimNodeTracker ScopedNodeTracker = Context.TrackAncestor(this);

	Source.Initialize(Context);

	EvalDeltaTime = 0.0f;
}

void FAnimNode_PoseSearchHistoryCollector::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);

	Source.Evaluate(Output);

	PoseHistory.Update(EvalDeltaTime, Output.Pose);

	EvalDeltaTime = 0.0f;
}

void FAnimNode_PoseSearchHistoryCollector::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread);

	FScopedAnimNodeTracker ScopedNodeTracker = Context.TrackAncestor(this);

	Source.Update(Context);

	EvalDeltaTime += Context.GetDeltaTime();
}

#undef LOCTEXT_NAMESPACE