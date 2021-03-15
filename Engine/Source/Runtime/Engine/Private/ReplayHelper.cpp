// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReplayHelper.h"
#include "Engine/ActorChannel.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetworkObjectList.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "Net/DataReplication.h"
#include "Net/NetworkGranularMemoryLogging.h"
#include "Net/RepLayout.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UnrealEngine.h"
#include "EngineUtils.h"
#include "ReplayNetConnection.h"

extern TAutoConsoleVariable<int32> CVarWithLevelStreamingFixes;
extern TAutoConsoleVariable<int32> CVarWithDeltaCheckpoints;
extern TAutoConsoleVariable<int32> CVarWithGameSpecificFrameData;
extern TAutoConsoleVariable<int32> CVarEnableCheckpoints;
extern TAutoConsoleVariable<float> CVarCheckpointUploadDelayInSeconds;
extern TAutoConsoleVariable<float> CVarCheckpointSaveMaxMSPerFrameOverride;

CSV_DECLARE_CATEGORY_EXTERN(Demo);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FScopedPacketManager::FScopedPacketManager(UNetConnection& InConnection, TArray<FQueuedDemoPacket>& InPackets, const uint32 InSeenLevelIndex) :
	Connection(InConnection),
	Packets(InPackets),
	SeenLevelIndex(InSeenLevelIndex)
{
	FReplayHelper::FlushNetChecked(Connection);
	StartPacketCount = Packets.Num();
}

FScopedPacketManager::~FScopedPacketManager()
{
	FReplayHelper::FlushNetChecked(Connection);
	AssociatePacketsWithLevel();
}

void FScopedPacketManager::AssociatePacketsWithLevel()
{
	for (int32 i = StartPacketCount; i < Packets.Num(); i++)
	{
		Packets[i].SeenLevelIndex = SeenLevelIndex;
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

FReplayHelper::FReplayHelper()
	: CurrentLevelIndex(0)
	, DemoFrameNum(0)
	, DemoCurrentTime(0.0f)
	, DemoTotalTime(0.0f)
	, LastCheckpointTime(0.0)
	, LatestReadFrameTime(0)
	, bWasStartRecordingSuccessful(false)
	, bIsWaitingForStream(false)
	, bIsLoadingCheckpoint(false)
	, bHasLevelStreamingFixes(false)
	, bHasDeltaCheckpoints(false)
	, bHasGameSpecificFrameData(false)
	, bPauseRecording(false)
	, bRecordMapChanges(false)
	, CheckpointSaveMaxMSPerFrame(0)
	, NumLevelsAddedThisFrame(0)
{
}

FReplayHelper::~FReplayHelper()
{
	FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
}

TSharedPtr<INetworkReplayStreamer> FReplayHelper::Init(const FURL& URL)
{
	DemoURL = URL;

	const TCHAR* const StreamerOverride = DemoURL.GetOption(TEXT("ReplayStreamerOverride="), nullptr);
	ReplayStreamer = FNetworkReplayStreaming::Get().GetFactory(StreamerOverride).CreateReplayStreamer();

	if (ReplayStreamer.IsValid())
	{
		ReplayStreamer->SetAnalyticsProvider(AnalyticsProvider);

		const TCHAR* const DemoPath = DemoURL.GetOption(TEXT("ReplayStreamerDemoPath="), nullptr);
		if (DemoPath != nullptr)
		{
			ReplayStreamer->SetDemoPath(DemoPath);
		}
	}


	FWorldDelegates::LevelRemovedFromWorld.AddRaw(this, &FReplayHelper::OnLevelRemovedFromWorld);
	FWorldDelegates::LevelAddedToWorld.AddRaw(this, &FReplayHelper::OnLevelAddedToWorld);

	return ReplayStreamer;
}

void FReplayHelper::SetPlaybackNetworkVersions(FArchive& Ar)
{
	Ar.SetEngineNetVer(PlaybackDemoHeader.EngineNetworkProtocolVersion);
	Ar.SetGameNetVer(PlaybackDemoHeader.GameNetworkProtocolVersion);
}

void FReplayHelper::SetPlaybackNetworkVersions(UNetConnection* Connection)
{
	if (Connection)
	{
		Connection->EngineNetworkProtocolVersion = PlaybackDemoHeader.EngineNetworkProtocolVersion;
		Connection->GameNetworkProtocolVersion = PlaybackDemoHeader.GameNetworkProtocolVersion;
	}
}

void FReplayHelper::OnStartRecordingComplete(const FStartStreamingResult& Result)
{
	check(Result.bRecording);

	bIsWaitingForStream = false;
	bWasStartRecordingSuccessful = Result.WasSuccessful();

	if (!bWasStartRecordingSuccessful)
	{
		UE_LOG(LogDemo, Warning, TEXT("FReplayRecordHelper::StartRecordingComplete: Failed"));
		OnReplayRecordError.Broadcast();
		return;
	}
}

void FReplayHelper::StartRecording(UNetConnection* Connection)
{
	World = Connection ? Connection->GetWorld() : nullptr;

	bHasLevelStreamingFixes = !!CVarWithLevelStreamingFixes.GetValueOnAnyThread();
	bHasDeltaCheckpoints = !!CVarWithDeltaCheckpoints.GetValueOnAnyThread() && ReplayStreamer->IsCheckpointTypeSupported(EReplayCheckpointType::Delta);
	bHasGameSpecificFrameData = !!CVarWithGameSpecificFrameData.GetValueOnAnyThread();

	const TCHAR* FriendlyNameOption = DemoURL.GetOption(TEXT("DemoFriendlyName="), nullptr);

	bRecordMapChanges = DemoURL.GetOption(TEXT("RecordMapChanges"), nullptr) != nullptr;

	TArray<int32> UserIndices;
	for (FLocalPlayerIterator It(GEngine, World.Get()); It; ++It)
	{
		if (*It)
		{
			UserIndices.Add(It->GetControllerId());
		}
	}

	bIsWaitingForStream = true;

	ActiveReplayName = DemoURL.Map;

	FStartStreamingParameters Params;
	Params.CustomName = DemoURL.Map;
	Params.FriendlyName = FriendlyNameOption != nullptr ? FString(FriendlyNameOption) : World->GetMapName();
	Params.DemoURL = DemoURL.ToString();
	Params.UserIndices = MoveTemp(UserIndices);
	Params.bRecord = true;
	Params.ReplayVersion = FNetworkVersion::GetReplayVersion();

	ReplayStreamer->StartStreaming(Params, FStartStreamingCallback::CreateRaw(this, &FReplayHelper::OnStartRecordingComplete));

	AddNewLevel(GetNameSafe(World->GetOuter()));

	WriteNetworkDemoHeader(Connection);
}

void FReplayHelper::StopReplay()
{
	FNetworkReplayDelegates::OnReplayRecordingComplete.Broadcast(World.Get());

	if (ReplayStreamer.IsValid())
	{
		ReplayStreamer->StopStreaming();
	}

	ActiveReplayName.Empty();
}

void FReplayHelper::WriteNetworkDemoHeader(UNetConnection* Connection)
{
	if (FArchive* FileAr = ReplayStreamer->GetHeaderArchive())
	{
		FNetworkDemoHeader DemoHeader;

		DemoHeader.LevelNamesAndTimes = LevelNamesAndTimes;

		FNetworkReplayDelegates::OnWriteGameSpecificDemoHeader.Broadcast(DemoHeader.GameSpecificData);

		if (UWorld* LocalWorld = World.Get())
		{
			// intentionally not checking for a demo net driver
			if (LocalWorld->GetNetDriver() != nullptr && !LocalWorld->GetNetDriver()->IsServer())
			{
				DemoHeader.HeaderFlags |= EReplayHeaderFlags::ClientRecorded;
			}
		}

		if (HasLevelStreamingFixes())
		{
			DemoHeader.HeaderFlags |= EReplayHeaderFlags::HasStreamingFixes;
		}

		if (HasDeltaCheckpoints())
		{
			DemoHeader.HeaderFlags |= EReplayHeaderFlags::DeltaCheckpoints;
		}

		if (HasGameSpecificFrameData())
		{
			DemoHeader.HeaderFlags |= EReplayHeaderFlags::GameSpecificFrameData;
		}

		if (Cast<UReplayNetConnection>(Connection) != nullptr)
		{
			DemoHeader.HeaderFlags |= EReplayHeaderFlags::ReplayConnection;
		}

		DemoHeader.Guid = FGuid::NewGuid();

		// Write the header
		(*FileAr) << DemoHeader;
		FileAr->Flush();
	}
	else
	{
		UE_LOG(LogDemo, Error, TEXT("WriteNetworkDemoHeader: Header archive is invalid."));
	}
}

void FReplayHelper::OnSeamlessTravelStart(UWorld* InWorld, const FString& LevelName, UNetConnection* Connection)
{
	if (World.Get() == InWorld)
	{
		bPauseRecording = true;

		AddNewLevel(LevelName);

		WriteNetworkDemoHeader(Connection);

		if (ReplayStreamer.IsValid())
		{
			ReplayStreamer->RefreshHeader();
		}
	}
}

APlayerController* FReplayHelper::CreateSpectatorController(UNetConnection* Connection)
{
	// Optionally skip spawning the demo spectator if requested via the URL option
	if (DemoURL.HasOption(TEXT("SkipSpawnSpectatorController")))
	{
		return nullptr;
	}

	check(Connection != nullptr);

	// Get the replay spectator controller class from the default game mode object,
	// since the game mode instance isn't replicated to clients of live games.
	AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	TSubclassOf<AGameModeBase> DefaultGameModeClass = GameState != nullptr ? GameState->GameModeClass : nullptr;

	// If we don't have a game mode class from the world, try to get it from the URL option.
	// This may be true on clients who are recording a replay before the game mode class was replicated to them.
	if (DefaultGameModeClass == nullptr)
	{
		const TCHAR* URLGameModeClass = DemoURL.GetOption(TEXT("game="), nullptr);
		if (URLGameModeClass != nullptr)
		{
			UClass* GameModeFromURL = StaticLoadClass(AGameModeBase::StaticClass(), nullptr, URLGameModeClass);
			DefaultGameModeClass = GameModeFromURL;
		}
	}

	AGameModeBase* DefaultGameMode = DefaultGameModeClass.GetDefaultObject();
	UClass* ReplaySpectatorClass = DefaultGameMode != nullptr ? DefaultGameMode->ReplaySpectatorPlayerControllerClass : nullptr;

	if (ReplaySpectatorClass == nullptr)
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::CreateDemoPlayerController: Failed to load demo spectator class."));
		return nullptr;
	}

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.ObjectFlags |= RF_Transient;	// We never want these to save into a map

	APlayerController* NewDemoController = World->SpawnActor<APlayerController>(ReplaySpectatorClass, SpawnInfo);

	if (NewDemoController == nullptr)
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::CreateDemoPlayerController: Failed to spawn demo spectator."));
		return nullptr;
	}

	// Streaming volumes logic must not be affected by replay spectator camera
	NewDemoController->bIsUsingStreamingVolumes = false;

	if (Connection->GetDriver())
	{
		// Make sure the player controller GetNetDriver returns this driver. Ensures functions that depend on it,
		// such as IsLocalController, work as expected.
		NewDemoController->SetNetDriverName(Connection->GetDriver()->NetDriverName);
	}

	// If the controller doesn't have a player state, we are probably recording on a client.
	// Spawn one manually.
	if (NewDemoController->PlayerState == nullptr && World != nullptr && World->IsRecordingClientReplay())
	{
		NewDemoController->InitPlayerState();
	}

	// Tell the game that we're spectator and not a normal player
	if (NewDemoController->PlayerState)
	{
		NewDemoController->PlayerState->SetIsOnlyASpectator(true);
	}

	for (FActorIterator It(World.Get()); It; ++It)
	{
		if (It->IsA(APlayerStart::StaticClass()))
		{
			NewDemoController->SetInitialLocationAndRotation(It->GetActorLocation(), It->GetActorRotation());
			break;
		}
	}

	NewDemoController->SetReplicates(true);
	NewDemoController->SetAutonomousProxy(true);
	NewDemoController->SetPlayer(Connection);

	return NewDemoController;
}

