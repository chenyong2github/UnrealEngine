// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeMessages.h"
#include "Engine/SkinnedAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_PoseSearchHistoryCollector)

#define LOCTEXT_NAMESPACE "AnimNode_PoseSearchHistoryCollector"

#if WITH_EDITORONLY_DATA && ENABLE_ANIM_DEBUG
TAutoConsoleVariable<bool> CVarAnimPoseHistoryDebugDraw(TEXT("a.AnimNode.PoseHistory.DebugDraw"), false, TEXT("Enable / Disable Pose History DebugDraw"));
#endif

namespace UE::PoseSearch::Private
{

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

} // namespace UE::PoseSearch::Private


/////////////////////////////////////////////////////
// FAnimNode_PoseSearchHistoryCollector

void FAnimNode_PoseSearchHistoryCollector::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);

	Super::Initialize_AnyThread(Context);

	// @@ need to do this once based on descendant node's (or input param?) search schema, not every node init
	PoseHistory.Init(PoseCount, PoseDuration);

	UE::Anim::TScopedGraphMessage<UE::PoseSearch::Private::FPoseHistoryProvider> ScopedMessage(Context, this);

	Source.Initialize(Context);
}

void FAnimNode_PoseSearchHistoryCollector::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)

	Source.CacheBones(Context);
}

void FAnimNode_PoseSearchHistoryCollector::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);

	Source.Evaluate(Output);
	PoseHistory.Update(Output.AnimInstanceProxy->GetDeltaSeconds(), Output, Output.AnimInstanceProxy->GetComponentTransform());

#if WITH_EDITORONLY_DATA && ENABLE_ANIM_DEBUG
	bWasEvaluated = true;
#endif
}

void FAnimNode_PoseSearchHistoryCollector::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread);

	UE::Anim::TScopedGraphMessage<UE::PoseSearch::Private::FPoseHistoryProvider> ScopedMessage(Context, this);

	Source.Update(Context);
}

void FAnimNode_PoseSearchHistoryCollector::GatherDebugData(FNodeDebugData& DebugData)
{
	Source.GatherDebugData(DebugData);
}

bool FAnimNode_PoseSearchHistoryCollector::HasPreUpdate() const
{
	return WITH_EDITORONLY_DATA && ENABLE_ANIM_DEBUG;
}

void FAnimNode_PoseSearchHistoryCollector::PreUpdate(const UAnimInstance* InAnimInstance)
{
#if WITH_EDITORONLY_DATA && ENABLE_ANIM_DEBUG
	if (bWasEvaluated && CVarAnimPoseHistoryDebugDraw.GetValueOnAnyThread())
	{
		if (const USkeletalMeshComponent* SkeletalMeshComponent = InAnimInstance->GetSkelMeshComponent())
		{
			const USkinnedAsset* SkinnedAsset = SkeletalMeshComponent->GetSkinnedAsset();
			const UWorld* World = SkeletalMeshComponent->GetWorld();
			if (SkinnedAsset && World)
			{
				if (const USkeleton* Skeleton = SkinnedAsset->GetSkeleton())
				{
					PoseHistory.DebugDraw(World, Skeleton);
				}
			}
		}
	}

	bWasEvaluated = false;
#endif // WITH_EDITORONLY_DATA && ENABLE_ANIM_DEBUG
}

#undef LOCTEXT_NAMESPACE
