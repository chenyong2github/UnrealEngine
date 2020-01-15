// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Engine/EngineCustomTimeStep.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MovieSceneTimeController.h"
#include "MoviePipeline.generated.h"

// Forward Declares
class UMoviePipelineMasterConfig;
class ULevelSequence;
class UMovieSceneSequencePlayer;
class UMoviePipelineCustomTimeStep;
class UEngineCustomTimeStep;
class ALevelSequenceActor;
class UMovieRenderDebugWidget;
class FMovieRenderViewport;
class FMovieRenderViewportClient;
struct FImagePixelPipe;
struct FMoviePipelineTimeController;
class FMoviePipelineOutputMerger;
class IImageWriteQueue;
class UMoviePipelineExecutorJob;
class UMoviePipelineSetting;



DECLARE_MULTICAST_DELEGATE_OneParam(FMoviePipelineFinished, UMoviePipeline*);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FMoviePipelineErrored, UMoviePipeline* /*Pipeline*/, bool /*bIsFatal*/, FText /*ErrorText*/);

UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipeline : public UObject
{
	GENERATED_BODY()
	
public:
	UMoviePipeline();

	/** 
	* Initialize the movie pipeline with the specified settings. This kicks off the rendering process. 
	
	* @param InJob	- This contains settings and sequence to render this Movie Pipeline with.
	*/
	void Initialize(UMoviePipelineExecutorJob* InJob);

	/** 
	* Call to shut down the pipeline. This flushes any outstanding file writes and unregisters all delegates.
	* This will not call the OnMoviePipelineFinished() delegate as it assumes the external party already knows
	* it is "Finished". 
	*/
	void Shutdown();

	/** 
	* Called when we have completely finished this pipeline. This means that all frames have been rendered,
	* all files written to disk, and any post-finalize exports have finished. This Pipeline will call
	* Shutdown() on itself before calling this delegate to ensure we've unregistered from all delegates
	* and are no longer trying to do anything (even if we still exist).
	*/
	FMoviePipelineFinished& OnMoviePipelineFinished()
	{
		return OnMoviePipelineFinishedDelegate;
	}

	/**
	* Called when there was an error during the rendering of this movie pipeline (such as missing sequence, i/o failure, etc.)
	*/
	FMoviePipelineErrored& OnMoviePipelineErrored()
	{
		return OnMoviePipelineErroredDelegate;
	}

	/**
	* Get the Master Configuration used to render this shot. This contains the global settings for the shot, as well as per-shot
	* configurations which can contain their own settings.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMoviePipelineMasterConfig* GetPipelineMasterConfig() const;
public:

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	ULevelSequence* GetTargetSequence() const { return TargetSequence; }

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	const TArray<FMoviePipelineShotInfo>& GetShotList() const { return ShotList; }

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	int32 GetCurrentShotIndex() const { return CurrentShotIndex; }

	UMoviePipelineExecutorJob* GetCurrentJob() const { return CurrentJob; }
public:
	void OnFrameCompletelyRendered(FMoviePipelineMergerOutputFrame&& OutputFrame, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData);
	void OnSampleRendered(TUniquePtr<FImagePixelData>&& OutputSample, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData);
private:

	/** Instantiate our Debug UI Widget and initialize it to ourself. */
	void LoadDebugWidget();
	
	/** Called before the Engine ticks for the given frame. We use this to calculate delta times that the frame should use. */
	void OnEngineTickBeginFrame();
	
	/** Called after the Engine has ticked for a given frame. Everything in the world has been updated by now so we can submit things to render. */
	void OnEngineTickEndFrame();

	void ValidateSequenceAndSettings();


	/** Runs the per-tick logic when doing the ProducingFrames state. */
	void TickProducingFrames();

	bool ProcessEndOfCameraCut(FMoviePipelineShotInfo &CurrentShot, FMoviePipelineCameraCutInfo &CurrentCameraCut);


	/** Called once when first moving to the Finalize state. */
	void BeginFinalize();

	/** 
	* Runs the per-tick logic when doing the Finalize state.
	* @param bInForceFinish		If true, this function will not return until all Output Containers say they have finalized.
	*/
	void TickFinalizeOutputContainers(const bool bInForceFinish);
	/** 
	* Runs the per-tick logic when doing the Export state. This is spread over multiple ticks to allow non-blocking background 
	* processes (such as extra encoding) 
	*/
	void TickPostFinalizeExport();

	/** Return true if we should early out of the TickProducingFrames function. Decrements the remaining number of steps when false. */
	bool DebugFrameStepPreTick();
	/** Returns true if we are idling because of debug frame stepping. */
	bool IsDebugFrameStepIdling() const;

	/** Debugging/Information. Don't hinge any logic on this as it will get called multiple times per frame in some cases. */
	void OnSequenceEvaluated(const UMovieSceneSequencePlayer& Player, FFrameTime CurrentTime, FFrameTime PreviousTime);

	/** Set up per-shot state for the specific shot, tearing down old state (if it exists) */
	void InitializeShot(FMoviePipelineShotInfo& InShot);
	void TeardownShot(FMoviePipelineShotInfo& InShot);

	/** Initialize the rendering pipeline for the given shot. This should not get called if rendering work is still in progress for a previous shot. */
	void SetupRenderingPipelineForShot(FMoviePipelineShotInfo& InShot);
	/** Deinitialize the rendering pipeline for the given shot. */
	void TeardownRenderingPipelineForShot(FMoviePipelineShotInfo& InShot);

	/** Flush any async resources in the engine that need to be finalized before submitting anything to the GPU, ie: Streaming Levels and Shaders */
	void FlushAsyncEngineSystems();

	template<typename SettingType>
	SettingType* FindOrAddSetting(const FMoviePipelineShotInfo& InShot) const
	{
		return (SettingType*)FindOrAddSetting(SettingType::StaticClass(), InShot);
	}

	UMoviePipelineSetting* FindOrAddSetting(TSubclassOf<UMoviePipelineSetting> InSetting, const FMoviePipelineShotInfo& InShot) const;
	
	/** 
	* Renders the next frame in the Pipeline. This updates/ticks all scene view render states
	* and produces data. This may not result in an output frame due to multiple renders 
	* accumulating together to produce an output. frame.
	* Should not be called if we're idling (debug), not initialized yet, or finalizing/exporting. 
	*/
	void RenderFrame();

	/** Allow any Settings to modify the (already duplicated) sequence. This allows inserting automatic pre-roll, etc. */
	void ModifySequenceViaExtensions(ULevelSequence* InSequence);

	/** Calculate the expected amount of total work that this Movie Pipeline is expected to do. */
	FMoviePipelineWorkInfo CalculateExpectedOutputMetrics();
private:
	/** Initialize a new Level Sequence Actor to evaluate our target sequence. Disables any existing Level Sequences pointed at our original sequence. */
	void InitializeLevelSequenceActor(ULevelSequence* OriginalLevelSequence, ULevelSequence* InSequenceToApply);

	/** This converts the sequence into a Shot List and expands bounds.*/
	TArray<FMoviePipelineShotInfo> BuildShotListFromSequence(const ULevelSequence* InSequence);

	/** 
	* Modifies the TargetSequence to ensure that only the specified Shot has it's associated Cinematic Shot Section enabled.
	* This way when Handle Frames are enabled and the sections are expanded, we don't end up evaluating the previous shot. 
	*/
	void SetSoloShot(const FMoviePipelineShotInfo& InShot);

	/* Expands the specified shot (and contained camera cuts)'s ranges for the given settings. */
	void ExpandShot(FMoviePipelineShotInfo& InShot);

	/** Calculates lots of useful numbers used in timing based off of the current shot. These are constant for a given shot. */
	MoviePipeline::FFrameConstantMetrics CalculateShotFrameMetrics(const FMoviePipelineShotInfo& InShot) const;

	/** It can be useful to know where the data we're generating was relative to the original Timeline, so this calculates that. */
	void CalculateFrameNumbersForOutputState(const MoviePipeline::FFrameConstantMetrics& InFrameMetrics, const FMoviePipelineCameraCutInfo& InCameraCut, FMoviePipelineFrameOutputState& InOutOutputState) const;

private:
	/** Custom TimeStep used to drive the engine while rendering. */
	UPROPERTY(Transient, Instanced)
	UMoviePipelineCustomTimeStep* CustomTimeStep;

	/** Custom Time Controller for the Sequence Player, used to match Custom TimeStep without any floating point accumulation errors. */
	TSharedPtr<FMoviePipelineTimeController> CustomSequenceTimeController;

	/** Hold a reference to the existing custom time step (if any) so we can restore it after we're done using our custom one. */
	UPROPERTY(Transient)
	UEngineCustomTimeStep* CachedPrevCustomTimeStep;

	/** This is our duplicated sequence that we're rendering. This will get modified throughout the rendering process. */
	UPROPERTY(Transient)
	ULevelSequence* TargetSequence;

	/** The Level Sequence Actor we spawned to play our TargetSequence. */
	UPROPERTY(Transient)
	ALevelSequenceActor* LevelSequenceActor;

	/** The Debug UI Widget that is spawned and placed on the player UI */
	UPROPERTY(Transient)
	UMovieRenderDebugWidget* DebugWidget;

	/** A list of all of the shots we are going to render out from this sequence. */
	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	TArray<FMoviePipelineShotInfo> ShotList;

	/** What state of the overall flow are we in? See enum for specifics. */
	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	EMovieRenderPipelineState PipelineState;

	/** What is the index of the shot we are working on. -1 if not initialized, may be greater than ShotList.Num() if we've reached the end. */
	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	int32 CurrentShotIndex;

	/** The time (in UTC) that Initialize was called. Used to track elapsed time. */
	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	FDateTime InitializationTime;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	FMoviePipelineFrameOutputState CachedOutputState;

	/** 
	* Cache the overall expected work for the entire Movie Pipeline. Used to compare current progress against total for progress.
	* Current Progress is tracked by individual shots.
	*/
	FMoviePipelineWorkInfo TotalExpectedWork;

	/** 
	* Have we hit the callback for the BeginFrame at least once? This solves an issue where the delegates
	* get registered mid-frame so you end up calling EndFrame before BeginFrame which is undesirable.
	*/
	bool bHasRunBeginFrameOnce;
	/** Should we pause the game at the end of the frame? Used to implement frame step debugger. */
	bool bPauseAtEndOfFrame;

	/** When using temporal sub-frame stepping common counts (such as 3) don't result in whole ticks. We keep track of how many ticks we lose so we can add them the next time there's a chance. */
	float AccumulatedTickSubFrameDeltas;

	/** Called when we have completely finished. This object will call Shutdown before this and stop ticking. */
	FMoviePipelineFinished OnMoviePipelineFinishedDelegate;

	/** Called when there is a warning/error that the user should pay attention to.*/
	FMoviePipelineErrored OnMoviePipelineErroredDelegate;

	/**
	 * We have to apply camera motion vectors manually. So we keep the current and previous fram'es camera view and rotation.
	 * Then we render a sequence of the same movement, and update after running the game sim.
	 **/
	MoviePipeline::FMoviePipelineFrameInfo FrameInfo;

public:
	/** A list of engine passes which need to be run each frame to generate required content for all the movie render passes. */
	TArray<TSharedPtr<MoviePipeline::FMoviePipelineEnginePass>> ActiveRenderPasses;

	/** This gathers all of the produced data for an output frame (which may come in async many frames later) before passing them onto the Output Containers. */
	TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputBuilder;

	/** A debug image sequence writer in the event they want to dump every sample generated on its own. */
	IImageWriteQueue* ImageWriteQueue;

private:
	/** Keep track of which job we're working on. This holds our Configuration + which shots we're supposed to render from it. */
	UPROPERTY(Transient)
	UMoviePipelineExecutorJob* CurrentJob;
};

