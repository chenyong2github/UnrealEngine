// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipeline.h"
#include "Engine/EngineTypes.h"
#include "MovieRenderPipelineSettings.generated.h"

class UMoviePipelineExecutorBase;
class UMoviePipeline;
class UMoviePipelineMasterConfig;

/**
 * Universal Movie Render Pipeline settings that apply to the whole project.
 */
UCLASS(BlueprintType, config=Editor, defaultconfig, MinimalAPI)
class UMovieRenderPipelineProjectSettings : public UObject
{
public:
	GENERATED_BODY()
	
	MOVIERENDERPIPELINEEDITOR_API UMovieRenderPipelineProjectSettings();

	/**
	* Which directory should we try to save presets in by default?
	*/
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline", DisplayName = "Preset Save Location")
	FDirectoryPath PresetSaveDir;


	/**
	* What was the last configuration preset the user used? Can be null.
	*/
	UPROPERTY(Transient, EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline")
	TSoftObjectPtr<UMoviePipelineMasterConfig> LastPresetOrigin;
	
	/**
	* When the user uses the UI to request we render a movie locally, which implementation should we use
	* to execute the queue of things they want rendered. This allows you to implement your own executor 
	* which does different logic. See UMoviePipelineExecutorBase for more information. This is used for
	* the Render button on the UI.
	*/
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category="Movie Render Pipeline")
	TSubclassOf<UMoviePipelineExecutorBase> DefaultLocalExecutor;

	/**
	* When the user uses the UI to request we render a movie remotely, which implementation should we use
	* to execute the queue of things they want rendered. This allows you to implement your own executor
	* which does different logic. See UMoviePipelineExecutorBase for more information. This is used for
	* the Render Remotely button on the UI.
	*/
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline")
	TSubclassOf<UMoviePipelineExecutorBase> DefaultRemoteExecutor;
	
	/**
	* Which Job class should we create by default when adding a job? This allows you to make custom jobs
	* that will have editable properties in the UI for special handling with your executor. This can be
	* made dynamic if you add jobs to the queue programatically instead of through the UI.
	*/
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category="Movie Render Pipeline")
	TSubclassOf<UMoviePipelineExecutorJob> DefaultExecutorJob;
	
	/**
	* This allows you to implement your own Pipeline to handle timing and rendering of a movie. Changing
	* this will allow you to re-use the existing UI/Executors while providing your own logic for producing
	* a single render.
	*/
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category="Movie Render Pipeline")
	TSubclassOf<UMoviePipeline> DefaultPipeline;

	/**
	* The settings specified here will automatically be added to a Movie Pipeline Master Configuration when using the UI. 
	* This does not apply to scripting and does not apply to runtime. It is only a convenience function so that when a job is
	* created, it can be pre-filled with some settings to make the render functional out of the gate. It can also be
	* used to automatically add your own setting to jobs.
	*
	* This only applies to jobs created via the UI. If you do not use the UI (ie: Scripting/Python) you will need to
	* add settings by hand for each job you create. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Movie Render Pipeline", DisplayName = "Default Job Settings Classes")
	TArray<TSubclassOf<UMoviePipelineSetting>> DefaultClasses;
};