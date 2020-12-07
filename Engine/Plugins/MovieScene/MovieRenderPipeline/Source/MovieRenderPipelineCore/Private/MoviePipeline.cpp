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
#include "Engine/LevelStreamingDynamic.h"
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
#include "Misc/CoreDelegates.h"
#if WITH_EDITOR
#include "MovieSceneExportMetadata.h"
#endif
#include "Interfaces/Interface_PostProcessVolume.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

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


void UMoviePipeline::ValidateSequenceAndSettings() const
{
	// ToDo: 
	// Warn for Blueprint Streaming Levels

	// Check to see if they're trying to output alpha and don't have the required project setting set.
	{
		IConsoleVariable* TonemapAlphaCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessing.PropagateAlpha"));
		check(TonemapAlphaCVar);

		TArray<UMoviePipelineRenderPass*> OutputSettings = GetPipelineMasterConfig()->FindSettings<UMoviePipelineRenderPass>();
		bool bAnyOutputWantsAlpha = false;

		for (const UMoviePipelineRenderPass* Output : OutputSettings)
		{
			bAnyOutputWantsAlpha |= Output->IsAlphaInTonemapperRequired();
		}

		if (bAnyOutputWantsAlpha && TonemapAlphaCVar->GetInt() == 0)
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("An output requested Alpha Support but the required project setting is not enabled! Go to Project Settings > Rendering > PostProcessing > 'Enable Alpha Channel Support in Post Processing' and set it to 'Linear Color Space Only'."));
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
		Shutdown(true);
		return;
	}

	if (!ensureAlwaysMsgf(InJob->GetConfiguration(), TEXT("MoviePipeline cannot be initialized with null configuration. Aborting.")))
	{
		Shutdown(true);
		return;
	}

	{
		// If they have a preset origin set, we  will attempt to load from it and copy it into our configuration.
		// A preset origin is only set if they have not modified the preset using the UI, if they have it will have
		// been copied into the local configuration when it was modified and the preset origin cleared. This resolves 
		// an issue where if a preset asset is updated after this job is made, the job uses the wrong settings because
		//  the UI is the one who updates the configuration from the preset.
		if (InJob->GetPresetOrigin())
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Job has a master preset specified, updating local master configuration from preset."));
			InJob->GetConfiguration()->CopyFrom(InJob->GetPresetOrigin());
		}

		// Now we need to update each shot as well.
		for (UMoviePipelineExecutorShot* Shot : InJob->ShotInfo)
		{
			if (Shot->GetShotOverridePresetOrigin())
			{
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Shot has a preset specified, updating local override configuraton from preset."));
				Shot->GetShotOverrideConfiguration()->CopyFrom(Shot->GetShotOverridePresetOrigin());
			}
		}
	}
	
	if (!ensureAlwaysMsgf(PipelineState == EMovieRenderPipelineState::Uninitialized, TEXT("Pipeline cannot be reused. Create a new pipeline to execute a job.")))
	{
		Shutdown(true);
		return;
	}

	// Ensure this object has the World as part of its Outer (so that it has context to spawn things)
	if (!ensureAlwaysMsgf(GetWorld(), TEXT("Pipeline does not contain the world as an outer.")))
	{
		Shutdown(true);
		return;
	}

	CurrentJob = InJob;
	
	ULevelSequence* OriginalSequence = Cast<ULevelSequence>(InJob->Sequence.TryLoad());
	if (!ensureAlwaysMsgf(OriginalSequence, TEXT("Failed to load Sequence Asset from specified path, aborting movie render! Attempted to load Path: %s"), *InJob->Sequence.ToString()))
	{
		Shutdown(true);
		return;
	}

	TargetSequence = Cast<ULevelSequence>(GetCurrentJob()->Sequence.TryLoad());

	// Disable some user settings that conflict with our need to mutate the data.
	{
#if WITH_EDITORONLY_DATA
		// Movie Scene Read Only
		SequenceChanges.bSequenceReadOnly = TargetSequence->GetMovieScene()->IsReadOnly();
		TargetSequence->GetMovieScene()->SetReadOnly(false);
		
		// Playback Range locked
		SequenceChanges.bSequencePlaybackRangeLocked = TargetSequence->GetMovieScene()->IsPlaybackRangeLocked();
		TargetSequence->GetMovieScene()->SetPlaybackRangeLocked(false);
#endif
		// Force Frame-locked evaluation off on the sequence. We control time and will respect that, but need it off for subsampling.
		SequenceChanges.EvaluationType = TargetSequence->GetMovieScene()->GetEvaluationType();
		TargetSequence->GetMovieScene()->SetEvaluationType(EMovieSceneEvaluationType::WithSubFrames);
	}

	if(UPackage* Package = TargetSequence->GetMovieScene()->GetTypedOuter<UPackage>())
	{
		SequenceChanges.bSequencePackageDirty = Package->IsDirty();
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

	// Now that we've fixed up the sequence, we're going to build a list of shots that we need
	// to produce in a simplified data structure. The simplified structure makes the flow/debugging easier.
	BuildShotListFromSequence();

	// Now that we've built up the shot list, we're going to run a validation pass on it. This will produce warnings
	// for anything we can't fix that might be an issue - extending sections, etc. This should be const as this
	// validation should re-use what was used in the UI.
	ValidateSequenceAndSettings();

#if WITH_EDITOR
	// Next, initialize the output metadata with the shot list data we just built
	OutputMetadata.Shots.Empty(ActiveShotList.Num());
	for (UMoviePipelineExecutorShot* Shot : ActiveShotList)
	{
		UMoviePipelineOutputSetting* OutputSettings = FindOrAddSettingForShot<UMoviePipelineOutputSetting>(Shot);

		FMovieSceneExportMetadataShot& ShotMetadata = OutputMetadata.Shots.AddDefaulted_GetRef();
		ShotMetadata.MovieSceneShotSection = Cast<UMovieSceneCinematicShotSection>(Shot->OuterPathKey.TryLoad());
		ShotMetadata.HandleFrames = OutputSettings->HandleFrameCount;
	}
#endif

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

	for (ULevelStreaming* Level : GetWorld()->GetStreamingLevels())
	{
		UClass* StreamingClass = Level->GetClass();

		if (StreamingClass == ULevelStreamingDynamic::StaticClass())
		{
			const FString NonPrefixedLevelName = UWorld::StripPIEPrefixFromPackageName(Level->GetWorldAssetPackageName(), GetWorld()->StreamingLevelsPrefix);
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Sub-level '%s' is set to blueprint streaming and will not be visible during a render unless a Sequencer Visibility Track controls its visibility or you have written other code to handle loading it."),
				*NonPrefixedLevelName);
		}
	}

	SetupAudioRendering();
	CurrentShotIndex = 0;
	CachedOutputState.ShotCount = ActiveShotList.Num();

	InitializationVersion = UMoviePipelineBlueprintLibrary::ResolveVersionNumber(this);

	// Initialization is complete. This engine frame is a wash (because the tick started with a 
	// delta time not generated by us) so we'll wait until the next engine frame to start rendering.
	InitializationTime = FDateTime::UtcNow();

	// If the shot mask entirely disabled everything we'll transition directly to finish as there is no work to do.
	if (ActiveShotList.Num() == 0)
	{
		// We have to transition twice as Uninitialized -> n state is a no-op, so the second tick will take us to Finished which shuts down.
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[%d] No shots detected to render. Either all outside playback range, or disabled via shot mask, bailing."), GFrameCounter);

		TransitionToState(EMovieRenderPipelineState::Export);
		TransitionToState(EMovieRenderPipelineState::Finished);
	}
	else
	{
		TransitionToState(EMovieRenderPipelineState::ProducingFrames);
	}
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
#if WITH_EDITORONLY_DATA
	TargetSequence->GetMovieScene()->SetReadOnly(SequenceChanges.bSequenceReadOnly);
	TargetSequence->GetMovieScene()->SetPlaybackRangeLocked(SequenceChanges.bSequencePlaybackRangeLocked);
