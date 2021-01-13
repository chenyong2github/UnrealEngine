// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneWeights.h"

#include "Misc/MemStack.h"


namespace UE 
{
namespace AnimationCore
{

static auto WeightSortPredicate = [](const FBoneWeight& A, const FBoneWeight& B) {
	return A.GetRawWeight() > B.GetRawWeight();
};


bool FBoneWeights::SetBoneWeight(
    FBoneWeight InBoneWeight,
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	FArrayWrapper W(BoneWeights);
	return W.AddBoneWeight(InBoneWeight, InSettings);
}


bool FBoneWeights::RemoveBoneWeight(
    FBoneIndexType InBoneIndex,
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	FArrayWrapper W(BoneWeights);
	return W.RemoveBoneWeight(InBoneIndex, InSettings);
}


void FBoneWeights::Renormalize(const FBoneWeightsSettings& InSettings /*= {}*/)
{
	FArrayWrapper W(BoneWeights);
	return W.Renormalize(InSettings);
}


FBoneWeights FBoneWeights::Create(
    const FBoneIndexType InBones[MaxInlineBoneWeightCount],
    const uint8 InInfluences[MaxInlineBoneWeightCount],
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	FBoneWeights Result;
	FArrayWrapper W(Result.BoneWeights);
	W.SetBoneWeights(InBones, InInfluences, InSettings);
	return Result;
}


FBoneWeights FBoneWeights::Create(
    const FBoneIndexType* InBones,
    const float* InInfluences,
    int32 NumEntries,
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	FBoneWeights Result;
	FArrayWrapper W(Result.BoneWeights);
	W.SetBoneWeights(InBones, InInfluences, NumEntries, InSettings);
	return Result;
}


FBoneWeights FBoneWeights::Create(
	TArrayView<const FBoneWeight> InBoneWeights, 
	const FBoneWeightsSettings& InSettings /*= {} */
)
{
	FBoneWeights Result;
	FArrayWrapper(Result.BoneWeights).SetBoneWeights(InBoneWeights, InSettings);
	return Result;
}


FBoneWeights FBoneWeights::Blend(
    const FBoneWeights& InA,
    const FBoneWeights& InB,
    float InBias,
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	FBoneWeights Result;
	FArrayWrapper W(Result.BoneWeights);
	FArrayWrapper A(const_cast<FBoneWeights&>(InA).BoneWeights);
	FArrayWrapper B(const_cast<FBoneWeights&>(InB).BoneWeights);
	W.Blend(A, B, InBias, InSettings);
	return Result;
}


void FBoneWeights::SortWeights()
{
	BoneWeights.Sort(WeightSortPredicate);
}


bool FBoneWeights::CullWeights(const FBoneWeightsSettings& InSettings)
{
	bool bCulled = false;
	if (BoneWeights.Num() > InSettings.GetMaxWeightCount())
	{
		BoneWeights.SetNum(InSettings.GetMaxWeightCount(), false);
		bCulled = true;
	}

	// If entries are now below the threshold, remove them.
	while (BoneWeights.Num() > 0 && BoneWeights.Last().GetRawWeight() < InSettings.GetRawWeightThreshold())
	{
		BoneWeights.SetNum(BoneWeights.Num() - 1, false);
		bCulled = true;
	}

	return bCulled;
}


void FBoneWeights::NormalizeWeights(
    EBoneWeightNormalizeType InNormalizeType)
{
	// Early checks
	if (InNormalizeType == EBoneWeightNormalizeType::None || BoneWeights.Num() == 0)
	{
		return;
	}

	// Common case.
	if (BoneWeights.Num() == 1)
	{
		if (InNormalizeType == EBoneWeightNormalizeType::Always)
		{
			BoneWeights[0].SetRawWeight(FBoneWeight::GetMaxRawWeight());
		}
		return;
	}

	// We operate on int64, since we can easily end up with wraparound issues during one of the
	// multiplications below when using int32. This would tank the division by WeightSum.
	int64 WeightSum = 0;
	for (const FBoneWeight& BW : BoneWeights)
	{
		WeightSum += BW.GetRawWeight();
	}

	if ((InNormalizeType == EBoneWeightNormalizeType::Always && ensure(WeightSum != 0)) ||
	    WeightSum > FBoneWeight::GetMaxRawWeight())
	{
		int64 Correction = 0;

		// Here we treat the raw weight as a 16.16 fixed point value and ensure that the
		// fraction, which would otherwise be lost through rounding, is carried over to the 
		// subsequent values to maintain a constant sum to the max weight value.
		// We do this in descending weight order in an attempt to ensure that weight values 
		// aren't needlessly lost after scaling.
		for (FBoneWeight& BW : BoneWeights)
		{
			int64 ScaledWeight = int64(BW.GetRawWeight()) * FBoneWeight::GetMaxRawWeight() + Correction;
			BW.SetRawWeight(uint16(FMath::Min(ScaledWeight / WeightSum, int64(FBoneWeight::GetMaxRawWeight()))));
			Correction = ScaledWeight - BW.GetRawWeight() * WeightSum;
		}
	}
}

} // namespace AnimationCore
} // namespace UE
