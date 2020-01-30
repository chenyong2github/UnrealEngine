// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineExecutor.generated.h"

class UMoviePipelineMasterConfig;
class UMoviePipelineExecutorBase;
class UMoviePipelineExecutorJob;
class UMoviePipeline;
class UMoviePipelineQueue;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMoviePipelineExecutorFinishedNative, UMoviePipelineExecutorBase* /*PipelineExecutor*/, bool /*bSuccess*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMoviePipelineExecutorFinished, UMoviePipelineExecutorBase*, PipelineExecutor, bool, bSuccess);

DECLARE_MULTICAST_DELEGATE_FourParams(FOnMoviePipelineExecutorErroredNative, UMoviePipelineExecutorBase* /*PipelineExecutor*/, UMoviePipeline* /*PipelineWithError*/, bool /*bIsFatal*/, FText /*ErrorText*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnMoviePipelineExecutorErrored, UMoviePipelineExecutorBase*, PipelineExecutor, UMoviePipeline*, PipelineWithError, bool, bIsFatal, FText, ErrorText);



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
		: bAnyJobHadFatalError(false)
		, TargetPipelineClass(nullptr)
	{
	}

	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	virtual void Execute(UMoviePipelineQueue* InPipelineQueue)
	{
		ExecuteImpl(InPipelineQueue);
	}

	FOnMoviePipelineExecutorFinishedNative& OnExecutorFinished()
	{
		return OnExecutorFinishedDelegateNative;
	}

	FOnMoviePipelineExecutorErroredNative& OnExecutorErrored()
	{
		return OnExecutorErroredDelegateNative;
	}

	void SetMoviePipelineClass(UClass* InPipelineClass)
	{
		TargetPipelineClass = InPipelineClass;
	}
protected:
	/** 
	* This should be called when the Executor has finished executing all of the things
	* it has been asked to execute. This should be called in the event of a failure as 
	* well, and passing in false for success to allow the caller to know failure. Errors
	* should be broadcast on the error delegate, so this is just a handy way to know at
	* the end without having to track it yourself.
	*
	* @param bInSuccess	True if the pipeline successfully executed all jobs. False if there was an error. 
	*/
	virtual void OnExecutorFinishedImpl()
	{
		// Broadcast to both Native and Python/BP
		OnExecutorFinishedDelegateNative.Broadcast(this, !bAnyJobHadFatalError);
		OnExecutorFinishedDelegate.Broadcast(this, !bAnyJobHadFatalError);
	}

	virtual void OnExecutorErroredImpl(UMoviePipeline* ErroredPipeline, bool bFatal, FText ErrorReason)
	{
		if (bFatal)
		{
			bAnyJobHadFatalError = true;
		}

		// Broadcast to both Native and Python/BP
		OnExecutorFinishedDelegateNative.Broadcast(this, bFatal);
		OnExecutorFinishedDelegate.Broadcast(this, bFatal);
	}

	virtual void ExecuteImpl(UMoviePipelineQueue* InPipelineQueue) PURE_VIRTUAL(UMoviePipelineExecutorBase::ExecuteImpl, );

private:
	/** 
	* Called when the Executor has finished all jobs. Reports success if no jobs
	* had fatal errors. Subscribe to the error delegate for more information about
	* any errors.
	*
	* Exposed for Blueprints/Python. Called at the same time as the native one.
	*/
	UPROPERTY(BlueprintAssignable, Category = "Movie Render Pipeline")
	FOnMoviePipelineExecutorFinished OnExecutorFinishedDelegate;

	/** For native C++ code. Called at the same time as the Blueprint/Python one. */
	FOnMoviePipelineExecutorFinishedNative OnExecutorFinishedDelegateNative;

	/**
	* Called when an individual job reports a warning/error. Jobs are considered fatal
	* if the severity was bad enough to abort the job (missing sequence, write failure, etc.)
	*
	* Exposed for Blueprints/Python. Called at the same time as the native one.
	*/
	UPROPERTY(BlueprintAssignable, Category = "Movie Render Pipeline")
	FOnMoviePipelineExecutorErrored OnExecutorErroredDelegate;

	/** For native C++ code. Called at the same time as the Blueprint/Python one. */
	FOnMoviePipelineExecutorErroredNative OnExecutorErroredDelegateNative;

	/** Set automatically when the error delegate gets broadcast (if fatal). */
	bool bAnyJobHadFatalError;
protected:
	/** Which Pipeline Class should be created by this Executor. May be null. */
	UPROPERTY()
	TSubclassOf<UMoviePipeline> TargetPipelineClass;
};