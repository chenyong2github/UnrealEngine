// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_BlendStack.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"

#define LOCTEXT_NAMESPACE "AnimNode_BlendStack"

/////////////////////////////////////////////////////
// FPoseSearchAnimPlayer
void FPoseSearchAnimPlayer::Initialize(ESearchIndexAssetType InAssetType, UAnimationAsset* AnimationAsset, float AccumulatedTime, bool bLoop, bool bMirrored, UMirrorDataTable* MirrorDataTable, float BlendTime, FVector BlendParameters)
{
	check(AnimationAsset);
	check(MirrorDataTable);

	AssetType = InAssetType;
	TotalBlendInTime = BlendTime;
	CurrentBlendInTime = 0.f;
	BlendWeight = 0.f;

	MirrorNode.SetMirrorDataTable(MirrorDataTable);
	MirrorNode.SetMirror(bMirrored);
	
	if (AssetType == ESearchIndexAssetType::Sequence)
	{
		UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset);
		check(Sequence);

		SequencePlayerNode.SetAccumulatedTime(AccumulatedTime);
		SequencePlayerNode.SetSequence(Sequence);
		SequencePlayerNode.SetLoopAnimation(bLoop);
		SequencePlayerNode.SetPlayRate(1.0f);
	}
	else if (AssetType == ESearchIndexAssetType::BlendSpace)
	{
		UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset);
		check(BlendSpace);
		BlendSpacePlayerNode.SetResetPlayTimeWhenBlendSpaceChanges(false /*!bReset*/);
		BlendSpacePlayerNode.SetAccumulatedTime(AccumulatedTime);
		BlendSpacePlayerNode.SetBlendSpace(BlendSpace);
		BlendSpacePlayerNode.SetLoop(bLoop);
		BlendSpacePlayerNode.SetPlayRate(1.0f);
		BlendSpacePlayerNode.SetPosition(BlendParameters);
	}
	else
	{
		checkNoEntry();
	}

	UpdateSourceLinkNode();
}

// @todo: maybe implement copy/move constructors and assignement operator do so (or use a list instead of an array)
// since we're making copies and moving this object in memory, we're using this method to set the MirrorNode SourceLinkNode when necessary
void FPoseSearchAnimPlayer::UpdateSourceLinkNode()
{
	if (AssetType == ESearchIndexAssetType::Sequence)
	{
		MirrorNode.SetSourceLinkNode(&SequencePlayerNode);
	}
	else if (AssetType == ESearchIndexAssetType::BlendSpace)
	{
		MirrorNode.SetSourceLinkNode(&BlendSpacePlayerNode);
	}
	else
	{
		checkNoEntry();
	}
}

void FPoseSearchAnimPlayer::Evaluate_AnyThread(FPoseContext& Output)
{
	UpdateSourceLinkNode();
	MirrorNode.Evaluate_AnyThread(Output);
}

void FPoseSearchAnimPlayer::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	UpdateSourceLinkNode();
	MirrorNode.Update_AnyThread(Context);
	CurrentBlendInTime += Context.GetDeltaTime();
}

float FPoseSearchAnimPlayer::GetAccumulatedTime() const
{
	if (AssetType == ESearchIndexAssetType::Sequence)
	{
		return SequencePlayerNode.GetAccumulatedTime();
	}
	
	if (AssetType == ESearchIndexAssetType::BlendSpace)
	{
		return BlendSpacePlayerNode.GetAccumulatedTime();
	}
	
	checkNoEntry();
	return 0.f;
}

float FPoseSearchAnimPlayer::GetBlendInPercentage() const
{
	if (FMath::IsNearlyZero(TotalBlendInTime))
	{
		return 1.f;
	}

	return FMath::Clamp(CurrentBlendInTime / TotalBlendInTime, 0.f, 1.f);
}

void FPoseSearchAnimPlayer::SetBlendWeight(float InBlendWeight)
{
	BlendWeight = InBlendWeight;
}