#endif
	if(UPackage* Package = TargetSequence->GetMovieScene()->GetTypedOuter<UPackage>())
	{
		Package->SetDirtyFlag(SequenceChanges.bSequencePackageDirty);
	}	


	for (FMovieSceneChanges::FSegmentChange& ModifiedSegment : SequenceChanges.Segments)
	{
		if (ModifiedSegment.MovieScene.IsValid())
		{
			ModifiedSegment.MovieScene->SetPlaybackRange(ModifiedSegment.MovieScenePlaybackRange);
#if WITH_EDITORONLY_DATA
			ModifiedSegment.MovieScene->SetReadOnly(ModifiedSegment.bMovieSceneReadOnly);
#endif
			if(UPackage* Package = ModifiedSegment.MovieScene->GetTypedOuter<UPackage>())
			{
				Package->SetDirtyFlag(ModifiedSegment.bMovieScenePackageDirty);
			}	
		}

		if (ModifiedSegment.ShotSection.IsValid())
		{
			ModifiedSegment.ShotSection->SetRange(ModifiedSegment.ShotSectionRange);
			ModifiedSegment.ShotSection->SetIsLocked(ModifiedSegment.bShotSectionIsLocked);
			
			ModifiedSegment.ShotSection->SetIsActive(ModifiedSegment.bShotSectionIsActive);
			ModifiedSegment.ShotSection->MarkAsChanged();
		}

		if (ModifiedSegment.CameraSection.IsValid())
		{
			ModifiedSegment.CameraSection->SetIsActive(ModifiedSegment.bCameraSectionIsActive);
			ModifiedSegment.CameraSection->MarkAsChanged();
		}
	}
}


