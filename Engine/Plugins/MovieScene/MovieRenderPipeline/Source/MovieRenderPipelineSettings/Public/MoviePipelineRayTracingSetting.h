// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "LevelSequenceActor.h"
#include "MoviePipelineRayTracingSetting.generated.h"

UCLASS(BlueprintType)
class UMoviePipelineRayTracingSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()

	UMoviePipelineRayTracingSetting()
		: bEnableDenoisers(true)
	{}
public:
	/**
	* Enabling the ray tracing denoisers will reduce the number of samples required per frame,
	* but can introduce artifacts (ghosting). If you disable this expect to need 64-128 samples
	* per frame to overcome the lack of denoiser.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition="bUsePathTracer"), Category = "Movie Pipeline")
	bool bEnableDenoisers;
	
	int32 AmbientOcclusionSPP;
	int32 GlobalIlluminationSPP;
	int32 ReflectionsSPP;
	int32 ShadowSPP;
		
	bool bUsePathTracer;
	/**
	
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category="Movie Pipeline")
	ULevelSequenceBurnInInitSettings* Settings;*/
};