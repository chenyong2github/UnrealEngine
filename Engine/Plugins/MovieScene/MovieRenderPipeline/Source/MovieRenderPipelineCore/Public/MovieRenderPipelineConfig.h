// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/FrameRate.h"
#include "Misc/FrameNumber.h"
#include "Engine/EngineTypes.h"

#include "MovieRenderPipelineConfig.generated.h"


// Forward Declares
class ULevelSequence;
class UMoviePipelineSetting;
class UMoviePipelineRenderPass;
class UMoviePipelineOutput;
class UMoviePipelineShotConfig;

/**
* This class describes the main configuration for a Movie Render Pipeline.
* Only settings that apply to the entire output should be stored here,
* anything that is changed on a per-shot basis should be stored inside of 
* UMovieRenderShotConfig instead.
*
* THIS CLASS SHOULD BE IMMUTABLE ONCE PASSED TO THE PIPELINE FOR PROCESSING.
* (Otherwise you will be modifying the instance that exists in the UI)
*/
UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMovieRenderPipelineConfig : public UObject
{
	GENERATED_BODY()
	
public:
	UMovieRenderPipelineConfig()
		: OutputResolution(FIntPoint(1920, 1080))
		, bUseCustomFrameRate(false)
		, OutputFrameRate(FFrameRate(24, 1))
		, OutputFrameStep(FFrameNumber(1))
		, bOverrideExistingOutput(true)
		, bUseCustomPlaybackRange(false)
	{
		
	}

public:
	/** Gets the effective frame rate (respecting framerate overrides). Should not be called unless the Sequence is set. */
	FFrameRate GetEffectiveFrameRate();

	/** Returns a pointer to the config specified for the shot, otherwise the default for this pipeline. */
	UMoviePipelineShotConfig* GetConfigForShot(const FString& ShotName) const;

public:
	/** What Sequence do we want to render out. Can be overridden by command line launch arguments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movie Render Pipeline", meta=(MetaClass="LevelSequence"))
	FSoftObjectPath Sequence;
	
	/** What resolution should our output files be exported at? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline")
	FIntPoint OutputResolution;
	
	/** Should we use the custom frame rate specified by OutputFrameRate? Otherwise defaults to Sequence frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline")
	bool bUseCustomFrameRate;
	
	/** What frame rate should the output files be exported at? This overrides the Display Rate of the target sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition=bUseCustomFrameRate), Category = "Movie Render Pipeline")
	FFrameRate OutputFrameRate;
	
	/** How many frames (in Display Rate) should we step at a time. Can be used to render every other frame for faster drafts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline")
	FFrameNumber OutputFrameStep;
	
	/** What directory should all of our output files be relative to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline")
	FDirectoryPath OutputDirectory;
	
	/** If true, output containers should attempt to override any existing files with the same name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline")
	bool bOverrideExistingOutput;
public:
	/** If true, override the Playback Range start/end bounds with the bounds specified below.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline")
	bool bUseCustomPlaybackRange;
	
	/** Used when overriding the playback range. In Display Rate frames. If bUseCustomPlaybackRange is false range will come from Sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition=bUseCustomPlaybackRange), Category = "Movie Render Pipeline")
	FFrameNumber CustomStartFrame;
	
	/** Used when overriding the playback range. In Display Rate frames. If bUseCustomPlaybackRange is false range will come from Sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition=bUseCustomPlaybackRange), Category = "Movie Render Pipeline")
	FFrameNumber CustomEndFrame;

public:
	
	/** The default shot-setup to use for any shot that doesn't a specific implementation. This is required! */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Movie Render Pipeline")
	UMoviePipelineShotConfig* DefaultShotConfig;
	
	/** A mapping of Shot Name -> Shot Config to use for rendering specific shots with specific configs. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Movie Render Pipeline")
	TMap<FString, UMoviePipelineShotConfig*> PerShotConfigMapping;
	
	/** Array of Output Containers. Each output container is passed data for each Input Buffer every frame. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Movie Render Pipeline")
	TArray<UMoviePipelineOutput*> OutputContainers;

private:
	/** Attempts to load the target sequence. Will trip an ensure if the sequence is not found. */
	void LoadTargetSequence();
	
	/** We want to store the loaded asset to avoid having to load it each time we need a setting from it. */
	UPROPERTY(Transient)
	ULevelSequence* LoadedSequence;
};