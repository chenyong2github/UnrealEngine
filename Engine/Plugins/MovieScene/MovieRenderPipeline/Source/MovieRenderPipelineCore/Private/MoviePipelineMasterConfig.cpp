// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineMasterConfig.h"
#include "MovieScene.h"
#include "LevelSequence.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipelineOutputBase.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineQueue.h"

#define LOCTEXT_NAMESPACE "MoviePipelineMasterConfig"



UMoviePipelineShotConfig* UMoviePipelineMasterConfig::GetConfigForShot(const FString& ShotName) const
{
	UMoviePipelineShotConfig* OutConfig = PerShotConfigMapping.FindRef(ShotName);

	// They didn't customize this shot, return the global pipeline default
	if (!OutConfig)
	{
		OutConfig = DefaultShotConfig;
	}

	return OutConfig;
}

void UMoviePipelineMasterConfig::GetFilenameFormatArguments(FFormatNamedArguments& OutArguments, const UMoviePipelineExecutorJob* InJob) const
{
	if (!InJob)
	{
		return;
	}

	// Add "global" ones not specific to a setting.
	{
		OutArguments.Add(TEXT("level_name"), FText::FromString(InJob->Map.GetAssetName()));
		OutArguments.Add(TEXT("sequence_name"), FText::FromString(InJob->Sequence.GetAssetName()));
		OutArguments.Add(TEXT("date"), FText::AsDate(FDateTime::UtcNow()));
		OutArguments.Add(TEXT("time"), FText::AsTime(FDateTime::UtcNow()));
		
		// FrameRate is a combination of Output Settings and Sequence so we do it here instead of in OutputSetting
		OutArguments.Add(TEXT("frame_rate"), FText::AsNumber(GetEffectiveFrameRate(Cast<ULevelSequence>(InJob->Sequence.TryLoad())).AsDecimal()));

	}

	// Let each setting provide its own set of key/value pairs.
	{
		for (UMoviePipelineSetting* Setting : GetSettings())
		{
			Setting->GetFilenameFormatArguments(OutArguments, InJob);
		}

		// ToDo: Should shots be able to provide arguments too? They're only overrides, and
		// outside of that shot the format would fail and end up leaving the {text} anyways.
	}
}

FFrameRate UMoviePipelineMasterConfig::GetEffectiveFrameRate(const ULevelSequence* InSequence) const
{
	UMoviePipelineOutputSetting* OutputSettings = FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	// Check to see if they overrode the frame rate.
	if (OutputSettings->bUseCustomFrameRate)
	{
		return OutputSettings->OutputFrameRate;
	}

	// Pull it from the sequence if they didn't.
	if (InSequence)
	{
		return InSequence->GetMovieScene()->GetDisplayRate();
	}

	return FFrameRate();
}


/*bool UMoviePipelineShotConfig::ValidateConfig(TArray<FText>& OutValidationErrors) const{
	bool bValidPipeline = true;
	OutValidationErrors.Reset();
	
	// Check that we have a sequence to render
	if(!Sequence.IsValid())
	{
		OutValidationErrors.Add(LOCTEXT("InvalidInputSequence", "No sequence to render has been specified."));
		bValidPipeline = false;
	}
	
	// Check that we have a valid output resolution
	if(OutputResolution.X == 0 || OutputResolution.Y == 0)
	{
		OutValidationErrors.Add(LOCTEXT("InvalidOutputResolution", "Output Resolution must be greater than zero."));
		bValidPipeline = false;
	}
	
	// Check for a non-zero framerate
	if(OutputFrameRate.AsDecimal() == 0.0)
	{
		OutValidationErrors.Add(LOCTEXT("InvalidOutputFrameRate", "Output FrameRate must be greater than zero."));
		bValidPipeline = false;
	}
	
	// Output Directory can probably be ignored. The folder path you output to may not exist yet anyways.
	
	// Give each setting a chance to validate the pipeline
	for(const UMoviePipelineSetting* Setting : Settings)
	{
		bValidPipeline &= Setting->ValidatePipeline(OutValidationErrors);
	}
	
	// ToDo-MoviePipeline: Reflect over all settings classes and make sure all required classes have been added.
	// ToDo-MoviePipeline: Scan for any shots/sections that don't start on whole frames issue warning.
	
	// Give each Input Buffer a chance to validate the pipeline
	for(const UMoviePipelineRenderPass* InputBuffer : InputBuffers)
	{
		bValidPipeline &= InputBuffer->ValidatePipeline(OutValidationErrors);
	}
	
	// Give each output container a chance to validate the pipeline
	for(const UMoviePipelineOutput* OutputContainer : OutputContainers)
	{
		bValidPipeline &= OutputContainer->ValidatePipeline(OutValidationErrors);
	}

	// Ensure we have at least one input buffer and one output buffer.
	if (InputBuffers.Num() == 0)
	{
		OutValidationErrors.Add(LOCTEXT("NoInputBuffer", "Must specify at least one Input Buffer to capture."));
		bValidPipeline = false;
	}

	if (OutputContainers.Num() == 0)
	{
		OutValidationErrors.Add(LOCTEXT("NoOutputContainer", "Must specify at least one Output Container to write to."));
		bValidPipeline = false;
	}
	
	return bValidPipeline;
}*/

UMoviePipelineMasterConfig::UMoviePipelineMasterConfig()
{
	DefaultShotConfig = CreateDefaultSubobject<UMoviePipelineShotConfig>("DefaultShotConfig");
	
	// Always add at least the output settings block since having a framerate/directory/etc. is critical.
	OutputSetting = CreateDefaultSubobject<UMoviePipelineOutputSetting>("DefaultOutputSetting");
}

TArray<UMoviePipelineSetting*> UMoviePipelineMasterConfig::GetSettings() const
{
	TArray<UMoviePipelineSetting*> BaseSettings = Super::GetSettings();
	BaseSettings.Add(OutputSetting);

	return BaseSettings;
}

void UMoviePipelineMasterConfig::CopyFrom(UMoviePipelineConfigBase* InConfig)
{
	Super::CopyFrom(InConfig);

	if (InConfig->IsA<UMoviePipelineMasterConfig>())
	{
		UMoviePipelineMasterConfig* MasterConfig = CastChecked<UMoviePipelineMasterConfig>(InConfig);
		OutputSetting = Cast<UMoviePipelineOutputSetting>(StaticDuplicateObject(MasterConfig->OutputSetting, this, MasterConfig->OutputSetting->GetFName()));
	}
}


TArray<UMoviePipelineOutputBase*> UMoviePipelineMasterConfig::GetOutputContainers() const
{
	TArray<UMoviePipelineOutputBase*> OutputContainers;

	for (UMoviePipelineSetting* Setting : GetSettings())
	{
		UMoviePipelineOutputBase* Output = Cast<UMoviePipelineOutputBase>(Setting);
		if (Output)
		{
			OutputContainers.Add(Output);
		}
	}

	return OutputContainers;
}

#undef LOCTEXT_NAMESPACE // "MovieRenderPipelineConfig"