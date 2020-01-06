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
		: SpatialSampleCount(1)
		, HandleFrameCount(0)
		, WarmUpFrameCount(0)
		, AccumulationGamma(1.f)
	{
	}

public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "AccumulationSettingDisplayName", "Subsample Accumulation"); }
#endif
protected:
	virtual bool IsValidOnShots() const override { return true; }
	virtual bool IsValidOnMaster() const override { return true; }
	
public:

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
};