// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MoviePipelineExecutor.generated.h"

class UMovieRenderPipelineConfig;
class UMoviePipelineExecutorBase;
class UMoviePipeline;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMoviePipelineExecutorFinishedNative, UMoviePipelineExecutorBase*);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMoviePipelineExecutorFinished, UMoviePipelineExecutorBase*, PipelineExecutor);

/**
* A Movie Pipeline Executor is responsible for executing an array of Movie Pipelines,
* and (optionally) reporting progress back for the movie pipelines. The entire array
* is passed at once to allow the implementations to choose how to split up the work.
* By default we provide a local execution (UMoviePipelineLocalExecutor) which works
* on them serially, but you can create an implementation of this class, change the 
* default in the Project Settings and use your own distribution logic. For example,
* you may want to distribute the work to multiple computers over a network, which
* may involve running command line options on each machine to sync the latest content
* from the project before the execution starts.
*/
UCLASS(Blueprintable, Abstract)
class MOVIERENDERPIPELINECORE_API UMoviePipelineExecutorBase : public UObject
{
	GENERATED_BODY()
public:
	UMoviePipelineExecutorBase()
		: TargetPipelineClass(nullptr)
	{
	}

	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	virtual void Execute(UPARAM(Ref) TArray<UMovieRenderPipelineConfig*>& InPipelines)
	{
		ExecuteImpl(InPipelines);
	}

	FOnMoviePipelineExecutorFinishedNative& OnExecutorFinished()
	{
		return OnExecutorFinishedDelegateNative;
	}

	void SetMoviePipelineClass(UClass* InPipelineClass)
	{
		TargetPipelineClass = InPipelineClass;
	}
protected:
	/** 
	* This should be called when the Executor has finished executing all of the things
	* it has been asked to execute. This should be called in the event of a failure as 
	* well, and passing in false for success to allow the caller to know failure.
	*/
	virtual void OnExecutorFinishedImpl(const bool bInSuccess)
	{
		// Broadcast to both Native and Python/BP
		OnExecutorFinishedDelegateNative.Broadcast(this);
		OnExecutorFinishedDelegate.Broadcast(this);
	}
	virtual void ExecuteImpl(TArray<UMovieRenderPipelineConfig*>& InPipelines) PURE_VIRTUAL(UMoviePipelineExecutorBase::ExecuteImpl, );

private:
	/** Exposed for Blueprints/Python. Called at the same time as the native one. */
	UPROPERTY(BlueprintAssignable, Category = "Movie Render Pipeline")
	FOnMoviePipelineExecutorFinished OnExecutorFinishedDelegate;

	/** For native C++ code. Called at the same time as the Blueprint/Python one. */
	FOnMoviePipelineExecutorFinishedNative OnExecutorFinishedDelegateNative;
protected:
	UPROPERTY()
	TSubclassOf<UMoviePipeline> TargetPipelineClass;
};