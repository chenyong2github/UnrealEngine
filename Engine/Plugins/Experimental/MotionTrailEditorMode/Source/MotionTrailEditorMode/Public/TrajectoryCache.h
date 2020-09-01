// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Misc/Optional.h"
#include "Math/Range.h"
#include "Math/Transform.h"

namespace UE
{
namespace MotionTrailEditor
{

bool RangesContain(const TSet<TRange<double>>& Ranges, const double Time);


// Intermediate range reprsentation structure
class FTrailEvaluateTimes
{
public:
	FTrailEvaluateTimes()
		: EvalTimes()
		, Spacing()
		, Range(TRange<double>::Empty())
	{}

	FTrailEvaluateTimes(TArrayView<double> InEvalTimes, TOptional<double> InSpacing) 
		: EvalTimes(InEvalTimes)
		, Spacing(InSpacing)
		, Range(TRange<double>(InEvalTimes[0], InEvalTimes.Last()))
	{}

	TArrayView<double> EvalTimes;
	TOptional<double> Spacing;
	TRange<double> Range;
};

class FTrajectoryCache
{
public:
	FTrajectoryCache()
		: CoveredRanges()
		, Default(FTransform::Identity)
	{}

	FTrajectoryCache(const FTransform& InDefault)
		: CoveredRanges()
		, Default(InDefault)
	{}

	virtual ~FTrajectoryCache() {}

	// alternatively, GetEvaluateTimes(const FTrailEvaluateTimes&) and GetInterpEvaluateTimes(const FTrailEvaluateTimes&)
	virtual const FTransform& Get(const double InTime) const = 0;
	virtual FTransform GetInterp(const double InTime) const = 0;
	virtual TArray<double> GetAllTimesInRange(const TRange<double>& InRange) const = 0;
	
	// And SetEvaluateTimes(const FTrailEvaluateTimes&, const TArray<FTransform>&)
	virtual void Set(const double InTime, const FTransform& InValue) = 0;

	// Optionally implemented
	virtual const FTransform& GetDefault() const { return Default; };
	virtual void UpdateCacheTimes(FTrailEvaluateTimes& InOutEvaluateTimes);

protected:
	TSet<TRange<double>> CoveredRanges;
	FTransform Default;
};

class FArrayTrajectoryCache : public FTrajectoryCache
{
public:

	// for FRootTrail
	FArrayTrajectoryCache()
		: FTrajectoryCache()
		, TrajectoryCache()
		, TrackRange()
		, Spacing()
	{}

	FArrayTrajectoryCache(const double InSpacing, const TRange<double>& InTrackRange, FTransform InDefault = FTransform::Identity)
		: FTrajectoryCache(InDefault)
		, TrajectoryCache()
		, TrackRange(TRange<double>(FMath::FloorToDouble(InTrackRange.GetLowerBoundValue() / InSpacing) * InSpacing, FMath::FloorToDouble(InTrackRange.GetUpperBoundValue() / InSpacing) * InSpacing)) // snap to spacing
		, Spacing(InSpacing)
	{
		TrajectoryCache.SetNumUninitialized(int32(InTrackRange.Size<double>() / InSpacing) + 1);
	}

	virtual const FTransform& Get(const double InTime) const override 
	{ 
		return TrajectoryCache.Num() > 0 ? TrajectoryCache[FMath::Clamp(int32((InTime - TrackRange.GetLowerBoundValue()) / Spacing), 0, TrajectoryCache.Num() - 1)] : Default; 
	}

	virtual FTransform GetInterp(const double InTime) const override;
	virtual TArray<double> GetAllTimesInRange(const TRange<double>& InRange) const override;

	virtual void Set(const double InTime, const FTransform& InValue) override 
	{ 
		if (TrajectoryCache.Num() > 0)
		{
			TrajectoryCache[FMath::Clamp(int32((InTime - TrackRange.GetLowerBoundValue()) / Spacing), 0, TrajectoryCache.Num() - 1)] = InValue;
		}
	}

	const TRange<double>& GetTrackRange() const { return TrackRange; }

private:
	TArray<FTransform> TrajectoryCache;
	TRange<double> TrackRange;
	double Spacing;
};

} // namespace MovieScene
} // namespace UE
