// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineLinearExecutor.h"
#include "Logging/MessageLog.h"
#include "Misc/OutputDevice.h"
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
	/** Native C++ event to listen to for when an individual job has been finished. */
	FOnMoviePipelineIndividualJobFinishedNative& OnIndividualJobFinished()
	{
		return OnIndividualJobFinishedDelegateNative;
	}


protected:
	virtual void Start(const UMoviePipelineExecutorJob* InJob) override;

	
	/**
	* This should be called after PIE has been shut down for an individual job and it is generally safer to
	* make modifications to the editor world.
	*/
	void OnIndividualJobFinishedImpl(UMoviePipelineExecutorJob* InJob)
	{
		// Broadcast to both Native and Python/BP
		OnIndividualJobFinishedDelegateNative.Broadcast(InJob, IsAnyJobErrored());
		OnIndividualJobFinishedDelegate.Broadcast(InJob, IsAnyJobErrored());
	}

private:
	/** Called when PIE finishes booting up and it is safe for us to spawn an object into that world. */
	void OnPIEStartupFinished(bool);

	/** If they're using delayed initialization, this is called each frame to process the countdown until start, also updates Window Title each frame. */
	void OnTick();

	/** Called before PIE tears down the world during shutdown. Used to detect cancel-via-escape/stop PIE. */
	void OnPIEEnded(bool);
	/** Called when the instance of the pipeline in the PIE world has finished. */
	void OnPIEMoviePipelineFinished(UMoviePipeline* InMoviePipeline, bool bFatalError);
	/** Called a short period of time after OnPIEMoviePipelineFinished to allow Editor the time to fully close PIE before we make a new request. */
	void DelayedFinishNotification();
private:
	/** If using delayed initialization, how many frames are left before we call Initialize. Will be -1 if not actively counting down. */
	int32 RemainingInitializationFrames;
	bool bPreviousUseFixedTimeStep;
	double PreviousFixedTimeStepDelta;
	TWeakPtr<class SWindow> WeakCustomWindow;


	/**
	* Called after PIE has ended for a particular job to allow modifications to the editor world before duplication.
	* You should only use this behavior if you know what you are doing.
	*
	* Exposed for Blueprints/Python. Called at the same time as the native one.
	*/
	UPROPERTY(BlueprintAssignable, Category = "Movie Render Pipeline")
	FOnMoviePipelineIndividualJobFinished OnIndividualJobFinishedDelegate;

	/** For native C++ code. Called at the same time as the Blueprint/Python one. */
	FOnMoviePipelineIndividualJobFinishedNative OnIndividualJobFinishedDelegateNative;


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