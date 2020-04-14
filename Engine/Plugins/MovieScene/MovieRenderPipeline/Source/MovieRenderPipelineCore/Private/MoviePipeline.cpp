// Copyright Epic Games, Inc. All Rights Reserved.
#include "MoviePipeline.h"
#include "MovieScene.h"
#include "MovieRenderPipelineCoreModule.h"
#include "LevelSequence.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "LevelSequenceActor.h"
#include "EngineUtils.h"
#include "MovieSceneSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "CanvasTypes.h"
#include "AudioDeviceManager.h"
#include "MovieSceneTimeHelpers.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/PlayerController.h"
#include "MovieRenderDebugWidget.h"
#include "MoviePipelineShotConfig.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineRenderPass.h"
#include "MoviePipelineOutputBase.h"
#include "ShaderCompiler.h"
#include "ImageWriteStream.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineOutputBuilder.h"
#include "DistanceFieldAtlas.h"
#include "UObject/SoftObjectPath.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineBlueprintLibrary.h"
#include "ImageWriteQueue.h"
#include "MoviePipelineHighResSetting.h"
#include "MoviePipelineCameraSetting.h"
#include "MoviePipelineQueue.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "MoviePipeline"

static TAutoConsoleVariable<int32> CVarMovieRenderPipelineFrameStepper(
	TEXT("MovieRenderPipeline.FrameStepDebug"),
	-1,
	TEXT("How many frames should the Movie Render Pipeline produce before pausing. Set to zero on launch to stall at the first frame. Debug tool.\n")
	TEXT("-1: Don't pause after each frame (default)\n")
	TEXT("0: Process engine ticks but don't progress in the movie rendering pipeline.\n")
	TEXT("1+: Run this many loops of the movie rendering pipeline before pausing again.\n"),
	ECVF_Default);


UMoviePipeline::UMoviePipeline()
	: CustomTimeStep(nullptr)
	, CachedPrevCustomTimeStep(nullptr)
	, TargetSequence(nullptr)
	, LevelSequenceActor(nullptr)
	, DebugWidget(nullptr)
	, PipelineState(EMovieRenderPipelineState::Uninitialized)
	, CurrentShotIndex(-1)
	, bPrevGScreenMessagesEnabled(true)
	, bHasRunBeginFrameOnce(false)
	, bPauseAtEndOfFrame(false)
	, bShutdownRequested(false)
	, bIsTransitioningState(false)
	, AccumulatedTickSubFrameDeltas(0.f)
	, CurrentJob(nullptr)
{
	CustomTimeStep = CreateDefaultSubobject<UMoviePipelineCustomTimeStep>("MoviePipelineCustomTimeStep");
	CustomSequenceTimeController = MakeShared<FMoviePipelineTimeController>();
	OutputBuilder = MakeShared<FMoviePipelineOutputMerger, ESPMode::ThreadSafe>(this);

	ImageWriteQueue = &FModuleManager::Get().LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue").GetWriteQueue();
}


void UMoviePipeline::ValidateSequenceAndSettings()
{
	// ToDo: 
	// Warn for Blueprint Streaming Levels
	// Warn for sections that aren't extended far enough (once handle frames are taken into account)
	// Warn for not whole frame aligned sections

	// Check to see if they're trying to output alpha and don't have the required project setting set.
	{
		IConsoleVariable* TonemapAlphaCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessing.PropagateAlpha"));
		check(TonemapAlphaCVar);

		TArray<UMoviePipelineOutputBase*> OutputSettings = GetPipelineMasterConfig()->FindSettings<UMoviePipelineOutputBase>();
		bool bAnyOutputWantsAlpha = false;

		for (const UMoviePipelineOutputBase* Output : OutputSettings)
		{
			bAnyOutputWantsAlpha |= Output->IsAlphaSupported();
		}

		if (bAnyOutputWantsAlpha && TonemapAlphaCVar->GetInt() == 0)
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("An output requested Alpha Support but the required project setting is not enabled! Go to Project Settings > Rendering > Tonemapping > 'Enable Alpha Channel Support in Post Processing' and set it to 'Linear Color Space Only'."));
		}
	}
}

void UMoviePipeline::Initialize(UMoviePipelineExecutorJob* InJob)
{
	// This function is called after the PIE world has finished initializing, but before
	// the PIE world is ticked for the first time. We'll end up waiting for the next tick
	// for FCoreDelegateS::OnBeginFrame to get called to actually start processing.
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Initializing overall Movie Pipeline"), GFrameCounter);

	bPrevGScreenMessagesEnabled = GAreScreenMessagesEnabled;
	GAreScreenMessagesEnabled = false;

	if (!ensureAlwaysMsgf(InJob, TEXT("MoviePipeline cannot be initialized with null job. Aborting.")))
	{
		OnMoviePipelineErroredDelegate.Broadcast(this, true, LOCTEXT("MissingJob", "Job was not specified, movie render is aborting."));

		Shutdown();
		return;
	}
	
	if (!ensureAlwaysMsgf(InJob->GetConfiguration(), TEXT("MoviePipeline cannot be initialized with null configuration. Aborting.")))
	{
		OnMoviePipelineErroredDelegate.Broadcast(this, true, LOCTEXT("MissingConfiguration", "Job did not specify a configuration, movie render is aborting."));

		Shutdown();
		return;
	}

	if (!ensureAlwaysMsgf(PipelineState == EMovieRenderPipelineState::Uninitialized, TEXT("Pipeline cannot be reused. Create a new pipeline to execute a job.")))
	{
		OnMoviePipelineErroredDelegate.Broadcast(this, true, LOCTEXT("DontReusePipeline", "Attempted to reuse an existing Movie Pipeline. Initialize a new pipeline instead of reusing an existing one."));

		Shutdown();
		return;
	}

	// Ensure this object has the World as part of its Outer (so that it has context to spawn things)
	if (!ensureAlwaysMsgf(GetWorld(), TEXT("Pipeline does not contain the world as an outer.")))
	{
		OnMoviePipelineErroredDelegate.Broadcast(this, true, LOCTEXT("MissingWorld", "Could not find World in the Outer Path for Pipeline. The world must be an outer to give the Pipeline enough context to spawn things."));

		Shutdown();
		return;
	}

	CurrentJob = InJob;
	
	ULevelSequence* OriginalSequence = Cast<ULevelSequence>(InJob->Sequence.TryLoad());
	if (!ensureAlwaysMsgf(OriginalSequence, TEXT("Failed to load Sequence Asset from specified path, aborting movie render! Attempted to load Path: %s"), *InJob->Sequence.ToString()))
	{
		OnMoviePipelineErroredDelegate.Broadcast(this, true, LOCTEXT("MissingSequence", "Could not load sequence asset, movie render is aborting. Check logs for additional details."));

		Shutdown();
		return;
	}

	TargetSequence = Cast<ULevelSequence>(GetCurrentJob()->TryLoadSequence());

	// Disable some user settings that conflict with our need to mutate the data.
	{
		// Movie Scene Read Only
		SequenceChanges.bSequenceReadOnly = TargetSequence->GetMovieScene()->IsReadOnly();
		TargetSequence->GetMovieScene()->SetReadOnly(false);
		
		// Playback Range locked
		SequenceChanges.bSequencePlaybackRangeLocked = TargetSequence->GetMovieScene()->IsPlaybackRangeLocked();
		TargetSequence->GetMovieScene()->SetPlaybackRangeLocked(false);

		// Force Frame-locked evaluation off on the sequence. We control time and will respect that, but need it off for subsampling.
		SequenceChanges.EvaluationType = TargetSequence->GetMovieScene()->GetEvaluationType();
		TargetSequence->GetMovieScene()->SetEvaluationType(EMovieSceneEvaluationType::WithSubFrames);
	}



	// Override the frame range on the target sequence if needed first before anyone has a chance to modify it.
	{
		SequenceChanges.PlaybackRange = TargetSequence->GetMovieScene()->GetPlaybackRange();

		UMoviePipelineOutputSetting* OutputSetting = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
		if (OutputSetting->bUseCustomPlaybackRange)
		{
			FFrameNumber StartFrameTickResolution = FFrameRate::TransformTime(FFrameTime(FFrameNumber(OutputSetting->CustomStartFrame)), TargetSequence->GetMovieScene()->GetDisplayRate(), TargetSequence->GetMovieScene()->GetTickResolution()).FloorToFrame();
			FFrameNumber EndFrameTickResolution = FFrameRate::TransformTime(FFrameTime(FFrameNumber(OutputSetting->CustomEndFrame)), TargetSequence->GetMovieScene()->GetDisplayRate(), TargetSequence->GetMovieScene()->GetTickResolution()).CeilToFrame();

			TRange<FFrameNumber> CustomPlaybackRange = TRange<FFrameNumber>(StartFrameTickResolution, EndFrameTickResolution);
			TargetSequence->GetMovieScene()->SetPlaybackRange(CustomPlaybackRange);
		}
	}
	
	// Initialize all of our master config settings. Shot specific ones will be called for their appropriate shot.
	for (UMoviePipelineSetting* Setting : GetPipelineMasterConfig()->GetAllSettings())
	{
		Setting->OnMoviePipelineInitialized(this);
	}

	// Allow master settings to modify the sequence. This can be useful when working with dynamic content, you might
	// want to modify things in the sequence, or modify things in the world before rendering.
	// @ToDo: ModifySequenceViaExtensions(TargetSequence);

	// Now that we've post-processed it, we're going to run a validation pass on it. This will produce warnings
	// for anything we can't fix that might be an issue - extending sections, etc. This should be const as this
	// validation should re-use what was used in the UI.
	// @ToDo: ValidateSequence(TargetSequence);

	// Now that we've fixed up the sequence and validated it, we're going to build a list of shots that we need
	// to produce in a simplified data structure. The simplified structure makes the flow/debugging easier.
	BuildShotListFromSequence();

	// Finally, we're going to create a Level Sequence Actor in the world that has its settings configured by us.
	// Because this callback is at the end of startup (and before tick) we should be able to spawn the actor
	// and give it a chance to tick once (where it should do nothing) before we start manually manipulating it.
	InitializeLevelSequenceActor();

	// Register any additional engine callbacks needed.
	{
		// Called before the Custom Timestep is updated. This gives us a chance to calculate
		// what we want the frame to look like and then cache that information so that the
		// Custom Timestep doesn't have to perform its own logic.
		FCoreDelegates::OnBeginFrame.AddUObject(this, &UMoviePipeline::OnEngineTickBeginFrame);
		// Called at the end of the frame after everything has been ticked and rendered for the frame.
		FCoreDelegates::OnEndFrame.AddUObject(this, &UMoviePipeline::OnEngineTickEndFrame);
	}

	// Construct a debug UI and bind it to this instance.
	LoadDebugWidget();
	
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->bDisableWorldRendering = true;
	}

	SetupAudioRendering();

	TransitionToState(EMovieRenderPipelineState::ProducingFrames);
	CurrentShotIndex = 0;
	CachedOutputState.ShotCount = ShotList.Num();

	// Initialization is complete. This engine frame is a wash (because the tick started with a 
	// delta time not generated by us) so we'll wait until the next engine frame to start rendering.
	InitializationTime = FDateTime::UtcNow();
}

