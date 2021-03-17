// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneReplaySystem.h"
#include "CoreGlobals.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneMasterInstantiatorSystem.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/SpectatorPawnMovement.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneFwd.h"
#include "Sections/MovieSceneReplaySection.h"
#include "EntitySystem/MovieSceneEntityFactory.h"
#include "EntitySystem/MovieSceneEntityFactoryTemplates.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "ReplaySubsystem.h"
#include "TimerManager.h"

namespace UE
{
namespace MovieScene
{

static TUniquePtr<FReplayComponentTypes> GReplayComponentTypes;

FReplayComponentTypes* FReplayComponentTypes::Get()
{
	if (!GReplayComponentTypes.IsValid())
	{
		GReplayComponentTypes.Reset(new FReplayComponentTypes);
	}
	return GReplayComponentTypes.Get();
}

FReplayComponentTypes::FReplayComponentTypes()
{
	using namespace UE::MovieScene;

	FComponentRegistry* ComponentRegistry = UMovieSceneEntitySystemLinker::GetComponents();

	ComponentRegistry->NewComponentType(&Replay, TEXT("Replay"));

	ComponentRegistry->Factories.DuplicateChildComponent(Replay);
}

} // namespace MovieScene
} // namespace UE

FDelegateHandle UMovieSceneReplaySystem::PreLoadMapHandle;
FDelegateHandle UMovieSceneReplaySystem::PostLoadMapHandle;
FDelegateHandle UMovieSceneReplaySystem::ReplayStartedHandle;
FTimerHandle UMovieSceneReplaySystem::ReEvaluateHandle;

UMovieSceneReplaySystem::UMovieSceneReplaySystem(const FObjectInitializer& ObjInit)
	: UMovieSceneEntitySystem(ObjInit)
{
	using namespace UE::MovieScene;

	const FReplayComponentTypes* ReplayComponents = FReplayComponentTypes::Get();
	RelevantComponent = ReplayComponents->Replay;

	Phase = ESystemPhase::Instantiation | ESystemPhase::Evaluation | ESystemPhase::Finalization;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UMovieSceneMasterInstantiatorSystem::StaticClass(), GetClass());
	}
}

void UMovieSceneReplaySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	// Bail out if we're not in a PIE/Game session... we can't replay stuff in editor.
	UWorld* OwningWorld = GetWorld();
	if (OwningWorld == nullptr || (OwningWorld->WorldType != EWorldType::Game && OwningWorld->WorldType != EWorldType::PIE))
	{
		return;
	}

	using namespace UE::MovieScene;

	FMovieSceneEntitySystemRunner* ActiveRunner = Linker->GetActiveRunner();
	ESystemPhase CurrentPhase = ActiveRunner->GetCurrentPhase();

	if (CurrentPhase == ESystemPhase::Instantiation)
	{
		OnRunInstantiation();
	}
	else if (CurrentPhase == ESystemPhase::Evaluation)
	{
		OnRunEvaluation();
	}
	else if (CurrentPhase == ESystemPhase::Finalization)
	{
		OnRunFinalization();
	}
}

