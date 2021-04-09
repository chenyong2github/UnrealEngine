// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineLinearExecutor.h"
#include "Logging/MessageLog.h"
#include "Misc/OutputDevice.h"
#include "MoviePipeline.h"
#include "MoviePipelinePIEExecutor.generated.h"

class UMoviePipeline;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMoviePipelineIndividualJobFinishedNative, UMoviePipelineExecutorJob* /*FinishedJob*/, bool /*bSuccess*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMoviePipelineIndividualJobFinished, UMoviePipelineExecutorJob*, FinishedJob, bool, bSuccess);

/**
* This is the implementation responsible for executing the rendering of
* multiple movie pipelines in the currently running Editor process. This
* involves launching a Play in Editor session for each Movie Pipeline to
* process.
*/
UCLASS(Blueprintable)
class MOVIERENDERPIPELINEEDITOR_API UMoviePipelinePIEExecutor : public UMoviePipelineLinearExecutorBase
{
	GENERATED_BODY()
	
public:
	UMoviePipelinePIEExecutor()
		: UMoviePipelineLinearExecutorBase()
		, RemainingInitializationFrames(-1)
		, bPreviousUseFixedTimeStep(false)
		, PreviousFixedTimeStepDelta(1/30.0)
	{
	}

public:
	/** Deprecated. Use OnIndividualJobWorkFinished instead. */
	UE_DEPRECATED(4.27, "Use OnIndividualJobWorkFinished() instead.")
	FOnMoviePipelineIndividualJobFinishedNative& OnIndividualJobFinished()
	{
		return OnIndividualJobFinishedDelegateNative;
	}

	/** Native C++ event to listen to for when an individual job has been finished. */
	FMoviePipelineWorkFinishedNative& OnIndividualJobWorkFinished()
	{
		return OnIndividualJobWorkFinishedDelegateNative;
	}

	/** Native C++ event to listen to for when an individual shot has been finished. Only called if the UMoviePipeline is set up correctly, see its headers for details. */
	FMoviePipelineWorkFinishedNative& OnIndividualShotWorkFinished()
	{
		return OnIndividualShotWorkFinishedDelegateNative;
	}

protected:
	virtual void Start(const UMoviePipelineExecutorJob* InJob) override;

	
	/**
	* This should be called after PIE has been shut down for an individual job and it is generally safer to
	* make modifications to the editor world.
	*/
	void OnIndividualJobFinishedImpl(FMoviePipelineOutputData InOutputData);

private:
	/** Called when PIE finishes booting up and it is safe for us to spawn an object into that world. */
	void OnPIEStartupFinished(bool);

	/** If they're using delayed initialization, this is called each frame to process the countdown until start, also updates Window Title each frame. */
	void OnTick();

	/** Called before PIE tears down the world during shutdown. Used to detect cancel-via-escape/stop PIE. */
	void OnPIEEnded(bool);
	/** Called when the instance of the pipeline in the PIE world has finished. */
	void OnPIEMoviePipelineFinished(FMoviePipelineOutputData InOutputData);
	void OnJobShotFinished(FMoviePipelineOutputData InOutputData);

	/** Called a short period of time after OnPIEMoviePipelineFinished to allow Editor the time to fully close PIE before we make a new request. */
	void DelayedFinishNotification();
private:
	/** If using delayed initialization, how many frames are left before we call Initialize. Will be -1 if not actively counting down. */
	int32 RemainingInitializationFrames;
	bool bPreviousUseFixedTimeStep;
	double PreviousFixedTimeStepDelta;
	TWeakPtr<class SWindow> WeakCustomWindow;

	FMoviePipelineOutputData CachedOutputDataParams;

	/** Deprecated. use OnIndividualJobWorkFinishedDelegate instead. */
	UE_DEPRECATED(4.27, "Use OnIndividualJobWorkFinishedDelegate instead.")
	UPROPERTY(BlueprintAssignable, Category = "Movie Render Pipeline")
	FOnMoviePipelineIndividualJobFinished OnIndividualJobFinishedDelegate;

	FOnMoviePipelineIndividualJobFinishedNative OnIndividualJobFinishedDelegateNative;


	/** Called after each job is finished in the queue. Params struct contains an output of all files written. */
	UPROPERTY(BlueprintAssignable, Category = "Movie Render Pipeline")
	FMoviePipelineWorkFinished OnIndividualJobWorkFinishedDelegate;

	/** 
	* Called after each shot is finished for a particular render. Params struct contains an output of files written for this shot. 
	* Only called if the UMoviePipeline is set up correctly, requires a flag in the output setting to be set. 
	*/
	UPROPERTY(BlueprintAssignable, Category = "Movie Render Pipeline")
	FMoviePipelineWorkFinished OnIndividualShotWorkFinishedDelegate;

	FMoviePipelineWorkFinishedNative OnIndividualJobWorkFinishedDelegateNative;
	FMoviePipelineWorkFinishedNative OnIndividualShotWorkFinishedDelegateNative;

	class FValidationMessageGatherer : public FOutputDevice
	{
	public:

		FValidationMessageGatherer();

		void StartGathering()
		{
			FString PageName = FString("High Quality Media Export: ") + FDateTime::Now().ToString();
			ExecutorLog->NewPage(FText::FromString(PageName));
			GLog->AddOutputDevice(this);
		}

		void StopGathering()
		{
			GLog->RemoveOutputDevice(this);
		}

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override;

		void OpenLog()
		{
			ExecutorLog->Open();
		}

	private:
		TUniquePtr<FMessageLog> ExecutorLog;
		const static TArray<FString> Whitelist;
	};

	FValidationMessageGatherer ValidationMessageGatherer;
};