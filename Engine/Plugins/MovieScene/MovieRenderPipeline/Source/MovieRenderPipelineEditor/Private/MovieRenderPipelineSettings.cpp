// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieRenderPipelineSettings.h"
#include "MoviePipelinePIEExecutor.h"
#include "MoviePipelineNewProcessExecutor.h"
#include "MoviePipeline.h"

UMovieRenderPipelineProjectSettings::UMovieRenderPipelineProjectSettings()
{
	PresetSaveDir.Path = TEXT("/Game/Cinematics/MoviePipeline/Presets/");
	DefaultLocalExecutor = UMoviePipelinePIEExecutor::StaticClass();
	DefaultRemoteExecutor = UMoviePipelineNewProcessExecutor::StaticClass();
	DefaultPipeline = UMoviePipeline::StaticClass();
}