// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineOutputSetting.h"
#include "Misc/Paths.h"

UMoviePipelineOutputSetting::UMoviePipelineOutputSetting()
	: OutputResolution(FIntPoint(1920, 1080))
	, bUseCustomFrameRate(false)
	, OutputFrameRate(FFrameRate(24, 1))
	, OutputFrameStep(FFrameNumber(1))
	, bOverrideExistingOutput(true)
	, bUseCustomPlaybackRange(false)
	, CustomStartFrame(FFrameNumber(0))
	, CustomEndFrame(FFrameNumber(0))
	{
		FileNameFormat = TEXT("{sequence_name}.{frame_number}");
		OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("/VideoCaptures/");
	}
