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
		, TileCount(1)
		, SpatialSampleCount(1)
		, bIsUsingOverlappedTiles(false)
		, PadRatioX(.50f)
		, PadRatioY(.50f)
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
	* How many tiles should the resulting movie render be broken into? A tile should be no larger than
	* the maximum texture resolution supported by your graphics card (likely 16k), so NumTiles should be
	* ceil(Width/MaxTextureSize). More tiles mean more individual passes over the whole scene at a smaller
	* resolution which may help with gpu timeouts. Requires at least 1 tile. Tiling is applied evenly to
	* both X and Y (to make final interlacing more straightforward).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 1, ClampMin = 1), Category = "Movie Pipeline")
	int32 TileCount;

	/**
	* This bias encourages the engine to use a higher detail texture when it would normally use a lower detail
	* texture due to the size of the texture on screen. A more negative number means overall sharper images
	* (up to the detail resolution of your texture). Too much sharpness will cause visual grain/noise in the
	* resulting image, but this can be mitigated with more spatial samples.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = -1, UIMax = 0), Category = "Movie Pipeline")
	float TextureSharpnessBias;

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
	* 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 1, ClampMin = 1), Category = "Movie Pipeline")
	bool bIsUsingOverlappedTiles;

	float PadRatioX;
	float PadRatioY;
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