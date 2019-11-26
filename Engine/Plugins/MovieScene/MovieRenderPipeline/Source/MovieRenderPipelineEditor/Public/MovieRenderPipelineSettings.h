// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipeline.h"
#include "MovieRenderPipelineSettings.generated.h"

class UMoviePipelineExecutorBase;
class UMoviePipeline;

/**
 * Universal Movie Render Pipeline settings that apply to the whole project.
 */
UCLASS(config=EditorPerProjectUserSettings, MinimalAPI)
class UMovieRenderPipelineProjectSettings : public UObject
{
public:
	GENERATED_BODY()
	
	MOVIERENDERPIPELINEEDITOR_API UMovieRenderPipelineProjectSettings();
	
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
	* This allows you to implement your own Pipeline to handle timing and rendering of a movie. Changing
	* this will allow you to re-use the existing UI/Executors while providing your own logic for producing
	* a single render.
	*/
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category="Movie Render Pipeline")
	TSubclassOf<UMoviePipeline> DefaultPipeline;
};