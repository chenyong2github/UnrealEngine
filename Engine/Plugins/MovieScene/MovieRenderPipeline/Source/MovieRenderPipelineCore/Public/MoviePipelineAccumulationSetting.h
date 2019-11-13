// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineAccumulationSetting.generated.h"


UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipelineAccumulationSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()
public:
	UMoviePipelineAccumulationSetting()
		: TemporalSampleCount(1)
		, CameraShutterAngle(180)
		, SpatialSampleCount(1)
		, AccumulationGamma(1.f)
	{
	}
	
	/** 
	* The number of frames we should combine together to produce each output frame. This blends the
	* results of this many sub-steps together to produce one output frame. See CameraShutterAngle to
	* control how much time passes between each sub-frame. See SpatialSampleCount to see how many
	* samples we average together to produce a sub-step. (This means rendering complexity is
	* SampleCount * TileCount^2 * SpatialSampleCount * NumPasses).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin = 1, ClampMin = 1), Category = "Movie Pipeline")
	int32 TemporalSampleCount;
	
	/** 
	* The camera shutter angle determines how much of a given frame the accumulation frames span.
	* For example, a 24fps film with a shutter angle of 180 will spread the accumulation frames
	* out over (180/360) percent of the frame (1/24th of a second). With 2 accumulation frames,
	* the first one will be at the very start of the frame and the second accumulation frame will
	* be land on the half way point between the start of this frame and the start of the next.
	*
	* A shutter angle of 360 means continuous movement, while a shutter angle of zero means no
	* motion blur.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 0, UIMax = 360, ClampMin = 0, ClampMax = 360), Category = "Movie Pipeline")
	int32 CameraShutterAngle;

	/**
	* How many frames should we accumulate together before contributing to one overall sample. This lets you
	* increase the anti-aliasing quality of an sample, or have high quality anti-aliasing if you don't want
	* any motion blur due to accumulation over time in SampleCount.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 1, ClampMin = 1), Category = "Movie Pipeline")
	int32 SpatialSampleCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 1, ClampMin = 1), Category = "Movie Pipeline")
	int32 HandleFrameCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 1, ClampMin = 1), Category = "Movie Pipeline")
	int32 WarmUpFrameCount;

	/**
	* For advanced users, the gamma space to apply accumulation in. During accumulation, pow(x,AccumulationGamma) 
	* is applied and pow(x,1/AccumulationGamma) is applied after accumulation is finished
	*/
	float AccumulationGamma;

	/**
	* Shutter Timing allows you to bias the timing of your shutter angle to either be before, during, or after
	* a frame. When set to FrameClose, it means that the motion gathered up to produce frame N comes from 
	* before and right up to frame N. When set to FrameCenter, the motion represents half the time before the
	* frame and half the time after the frame. When set to FrameOpen, the motion represents the time from 
	* Frame N onwards.
	*/
	EMoviePipelineShutterTiming ShutterTiming;
};