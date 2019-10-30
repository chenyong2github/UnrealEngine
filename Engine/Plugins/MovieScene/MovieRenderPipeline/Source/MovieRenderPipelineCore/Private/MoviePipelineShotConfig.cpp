// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineShotConfig.h"
#include "MoviePipelineSetting.h"
#include "MoviePipelineRenderPass.h"
#include "MoviePipelineOutput.h"
#include "Misc/FrameRate.h"

#define LOCTEXT_NAMESPACE "MovieRenderShotConfig"

bool UMoviePipelineShotConfig::ValidateConfig(TArray<FText>& OutValidationErrors) const
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
}


void UMoviePipelineShotConfig::RemoveSetting(UMoviePipelineSetting* InSource)
{
	Settings.Remove(InSource);

	// Update our cached serial number so the UI rebuilds the tree
	++SettingsSerialNumber;
}
#undef LOCTEXT_NAMESPACE // "MovieRenderShotConfig"