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
class UTexture;



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
	* Request the movie pipeline to shut down at the next available time. The pipeline will attempt to abandon
	* the current frame (such as if there are more temporal samples pending) but may be forced into finishing if
	* there are spatial samples already submitted to the GPU. The shutdown flow will be run to ensure already
	* completed work is written to disk. This is a non-blocking operation, use Shutdown() instead if you need to
	* block until it is fully shut down.
	*
	* This function is thread safe.
	*/
	void RequestShutdown();
	
	/** 
	* Abandons any future work on this Movie Pipeline and runs through the shutdown flow to ensure already
	* completed work is written to disk. This is a blocking-operation and will not return until all outstanding
	* work has been completed.
	*
	* This function should only be called from the game thread.
	*/
	void Shutdown();

	/**
	* Has RequestShutdown() been called?
	*/
	bool IsShutdownRequested() const { return bShutdownRequested; }

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
	ULevelSequence* GetTargetSequence() const { return TargetSequence; }

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UTexture* GetPreviewTexture() const { return PreviewTexture; }

	void SetPreviewTexture(UTexture* InTexture) { PreviewTexture = InTexture; }

	const TArray<FMoviePipelineShotInfo>& GetShotList() const { return ShotList; }

	int32 GetCurrentShotIndex() const { return CurrentShotIndex; }
	const FMoviePipelineFrameOutputState& GetOutputState() const { return CachedOutputState; }

	UMoviePipelineExecutorJob* GetCurrentJob() const { return CurrentJob; }
	EMovieRenderPipelineState GetPipelineState() const { return PipelineState; }

	FDateTime GetInitializationTime() const { return InitializationTime; }
public:
	void ProcessOutstandingFinishedFrames();
	void OnSampleRendered(TUniquePtr<FImagePixelData>&& OutputSample, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData);
	const MoviePipeline::FAudioState& GetAudioState() const { return AudioState; }
public:
	template<typename SettingType>
	SettingType* FindOrAddSetting(const FMoviePipelineShotInfo& InShot) const
	{
		return (SettingType*)FindOrAddSetting(SettingType::StaticClass(), InShot);
	}

	UMoviePipelineSetting* FindOrAddSetting(TSubclassOf<UMoviePipelineSetting> InSetting, const FMoviePipelineShotInfo& InShot) const;
	
	/**
	* Resolves the provided InFormatString by converting {format_strings} into settings provided by the master config.
	* @param	InFormatString		A format string (in the form of "{format_key1}_{format_key2}") to resolve.
	* @param	InOutputState		The output state for frame information.
	* @param	InFormatOverrides	A series of Key/Value pairs to override particular format keys. Useful for things that
	*								change based on the caller such as filename extensions.
	*/
	FString ResolveFilenameFormatArguments(const FString& InFormatString, const FMoviePipelineFrameOutputState& InOutputState, const FStringFormatNamedArguments& InFormatOverrides) const;

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

	void ProcessEndOfCameraCut(FMoviePipelineShotInfo &CurrentShot, FMoviePipelineCameraCutInfo &CurrentCameraCut);


	/** Called once when first moving to the Finalize state. */
	void BeginFinalize();
	/** Called once when first moving to the Export state. */
	void BeginExport();

	/** 
	* Runs the per-tick logic when doing the Finalize state.
	*
	* @param bInForceFinish		If true, this function will not return until all Output Containers say they have finalized.
	*/
	void TickFinalizeOutputContainers(const bool bInForceFinish);
	/** 
	* Runs the per-tick logic when doing the Export state. This is spread over multiple ticks to allow non-blocking background 
	* processes (such as extra encoding).
	*
	* @param bInForceFinish		If true, this function will not return until all exports say they have finished.
	*/
	void TickPostFinalizeExport(const bool bInForceFinish);

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


	/** Tell our submixes to start capturing the data they are generating. Should only be called once output frames are being produced. */
	void StartAudioRecording();
	/** Tell our submixes to stop capturing the data, and then store a copy of it. */
	void StopAudioRecording();
	/** Attempt to process the audio thread work. This is complicated by our non-linear time steps */
	void ProcessAudioTick();
	void SetupAudioRendering();
	void TeardownAudioRendering();
	
	/** 
	* Renders the next frame in the Pipeline. This updates/ticks all scene view render states
	* and produces data. This may not result in an output frame due to multiple renders 
	* accumulating together to produce an output. frame.
	* Should not be called if we're idling (debug), not initialized yet, or finalizing/exporting. 
	*/
	void RenderFrame();

	/** Allow any Settings to modify the (already duplicated) sequence. This allows inserting automatic pre-roll, etc. */
	void ModifySequenceViaExtensions(ULevelSequence* InSequence);

	/**
	* Should the Progress UI be visible on the player's screen?
	*/
	void SetProgressWidgetVisible(bool bVisible);



