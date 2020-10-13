// Copyright Epic Games, Inc. All Rights Reserved.
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
		: ShutterTiming(EMoviePipelineShutterTiming::FrameCenter)
	{}

public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "CameraSettingDisplayName", "Camera"); }
#endif
protected:
	virtual bool IsValidOnShots() const override { return true; }
	virtual bool IsValidOnMaster() const override { return true; }
	
	virtual void GetFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const override
	{
		Super::GetFormatArguments(InOutFormatArgs);

		InOutFormatArgs.FilenameArguments.Add(TEXT("shutter_timing"), StaticEnum<EMoviePipelineShutterTiming>()->GetNameStringByValue((int64)ShutterTiming));
		InOutFormatArgs.FileMetadata.Add(TEXT("unreal/camera/shutterTiming"), StaticEnum<EMoviePipelineShutterTiming>()->GetNameStringByValue((int64)ShutterTiming));
	}
public:	
	/**
	* Shutter Timing allows you to bias the timing of your shutter angle to either be before, during, or after
	* a frame. When set to FrameClose, it means that the motion gathered up to produce frame N comes from 
	* before and right up to frame N. When set to FrameCenter, the motion represents half the time before the
	* frame and half the time after the frame. When set to FrameOpen, the motion represents the time from 
	* Frame N onwards.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "360", ClampMin = "0", ClampMax = "360"), Category = "Camera Settings")
	EMoviePipelineShutterTiming ShutterTiming;
	
};