// Copyright Epic Games, Inc. All Rights Reserved.
#include "MoviePipelineInProcessExecutor.h"
#include "MoviePipeline.h"
#include "Engine/World.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineInProcessExecutorSettings.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Misc/PackageName.h"

#define LOCTEXT_NAMESPACE "MoviePipelineInProcessExecutor"
void UMoviePipelineInProcessExecutor::Start(const UMoviePipelineExecutorJob* InJob)
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMoviePipelineInProcessExecutor::OnMapLoadFinished);

	// Force the engine into fixed timestep mode. There may be a global delay on the job that passes a fixed
	// number of frames, so we want those frames to always pass the same amount of time for determinism. 
	ULevelSequence* LevelSequence = CastChecked<ULevelSequence>(InJob->Sequence.TryLoad());
	if (LevelSequence)
	{
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(InJob->GetConfiguration()->GetEffectiveFrameRate(LevelSequence).AsInterval());
	}
}

void UMoviePipelineInProcessExecutor::OnMapLoadFinished(UWorld* NewWorld)
{
	// NewWorld can be null if a world is being destroyed.
	if (!NewWorld)
	{
		FCoreDelegates::OnBeginFrame.RemoveAll(this);
		return;
	}
	
	// Stop listening for map load until we're done and know we want to start the next config.
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	
	UMoviePipelineExecutorJob* CurrentJob = Queue->GetJobs()[CurrentPipelineIndex];

	ActiveMoviePipeline = NewObject<UMoviePipeline>(NewWorld, TargetPipelineClass);

	// We allow users to set a multi-frame delay before we actually run the Initialization function and start thinking.
	// This solves cases where there are engine systems that need to finish loading before we do anything.
	const UMoviePipelineInProcessExecutorSettings* ExecutorSettings = GetDefault<UMoviePipelineInProcessExecutorSettings>();

	// We tick each frame to update the Window Title, and kick off latent pipeling initialization.
	FCoreDelegates::OnBeginFrame.AddUObject(this, &UMoviePipelineInProcessExecutor::OnTick);
	
	// Listen for when the pipeline thinks it has finished.
	ActiveMoviePipeline->OnMoviePipelineFinished().AddUObject(this, &UMoviePipelineInProcessExecutor::OnMoviePipelineFinished);
	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UMoviePipelineInProcessExecutor::OnApplicationQuit);

	// Wait until we actually recieved the right map and created the pipeline before saying that we're actively rendering
	bIsRendering = true;
	
	if (ExecutorSettings->InitialDelayFrameCount == 0)
	{
		ActiveMoviePipeline->Initialize(Queue->GetJobs()[CurrentPipelineIndex]);
		RemainingInitializationFrames = -1;
	}
	else
	{
		RemainingInitializationFrames = ExecutorSettings->InitialDelayFrameCount;	
	}
}

void UMoviePipelineInProcessExecutor::OnTick()
{
	if (RemainingInitializationFrames >= 0)
	{
		if (RemainingInitializationFrames == 0)
		{
			ActiveMoviePipeline->Initialize(Queue->GetJobs()[CurrentPipelineIndex]);
		}

		RemainingInitializationFrames--;
	}

	FText WindowTitle = GetWindowTitle();
	UKismetSystemLibrary::SetWindowTitle(WindowTitle);
}

void UMoviePipelineInProcessExecutor::OnApplicationQuit()
{
	// Only call Shutdown if the pipeline hasn't been finished.
	if (ActiveMoviePipeline && ActiveMoviePipeline->GetPipelineState() != EMovieRenderPipelineState::Finished)
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineInProcessExecutor: Application quit while Movie Pipeline was still active. Stalling to do full shutdown."));

		// This will flush any outstanding work on the movie pipeline (file writes) immediately
		ActiveMoviePipeline->RequestShutdown(); // Set the Shutdown Requested flag.
		ActiveMoviePipeline->Shutdown(); // Flush the shutdown.

		UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineInProcessExecutor: Stalling finished, pipeline has shut down."));
	}
}

void UMoviePipelineInProcessExecutor::OnMoviePipelineFinished(UMoviePipeline* InMoviePipeline)
{
	FCoreDelegates::OnBeginFrame.RemoveAll(this);
	UMoviePipeline* MoviePipeline = ActiveMoviePipeline;

	if (ActiveMoviePipeline)
	{
		// Unsubscribe in the event that it gets called twice we don't have issues.
		ActiveMoviePipeline->OnMoviePipelineFinished().RemoveAll(this);
	}

	// Null these out now since OnIndividualPipelineFinished might invoke something that causes a GC
	// and we want them to go away with the GC.
	ActiveMoviePipeline = nullptr;

	// Now that another frame has passed and we should be OK to start another PIE session, notify our owner.
	OnIndividualPipelineFinished(MoviePipeline);
}

void UMoviePipelineInProcessExecutor::OnIndividualPipelineFinished(UMoviePipeline* FinishedPipeline)
{
	if (CurrentPipelineIndex < Queue->GetJobs().Num() - 1)
	{
		// Get the next job in the queue
		UMoviePipelineExecutorJob* NextJob = Queue->GetJobs()[CurrentPipelineIndex + 1];

		FString MapOptions;

		// Initialize the transient settings so that they will exist in time for the GameOverrides check.
		NextJob->GetConfiguration()->InitializeTransientSettings();

		TArray<UMoviePipelineSetting*> AllSettings = NextJob->GetConfiguration()->GetAllSettings();
		UMoviePipelineSetting** GameOverridesPtr = AllSettings.FindByPredicate([](UMoviePipelineSetting* InSetting) { return InSetting->GetClass() == UMoviePipelineGameOverrideSetting::StaticClass(); });
		if (GameOverridesPtr)
		{
			UMoviePipelineSetting* Setting = *GameOverridesPtr;
			if (Setting)
			{
				UMoviePipelineGameOverrideSetting* GameOverrideSetting = CastChecked<UMoviePipelineGameOverrideSetting>(Setting);
				if (GameOverrideSetting->GameModeOverride)
				{
					FString GameModeOverride = FPackageName::GetShortName(*GameOverrideSetting->GameModeOverride->GetPathName());
					MapOptions = TEXT("?game=") + GameModeOverride;
				}

			}
		}

		UGameplayStatics::OpenLevel(FinishedPipeline->GetWorld(), NextJob->Map.GetAssetPathName(), true, MapOptions);
	}

	Super::OnIndividualPipelineFinished(FinishedPipeline);

}
#undef LOCTEXT_NAMESPACE // "MoviePipelineInProcessExecutor"