void UMoviePipeline::RestoreTargetSequenceToOriginalState()
{
	if (PipelineState == EMovieRenderPipelineState::Uninitialized)
	{
		return;
	}

	if (!TargetSequence)
	{
		return;
	}

	TargetSequence->GetMovieScene()->SetEvaluationType(SequenceChanges.EvaluationType);
	TargetSequence->GetMovieScene()->SetPlaybackRange(SequenceChanges.PlaybackRange);
	TargetSequence->GetMovieScene()->SetReadOnly(SequenceChanges.bSequenceReadOnly);
	TargetSequence->GetMovieScene()->SetPlaybackRangeLocked(SequenceChanges.bSequencePlaybackRangeLocked);

	for (FMovieSceneChanges::FSegmentChange& ModifiedSegment : SequenceChanges.Segments)
	{
		if (ModifiedSegment.MovieScene.IsValid())
		{
			ModifiedSegment.MovieScene->SetPlaybackRange(ModifiedSegment.MovieScenePlaybackRange);
			ModifiedSegment.MovieScene->SetReadOnly(ModifiedSegment.bMovieSceneReadOnly);
		}

		if (ModifiedSegment.ShotSection.IsValid())
		{
			ModifiedSegment.ShotSection->SetRange(ModifiedSegment.ShotSectionRange);
			ModifiedSegment.ShotSection->SetIsLocked(ModifiedSegment.bShotSectionIsLocked);
			
			// We only make modified segments for shots that were previously active
			ModifiedSegment.ShotSection->SetIsActive(true);
			ModifiedSegment.ShotSection->MarkAsChanged();
		}
	}
}


void UMoviePipeline::RequestShutdown()
{
	// The user has requested a shutdown, it will be read the next available chance and possibly acted on.
	bShutdownRequested = true;
	switch (PipelineState)
	{
		// It is valid to call Shutdown at any point during these two states.
	case EMovieRenderPipelineState::Uninitialized:
	case EMovieRenderPipelineState::ProducingFrames:
		break;
		// You can call Shutdown during these two, but they won't do anything as we're already shutting down at that point.
	case EMovieRenderPipelineState::Finalize:
	case EMovieRenderPipelineState::Export:
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("[GFrameCounter: %d] Async Shutdown Requested, ignoring due to already being on the way to shutdown."), GFrameCounter);
		break;
	}
}

void UMoviePipeline::Shutdown()
{
	check(IsInGameThread());

	// This is a blocking operation which abandons any outstanding work to be submitted but finishes
	// the existing work already processed.
	if (PipelineState == EMovieRenderPipelineState::Uninitialized)
	{
		// If initialize is not called, no need to do anything.
		return;
	}
	
	if (PipelineState == EMovieRenderPipelineState::Finished)
	{
		// If already shut down, no need to do anything.
		return;
	}

	if (PipelineState == EMovieRenderPipelineState::ProducingFrames)
	{

		// Teardown the currently active shot (if there is one). This will flush any outstanding rendering
		// work that has already submitted - it cannot be canceled, so we may as well execute it and save the results.
		TransitionToState(EMovieRenderPipelineState::Finalize);

		// Abandon the current frame. When using temporal sampling we may had canceled mid-frame, so the rendering
		// commands were never submitted, thus the output builder will still be expecting a frame to come in.
		if (CachedOutputState.TemporalSampleCount > 1)
		{
			OutputBuilder->AbandonOutstandingWork();
		}
	}

	if (PipelineState == EMovieRenderPipelineState::Finalize)
	{
		// We were either in the middle of writing frames to disk, or we have moved to Finalize as a result of the above block.
		// Tick output containers until they report they have finished writing to disk. This is a blocking operation. 
		// Finalize automatically switches our state to Export so no need to manually transition afterwards.
		TickFinalizeOutputContainers(true);
	}

	if (PipelineState == EMovieRenderPipelineState::Export)
	{
		// All frames have been written to disk but we're doing a post-export step (such as encoding). Flush this operation as well.
		// Export automatically switches our state to Finished so no need to manually transition afterwards.
		TickPostFinalizeExport(true);
	}
}