void FReplayHelper::AddNewLevel(const FString& NewLevelName)
{
	LevelNamesAndTimes.Add(FLevelNameAndTime(UWorld::RemovePIEPrefix(NewLevelName), ReplayStreamer->GetTotalDemoTime()));
}

bool FReplayHelper::ReadPlaybackDemoHeader(FString& Error)
{
	UGameInstance* GameInstance = World->GetGameInstance();

	PlaybackDemoHeader = FNetworkDemoHeader();

	FArchive* FileAr = ReplayStreamer->GetHeaderArchive();
	if (!FileAr)
	{
		Error = FString::Printf(TEXT("Couldn't open demo file %s for reading"), *DemoURL.Map);
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadPlaybackDemoHeader: %s"), *Error);
		GameInstance->HandleDemoPlaybackFailure(EDemoPlayFailure::DemoNotFound, FString(EDemoPlayFailure::ToString(EDemoPlayFailure::DemoNotFound)));
		return false;
	}

	(*FileAr) << PlaybackDemoHeader;

	if (FileAr->IsError())
	{
		Error = FString(TEXT("Demo file is corrupt"));
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadPlaybackDemoHeader: %s"), *Error);
		GameInstance->HandleDemoPlaybackFailure(EDemoPlayFailure::Corrupt, Error);
		return false;
	}

	// Check whether or not we need to process streaming level fixes.
	bHasLevelStreamingFixes = EnumHasAnyFlags(PlaybackDemoHeader.HeaderFlags, EReplayHeaderFlags::HasStreamingFixes);
	// Or delta checkpoints
	bHasDeltaCheckpoints = EnumHasAnyFlags(PlaybackDemoHeader.HeaderFlags, EReplayHeaderFlags::DeltaCheckpoints);

	if (HasDeltaCheckpoints() && !ReplayStreamer->IsCheckpointTypeSupported(EReplayCheckpointType::Delta))
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadPlaybackDemoHeader: Replay has delta checkpoints but streamer does not support them."));
		GameInstance->HandleDemoPlaybackFailure(EDemoPlayFailure::Generic, FString(EDemoPlayFailure::ToString(EDemoPlayFailure::Generic)));
		return false;
	}

	bHasGameSpecificFrameData = EnumHasAnyFlags(PlaybackDemoHeader.HeaderFlags, EReplayHeaderFlags::GameSpecificFrameData);

	FNetworkReplayDelegates::OnProcessGameSpecificDemoHeader.Broadcast(PlaybackDemoHeader.GameSpecificData, Error);
	
	if (!Error.IsEmpty())
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadPlaybackDemoHeader: (Game Specific) %s"), *Error);
		GameInstance->HandleDemoPlaybackFailure(EDemoPlayFailure::GameSpecificHeader, Error);
		return false;
	}

	return true;
}

void FReplayHelper::TickRecording(float DeltaSeconds, UNetConnection* Connection)
{
	if (!bWasStartRecordingSuccessful || bIsWaitingForStream)
	{
		// Nothing to do
		return;
	}

	if (ReplayStreamer->GetLastError() != ENetworkReplayError::None)
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::TickRecording: ReplayStreamer ERROR: %s"), ENetworkReplayError::ToString(ReplayStreamer->GetLastError()));
		OnReplayRecordError.Broadcast();
		return;
	}

	if (bPauseRecording)
	{
		return;
	}

	FArchive* FileAr = ReplayStreamer->GetStreamingArchive();

	if (FileAr == nullptr)
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::TickRecording: FileAr == nullptr"));
		OnReplayRecordError.Broadcast();
		return;
	}

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Net replay record time"), STAT_ReplayRecordTime, STATGROUP_Net);

	CSV_SCOPED_TIMING_STAT(Demo, DemoRecordTime);

	// DeltaSeconds that is padded in, is unclampd and not time dilated
	DemoCurrentTime += GetClampedDeltaSeconds(World.Get(), DeltaSeconds);

	ReplayStreamer->UpdateTotalDemoTime(GetDemoCurrentTimeInMS());

	if (CheckpointSaveContext.CheckpointSaveState != FReplayHelper::ECheckpointSaveState::Idle)
	{
		// If we're in the middle of saving a checkpoint, then update that now and return
		TickCheckpoint(Connection);
	}
	else
	{
		RecordFrame(DeltaSeconds, Connection);

		// Save a checkpoint if it's time
		if (CVarEnableCheckpoints.GetValueOnAnyThread() == 1)
		{
			check(CheckpointSaveContext.CheckpointSaveState == FReplayHelper::ECheckpointSaveState::Idle);		// We early out above, so this shouldn't be possible

			if (ShouldSaveCheckpoint())
			{
				SaveCheckpoint(Connection);
			}
		}
	}
}

void FReplayHelper::FlushNetChecked(UNetConnection& NetConnection)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Replay net flush"), STAT_ReplayFlushNet, STATGROUP_Net);

	NetConnection.FlushNet();
	check(NetConnection.SendBuffer.GetNumBits() == 0);
}

void FReplayHelper::RecordFrame(float DeltaSeconds, UNetConnection* Connection)
{
	FArchive* FileAr = ReplayStreamer->GetStreamingArchive();
	if (FileAr == nullptr)
	{
		return;
	}

	// Mark any new streaming levels, so that they are saved out this frame
	if (!HasLevelStreamingFixes())
	{
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (StreamingLevel == nullptr || !StreamingLevel->ShouldBeLoaded() || StreamingLevel->ShouldBeAlwaysLoaded())
			{
				continue;
			}

			TWeakObjectPtr<UObject> WeakStreamingLevel;
			WeakStreamingLevel = StreamingLevel;
			if (!UniqueStreamingLevels.Contains(WeakStreamingLevel))
			{
				UniqueStreamingLevels.Add(WeakStreamingLevel);
				NewStreamingLevelsThisFrame.Add(WeakStreamingLevel);
			}
		}
	}

	FlushNetChecked(*Connection);

	WriteDemoFrame(Connection, *FileAr, QueuedDemoPackets, DemoCurrentTime, EWriteDemoFrameFlags::None);
}