/////////////////////////////////////////////////////
// FAnimNode_BlendStack
void FAnimNode_BlendStack::Evaluate_AnyThread(FPoseContext& Output)
{
	Super::Evaluate_AnyThread(Output);

	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);

	const int32 BlendStackSize = AnimPlayers.Num();
	if (BlendStackSize <= 0)
	{
		Output.ResetToRefPose();
	}
	else if (BlendStackSize == 1)
	{
		AnimPlayers.First().Evaluate_AnyThread(Output);
	}
	else
	{
		TArray<FPoseContext, TMemStackAllocator<>> PoseContexts;
		PoseContexts.Reserve(BlendStackSize);
		for (int32 i = 0; i < BlendStackSize; ++i)
		{
			PoseContexts.Add(Output);
			AnimPlayers[i].Evaluate_AnyThread(PoseContexts[i]);
		}

		// @todo: optimize copies and allocations: is there a FAnimationRuntime::BlendPosesTogether(PoseContexts, Weights, AnimationPoseData)?
		TArray<FCompactPose, TMemStackAllocator<>> Poses;
		TArray<FBlendedCurve, TMemStackAllocator<>> Curves;
		TArray<UE::Anim::FStackAttributeContainer, TMemStackAllocator<>> Attributes;
		TArray<float, TMemStackAllocator<>> Weights;

		Poses.AddDefaulted(BlendStackSize);
		Curves.AddDefaulted(BlendStackSize);
		Attributes.AddDefaulted(BlendStackSize);
		Weights.AddDefaulted(BlendStackSize);

		for (int32 i = 0; i < BlendStackSize; ++i)
		{
			Poses[i] = PoseContexts[i].Pose;
			Curves[i] = PoseContexts[i].Curve;
			Attributes[i] = PoseContexts[i].CustomAttributes;
			Weights[i] = AnimPlayers[i].GetBlendWeight();
		}

		FAnimationPoseData AnimationPoseData(Output);
		FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, AnimationPoseData);
	}
}

void FAnimNode_BlendStack::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	Super::UpdateAssetPlayer(Context);

	const int32 BlendStackSize = AnimPlayers.Num();

	CalculateWeights();
	PruneBlendStack(BlendStackSize);

	for (FPoseSearchAnimPlayer& AnimPlayer : AnimPlayers)
	{
		const FAnimationUpdateContext AnimPlayerContext = Context.FractionalWeightAndRootMotion(AnimPlayer.GetBlendWeight(), AnimPlayer.GetBlendWeight());
		AnimPlayer.Update_AnyThread(AnimPlayerContext);
		check(AnimPlayer.GetBlendWeight() == AnimPlayerContext.GetFinalBlendWeight());
		check(AnimPlayer.GetBlendWeight() == AnimPlayerContext.GetRootMotionWeightModifier());
	}
}

float FAnimNode_BlendStack::GetAccumulatedTime() const
{
	return AnimPlayers.IsEmpty() ? 0.f : AnimPlayers.First().GetAccumulatedTime();
}

void FAnimNode_BlendStack::BlendTo(ESearchIndexAssetType AssetType, UAnimationAsset* AnimationAsset, float AccumulatedTime, bool bLoop, bool bMirrored, UMirrorDataTable* MirrorDataTable, int32 MaxActiveBlends, float BlendTime, FVector BlendParameters)
{
	AnimPlayers.PushFirst(FPoseSearchAnimPlayer());
	AnimPlayers.First().Initialize(AssetType, AnimationAsset, AccumulatedTime, bLoop, bMirrored, MirrorDataTable, BlendTime, BlendParameters);

	CalculateWeights();
	PruneBlendStack(MaxActiveBlends);
}

void FAnimNode_BlendStack::CalculateWeights()
{
	// AnimPlayers[0] is the most newly inserted AnimPlayer, AnimPlayers[AnimPlayers.Num()-1] is the oldest, so to calculate the weights
	// we ask AnimPlayers[0] its BlendInPercentage and then distribute the left over (CurrentWeightMultiplier) to the rest of the AnimPlayers
	float CurrentWeightMultiplier = 1.f;
	const int32 BlendStackSize = AnimPlayers.Num();
	for (int32 i = 0; i < BlendStackSize; ++i)
	{
		const bool bIsLastAnimPlayers = i == BlendStackSize - 1;
		const float BlendInPercentage = bIsLastAnimPlayers ? 1.f : AnimPlayers[i].GetBlendInPercentage();
		AnimPlayers[i].SetBlendWeight(CurrentWeightMultiplier * BlendInPercentage);

		CurrentWeightMultiplier *= (1.f - BlendInPercentage);
	}
}

void FAnimNode_BlendStack::PruneBlendStack(int32 MaxActiveBlends)
{
	int32 FirstZeroWeightAnimPlayerIndex = 1;
	for (; FirstZeroWeightAnimPlayerIndex < AnimPlayers.Num(); ++FirstZeroWeightAnimPlayerIndex)
	{
		if (FMath::IsNearlyZero(AnimPlayers[FirstZeroWeightAnimPlayerIndex].GetBlendWeight()))
		{
			break;
		}
	}
	// if the weight of the FirstZeroWeightAnimPlayerIndex-th AnimPlayer is zero, all the subsequent AnimPlayers contribution will be zero, so we delete from FirstZeroWeightAnimPlayerIndex forward

	const int32 WantedAnimPlayersNum = FMath::Max(1, FMath::Min(FirstZeroWeightAnimPlayerIndex, MaxActiveBlends)); // we save at least one FPoseSearchAnimPlayer

	while (AnimPlayers.Num() > WantedAnimPlayersNum)
	{
		// @todo: instead of just popping the unwanted AnimPlayers we should store the blended pose + weight in a FPoseContext
		AnimPlayers.PopLast();
	}
}

#undef LOCTEXT_NAMESPACE