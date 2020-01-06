// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineCameraSetting.generated.h"


UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipelineCameraSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()
public:
	UMoviePipelineCameraSetting()
		: TemporalSampleCount(1)
		, CameraShutterAngle(180)
		, ShutterTiming(EMoviePipelineShutterTiming::FrameCenter)
		, bManualExposure(false)
		, ExposureCompensation(8.0)
	{}

public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "CameraSettingDisplayName", "Camera"); }
#endif
protected:
	virtual bool IsValidOnShots() const override { return true; }
	virtual bool IsValidOnMaster() const override { return true; }

public:

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
	* Shutter Timing allows you to bias the timing of your shutter angle to either be before, during, or after
	* a frame. When set to FrameClose, it means that the motion gathered up to produce frame N comes from 
	* before and right up to frame N. When set to FrameCenter, the motion represents half the time before the
	* frame and half the time after the frame. When set to FrameOpen, the motion represents the time from 
	* Frame N onwards.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 0, UIMax = 360, ClampMin = 0, ClampMax = 360), Category = "Movie Pipeline")
	EMoviePipelineShutterTiming ShutterTiming;
	
	/**
	* Should we override the exposure on the camera to use a fixed value? This is required for high res screenshots to use consistent
	* exposure between the different tiles of the image. Leaving this off lets the camera determine based on the previous frame rendered,
	* which can require long warm up times between shots to allow the exposure to settle.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 0, UIMax = 360, ClampMin = 0, ClampMax = 360), Category = "Movie Pipeline")
	bool bManualExposure;
	
	/**
	* What exposure should we use when using Manual Exposure? Same behavior as the Post Processing volume.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = -10, UIMax = 10, EditCondition="bManualExposure"), Category = "Movie Pipeline")
	float ExposureCompensation;
};