void UMoviePipeline::RequestShutdown(bool bIsError)
{
	// It's possible for a previous call to RequestionShutdown to have set an error before this call that may not
	// We don't want to unset a previously set error state
	if (bIsError)
	{
		bFatalError = true;
	}

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

void UMoviePipeline::Shutdown(bool bIsError)
{
	check(IsInGameThread());

	// It's possible for a previous call to RequestionShutdown to have set an error before this call that may not
	// We don't want to unset a previously set error state
	if (bIsError)
	{
		bFatalError = true;
	}

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
			if (CurrentShotIndex < ActiveShotList.Num())
			{
				// Ensures all in-flight work for that shot is handled.
				TeardownShot(ActiveShotList[CurrentShotIndex]);
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
			LevelSequenceActor->GetSequencePlayer()->Stop();
			RestoreTargetSequenceToOriginalState();

			if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
			{
				Viewport->bDisableWorldRendering = false;
			}

			GAreScreenMessagesEnabled = bPrevGScreenMessagesEnabled;

			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Movie Pipeline completed. Duration: %s"), *(FDateTime::UtcNow() - InitializationTime).ToString());

			OnMoviePipelineFinishedImpl();
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

void UMoviePipeline::ProcessEndOfCameraCut(UMoviePipelineExecutorShot* InCameraCut)
{
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Finished processing Camera Cut [%d/%d]."), GFrameCounter, CurrentShotIndex + 1, ActiveShotList.Num());
	InCameraCut->ShotInfo.State = EMovieRenderShotState::Finished;

	// We pause at the end too, just so that frames during finalize don't continue to trigger Sequence Eval messages.
	LevelSequenceActor->GetSequencePlayer()->Pause();

	TeardownShot(InCameraCut);
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

void UMoviePipelineCustomTimeStep::SetCachedFrameTiming(const MoviePipeline::FFrameTimeStepCache& InTimeCache)
{ 
	if (ensureMsgf(!FMath::IsNearlyZero(InTimeCache.DeltaTime), TEXT("An incorrect or uninitialized time step was used! Delta Time of 0 isn't allowed.")))
	{
		TimeCache = InTimeCache;
	}
	else
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("SetCachedFrameTiming called with zero delta time, falling back to 1/24"));
		TimeCache = MoviePipeline::FFrameTimeStepCache(1 / 24.0);
	}
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
	
	// Enforce settings.
	LevelSequenceActor->PlaybackSettings.LoopCount.Value = 0;
	LevelSequenceActor->PlaybackSettings.bAutoPlay = false;
	LevelSequenceActor->PlaybackSettings.bPauseAtEnd = true;
	LevelSequenceActor->PlaybackSettings.bRestoreState = true;

	// Use our duplicated sequence
	LevelSequenceActor->SetSequence(TargetSequence);

	LevelSequenceActor->GetSequencePlayer()->SetTimeController(CustomSequenceTimeController);
	LevelSequenceActor->GetSequencePlayer()->Stop();

	LevelSequenceActor->GetSequencePlayer()->OnSequenceUpdated().AddUObject(this, &UMoviePipeline::OnSequenceEvaluated);
}

void UMoviePipeline::BuildShotListFromSequence()
{
	// Synchronize our shot list with our target sequence. New shots will be added and outdated shots removed.
	// Shots that are already in the list will be updated but their enable flag will be respected.
	UMoviePipelineBlueprintLibrary::UpdateJobShotListFromSequence(TargetSequence, GetCurrentJob());
	int32 ShotIndex = 0;

	for (UMoviePipelineExecutorShot* Shot : GetCurrentJob()->ShotInfo)
	{
		// Cache the original values before we modify them so that we can restore them at the end.
		UMovieScene* InnerMovieScene = nullptr;
		UMovieSceneCinematicShotSection* ShotSection = Cast<UMovieSceneCinematicShotSection>(Shot->OuterPathKey.TryLoad());

		if (ShotSection && ShotSection->GetSequence())
		{
			InnerMovieScene = ShotSection->GetSequence()->GetMovieScene();
		}

		// Cache information about all segments, as we disable all segments when rendering, not just active ones.
		FMovieSceneChanges::FSegmentChange& ModifiedSegment = SequenceChanges.Segments.AddDefaulted_GetRef();
		if (InnerMovieScene)
		{
			// Look to see if we have already stored data about this inner movie scene. If we have, we simply use that data.
			// If we were to build the data from scratch each time, then the first time a inner movie scene is used it will be
			// cached correctly, but subsequent uses would cache incorrectly as the 1st instance would modify playback bounds.
			FMovieSceneChanges::FSegmentChange* ExistingSegment = nullptr;
			for (int32 Index = 0; Index < SequenceChanges.Segments.Num(); Index++)
			{
				if (InnerMovieScene == SequenceChanges.Segments[Index].MovieScene)
				{
					ExistingSegment = &SequenceChanges.Segments[Index];
				}
			}

			if (ExistingSegment)
			{
				ModifiedSegment.MovieScenePlaybackRange = ExistingSegment->MovieScenePlaybackRange;
				ModifiedSegment.bMovieSceneReadOnly = ExistingSegment->bMovieSceneReadOnly;
				ModifiedSegment.bMovieScenePackageDirty = ExistingSegment->bMovieScenePackageDirty;
			}
			else
			{
				ModifiedSegment.MovieScenePlaybackRange = InnerMovieScene->GetPlaybackRange();
#if WITH_EDITORONLY_DATA
				ModifiedSegment.bMovieSceneReadOnly = InnerMovieScene->IsReadOnly();
#endif
				if (UPackage* OwningPackage = InnerMovieScene->GetTypedOuter<UPackage>())
				{
					ModifiedSegment.bMovieScenePackageDirty = OwningPackage->IsDirty();
				}

#if WITH_EDITORONLY_DATA
				// Unlock the playback range and readonly flags so we can modify the scene.
				InnerMovieScene->SetReadOnly(false);
#endif
			}
		}

		// Don't set this until after we've searched the existing Segments for a matching movie scene, otherwise
		// we match immediately and then we copy default values from our first segment.
		ModifiedSegment.MovieScene = InnerMovieScene;

		ModifiedSegment.CameraSection = Cast<UMovieSceneCameraCutSection>(Shot->InnerPathKey.TryLoad());
		if (ModifiedSegment.CameraSection.IsValid())
		{
			ModifiedSegment.bCameraSectionIsActive = ModifiedSegment.CameraSection->IsActive();
		}

		if (ShotSection)
		{
			// Since multiple segments could map to the same cinematic shot section, and the cinematic 
			// shot could have been modified already, find the first segment corresponding to this shot 
			// and use its cached properties.
			FMovieSceneChanges::FSegmentChange* ExistingShotSegment = nullptr;
			for (int32 Index = 0; Index < SequenceChanges.Segments.Num(); Index++)
			{
				if (ShotSection == SequenceChanges.Segments[Index].ShotSection)
				{
					ExistingShotSegment = &SequenceChanges.Segments[Index];
					break;
				}
			}
			if (ExistingShotSegment)
			{
				ModifiedSegment.bShotSectionIsLocked = ExistingShotSegment->bShotSectionIsLocked;
				ModifiedSegment.ShotSectionRange = ExistingShotSegment->ShotSectionRange;
				ModifiedSegment.bShotSectionIsActive = ExistingShotSegment->bShotSectionIsActive;
			}
			else
			{
				ModifiedSegment.bShotSectionIsLocked = ShotSection->IsLocked();
				ModifiedSegment.ShotSectionRange = ShotSection->GetRange();
				ModifiedSegment.bShotSectionIsActive = ShotSection->IsActive();
			}
			
			ShotSection->SetIsLocked(false);
		}
		ModifiedSegment.ShotSection = ShotSection;

		// For non-active shots, this is where we stop
		if (!Shot->bEnabled)
		{
			continue;
		}

		ActiveShotList.Add(Shot);

		UE_LOG(LogMovieRenderPipeline, Log, TEXT("Expanding Shot %d/%d (Shot: %s Camera: %s)"), ShotIndex  + 1, ActiveShotList.Num(), *Shot->OuterName, *Shot->InnerName);
		ShotIndex++;

		UMoviePipelineOutputSetting* OutputSettings = FindOrAddSettingForShot<UMoviePipelineOutputSetting>(Shot);
		UMoviePipelineAntiAliasingSetting* AntiAliasingSettings = FindOrAddSettingForShot<UMoviePipelineAntiAliasingSetting>(Shot);
		UMoviePipelineHighResSetting* HighResSettings = FindOrAddSettingForShot<UMoviePipelineHighResSetting>(Shot);

		// Expand the shot to encompass handle frames. This will modify our Camera Cuts bounds.
		ExpandShot(Shot, ModifiedSegment, OutputSettings->HandleFrameCount);

		bool bUseCameraCutForWarmUp = AntiAliasingSettings->bUseCameraCutForWarmUp;
		if (Shot->ShotInfo.NumEngineWarmUpFramesRemaining == 0)
		{
			// If they don't have enough data for warmup (no camera cut extended track) fall back to emulated warmup.
			bUseCameraCutForWarmUp = false;
		}
		// Warm Up Frames. If there are any render samples we require at least one engine warm up frame.
		int32 NumWarmupFrames = bUseCameraCutForWarmUp ? Shot->ShotInfo.NumEngineWarmUpFramesRemaining : AntiAliasingSettings->EngineWarmUpCount;
		Shot->ShotInfo.NumEngineWarmUpFramesRemaining = FMath::Max(NumWarmupFrames, AntiAliasingSettings->RenderWarmUpCount > 0 ? 1 : 0);

		// When using real warmup we don't emulate a first frame motion blur as we actually have real data.
		Shot->ShotInfo.bEmulateFirstFrameMotionBlur = !bUseCameraCutForWarmUp;
		Shot->ShotInfo.NumTemporalSamples = AntiAliasingSettings->TemporalSampleCount;
		Shot->ShotInfo.NumSpatialSamples = AntiAliasingSettings->SpatialSampleCount;
		Shot->ShotInfo.CachedFrameRate = GetPipelineMasterConfig()->GetEffectiveFrameRate(TargetSequence);
		Shot->ShotInfo.CachedTickResolution = TargetSequence->GetMovieScene()->GetTickResolution();
		Shot->ShotInfo.NumTiles = FIntPoint(HighResSettings->TileCount, HighResSettings->TileCount);
		Shot->ShotInfo.CalculateWorkMetrics();

		// When we expanded the shot above, it pushed the first/last camera cuts ranges to account for Handle Frames.
		// We want to start rendering from the first handle frame. Shutter Timing is a fixed offset from this number.
		Shot->ShotInfo.CurrentLocalSeqTick = Shot->ShotInfo.TotalOutputRangeLocal.GetLowerBoundValue();
	}

}

void UMoviePipeline::InitializeShot(UMoviePipelineExecutorShot* InShot)
{
	// Set the new shot as the active shot. This enables the specified shot section and disables all other shot sections.
	SetSoloShot(InShot);

	if (InShot->GetShotOverrideConfiguration() != nullptr)
	{
		// Any shot-specific overrides haven't had first time initialization. So we'll do that now.
		for (UMoviePipelineSetting* Setting : InShot->GetShotOverrideConfiguration()->GetUserSettings())
		{
			Setting->OnMoviePipelineInitialized(this);
		}
	}

	// Setup required rendering architecture for all passes in this shot.
	SetupRenderingPipelineForShot(InShot);
}

void UMoviePipeline::TeardownShot(UMoviePipelineExecutorShot* InShot)
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

	if (InShot->GetShotOverrideConfiguration() != nullptr)
	{
		// Any shot-specific overrides haven't had first time initialization. So we'll do that now.
		for (UMoviePipelineSetting* Setting : InShot->GetShotOverrideConfiguration()->GetUserSettings())
		{
			Setting->OnMoviePipelineShutdown(this);
		}
	}

	// Teardown any rendering architecture for this shot.
	TeardownRenderingPipelineForShot(InShot);

	CurrentShotIndex++;

	// Check to see if this was the last shot in the Pipeline, otherwise on the next
	// tick the new shot will be initialized and processed.
	if (CurrentShotIndex >= ActiveShotList.Num())
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Finished rendering last shot. Moving to Finalize to finish writing items to disk."), GFrameCounter);
		TransitionToState(EMovieRenderPipelineState::Finalize);
	}
}

