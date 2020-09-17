// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneDecompositionQuery.h"
#include "Algo/Accumulate.h"


namespace UE
{
namespace MovieScene
{

void FDecomposedFloat::Decompose(FMovieSceneEntityID EntityID, FWeightedFloat& ThisValue, bool& bOutIsAdditive, FWeightedFloat& Absolutes, FWeightedFloat& Additives) const
{
	for (TTuple<FMovieSceneEntityID, FWeightedFloat> Pair : DecomposedAbsolutes)
	{
		if (Pair.Get<0>() == EntityID)
		{
			ThisValue = Pair.Value;
			bOutIsAdditive = false;
		}
		else
		{
			Absolutes.Value += Pair.Value.Value * Pair.Value.Weight;
			Absolutes.Weight += Pair.Value.Weight;
		}
	}
	for (TTuple<FMovieSceneEntityID, FWeightedFloat> Pair : DecomposedAdditives)
	{
		if (Pair.Get<0>() == EntityID)
		{
			ThisValue = Pair.Value;
			bOutIsAdditive = true;
		}
		else
		{
			Additives.Value += Pair.Value.Value * Pair.Value.Weight;
			Additives.Weight += Pair.Value.Weight;
		}
	}
}

float FDecomposedFloat::Recompose(FMovieSceneEntityID RecomposeEntity, float CurrentValue, const float* InitialValue) const
{
	FWeightedFloat DecomposedAbsolute;
	FWeightedFloat DecomposedAdditive;

	FWeightedFloat Channel;
	bool bIsAdditive = false;
	Decompose(RecomposeEntity, Channel, bIsAdditive, DecomposedAbsolute, DecomposedAdditive);

	FWeightedFloat ResultAbsolute = Result.Absolute;
	float TotalAbsoluteWeight = ResultAbsolute.Weight + DecomposedAbsolute.Weight;
	if (!bIsAdditive)
	{
		TotalAbsoluteWeight += Channel.Weight;
	}
	if (TotalAbsoluteWeight < 1.f && InitialValue != nullptr)
	{
		const float InitialValueWeight = (1.f - TotalAbsoluteWeight);
		ResultAbsolute.Value = (*InitialValue) * InitialValueWeight + ResultAbsolute.WeightedValue();
		ResultAbsolute.Weight = 1.f;
	}

	// If this channel is the only thing we decomposed, that is simple
	if (DecomposedAbsolute.Weight == 0.f && DecomposedAdditive.Weight == 0.f)
	{
		if (bIsAdditive)
		{
			const float WeightedAdditiveResult = CurrentValue - ResultAbsolute.Combine(DecomposedAbsolute).WeightedValue() - Result.Additive;
			return Channel.Weight == 0.f ? WeightedAdditiveResult : WeightedAdditiveResult / Channel.Weight;
		}
		else
		{
			if (Channel.Weight != 0.f)
			{
				const float TotalWeight = Channel.Weight + ResultAbsolute.Weight;
				const float WeightedAbsoluteResult = CurrentValue - Result.Additive - ResultAbsolute.Value / TotalWeight;
				return WeightedAbsoluteResult * TotalWeight / Channel.Weight;
			}
			else
			{
				return CurrentValue - Result.Additive - ResultAbsolute.WeightedValue();
			}
		}
	}

	// If the channel had no weight, we can't recompose it - everything else will get the full weighting
	if (Channel.Weight == 0.f)
	{
		return Channel.Value;
	}

	if (bIsAdditive)
	{
		CurrentValue -= ResultAbsolute.Combine(DecomposedAbsolute).WeightedValue();

		const float ThisAdditive = Channel.WeightedValue();
		if (ThisAdditive == 0.f && DecomposedAdditive.WeightedValue() == 0.f)
		{
			const float TotalAdditiveWeight = DecomposedAdditive.Weight + Channel.Weight;
			return CurrentValue * Channel.Weight / TotalAdditiveWeight;
		}

		// Use the fractions of the values for the recomposition if we have non-zero values
		const float DecoposeFactor = ThisAdditive / (DecomposedAdditive.WeightedValue() + ThisAdditive);
		return CurrentValue * DecoposeFactor / Channel.Weight;
	}
	else if (DecomposedAdditives.Num() != 0)
	{
		// Absolute channel, but we're keying additives, put the full weight to the additives
		return Channel.Value;
	}
	else
	{
		const float TotalDecomposedWeight = DecomposedAbsolute.Weight + Channel.Weight;
		CurrentValue -= Result.Additive;
		CurrentValue *= ResultAbsolute.Weight + TotalDecomposedWeight;
		CurrentValue -= ResultAbsolute.Value;

		const float AbsValue = Algo::Accumulate(DecomposedAbsolutes, 0.f, [](float Accum, TTuple<FMovieSceneEntityID, FWeightedFloat> In) { return Accum + FMath::Abs(In.Value.Value)*In.Value.Weight; });
		if (AbsValue != 0.f)
		{
			return ((CurrentValue * FMath::Abs(Channel.Value) * Channel.Weight / AbsValue) - Channel.Value) / Channel.Weight;
		}
		else if (TotalDecomposedWeight == 0.f)
		{
			return Channel.Value;
		}
		return (CurrentValue * Channel.Weight / TotalDecomposedWeight) / Channel.Weight;
	}

	return Channel.Value;
}

} // namespace MovieScene
} // namespace UE


