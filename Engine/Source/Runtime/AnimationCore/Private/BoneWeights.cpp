// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneWeights.h"

#include "Algo/IsSorted.h"
#include "Misc/MemStack.h"


static auto WeightSortPredicate = [](const FBoneWeight& A, const FBoneWeight& B) {
	return A.GetRawWeight() > B.GetRawWeight();
};


bool FBoneWeights::SetBoneWeight(
    FBoneWeight InBoneWeight,
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	// Does this bone already exist?
	int32 WeightIndex = FindWeightIndexByBone(InBoneWeight.GetBoneIndex());

	// If the sum of weights could possibly exceed 1.0, we may need normalization based on
	// the weight settings.
	bool bMayNeedNormalization = false;

	if (WeightIndex != INDEX_NONE)
	{
		FBoneWeight& ExistingBoneWeight = BoneWeights[WeightIndex];
		// New weight is below the threshold. Remove the current bone weight altogether.
		if (InBoneWeight.GetRawWeight() < InSettings.GetRawWeightThreshold())
		{
			BoneWeights.RemoveAt(WeightIndex);

			// If always normalizing, we need to re-normalize after removing this entry.
			if (InSettings.GetNormalizeType() == EBoneWeightNormalizeType::Always)
			{
				NormalizeWeights(EBoneWeightNormalizeType::Always);
			}

			return false;
		}

		if (ExistingBoneWeight.GetRawWeight() == InBoneWeight.GetRawWeight())
		{
			return true;
		}

		bMayNeedNormalization = (ExistingBoneWeight.GetRawWeight() < InBoneWeight.GetRawWeight());

		ExistingBoneWeight.SetRawWeight(InBoneWeight.GetRawWeight());
	}
	else
	{
		// If the new weight is below the threshold, reject and return.
		if (InBoneWeight.GetRawWeight() < InSettings.GetRawWeightThreshold())
		{
			return false;
		}

		// Are we already at the limit of weights for this container?
		if (BoneWeights.Num() == InSettings.GetMaxWeightCount())
		{
			// If the weight is smaller than the smallest weight currently, then we reject.
			if (InBoneWeight.GetRawWeight() < BoneWeights.Last().GetRawWeight())
			{
				return false;
			}

			// Overwrite the last one, we'll put it in its correct place when we sort.
			BoneWeights.Last() = InBoneWeight;
		}
		else
		{
			BoneWeights.Add(InBoneWeight);
		}

		bMayNeedNormalization = true;
	}

	// If we got here, then we updated/added weights. We're contractually obligated to keep the
	// weights sorted.
	SortWeights();

	if ((InSettings.GetNormalizeType() == EBoneWeightNormalizeType::Always) ||
	    (InSettings.GetNormalizeType() == EBoneWeightNormalizeType::AboveOne && bMayNeedNormalization))
	{
		Renormalize(InSettings);
	}

	return true;
}


bool FBoneWeights::RemoveBoneWeight(
    FBoneIndexType InBoneIndex,
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	int32 WeightIndex = FindWeightIndexByBone(InBoneIndex);
	if (WeightIndex == INDEX_NONE)
	{
		return false;
	}

	BoneWeights.RemoveAt(WeightIndex);

	// Cull all weights that exceed limits set by the settings.
	CullWeights(InSettings);

	// Removing weights will always cause the weight sum to decrease, so we only have to normalize
	// if always asked to.
	if (InSettings.GetNormalizeType() == EBoneWeightNormalizeType::Always)
	{
		NormalizeWeights(EBoneWeightNormalizeType::Always);
	}

	return true;
}


void FBoneWeights::Renormalize(const FBoneWeightsSettings& InSettings /*= {}*/)
{
	NormalizeWeights(InSettings.GetNormalizeType());

	// If entries are now below the threshold, remove them.
	if (InSettings.GetNormalizeType() == EBoneWeightNormalizeType::Always && CullWeights(InSettings))
	{
		NormalizeWeights(EBoneWeightNormalizeType::Always);
	}
}