UCLASS()
class UMoviePipelineCustomTimeStep : public UEngineCustomTimeStep
{
	GENERATED_BODY()

public:
	// UEngineCustomTimeStep Interface
	virtual bool Initialize(UEngine* InEngine) override { return true; }
	virtual void Shutdown(UEngine* InEngine) override {}
	virtual bool UpdateTimeStep(UEngine* InEngine) override;
	virtual ECustomTimeStepSynchronizationState GetSynchronizationState() const override { return ECustomTimeStepSynchronizationState::Synchronized; }
	// ~UEngineCustomTimeStep Interface

	void SetCachedFrameTiming(const MoviePipeline::FFrameTimeStepCache& InTimeCache) { TimeCache = InTimeCache; }

private:
	/** We don't do any thinking on our own, instead we just spit out the numbers stored in our time cache. */
	MoviePipeline::FFrameTimeStepCache TimeCache;
};

struct FMoviePipelineTimeController : public FMovieSceneTimeController
{
	virtual FFrameTime OnRequestCurrentTime(const FQualifiedFrameTime& InCurrentTime, float InPlayRate) override;
	void SetCachedFrameTiming(const FQualifiedFrameTime& InTimeCache) { TimeCache = InTimeCache; }

private:
	/** Simply store the number calculated and return it when requested. */
	FQualifiedFrameTime TimeCache;
};