void UMoviePipeline::TransitionToState(const EMovieRenderPipelineState InNewState)
{
	// No re-entrancy. This isn't an error as tearing down a shot may try to move to
	// Finalize on its own, but we don't want that.
	if (bIsTransitioningState)
	{
		return;
	}

	TGuardValue<bool> StateTransitionGuard(bIsTransitioningState, true);

	bool bInvalidTransition = true;
	switch (PipelineState)
	{
	case EMovieRenderPipelineState::Uninitialized:
		PipelineState = InNewState;
		bInvalidTransition = false;
		break;
	case EMovieRenderPipelineState::ProducingFrames:
		if (InNewState == EMovieRenderPipelineState::Finalize)
		{
			bInvalidTransition = false;

			// If we had naturally finished the last shot before doing this transition it will have
			// already been torn down, so this only catches mid-shot transitions to ensure teardown.
			if (CurrentShotIndex < ShotList.Num())
			{
				FMoviePipelineShotInfo& CurrentShot = ShotList[CurrentShotIndex];

				// Ensures all in-flight work for that shot is handled.
				TeardownShot(CurrentShot);
			}

			// Unregister our OnEngineTickEndFrame delegate. We don't unregister BeginFrame as we need
			// to continue to call it to allow ticking the Finalization stage.
			FCoreDelegates::OnEndFrame.RemoveAll(this);

			// Reset the Custom Timestep because we don't care how long the engine takes now
			GEngine->SetCustomTimeStep(CachedPrevCustomTimeStep);

			// Ensure all frames have been processed by the GPU and sent to the Output Merger
			FlushRenderingCommands();

			// And then make sure all frames are sent to the Output Containers before we finalize.
			ProcessOutstandingFinishedFrames();

			PreviewTexture = nullptr;

			// This is called once notifying output containers that all frames that will be submitted have been submitted.
			PipelineState = EMovieRenderPipelineState::Finalize;
			BeginFinalize();
		}
		break;
	case EMovieRenderPipelineState::Finalize:
		if (InNewState == EMovieRenderPipelineState::Export)
		{
			bInvalidTransition = false;

			// This is called once notifying our export step that they can begin the export.
			PipelineState = EMovieRenderPipelineState::Export;
			BeginExport();
		}
		break;
	case EMovieRenderPipelineState::Export:
		if (InNewState == EMovieRenderPipelineState::Finished)
		{
			bInvalidTransition = false;
			PipelineState = InNewState;

			// Uninitialize our master config settings.
			for (UMoviePipelineSetting* Setting : GetPipelineMasterConfig()->GetAllSettings())
			{
				Setting->OnMoviePipelineShutdown(this);
			}

			// Restore any custom Time Step that may have been set before.
			GEngine->SetCustomTimeStep(CachedPrevCustomTimeStep);

			// Ensure our delegates don't get called anymore as we're going to become null soon.
			FCoreDelegates::OnBeginFrame.RemoveAll(this);
			FCoreDelegates::OnEndFrame.RemoveAll(this);

			if (DebugWidget)
			{
				DebugWidget->RemoveFromParent();
				DebugWidget = nullptr;
			}

			for (UMoviePipelineOutputBase* Setting : GetPipelineMasterConfig()->GetOutputContainers())
			{
				Setting->OnPipelineFinished();
			}

			TeardownAudioRendering();
			RestoreTargetSequenceToOriginalState();

			if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
			{
				Viewport->bDisableWorldRendering = false;
			}

			GAreScreenMessagesEnabled = bPrevGScreenMessagesEnabled;

			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Movie Pipeline completed. Duration: %s"), *(FDateTime::UtcNow() - InitializationTime).ToString());

			OnMoviePipelineFinished().Broadcast(this);
		}
		break;
	}

	if (!ensureAlwaysMsgf(!bInvalidTransition, TEXT("[GFrameCounter: %d] An invalid transition was requested (from: %d to: %d), ignoring transition request."),
		GFrameCounter, PipelineState, InNewState))
	{
		return;
	}


}


void UMoviePipeline::OnEngineTickBeginFrame()
{
	UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("OnEngineTickBeginFrame (Start) Engine Frame: [%d]"), GFrameCounter);
	
	// We should have a custom timestep set up by now.
	check(CustomTimeStep);

	switch (PipelineState)
	{
	case EMovieRenderPipelineState::Uninitialized:
		// We shouldn't register this delegate until we're initialized.
		check(false);
		break;
	case EMovieRenderPipelineState::ProducingFrames:
		TickProducingFrames();
		break;
	case EMovieRenderPipelineState::Finalize:
		// Don't flush the finalize to keep the UI responsive.
		TickFinalizeOutputContainers(false);
		break;
	case EMovieRenderPipelineState::Export:
		// Don't flush the export to keep the UI responsive.
		TickPostFinalizeExport(false);
		break;
	}

	bHasRunBeginFrameOnce = true;
	UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("OnEngineTickBeginFrame (End) Engine Frame: [%d]"), GFrameCounter);
}

void UMoviePipeline::OnSequenceEvaluated(const UMovieSceneSequencePlayer& Player, FFrameTime CurrentTime, FFrameTime PreviousTime)
{
	// This callback exists for logging purposes. DO NOT HINGE LOGIC ON THIS CALLBACK
	// because this may get called multiple times per frame and may be the result of
	// a seek operation which is reverted before a frame is even rendered.
	UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("[GFrameCounter: %d] Sequence Evaluated. CurrentTime: %s PreviousTime: %s"), GFrameCounter, *LexToString(CurrentTime), *LexToString(PreviousTime));
}

void UMoviePipeline::OnEngineTickEndFrame()
{
	// Unfortunately, since we can't control when our Initialization function is called
	// we can end up in a situation where this callback is registered but the matching
	// OnEngineTickBeginFrame() hasn't been called for that given engine tick. Instead of
	// changing this registration to hang off of the end of the first OnEngineTickBeginFrame()
	// we instead just early out here if that hasn't actually been called once. This decision
	// is designed to minimize places where callbacks are registered and where flow changes.
	if (!bHasRunBeginFrameOnce)
	{
		return;
	}

	// Early out if we're idling as we don't want to process a frame. This prevents us from
	// overwriting render state when the engine is processing ticks but we're not trying to
	// change the evaluation. 
	if (IsDebugFrameStepIdling())
	{
		return;
	}

	// It is important that there is no early out that skips hitting this
	// (Otherwise we don't pause on the frame we transition from step -> idle
	// and the world plays even though the state is frozen).
	if (bPauseAtEndOfFrame)
	{
		GetWorld()->GetFirstPlayerController()->SetPause(true);
		bPauseAtEndOfFrame = false;
	}

	UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("OnEngineTickEndFrame (Start) Engine Frame: [%d]"), GFrameCounter);

	ProcessAudioTick();
	RenderFrame();

	UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("OnEngineTickEndFrame (End) Engine Frame: [%d]"), GFrameCounter);
}

void UMoviePipeline::ProcessEndOfCameraCut(FMoviePipelineShotInfo &CurrentShot, FMoviePipelineCameraCutInfo &CurrentCameraCut)
{
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Finished processing Camera Cut [%d/%d] on Shot [%d] (%s) with stats [...]"), GFrameCounter,
		CurrentShot.CurrentCameraCutIndex + 1, CurrentShot.CameraCuts.Num(),
		CurrentShotIndex + 1, *CurrentShot.GetDisplayName());

	CurrentCameraCut.State = EMovieRenderShotState::Finished;

	// Compare our expected vs. actual results for logging.
	//if (CurrentCameraCut.CurrentWorkInfo != CurrentCameraCut.TotalWorkInfo)
	//{
	//	UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Mismatch in work done vs. expected work done.\nExpected: %s\nTotal: %s"),
	//		*CurrentCameraCut.TotalWorkInfo.ToDisplayString(), *CurrentCameraCut.CurrentWorkInfo.ToDisplayString());
	//}

	// We pause at the end too, just so that frames during finalize don't continue to trigger Sequence Eval messages.
	LevelSequenceActor->GetSequencePlayer()->Pause();

	bool bWasLastCameraCut = CurrentShot.SetNextShotActive();
	if (bWasLastCameraCut)
	{
		TeardownShot(CurrentShot);
	}
}