void UMoviePipeline::SetSoloShot(const UMoviePipelineExecutorShot* InShot)
{
	// We want to iterate through the entire shot list, not the active shot list to disable camera cuts and segments.
	// Otherwise shots that may have originally fallen outside of our playback range (or were disabled by shot mask)
	// would get skipped and could still be enabled and thus evaluated.
	for (UMoviePipelineExecutorShot* Shot : GetCurrentJob()->ShotInfo)
	{
		UMovieSceneCinematicShotSection* CinematicShotSection = Cast<UMovieSceneCinematicShotSection>(Shot->OuterPathKey.TryLoad());
		if (CinematicShotSection)
		{
			CinematicShotSection->SetIsActive(false);
			CinematicShotSection->MarkAsChanged();
		}

		UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(Shot->InnerPathKey.TryLoad());
		if (CameraCutSection)
		{
			CameraCutSection->SetIsActive(false);
			CameraCutSection->MarkAsChanged();
		}
	}

	// Now that we've set them all to inactive we'll ensure that our passed in shot is active.
	if (UMovieSceneCinematicShotSection* CurrentShotSection = Cast<UMovieSceneCinematicShotSection>(InShot->OuterPathKey.TryLoad()))
	{
		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Disabled all shot tracks and re-enabling %s for solo."), *CurrentShotSection->GetShotDisplayName());
		CurrentShotSection->SetIsActive(true);
		CurrentShotSection->MarkAsChanged();
	}
	else
	{
		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Disabled all shot tracks and skipped enabling a shot track due to no shot section associated with the provided shot."));
	}
	if (UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(InShot->InnerPathKey.TryLoad()))
	{
		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Disabled all camera cut tracks and re-enabling %s for solo."), *CameraCutSection->GetName());
		CameraCutSection->SetIsActive(true);
		CameraCutSection->MarkAsChanged();
	}
	else
	{
		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Disabled all camera cut tracks and skipped enabling a camera cut track due to no camera cut section associated with the provided shot."));
	}
}

