// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertClientSequencerManager.h"
#include "Delegates/IDelegateInstance.h"
#include "HAL/IConsoleManager.h"
#include "Misc/QualifiedFrameTime.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "Logging/LogMacros.h"

#include "IConcertClient.h"
#include "IConcertSyncClient.h"
#include "IConcertSession.h"
#include "ConcertSettings.h"

#include "ConcertClientWorkspace.h"

#include "Engine/GameEngine.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTimeHelpers.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"

#if WITH_EDITOR
	#include "ISequencerModule.h"
	#include "ISequencer.h"
	#include "Subsystems/AssetEditorSubsystem.h"
	#include "Editor.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogConcertSequencerSync, Warning, Log)

#if WITH_EDITOR

// Enable Sequence Playback Syncing
static TAutoConsoleVariable<int32> CVarEnablePlaybackSync(TEXT("Concert.EnableSequencerPlaybackSync"), 1, TEXT("Enable Concert Sequencer Playback Syncing of opened Sequencer."));

// Enable Sequence Playing on game client
static TAutoConsoleVariable<int32> CVarEnableSequencePlayer(TEXT("Concert.EnableSequencePlayer"), 1, TEXT("Enable Concert Sequence Players on `-game` client."));

// Enable opening Sequencer on remote machine whenever a sequencer is opened, if both instance have this option on.
static TAutoConsoleVariable<int32> CVarEnableRemoteSequencerOpen(TEXT("Concert.EnableOpenRemoteSequencer"), 0, TEXT("Enable Concert remote Sequencer opening."));

// Enable opening Sequencer on remote machine whenever a sequencer is opened, if both instance have this option on.
static TAutoConsoleVariable<int32> CVarEnableUnrelatedTimelineSync(TEXT("Concert.EnableUnrelatedTimelineSync"), 0, TEXT("Enable syncing unrelated sequencer timeline."));


FConcertClientSequencerManager::FConcertClientSequencerManager(IConcertSyncClient* InOwnerSyncClient)
	: OwnerSyncClient(InOwnerSyncClient)
{
	check(OwnerSyncClient);

	bRespondingToTransportEvent = false;

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	OnSequencerCreatedHandle = SequencerModule.RegisterOnSequencerCreated(FOnSequencerCreated::FDelegate::CreateRaw(this, &FConcertClientSequencerManager::OnSequencerCreated));
	FCoreDelegates::OnEndFrame.AddRaw(this, &FConcertClientSequencerManager::OnEndFrame);
}

FConcertClientSequencerManager::~FConcertClientSequencerManager()
{
	ISequencerModule* SequencerModulePtr = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModulePtr)
	{
		SequencerModulePtr->UnregisterOnSequencerCreated(OnSequencerCreatedHandle);
	}

	FCoreDelegates::OnEndFrame.RemoveAll(this);
	
	for (FOpenSequencerData& OpenSequencer : OpenSequencers)
	{
		TSharedPtr<ISequencer> Sequencer = OpenSequencer.WeakSequencer.Pin();
		if (Sequencer.IsValid())
		{
			Sequencer->OnGlobalTimeChanged().Remove(OpenSequencer.OnGlobalTimeChangedHandle);
			Sequencer->OnCloseEvent().Remove(OpenSequencer.OnCloseEventHandle);
		}
	}
}

