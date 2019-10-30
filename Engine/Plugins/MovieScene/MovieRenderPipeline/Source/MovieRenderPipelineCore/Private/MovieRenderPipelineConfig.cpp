// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieRenderPipelineConfig.h"
#include "MovieScene.h"
#include "LevelSequence.h"

#define LOCTEXT_NAMESPACE "MovieRenderPipelineConfig"

FFrameRate UMovieRenderPipelineConfig::GetEffectiveFrameRate()
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
}

UMoviePipelineShotConfig* UMovieRenderPipelineConfig::GetConfigForShot(const FString& ShotName) const
{
	UMoviePipelineShotConfig* OutConfig = PerShotConfigMapping.FindRef(ShotName);

	// They didn't customize this shot, return the global pipeline default
	if (!OutConfig)
	{
		OutConfig = DefaultShotConfig;
	}

	return OutConfig;
}

void UMovieRenderPipelineConfig::LoadTargetSequence()
{
	if (LoadedSequence)
	{
		return;
	}

	LoadedSequence = LoadObject<ULevelSequence>(this, *Sequence.GetAssetPathString());
	ensureMsgf(LoadedSequence, TEXT("Failed to load target sequence. Pipeline is not fully configured."));
}

#undef LOCTEXT_NAMESPACE // "MovieRenderPipelineConfig"