void FReplayHelper::SaveCheckpoint(UNetConnection* Connection)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SaveCheckpoint time"), STAT_ReplayCheckpointSaveTime, STATGROUP_Net);

	check(ReplayStreamer.IsValid());

	FArchive* CheckpointArchive = ReplayStreamer->GetCheckpointArchive();

	if (CheckpointArchive == nullptr)
	{
		// This doesn't mean error, it means the streamer isn't ready to save checkpoints
		return;
	}

	check(Connection);
	check(CheckpointArchive->TotalSize() == 0);
	check(Connection->SendBuffer.GetNumBits() == 0);
	check(CheckpointSaveContext.CheckpointSaveState == ECheckpointSaveState::Idle);

	UNetDriver* Driver = Connection->GetDriver();
	check(Driver);

	const FNetworkObjectList& NetworkObjectList = Driver->GetNetworkObjectList();

	const bool bDeltaCheckpoint = HasDeltaCheckpoints();

	CSV_SCOPED_TIMING_STAT(Demo, DemoSaveCheckpointTime);

	const FActorChannelMap& ActorChannelMap = Connection->ActorChannelMap();

	if (HasLevelStreamingFixes())
	{
		SCOPED_NAMED_EVENT(FReplayHelper_ReplayLevelSortAndAssign, FColor::Purple);

		struct FStrippedActorInfo
		{
			TWeakObjectPtr<AActor> Actor;
			const UObject* Level;
		};

		const FNetworkObjectList::FNetworkObjectSet& AllObjectsSet = NetworkObjectList.GetAllObjects();

		TArray<FStrippedActorInfo> ActorArray;
		ActorArray.Reserve(ActorChannelMap.Num() + NetworkObjectList.GetNumDormantActorsForConnection(Connection));

		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Replay actor level sorting time."), STAT_ReplayLevelSorting, STATGROUP_Net);

			for (const TSharedPtr<FNetworkObjectInfo>& NetworkObjectInfo : AllObjectsSet)
			{
				if (NetworkObjectInfo.IsValid())
				{
					if (!bDeltaCheckpoint || NetworkObjectInfo->bDirtyForReplay)
					{
						AActor* Actor = NetworkObjectInfo->Actor;

						// check to see if it should replicate at all
						bool bCheckpointActor = Actor && !Actor->IsPendingKill() && ((Actor->GetRemoteRole() != ROLE_None || Actor->GetTearOff()) && (Actor == Connection->PlayerController || Cast<APlayerController>(Actor) == nullptr));
						
						// now look for an open channel
						bCheckpointActor = bCheckpointActor && ActorChannelMap.Contains(NetworkObjectInfo->Actor);

						if (!bCheckpointActor)
						{
							// has it gone dormant?
							bCheckpointActor = (NetworkObjectInfo->Actor->NetDormancy != DORM_Initial) &&  (NetworkObjectInfo->DormantConnections.Contains(Connection) || NetworkObjectInfo->RecentlyDormantConnections.Contains(Connection));
						}
						
						if (bCheckpointActor)
						{
							ActorArray.Add({ NetworkObjectInfo->Actor, NetworkObjectInfo->Actor->GetOuter() });

							NetworkObjectInfo->bDirtyForReplay = false;
						}
					}
				}
			}

			// Sort by level			
			ActorArray.Sort([](const FStrippedActorInfo& A, const FStrippedActorInfo& B) { return (B.Level < A.Level); });
		}

		CheckpointSaveContext.PendingCheckpointActors.Reserve(ActorArray.Num());

		uint32 LevelIt = 0;
		for (int32 CurrentIt = 0, EndIt = ActorArray.Num(); CurrentIt != EndIt; ++LevelIt)
		{
			const UObject* CurrentLevelToIndex = ActorArray[CurrentIt].Level;
			const FLevelStatus& LevelStatus = FindOrAddLevelStatus(*Cast<const ULevel>(CurrentLevelToIndex));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			// Validate that we get the correct level
			check(Cast<const ULevel>(CurrentLevelToIndex) == ActorArray[CurrentIt].Actor->GetLevel());
#endif
			while (CurrentIt < EndIt && (CurrentLevelToIndex == ActorArray[CurrentIt].Level))
			{
				CheckpointSaveContext.PendingCheckpointActors.Add({ ActorArray[CurrentIt].Actor, LevelStatus.LevelIndex });
				++CurrentIt;
			};
		}
	}
	else
	{
		// Add any actor with a valid channel to the PendingCheckpointActors list
		for (const TSharedPtr<FNetworkObjectInfo>& ObjectInfo : NetworkObjectList.GetAllObjects())
		{
			AActor* Actor = ObjectInfo.Get()->Actor;

			if (!bDeltaCheckpoint || ObjectInfo->bDirtyForReplay)
			{
				bool bCheckpointActor = ActorChannelMap.Contains(Actor);
				if (!bCheckpointActor)
				{
					bCheckpointActor = (Actor->NetDormancy != DORM_Initial) && (ObjectInfo->DormantConnections.Contains(Connection) || ObjectInfo->RecentlyDormantConnections.Contains(Connection));
				}

				if (bCheckpointActor)
				{
					CheckpointSaveContext.PendingCheckpointActors.Add({ Actor, -1 });

					ObjectInfo->bDirtyForReplay = false;
				}
			}
		}
	}

	if (CheckpointSaveContext.PendingCheckpointActors.Num() == 0)
	{
		return;
	}

	UPackageMapClient* PackageMapClient = Cast<UPackageMapClient>(Connection->PackageMap);

	PackageMapClient->SavePackageMapExportAckStatus(CheckpointSaveContext.CheckpointAckState);

	Connection->SetIgnoreReservedChannels(true);
	Connection->SetReserveDestroyedChannels(true);

	// We are now processing checkpoint actors	
	CheckpointSaveContext.CheckpointSaveState = ECheckpointSaveState::ProcessCheckpointActors;
	CheckpointSaveContext.TotalCheckpointSaveTimeSeconds = 0;
	CheckpointSaveContext.TotalCheckpointReplicationTimeSeconds = 0;
	CheckpointSaveContext.TotalCheckpointSaveFrames = 0;
	CheckpointSaveContext.TotalCheckpointActors = CheckpointSaveContext.PendingCheckpointActors.Num();

	LastCheckpointTime = DemoCurrentTime;

	if (bDeltaCheckpoint)
	{
		CheckpointSaveContext.DeltaCheckpointData = MoveTemp(RecordingDeltaCheckpointData);
	}
	else
	{
		CheckpointSaveContext.NameTableMap.Empty();
	}

	UE_LOG(LogDemo, Log, TEXT("Starting checkpoint. Networked Actors: %i"), NetworkObjectList.GetAllObjects().Num());

	// Do the first checkpoint tick now if we're not amortizing
	if (GetCheckpointSaveMaxMSPerFrame() <= 0.0f)
	{
		TickCheckpoint(Connection);
	}
}

// Only start execution if a certain percentage remains of the 
static bool inline ShouldExecuteState(const FRepActorsCheckpointParams& Params, double CurrentTime, double RequiredRatioToStart)
{
	const double CheckpointMaxUploadTimePerFrame = Params.CheckpointMaxUploadTimePerFrame;
	if (CheckpointMaxUploadTimePerFrame <= 0.0)
	{
		return true;
	}

	return (1.0 - ((CurrentTime - Params.StartCheckpointTime) / Params.CheckpointMaxUploadTimePerFrame)) > RequiredRatioToStart;
}

