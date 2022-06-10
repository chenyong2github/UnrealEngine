// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderPageManager.h"
#include "RenderPageMoviePipelineJob.generated.h"


class UMoviePipelineSetting;
class UMoviePipelineOutputBase;
struct FMoviePipelineOutputData;
class URenderPageCollection;
class URenderPage;
class UMoviePipelineExecutorBase;
class UMoviePipelineQueue;
class UMoviePipelineExecutorJob;
class URenderPagesMoviePipelineRenderJob;


namespace UE::RenderPages
{
	/** A delegate for when a render job is about to start. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRenderPagesMoviePipelineRenderJobStarted, URenderPagesMoviePipelineRenderJob* /*RenderJob*/);

	/** A delegate for when a render job has finished. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRenderPagesMoviePipelineRenderJobFinished, URenderPagesMoviePipelineRenderJob* /*RenderJob*/, bool /*bSuccess*/);

	/**
	 * The arguments for the URenderPagesMoviePipelineRenderJob::Create function.
	 */
	struct RENDERPAGES_API FRenderPagesMoviePipelineRenderJobCreateArgs
	{
	public:
		/** The render page collection of the given render pages that will be rendered. */
		TObjectPtr<URenderPageCollection> PageCollection = nullptr;

		/** The specific render pages that will be rendered. */
		TArray<TObjectPtr<URenderPage>> Pages;

		/** If not null, it will override the MRQ pipeline executor class with this class. */
		TSubclassOf<UMoviePipelineExecutorBase> PipelineExecutorClass = nullptr;

		/** The MRQ settings classes to disable (things like Anti-Aliasing, High-Res, etc). */
		TArray<TSubclassOf<UMoviePipelineSetting>> DisableSettingsClasses;

		/** Whether it should run invisibly (so without any UI elements popping up during rendering) or not. */
		bool bHeadless = false;

		/** Whether it should make sure it will output an image or not (if this bool is true, it will test if JPG/PNG/etc output is enabled, if none are, it will enable PNG output). */
		bool bForceOutputImage = false;

		/** Whether it should make sure it will only output in a single format (if this bool is true, if for example JPG and PNG output are enabled, one will be disabled, so that there will only be 1 output that's enabled). */
		bool bForceOnlySingleOutput = false;

		/** Whether it should use the sequence's framerate rather than any manually set framerate (if this bool is true, it will make sure bUseCustomFrameRate is set to false). */
		bool bForceUseSequenceFrameRate = false;

		/** Whether it should make sure it will output files named 0000000001, 0000000002, etc (if this bool is true, it will override the FileNameFormat to simply output the frame number, and it will add 1000000000 to that frame number to hopefully ensure that any negative frame numbers will not result in filenames starting with a minus character). */
		bool bEnsureSequentialFilenames = false;
	};
}


/**
 * This class is responsible for rendering render pages.
 */
UCLASS()
class RENDERPAGES_API URenderPagesMoviePipelineRenderJob : public UObject
{
	GENERATED_BODY()

public:
	/** Creates a new render job instance, it won't be started right away. */
	static URenderPagesMoviePipelineRenderJob* Create(const UE::RenderPages::FRenderPagesMoviePipelineRenderJobCreateArgs& Args);

	/** Starts this render job. */
	void Execute();

	/** Cancels this render job. Relies on the internal MRQ implementation of job canceling on whether this will do anything or not. */
	void Cancel();

	/** Retrieves the rendering status of the given render page. */
	FString GetPageStatus(URenderPage* Page) const;

private:
	void ComputePlaybackContext(bool& bOutAllowBinding);
	void ExecutePageStarted(UMoviePipelineExecutorJob* JobToStart);
	void ExecutePageFinished(FMoviePipelineOutputData PipelineOutputData);
	void ExecuteFinished(UMoviePipelineExecutorBase* PipelineExecutor, const bool bSuccess);


protected:
	/** The MRQ queue. */
	UPROPERTY(Transient)
	TObjectPtr<UMoviePipelineQueue> RenderQueue;

	/** The MRQ pipeline executor. */
	UPROPERTY(Transient)
	TObjectPtr<UMoviePipelineExecutorBase> ActiveExecutor;

	/** The render page collection of the given render pages that will be rendered. */
	UPROPERTY(Transient)
	TObjectPtr<URenderPageCollection> PageCollection;

	/** The render pages that are to be rendered, mapped to the job of each specific render page. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<const URenderPage>, TObjectPtr<UMoviePipelineExecutorJob>> PageExecutorJobs;

	/** The render pages that are to be rendered, mapped to the rendering status of each specific render page. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<const URenderPage>, FString> PageStatuses;

	/** The render page properties that have been overwritten by the currently applied page properties. */
	UPROPERTY(Transient)
	FRenderPageManagerPreviousPagePropValues PreviousPageProps;


public:
	/** A delegate for when the render job is about to start. */
	UE::RenderPages::FOnRenderPagesMoviePipelineRenderJobStarted& OnExecuteStarted() { return OnExecuteStartedDelegate; }

	/** A delegate for when the render job has finished. */
	UE::RenderPages::FOnRenderPagesMoviePipelineRenderJobFinished& OnExecuteFinished() { return OnExecuteFinishedDelegate; }

private:
	UE::RenderPages::FOnRenderPagesMoviePipelineRenderJobStarted OnExecuteStartedDelegate;
	UE::RenderPages::FOnRenderPagesMoviePipelineRenderJobFinished OnExecuteFinishedDelegate;
};
