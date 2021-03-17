// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "ReplaySubsystem.generated.h"

class UReplayNetConnection;

UCLASS(DisplayName = "Replay Subsystem")
class ENGINE_API UReplaySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/* UGameInstanceSubsystem */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Begin replay recording
	 *
	 * @param Name Replay name (session name, file name, etc)
	 * @param FriendlyName Description of replay, preferably human readable 
	 * @param AdditionalOptions Additional options values, if any, such as a replay streamer override
	 * @param AnalyticsProvider Any analytics provider interface in case the replay subsystem/streamer has events to report
	 */
	void RecordReplay(const FString& Name, const FString& FriendlyName, const TArray<FString>& AdditionalOptions, TSharedPtr<IAnalyticsProvider> AnalyticsProvider);

	/**
	 * Begin replay playback
	 *
	 * @param Name Replay name (session name, file name, etc)
	 * @param WorldOverride world overridef for playing back on specific UWorld
	 * @param AdditionalOptions addition options values, if any, such as a replay streamer override
	 */
	bool PlayReplay(const FString& Name, UWorld* WorldOverride, const TArray<FString>& AdditionalOptions);

	/**
	 * Stop replay recording/playback
	 */
	void StopReplay();

	/**
	 * Get current recording/playing replay name
	 *
	 * @return FString Name of relpay (session id, file name, etc)
	 */
	FString GetActiveReplayName() const;

	/**
	 * Get current recording/playing replay time
	 *
	 * @return float Current recording/playback time in seconds
	 */
	float GetReplayCurrentTime() const;

	/**
	 * Add a user to be associated with the replay (legacy)
	 *
	 * @param UserString String representing user (platform specific id, user name, etc)
	 */
	void AddUserToReplay(const FString& UserString);

	bool IsRecording() const;
	bool IsPlaying() const;
	bool IsSavingCheckpoint() const;

	/**
	 * Add an event to the currently recording replay, associated with the current time
	 *
	 * @param Group Event group identifier
	 * @param Meta Metadata associated with the event
	 * @param Data Buffer of bytes representing the event payload
	 */
	void AddEvent(const FString& Group, const FString& Meta, const TArray<uint8>& Data);

	/**
	 * Add or update an existing event in the recording replay, see AddEvent as well
	 *
	 * @param EventName Unqiue event name identifier
	 * @param Group Event group identifier
	 * @param Meta Metadata associated with the event
	 * @param Data Buffer of bytes representing the event payload
	 */
	void AddOrUpdateEvent(const FString& EventName, const FString& Group, const FString& Meta, const TArray<uint8>& Data);

	/**
	 * Set per frame limit spent recording checkpoint data
	 *
	 * @param InCheckpointSaveMaxMSPerFrame Time in milliseconds
	 */
	void SetCheckpointSaveMaxMSPerFrame(const float InCheckpointSaveMaxMSPerFrame);

	/**
	 * Whether to reload the default map when StopReplay is called.
	 */
	UPROPERTY(EditAnywhere, Category=Replay)
	bool bLoadDefaultMapOnStop = true;

private:
	void StopExistingReplays(UWorld* InWorld);

	void OnSeamlessTravelStart(UWorld* CurrentWorld, const FString& LevelName);
	void OnSeamlessLevelTransition(UWorld* CurrentWorld);
	void OnCopyWorldData(UWorld* CurrentWorld, UWorld* LoadedWorld);

	TWeakObjectPtr<UReplayNetConnection> ReplayConnection;
};