void UMoviePipeline::ExpandShot(UMoviePipelineExecutorShot* InShot, const FMovieSceneChanges::FSegmentChange& InSegmentData, const int32 InNumHandleFrames)
{
	const MoviePipeline::FFrameConstantMetrics FrameMetrics = CalculateShotFrameMetrics(InShot);

	TRange<FFrameNumber> TotalPlaybackRangeMaster = TargetSequence->GetMovieScene()->GetPlaybackRange();

	// Handle Frames will be added onto our original shot size- the actual rendering code is unaware of the handle frames. 
	// Handle Frames only apply to the shot and expand the first/last inner cut to cover this area.
	FFrameNumber HandleFrameTicks = FrameMetrics.TicksPerOutputFrame.FloorToFrame().Value * InNumHandleFrames;

	// Expand their total output range so the renderer accounts for them.
	InShot->ShotInfo.TotalOutputRangeLocal = UE::MovieScene::DilateRange(InShot->ShotInfo.TotalOutputRangeLocal, -HandleFrameTicks, HandleFrameTicks);

	// Warm up frames only apply to the first camera cut in a shot, so we'll take the number of ticks for real warm up and
	// convert that into frames. if they don't want real warm up it will be overriden later. 
	if (!InShot->ShotInfo.WarmUpRangeLocal.IsEmpty())
	{
		FFrameNumber TicksForWarmUp = InShot->ShotInfo.WarmUpRangeLocal.Size<FFrameNumber>();
		InShot->ShotInfo.NumEngineWarmUpFramesRemaining = FFrameRate::TransformTime(FFrameTime(TicksForWarmUp), FrameMetrics.TickResolution, FrameMetrics.FrameRate).CeilToFrame().Value;

		// Handle frames weren't accounted for when we calculated the warm up range, so just reduce the amount of warmup by that.
		// When we actually evaluate we will start our math from the first handle frame so we're still starting from the same
		// absolute position regardless of the handle frame count.
		InShot->ShotInfo.NumEngineWarmUpFramesRemaining = FMath::Max(InShot->ShotInfo.NumEngineWarmUpFramesRemaining - InNumHandleFrames, 0);
	}

	FFrameNumber LeftDeltaTicks = 0;
	FFrameNumber RightDeltaTicks = 0;

	UMoviePipelineAntiAliasingSetting* AntiAliasingSettings = FindOrAddSettingForShot<UMoviePipelineAntiAliasingSetting>(InShot);
	const bool bHasMultipleTemporalSamples = AntiAliasingSettings->TemporalSampleCount > 1;
	if (bHasMultipleTemporalSamples)
	{
		LeftDeltaTicks += FrameMetrics.TicksPerOutputFrame.FloorToFrame();
		RightDeltaTicks += FrameMetrics.TicksPerOutputFrame.FloorToFrame();
	}

	// Account for handle frame expansion
	LeftDeltaTicks += HandleFrameTicks;
	RightDeltaTicks += HandleFrameTicks;

	if (InSegmentData.MovieScene.IsValid())
	{
		// We need to expand the inner playback bounds to cover three features:
		// 1) Temporal Sampling (+1 frame each end)
		// 2) Handle frames (+n frames left/right)
		// 3) Using the camera-cut as real warm-up frames (+n frames left side only)
		// To keep the inner movie scene and outer sequencer section in sync we can calculate the tick delta
		// to each side and simply expand both sections like that - ignoring all start frame offsets, etc.

		// Left side only warm up.
		if (InShot->ShotInfo.NumEngineWarmUpFramesRemaining > 0 && !InShot->ShotInfo.WarmUpRangeLocal.IsEmpty())
		{
			// Handle frames eat into warm up frames (accounted for above) so we don't double add them for expansion.
			LeftDeltaTicks += (InShot->ShotInfo.WarmUpRangeLocal.Size<FFrameNumber>() - HandleFrameTicks);
		}

		// Expand our inner playback bounds and outer movie scene section to keep them in sync.
		InSegmentData.ShotSection->SetRange(UE::MovieScene::DilateRange(InSegmentData.ShotSection->GetRange(), -LeftDeltaTicks, RightDeltaTicks));
		InSegmentData.MovieScene->SetPlaybackRange(UE::MovieScene::DilateRange(InSegmentData.MovieScene->GetPlaybackRange(), -LeftDeltaTicks, RightDeltaTicks));
	}

	// Expansion of the top level playback bounds needs to happen regardless of whether there is an inner movie scene or not to cover handle frames + temporal sample.
	// This will over-expand (once per camera cut) but it's effectively cosmetic so no harm.
	TotalPlaybackRangeMaster = UE::MovieScene::DilateRange(TargetSequence->MovieScene->GetPlaybackRange(), -LeftDeltaTicks, RightDeltaTicks);

	// Ensure the overall Movie Scene Playback Range is large enough. This will clamp evaluation if we don't expand it. We hull the existing range
	// with the new range.
	TRange<FFrameNumber> EncompassingPlaybackRange = TRange<FFrameNumber>::Hull(TotalPlaybackRangeMaster, TargetSequence->MovieScene->GetPlaybackRange());
	TargetSequence->GetMovieScene()->SetPlaybackRange(EncompassingPlaybackRange);

	// Validate sections evaluated in the expanded shot 
	check(InShot->ShotInfo.OriginalRangeLocal.HasLowerBound());
	TRange<FFrameNumber> WarningRange = TRange<FFrameNumber>(
		TRangeBound<FFrameNumber>::Exclusive(InShot->ShotInfo.OriginalRangeLocal.GetLowerBoundValue() - LeftDeltaTicks), 
		TRangeBound<FFrameNumber>::Inclusive(InShot->ShotInfo.OriginalRangeLocal.GetLowerBoundValue())
	);

	// Iterate through increasing outer shot sections 
	UMovieSceneCinematicShotSection* OuterShotSection = Cast<UMovieSceneCinematicShotSection>(InShot->OuterPathKey.TryLoad());
	while (OuterShotSection)
	{
		// Warn if any of the sections are contained in the handle + temporal sampling ranges
		for (UMovieSceneSection* SectionToValidate : OuterShotSection->GetSequence()->GetMovieScene()->GetAllSections())
		{
			// Skip shot sections and camera cut sections since they don't apply and their range may already be expanded anyways
			if (SectionToValidate->GetClass() == UMovieSceneCinematicShotSection::StaticClass() ||
				SectionToValidate->GetClass() == UMovieSceneCameraCutSection::StaticClass())
			{
				continue;
			}
			
			if (SectionToValidate->GetRange().HasLowerBound() && WarningRange.Contains(SectionToValidate->GetRange().GetLowerBoundValue()))
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("A section (%s) starts before the camera cut begins but after temporal and handle samples begin. Evaluation in this area may fail unexpectedly."), *SectionToValidate->GetPathName());
			}
		}

		// Update the ranges to be in the outer sequence space
		FMovieSceneSequenceTransform InnerToOuterTransform = OuterShotSection->OuterToInnerTransform().InverseLinearOnly();
		WarningRange = InnerToOuterTransform.TransformRangePure(WarningRange);

		OuterShotSection = OuterShotSection->GetSequence()->GetTypedOuter<UMovieSceneCinematicShotSection>();
	}

	// Same warning as above, but for the top level sequence
	for (UMovieSceneSection* SectionToValidate : TargetSequence->GetMovieScene()->GetAllSections())
	{
		if (SectionToValidate->GetClass() == UMovieSceneCinematicShotSection::StaticClass() ||
			SectionToValidate->GetClass() == UMovieSceneCameraCutSection::StaticClass())
		{
			continue;
		}

		if (SectionToValidate->GetRange().HasLowerBound() && WarningRange.Contains(SectionToValidate->GetRange().GetLowerBoundValue()))
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("A section (%s) starts on or before the camera cut begins but after temporal and handle samples begin. Evaluation in this area may fail unexpectedly."), *SectionToValidate->GetPathName());
		}
	}

	// Warn for not whole frame aligned shots
	const TRange<FFrameNumber> SectionRange = InShot->ShotInfo.OriginalRangeLocal;

	TRange<FFrameTime> OriginalRangeOuter;
	const FFrameNumber LowerInMasterTicks = (SectionRange.GetLowerBoundValue() * InShot->ShotInfo.InnerToOuterTransform).FloorToFrame();
	FFrameTime OriginalRangeOuterLower = FFrameRate::TransformTime(FFrameTime(LowerInMasterTicks, 0.0f),
		TargetSequence->GetMovieScene()->GetTickResolution(), TargetSequence->GetMovieScene()->GetDisplayRate());

	if (OriginalRangeOuterLower.GetSubFrame() != 0.0f)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Detected a camera cut that started on a sub-frame (%s%s starts on %f). Output frame numbers may not match original Sequencer frame numbers"), *InShot->InnerName, InShot->OuterName.IsEmpty() ? TEXT("") : *FString(" in " + InShot->OuterName), OriginalRangeOuterLower.AsDecimal());
	}
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
	TSubclassOf<UMovieRenderDebugWidget> DebugWidgetClassToUse = DebugWidgetClass;
	if (DebugWidgetClassToUse.Get() == nullptr)
	{
		DebugWidgetClassToUse = LoadClass<UMovieRenderDebugWidget>(nullptr, TEXT("/MovieRenderPipeline/Blueprints/UI_MovieRenderPipelineScreenOverlay.UI_MovieRenderPipelineScreenOverlay_C"), nullptr, LOAD_None, nullptr);
	}

	if (DebugWidgetClassToUse.Get() != nullptr)
	{
		DebugWidget = CreateWidget<UMovieRenderDebugWidget>(GetWorld(), DebugWidgetClassToUse.Get());
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

MoviePipeline::FFrameConstantMetrics UMoviePipeline::CalculateShotFrameMetrics(const UMoviePipelineExecutorShot* InShot) const
{
	MoviePipeline::FFrameConstantMetrics Output;
	Output.TickResolution = TargetSequence->GetMovieScene()->GetTickResolution();
	Output.FrameRate = GetPipelineMasterConfig()->GetEffectiveFrameRate(TargetSequence);
	Output.TicksPerOutputFrame = FFrameRate::TransformTime(FFrameTime(FFrameNumber(1)), Output.FrameRate, Output.TickResolution);

	UMoviePipelineCameraSetting* CameraSettings = FindOrAddSettingForShot<UMoviePipelineCameraSetting>(InShot);
	UMoviePipelineAntiAliasingSetting* AntiAliasingSettings = FindOrAddSettingForShot<UMoviePipelineAntiAliasingSetting>(InShot);

	// We are overriding blur settings to account for how we sample multiple frames, so
	// we need to process any camera and post process volume settings for motion blur manually

	// Start with engine default for motion blur in the event no one overrides it.
	Output.ShutterAnglePercentage = 0.5;

	APlayerCameraManager* PlayerCameraManager = GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
	if (PlayerCameraManager)
	{
		// Apply any motion blur settings from post process volumes in the world
		FVector ViewLocation = PlayerCameraManager->GetCameraLocation();
		for (IInterface_PostProcessVolume* PPVolume : GetWorld()->PostProcessVolumes)
		{
			const FPostProcessVolumeProperties VolumeProperties = PPVolume->GetProperties();

			// Skip any volumes which are either disabled or don't modify blur amount
			if (!VolumeProperties.bIsEnabled || !VolumeProperties.Settings->bOverride_MotionBlurAmount)
			{
				continue;
			}

			float LocalWeight = FMath::Clamp(VolumeProperties.BlendWeight, 0.0f, 1.0f);

			if (!VolumeProperties.bIsUnbound)
			{
				float DistanceToPoint = 0.0f;
				PPVolume->EncompassesPoint(ViewLocation, 0.0f, &DistanceToPoint);

				if (DistanceToPoint >= 0 && DistanceToPoint < VolumeProperties.BlendRadius)
				{
					LocalWeight *= FMath::Clamp(1.0f - DistanceToPoint / VolumeProperties.BlendRadius, 0.0f, 1.0f);
				}
				else
				{
					LocalWeight = 0.0f;
				}
			}

			if (LocalWeight > 0.0f)
			{
				Output.ShutterAnglePercentage = FMath::Lerp(Output.ShutterAnglePercentage, (double)VolumeProperties.Settings->MotionBlurAmount, LocalWeight);
			}
		}

		// Now try from the camera, which takes priority over post processing volumes.
		ACameraActor* CameraActor = Cast<ACameraActor>(PlayerCameraManager->GetViewTarget());
		if (CameraActor)
		{
			UCameraComponent* CameraComponent = CameraActor->GetCameraComponent();
			if (CameraComponent && CameraComponent->PostProcessSettings.bOverride_MotionBlurAmount)
			{
				Output.ShutterAnglePercentage = FMath::Lerp(Output.ShutterAnglePercentage, (double)CameraComponent->PostProcessSettings.MotionBlurAmount, (double)CameraComponent->PostProcessBlendWeight);
			}
		}
		
		// Apply any motion blur settings from post processing blends attached to the camera manager
		TArray<FPostProcessSettings> const* CameraAnimPPSettings;
		TArray<float> const* CameraAnimPPBlendWeights;
		PlayerCameraManager->GetCachedPostProcessBlends(CameraAnimPPSettings, CameraAnimPPBlendWeights);
		for (int32 PPIdx = 0; PPIdx < CameraAnimPPBlendWeights->Num(); ++PPIdx)
		{
			if ((*CameraAnimPPSettings)[PPIdx].bOverride_MotionBlurAmount)
			{
				Output.ShutterAnglePercentage = FMath::Lerp(Output.ShutterAnglePercentage, (double)(*CameraAnimPPSettings)[PPIdx].MotionBlurAmount, (*CameraAnimPPBlendWeights)[PPIdx]);
			}
		}
	}

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

		// If the shutter angle is effectively zero, lie about how long a frame is to prevent divide by zero
		if (Output.ShutterAnglePercentage < 1.0 / 360.0)
		{
			Output.TicksWhileShutterOpen = Output.TicksPerOutputFrame * (1.0 / 360.0);
		}
		else
		{
			// Otherwise, calculate the amount of time the shutter is open.
			Output.TicksWhileShutterOpen = Output.TicksPerOutputFrame * Output.ShutterAnglePercentage;
		}

		// Divide that amongst all of our accumulation sample frames.
		Output.TicksPerSample = Output.TicksWhileShutterOpen / AntiAliasingSettings->TemporalSampleCount;

	}

	Output.ShutterClosedFraction = 1.0 - Output.ShutterAnglePercentage;
	Output.TicksWhileShutterClosed = Output.TicksPerOutputFrame - Output.TicksWhileShutterOpen;

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

UMoviePipelineSetting* UMoviePipeline::FindOrAddSettingForShot(TSubclassOf<UMoviePipelineSetting> InSetting, const UMoviePipelineExecutorShot* InShot) const
{
	// Check to see if this setting is in the shot override, if it is we'll use the shot version of that.
	if (InShot->GetShotOverrideConfiguration())
	{
		UMoviePipelineSetting* Setting = InShot->GetShotOverrideConfiguration()->FindSettingByClass(InSetting);
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

TArray<UMoviePipelineSetting*> UMoviePipeline::FindSettingsForShot(TSubclassOf<UMoviePipelineSetting> InSetting, const UMoviePipelineExecutorShot* InShot) const
{
	TArray<UMoviePipelineSetting*> FoundSettings;

	// Find all enabled settings of given subclass in the shot override first
	if (UMoviePipelineShotConfig* ShotOverride = InShot->GetShotOverrideConfiguration())
	{
		for (UMoviePipelineSetting* Setting : ShotOverride->FindSettingsByClass(InSetting))
		{
			if (Setting && Setting->IsEnabled())
			{
				FoundSettings.Add(Setting);
			}
		}
	}

	// Add all enabled settings of given subclass not overridden by shot override
	for (UMoviePipelineSetting* Setting : GetPipelineMasterConfig()->FindSettingsByClass(InSetting))
	{
		if (Setting && Setting->IsEnabled())
		{
			TSubclassOf<UMoviePipelineSetting> SettingClass = Setting->GetClass();
			if (!FoundSettings.ContainsByPredicate([SettingClass](UMoviePipelineSetting* ExistingSetting) { return ExistingSetting && ExistingSetting->GetClass() == SettingClass; } ))
			{
				FoundSettings.Add(Setting);
			}
		}
	}

	return FoundSettings;
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

void UMoviePipeline::ResolveFilenameFormatArguments(const FString& InFormatString, const FStringFormatNamedArguments& InFormatOverrides, FString& OutFinalPath, FMoviePipelineFormatArgs& OutFinalFormatArgs, const FMoviePipelineFrameOutputState* InOutputState, const int32 InFrameNumberOffset) const
{
	UMoviePipelineOutputSetting* OutputSettings = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	// Gather all the variables
	OutFinalFormatArgs = FMoviePipelineFormatArgs();
	OutFinalFormatArgs.InJob = CurrentJob;

	// Copy the file metadata from our InOutputState
	if (InOutputState)
	{
		OutFinalFormatArgs.FileMetadata = InOutputState->FileMetadata;
	}

	// Now get the settings from our config. This will expand the FileMetadata and assign the default values used in the UI.
	GetPipelineMasterConfig()->GetFormatArguments(OutFinalFormatArgs, true);

	// Ensure they used relative frame numbers in the output so they get the right number of output frames.
	bool bForceRelativeFrameNumbers = false;
	if (InFormatString.Contains(TEXT("{frame")) && InOutputState && InOutputState->TimeData.IsTimeDilated() && !InFormatString.Contains(TEXT("_rel}")))
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Time Dilation was used but output format does not use relative time, forcing relative numbers."));
		bForceRelativeFrameNumbers = true;
	}

	// From Output State
	if (InOutputState)
	{
		// Now that the settings have been added from the configuration, we overwrite them with the ones in the output state. This is required so that
		// things like frame number resolve to the actual output state correctly.
		InOutputState->GetFilenameFormatArguments(OutFinalFormatArgs, OutputSettings->ZeroPadFrameNumbers, OutputSettings->FrameNumberOffset + InFrameNumberOffset, bForceRelativeFrameNumbers);
	}

	// And from ourself
	{
		OutFinalFormatArgs.FilenameArguments.Add(TEXT("date"), InitializationTime.ToString(TEXT("%Y.%m.%d")));
		OutFinalFormatArgs.FilenameArguments.Add(TEXT("time"), InitializationTime.ToString(TEXT("%H.%M.%S")));

		FString VersionText = FString::Printf(TEXT("v%0*d"), 3, InitializationVersion);
		
		OutFinalFormatArgs.FilenameArguments.Add(TEXT("version"), VersionText);

		OutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/jobDate"), InitializationTime.ToString(TEXT("%Y.%m.%d")));
		OutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/jobTime"), InitializationTime.ToString(TEXT("%H.%M.%S")));
		OutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/jobVersion"), InitializationVersion);
		OutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/jobName"), CurrentJob->JobName);
		OutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/jobAuthor"), CurrentJob->Author);

		// By default, we don't want to show frame duplication numbers. If we need to start writing them,
		// they need to come before the frame number (so that tools recognize them as a sequence).
		OutFinalFormatArgs.FilenameArguments.Add(TEXT("file_dup"), FString());
	}

	// Overwrite the variables with overrides if needed. This allows different requesters to share the same variables (ie: filename extension, render pass name)
	for (const TPair<FString, FStringFormatArg>& KVP : InFormatOverrides)
	{
		OutFinalFormatArgs.FilenameArguments.Add(KVP);
	}

	// No extension should be provided at this point, because we need to tack the extension onto the end after appending numbers (in the event of no overwrites)
	FString BaseFilename = FString::Format(*InFormatString, OutFinalFormatArgs.FilenameArguments);
	FPaths::NormalizeFilename(BaseFilename);

	// If we end with a "." character, remove it. The extension will put it back on. We can end up with this sometimes after resolving file format strings, ie:
	// {sequence_name}.{frame_number} becomes {sequence_name}. for videos (which can't use frame_numbers).
	BaseFilename.RemoveFromEnd(TEXT("."));

	FString Extension = FString::Format(TEXT(".{ext}"), OutFinalFormatArgs.FilenameArguments);


	FString ThisTry = BaseFilename + Extension;

	if (CanWriteToFile(*ThisTry, OutputSettings->bOverrideExistingOutput))
	{
		OutFinalPath = ThisTry;
		return;
	}

	int32 DuplicateIndex = 2;
	while(true)
	{
		OutFinalFormatArgs.FilenameArguments.Add(TEXT("file_dup"), FString::Printf(TEXT("_(%d)"), DuplicateIndex));

		// Re-resolve the format string now that we've reassigned frame_dup to a number.
		ThisTry = FString::Format(*InFormatString, OutFinalFormatArgs.FilenameArguments) + Extension;

		// If the file doesn't exist, we can use that, else, increment the index and try again
		if (CanWriteToFile(*ThisTry, OutputSettings->bOverrideExistingOutput))
		{
			OutFinalPath = ThisTry;
			return;
		}

		++DuplicateIndex;
	}
}

void UMoviePipeline::SetProgressWidgetVisible(bool bVisible)
{
	if (DebugWidget)
	{
		DebugWidget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

#undef LOCTEXT_NAMESPACE // "MoviePipeline"