FBoneWeights FBoneWeights::Create(
    const FBoneIndexType InBones[MaxInlineBoneWeightCount],
    const uint8 InInfluences[MaxInlineBoneWeightCount],
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	FBoneWeights Result;
	Result.BoneWeights.Reserve(MaxInlineBoneWeightCount);

	for (int32 Index = 0; Index < MaxInlineBoneWeightCount && InInfluences[Index]; Index++)
	{
		FBoneWeight BW(InBones[Index], InInfluences[Index]);
		if (BW.GetRawWeight() >= InSettings.GetRawWeightThreshold())
		{
			Result.BoneWeights.Add(BW);
		}
	}

	// Sort the weights by descending weight value before we clip it.
	Result.SortWeights();

	if (Result.BoneWeights.Num() > InSettings.GetMaxWeightCount())
	{
		Result.BoneWeights.SetNum(InSettings.GetMaxWeightCount());
	}

	Result.NormalizeWeights(InSettings.GetNormalizeType());

	return Result;
}


FBoneWeights FBoneWeights::Create(
    const FBoneIndexType* InBones,
    const float* InInfluences,
    int32 NumEntries,
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	TArray<FBoneWeight, BoneWeightsTempAllocatorT> StackBoneWeights;

	StackBoneWeights.Reserve(NumEntries);
	for (int32 Index = 0; Index < NumEntries; Index++)
	{
		FBoneWeight BW(InBones[Index], InInfluences[Index]);
		if (BW.GetRawWeight() >= InSettings.GetRawWeightThreshold())
		{
			StackBoneWeights.Add(BW);
		}
	}

	return CreateFromArrayView(StackBoneWeights, InSettings);
}


FBoneWeights FBoneWeights::Create(TArrayView<const FBoneWeight> BoneWeights, const FBoneWeightsSettings& InSettings /*= {} */)
{
	TArray<FBoneWeight, BoneWeightsTempAllocatorT> StackBoneWeights;

	StackBoneWeights.Reserve(BoneWeights.Num());
	for (const FBoneWeight& BW : BoneWeights)
	{
		if (BW.GetRawWeight() >= InSettings.GetRawWeightThreshold())
		{
			StackBoneWeights.Add(BW);
		}
	}

	return CreateFromArrayView(StackBoneWeights, InSettings);
}


FBoneWeights FBoneWeights::CreateFromArrayView(
    TArrayView<FBoneWeight> BoneWeights,
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	BoneWeights.Sort(WeightSortPredicate);

	int32 NumEntries = FMath::Min(BoneWeights.Num(), InSettings.GetMaxWeightCount());
	FBoneWeights Result;
	if (NumEntries)
	{
		Result.BoneWeights = BoneWeights.Slice(0, NumEntries);
	}
	Result.NormalizeWeights(InSettings.GetNormalizeType());

	return Result;
}