void FReplayHelper::TickCheckpoint(UNetConnection* Connection)
{
	CSV_SCOPED_TIMING_STAT(Demo, DemoRecordCheckpointTime);
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SaveCheckpoint time"), STAT_ReplayCheckpointSaveTime, STATGROUP_Net);

	check(Connection);

	if (CheckpointSaveContext.CheckpointSaveState == ECheckpointSaveState::Idle)
	{
		return;
	}

	FArchive* CheckpointArchive = ReplayStreamer->GetCheckpointArchive();

	if (!ensure(CheckpointArchive != nullptr))
	{
		return;
	}

	CSV_SCOPED_TIMING_STAT(Demo, DemoTickCheckpointTime);

	FRepActorsCheckpointParams Params
	{
		FPlatformTime::Seconds(),
		(double)GetCheckpointSaveMaxMSPerFrame() / 1000
	};

	bool bExecuteNextState = true;
	double CurrentTime = Params.StartCheckpointTime;

	{
		FScopedForceUnicodeInArchive ScopedUnicodeSerialization(*CheckpointArchive);

		CheckpointSaveContext.TotalCheckpointSaveFrames++;

		FReplayHelper::FlushNetChecked(*Connection);

		UPackageMapClient* PackageMapClient = Cast<UPackageMapClient>(Connection->PackageMap);

		// Save package map ack status in case we export stuff during the checkpoint (so we can restore the connection back to what it was before we saved the checkpoint)
		PackageMapClient->OverridePackageMapExportAckStatus(&CheckpointSaveContext.CheckpointAckState);

		const bool bDeltaCheckpoint = HasDeltaCheckpoints();

		while (bExecuteNextState && (CheckpointSaveContext.CheckpointSaveState != ECheckpointSaveState::Finalize) && !(Params.CheckpointMaxUploadTimePerFrame > 0 && CurrentTime - Params.StartCheckpointTime > Params.CheckpointMaxUploadTimePerFrame))
		{
			switch (CheckpointSaveContext.CheckpointSaveState)
			{
			case ECheckpointSaveState::ProcessCheckpointActors:
			{
				SCOPED_NAMED_EVENT(FReplayHelper_ProcessCheckpointActors, FColor::Green);

				Connection->SetReserveDestroyedChannels(false);

				// Save the replicated server time so we can restore it after the checkpoint has been serialized.
				// This preserves the existing behavior and prevents clients from receiving updated server time
				// more often than the normal update rate.
				AGameStateBase* const GameState = World != nullptr ? World->GetGameState() : nullptr;

				const float SavedReplicatedServerTimeSeconds = GameState ? GameState->ReplicatedWorldTimeSeconds : -1.0f;

				// Normally AGameStateBase::ReplicatedWorldTimeSeconds is only updated periodically,
				// but we want to make sure it's accurate for the checkpoint.
				if (GameState)
				{
					GameState->UpdateServerTimeSeconds();
				}

				{
					// Re-use the existing connection to record all properties that have changed since channels were first opened
					TGuardValue<EResendAllDataState> ResendAllData(Connection->ResendAllDataState, bDeltaCheckpoint ? EResendAllDataState::SinceCheckpoint : EResendAllDataState::SinceOpen);

					bool bContinue = true;

					int32 NumActorsToReplicate = CheckpointSaveContext.PendingCheckpointActors.Num();

					do
					{
						const FPendingCheckPointActor Current = CheckpointSaveContext.PendingCheckpointActors.Pop();

						AActor* Actor = Current.Actor.Get();

						bContinue = ReplicateCheckpointActor(Actor, Connection, Params);
					} 
					while (--NumActorsToReplicate && bContinue);

					if (GameState)
					{
						// Restore the game state's replicated world time
						GameState->ReplicatedWorldTimeSeconds = SavedReplicatedServerTimeSeconds;
					}

					FlushNetChecked(*Connection);

					PackageMapClient->OverridePackageMapExportAckStatus(nullptr);
				}

				Connection->SetReserveDestroyedChannels(true);

				// We are done processing for this frame so  store the TotalCheckpointSave time here to be true to the old behavior which did not account for the	actual saving time of the check point
				CheckpointSaveContext.TotalCheckpointReplicationTimeSeconds += (FPlatformTime::Seconds() - Params.StartCheckpointTime);

				// if we have replicated all checkpointactors, move on to the next state
				if (CheckpointSaveContext.PendingCheckpointActors.Num() == 0)
				{
					CheckpointSaveContext.CheckpointSaveState = ECheckpointSaveState::SerializeDeletedStartupActors;

					Connection->SetReserveDestroyedChannels(false);
					Connection->SetIgnoreReservedChannels(false);
				}
			}
			break;

			case ECheckpointSaveState::SerializeDeletedStartupActors:
			{
				// Postpone execution of this state if we have used to much of our alloted time, this value can be tweaked based on profiling
				const double RequiredRatioFor_SerializeDeletedStartupActors = 0.6;
				if ((bExecuteNextState = ShouldExecuteState(Params, CurrentTime, RequiredRatioFor_SerializeDeletedStartupActors)) == true)
				{
					SCOPED_NAMED_EVENT(FReplayHelper_SerializeDeletedStartupActors, FColor::Green);

					//
					// We're done saving this checkpoint, now we need to write out all data for it.
					//

					CheckpointSaveContext.bWriteCheckpointOffset = HasLevelStreamingFixes();
					if (HasLevelStreamingFixes())
					{
						CheckpointSaveContext.CheckpointOffset = CheckpointArchive->Tell();
						// We will rewrite this offset when we are done saving the checkpoint
						*CheckpointArchive << CheckpointSaveContext.CheckpointOffset;
					}

					*CheckpointArchive << CurrentLevelIndex;

					// Save deleted startup actors
					if (bDeltaCheckpoint)
					{
						WriteDeletedStartupActors(Connection, *CheckpointArchive, CheckpointSaveContext.DeltaCheckpointData.DestroyedNetStartupActors);
						CheckpointSaveContext.DeltaCheckpointData.DestroyedNetStartupActors.Empty();

						*CheckpointArchive << CheckpointSaveContext.DeltaCheckpointData.DestroyedDynamicActors;
						CheckpointSaveContext.DeltaCheckpointData.DestroyedDynamicActors.Empty();

						*CheckpointArchive << CheckpointSaveContext.DeltaCheckpointData.ChannelsToClose;
						CheckpointSaveContext.DeltaCheckpointData.ChannelsToClose.Empty();
					}
					else
					{
						WriteDeletedStartupActors(Connection, *CheckpointArchive, DeletedNetStartupActors);
					}

					CheckpointSaveContext.CheckpointSaveState = ECheckpointSaveState::CacheNetGuids;
				}
			}
			break;

			case ECheckpointSaveState::CacheNetGuids:
			{
				// Postpone execution of this state if we have used too much of our alloted time, this value can be tweaked based on profiling
				const double RequiredRatioFor_CacheNetGuids = 0.8;
				if ((bExecuteNextState = ShouldExecuteState(Params, CurrentTime, RequiredRatioFor_CacheNetGuids)) == true)
				{
					SCOPED_NAMED_EVENT(FReplayHelper_CacheNetGuids, FColor::Green);

					CacheNetGuids(Connection);
					CheckpointSaveContext.CheckpointSaveState = ECheckpointSaveState::SerializeGuidCache;
				}
			}
			break;

			case ECheckpointSaveState::SerializeGuidCache:
			{
				SCOPED_NAMED_EVENT(FReplayHelper_SerializeGuidCache, FColor::Green);

				// Save the current guid cache
				bExecuteNextState = SerializeGuidCache(Connection, Params, CheckpointArchive);
				if (bExecuteNextState)
				{
					CheckpointSaveContext.CheckpointSaveState = ECheckpointSaveState::SerializeNetFieldExportGroupMap;
				}
			}
			break;

			case ECheckpointSaveState::SerializeNetFieldExportGroupMap:
			{
				// Postpone execution of this state if we have used to much of our alloted time, this value can be tweaked based on profiling
				const double RequiredRatioFor_SerializeNetFieldExportGroupMap = 0.6;
				if ((bExecuteNextState = ShouldExecuteState(Params, CurrentTime, RequiredRatioFor_SerializeNetFieldExportGroupMap)) == true)
				{
					SCOPED_NAMED_EVENT(FReplayHelper_SerializeNetFieldExportGroupMap, FColor::Green);

					// Save the compatible rep layout map
					if (bDeltaCheckpoint)
					{
						PackageMapClient->SerializeNetFieldExportDelta(*CheckpointArchive);
					}
					else
					{
						PackageMapClient->SerializeNetFieldExportGroupMap(*CheckpointArchive);
					}

					CheckpointSaveContext.CheckpointSaveState = ECheckpointSaveState::SerializeDemoFrameFromQueuedDemoPackets;
				}
			}
			break;

			case ECheckpointSaveState::SerializeDemoFrameFromQueuedDemoPackets:
			{
				// Postpone execution of this state if we have used to much of our alloted time, this value can be tweaked based on profiling
				const double RequiredRatioFor_SerializeDemoFrameFromQueuedDemoPackets = 0.8;
				if ((bExecuteNextState = ShouldExecuteState(Params, CurrentTime, RequiredRatioFor_SerializeDemoFrameFromQueuedDemoPackets)) == true)
				{
					SCOPED_NAMED_EVENT(FReplayHelper_SerializeDemoFrameFromQueuedDemoPackets, FColor::Green);

					// Write offset
					if (CheckpointSaveContext.bWriteCheckpointOffset)
					{
						const FArchivePos CurrentPosition = CheckpointArchive->Tell();
						FArchivePos Offset = CurrentPosition - (CheckpointSaveContext.CheckpointOffset + sizeof(FArchivePos));
						CheckpointArchive->Seek(CheckpointSaveContext.CheckpointOffset);
						*CheckpointArchive << Offset;
						CheckpointArchive->Seek(CurrentPosition);
					}

					// Get the size of the guid data saved
					CheckpointSaveContext.GuidCacheSize = CheckpointArchive->TotalSize();

					// This will cause the entire name list to be written out again.
					// Note, WriteDemoFrame will set this to 0 so we guard the value.
					// This is because when checkpoint amortization is enabled, it's possible for new levels to stream
					// in while recording a checkpoint, and we want to make sure those get written out to the normal
					// streaming archive next frame.
					TGuardValue<uint32> NumLevelsAddedThisFrameGuard(NumLevelsAddedThisFrame, AllLevelStatuses.Num());

					// Write out all of the queued up packets generated while saving the checkpoint
					WriteDemoFrame(Connection, *CheckpointArchive, QueuedCheckpointPackets, static_cast<float>(LastCheckpointTime), EWriteDemoFrameFlags::SkipGameSpecific);

					CheckpointSaveContext.CheckpointSaveState = ECheckpointSaveState::Finalize;
				}
			}
			break;

			default:
				break;
			}

			CurrentTime = FPlatformTime::Seconds();
		}
	}

	// accumulate time spent over all checkpoint ticks
	CheckpointSaveContext.TotalCheckpointSaveTimeSeconds += (CurrentTime - Params.StartCheckpointTime);

	if (CheckpointSaveContext.CheckpointSaveState == ECheckpointSaveState::Finalize)
	{
		SCOPED_NAMED_EVENT(FReplayHelper_Finalize, FColor::Green);

		// Get the total checkpoint size
		const int32 TotalCheckpointSize = CheckpointArchive->TotalSize();

		if (CheckpointArchive->TotalSize() > 0)
		{
			ReplayStreamer->FlushCheckpoint(GetLastCheckpointTimeInMS());
		}

		const float TotalCheckpointTimeInMS = CheckpointSaveContext.TotalCheckpointReplicationTimeSeconds * 1000.0f;
		const float TotalCheckpointTimeWithOverheadInMS = CheckpointSaveContext.TotalCheckpointSaveTimeSeconds * 1000.0f;

		UE_LOG(LogDemo, Log, TEXT("Finished checkpoint. Checkpoint Actors: %i, GuidCacheSize: %i, TotalSize: %i, TotalCheckpointSaveFrames: %i, TotalCheckpointTimeInMS: %2.2f, TotalCheckpointTimeWithOverheadInMS: %2.2f"), CheckpointSaveContext.TotalCheckpointActors, CheckpointSaveContext.GuidCacheSize, TotalCheckpointSize, CheckpointSaveContext.TotalCheckpointSaveFrames, TotalCheckpointTimeInMS, TotalCheckpointTimeWithOverheadInMS);

		// we are done, out
		CheckpointSaveContext.CheckpointSaveState = ECheckpointSaveState::Idle;
	}
}

