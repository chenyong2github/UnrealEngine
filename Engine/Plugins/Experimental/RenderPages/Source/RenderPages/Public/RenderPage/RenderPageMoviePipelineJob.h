// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderPageManager.h"
#include "RenderPagesUtils.h"
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

namespace UE::RenderPages::Private
{
	class FRenderPageQueue;
}


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
 * This class is responsible for the MRQ part of the rendering of the given render page.
 */
UCLASS()
class RENDERPAGES_API URenderPagesMoviePipelineRenderJobEntry : public UObject
{
	GENERATED_BODY()

public:
	/** Creates a new render job instance, it won't be started right away. */
	static URenderPagesMoviePipelineRenderJobEntry* Create(URenderPagesMoviePipelineRenderJob* Job, URenderPage* Page, const UE::RenderPages::FRenderPagesMoviePipelineRenderJobCreateArgs& Args);

	/** The destructor, cleans up the TPromise (if it's set). */
	virtual void BeginDestroy() override;

	/** Starts this render job. */
	TSharedFuture<void> Execute();

	/** Cancels this render job. Relies on the internal MRQ implementation of job canceling on whether this will do anything or not. */
	void Cancel();

	/** Retrieves the rendering status of the given render page. */
	FString GetStatus() const;

	/** Retrieves the "Engine Warm Up Count" value from the AntiAliasingSettings from the render preset that this render page uses. */
	int32 GetEngineWarmUpCount() const;

public:
	/** Returns true if this render job was canceled (which for example can be caused by calling Cancel(), or by closing the render popup). */
	bool IsCanceled() const { return bCanceled; }

private:
	void ComputePlaybackContext(bool& bOutAllowBinding);
	void ExecuteFinished(UMoviePipelineExecutorBase* PipelineExecutor, const bool bSuccess);

protected:
	/** The MRQ queue. */
	UPROPERTY(Transient)
	TObjectPtr<UMoviePipelineQueue> RenderQueue;

	/** The MRQ pipeline executor. */
	UPROPERTY(Transient)
	TObjectPtr<UMoviePipelineExecutorBase> Executor;

	/** The MRQ job of the given render page. */
	UPROPERTY(Transient)
	TObjectPtr<UMoviePipelineExecutorJob> ExecutorJob;

	/** The TPromise of the rendering process. */
	TSharedPtr<TPromise<void>> Promise;

	/** The TFuture of the rendering process. */
	TSharedFuture<void> PromiseFuture;

	/** The rendering status of the given render page. */
	UPROPERTY(Transient)
	FString Status;

	/** Whether the entry can execute, or whether it should just skip execution. */
	UPROPERTY(Transient)
	bool bCanExecute;

	/** Whether the entry was canceled (like by calling Cancel(), or by closing the render popup). */
	UPROPERTY(Transient)
	bool bCanceled;
};


/**
 * This class is responsible for rendering the given render pages.
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

	/** Cancels this render job. Relies on the internal MRQ implementation of job canceling on whether this will stop the current page from rendering or not. Will always prevent new pages from rendering. */
	void Cancel();

	/** Returns true if this render job has been canceled. */
	bool IsCanceled() const { return bCanceled; }

	/** Retrieves the rendering status of the given render page. */
	FString GetPageStatus(URenderPage* Page) const;

protected:
	/** The queue containing the render actions. */
	TSharedPtr<UE::RenderPages::Private::FRenderPageQueue> Queue;

	/** The render pages that are to be rendered, mapped to the rendering job of each specific render page. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<const URenderPage>, TObjectPtr<URenderPagesMoviePipelineRenderJobEntry>> Entries;

	/** The render page collection of the given render page that will be rendered. */
	UPROPERTY(Transient)
	TObjectPtr<URenderPageCollection> PageCollection;

	/** Whether the remaining pages should be prevented from rendering. */
	UPROPERTY(Transient)
	bool bCanceled;

	/** The render page property values that have been overwritten by the currently applied page property values. */
	UPROPERTY(Transient)
	FRenderPageManagerPreviousPagePropValues PreviousPageProps;

	/** The engine framerate settings values that have been overwritten by the currently applied engine framerate settings values. */
	UPROPERTY(Transient)
	FRenderPagePreviousEngineFpsSettings PreviousFrameLimitSettings;

	/** True if the queue has previously executed the pre-render event of a page. */
	UPROPERTY(Transient)
	bool bRanPreRender;

public:
	/** A delegate for when the render job is about to start. */
	UE::RenderPages::FOnRenderPagesMoviePipelineRenderJobStarted& OnExecuteStarted() { return OnExecuteStartedDelegate; }

	/** A delegate for when the render job has finished. */
	UE::RenderPages::FOnRenderPagesMoviePipelineRenderJobFinished& OnExecuteFinished() { return OnExecuteFinishedDelegate; }

private:
	UE::RenderPages::FOnRenderPagesMoviePipelineRenderJobStarted OnExecuteStartedDelegate;
	UE::RenderPages::FOnRenderPagesMoviePipelineRenderJobFinished OnExecuteFinishedDelegate;
};
