// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineMasterConfig.h"
#include "MovieScene.h"
#include "LevelSequence.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipelineOutputBase.h"
#include "MoviePipelineOutputSetting.h"

#define LOCTEXT_NAMESPACE "MoviePipelineMasterConfig"

/*FFrameRate UMoviePipelineMasterConfig::GetEffectiveFrameRate()
{
	LoadTargetSequence();

	// Check to see if they overrode the frame rate.
	if (bUseCustomFrameRate)
	{
		return OutputFrameRate;
	}

	// Pull it from the sequence if they didn't.
	if (LoadedSequence)
	{
		return LoadedSequence->GetMovieScene()->GetDisplayRate();
	}

	return FFrameRate();
}*/

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

/*void UMoviePipelineMasterConfig::LoadTargetSequence()
{
	if (LoadedSequence)
	{
		return;
	}

	LoadedSequence = LoadObject<ULevelSequence>(this, *Sequence.GetAssetPathString());
	ensureMsgf(LoadedSequence, TEXT("Failed to load target sequence. Pipeline is not fully configured."));
}*/

/*bool UMoviePipelineShotConfig::ValidateConfig(TArray<FText>& OutValidationErrors) const
{
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
	UMoviePipelineOutputSetting* DefaultOutputBlock = CreateDefaultSubobject<UMoviePipelineOutputSetting>("DefaultOutputSetting");
	Settings.Add(DefaultOutputBlock);
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