void UMoviePipeline::BeginFinalize()
{
	// Notify all of our output containers that we have finished producing and
	// submitting all frames to them and that they should start any async flushes.
	for (UMoviePipelineOutputBase* Container : GetPipelineMasterConfig()->GetOutputContainers())
	{
		Container->BeginFinalize();
	}
}

void UMoviePipeline::BeginExport()
{
	for (UMoviePipelineSetting* Setting : GetPipelineMasterConfig()->GetAllSettings())
	{
		Setting->BeginExport();
	}
}

void UMoviePipeline::TickFinalizeOutputContainers(const bool bInForceFinish)
{
	// Tick all containers until they all report that they have finalized.
	bool bAllContainsFinishedProcessing;

	do
	{
		bAllContainsFinishedProcessing = true;

		// Ask the containers if they're all done processing.
		for (UMoviePipelineOutputBase* Container : GetPipelineMasterConfig()->GetOutputContainers())
		{
			bAllContainsFinishedProcessing &= Container->HasFinishedProcessing();
		}

		// If we aren't forcing a finish, early out after one loop to keep
		// the editor/ui responsive.
		if (!bInForceFinish || bAllContainsFinishedProcessing)
		{
			break;
		}

		// If they've reached here, they're forcing them to finish so we'll sleep for a touch to give
		// everyone a chance to actually do work before asking them if they're done.
		FPlatformProcess::Sleep(1.f);

	} while (true);

	// If an output container is still working, we'll early out to keep the UI responsive.
	// If they've forced a finish this will have to be true before we can reach this block.
	if (!bAllContainsFinishedProcessing)
	{
		return;
	}

	for (UMoviePipelineOutputBase* Container : GetPipelineMasterConfig()->GetOutputContainers())
	{
		// All containers have finished processing, final shutdown.
		Container->Finalize();
	}

	TransitionToState(EMovieRenderPipelineState::Export);
}

void UMoviePipeline::TickPostFinalizeExport(const bool bInForceFinish)
{
	// This step assumes you have produced data and filled the data structures.
	check(PipelineState == EMovieRenderPipelineState::Export);
	UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("[%d] PostFinalize Export (Start)."), GFrameCounter);

	// ToDo: Loop through any extensions (such as XML export) and let them export using all of the
	// data that was generated during this run such as containers, output names and lengths.
	// Tick all containers until they all report that they have finalized.
	bool bAllContainsFinishedProcessing;

	do
	{
		bAllContainsFinishedProcessing = true;

		// Ask the containers if they're all done processing.
		for (UMoviePipelineSetting* Setting : GetPipelineMasterConfig()->GetAllSettings())
		{
			bAllContainsFinishedProcessing &= Setting->HasFinishedExporting();
		}
		
		// If we aren't forcing a finish, early out after one loop to keep
		// the editor/ui responsive.
		if (!bInForceFinish || bAllContainsFinishedProcessing)
		{
			break;
		}

		// If they've reached here, they're forcing them to finish so we'll sleep for a touch to give
		// everyone a chance to actually do work before asking them if they're done.
		FPlatformProcess::Sleep(1.f);

	} while (true);

	UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("[%d] PostFinalize Export (End)."), GFrameCounter);

	// If an output container is still working, we'll early out to keep the UI responsive.
	// If they've forced a finish this will have to be true before we can reach this block.
	if (!bAllContainsFinishedProcessing)
	{
		return;
	}

	TransitionToState(EMovieRenderPipelineState::Finished);
}

bool UMoviePipelineCustomTimeStep::UpdateTimeStep(UEngine* /*InEngine*/)
{
	if (ensureMsgf(!FMath::IsNearlyZero(TimeCache.DeltaTime), TEXT("An incorrect or uninitialized time step was used! Delta Time of 0 isn't allowed.")))
	{
		FApp::UpdateLastTime();
		FApp::SetDeltaTime(TimeCache.DeltaTime);
		FApp::SetCurrentTime(FApp::GetCurrentTime() + FApp::GetDeltaTime());
	}

	// Clear our cached time to ensure we're always explicitly telling this what to do and never relying on the last set value.
	// (This will cause the ensure above to check on the next tick if someone didn't reset our value.)
	TimeCache = MoviePipeline::FFrameTimeStepCache();

	// Return false so the engine doesn't run its own logic to overwrite FApp timings.
	return false;
}

void UMoviePipeline::ModifySequenceViaExtensions(ULevelSequence* InSequence)
{
}


void UMoviePipeline::InitializeLevelSequenceActor()
{
	// There is a reasonable chance that there exists a Level Sequence Actor in the world already set up to play this sequence.
	ALevelSequenceActor* ExistingActor = nullptr;

	for (auto It = TActorIterator<ALevelSequenceActor>(GetWorld()); It; ++It)
	{
		// Iterate through all of them in the event someone has multiple copies in the world on accident.
		if (It->LevelSequence == TargetSequence)
		{
			// Found it!
			ExistingActor = *It;

			// Stop it from playing if it's already playing.
			if (ExistingActor->GetSequencePlayer())
			{
				ExistingActor->GetSequencePlayer()->Stop();
			}
		}
	}

	LevelSequenceActor = ExistingActor;
	if (!LevelSequenceActor)
	{
		// Spawn a new level sequence
		LevelSequenceActor = GetWorld()->SpawnActor<ALevelSequenceActor>();
		check(LevelSequenceActor);
	}

	// Use our duplicated sequence
	LevelSequenceActor->SetSequence(TargetSequence);

	// Enforce settings.
	LevelSequenceActor->PlaybackSettings.LoopCount.Value = 0;
	LevelSequenceActor->PlaybackSettings.bAutoPlay = false;
	LevelSequenceActor->PlaybackSettings.bPauseAtEnd = true;
	LevelSequenceActor->GetSequencePlayer()->SetTimeController(CustomSequenceTimeController);

	LevelSequenceActor->GetSequencePlayer()->OnSequenceUpdated().AddUObject(this, &UMoviePipeline::OnSequenceEvaluated);
}


FMoviePipelineShotInfo CreateShotFromMovieScene(const UMovieScene* InMovieScene, const TRange<FFrameNumber>& InIntersectionRange, UMovieSceneSubSection* InSubSection)
{
	check(InMovieScene);

	// We will generate a range for each sub-section within the shot we wish to render. These will be clipped by
	// the overall range of the shot to represent the final desired render duration.
	struct FCameraCutRange
	{
		TRange<FFrameNumber> Range;
		UMovieSceneCameraCutSection* Section;
	};

	TArray<FCameraCutRange> IntersectedRanges;

	// We're going to search for Camera Cut tracks within this shot. If none are found, we'll use the whole range of the shot.
	const UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(InMovieScene->GetCameraCutTrack());
	if (CameraCutTrack)
	{
		for (UMovieSceneSection* Section : CameraCutTrack->GetAllSections())
		{
			// ToDo: Inner vs. Outer resolution differences.
			UMovieSceneCameraCutSection* CameraCutSection = CastChecked<UMovieSceneCameraCutSection>(Section);
			
			if (Section->GetRange().IsEmpty())
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Found zero-length section in CameraCutTrack: %s Skipping..."), *CameraCutSection->GetPathName());
				continue;
			}

			TRange<FFrameNumber> SectionRangeInMaster = Section->GetRange();
			
			// If this camera cut track is inside of a shot subsection, we need to take the parent section into account.
			if (InSubSection)
			{
				TRange<FFrameNumber> LocalSectionRange = Section->GetRange(); // Section in local space
				LocalSectionRange = MovieScene::TranslateRange(LocalSectionRange, -InMovieScene->GetPlaybackRange().GetLowerBoundValue()); // Section relative to zero
				SectionRangeInMaster = MovieScene::TranslateRange(LocalSectionRange, InSubSection->GetRange().GetLowerBoundValue()); // Convert to master sequence space.
			}

			if (!SectionRangeInMaster.Overlaps(InIntersectionRange))
			{
				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Skipping camera cut section due to no overlap with playback range. CameraCutTrack: %s"), *CameraCutSection->GetPathName());
				continue;
			}
			
			// Intersect this cut with the outer range in the likely event that the section goes past the bounds.
			TRange<FFrameNumber> IntersectingRange = TRange<FFrameNumber>::Intersection(SectionRangeInMaster, InIntersectionRange);

			FCameraCutRange& NewRange = IntersectedRanges.AddDefaulted_GetRef();
			NewRange.Range = IntersectingRange;
			NewRange.Section = CameraCutSection;
		}
	}

	if(IntersectedRanges.Num() == 0)
	{
		// No camera cut track (or section) was found inside. We'll treat the whole shot as the desired range.
		FCameraCutRange& NewRange = IntersectedRanges.AddDefaulted_GetRef();
		NewRange.Range = InIntersectionRange;
		NewRange.Section = nullptr;
	}

	FMoviePipelineShotInfo NewShot;
	NewShot.OriginalRange = InIntersectionRange;
	NewShot.TotalOutputRange = NewShot.OriginalRange;

	for (const FCameraCutRange& Range : IntersectedRanges)
	{
		// Generate a CameraCut for each range.
		FMoviePipelineCameraCutInfo& CameraCut = NewShot.CameraCuts.AddDefaulted_GetRef();

		CameraCut.CameraCutSection = Range.Section; // May be nullptr.
		CameraCut.OriginalRange = Range.Range;
		CameraCut.TotalOutputRange = CameraCut.OriginalRange;
	}

	return NewShot;
}