FBoneWeights FBoneWeights::Blend(
    const FBoneWeights& InA,
    const FBoneWeights& InB,
    float InBias,
    const FBoneWeightsSettings& InSettings /*= {} */
)
{
	checkSlow(Algo::IsSorted(InA.BoneWeights, WeightSortPredicate));
	checkSlow(Algo::IsSorted(InB.BoneWeights, WeightSortPredicate));

	// Both empty?
	if (InA.BoneWeights.Num() == 0 && InA.BoneWeights.Num() == 0)
	{
		return {};
	}
	// FIXME: We can probably special-case a few more fast paths. But let's collect statistics
	// first.

	// To simplify lookup and iteration over the two bone weight arrays, we sort by bone index
	// value, but indirectly, since we can't sort them directly, as that would violate the
	// sorted-by-descending-weight contract. Instead we create an indirection array on the stack
	// and use that to iterate
	auto CreateIndirectIndex = [](TArrayView<const FBoneWeight> BoneWeights, TArrayView<int32> IndexIndirect) {
		for (int32 Index = 0; Index < IndexIndirect.Num(); Index++)
		{
			IndexIndirect[Index] = Index;
		}
		IndexIndirect.Sort([BoneWeights](int32 A, int32 B) {
			return BoneWeights[A].GetBoneIndex() < BoneWeights[B].GetBoneIndex();
		});
	};

	TArray<int32, BoneWeightsTempAllocatorT> IndirectIndexA;
	IndirectIndexA.SetNumUninitialized(InA.BoneWeights.Num());
	CreateIndirectIndex(InA.BoneWeights, IndirectIndexA);

	TArray<int32, BoneWeightsTempAllocatorT> IndirectIndexB;
	IndirectIndexB.SetNumUninitialized(InB.BoneWeights.Num());
	CreateIndirectIndex(InB.BoneWeights, IndirectIndexB);

	TArray<FBoneWeight, TInlineAllocator<MaxInlineBoneWeightCount * 2>> BoneWeights;
	BoneWeights.Reserve(InA.BoneWeights.Num() + InB.BoneWeights.Num());

	int32 RawBiasB = int32(InBias * float(FBoneWeight::MaxRawWeight));
	int32 RawBiasA = FBoneWeight::MaxRawWeight - RawBiasB;

	int32 IndexA = 0, IndexB = 0;
	for (; IndexA < InA.BoneWeights.Num() && IndexB < InB.BoneWeights.Num(); /**/)
	{
		const FBoneWeight& BWA = InA.BoneWeights[IndirectIndexA[IndexA]];
		const FBoneWeight& BWB = InB.BoneWeights[IndirectIndexB[IndexB]];

		// If both have the same bone index, we blend them using the bias given and advance
		// both arrays. If the bone indices differ, we copy from the array with the lower bone
		// index value, to ensure we can possibly catch up with the other array. We then
		// advance until we hit the end of either array after which we blindly copy the remains.
		if (BWA.GetBoneIndex() == BWB.GetBoneIndex())
		{
			uint16 RawWeight = (BWA.GetRawWeight() * RawBiasA + BWB.GetRawWeight() * RawBiasB) / FBoneWeight::MaxRawWeight;

			BoneWeights.Emplace(BWA.GetBoneIndex(), RawWeight);
			IndexA++;
			IndexB++;
		}
		else if (BWA.GetBoneIndex() < BWB.GetBoneIndex())
		{
			BoneWeights.Add(BWA);
			IndexA++;
		}
		else
		{
			BoneWeights.Add(BWB);
			IndexB++;
		}
	}

	for (; IndexA < InA.BoneWeights.Num(); IndexA++)
	{
		BoneWeights.Add(InA.BoneWeights[IndirectIndexA[IndexA]]);
	}
	for (; IndexB < InB.BoneWeights.Num(); IndexB++)
	{
		BoneWeights.Add(InB.BoneWeights[IndirectIndexB[IndexB]]);
	}

	return CreateFromArrayView(BoneWeights, InSettings);
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
			BoneWeights[0].SetRawWeight(FBoneWeight::MaxRawWeight);
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
	    WeightSum > FBoneWeight::MaxRawWeight)
	{
		int64 Correction = 0;

		// Here we treat the raw weight as a 16.16 fixed point value and ensure that the
		// fraction, which would otherwise be lost through rounding, is carried over to the 
		// subsequent values to maintain a constant sum to the max weight value.
		// We do this in descending weight order in an attempt to ensure that weight values 
		// aren't needlessly lost after scaling.
		for (FBoneWeight& BW : BoneWeights)
		{
			int64 ScaledWeight = int64(BW.GetRawWeight()) * FBoneWeight::MaxRawWeight + Correction;
			BW.SetRawWeight(uint16(FMath::Min(ScaledWeight / WeightSum, int64(FBoneWeight::MaxRawWeight))));
			Correction = ScaledWeight - BW.GetRawWeight() * WeightSum;
		}
	}
}
