// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/WidgetStyle.h"

enum class ESlateDrawEffect : uint8;

struct FDrawContext;
struct FGeometry;
struct FSlateBrush;

class FFrameTrackViewport;
class FSlateWindowElementList;

namespace Trace
{
	struct FFrame;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class EFrameType
{
	Unknown = -1,

	Game,
	Render
};

////////////////////////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FFrameTrackTimeline
{
	uint64 Id;
	//TODO: EFrameType Type;

	//TODO: double MinValue; // min value
	//TODO: double MaxValue; // max value

	//TODO: float SampleW; // width of a sample, in Slate units
	//TODO: int32 FramesPerSample; // number of frames in a sample
	//TODO: int32 FirstFrameIndex; // index of first frame in first sample; can be negative
	//TODO: int32 NumSamples; // total number of samples
	int32 NumAggregatedFrames; // total number of frames aggregated in samples; ie. sum of all Sample.NumFrames

	TArray<FFrameTrackSample> Samples;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFrameTrackTimelineBuilder
{
public:
	FFrameTrackTimelineBuilder(FFrameTrackTimeline& InTimeline, const FFrameTrackViewport& InViewport);

	/**
	 * Non-copyable
	 */
	FFrameTrackTimelineBuilder(const FFrameTrackTimelineBuilder&) = delete;
	FFrameTrackTimelineBuilder& operator=(const FFrameTrackTimelineBuilder&) = delete;

	void AddFrame(const Trace::FFrame& Frame);

	int32 GetNumAddedFrames() const { return NumAddedFrames; }

private:
	FFrameTrackTimeline& Timeline;
	const FFrameTrackViewport& Viewport;

	float SampleW; // width of a sample, in Slate units
	int32 FramesPerSample; // number of frames in a sample
	int32 FirstFrameIndex; // index of first frame in first sample; can be negative
	int32 NumSamples; // total number of samples

	// Debug stats.
	int32 NumAddedFrames; // counts total number of added frame events
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFrameTrackDrawHelper
{
public:
	FFrameTrackDrawHelper(const FDrawContext& InDrawContext, const FFrameTrackViewport& InViewport);

	/**
	 * Non-copyable
	 */
	FFrameTrackDrawHelper(const FFrameTrackDrawHelper&) = delete;
	FFrameTrackDrawHelper& operator=(const FFrameTrackDrawHelper&) = delete;

	void DrawBackground() const;
	void DrawCached(const FFrameTrackTimeline& Timeline) const;
	void DrawHoveredSample(const FFrameTrackSample& Sample) const;
	void DrawHighlightedInterval(const FFrameTrackTimeline& Timeline, const double StartTime, const double EndTime) const;

	FLinearColor GetColorById(int32 Id) const;

	int32 GetNumFrames() const { return NumFrames; }
	int32 GetNumDrawSamples() const { return NumDrawSamples; }

private:
	const FDrawContext& DrawContext;
	const FFrameTrackViewport& Viewport;

	const FSlateBrush* WhiteBrush;
	const FSlateBrush* BorderBrush;

	// Debug stats.
	mutable int32 NumFrames;
	mutable int32 NumDrawSamples;
};
