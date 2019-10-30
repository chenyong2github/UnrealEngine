// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Engine/EngineCustomTimeStep.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MovieSceneTimeController.h"
#include "MoviePipeline.generated.h"

// Forward Declares
class UMovieRenderPipelineConfig;
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

DECLARE_MULTICAST_DELEGATE_OneParam(FMoviePipelineFinished, UMoviePipeline*);

UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipeline : public UObject
{
	GENERATED_BODY()
	
public:
	UMoviePipeline();

	/** 
	* Initialize the movie pipeline with the specified settings. This kicks off the rendering process. 
	
	* @param InInitSettings - Settings related to the context the pipeline is being executed in. Not configuration for the Pipeline.
	* @param InConfig		- What settings should the Pipeline use to render with? This should be filled out completely
	*						  before starting the pipeline. Any overrides based on command line or other should be done
	*						  before the pipeline is started.
	*/
	void Initialize(UMovieRenderPipelineConfig* InConfig);

	void LoadDebugWidget();

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

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	FMoviePipelineShotCache GetCurrentShotSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	FMoviePipelineShotCutCache GetCurrentCameraCutSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	FMoviePipelineFrameOutputState GetOutputStateSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	ULevelSequence* GetTargetSequence() const { return TargetSequence; }

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMovieRenderPipelineConfig* GetPipelineConfig() const { return Config; }

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	FFrameNumber GetTotalOutputFrameCountEstimate() const;

	/** 
	* Returns an estimate based on the average time taken to render all previous frames.
	* Will be incorrect if PlayRate tracks are in use. Will be inaccurate when different
	* shots take significantly different amounts of time to render.
	*
	* @param OutTimespan: The returned estimated time left. Will be default initialized if there is no estimate.
	* @return True if we can make a reasonable estimate, false otherwise (ie: no rendering has been done to estimate).
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	bool GetRemainingTimeEstimate(FTimespan& OutTimespan) const;

	const TArray<FMoviePipelineShotCache>& GetShotList() const { return ShotList; }
	int32 GetCurrentShotIndex() const { return CurrentShotIndex; }

	TSharedPtr<FImagePixelPipe, ESPMode::ThreadSafe> GetOutputPipe() const { return OutputPipe; }
private:

	void OnEngineTickBeginFrame();
	void TickProducingFrames();

	/** Return true if we should early out of the Tick function */
	bool DebugFrameStepPreTick();
	/** Returns true if we are idling because of debug frame stepping. */
	bool IsDebugFrameStepIdling() const;

	void OnSequenceEvaluated(const UMovieSceneSequencePlayer& Player, FFrameTime CurrentTime, FFrameTime PreviousTime);

	void OnEngineTickEndFrame();
	void TickFinalizeOutputContainers(const bool bInForceFinish);
	void PostFinalizeExport();
	void TickPostFinalizeExport();
	void InitializeShot(FMoviePipelineShotCache& InShot);
	void SetupRenderingPipelineForShot(FMoviePipelineShotCache& InShot);
	void TeardownRenderingPipelineForShot(FMoviePipelineShotCache& InShot);

	void BeginFinalize();
	void FlushAsyncSystems();
	/** 
	* Renders the next frame in the Pipeline. This updates/ticks all scene view render states
	* and produces data. This may not result in an output frame due to multiple renders 
	* accumulating together to produce an output. frame.
	* Should not be called if we're idling (debug) or not initialized yet. 
	*/
	void RenderFrame();
private:
	ULevelSequence* CreateCopyOfSequence(ULevelSequence* InSequence);
	void InitializeLevelSequenceActor(ULevelSequence* OriginalLevelSequence, ULevelSequence* InSequenceToApply);

	/** Modifies the TargetSequence to ensure that only the specified Shot has it's associated Cinematic Shot Section enabled. */
	void SetSoloShot(const FMoviePipelineShotCache& InShot);
	void SetSoloCameraCut(FMoviePipelineShotCutCache& InCameraCut);

	/** This converts the sequence into a Shot List and expands bounds.*/
	TArray<FMoviePipelineShotCache> BuildShotListFromSequence(const ULevelSequence* InSequence);

	/* Tick Resolution Frames*/
	FFrameNumber GetMotionBlurDuration(int32 InShotIndex) const;
	/* Tick Resolution Frames*/
	FFrameNumber CalculateHandleFrameDuration(const FMoviePipelineShotCache& InShot) const;
	
	MoviePipeline::FFrameConstantMetrics CalculateShotFrameMetrics(const FMoviePipelineShotCache& InShot) const;

	void CalculateFrameNumbersForOutputState(const MoviePipeline::FFrameConstantMetrics& InFrameMetrics, const FMoviePipelineShotCutCache& InCameraCut, FMoviePipelineFrameOutputState& InOutOutputState) const;

	/* Expands the specified shot by the specified offsets. Offsets should be in TickResolution */
	void ExpandShot(FMoviePipelineShotCache& InShot);

private:
	UPROPERTY(Transient, Instanced)
	UMoviePipelineCustomTimeStep* CustomTimeStep;

	TSharedPtr<FMoviePipelineTimeController> CustomSequenceTimeController;

	/** Hold a reference to the existing custom time step (if any) so we can restore it after we're done using our custom one. */
	UPROPERTY(Transient)
	UEngineCustomTimeStep* CachedCustomTimeStep;

	UPROPERTY(Transient, BlueprintReadOnly, meta=(AllowPrivateAccess=true), Category = "Movie Render Pipeline")
	ULevelSequence* TargetSequence;

	UPROPERTY(Transient)
	ALevelSequenceActor* LevelSequenceActor;

	UPROPERTY(Transient)
	UMovieRenderDebugWidget* DebugWidget;

	/** A list of all of the shots we are going to render out from this sequence. */
	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	TArray<FMoviePipelineShotCache> ShotList;

	/** What state of the overall flow are we in? See enum for specifics. */
	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	EMovieRenderPipelineState PipelineState;

	/** What is the index of the shot we are working on. -1 if not initialized, may be greater than ShotList.Num() if we've reached the end. */
	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	int32 CurrentShotIndex;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	FMoviePipelineFrameOutputState CachedOutputState;

	/** The time (in UTC) that Initialize was called. Used to track elapsed time. */
	UPROPERTY(Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = true), Category = "Movie Render Pipeline")
	FDateTime InitializationTime;

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

	/** This gathers all of the produced data for an output frame (which may come in async many frames later) before passing them onto the Output Containers. */
	TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputBuilder;

	// Debug Rendering
	TSharedPtr<FMovieRenderViewport> DummyViewport;
	TSharedPtr<FMovieRenderViewportClient> ViewportClient;


	TSharedPtr<FImagePixelPipe, ESPMode::ThreadSafe> OutputPipe;

private:
	/** The overall configuration for how this Pipeline executes. Should be immutable. */
	UPROPERTY(Transient)
	UMovieRenderPipelineConfig* Config;
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