void FConcertClientSequencerManager::OnSequencerCreated(TSharedRef<ISequencer> InSequencer)
{
	// Find a Sequencer state for a newly opened sequencer if we have one.
	UMovieSceneSequence* Sequence = InSequencer->GetRootMovieSceneSequence();
	check(Sequence != nullptr);

	if (!SequencerStates.Contains(*Sequence->GetPathName()))
	{
		FConcertSequencerState NewState;
		NewState.Time = InSequencer->GetGlobalTime();
		SequencerStates.Add(*Sequence->GetPathName(), NewState);
	}

	FConcertSequencerState& SequencerState = SequencerStates.FindOrAdd(*Sequence->GetPathName());

	// Setup the Sequencer
	FOpenSequencerData OpenSequencer;
	OpenSequencer.WeakSequencer = TWeakPtr<ISequencer>(InSequencer);
	OpenSequencer.PlaybackMode = EPlaybackMode::Undefined;
	OpenSequencer.OnGlobalTimeChangedHandle = InSequencer->OnGlobalTimeChanged().AddRaw(this, &FConcertClientSequencerManager::OnSequencerTimeChanged, OpenSequencer.WeakSequencer);
	OpenSequencer.OnCloseEventHandle = InSequencer->OnCloseEvent().AddRaw(this, &FConcertClientSequencerManager::OnSequencerClosed);
	int OpenIndex = OpenSequencers.Add(OpenSequencer);

	// Setup stored state
	InSequencer->SetPlaybackStatus((EMovieScenePlayerStatus::Type)SequencerState.PlayerStatus);
	InSequencer->SetPlaybackSpeed(SequencerState.PlaybackSpeed);
	// Setting the global time will notify the server of this newly opened state.
	InSequencer->SetGlobalTime(SequencerState.Time.ConvertTo(InSequencer->GetRootTickResolution()));
	// Since setting the global time will potentially have set our playback mode put us back to undefined
	OpenSequencers[OpenIndex].PlaybackMode = EPlaybackMode::Undefined;

	// if we allow for Sequencer remote opening send an event, if we aren't currently responding to one
	if (!bRespondingToTransportEvent && IsSequencerRemoteOpenEnabled())
	{
		if (TSharedPtr<IConcertClientSession> Session = WeakSession.Pin())
		{
			FConcertSequencerOpenEvent OpenEvent;
			OpenEvent.SequenceObjectPath = Sequence->GetPathName();

			UE_LOG(LogConcertSequencerSync, Verbose, TEXT("OnsequencerCreated: %s"), *OpenEvent.SequenceObjectPath);
			Session->SendCustomEvent(OpenEvent, Session->GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
		}
	}
}

TArray<FConcertClientSequencerManager::FOpenSequencerData*, TInlineAllocator<1>> FConcertClientSequencerManager::GatherRootSequencersByState(const FConcertSequencerState& InSequenceState)
{
	TArray<FOpenSequencerData*, TInlineAllocator<1>> OutSequencers;
	for (FOpenSequencerData& Entry : OpenSequencers)
	{
		TSharedPtr<ISequencer> Sequencer = Entry.WeakSequencer.Pin();
		UMovieSceneSequence*   Sequence = Sequencer.IsValid() ? Sequencer->GetRootMovieSceneSequence() : nullptr;

		if (Sequence && (Sequence->GetPathName() == InSequenceState.SequenceObjectPath || CVarEnableUnrelatedTimelineSync.GetValueOnAnyThread() > 0))
		{
			OutSequencers.Add(&Entry);
		}
	}
	return OutSequencers;
}

void FConcertClientSequencerManager::SetActiveWorkspace(TSharedPtr<FConcertClientWorkspace> InWorkspace)
{
	Workspace = InWorkspace;
}

float FConcertClientSequencerManager::GetLatencyCompensationMs() const
{
	IConcertClientRef ConcertClient = OwnerSyncClient->GetConcertClient();
	return ConcertClient->IsConfigured()
		? ConcertClient->GetConfiguration()->ClientSettings.LatencyCompensationMs
		: 0.0f;
}

void FConcertClientSequencerManager::Register(TSharedRef<IConcertClientSession> InSession)
{
	// Hold onto the session so we can trigger events
	WeakSession = InSession;

	// Register our events
	InSession->RegisterCustomEventHandler<FConcertSequencerStateEvent>(this, &FConcertClientSequencerManager::OnTransportEvent);
	InSession->RegisterCustomEventHandler<FConcertSequencerCloseEvent>(this, &FConcertClientSequencerManager::OnCloseEvent);
	InSession->RegisterCustomEventHandler<FConcertSequencerOpenEvent>(this, &FConcertClientSequencerManager::OnOpenEvent);
	InSession->RegisterCustomEventHandler<FConcertSequencerStateSyncEvent>(this, &FConcertClientSequencerManager::OnSyncEvent);
}

void FConcertClientSequencerManager::Unregister(TSharedRef<IConcertClientSession> InSession)
{
	// Unregister our events and explicitly reset the session ptr
	if (TSharedPtr<IConcertClientSession> Session = WeakSession.Pin())
	{
		check(Session == InSession);
		Session->UnregisterCustomEventHandler<FConcertSequencerStateEvent>(this);
		Session->UnregisterCustomEventHandler<FConcertSequencerCloseEvent>(this);
		Session->UnregisterCustomEventHandler<FConcertSequencerOpenEvent>(this);
		Session->UnregisterCustomEventHandler<FConcertSequencerStateSyncEvent>(this);
	}
	WeakSession.Reset();
}

bool FConcertClientSequencerManager::IsSequencerPlaybackSyncEnabled() const
{
	return CVarEnablePlaybackSync.GetValueOnAnyThread() > 0;
}

void FConcertClientSequencerManager::SetSequencerPlaybackSync(bool bEnable)
{
	CVarEnablePlaybackSync->AsVariable()->Set(bEnable ? 1 : 0);
}

bool FConcertClientSequencerManager::IsUnrelatedSequencerTimelineSyncEnabled() const
{
	return CVarEnableUnrelatedTimelineSync.GetValueOnAnyThread() > 0;
}

void FConcertClientSequencerManager::SetUnrelatedSequencerTimelineSync(bool bEnable)
{
	CVarEnableUnrelatedTimelineSync->AsVariable()->Set(bEnable ? 1 : 0);
}

bool FConcertClientSequencerManager::IsSequencerRemoteOpenEnabled() const
{
	return CVarEnableRemoteSequencerOpen.GetValueOnAnyThread() > 0;
}

void FConcertClientSequencerManager::SetSequencerRemoteOpen(bool bEnable)
{
	CVarEnableRemoteSequencerOpen->AsVariable()->Set(bEnable ? 1 : 0);
}

void FConcertClientSequencerManager::OnSequencerClosed(TSharedRef<ISequencer> InSequencer)
{
	// Find the associated open sequencer index
	int Index = 0;
	for (; Index < OpenSequencers.Num(); ++Index)
	{
		if (OpenSequencers[Index].WeakSequencer.HasSameObject(&InSequencer.Get()))
		{
			break;
		}
	}
	// We didn't find the sequencer
	if (Index >= OpenSequencers.Num())
	{
		return;
	}

	FOpenSequencerData& ClosingSequencer = OpenSequencers[Index];

	// Send close event to server and put back playback mode to undefined
	TSharedPtr<IConcertClientSession> Session = WeakSession.Pin();
	if (Session.IsValid())
	{
		// Find the associated sequence path name
		UMovieSceneSequence* Sequence = InSequencer->GetRootMovieSceneSequence();
		if (Sequence)
		{
			FConcertSequencerCloseEvent CloseEvent;
			CloseEvent.bMasterClose = ClosingSequencer.PlaybackMode == EPlaybackMode::Master; // this sequencer had control over the sequence playback
			CloseEvent.SequenceObjectPath = *Sequence->GetPathName();
			Session->SendCustomEvent(CloseEvent, Session->GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
		}
	}
	else
	{
		UMovieSceneSequence* Sequence = InSequencer->GetRootMovieSceneSequence();
		if (Sequence)
		{
			SequencerStates.Remove(*Sequence->GetPathName());
		}
	}

	// Removed the closed Sequencer
	OpenSequencers.RemoveAtSwap(Index);
}

void FConcertClientSequencerManager::OnSyncEvent(const FConcertSessionContext& InEventContext, const FConcertSequencerStateSyncEvent& InEvent)
{
	for (const auto& State : InEvent.SequencerStates)
	{
		FConcertSequencerState& SequencerState = SequencerStates.FindOrAdd(*State.SequenceObjectPath);
		SequencerState = State;
		for (FOpenSequencerData* OpenSequencer : GatherRootSequencersByState(SequencerState))
		{
			TSharedPtr<ISequencer> Sequencer = OpenSequencer->WeakSequencer.Pin();
			if (Sequencer.IsValid() && IsSequencerPlaybackSyncEnabled())
			{
				Sequencer->SetGlobalTime(SequencerState.Time.ConvertTo(Sequencer->GetRootTickResolution()));
				Sequencer->SetPlaybackStatus((EMovieScenePlayerStatus::Type)SequencerState.PlayerStatus);
				Sequencer->SetPlaybackSpeed(SequencerState.PlaybackSpeed);
			}
		}
	}
}

void FConcertClientSequencerManager::OnSequencerTimeChanged(TWeakPtr<ISequencer> InSequencer)
{
	if (bRespondingToTransportEvent)
	{
		return;
	}

	TGuardValue<bool> ReentrancyGuard(bRespondingToTransportEvent, true);

	TSharedPtr<ISequencer> Sequencer = InSequencer.Pin();
	UMovieSceneSequence*   Sequence  = Sequencer.IsValid() ? Sequencer->GetRootMovieSceneSequence() : nullptr;

	TSharedPtr<IConcertClientSession> Session = WeakSession.Pin();
	if (Session.IsValid() && Sequence && IsSequencerPlaybackSyncEnabled())
	{
		// Find the entry that has been updated so we can check/assign its playback mode, or add it in case a Sequencer root sequence was just reassigned
		FConcertSequencerState& SequencerState = SequencerStates.FindOrAdd(*Sequence->GetPathName());

		FOpenSequencerData* OpenSequencer = Algo::FindBy(OpenSequencers, InSequencer, &FOpenSequencerData::WeakSequencer);
		check(OpenSequencer);
		// We only send transport events if we're driving playback (Master), or nothing is currently playing back to our knowledge (Undefined)
		// @todo: Do we need to handle race conditions and/or contention between sequencers either initiating playback or scrubbing?
		if (OpenSequencer->PlaybackMode == EPlaybackMode::Master || OpenSequencer->PlaybackMode == EPlaybackMode::Undefined)
		{
			FConcertSequencerStateEvent SequencerStateEvent;
			SequencerStateEvent.State.SequenceObjectPath	= Sequence->GetPathName();
			SequencerStateEvent.State.Time					= Sequencer->GetGlobalTime();
			SequencerStateEvent.State.PlayerStatus			= (EConcertMovieScenePlayerStatus)Sequencer->GetPlaybackStatus();
			SequencerStateEvent.State.PlaybackSpeed			= Sequencer->GetPlaybackSpeed();
			SequencerState = SequencerStateEvent.State;

			// Send to client and server
			UE_LOG(LogConcertSequencerSync, Verbose, TEXT("OnSequencerTimeChanged: %s, at frame: %d"), *SequencerStateEvent.State.SequenceObjectPath, SequencerStateEvent.State.Time.Time.FrameNumber.Value);
			Session->SendCustomEvent(SequencerStateEvent, Session->GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);

			// If we're playing then ensure we are set to master (driving the playback on all clients)
			if (SequencerStateEvent.State.PlayerStatus == EConcertMovieScenePlayerStatus::Playing)
			{
				OpenSequencer->PlaybackMode = EPlaybackMode::Master;
			}
			else
			{
				OpenSequencer->PlaybackMode = EPlaybackMode::Undefined;
			}
		}
	}
}

void FConcertClientSequencerManager::OnCloseEvent(const FConcertSessionContext&, const FConcertSequencerCloseEvent& InEvent)
{
	UE_LOG(LogConcertSequencerSync, Verbose, TEXT("OnCloseEvent: %s"), *InEvent.SequenceObjectPath);
	PendingSequenceCloseEvents.Add(InEvent);
}

void FConcertClientSequencerManager::ApplyTransportCloseEvent(const FConcertSequencerCloseEvent& PendingClose)
{
	FConcertSequencerState* SequencerState = SequencerStates.Find(*PendingClose.SequenceObjectPath);
	if (SequencerState)
	{
		// if the event was that a sequencer that was in master playback mode was closed, stop playback
		if (PendingClose.bMasterClose)
		{
			SequencerState->PlayerStatus = EConcertMovieScenePlayerStatus::Stopped;
			for (FOpenSequencerData* OpenSequencer : GatherRootSequencersByState(*SequencerState))
			{
				OpenSequencer->PlaybackMode = EPlaybackMode::Undefined;
				OpenSequencer->WeakSequencer.Pin()->SetPlaybackStatus(EMovieScenePlayerStatus::Stopped);
			}
		}
		// otherwise, discard the state, it's no longer opened.
		else
		{
			SequencerStates.Remove(*PendingClose.SequenceObjectPath);
		}
	}

	ApplyCloseToPlayers(PendingClose);
}

void FConcertClientSequencerManager::OnOpenEvent(const FConcertSessionContext&, const FConcertSequencerOpenEvent& InEvent)
{
	UE_LOG(LogConcertSequencerSync, Verbose, TEXT("OnOpenEvent: %s"), *InEvent.SequenceObjectPath);
	PendingSequenceOpenEvents.Add(InEvent.SequenceObjectPath);
}

void FConcertClientSequencerManager::ApplyTransportOpenEvent(const FString& SequenceObjectPath)
{
	TGuardValue<bool> ReentrancyGuard(bRespondingToTransportEvent, true);
#if WITH_EDITOR
	if (IsSequencerRemoteOpenEnabled() && GIsEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(SequenceObjectPath);
	}
#endif
}

void FConcertClientSequencerManager::CreateNewSequencePlayerIfNotExists(const FString& SequenceObjectPath)
{
	// we do not have a player for this state yet
	if (SequencePlayers.Contains(*SequenceObjectPath))
	{
		return;
	}

	UWorld* CurrentWorld = nullptr;
	if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
	{
		CurrentWorld = GameEngine->GetGameWorld();
	}
	check(CurrentWorld);

	ALevelSequenceActor* LevelSequenceActor = nullptr;

	// Get the actual sequence
	ULevelSequence* Sequence = LoadObject<ULevelSequence>( nullptr, *SequenceObjectPath);

	if (!Sequence)
	{
		return;
	}

	FDelegateHandle Handle;
	FMovieSceneSequencePlaybackSettings PlaybackSettings;
	// Sequencer behaves differently to Player.
	// Sequencer pauses at the last frame and Player Stops and goes to the first frame unless we set this flag.
	PlaybackSettings.bPauseAtEnd = true;

	// This call will initialize LevelSequenceActor as an output parameter.
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(CurrentWorld->PersistentLevel, Sequence, PlaybackSettings, LevelSequenceActor);
	check(Player);

	UMovieScene* Scene = Sequence->GetMovieScene();
	check(Scene != nullptr);

	Handle = Scene->OnSignatureChanged().AddLambda([SceneObj = TWeakObjectPtr<UMovieScene>(Scene),
													LevelActorObj = TWeakObjectPtr<ALevelSequenceActor>(LevelSequenceActor)]() {
		if (LevelActorObj.IsValid() && SceneObj.IsValid())
		{
			const TRange<FFrameNumber> PlayRange = SceneObj->GetPlaybackRange();
			const FFrameRate TickResolution = SceneObj->GetTickResolution();
			const FFrameRate DisplayRate = SceneObj->GetDisplayRate();

			const FFrameNumber SrcStartFrame = UE::MovieScene::DiscreteInclusiveLower(PlayRange);
			const FFrameNumber SrcEndFrame   = UE::MovieScene::DiscreteExclusiveUpper(PlayRange);

			const FFrameTime EndingTime = ConvertFrameTime(SrcEndFrame, TickResolution, DisplayRate);

			const FFrameNumber StartingFrame = ConvertFrameTime(SrcStartFrame, TickResolution, DisplayRate).FloorToFrame();
			const FFrameNumber EndingFrame   = EndingTime.FloorToFrame();

			const int32 CurrentDuration = LevelActorObj->GetSequencePlayer()->GetFrameDuration();
			const FQualifiedFrameTime QualifiedStartTime = LevelActorObj->GetSequencePlayer()->GetStartTime();
			const int32 NewDuration = (EndingFrame - StartingFrame).Value;
			if (CurrentDuration != NewDuration || QualifiedStartTime.Time.GetFrame() != StartingFrame)
			{
				LevelActorObj->GetSequencePlayer()->SetFrameRange(StartingFrame.Value, NewDuration, EndingTime.GetSubFrame());
			}
		}
	});
	SequencePlayers.Add(*SequenceObjectPath, {LevelSequenceActor, MoveTemp(Handle)});
}

void FConcertClientSequencerManager::ApplyCloseToPlayers(const FConcertSequencerCloseEvent& InEvent)
{
	FSequencePlayer Player = SequencePlayers.FindRef(*InEvent.SequenceObjectPath);
	ALevelSequenceActor* LevelSequenceActor = Player.Actor.Get();
	if (LevelSequenceActor && LevelSequenceActor->SequencePlayer)
	{
		ULevelSequence *Sequence = LevelSequenceActor->GetSequence();
		if (Sequence)
		{
			UMovieScene *Scene = Sequence->GetMovieScene();
			if (Scene && Player.SignatureChangedHandle.IsValid())
			{
				Scene->OnSignatureChanged().Remove(Player.SignatureChangedHandle);
			}
		}

		LevelSequenceActor->SequencePlayer->Stop();
	}
	UE_LOG(LogConcertSequencerSync, Verbose, TEXT("CloseEvent: %s, is from master: %d"), *InEvent.SequenceObjectPath, InEvent.bMasterClose);
	if (!InEvent.bMasterClose)
	{
		if (LevelSequenceActor)
		{
			LevelSequenceActor->Destroy(false, false);
		}
		SequencePlayers.Remove(*InEvent.SequenceObjectPath);
	}
}

void FConcertClientSequencerManager::OnTransportEvent(const FConcertSessionContext&, const FConcertSequencerStateEvent& InEvent)
{
	PendingSequencerEvents.Add(InEvent.State);
}

void FConcertClientSequencerManager::ApplyTransportEvent(const FConcertSequencerState& EventState)
{
	if (bRespondingToTransportEvent)
	{
		return;
	}

	TGuardValue<bool> ReentrancyGuard(bRespondingToTransportEvent, true);

	// Update the sequencer pointing to the same sequence
	FConcertSequencerState& SequencerState = SequencerStates.FindOrAdd(*EventState.SequenceObjectPath);

	// Record the Sequencer State
	SequencerState = EventState;

	if (GIsEditor)
	{
		ApplyEventToSequencers(SequencerState);
	}
	else if (CVarEnableSequencePlayer.GetValueOnAnyThread() > 0)
	{
		CreateNewSequencePlayerIfNotExists(*EventState.SequenceObjectPath);
		ApplyEventToPlayers(SequencerState);
	}
}

void FConcertClientSequencerManager::ApplyEventToSequencers(const FConcertSequencerState& EventState)
{
	UE_LOG(LogConcertSequencerSync, Verbose, TEXT("ApplyEvent: %s, at frame: %d"), *EventState.SequenceObjectPath, EventState.Time.Time.FrameNumber.Value);
	FConcertSequencerState& SequencerState = SequencerStates.FindOrAdd(*EventState.SequenceObjectPath);

	// Record the Sequencer State
	SequencerState = EventState;

	float LatencyCompensationMs = GetLatencyCompensationMs();

	// Update all opened sequencer with this root sequence
	for (FOpenSequencerData* OpenSequencer : GatherRootSequencersByState(EventState))
	{
		ISequencer* Sequencer = OpenSequencer->WeakSequencer.Pin().Get();
		// If the entry is driving playback (PlaybackMode == Master) then we never respond to external transport events
		if (!Sequencer || OpenSequencer->PlaybackMode == EPlaybackMode::Master)
		{
			return;
		}

		FFrameRate SequenceRate = Sequencer->GetRootTickResolution();
		FFrameTime IncomingTime = EventState.Time.ConvertTo(SequenceRate);

		// If the event is coming from a sequencer that is playing back, we are a slave to its updates until it stops
		// We also apply any latency compensation when playing back
		if (EventState.PlayerStatus == EConcertMovieScenePlayerStatus::Playing)
		{
			OpenSequencer->PlaybackMode = EPlaybackMode::Slave;

			FFrameTime CurrentTime = Sequencer->GetGlobalTime().Time;

			// We should be playing back, but are not currently - we compensate the event time for network latency and commence playback
			if (Sequencer->GetPlaybackStatus() != EMovieScenePlayerStatus::Playing)
			{
				// @todo: latency compensation could be more accurate (and automatic) if we're genlocked, and events are timecoded.
				// @todo: latency compensation does not take into account slomo tracks on the sequence - should it? (that would be intricate to support)
				FFrameTime CompensatedTime = IncomingTime + (LatencyCompensationMs / 1000.0) * SequenceRate;

				// Log time metrics
				UE_LOG(LogConcertSequencerSync, Display, TEXT(
					"Starting multi-user playback for sequence '%s':\n"
					"    Current Time           = %d+%fs (%f seconds)\n"
					"    Incoming Time          = %d+%fs (%f seconds)\n"
					"    Compensated Time       = %d+%fs (%f seconds)"),
					*EventState.SequenceObjectPath,
					CurrentTime.FrameNumber.Value, CurrentTime.GetSubFrame(), CurrentTime / SequenceRate,
					IncomingTime.FrameNumber.Value, IncomingTime.GetSubFrame(), IncomingTime / SequenceRate,
					CompensatedTime.FrameNumber.Value, CompensatedTime.GetSubFrame(), CompensatedTime / SequenceRate
				);

				Sequencer->SetGlobalTime(CompensatedTime);
				Sequencer->SetPlaybackStatus(EMovieScenePlayerStatus::Playing);
				Sequencer->SetPlaybackSpeed(EventState.PlaybackSpeed);
			}
			else
			{
				// We're already playing so just report the time metrics, but adjust playback speed
				FFrameTime Error = FMath::Abs(IncomingTime - CurrentTime);
				Sequencer->SetPlaybackSpeed(EventState.PlaybackSpeed);

				UE_LOG(LogConcertSequencerSync, Display, TEXT(
					"Incoming update to sequence '%s':\n"
					"    Current Time       = %d+%fs (%f seconds)\n"
					"    Incoming Time      = %d+%fs (%f seconds)\n"
					"    Error              = %d+%fs (%f seconds)"),
					*EventState.SequenceObjectPath,
					CurrentTime.FrameNumber.Value, CurrentTime.GetSubFrame(), CurrentTime / SequenceRate,
					IncomingTime.FrameNumber.Value, IncomingTime.GetSubFrame(), IncomingTime / SequenceRate,
					Error.FrameNumber.Value, Error.GetSubFrame(), Error / SequenceRate
				);
			}
		}
		else
		{
			OpenSequencer->PlaybackMode = EPlaybackMode::Undefined;

			// If the incoming event is not playing back, set the player status to that of the event, and set the time
			if (Sequencer->GetPlaybackStatus() != (EMovieScenePlayerStatus::Type)EventState.PlayerStatus)
			{
				Sequencer->SetPlaybackStatus((EMovieScenePlayerStatus::Type)EventState.PlayerStatus);
			}

			// Set time after the status so that audio correctly stops playing after the sequence stops
			Sequencer->SetGlobalTime(IncomingTime);
			Sequencer->SetPlaybackSpeed(EventState.PlaybackSpeed);
		}
	}
}


void FConcertClientSequencerManager::ApplyEventToPlayers(const FConcertSequencerState& EventState)
{
	FConcertClientSequencerManager::FSequencePlayer* SeqPlayer = SequencePlayers.Find(*EventState.SequenceObjectPath);

	if (!SeqPlayer)
	{
		return;
	}

	ALevelSequenceActor* LevelSequenceActor = SeqPlayer->Actor.Get();
	if (LevelSequenceActor && LevelSequenceActor->SequencePlayer)
	{
		ULevelSequencePlayer* Player = LevelSequenceActor->SequencePlayer;
		check(Player);
		float LatencyCompensationMs = GetLatencyCompensationMs();

		FFrameRate SequenceRate = Player->GetFrameRate();
		FFrameTime IncomingTime = EventState.Time.ConvertTo(SequenceRate);

		// If the event is coming from a sequencer that is playing back, we are a slave to its updates until it stops
		// We also apply any latency compensation when playing back
		if (EventState.PlayerStatus == EConcertMovieScenePlayerStatus::Playing)
		{
			FFrameTime CurrentTime = Player->GetCurrentTime().Time;

			// We should be playing back, but are not currently - we compensate the event time for network latency and commence playback
			if (!Player->IsPlaying())
			{
				// @todo: latency compensation could be more accurate (and automatic) if we're genlocked, and events are timecoded.
				// @todo: latency compensation does not take into account slomo tracks on the sequence - should it? (that would be intricate to support)
				FFrameTime CompensatedTime = IncomingTime + (LatencyCompensationMs / 1000.0) * SequenceRate;

				// Log time metrics
				UE_LOG(LogConcertSequencerSync, Display, TEXT(
					"Starting multi-user playback for sequence '%s':\n"
					"    Current Time           = %d+%fs (%f seconds)\n"
					"    Incoming Time          = %d+%fs (%f seconds)\n"
					"    Compensated Time       = %d+%fs (%f seconds)"),
					*EventState.SequenceObjectPath,
					CurrentTime.FrameNumber.Value, CurrentTime.GetSubFrame(), CurrentTime / SequenceRate,
					IncomingTime.FrameNumber.Value, IncomingTime.GetSubFrame(), IncomingTime / SequenceRate,
					CompensatedTime.FrameNumber.Value, CompensatedTime.GetSubFrame(), CompensatedTime / SequenceRate
				);

				Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(CompensatedTime, EUpdatePositionMethod::Play));
				Player->SetPlayRate(EventState.PlaybackSpeed);
				Player->Play();
			}
			else
			{
				// We're already playing so just report the time metrics, but adjust playback speed
				FFrameTime Error = FMath::Abs(IncomingTime - CurrentTime);
				Player->SetPlayRate(EventState.PlaybackSpeed);

				UE_LOG(LogConcertSequencerSync, Display, TEXT(
					"Incoming update to sequence '%s':\n"
					"    Current Time       = %d+%fs (%f seconds)\n"
					"    Incoming Time      = %d+%fs (%f seconds)\n"
					"    Error              = %d+%fs (%f seconds)"),
					*EventState.SequenceObjectPath,
					CurrentTime.FrameNumber.Value, CurrentTime.GetSubFrame(), CurrentTime / SequenceRate,
					IncomingTime.FrameNumber.Value, IncomingTime.GetSubFrame(), IncomingTime / SequenceRate,
					Error.FrameNumber.Value, Error.GetSubFrame(), Error / SequenceRate
				);
			}
		}
		else
		{
			switch (EventState.PlayerStatus)
			{
			case EConcertMovieScenePlayerStatus::Stepping:
				// fallthrough, handles as scrub
			case EConcertMovieScenePlayerStatus::Scrubbing:
				Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(IncomingTime, EUpdatePositionMethod::Scrub));
				break;
			case EConcertMovieScenePlayerStatus::Paused:
				Player->Pause();
				Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(IncomingTime, EUpdatePositionMethod::Jump));
				break;
			case EConcertMovieScenePlayerStatus::Stopped:
				// Stopping will reset the position, so we need to stop first and then set the position.
				Player->Pause();
				Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(IncomingTime, EUpdatePositionMethod::Jump));
				break;
			case EConcertMovieScenePlayerStatus::Jumping:
				// fallthrough, handles as stop
			default:
				Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(IncomingTime, EUpdatePositionMethod::Jump));
			}

			Player->SetPlayRate(EventState.PlaybackSpeed);
		}
	}
}