// Checkpoint saving step.
// Serialize as many net guids as fit into a single frame (if time boxed) from previously made snapshot
bool FReplayHelper::SerializeGuidCache(UNetConnection* Connection, const FRepActorsCheckpointParams& Params, FArchive* CheckpointArchive)
{
	check(Connection);

	if (CheckpointSaveContext.NextNetGuidForRecording == 0) // is the first iteration?
	{
		CheckpointSaveContext.NetGuidsCountPos = CheckpointArchive->Tell();
		*CheckpointArchive << CheckpointSaveContext.NextNetGuidForRecording;
	}

	const double StartTime = FPlatformTime::Seconds();
	const double Deadline = Params.StartCheckpointTime + Params.CheckpointMaxUploadTimePerFrame;

	check(CheckpointSaveContext.NetGuidCacheSnapshot.Num() == 0 || CheckpointSaveContext.NetGuidCacheSnapshot.IsValidIndex(CheckpointSaveContext.NextNetGuidForRecording));

	for (; CheckpointSaveContext.NextNetGuidForRecording != CheckpointSaveContext.NetGuidCacheSnapshot.Num(); ++CheckpointSaveContext.NextNetGuidForRecording)
	{
		FNetworkGUID& NetworkGUID = CheckpointSaveContext.NetGuidCacheSnapshot[CheckpointSaveContext.NextNetGuidForRecording].NetGuid;
		FNetGuidCacheObject& CacheObject = CheckpointSaveContext.NetGuidCacheSnapshot[CheckpointSaveContext.NextNetGuidForRecording].NetGuidCacheObject;

		const UObject* Object = CacheObject.Object.Get();

		if (Object && (NetworkGUID.IsStatic() || Object->IsNameStableForNetworking()))
		{
			*CheckpointArchive << NetworkGUID;
			*CheckpointArchive << CacheObject.OuterGUID;

			uint32* NametableIndex = CheckpointSaveContext.NameTableMap.Find(CacheObject.PathName);
			if (NametableIndex == nullptr)
			{
				uint8 bExported = 1;
				*CheckpointArchive << bExported;

				FString PathName = CacheObject.PathName.ToString();
				GEngine->NetworkRemapPath(Connection, PathName, false);

				*CheckpointArchive << PathName;

				uint32 TableIndex = CheckpointSaveContext.NameTableMap.Num();
				
				CheckpointSaveContext.NameTableMap.Add(CacheObject.PathName, TableIndex);
			}
			else
			{
				uint8 bExported = 0;
				*CheckpointArchive << bExported;

				uint32 TableIndex = *NametableIndex;

				CheckpointArchive->SerializeIntPacked(TableIndex);
			}

			uint8 Flags = 0;
			Flags |= CacheObject.bNoLoad ? (1 << 0) : 0;
			Flags |= CacheObject.bIgnoreWhenMissing ? (1 << 1) : 0;

			*CheckpointArchive << Flags;

			++CheckpointSaveContext.NumNetGuidsForRecording;
		}

		if (Params.CheckpointMaxUploadTimePerFrame > 0 && (FPlatformTime::Seconds() >= Deadline))
		{
			break;
		}
	}

	const bool bCompleted = CheckpointSaveContext.NextNetGuidForRecording == CheckpointSaveContext.NetGuidCacheSnapshot.Num();
	if (bCompleted)
	{
		FArchivePos Pos = CheckpointArchive->Tell();
		CheckpointArchive->Seek(CheckpointSaveContext.NetGuidsCountPos);
		*CheckpointArchive << CheckpointSaveContext.NumNetGuidsForRecording;
		CheckpointArchive->Seek(Pos);
	}

	UE_LOG(LogDemo, Log, TEXT("Checkpoint. SerializeGuidCache: %i/%i (total %i), took %.3f (%.3f)"), CheckpointSaveContext.NextNetGuidForRecording, CheckpointSaveContext.NetGuidCacheSnapshot.Num(), CheckpointSaveContext.NumNetGuidsForRecording, FPlatformTime::Seconds() - Params.StartCheckpointTime, FPlatformTime::Seconds() - StartTime);

	return bCompleted;
}

void FReplayHelper::ResetLevelStatuses()
{
	ClearLevelStreamingState();

	// There are times (e.g., during travel) when we may not have a valid level.
	// This **should never** be called during those times.
	check(World.Get());

	// ResetLevelStatuses should only ever be called before receiving *any* data from the Replay stream,
	// immediately before processing checkpoint data, or after a level transition (in which case no data
	// will be relevant to the new sublevels).
	// In any case, we can just flag these sublevels as ready immediately.
	FindOrAddLevelStatus(*(World->PersistentLevel)).bIsReady = true;

	for (ULevelStreaming* LevelStreaming : World->GetStreamingLevels())
	{
		if (LevelStreaming && LevelStreaming->IsLevelVisible())
		{
			FindOrAddLevelStatus(*LevelStreaming->GetLoadedLevel()).bIsReady = true;
		}
	}
}

void FReplayHelper::WriteDemoFrame(UNetConnection* Connection, FArchive& Ar, TArray<FQueuedDemoPacket>& QueuedPackets, float FrameTime, EWriteDemoFrameFlags Flags)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Replay write frame time"), STAT_ReplayWriteDemoFrame, STATGROUP_Net);

	check(Connection);

	Ar << CurrentLevelIndex;

	// Save total absolute demo time in seconds
	Ar << FrameTime;

	Cast<UPackageMapClient>(Connection->PackageMap)->AppendExportData(Ar);

	if (HasLevelStreamingFixes())
	{
		uint32 NumStreamingLevels = AllLevelStatuses.Num();
		Ar.SerializeIntPacked(NumLevelsAddedThisFrame);

		for (uint32 i = NumStreamingLevels - NumLevelsAddedThisFrame; i < NumStreamingLevels; i++)
		{
			Ar << AllLevelStatuses[i].LevelName;
		}

		NumLevelsAddedThisFrame = 0;
	}
	else
	{
		// Save any new streaming levels
		uint32 NumStreamingLevels = NewStreamingLevelsThisFrame.Num();
		Ar.SerializeIntPacked(NumStreamingLevels);

		for (uint32 i = 0; i < NumStreamingLevels; i++)
		{
			ULevelStreaming* StreamingLevel = World->GetStreamingLevels()[i];

			// TODO: StreamingLevel could be null, but since we've already written out the integer count, skipping entries could cause an issue, so leaving as is for now
			FString PackageName = StreamingLevel->GetWorldAssetPackageName();
			FString PackageNameToLoad = StreamingLevel->PackageNameToLoad.ToString();

			Ar << PackageName;
			Ar << PackageNameToLoad;
			Ar << StreamingLevel->LevelTransform;

			UE_LOG(LogDemo, Log, TEXT("WriteDemoFrame: StreamingLevel: %s, %s"), *PackageName, *PackageNameToLoad);
		}

		NewStreamingLevelsThisFrame.Empty();
	}

	{
		TUniquePtr<FScopedStoreArchiveOffset> ScopedOffset(HasLevelStreamingFixes() ? new FScopedStoreArchiveOffset(Ar) : nullptr);

		// Save external data
		SaveExternalData(Connection, Ar);
	}

	if (HasGameSpecificFrameData())
	{
		FScopedStoreArchiveOffset ScopedOffset(Ar);

		if (!EnumHasAnyFlags(Flags, EWriteDemoFrameFlags::SkipGameSpecific))
		{
			FDemoFrameDataMap Data;
			FNetworkReplayDelegates::OnWriteGameSpecificFrameData.Broadcast(World.Get(), FrameTime, Data);

			Ar << Data;
		}
	}

	for (FQueuedDemoPacket& DemoPacket : QueuedPackets)
	{
		if (HasLevelStreamingFixes())
		{
			ensureAlways(DemoPacket.SeenLevelIndex);
			Ar.SerializeIntPacked(DemoPacket.SeenLevelIndex);
		}

		WritePacket(Ar, DemoPacket.Data.GetData(), DemoPacket.Data.Num());
	}

	QueuedPackets.Empty();

	if (HasLevelStreamingFixes())
	{
		uint32 EndCountUnsigned = 0;
		Ar.SerializeIntPacked(EndCountUnsigned);
	}

	// Write a count of 0 to signal the end of the frame
	int32 EndCount = 0;
	Ar << EndCount;
}