private:
	/** Iterates through the changes we've made to a shot and applies the original settings. */
	void RestoreTargetSequenceToOriginalState();

	/** Initialize a new Level Sequence Actor to evaluate our target sequence. Disables any existing Level Sequences pointed at our original sequence. */
	void InitializeLevelSequenceActor();

	/** This builds the shot list from the target sequence, and expands Playback Bounds to cover any future evaluation we may need. */
	void BuildShotListFromSequence();

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

	/** Handles transitioning between states, preventing reentrancy. Normal state flow should be respected, does not handle arbitrary x to y transitions. */
	void TransitionToState(const EMovieRenderPipelineState InNewState);
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

	UPROPERTY(Transient)
	UTexture* PreviewTexture;

	/** A list of all of the shots we are going to render out from this sequence. */
	TArray<FMoviePipelineShotInfo> ShotList;

	/** What state of the overall flow are we in? See enum for specifics. */
	EMovieRenderPipelineState PipelineState;

	/** What is the index of the shot we are working on. -1 if not initialized, may be greater than ShotList.Num() if we've reached the end. */
	int32 CurrentShotIndex;

	/** The time (in UTC) that Initialize was called. Used to track elapsed time. */
	FDateTime InitializationTime;

	FMoviePipelineFrameOutputState CachedOutputState;

	MoviePipeline::FAudioState AudioState;

	/** Cached state of GAreScreenMessagesEnabled. We disable them since some messages are written to the FSceneView directly otherwise. */
	bool bPrevGScreenMessagesEnabled;

	/** 
	* Have we hit the callback for the BeginFrame at least once? This solves an issue where the delegates
	* get registered mid-frame so you end up calling EndFrame before BeginFrame which is undesirable.
	*/
	bool bHasRunBeginFrameOnce;
	/** Should we pause the game at the end of the frame? Used to implement frame step debugger. */
	bool bPauseAtEndOfFrame;

	/** True if RequestShutdown() was called. At the start of the next frame we will stop producing frames (if needed) and start shutting down. */
	FThreadSafeBool bShutdownRequested;

	/** True if we're in a TransitionToState call. Used to prevent reentrancy. */
	bool bIsTransitioningState;

	/** When using temporal sub-frame stepping common counts (such as 3) don't result in whole ticks. We keep track of how many ticks we lose so we can add them the next time there's a chance. */
	float AccumulatedTickSubFrameDeltas;

	/** Called when we have completely finished. This object will call Shutdown before this and stop ticking. */
	FMoviePipelineFinished OnMoviePipelineFinishedDelegate;

	/** Called when there is a warning/error that the user should pay attention to.*/
	FMoviePipelineErrored OnMoviePipelineErroredDelegate;

	/**
	 * We have to apply camera motion vectors manually. So we keep the current and previous frame's camera view and rotation.
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


	/** Previous values for data that we modified in the sequence for restoration in shutdown. */
	struct FMovieSceneChanges
	{
		// Master level settings
		EMovieSceneEvaluationType EvaluationType;
		TRange<FFrameNumber> PlaybackRange;
		bool bSequenceReadOnly;
		bool bSequencePlaybackRangeLocked;

		struct FSegmentChange
		{
			TWeakObjectPtr<class UMovieScene> MovieScene;
			TRange<FFrameNumber> MovieScenePlaybackRange;
			bool bMovieSceneReadOnly;
			TWeakObjectPtr<UMovieSceneCinematicShotSection> ShotSection;
			bool bShotSectionIsLocked;
			TRange<FFrameNumber> ShotSectionRange;
		};

		// Shot-specific settings
		TArray<FSegmentChange> Segments;
	};

	FMovieSceneChanges SequenceChanges;
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