void FConcertClientSequencerManager::OnEndFrame()
{
	TSharedPtr<FConcertClientWorkspace> SharedWorkspace = Workspace.Pin();
	if (SharedWorkspace)
	{
		if (!SharedWorkspace->CanProcessPendingPackages())
		{
			// There is currently a lock on the workspace.  We should wait for those to finish before processing
			// sequencer events.
			//
			return;
		}
	}

	for (const FConcertSequencerCloseEvent& CloseEvent: PendingSequenceCloseEvents)
	{
		ApplyTransportCloseEvent(CloseEvent);
	}
	PendingSequenceCloseEvents.Reset();

	for (const FString& SequenceObjectPath : PendingSequenceOpenEvents)
	{
		ApplyTransportOpenEvent(SequenceObjectPath);
	}
	PendingSequenceOpenEvents.Reset();

	for (const FConcertSequencerState& State : PendingSequencerEvents)
	{
		ApplyTransportEvent(State);
	}
	PendingSequencerEvents.Reset();
}

void FConcertClientSequencerManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	TArray<ALevelSequenceActor*> Actors;
	for (TTuple<FName, FSequencePlayer>& Item : SequencePlayers)
	{
		if (Item.Value.Actor.IsValid())
		{
			Actors.Add(Item.Value.Actor.Get());
		}
	}
	Collector.AddReferencedObjects(Actors);
}

#endif

