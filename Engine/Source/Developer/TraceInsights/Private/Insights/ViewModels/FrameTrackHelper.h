// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/WidgetStyle.h"

enum class ESlateDrawEffect : uint8;

struct FGeometry;
struct FSlateBrush;

class FFrameTrackViewport;
class FSlateWindowElementList;

namespace Trace
{
	struct FFrame;
}

enum EFrameType
{
	Unknown = -1,

	Game,
	Render
};

struct FFrameTrackSample
{
	int32 NumFrames;
	double TotalDuration; // sum of durations of all frames in this sample
	double StartTime; // min start time of all frames in this sample
	double EndTime; // max end time of all frames in this sample
	double LargestFrameIndex; // index of the largest frame
	double LargestFrameStartTime; // start time of the largest frame
	double LargestFrameDuration; // duration of the largest frame

	FFrameTrackSample()
		: NumFrames(0)
		, TotalDuration(0.0)
		, StartTime(DBL_MAX)
		, EndTime(-DBL_MAX)
		, LargestFrameIndex(0)
		, LargestFrameStartTime(0.0)
		, LargestFrameDuration(0.0)
	{}
};

struct FFrameTrackTimeline
{
	uint64 Id;
	//EFrameType Type;

	//double MinValue; // min value
	//double MaxValue; // max value
	//double AvgValue; // average value

	//float SampleW; // width of a sample, in Slate units
	//int FramesPerSample; // number of frames in a sample
	//int FirstFrameIndex; // index of first frame in first sample; can be negative
	//int NumSamples; // total number of samples
	int NumAggregatedFrames; // total number of frames aggregated in samples; ie. sum of all Sample.NumFrames

	TArray<FFrameTrackSample> Samples;
};

class FFrameTrackCacheContext : public FNoncopyable
{
public:
	FFrameTrackCacheContext(const FFrameTrackViewport& InViewport);
	//~FFrameTrackCacheContext();

	void Begin();
	bool BeginTimeline(FFrameTrackTimeline& CachedTimeline);
	void AddEvent(const Trace::FFrame& Event);
	void EndTimeline();
	void End();

public:
	const FFrameTrackViewport& Viewport;

	FFrameTrackTimeline* CurrentCachedTimeline; // pointer to current FFrameTrackTimeline; valid only between BeginTimeline - EndTimeline calls.

	float SampleW; // width of a sample, in Slate units
	int FramesPerSample; // number of frames in a sample
	int FirstFrameIndex; // index of first frame in first sample; can be negative
	int NumSamples; // total number of samples

	// Debug stats.
	int NumFrames; // number of added frames
};

class FFrameTrackDrawContext : public FNoncopyable
{
public:
	FFrameTrackDrawContext(const FFrameTrackViewport& InViewport, const FGeometry& InAllottedGeometry, int32& InLayerId, FSlateWindowElementList& InElementList);
	//~FFrameTrackDrawContext();

	void DrawBackground(const FWidgetStyle& InWidgetStyle) const;
	void DrawCached(const FFrameTrackTimeline& CachedTimeline) const;
	void DrawHoveredSample(const FFrameTrackSample& Sample) const;
	void DrawHighlightedInterval(const FFrameTrackTimeline& CachedTimeline, const double StartTime, const double EndTime) const;

	FLinearColor GetColorById(int Id) const;

public:
	const FFrameTrackViewport& Viewport;
	const FGeometry& AllottedGeometry;
	int32& LayerId;
	FSlateWindowElementList& ElementList;
	const ESlateDrawEffect DrawEffects;

	const FSlateBrush* WhiteBrush;
	const FSlateBrush* BorderBrush;

	// Debug stats.
	mutable int NumFrames;
	mutable int NumDrawSamples;
};
