#include "TrajectoryCache.h"

namespace UE
{
	bool RangesContain(const TSet<TRange<double>>& Ranges, const double Time)
	{
		for (const TRange<double>& Range : Ranges)
		{
			if (Range.Contains(Time))
			{
				return true;
			}
		}
		return false;
	}
}

void FTrajectoryCache::UpdateCacheTimes(FTrailEvaluateTimes& InOutEvaluateTimes)
{
	if (!InOutEvaluateTimes.Spacing)
	{
		return;
	}

	const double Spacing = InOutEvaluateTimes.Spacing.GetValue();

	if (CoveredRanges.Num() == 0)
	{
		CoveredRanges.Add(InOutEvaluateTimes.Range);
		return;
	}

	TArray<TRange<double>> RangesToRemove;
	TRange<double> EvalRange = InOutEvaluateTimes.Range;
	TRange<double> HullRange = InOutEvaluateTimes.Range;
	for (const TRange<double>& CoveredRange : CoveredRanges)
	{
		if (EvalRange.Contains(CoveredRange.GetLowerBoundValue()))
		{
			RangesToRemove.Remove(CoveredRange);
			EvalRange.SetUpperBoundValue(CoveredRange.GetLowerBoundValue());
			HullRange.SetUpperBoundValue(CoveredRange.GetUpperBoundValue());
		}
		else if (EvalRange.Contains(CoveredRange.GetUpperBoundValue()))
		{
			RangesToRemove.Add(CoveredRange);
			EvalRange.SetLowerBoundValue(CoveredRange.GetUpperBoundValue());
			HullRange.SetLowerBoundValue(CoveredRange.GetLowerBoundValue());
		}
	}

	if (RangesToRemove.Num() > 0)
	{
		CoveredRanges.Add(HullRange);
	}

	for (const TRange<double>& RangeToRemove : RangesToRemove)
	{
		CoveredRanges.Remove(RangeToRemove);
	}

	const int32 BeginOff = int32((EvalRange.GetLowerBoundValue() - InOutEvaluateTimes.Range.GetLowerBoundValue()) / Spacing);
	const int32 EndOff = FMath::Max(int32((InOutEvaluateTimes.Range.GetUpperBoundValue() - EvalRange.GetUpperBoundValue()) / Spacing), 0);

	TArrayView<double> NewEvalTimes = InOutEvaluateTimes.EvalTimes.Slice(BeginOff, InOutEvaluateTimes.EvalTimes.Num() - EndOff - BeginOff);

	InOutEvaluateTimes = FTrailEvaluateTimes(NewEvalTimes, Spacing);
}

FTransform FArrayTrajectoryCache::GetInterp(const double InTime) const
{
	if (TrajectoryCache.Num() == 0)
	{
		return Default;
	}

	const double T = (InTime/Spacing) - FMath::FloorToDouble(InTime / Spacing);
	const int32 LowIdx = FMath::Clamp(int32((InTime - TrackRange.GetLowerBoundValue()) / Spacing), 0, TrajectoryCache.Num() - 1);
	const int32 HighIdx = FMath::Clamp(LowIdx + 1, 0, TrajectoryCache.Num() - 1);

	FTransform TempBlended; 
	TempBlended.Blend(TrajectoryCache[LowIdx], TrajectoryCache[HighIdx], T);
	return TempBlended;
}

TArray<double> FArrayTrajectoryCache::GetAllTimesInRange(const TRange<double>& InRange) const
{
	TRange<double> GenRange = TRange<double>::Intersection({ TrackRange, InRange });

	TArray<double> AllTimesInRange;
	AllTimesInRange.Reserve(int(GenRange.Size<double>() / Spacing) + 1);
	const double FirstTick = FMath::FloorToDouble((GenRange.GetLowerBoundValue()) / Spacing) * Spacing;
	for (double TickItr = FirstTick; TickItr < GenRange.GetUpperBoundValue(); TickItr += Spacing)
	{
		AllTimesInRange.Add(TickItr);
	}

	return AllTimesInRange;
}