void UMoviePipeline::BuildShotListFromSequence()
{
	// Shot Tracks take precedent over camera cuts, as settings can only be applied as granular as a shot.
	UMovieSceneCinematicShotTrack* CinematicShotTrack = TargetSequence->GetMovieScene()->FindMasterTrack<UMovieSceneCinematicShotTrack>();
	
	if (CinematicShotTrack)
	{
		ensureMsgf(CinematicShotTrack->GetAllSections().Num() > 0, TEXT("Cinematic Shot track must have at least one section to be rendered."));

		for (UMovieSceneSection* Section : CinematicShotTrack->GetAllSections())
		{
			UMovieSceneCinematicShotSection* ShotSection = CastChecked<UMovieSceneCinematicShotSection>(Section);

			// If the user has manually marked a section as inactive we don't produce a shot for it.
			if (!Section->IsActive())
			{
				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Skipped adding Shot %s to Shot List due to being inactive."), *ShotSection->GetShotDisplayName());
				continue;
			}

			if (!ShotSection->GetSequence())
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Skipped adding Shot %s to Shot List due to no inner sequence."), *ShotSection->GetShotDisplayName());
				continue;
			}

			// Skip this section if it falls entirely outside of our playback bounds.
			TRange<FFrameNumber> MasterPlaybackBounds = GetCurrentJob()->GetConfiguration()->GetEffectivePlaybackRange(TargetSequence);
			if (!ShotSection->GetRange().Overlaps(MasterPlaybackBounds))
			{
				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Skipped adding Shot %s to Shot List due to not overlapping playback bounds."), *ShotSection->GetShotDisplayName());
				continue;
			}
			
			//if (CurrentJob->ShotRenderMask.Num() > 0)
			//{
			//	if (!CurrentJob->ShotRenderMask.Contains(ShotSection->GetShotDisplayName()))
			//	{
			//		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Skipped adding Shot %s to Shot List due to a shot render mask being active, and this shot not being on the list."), *ShotSection->GetShotDisplayName());
			//		continue;
			//	}
			//}

			// The Shot Section may extend past our Sequence's Playback Bounds. We intersect the two bounds to ensure that
			// the Playback Start/Playback End of the overall sequence is respected.
			TRange<FFrameNumber> CinematicShotSectionRange = TRange<FFrameNumber>::Intersection(ShotSection->GetRange(), TargetSequence->GetMovieScene()->GetPlaybackRange());
			FMoviePipelineShotInfo NewShot = CreateShotFromMovieScene(ShotSection->GetSequence()->GetMovieScene(), CinematicShotSectionRange, ShotSection);

			// Convert the offset time to ticks
			FFrameTime OffsetTime = ShotSection->GetOffsetTime().GetValue();

			NewShot.StartFrameOffsetTick = ShotSection->GetSequence()->GetMovieScene()->GetPlaybackRange().GetLowerBoundValue() + OffsetTime.RoundToFrame();
			// The first thing we do is find the appropriate configuration from the settings. Each shot can have its own config
			// or they fall back to a default one specified for the whole pipeline.
			// NewShot.ShotConfig = GetPipelineMasterConfig()->GetConfigForShot(ShotSection->GetShotDisplayName());
			NewShot.CinematicShotSection = ShotSection;

			// There should always be a shot config as the Pipeline default is returned in the event they didn't customize.
			// check(NewShot.ShotConfig);

			ShotList.Add(MoveTemp(NewShot));
		}
	}
	else
	{
		// They don't have a cinematic shot track. We'll slice them up by camera cuts instead.
		FMoviePipelineShotInfo NewShot = CreateShotFromMovieScene(TargetSequence->GetMovieScene(), TargetSequence->GetMovieScene()->GetPlaybackRange(), nullptr);
		// NewShot.ShotConfig = GetPipelineMasterConfig()->DefaultShotConfig;
		
		ShotList.Add(MoveTemp(NewShot));
	}

	// If they don't have a cinematic shot track, or a camera cut track then they want to control the camera
	// through their own logic. We'll just use the duration of the Sequence as the render, plus warn them.
	if (ShotList.Num() == 0)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("No Cinematic Shot Tracks found, and no Camera Cut Tracks found. Playback Range will be used but camera will render from Pawns perspective."));
		FMoviePipelineShotInfo NewShot;
		// NewShot.ShotConfig = GetPipelineMasterConfig()->DefaultShotConfig;

		FMoviePipelineCameraCutInfo& CameraCut = NewShot.CameraCuts.AddDefaulted_GetRef();
		CameraCut.OriginalRange = TargetSequence->GetMovieScene()->GetPlaybackRange();
	}

	// Now that we've gathered at least one or more shots with one or more cuts, we can apply settings. It's easier to
	// debug when all of the shots are calculated up front and debug info is printed in a block instead of as it is reached.
	int32 ShotIndex = 1;
	for (FMoviePipelineShotInfo& Shot : ShotList)
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("Shot %d/%d has %d Camera Cuts."), ShotIndex, ShotList.Num(), Shot.CameraCuts.Num());
		ShotIndex++;

		UMoviePipelineOutputSetting* OutputSettings = FindOrAddSetting<UMoviePipelineOutputSetting>(Shot);
		UMoviePipelineAntiAliasingSetting* AntiAliasingSettings = FindOrAddSetting<UMoviePipelineAntiAliasingSetting>(Shot);
		UMoviePipelineHighResSetting* HighResSettings = FindOrAddSetting<UMoviePipelineHighResSetting>(Shot);
		Shot.NumHandleFrames = OutputSettings->HandleFrameCount;
		
		// Expand the shot to encompass handle frames. This will modify our Camera Cuts bounds.
		ExpandShot(Shot);

		FString ShotName;
		if (Shot.CinematicShotSection.IsValid())
		{
			ShotName = Shot.CinematicShotSection->GetShotDisplayName();
		}

		for (FMoviePipelineCameraCutInfo& CameraCut : Shot.CameraCuts)
		{
			// Warm Up Frames. If there are any render samples we require at least one engine warm up frame.
			CameraCut.NumEngineWarmUpFramesRemaining = FMath::Max(AntiAliasingSettings->EngineWarmUpCount, AntiAliasingSettings->RenderWarmUpCount > 0 ? 1 : 0);

			CameraCut.bEmulateFirstFrameMotionBlur = true;
			CameraCut.NumTemporalSamples = AntiAliasingSettings->TemporalSampleCount;
			CameraCut.NumSpatialSamples = AntiAliasingSettings->SpatialSampleCount;
			CameraCut.CachedFrameRate = GetPipelineMasterConfig()->GetEffectiveFrameRate(TargetSequence);
			CameraCut.CachedTickResolution = TargetSequence->GetMovieScene()->GetTickResolution();
			CameraCut.NumTiles = FIntPoint(HighResSettings->TileCount, HighResSettings->TileCount);
			CameraCut.CalculateWorkMetrics();

			FString CameraName;
			if (CameraCut.CameraCutSection.IsValid())
			{
				const FMovieSceneObjectBindingID& CameraObjectBindingId = CameraCut.CameraCutSection->GetCameraBindingID();
				if (CameraObjectBindingId.IsValid())
				{
					UMovieScene* OwningMovieScene = CameraCut.CameraCutSection->GetTypedOuter<UMovieScene>();
					if (OwningMovieScene)
					{
						FMovieSceneBinding* Binding = OwningMovieScene->FindBinding(CameraObjectBindingId.GetGuid());
						if (Binding)
						{
							CameraName = Binding->GetName();
						}
					}
				}
			}

			CameraCut.ShotName = ShotName;
			CameraCut.CameraName = CameraName;
			
			// When we expanded the shot above, it pushed the first/last camera cuts ranges to account for Handle Frames.
			// We want to start rendering from the first handle frame. Shutter Timing is a fixed offset from this number.
			CameraCut.CurrentMasterSeqTick = CameraCut.TotalOutputRange.GetLowerBoundValue();
			CameraCut.CurrentLocalSeqTick = Shot.StartFrameOffsetTick;
		}
		
		// Sort the camera cuts within a shot. The correct render order is required for relative frame counts to work.
		Shot.CameraCuts.Sort([](const FMoviePipelineCameraCutInfo& A, const FMoviePipelineCameraCutInfo& B)
		{
			return A.TotalOutputRange.GetLowerBoundValue() < B.TotalOutputRange.GetLowerBoundValue();
		});
	}

	// Sort the shot list overall. The correct order is required for relative frame counts to put the shots on disk in the right order.
	ShotList.Sort([](const FMoviePipelineShotInfo& A, const FMoviePipelineShotInfo& B)
	{
		return A.TotalOutputRange.GetLowerBoundValue() < B.TotalOutputRange.GetLowerBoundValue();
	});
}