void UMovieSceneReplaySystem::OnRunInstantiation()
{
	using namespace UE::MovieScene;

	const FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	const FReplayComponentTypes* ReplayComponents = FReplayComponentTypes::Get();

	// Check if we have any previously active replay.
	FReplayInfo PreviousReplayInfo;
	if (CurrentReplayInfos.Num() > 0)
	{
		PreviousReplayInfo = CurrentReplayInfos[0];
	}

	// Update our list of active replays.
	auto RemoveOldReplayInfos = [this](const FInstanceHandle& InstanceHandle, const FReplayComponentData& ReplayData)
	{
		if (ReplayData.Section != nullptr)
		{
			const FReplayInfo Key{ ReplayData.Section, InstanceHandle };
			const int32 Removed = CurrentReplayInfos.RemoveSingle(Key);
			ensure(Removed == 1);
		}
	};

	FEntityTaskBuilder()
		.Read(BuiltInComponents->InstanceHandle)
		.Read(ReplayComponents->Replay)
		.FilterAll({ BuiltInComponents->Tags.NeedsUnlink })
		.Iterate_PerEntity(&Linker->EntityManager, RemoveOldReplayInfos);

	auto AddNewReplayInfos = [this](const FInstanceHandle& InstanceHandle, const FReplayComponentData& ReplayData)
	{
		if (ReplayData.Section != nullptr)
		{
			const FReplayInfo Key{ ReplayData.Section, InstanceHandle };
			CurrentReplayInfos.Add(Key);
		}
	};

	FEntityTaskBuilder()
		.Read(BuiltInComponents->InstanceHandle)
		.Read(ReplayComponents->Replay)
		.FilterAll({ BuiltInComponents->Tags.NeedsLink })
		.Iterate_PerEntity(&Linker->EntityManager, AddNewReplayInfos);


	// Check if we have any new current active replay.
	FReplayInfo NewReplayInfo;
	if (CurrentReplayInfos.Num() > 0)
	{
		NewReplayInfo = CurrentReplayInfos[0];
	}

	const FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	// If we have lost our previous replay, stop it.
	if (PreviousReplayInfo.IsValid() &&
		(NewReplayInfo != PreviousReplayInfo))
	{
		const FSequenceInstance& Instance = InstanceRegistry->GetInstance(PreviousReplayInfo.InstanceHandle);
		IMovieScenePlayer* Player = Instance.GetPlayer();
		const UWorld* World = Player->GetPlaybackContext()->GetWorld();
		UGameInstance* GameInstance = World->GetGameInstance();
		UReplaySubsystem* ReplaySubsystem = GameInstance ? GameInstance->GetSubsystem<UReplaySubsystem>() : nullptr;
		if (ReplaySubsystem != nullptr)
		{
			ReplaySubsystem->bLoadDefaultMapOnStop = false;

			World->GetTimerManager().SetTimerForNextTick([ReplaySubsystem]()
				{
					ReplaySubsystem->StopReplay();
				});
		}
	}

	// If we have a new replay, start it... although it may have already been started, in which case we catch up with it.
	// This happens because the first time we get here, it runs the replay, which loads the replay's map, which wipes
	// everything. Once the replay map is loaded, the new evaluation gets us back here and it *looks* like we have a brand
	// new replay, but that's not really the case.
	if (NewReplayInfo.IsValid() &&
		NewReplayInfo != PreviousReplayInfo)
	{
		const FSequenceInstance& Instance = InstanceRegistry->GetInstance(NewReplayInfo.InstanceHandle);
		IMovieScenePlayer* Player = Instance.GetPlayer();
		const FMovieSceneContext Context = Instance.GetContext();
		const UWorld* World = Player->GetPlaybackContext()->GetWorld();

		UDemoNetDriver* DemoNetDriver = World->GetDemoNetDriver();
		if (DemoNetDriver == nullptr)
		{
			UGameInstance* GameInstance = World->GetGameInstance();
			UReplaySubsystem* ReplaySubsystem = GameInstance ? GameInstance->GetSubsystem<UReplaySubsystem>() : nullptr;
			if (ReplaySubsystem != nullptr)
			{
				// Delay starting replay until next tick so that we don't have any problems loading a new level while we're
				// in the middle of the sequencer evaluation.
				const FString ReplayName = NewReplayInfo.Section->ReplayName;
				World->GetTimerManager().SetTimerForNextTick([ReplaySubsystem, ReplayName]()
					{
						ReplaySubsystem->PlayReplay(ReplayName, nullptr, TArray<FString>());
					});

				// We have a few things to do just before/after the map has been loaded.
				if (!PreLoadMapHandle.IsValid())
				{
					PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddStatic(UMovieSceneReplaySystem::OnPreLoadMap, Player);
				}
				if (!PostLoadMapHandle.IsValid())
				{
					PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddStatic(UMovieSceneReplaySystem::OnPostLoadMap, Player, Context);
				}

				// Hook up a callback for when replay has started, so we can do a bunch of specific things.
				// It must not rely on any current objects because this replay system, linker, and everything that goes with it
				// will be torn down once the replay starts streaming in a new map.
				if (!ReplayStartedHandle.IsValid())
				{
					ReplayStartedHandle = FNetworkReplayDelegates::OnReplayStarted.AddStatic(UMovieSceneReplaySystem::OnReplayStarted, Player, Context);
				}
			}
		}
		// else: we are already in replay... we were just re-created in the loaded replay level.
	}

	// Initialize other stuff we need for evaluation.
	if (ShowFlagMotionBlur == nullptr)
	{
		ShowFlagMotionBlur = IConsoleManager::Get().FindConsoleVariable(TEXT("showflag.motionblur"));
	}
}