void FReplayHelper::WritePacket(FArchive& Ar, uint8* Data, int32 Count)
{
	Ar << Count;
	Ar.Serialize(Data, Count);
}

void FReplayHelper::SaveExternalData(UNetConnection* Connection, FArchive& Ar)
{
	check(Connection && Connection->Driver);

	SCOPED_NAMED_EVENT(FReplayHelper_SaveExternalData, FColor::Blue);
	for (auto It = ObjectsWithExternalData.CreateIterator(); It; ++It)
	{
		FReplayExternalOutData& Element = *It;
		if (UObject* Object = Element.Object.Get())
		{
			FRepChangedPropertyTracker* PropertyTracker = Connection->Driver->RepChangedPropertyTrackerMap.FindChecked(Object).Get();

			uint32 ExternalDataNumBits = PropertyTracker->ExternalDataNumBits;
			if (ExternalDataNumBits > 0)
			{
				// Save payload size (in bits)
				Ar.SerializeIntPacked(ExternalDataNumBits);

				// Save GUID
				Ar << Element.GUID;

				// Save payload
				Ar.Serialize(PropertyTracker->ExternalData.GetData(), PropertyTracker->ExternalData.Num());

				PropertyTracker->ExternalData.Empty();
				PropertyTracker->ExternalDataNumBits = 0;
			}
		}
	}

	// Reset external out datas
	ObjectsWithExternalData.Reset();

	uint32 StopCount = 0;
	Ar.SerializeIntPacked(StopCount);
}

FString FReplayHelper::GetLevelPackageName(const ULevel& InLevel)
{
	FString PathName = InLevel.GetOutermost()->GetFName().ToString();
	return UWorld::RemovePIEPrefix(PathName);
}

float FReplayHelper::GetClampedDeltaSeconds(UWorld* World, const float DeltaSeconds)
{
	check(World != nullptr);

	const float RealDeltaSeconds = DeltaSeconds;

	// Clamp delta seconds
	AWorldSettings* WorldSettings = World->GetWorldSettings();
	const float ClampedDeltaSeconds = WorldSettings->FixupDeltaSeconds(DeltaSeconds * WorldSettings->GetEffectiveTimeDilation(), RealDeltaSeconds);
	check(ClampedDeltaSeconds >= 0.0f);

	return ClampedDeltaSeconds;
}

void FReplayHelper::CacheNetGuids(UNetConnection* Connection)
{
	if (Connection && Connection->Driver)
	{
		int32 NumValues = 0;
		const bool bDeltaCheckpoint = HasDeltaCheckpoints();
		const double StartTime = FPlatformTime::Seconds();

		// initialize NetGuidCache serialization
		CheckpointSaveContext.NetGuidCacheSnapshot.Reset();
		CheckpointSaveContext.NextNetGuidForRecording = 0;
		CheckpointSaveContext.NumNetGuidsForRecording = 0;

		for (auto It = Connection->Driver->GuidCache->ObjectLookup.CreateIterator(); It; ++It)
		{
			FNetworkGUID& NetworkGUID = It.Key();
			FNetGuidCacheObject& CacheObject = It.Value();

			if (bDeltaCheckpoint && !CacheObject.bDirtyForReplay)
			{
				continue;
			}

			// Do not add guids we would filter out in the serialize step
			if (NetworkGUID.IsValid() && CacheObject.Object.Get() && (NetworkGUID.IsStatic() || CacheObject.Object->IsNameStableForNetworking()))
			{
				CheckpointSaveContext.NetGuidCacheSnapshot.Add({ NetworkGUID, CacheObject });

				CacheObject.bDirtyForReplay = false;

				++NumValues;
			}
		}

		UE_LOG(LogDemo, Verbose, TEXT("CacheNetGuids: %d, %.1f ms"), NumValues, (FPlatformTime::Seconds() - StartTime) * 1000);
	}
}

bool FReplayHelper::ReplicateCheckpointActor(AActor* ToReplicate, UNetConnection* Connection, class FRepActorsCheckpointParams& Params)
{
	// Early out if the actor has been destroyed or the world is streamed out.
	if (ToReplicate == nullptr || ToReplicate->GetWorld() == nullptr)
	{
		return true;
	}

	bool bOpenedChannelForDormancy = false;

	UActorChannel* ActorChannel = Connection->FindActorChannelRef(ToReplicate);

	if (ActorChannel == nullptr && ToReplicate->NetDormancy > DORM_Awake)
	{
		// Create a new channel for this actor.
		ActorChannel = Cast<UActorChannel>(Connection->CreateChannelByName(NAME_Actor, EChannelCreateFlags::OpenedLocally));
		if (ActorChannel != nullptr)
		{
			ActorChannel->SetChannelActor(ToReplicate, ESetChannelActorFlags::SkipMarkActive);

			bOpenedChannelForDormancy = true;
		}
	}

	if (ActorChannel)
	{
		const bool bReplicated = ReplicateActor(ToReplicate, Connection, true);

		if (bOpenedChannelForDormancy)
		{
			ActorChannel->bPendingDormancy = false;
			ActorChannel->bIsInDormancyHysteresis = false;
			ActorChannel->Dormant = true;

			if (bReplicated)
			{
				ActorChannel->Close(EChannelCloseReason::Dormancy);
			}

			ActorChannel->ConditionalCleanUp(false, EChannelCloseReason::Dormancy);
		}

		UpdateExternalDataForActor(Connection, ToReplicate);

		const double CheckpointTime = FPlatformTime::Seconds();

		if (Params.CheckpointMaxUploadTimePerFrame > 0 && CheckpointTime - Params.StartCheckpointTime > Params.CheckpointMaxUploadTimePerFrame)
		{
			return false;
		}
	}

	return true;
}

void FReplayHelper::LoadExternalData(FArchive& Ar, const float TimeSeconds)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Demo_LoadExternalData"), Demo_LoadExternalData, STATGROUP_Net);

	while (true)
	{
		uint32 ExternalDataNumBits;

		// Read payload into payload/guid map
		Ar.SerializeIntPacked(ExternalDataNumBits);

		if (ExternalDataNumBits == 0)
		{
			return;
		}

		FNetworkGUID NetGUID;

		// Read net guid this payload belongs to
		Ar << NetGUID;

		int32 ExternalDataNumBytes = (ExternalDataNumBits + 7) >> 3;

		FBitReader Reader(nullptr, ExternalDataNumBits);

		Ar.Serialize(Reader.GetData(), ExternalDataNumBytes);

		SetPlaybackNetworkVersions(Reader);

		FReplayExternalDataArray& ExternalDataArray = ExternalDataToObjectMap.FindOrAdd(NetGUID);

		ExternalDataArray.Add(new FReplayExternalData(MoveTemp(Reader), TimeSeconds));
	}
}

bool FReplayHelper::UpdateExternalDataForActor(UNetConnection* Connection, AActor* Actor)
{
	check(Connection && Connection->Driver);

	UNetDriver::FRepChangedPropertyTrackerWrapper* PropertyTrackerWrapper = Connection->Driver->RepChangedPropertyTrackerMap.Find(Actor);
	if (PropertyTrackerWrapper == nullptr)
	{
		return false;
	}

	FRepChangedPropertyTracker* PropertyTracker = PropertyTrackerWrapper->Get();

	if (PropertyTracker->ExternalData.Num() == 0)
	{
		return false;
	}

	if (FNetworkGUID* NetworkGUID = Connection->Driver->GuidCache->NetGUIDLookup.Find(Actor))
	{
		ObjectsWithExternalData.Add({ Actor, *NetworkGUID });

		return true;
	}
	else
	{
		// Clear external data if the actor has never replicated yet (and doesn't have a net guid)
		PropertyTracker->ExternalData.Reset();
		PropertyTracker->ExternalDataNumBits = 0;

		return false;
	}
}

