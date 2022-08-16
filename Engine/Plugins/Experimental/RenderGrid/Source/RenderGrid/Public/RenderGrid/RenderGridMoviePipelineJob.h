// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGridManager.h"
#include "RenderGridUtils.h"
#include "RenderGridMoviePipelineJob.generated.h"


class UMoviePipelineSetting;
class UMoviePipelineOutputBase;
struct FMoviePipelineOutputData;
class URenderGrid;
class URenderGridJob;
class UMoviePipelineExecutorBase;
class UMoviePipelinePIEExecutor;
class UMoviePipelineQueue;
class UMoviePipelineExecutorJob;
class URenderGridMoviePipelineRenderJob;

namespace UE::RenderGrid::Private
{
	class FRenderGridQueue;
}


namespace UE::RenderGrid
{
	/** A delegate for when a render job is about to start. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRenderGridMoviePipelineRenderJobStarted, URenderGridMoviePipelineRenderJob* /*RenderJob*/);

	/** A delegate for when a render job has finished. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRenderGridMoviePipelineRenderJobFinished, URenderGridMoviePipelineRenderJob* /*RenderJob*/, bool /*bSuccess*/);

	/**
	 * The arguments for the URenderGridMoviePipelineRenderJob::Create function.
	 */
	struct RENDERGRID_API FRenderGridMoviePipelineRenderJobCreateArgs
	{
	public:
		/** The render grid of the given render grid jobs that will be rendered. */
		TObjectPtr<URenderGrid> RenderGrid = nullptr;

		/** The specific render grid jobs that will be rendered. */
		TArray<TObjectPtr<URenderGridJob>> RenderGridJobs;

		/** If not null, it will override the MRQ pipeline executor class with this class. */
		TSubclassOf<UMoviePipelinePIEExecutor> PipelineExecutorClass = nullptr;

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
 * This class is responsible for the MRQ part of the rendering of the given render grid job.
 */
UCLASS()
class RENDERGRID_API URenderGridMoviePipelineRenderJobEntry : public UObject
{
	GENERATED_BODY()

public:
	/** Creates a new render job instance, it won't be started right away. */
	static URenderGridMoviePipelineRenderJobEntry* Create(URenderGridMoviePipelineRenderJob* RenderJob, URenderGridJob* Job, const UE::RenderGrid::FRenderGridMoviePipelineRenderJobCreateArgs& Args);

	/** The destructor, cleans up the TPromise (if it's set). */
	virtual void BeginDestroy() override;

	/** Starts this render job. */
	TSharedFuture<void> Execute();

	/** Cancels this render job. Relies on the internal MRQ implementation of job canceling on whether this will do anything or not. */
	void Cancel();

	/** Retrieves the rendering status of the given render grid job. */
	FString GetStatus() const;

	/** Retrieves the "Engine Warm Up Count" value from the AntiAliasingSettings from the render preset that this render grid job uses. */
	int32 GetEngineWarmUpCount() const;

public:
	/** Returns true if this render job was canceled (which for example can be caused by calling Cancel(), or by closing the render popup). */
	bool IsCanceled() const { return bCanceled; }

private:
	void ComputePlaybackContext(bool& bOutAllowBinding);
	void ExecuteJobStarted(UMoviePipelineExecutorJob* StartingExecutorJob);
	void ExecuteJobFinished(FMoviePipelineOutputData PipelineOutputData);
	void ExecuteFinished(UMoviePipelineExecutorBase* PipelineExecutor, const bool bSuccess);

protected:
	/** The render grid job that will be rendered. */
	UPROPERTY(Transient)
	TObjectPtr<URenderGridJob> RenderGridJob;

	/** The render grid that the render grid job (that will be rendered) belongs to. */
	UPROPERTY(Transient)
	TObjectPtr<URenderGrid> RenderGrid;

	/** The MRQ queue. */
	UPROPERTY(Transient)
	TObjectPtr<UMoviePipelineQueue> RenderQueue;

	/** The MRQ pipeline executor. */
	UPROPERTY(Transient)
	TObjectPtr<UMoviePipelineExecutorBase> Executor;

	/** The MRQ job of the given render grid job. */
	UPROPERTY(Transient)
	TObjectPtr<UMoviePipelineExecutorJob> ExecutorJob;

	/** The TPromise of the rendering process. */
	TSharedPtr<TPromise<void>> Promise;

	/** The TFuture of the rendering process. */
	TSharedFuture<void> PromiseFuture;

	/** The rendering status of the given render grid job. */
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
 * This class is responsible for rendering the given render grid jobs.
 */
UCLASS()
class RENDERGRID_API URenderGridMoviePipelineRenderJob : public UObject
{
	GENERATED_BODY()

public:
	/** Creates a new render job instance, it won't be started right away. */
	static URenderGridMoviePipelineRenderJob* Create(const UE::RenderGrid::FRenderGridMoviePipelineRenderJobCreateArgs& Args);

	/** Starts this render job. */
	void Execute();

	/** Cancels this render job. Relies on the internal MRQ implementation of job canceling on whether this will stop the current render grid job from rendering or not. Will always prevent new render grid jobs from rendering. */
	void Cancel();

	/** Returns true if this render job has been canceled. */
	bool IsCanceled() const { return bCanceled; }

	/** Retrieves the rendering status of the given render grid job. */
	FString GetRenderGridJobStatus(URenderGridJob* Job) const;

protected:
	/** The queue containing the render actions. */
	TSharedPtr<UE::RenderGrid::Private::FRenderGridQueue> Queue;

	/** The render grid jobs that are to be rendered, mapped to the rendering job of each specific render grid job. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<const URenderGridJob>, TObjectPtr<URenderGridMoviePipelineRenderJobEntry>> Entries;

	/** The render grid of the given render grid job that will be rendered. */
	UPROPERTY(Transient)
	TObjectPtr<URenderGrid> RenderGrid;

	/** Whether the remaining render grid jobs should be prevented from rendering. */
	UPROPERTY(Transient)
	bool bCanceled;

	/** The property values that have been overwritten by the currently applied render grid job property values. */
	UPROPERTY(Transient)
	FRenderGridManagerPreviousPropValues PreviousProps;

	/** The engine framerate settings values that have been overwritten by the currently applied engine framerate settings values. */
	UPROPERTY(Transient)
	FRenderGridPreviousEngineFpsSettings PreviousFrameLimitSettings;

public:
	/** A delegate for when the render job is about to start. */
	UE::RenderGrid::FOnRenderGridMoviePipelineRenderJobStarted& OnExecuteStarted() { return OnExecuteStartedDelegate; }

	/** A delegate for when the render job has finished. */
	UE::RenderGrid::FOnRenderGridMoviePipelineRenderJobFinished& OnExecuteFinished() { return OnExecuteFinishedDelegate; }

private:
	UE::RenderGrid::FOnRenderGridMoviePipelineRenderJobStarted OnExecuteStartedDelegate;
	UE::RenderGrid::FOnRenderGridMoviePipelineRenderJobFinished OnExecuteFinishedDelegate;
};
