// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Graph/MovieGraphDataTypes.h"
#include "Misc/FrameRate.h"
#include "Misc/FrameTime.h"
#include "MovieGraphLinearTimeStep.generated.h"

/**
* This class is responsible for calculating the time step of each tick of the engine during a
* Movie Render Queue render using a linear strategy - the number of temporal sub-samples is
* read from the graph for each output frame, and we then take the time the shutter is open
* and break it into that many sub-samples. Time always advances forward until we reach the
* end of the range of time we wish to render. This is useful for deferred rendering (where 
* we have a small number of temporal sub-samples and no feedback mechanism for measuring
* noise in the final image) but is less useful for Path Traced images which have a varying
* amount of noise (and thus want a varying amount of samples) based on their content.
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMovieGraphLinearTimeStep : public UMovieGraphTimeStepBase
{
	GENERATED_BODY()
public:
	virtual void TickProducingFrames() override;
	virtual FMovieGraphTimeStepData GetCalculatedTimeData() const override { return CurrentTimeStepData; }

protected:
	virtual void UpdateFrameMetrics();
	virtual float GetBlendedMotionBlurAmount();

protected:
	/** This is the output data needed by the rest of MRQ to produce a frame. */
	FMovieGraphTimeStepData CurrentTimeStepData;

	struct FFrameData
	{
		FFrameData()
			: MotionBlurAmount(0.f)
		{}
		
		/** The human readable frame rate, adjusted by the config file (24fps, 30fps, etc.) */
		FFrameRate FrameRate;

		/** The internal resolution data is actually stored at. (24,000, 120,000 etc.) */
		FFrameRate TickResolution;

		/** The amount of time (in Tick Resolution) that represents one output frame (ie: 1/24). */
		FFrameTime FrameTimePerOutputFrame;

		/** The 0-1 Motion Blur amount the camera has. */
		float MotionBlurAmount;

		/** The amount of time (in Tick Resolution) that the shutter is open for. Could be <= FrameTimePerOutputFrame. */
		FFrameTime FrameTimeWhileShutterOpen;

		/** 
		* The amount of time (in Tick Resolution) per temporal sample. The actual time slice for a temporal sub-sample
		* may be different than this (as we have to jump over the time period that the shutter is closed too for one 
		* temporal sub-sample.
		*/
		FFrameTime FrameTimePerTemporalSample;
		
		/** The inverse of FrameTimeWhileShutterOpen. ShutterClosed+ShutterOpen should add up to FrameTimePerOutputFrame. */
		FFrameTime FrameTimeWhileShutterClosed;

		/** A constant offset applied to the final evaluated time to allow users to influence what counts as a frame. (the time before, during, or after a frame) */
		FFrameTime ShutterOffsetFrameTime;

		/** An offset to the evaluated time to get us into the center of the time period (since motion blur is bi-directional and centered) */
		FFrameTime MotionBlurCenteringOffsetTime;
	};

	/**
	* A set of cached values that are true for the current output frame. They will be recalculated
	* at the start of the next output frame.
	*/
	FFrameData CurrentFrameMetrics;
};