void FReplayHelper::Serialize(FArchive& Ar)
{
	GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Ar, "FReplayHelper::Serialize");

	if (Ar.IsCountingMemory())
	{
		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("UniqueStreamingLevels", UniqueStreamingLevels.CountBytes(Ar));
		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("NewStreamingLevelsThisFrame", NewStreamingLevelsThisFrame.CountBytes(Ar));

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("PlaybackDemoHeader", PlaybackDemoHeader.CountBytes(Ar));

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("LevelNamesAndTimes",
			LevelNamesAndTimes.CountBytes(Ar);
			for (const FLevelNameAndTime& LevelNameAndTime : LevelNamesAndTimes)
			{
				LevelNameAndTime.CountBytes(Ar);
			}
		);

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("AllLevelStatuses",
			AllLevelStatuses.CountBytes(Ar);
			for (const FLevelStatus& LevelStatus : AllLevelStatuses)
			{
				LevelStatus.CountBytes(Ar);
			}
		);

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("LevelStatusesByName",
			LevelStatusesByName.CountBytes(Ar);
			for (const auto& LevelStatusNamePair : LevelStatusesByName)
			{
				LevelStatusNamePair.Key.CountBytes(Ar);
			}
		);

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("LevelStatusIndexByLevel", LevelStatusIndexByLevel.CountBytes(Ar));
		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("SeenLevelStatuses", SeenLevelStatuses.CountBytes(Ar));
		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("LevelsPendingFastForward", LevelsPendingFastForward.CountBytes(Ar));
		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("ObjectsWithExternalData", ObjectsWithExternalData.CountBytes(Ar));
		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("CheckpointSaveContext", CheckpointSaveContext.CountBytes(Ar));

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("QueuedDemoPackets",
			QueuedDemoPackets.CountBytes(Ar);
			for (const FQueuedDemoPacket& QueuedPacket : QueuedDemoPackets)
			{
				QueuedPacket.CountBytes(Ar);
			}
		);

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("QueuedCheckpointPackets",
			QueuedCheckpointPackets.CountBytes(Ar);
			for (const FQueuedDemoPacket& QueuedPacket : QueuedCheckpointPackets)
			{
				QueuedPacket.CountBytes(Ar);
			}
		);

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("ExternalDataToObjectMap",
			ExternalDataToObjectMap.CountBytes(Ar);
			for (const auto& ExternalDataToObjectPair : ExternalDataToObjectMap)
			{
				ExternalDataToObjectPair.Value.CountBytes(Ar);
			}
		);

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("PlaybackFrames",
			PlaybackFrames.CountBytes(Ar);
			for (auto& Frame : PlaybackFrames)
			{
				Frame.Value.CountBytes(Ar);
			}
		);

		GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("DeletedNetStartupActors",
			DeletedNetStartupActors.CountBytes(Ar);
			for (FString& ActorString : DeletedNetStartupActors)
			{
				Ar << ActorString;
			}
		);
	}
}

bool FReplayHelper::ReplicateActor(AActor* Actor, UNetConnection* Connection, bool bMustReplicate)
{
	if (UNetDriver::IsDormInitialStartupActor(Actor))
	{
		return false;
	}

	check(Connection && Connection->Driver);

	const int32 OriginalOutBunches = Connection->Driver->OutBunches;

	bool bDidReplicateActor = false;

	Actor->CallPreReplication(Connection->Driver);

	// Handle role swapping if this is a client-recorded replay.
	FScopedActorRoleSwap RoleSwap(Actor);

	if ((Actor->GetRemoteRole() != ROLE_None || Actor->GetTearOff()) && (Actor == Connection->PlayerController || Cast<APlayerController>(Actor) == nullptr))
	{
		const bool bShouldHaveChannel =
			Actor->bRelevantForNetworkReplays &&
			!Actor->GetTearOff() &&
			(!Actor->IsNetStartupActor() || Connection->ClientHasInitializedLevelFor(Actor));

		UActorChannel* Channel = Connection->FindActorChannelRef(Actor);

		if (bShouldHaveChannel && Channel == nullptr)
		{
			// Create a new channel for this actor.
			Channel = (UActorChannel*)Connection->CreateChannelByName(NAME_Actor, EChannelCreateFlags::OpenedLocally);
			if (Channel != nullptr)
			{
				Channel->SetChannelActor(Actor, ESetChannelActorFlags::None);
			}
		}

		if (Channel != nullptr && !Channel->Closing)
		{
			// Send it out!
			bDidReplicateActor = (Channel->ReplicateActor() > 0);

			// Close the channel if this actor shouldn't have one
			if (!bShouldHaveChannel)
			{
				if (Connection->ResendAllDataState == EResendAllDataState::None)		// Don't close the channel if we're forcing them to re-open for checkpoints
				{
					Channel->Close(EChannelCloseReason::Destroyed);
				}
			}
		}
	}

	if (bMustReplicate && !HasDeltaCheckpoints() && Connection->Driver->OutBunches == OriginalOutBunches)
	{
		UE_LOG(LogDemo, Error, TEXT("DemoReplicateActor: bMustReplicate is true but nothing was sent: %s"), *GetNameSafe(Actor));
	}

	return bDidReplicateActor;
}

bool FReplayHelper::ReadDemoFrame(UNetConnection* Connection, FArchive& Ar, TArray<FPlaybackPacket>& InPlaybackPackets, const bool bForLevelFastForward, const FArchivePos MaxArchiveReadPos, float* OutTime)
{
	SCOPED_NAMED_EVENT(FReplayHelper_ReadDemoFrame, FColor::Purple);

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ReadDemoFrame"), ReadDemoFrame, STATGROUP_Net);

	check(!bForLevelFastForward || HasLevelStreamingFixes());
	check(Connection);

	if (Ar.IsError())
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadDemoFrame: Archive Error"));
		OnReplayPlaybackError.Broadcast(EDemoPlayFailure::Serialization);
		return false;
	}

	if (Ar.AtEnd())
	{
		return false;
	}

	if (ReplayStreamer->GetLastError() != ENetworkReplayError::None)
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadDemoFrame: ReplayStreamer ERROR: %s"), ENetworkReplayError::ToString(ReplayStreamer->GetLastError()));
		OnReplayPlaybackError.Broadcast(EDemoPlayFailure::ReplayStreamerInternal);
		return false;
	}

	// Above checks guarantee the Archive is in a valid state, but it's entirely possible that
	// the ReplayStreamer doesn't have more stream data available (i.e., if we only have checkpoint data).
	// Therefore, skip this if we know we're only reading in checkpoint data.
	if (!bIsLoadingCheckpoint && !ReplayStreamer->IsDataAvailable())
	{
		return false;
	}

	int32 ReadCurrentLevelIndex = 0;

	uint32 PlaybackVersion = PlaybackDemoHeader.Version;

	if (PlaybackVersion >= HISTORY_MULTIPLE_LEVELS)
	{
		Ar << ReadCurrentLevelIndex;
	}

	float TimeSeconds = 0.0f;

	Ar << TimeSeconds;

	if (OutTime)
	{
		*OutTime = TimeSeconds;
	}

	if (PlaybackVersion >= HISTORY_LEVEL_STREAMING_FIXES)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Demo_ReceiveExports"), Demo_ReceiveExports, STATGROUP_Net);
		CastChecked<UPackageMapClient>(Connection->PackageMap)->ReceiveExportData(Ar);
	}

	// Check to see if we can skip adding these packets.
	// This may happen if the archive isn't set to a proper position due to level fast forwarding.
	const bool bAppendPackets = bIsLoadingCheckpoint || bForLevelFastForward || LatestReadFrameTime < TimeSeconds;

	LatestReadFrameTime = FMath::Max(LatestReadFrameTime, TimeSeconds);

	if (HasLevelStreamingFixes())
	{
		uint32 NumStreamingLevels = 0;
		Ar.SerializeIntPacked(NumStreamingLevels);

		// We want to avoid adding the same levels to the Seen list multiple times.
		// This can occur if the Archive is "double read" due to a level fast forward.
		const bool bAddToSeenList = bAppendPackets && !bForLevelFastForward;

		FString NameTemp;
		for (uint32 i = 0; i < NumStreamingLevels; i++)
		{
			Ar << NameTemp;

			if (bAddToSeenList)
			{
				// Add this level to the seen list, but don't actually mark it as being seen.
				// It will be marked when we have processed packets for it.
				const FLevelStatus& LevelStatus = FindOrAddLevelStatus(NameTemp);
				SeenLevelStatuses.Add(LevelStatus.LevelIndex);
			}
		}
	}
	else
	{
		// Read any new streaming levels this frame
		uint32 NumStreamingLevels = 0;
		Ar.SerializeIntPacked(NumStreamingLevels);

		for (uint32 i = 0; i < NumStreamingLevels; ++i)
		{
			FString PackageName;
			FString PackageNameToLoad;
			FTransform LevelTransform;

			Ar << PackageName;
			Ar << PackageNameToLoad;
			Ar << LevelTransform;

			// Don't add if already exists
			bool bFound = false;

			for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
			{
				FString SrcPackageName = StreamingLevel->GetWorldAssetPackageName();
				FString SrcPackageNameToLoad = StreamingLevel->PackageNameToLoad.ToString();

				if (SrcPackageName == PackageName && SrcPackageNameToLoad == PackageNameToLoad)
				{
					bFound = true;
					break;
				}
			}

			if (bFound)
			{
				continue;
			}

			ULevelStreamingDynamic* StreamingLevel = NewObject<ULevelStreamingDynamic>(World.Get(), NAME_None, RF_NoFlags, nullptr);

			StreamingLevel->SetShouldBeLoaded(true);
			StreamingLevel->SetShouldBeVisible(true);
			StreamingLevel->bShouldBlockOnLoad = false;
			StreamingLevel->bInitiallyLoaded = true;
			StreamingLevel->bInitiallyVisible = true;
			StreamingLevel->LevelTransform = LevelTransform;

			StreamingLevel->PackageNameToLoad = FName(*PackageNameToLoad);
			StreamingLevel->SetWorldAssetByPackageName(FName(*PackageName));

			World->AddStreamingLevel(StreamingLevel);

			UE_LOG(LogDemo, Log, TEXT("ReadDemoFrame: Loading streamingLevel: %s, %s"), *PackageName, *PackageNameToLoad);
		}
	}

	if (Ar.IsError())
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadDemoFrame: Failed to read demo ServerDeltaTime"));
		OnReplayPlaybackError.Broadcast(EDemoPlayFailure::Serialization);
		return false;
	}

	FArchivePos SkipExternalOffset = 0;
	if (HasLevelStreamingFixes())
	{
		Ar << SkipExternalOffset;
	}

	if (!bForLevelFastForward)
	{
		// Load any custom external data in this frame
		LoadExternalData(Ar, TimeSeconds);
	}
	else
	{
		Ar.Seek(Ar.Tell() + SkipExternalOffset);
	}

	FArchivePos SkipGameSpecificOffset = 0;
	if (HasGameSpecificFrameData())
	{
		Ar << SkipGameSpecificOffset;

		if ((SkipGameSpecificOffset > 0) && !bForLevelFastForward)
		{
			FDemoFrameDataMap Data;
			Ar << Data;

			if (Data.Num() > 0)
			{
				PlaybackFrames.Emplace(TimeSeconds, MoveTemp(Data));
			}
		}
		else
		{
			Ar.Seek(Ar.Tell() + SkipGameSpecificOffset);
		}
	}

	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Demo_ReadPackets"), Demo_ReadPackets, STATGROUP_Net);

		FPlaybackPacket ScratchPacket;
		ScratchPacket.TimeSeconds = TimeSeconds;
		ScratchPacket.LevelIndex = ReadCurrentLevelIndex;
		ScratchPacket.SeenLevelIndex = INDEX_NONE;

		const EReadPacketMode ReadPacketMode = bAppendPackets ? EReadPacketMode::Default : EReadPacketMode::SkipData;

		while ((MaxArchiveReadPos == 0) || (Ar.Tell() < MaxArchiveReadPos))
		{
			if (HasLevelStreamingFixes())
			{
				Ar.SerializeIntPacked(ScratchPacket.SeenLevelIndex);
			}

			switch (ReadPacket(Ar, ScratchPacket.Data, ReadPacketMode))
			{
			case EReadPacketState::Error:
			{
				UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadDemoFrame: ReadPacket failed."));
				OnReplayPlaybackError.Broadcast(EDemoPlayFailure::Serialization);
				return false;
			}

			case EReadPacketState::Success:
			{
				if (EReadPacketMode::SkipData == ReadPacketMode)
				{
					continue;
				}

				InPlaybackPackets.Emplace(MoveTemp(ScratchPacket));
				ScratchPacket.Data = TArray<uint8>();
				break;
			}

			case EReadPacketState::End:
			{
				return true;
			}

			default:
			{
				check(false);
				return false;
			}
			}
		}
	}

	// We should never hit this, as the while loop above should return on error or success.
	check(false);
	return false;
}

