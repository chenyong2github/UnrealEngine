// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_SaveCachedPose.h"
#include "Animation/AnimInstanceProxy.h"

/////////////////////////////////////////////////////
// FAnimNode_SaveCachedPose

FAnimNode_SaveCachedPose::FAnimNode_SaveCachedPose()
	: GlobalWeight(0.0f)
{
}

void FAnimNode_SaveCachedPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	// StateMachines cause reinitialization on state changes.
	// we only want to let them through if we're not relevant as to not create a pop.
	if (!InitializationCounter.IsSynchronized_Counter(Context.AnimInstanceProxy->GetInitializationCounter())
		|| (UpdateCounter.HasEverBeenUpdated() && !UpdateCounter.WasSynchronizedCounter(Context.AnimInstanceProxy->GetUpdateCounter())))
	{
		InitializationCounter.SynchronizeWith(Context.AnimInstanceProxy->GetInitializationCounter());

		FAnimNode_Base::Initialize_AnyThread(Context);

		// Initialize the subgraph
		Pose.Initialize(Context);
	}
}

void FAnimNode_SaveCachedPose::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	if (!CachedBonesCounter.IsSynchronized_Counter(Context.AnimInstanceProxy->GetCachedBonesCounter()))
	{
		CachedBonesCounter.SynchronizeWith(Context.AnimInstanceProxy->GetCachedBonesCounter());

		// Cache bones in the subgraph
		Pose.CacheBones(Context);
	}
}

void FAnimNode_SaveCachedPose::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	FCachedUpdateContext& CachedUpdate = CachedUpdateContexts.AddDefaulted_GetRef();

	// Make a minimal copy of the shared context for cached updates
	if (FAnimationUpdateSharedContext* SharedContext = Context.GetSharedContext())
	{
		CachedUpdate.SharedContext = MakeShared<FAnimationUpdateSharedContext>();
		CachedUpdate.SharedContext->CopyForCachedUpdate(*SharedContext);

	}

	// Store this context for the post update
	CachedUpdate.Context = Context.WithOtherSharedContext(CachedUpdate.SharedContext.Get());
}

void FAnimNode_SaveCachedPose::Evaluate_AnyThread(FPoseContext& Output)
{
	if (!EvaluationCounter.IsSynchronized_Counter(Output.AnimInstanceProxy->GetEvaluationCounter()))
	{
		EvaluationCounter.SynchronizeWith(Output.AnimInstanceProxy->GetEvaluationCounter());

		FPoseContext CachingContext(Output);
		Pose.Evaluate(CachingContext);
		CachedPose.MoveBonesFrom(CachingContext.Pose);
		CachedCurve.MoveFrom(CachingContext.Curve);
	}

	// Return the cached result
	Output.Pose.CopyBonesFrom(CachedPose);
	Output.Curve.CopyFrom(CachedCurve);
}

void FAnimNode_SaveCachedPose::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("%s:"), *CachePoseName.ToString());

	if (FNodeDebugData* SaveCachePoseDebugDataPtr = DebugData.GetCachePoseDebugData(GlobalWeight))
	{
		SaveCachePoseDebugDataPtr->AddDebugItem(DebugLine);
		Pose.GatherDebugData(*SaveCachePoseDebugDataPtr);
	}
}

void FAnimNode_SaveCachedPose::PostGraphUpdate()
{
	GlobalWeight = 0.f;

	// Update GlobalWeight based on highest weight calling us.
	const int32 NumContexts = CachedUpdateContexts.Num();
	if (NumContexts > 0)
	{
		GlobalWeight = CachedUpdateContexts[0].Context.GetFinalBlendWeight();
		int32 MaxWeightIdx = 0;
		for (int32 CurrIdx = 1; CurrIdx < NumContexts; ++CurrIdx)
		{
			const float BlendWeight = CachedUpdateContexts[CurrIdx].Context.GetFinalBlendWeight();
			if (BlendWeight > GlobalWeight)
			{
				GlobalWeight = BlendWeight;
				MaxWeightIdx = CurrIdx;
			}
		}

		// Update the max weighted pose node
		Pose.Update(CachedUpdateContexts[MaxWeightIdx].Context);

		// Determine if any ancestors are interested in the other updates we'll be skipping
		TArray<FAnimNode_Base*, TInlineAllocator<4>> AncestorsWithSkippedUpdateHandlers;
		if (NumContexts > 1)
		{
			FAnimationUpdateSharedContext* SharedContext = CachedUpdateContexts[MaxWeightIdx].Context.GetSharedContext();
			FAnimNodeTracker* AncestorTracker = SharedContext ? &SharedContext->AncestorTracker : nullptr;

			if (AncestorTracker)
			{
				for (auto Iter : AncestorTracker->Map)
				{
					FAnimNode_Base* AncestorNode = Iter.Value.Num() ? Iter.Value.Top() : nullptr;
					if (AncestorNode && AncestorNode->WantsSkippedUpdates())
					{
						AncestorsWithSkippedUpdateHandlers.Add(AncestorNode);
					}
				}
			}
		}

		if (AncestorsWithSkippedUpdateHandlers.Num() > 0)
		{
			// Build a list of skipped updates
			TArray<const FAnimationUpdateContext *, TInlineAllocator<4>> SkippedUpdateContexts;
			for (int32 CurrIdx = 0; CurrIdx < NumContexts; ++CurrIdx)
			{
				if (CurrIdx != MaxWeightIdx)
				{
					SkippedUpdateContexts.Add(&CachedUpdateContexts[CurrIdx].Context);
				}
			}

			// Inform any interested ancestors about the skipped updates
			for (FAnimNode_Base* AncestorNode : AncestorsWithSkippedUpdateHandlers)
			{
				AncestorNode->OnUpdatesSkipped(SkippedUpdateContexts);
			}
		}

	}

	CachedUpdateContexts.Reset();
}
