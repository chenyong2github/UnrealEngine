// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeMessages.h"

#define LOCTEXT_NAMESPACE "AnimNode_PoseSearchHistoryCollector"

namespace UE { namespace PoseSearch { namespace Private {

class FPoseHistoryProvider : public IPoseHistoryProvider
{
public:
	FPoseHistoryProvider(FAnimNode_PoseSearchHistoryCollector* InNode)
		: Node(*InNode)
	{}

	// IPoseHistoryProvider interface
	virtual const FPoseHistory& GetPoseHistory() const override
	{
		return Node.GetPoseHistory();
	}

	virtual FPoseHistory& GetPoseHistory() override
	{
		return Node.GetPoseHistory();
	}

	// Node we wrap
	FAnimNode_PoseSearchHistoryCollector& Node;
};

}}} // namespace UE::PoseSearch::Private

/////////////////////////////////////////////////////
// FAnimNode_PoseSearchHistoryCollector

void FAnimNode_PoseSearchHistoryCollector::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);

	Super::Initialize_AnyThread(Context);

	// @@ need to do this once based on descendant node's (or input param?) search schema, not every node init
	PoseHistory.Init(32, 1.0f);

	UE::Anim::TScopedGraphMessage<UE::PoseSearch::Private::FPoseHistoryProvider> ScopedMessage(Context, this);

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

	UE::Anim::TScopedGraphMessage<UE::PoseSearch::Private::FPoseHistoryProvider> ScopedMessage(Context, this);

	Source.Update(Context);

	EvalDeltaTime += Context.GetDeltaTime();
}

void FAnimNode_PoseSearchHistoryCollector::GatherDebugData(FNodeDebugData& DebugData)
{
	Source.GatherDebugData(DebugData);
}

#undef LOCTEXT_NAMESPACE