const FReplayHelper::EReadPacketState FReplayHelper::ReadPacket(FArchive& Archive, TArray<uint8>& OutBuffer, const EReadPacketMode Mode)
{
	const bool bSkipData = (EReadPacketMode::SkipData == Mode);

	int32 BufferSize = 0;
	Archive << BufferSize;

	if (UNLIKELY(Archive.IsError()))
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadPacket: Failed to read demo OutBufferSize"));
		return EReadPacketState::Error;
	}

	if (BufferSize == 0)
	{
		return EReadPacketState::End;
	}

	else if (UNLIKELY(BufferSize > FReplayHelper::MAX_DEMO_READ_WRITE_BUFFER))
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadPacket: OutBufferSize > MAX_DEMO_READ_WRITE_BUFFER"));
		return EReadPacketState::Error;
	}
	else if (UNLIKELY(BufferSize < 0))
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadPacket: OutBufferSize < 0"));
		return EReadPacketState::Error;
	}

	if (bSkipData)
	{
		Archive.Seek(Archive.Tell() + static_cast<int64>(BufferSize));
	}
	else
	{
		OutBuffer.SetNumUninitialized(BufferSize, false);
		Archive.Serialize(OutBuffer.GetData(), BufferSize);
	}

	if (UNLIKELY(Archive.IsError()))
	{
		UE_LOG(LogDemo, Error, TEXT("FReplayHelper::ReadPacket: Failed to read demo file packet"));
		return EReadPacketState::Error;
	}

	return EReadPacketState::Success;
}

bool FReplayHelper::ShouldSaveCheckpoint() const
{
	const double CHECKPOINT_DELAY = CVarCheckpointUploadDelayInSeconds.GetValueOnAnyThread();

	if (DemoCurrentTime - LastCheckpointTime > CHECKPOINT_DELAY)
	{
		return true;
	}

	return false;
}

float FReplayHelper::GetCheckpointSaveMaxMSPerFrame() const
{
	const float CVarValue = CVarCheckpointSaveMaxMSPerFrameOverride.GetValueOnAnyThread();
	if (CVarValue >= 0.0f)
	{
		return CVarValue;
	}

	return CheckpointSaveMaxMSPerFrame;
}

void FReplayHelper::ResetState()
{
	DemoFrameNum = 0;
	LatestReadFrameTime = 0.0f;
	bIsLoadingCheckpoint = false;
	LastCheckpointTime = 0.0f;
	ExternalDataToObjectMap.Empty();
	PlaybackFrames.Empty();

	ClearLevelStreamingState();
}

void FReplayHelper::OnLevelAddedToWorld(ULevel* InLevel, UWorld* InWorld)
{
	LLM_SCOPE(ELLMTag::Networking);

	if (InLevel && !InLevel->bClientOnlyVisible && (World == InWorld) && HasLevelStreamingFixes() && InWorld->IsPlayingReplay())
	{
		if (!NewStreamingLevelsThisFrame.Contains(InLevel) && !LevelsPendingFastForward.Contains(InLevel))
		{
			FLevelStatus& LevelStatus = FindOrAddLevelStatus(*InLevel);

			// If we haven't processed any packets for this level yet, immediately mark it as ready.
			if (!LevelStatus.bHasBeenSeen)
			{
				LevelStatus.bIsReady = true;
			}

			// If the level isn't ready, go ahead and queue it up to get fast-forwarded.
			// Note, we explicitly check the visible flag in case same the level gets notified multiple times.
			else if (!LevelStatus.bIsReady)
			{
				NewStreamingLevelsThisFrame.Add(InLevel);
			}
		}
	}
}

void FReplayHelper::OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	if (InLevel && !InLevel->bClientOnlyVisible && (World == InWorld) && HasLevelStreamingFixes() && InWorld->IsPlayingReplay())
	{
		const FString LevelPackageName = GetLevelPackageName(*InLevel);
		if (LevelStatusesByName.Contains(LevelPackageName))
		{
			FLevelStatus& LevelStatus = GetLevelStatus(LevelPackageName);
			LevelStatus.bIsReady = false;

			// Make sure we don't try to fast-forward this level later.
			LevelsPendingFastForward.Remove(InLevel);
			NewStreamingLevelsThisFrame.Remove(InLevel);
		}
	}

	// always invalidate cache since it uses pointers
	LevelStatusIndexByLevel.Remove(InLevel);
}

void FReplayHelper::AddOrUpdateEvent(const FString& Name, const FString& Group, const FString& Meta, const TArray<uint8>& Data)
{
	const uint32 SavedTimeMS = GetDemoCurrentTimeInMS();

	if (ReplayStreamer.IsValid())
	{
		ReplayStreamer->AddOrUpdateEvent(Name, SavedTimeMS, Group, Meta, Data);
	}

	UE_LOG(LogDemo, Verbose, TEXT("AddOrUpdateEvent %s.%s. Total: %i, Time: %2.2f"), *Group, *Name, Data.Num(), SavedTimeMS);
}

void FReplayHelper::ReadDeletedStartupActors(UNetConnection* Connection, FArchive& Ar, TSet<FString>& DeletedStartupActors)
{
	TSet<FString> TempSet;
	Ar << TempSet;

	DeletedStartupActors.Reserve(TempSet.Num());

	for (FString& Path : TempSet)
	{
		GEngine->NetworkRemapPath(Connection, Path, true);

		DeletedStartupActors.Emplace(MoveTemp(Path));
	}
}

void FReplayHelper::WriteDeletedStartupActors(UNetConnection* Connection, FArchive& Ar, const TSet<FString>& DeletedStartupActors)
{
	TSet<FString> TempSet;
	TempSet.Reserve(DeletedStartupActors.Num());

	// intentionally copying to avoid destroying the TSet hash
	for (FString Path : DeletedStartupActors)
	{
		GEngine->NetworkRemapPath(Connection, Path, false);

		TempSet.Emplace(MoveTemp(Path));
	}

	Ar << TempSet;
}

void FReplayHelper::SetAnalyticsProvider(TSharedPtr<IAnalyticsProvider> InProvider)
{
	AnalyticsProvider = InProvider;

	if (ReplayStreamer.IsValid())
	{
		ReplayStreamer->SetAnalyticsProvider(InProvider);
	}
}

const TCHAR* LexToString(EReplayHeaderFlags Flag)
{
	switch (Flag)
	{
	case EReplayHeaderFlags::ClientRecorded:
		return TEXT("ClientRecorded");
	case EReplayHeaderFlags::HasStreamingFixes:
		return TEXT("HasStreamingFixes");
	case EReplayHeaderFlags::DeltaCheckpoints:
		return TEXT("DeltaCheckpoints");
	case EReplayHeaderFlags::GameSpecificFrameData:
		return TEXT("GameSpecificFrameData");
	case EReplayHeaderFlags::ReplayConnection:
		return TEXT("ReplayConnection");
	default:
		check(false);
		return TEXT("Unknown");
	}
}