void UMoviePipeline::InitializeShot(FMoviePipelineShotInfo& InShot)
{
	// Set the new shot as the active shot. This enables the specified shot section and disables all other shot sections.
	SetSoloShot(InShot);

	if (InShot.ShotOverrideConfig != nullptr)
	{
		// Any shot-specific overrides haven't had first time initialization. So we'll do that now.
		for (UMoviePipelineSetting* Setting : InShot.ShotOverrideConfig->GetUserSettings())
		{
			Setting->OnMoviePipelineInitialized(this);
		}
	}

	// Setup required rendering architecture for all passes in this shot.
	SetupRenderingPipelineForShot(InShot);
}

void UMoviePipeline::TeardownShot(FMoviePipelineShotInfo& InShot)
{
	// Teardown happens at the start of the first frame the shot is finished so we'll stop recording
	// audio, which will prevent it from capturing any samples for this frame. We don't do a similar
	// Start in InitializeShot() because we don't want to record samples during warm up/motion blur.
	StopAudioRecording();

	// Notify our containers that the current shot has ended.
	for (UMoviePipelineOutputBase* Container : GetPipelineMasterConfig()->GetOutputContainers())
	{
		Container->OnShotFinished(InShot);
	}

	if (InShot.ShotOverrideConfig != nullptr)
	{
		// Any shot-specific overrides haven't had first time initialization. So we'll do that now.
		for (UMoviePipelineSetting* Setting : InShot.ShotOverrideConfig->GetUserSettings())
		{
			Setting->OnMoviePipelineShutdown(this);
		}
	}

	// Teardown any rendering architecture for this shot.
	TeardownRenderingPipelineForShot(InShot);

	CurrentShotIndex++;

	// Check to see if this was the last shot in the Pipeline, otherwise on the next
	// tick the new shot will be initialized and processed.
	if (CurrentShotIndex >= ShotList.Num())
	{


		UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Finished rendering last shot. Moving to Finalize to finish writing items to disk."), GFrameCounter);
		TransitionToState(EMovieRenderPipelineState::Finalize);
	}
}

void UMoviePipeline::SetSoloShot(const FMoviePipelineShotInfo& InShot)
{
	// Iterate through the shot list and ensure all shots have been set to inactive.
	for (FMoviePipelineShotInfo& Shot : ShotList)
	{
		UMovieSceneCinematicShotSection* Section = Shot.CinematicShotSection.Get();

		// Skip the one we will be modifying below to avoid extra MarkAsChanged calls.
		if (Section && Section != InShot.CinematicShotSection.Get())
		{
			Section->SetIsActive(false);
			Section->MarkAsChanged();
		}
	}

	// Now that we've set them all to inactive we'll ensure that our passed in shot is active.
	if (UMovieSceneCinematicShotSection* CurrentShotSection = InShot.CinematicShotSection.Get())
	{
		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Disabled all shot tracks and re-enabling %s for solo."), *CurrentShotSection->GetShotDisplayName());
		CurrentShotSection->SetIsActive(true);
		CurrentShotSection->MarkAsChanged();
	}
	else
	{
		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Disabled all shot tracks and skipped enabling a shot track due to no shot section associated with the provided shot."));
	}
}

