// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineOutputSetting.h"
#include "Misc/Paths.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineQueue.h"

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
	OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("MovieRenders/");
}

FText UMoviePipelineOutputSetting::GetFooterText(UMoviePipelineExecutorJob* InJob) const 
{
	FTextBuilder TextBuilder;
	TextBuilder.AppendLine(NSLOCTEXT("MovieRenderPipeline", "OutputSettingFooterText_Fmt",
		"A list of {format_strings} and example values that are valid to use in the File Name Format:\n"));

	FFormatNamedArguments Arguments;
	
	// Find the master configuration that owns us
	UMoviePipelineMasterConfig* MasterConfig = GetTypedOuter<UMoviePipelineMasterConfig>();
	if (MasterConfig)
	{
		MasterConfig->GetFilenameFormatArguments(Arguments, InJob);
	}

	for (const TPair<FString, FFormatArgumentValue>& KVP : Arguments)
	{
		TextBuilder.AppendLineFormat(NSLOCTEXT("MovieRenderPipeline", "FormatKeyValuePair_Fmt", "`{{0}`} => {1}"), FText::FromString(KVP.Key), KVP.Value);
	}
	
	return TextBuilder.ToText();
}

void UMoviePipelineOutputSetting::GetFilenameFormatArguments(FFormatNamedArguments& OutArguments, const UMoviePipelineExecutorJob* InJob) const
{
	// Resolution Arguments
	{
		FString Resolution = FString::Printf(TEXT("%d_%d"), OutputResolution.X, OutputResolution.Y);
		OutArguments.Add(TEXT("output_resolution"), FText::FromString(Resolution));
		OutArguments.Add(TEXT("output_width"), FText::AsNumber(OutputResolution.X));
		OutArguments.Add(TEXT("output_height"), FText::AsNumber(OutputResolution.Y));
	}
}