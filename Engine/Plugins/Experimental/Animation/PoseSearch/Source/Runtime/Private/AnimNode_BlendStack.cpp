// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_BlendStack.h"

#include "Algo/MaxElement.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"

#define LOCTEXT_NAMESPACE "AnimNode_BlendStack"

TAutoConsoleVariable<int32> CVarAnimBlendStackEnable(TEXT("a.AnimNode.BlendStack.Enable"), 1, TEXT("Enable / Disable Blend Stack"));

/////////////////////////////////////////////////////
// FPoseSearchAnimPlayer
void FPoseSearchAnimPlayer::Initialize(ESearchIndexAssetType InAssetType, UAnimationAsset* AnimationAsset, float AccumulatedTime, bool bLoop, bool bMirrored, UMirrorDataTable* MirrorDataTable, float BlendTime, const UBlendProfile* BlendProfile, EAlphaBlendOption InBlendOption, FVector BlendParameters)
{
	check(AnimationAsset);

	if (bMirrored && !MirrorDataTable)
	{
		UE_LOG(
			LogPoseSearch,
			Error,
			TEXT("FPoseSearchAnimPlayer failed to Initialize for %s. Mirroring will not work becasue MirrorDataTable is missing"),
			*GetNameSafe(AnimationAsset));
	}

	if (BlendProfile != nullptr)
	{
		const USkeleton* SkeletonAsset = BlendProfile->OwningSkeleton;
		check(SkeletonAsset);

		const FReferenceSkeleton& RefSkeleton = SkeletonAsset->GetReferenceSkeleton();
		const int32 NumSkeletonBones = RefSkeleton.GetNum();
		TotalBlendInTimePerBone.Init(BlendTime, NumSkeletonBones);

		BlendProfile->FillSkeletonBoneDurationsArray(TotalBlendInTimePerBone, BlendTime);
		BlendTime = *Algo::MaxElement(TotalBlendInTimePerBone);
	}
	BlendOption = InBlendOption;

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

bool FPoseSearchAnimPlayer::GetBlendInWeights(TArray<float>& Weights) const
{
	const int32 NumBones = TotalBlendInTimePerBone.Num();
	if (NumBones > 0)
	{
		Weights.SetNumUninitialized(NumBones);
		for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
		{
			const float TotalBlendInTimeBoneIdx = TotalBlendInTimePerBone[BoneIdx];
			if (FMath::IsNearlyZero(TotalBlendInTimeBoneIdx))
			{
				Weights[BoneIdx] = 1.f;
			}
			else
			{
				const float unclampedLinearWeight = CurrentBlendInTime / TotalBlendInTimeBoneIdx;
				Weights[BoneIdx] = FAlphaBlend::AlphaToBlendOption(unclampedLinearWeight, BlendOption);
			}
		}
		return true;
	}
	return false;
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

	int32 BlendStackSize = AnimPlayers.Num();

	// Disable blend stack if requested (for testing / debugging)
	if (!CVarAnimBlendStackEnable.GetValueOnAnyThread())
	{
		if (BlendStackSize > 1)
		{
			BlendStackSize = 1;
		}
	}

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
		AnimPlayers[BlendStackSize - 1].Evaluate_AnyThread(Output);

		FPoseContext EvaluationPoseContext(Output);
		FPoseContext BlendedPoseContext(Output); // @todo: this should not be necessary (but FBaseBlendedCurve::InitFrom complains about "ensure(&InCurveToInitFrom != this)"): optimize it away!
		FAnimationPoseData BlendedAnimationPoseData(BlendedPoseContext);

		const USkeleton* SkeletonAsset = Output.AnimInstanceProxy->GetRequiredBones().GetSkeletonAsset();
		check(SkeletonAsset);

		const FReferenceSkeleton& RefSkeleton = SkeletonAsset->GetReferenceSkeleton();
		const int32 NumSkeletonBones = RefSkeleton.GetNum();
		TArray<float> Weights;
		for (int32 i = BlendStackSize - 2; i >= 0; --i)
		{
			// Since we're re-using the same pose context for different players, curves need to be reset before extraction.
			EvaluationPoseContext.Curve.InitFrom(Output.AnimInstanceProxy->GetRequiredBones());
			AnimPlayers[i].Evaluate_AnyThread(EvaluationPoseContext);
			
			if (AnimPlayers[i].GetBlendInWeights(Weights))
			{
				// @todo: have BlendTwoPosesTogetherPerBone using a TArrayView for the Weights to avoid allocations
				FAnimationRuntime::BlendTwoPosesTogetherPerBone(FAnimationPoseData(Output), FAnimationPoseData(EvaluationPoseContext), Weights, BlendedAnimationPoseData);
			}
			else
			{
				const float Weight = 1.f - FAlphaBlend::AlphaToBlendOption(AnimPlayers[i].GetBlendInPercentage(), AnimPlayers[i].GetBlendOption());
				FAnimationRuntime::BlendTwoPosesTogether(FAnimationPoseData(Output), FAnimationPoseData(EvaluationPoseContext), Weight, BlendedAnimationPoseData);
			}
			Output = BlendedPoseContext; // @todo: this should not be necessary either: optimize it away!
		}
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
	}
}

float FAnimNode_BlendStack::GetAccumulatedTime() const
{
	return AnimPlayers.IsEmpty() ? 0.f : AnimPlayers.First().GetAccumulatedTime();
}

void FAnimNode_BlendStack::BlendTo(ESearchIndexAssetType AssetType, UAnimationAsset* AnimationAsset, float AccumulatedTime, bool bLoop, bool bMirrored, UMirrorDataTable* MirrorDataTable, int32 MaxActiveBlends, float BlendTime, const UBlendProfile* BlendProfile, EAlphaBlendOption BlendOption, FVector BlendParameters)
{
	AnimPlayers.PushFirst(FPoseSearchAnimPlayer());
	AnimPlayers.First().Initialize(AssetType, AnimationAsset, AccumulatedTime, bLoop, bMirrored, MirrorDataTable, BlendTime, BlendProfile, BlendOption, BlendParameters);

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