void UMoviePipeline::ExpandShot(FMoviePipelineShotInfo& InShot)
{
	const MoviePipeline::FFrameConstantMetrics FrameMetrics = CalculateShotFrameMetrics(InShot);

	// Handle Frames will be added onto our original shot size. We track the ranges separately for counting purposes
	// later - the actual rendering code is unaware of the handle frames. Handle Frames only apply to the shot
	// and expand the first/last inner cut to cover this area.
	FFrameNumber HandleFrameTicks = FrameMetrics.TicksPerOutputFrame.FloorToFrame().Value * InShot.NumHandleFrames;
	InShot.TotalOutputRange = MovieScene::DilateRange(InShot.OriginalRange, -HandleFrameTicks, HandleFrameTicks);

	// We'll expand the overall sequence by at least this much to ensure we don't get clamped. This is +1 frame for shutter timing overlap.
	TRange<FFrameNumber> TotalPlaybackRange = MovieScene::DilateRange(InShot.TotalOutputRange, -FrameMetrics.TicksPerOutputFrame.CeilToFrame(), FrameMetrics.TicksPerOutputFrame.CeilToFrame());

	// We can now take the difference between the two ranges to isolate just the opening/closing range (so we can use the range for counting later)
	TArray<TRange<FFrameNumber>> HandleFrameRanges = TRange<FFrameNumber>::Difference(InShot.TotalOutputRange, InShot.OriginalRange);
	if (HandleFrameRanges.Num() == 2)
	{
		InShot.HandleFrameRangeStart = HandleFrameRanges[0];
		InShot.HandleFrameRangeEnd = HandleFrameRanges[1];
	}

	// Expand the first and last cut inside this shot to cover the handle frame distance.
	if (InShot.CameraCuts.Num() > 0)
	{
		// These may be the same or they may be different.
		FMoviePipelineCameraCutInfo& FirstCut = InShot.CameraCuts[0];
		FMoviePipelineCameraCutInfo& LastCut = InShot.CameraCuts[InShot.CameraCuts.Num() - 1];

		FirstCut.TotalOutputRange = MovieScene::DilateRange(FirstCut.TotalOutputRange, -HandleFrameTicks, FFrameNumber(0));
		LastCut.TotalOutputRange = MovieScene::DilateRange(LastCut.TotalOutputRange, FFrameNumber(0), HandleFrameTicks);
	}

	// When we expand a shot section, we also need to expand the inner sequence's range by the same amount.
	UMovieSceneCinematicShotSection* ShotSection = InShot.CinematicShotSection.Get();
	if(ShotSection)
	{
		UMovieScene* ShotMovieScene = ShotSection->GetSequence() ? ShotSection->GetSequence()->GetMovieScene() : nullptr;
		if (!ShotMovieScene)
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("ShotSection (%s) with no inner sequence, skipping expansion!"), *ShotSection->GetShotDisplayName());
			return;
		}

		// Expand the inner shot section range by the handle size, multiplied by the difference between the outer and inner tick resolutions (and factoring in the time scale)
		const bool bRateMatches = TargetSequence->GetMovieScene()->GetTickResolution() == ShotMovieScene->GetTickResolution();
		const double OuterToInnerRateDilation = bRateMatches ? 1.0 : (ShotMovieScene->GetTickResolution() / TargetSequence->GetMovieScene()->GetTickResolution()).AsDecimal();
		const double OuterToInnerScale = OuterToInnerRateDilation * (double)ShotSection->Parameters.TimeScale;

		// We push the playback bounds one frame further (in both directions) to handle the case where shutter timings offset them +/- the actual edge.
		// These bounds don't affect the period of time which is rendered, so it's okay that this pushes beyond the actual evaluated range.

		const FFrameNumber StartOffset = HandleFrameTicks + FrameMetrics.TicksPerOutputFrame.FloorToFrame();
		const FFrameNumber EndOffset = HandleFrameTicks + FrameMetrics.TicksPerOutputFrame.FloorToFrame();

		const FFrameNumber DilatedStartOffset = FFrameNumber(FMath::CeilToInt(StartOffset.Value * OuterToInnerScale));
		const FFrameNumber DilatedEndOffset = FFrameNumber(FMath::CeilToInt(EndOffset.Value * OuterToInnerScale));

		TRange<FFrameNumber> OldPlaybackRange = ShotMovieScene->GetPlaybackRange();
		TRange<FFrameNumber> NewPlaybackRange = MovieScene::DilateRange(ShotMovieScene->GetPlaybackRange(), -DilatedStartOffset, DilatedEndOffset);

		FMovieSceneChanges::FSegmentChange& ModifiedSegment = SequenceChanges.Segments.AddDefaulted_GetRef();
		ModifiedSegment.MovieScene = ShotMovieScene;
		ModifiedSegment.MovieScenePlaybackRange = OldPlaybackRange;
		ModifiedSegment.bMovieSceneReadOnly = ShotMovieScene->IsReadOnly();
		ModifiedSegment.ShotSection = ShotSection;
		ModifiedSegment.bShotSectionIsLocked = ShotSection->IsLocked();
		ModifiedSegment.ShotSectionRange = ShotSection->GetRange();

		FFrameNumber TotalExpansionSizeInFrames = FFrameRate::TransformTime(FFrameTime(HandleFrameTicks) + FrameMetrics.TicksPerOutputFrame, FrameMetrics.TickResolution, FrameMetrics.FrameRate).CeilToFrame();
		// ToDo: ShotList isn't assigned yet so IndexOfByKey fails.
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("ShotSection ([%d] %s) Expanding Shot by [-%d, %d] Ticks (+/-%d Frames). Old Inner Playback Range: %s New Inner Playback Range: %s"),
			ShotList.IndexOfByKey(InShot), *ShotSection->GetShotDisplayName(), 
			StartOffset.Value, EndOffset.Value, TotalExpansionSizeInFrames.Value,
			*LexToString(OldPlaybackRange), *LexToString(NewPlaybackRange));

		// Expand the inner scene
		ShotMovieScene->SetReadOnly(false);
		ShotMovieScene->SetPlaybackRange(NewPlaybackRange, false);

		// Expand the outer owning section
		ShotSection->SetIsLocked(false); // Unlock the section so we can modify the range
		ShotSection->SetRange(MovieScene::DilateRange(ShotSection->GetRange(), -StartOffset, EndOffset));
	}

	// Ensure the overall Movie Scene Playback Range is large enough. This will clamp evaluation if we don't expand it. We hull the existing range
	// with the new range.
	TRange<FFrameNumber> EncompassingPlaybackRange = TRange<FFrameNumber>::Hull(TotalPlaybackRange, TargetSequence->MovieScene->GetPlaybackRange());
	TargetSequence->GetMovieScene()->SetPlaybackRange(EncompassingPlaybackRange);
}

bool UMoviePipeline::IsDebugFrameStepIdling() const
{
	// We're only idling when we're at zero, otherwise there's more frames to process.
	// Caveat is that this will be zero on the last frame we want to render, so we
	// take into account whether or not we've queued up a pause at the end of the frame
	// which is indicator that we want to process the current frame.
	int32 DebugFrameStepValue = CVarMovieRenderPipelineFrameStepper.GetValueOnGameThread();
	return DebugFrameStepValue == 0 && !bPauseAtEndOfFrame;
}

bool UMoviePipeline::DebugFrameStepPreTick()
{
	int32 DebugFrameStepValue = CVarMovieRenderPipelineFrameStepper.GetValueOnGameThread();
	if (DebugFrameStepValue == 0)
	{
		// A value of 0 means that they are using the frame stepper and that we have stepped
		// the specified number of frames. We will create a DeltaTime for the engine
		// and not process anything below, which prevents us from trying to produce an
		// output frame later.
		CustomTimeStep->SetCachedFrameTiming(MoviePipeline::FFrameTimeStepCache(1 / 24.0));
		return true;
	}
	else if (DebugFrameStepValue > 0)
	{
		// They want to process at least one frame, deincrement, then we
		// process the frame. We pause the game here to preserve render state.
		CVarMovieRenderPipelineFrameStepper->Set(DebugFrameStepValue - 1, ECVF_SetByConsole);

		// We want to run this one frame and then pause again at the end.
		bPauseAtEndOfFrame = true;
	}

	return false;
}

void UMoviePipeline::LoadDebugWidget()
{
	FSoftClassPath DebugWidgetClassRef(TEXT("/MovieRenderPipeline/Blueprints/UI_MovieRenderPipelineScreenOverlay.UI_MovieRenderPipelineScreenOverlay_C"));
	if (UClass* DebugWidgetClass = DebugWidgetClassRef.TryLoadClass<UMovieRenderDebugWidget>())
	{
		DebugWidget = CreateWidget<UMovieRenderDebugWidget>(GetWorld(), DebugWidgetClass);
		if (DebugWidget)
		{
			DebugWidget->OnInitializedForPipeline(this);
			DebugWidget->AddToViewport();
		}
		else
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Failed to create Debug Screen UMG Widget. No debug overlay available."));
		}
	}
	else
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Failed to find Debug Screen UMG Widget class. No debug overlay available."));
	}
}

FFrameTime FMoviePipelineTimeController::OnRequestCurrentTime(const FQualifiedFrameTime& InCurrentTime, float InPlayRate)
{
	FFrameTime RequestTime = FFrameRate::TransformTime(TimeCache.Time, TimeCache.Rate, InCurrentTime.Rate);
	UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("[%d] OnRequestCurrentTime: %d %f"), GFrameCounter, RequestTime.FloorToFrame().Value, RequestTime.GetSubFrame());

	return RequestTime;
}