void UMovieSceneReplaySystem::OnRunEvaluation()
{
	using namespace UE::MovieScene;

	FReplayInfo ActiveReplayInfo;
	if (CurrentReplayInfos.Num() > 0)
	{
		ActiveReplayInfo = CurrentReplayInfos[0];
	}

	if (ActiveReplayInfo.IsValid())
	{
		const FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();
		const FSequenceInstance& Instance = InstanceRegistry->GetInstance(ActiveReplayInfo.InstanceHandle);

		IMovieScenePlayer* Player = Instance.GetPlayer();
		const FMovieSceneContext& Context = Instance.GetContext();

		UWorld* World = Player->GetPlaybackContext()->GetWorld();
		AWorldSettings* WorldSettings = World->GetWorldSettings();
		UDemoNetDriver* DemoNetDriver = World->GetDemoNetDriver();

		if (DemoNetDriver)
		{
			// Set time dilation and current demo time according to our current sequencer playback.
			const bool bIsPlaying = (Player->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing);
			const bool bIsScrubbing = Context.HasJumped() ||
				(Player->GetPlaybackStatus() == EMovieScenePlayerStatus::Scrubbing) ||
				(Player->GetPlaybackStatus() == EMovieScenePlayerStatus::Jumping) ||
				(Player->GetPlaybackStatus() == EMovieScenePlayerStatus::Stepping);

			if (bIsPlaying)
			{
				WorldSettings->DemoPlayTimeDilation = 1.f;
				WorldSettings->SetPauserPlayerState(nullptr);
			}
			else
			{
				// Some games don't support a true time dilation of zero so we need to respect
				// their minimum time dilation value.
				WorldSettings->DemoPlayTimeDilation = WorldSettings->MinGlobalTimeDilation;
			}

			const float CurrentTime = Context.GetFrameRate().AsSeconds(Context.GetTime());
			if (bIsScrubbing)
			{
				DemoNetDriver->GotoTimeInSeconds(CurrentTime);
			}
			else
			{
				// Keep time in sync with sequencer while playing.
				DemoNetDriver->SetDemoCurrentTime(CurrentTime);
			}

			// Set some CVars according to the playback state.
			if (ShowFlagMotionBlur)
			{
				int32 ShowMotionBlur = bIsPlaying ? 1 : 0;
				ShowFlagMotionBlur->Set(ShowMotionBlur);
			}

			// Hack some stuff for known spectator controllers.
			if (APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				if (ASpectatorPawn* SpectatorPawn = PlayerController->GetSpectatorPawn())
				{
					if (USpectatorPawnMovement* SpectatorPawnMovement = Cast<USpectatorPawnMovement>(SpectatorPawn->GetMovementComponent()))
					{
						SpectatorPawnMovement->bIgnoreTimeDilation = true;
					}
				}
			}
		}
		// else: replay hasn't yet started (loading map, etc.)
	}
}

void UMovieSceneReplaySystem::OnRunFinalization()
{
}

void UMovieSceneReplaySystem::OnPreLoadMap(const FString& MapName, IMovieScenePlayer* Player)
{
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	PreLoadMapHandle.Reset();

	// Clear the spawn register, so that any spawnables (like cameras) can respawn immediately in the newly loaded replay map.
	// This isn't done by default because when the map gets unloaded everything gets destroyed and we don't end up being able
	// to call "Finish" on our sequence instances.
	// We could call "Finish" here but we dont' want that either because it actually re-evaluates the sequence one last time, 
	// which would re-trigger the replay and re-re-load the replay map again.
	// So we just do the minimum we need here.
	FMovieSceneSpawnRegister& SpawnRegister = Player->GetSpawnRegister();
	SpawnRegister.ForgetExternallyOwnedSpawnedObjects(Player->State, *Player);
	SpawnRegister.CleanUp(*Player);
}

void UMovieSceneReplaySystem::OnPostLoadMap(UWorld* World, IMovieScenePlayer* LastPlayer, FMovieSceneContext LastContext)
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	PostLoadMapHandle.Reset();

	// After the map has loaded, we wait for the pawn to respawn so we can re-evaluate the sequence to force re-creating the linker,
	// systems, and so on inside the new map. Otherwise, the new map starts replay and we have nothing to control it (until the user scrubs).
	//
	// THIS IS WRONG: we have no idea if "LastPlayer" is still valid! In the case of the sequencer editor, it is, but we don't know that!
	World->GetTimerManager().SetTimer(ReEvaluateHandle, [World, LastPlayer, LastContext]()
		{
			if (APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				if (ASpectatorPawn* SpectatorPawn = PlayerController->GetSpectatorPawn())
				{
					World->GetTimerManager().ClearTimer(ReEvaluateHandle);

					FMovieSceneRootEvaluationTemplateInstance& RootEvalTemplate = LastPlayer->GetEvaluationTemplate();
					RootEvalTemplate.Evaluate(LastContext, *LastPlayer);
				}
			}
		},
		0.1f,
		true);
}

void UMovieSceneReplaySystem::OnReplayStarted(UWorld* World, IMovieScenePlayer* LastPlayer, FMovieSceneContext LastContext)
{
	FNetworkReplayDelegates::OnReplayStarted.Remove(ReplayStartedHandle);
	ReplayStartedHandle.Reset();
}

bool UMovieSceneReplaySystem::FReplayInfo::IsValid() const
{
	return Section != nullptr && !Section->ReplayName.IsEmpty() && InstanceHandle.IsValid();
}