MoviePipeline::FFrameConstantMetrics UMoviePipeline::CalculateShotFrameMetrics(const FMoviePipelineShotInfo& InShot) const
{
	MoviePipeline::FFrameConstantMetrics Output;
	Output.TickResolution = TargetSequence->GetMovieScene()->GetTickResolution();
	Output.FrameRate = GetPipelineMasterConfig()->GetEffectiveFrameRate(TargetSequence);
	Output.TicksPerOutputFrame = FFrameRate::TransformTime(FFrameTime(FFrameNumber(1)), Output.FrameRate, Output.TickResolution);

	UMoviePipelineCameraSetting* CameraSettings = FindOrAddSetting<UMoviePipelineCameraSetting>(InShot);
	UMoviePipelineAntiAliasingSetting* AntiAliasingSettings = FindOrAddSetting<UMoviePipelineAntiAliasingSetting>(InShot);

	// (CameraShutterAngle/360) gives us the fraction-of-the-output-frame the accumulation frames should cover.
	Output.ShutterAnglePercentage = FMath::Max(CameraSettings->CameraShutterAngle / 360.0, 1 / 360.0);

	{
		/*
		* Calculate out how many ticks a normal sub-frame occupies.
		* (TickRes/FrameRate) gives you ticks-per-second, and then divide that by the percentage of time the
		* shutter is open. Finally, divide the percentage of time the shutter is open by the number of frames
		* we're accumulating.
		*
		* It is common that there is potential to have some leftover here. ie:
		* 24000 Ticks / 24fps = 1000 ticks per second. At 180 degree shutter angle that gives you 500 ticks
		* spread out amongst n=3 sub-frames. 500/3 = 166.66 ticks. We'll floor that when we use it, and ensure
		* we accumulate the sub-tick and choose when to apply it.
		*/

		// Now we take the amount of time the shutter is open.
		Output.TicksWhileShutterOpen = Output.TicksPerOutputFrame * Output.ShutterAnglePercentage;

		// Divide that amongst all of our accumulation sample frames.
		Output.TicksPerSample = Output.TicksWhileShutterOpen / AntiAliasingSettings->TemporalSampleCount;
	}

	Output.ShutterClosedFraction = (360 - CameraSettings->CameraShutterAngle) / 360.0;
	Output.TicksWhileShutterClosed = Output.TicksPerOutputFrame * Output.ShutterClosedFraction;

	// Shutter Offset
	switch (CameraSettings->ShutterTiming)
	{
		// Subtract the entire time the shutter is open.
	case EMoviePipelineShutterTiming::FrameClose:
		Output.ShutterOffsetTicks = -Output.TicksWhileShutterOpen;
		break;
		// Only subtract half the time the shutter is open.
	case EMoviePipelineShutterTiming::FrameCenter:
		Output.ShutterOffsetTicks = -Output.TicksWhileShutterOpen / 2.0;
		break;
		// No offset needed
	case EMoviePipelineShutterTiming::FrameOpen:
		break;
	}

	// Then, calculate our motion blur offset. Motion Blur in the engine is always
	// centered around the object so we offset our time sampling by half of the
	// motion blur distance so that the distance blurred represents that time.
	Output.MotionBlurCenteringOffsetTicks = Output.TicksPerSample / 2.0;

	return Output;
}


UMoviePipelineMasterConfig* UMoviePipeline::GetPipelineMasterConfig() const
{ 
	return CurrentJob->GetConfiguration(); 
}

UMoviePipelineSetting* UMoviePipeline::FindOrAddSetting(TSubclassOf<UMoviePipelineSetting> InSetting, const FMoviePipelineShotInfo& InShot) const
{
	// Check to see if this setting is in the shot override, if it is we'll use the shot version of that.
	if (InShot.ShotOverrideConfig)
	{
		UMoviePipelineSetting* Setting = InShot.ShotOverrideConfig->FindSettingByClass(InSetting);
		if (Setting)
		{
			// If they specified the setting at all, respect the enabled setting. If it's not enabled, we return the
			// default instead which is the same as if they hadn't added the setting at all.
			return Setting->IsEnabled() ? Setting : InSetting->GetDefaultObject<UMoviePipelineSetting>();
		}
	}

	// If they didn't have a shot override, or the setting wasn't enabled, we'll check the master config.
	UMoviePipelineSetting* Setting = GetPipelineMasterConfig()->FindSettingByClass(InSetting);
	if (Setting)
	{
		return Setting->IsEnabled() ? Setting : InSetting->GetDefaultObject<UMoviePipelineSetting>();
	}

	// If no one overrode it, then we return the default.
	return InSetting->GetDefaultObject<UMoviePipelineSetting>();
}

static bool CanWriteToFile(const TCHAR* InFilename, bool bOverwriteExisting)
{
	// Check if there is space on the output disk.
	bool bIsFreeSpace = true;

	uint64 TotalNumberOfBytes, NumberOfFreeBytes;
	if (FPlatformMisc::GetDiskTotalAndFreeSpace(InFilename, TotalNumberOfBytes, NumberOfFreeBytes))
	{
		bIsFreeSpace = NumberOfFreeBytes > 64 * 1024 * 1024; // 64mb minimum
	}
	// ToDO: Infinite loop possible.
	return bIsFreeSpace && (bOverwriteExisting || IFileManager::Get().FileSize(InFilename) == -1);
}

FString UMoviePipeline::ResolveFilenameFormatArguments(const FString& InFormatString, const FMoviePipelineFrameOutputState& InOutputState, const FStringFormatNamedArguments& InFormatOverrides) const
{
	UMoviePipelineOutputSetting* OutputSettings = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	// Gather all the variables
	FMoviePipelineFormatArgs FilenameFormatArgs;
	FilenameFormatArgs.InJob = CurrentJob;

	// From Settings
	GetPipelineMasterConfig()->GetFilenameFormatArguments(FilenameFormatArgs);

	// From Output State
	InOutputState.GetFilenameFormatArguments(FilenameFormatArgs, OutputSettings->ZeroPadFrameNumbers, OutputSettings->FrameNumberOffset);

	// And from ourself
	{
		FilenameFormatArgs.Arguments.Add(TEXT("date"), InitializationTime.ToString(TEXT("%Y.%m.%d")));
		FilenameFormatArgs.Arguments.Add(TEXT("time"), InitializationTime.ToString(TEXT("%H.%M.%S")));

		// By default, we don't want to show frame duplication numbers. If we need to start writing them,
		// they need to come before the frame number (so that tools recognize them as a sequence).
		FilenameFormatArgs.Arguments.Add(TEXT("file_dup"), FString());
	}

	// Overwrite the variables with overrides if needed. This allows different requesters to share the same variables (ie: filename extension, render pass name)
	for (const TPair<FString, FStringFormatArg>& KVP : InFormatOverrides)
	{
		FilenameFormatArgs.Arguments.Add(KVP);
	}

	// No extension should be provided at this point, because we need to tack the extension onto the end after appending numbers (in the event of no overwrites)
	FString BaseFilename = FString::Format(*InFormatString, FilenameFormatArgs.Arguments);
	FPaths::NormalizeFilename(BaseFilename);


	FString Extension = FString::Format(TEXT(".{ext}"), FilenameFormatArgs.Arguments);


	FString ThisTry = BaseFilename + Extension;

	if (CanWriteToFile(*ThisTry, OutputSettings->bOverrideExistingOutput))
	{
		return ThisTry;
	}

	int32 DuplicateIndex = 2;
	while(true)
	{
		FilenameFormatArgs.Arguments.Add(TEXT("file_dup"), FString::Printf(TEXT("_(%d)"), DuplicateIndex));

		// Re-resolve the format string now that we've reassigned frame_dup to a number.
		ThisTry = FString::Format(*InFormatString, FilenameFormatArgs.Arguments) + Extension;

		// If the file doesn't exist, we can use that, else, increment the index and try again
		if (CanWriteToFile(*ThisTry, OutputSettings->bOverrideExistingOutput))
		{
			return ThisTry;
		}

		++DuplicateIndex;
	}

	return ThisTry;
}

void UMoviePipeline::SetProgressWidgetVisible(bool bVisible)
{
	if (DebugWidget)
	{
		DebugWidget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

#undef LOCTEXT_NAMESPACE // "MoviePipeline"
