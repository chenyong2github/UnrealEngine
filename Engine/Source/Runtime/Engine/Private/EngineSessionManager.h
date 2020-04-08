// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FUserActivity;

enum class EEngineSessionManagerMode
{
	Editor,
	Game
};

/* Handles writing session records to platform's storage to track crashed and timed-out editor sessions */
class FEngineSessionManager
{
public:
	FEngineSessionManager(EEngineSessionManagerMode InMode)
		: Mode(InMode)
		, bInitializedRecords(false)
		, bShutdown(false)
		, HeartbeatTimeElapsed(0.0f)
	{
		CurrentSession.Mode = EEngineSessionManagerMode::Editor;
		CurrentSession.Timestamp = FDateTime::MinValue();		
		CurrentSession.bCrashed = false;
		CurrentSession.bGPUCrashed = false;
		CurrentSession.bIsDebugger = false;
		CurrentSession.bWasEverDebugger = false;
		CurrentSession.bIsDeactivated = false;
		CurrentSession.bIsInBackground = false;
		CurrentSession.bIsVanilla = false;
		CurrentSession.bIsTerminating = false;
	}

	void Initialize();

	void Tick(float DeltaTime);

	void Shutdown();

private:
	struct FSessionRecord
	{
		FString SessionId;
		EEngineSessionManagerMode Mode;
		FString ProjectName;
		FString EngineVersion;
		FDateTime Timestamp;
		bool bCrashed;
		bool bGPUCrashed;
		bool bIsDebugger;
		bool bWasEverDebugger;
		bool bIsDeactivated;
		bool bIsInBackground;
		FString CurrentUserActivity;
		bool bIsVanilla;
		bool bIsTerminating;
	};

private:
	void InitializeRecords(bool bFirstAttempt);
	bool BeginReadWriteRecords();
	void EndReadWriteRecords();
	void DeleteStoredRecord(const FSessionRecord& Record);
	void DeleteStoredRecordValues(const FString& SectionName) const;
	void SendAbnormalShutdownReport(const FSessionRecord& Record);
	void CreateAndWriteRecordForSession();
	void OnCrashing();
	void OnAppReactivate();
	void OnAppDeactivate();
	void OnAppBackground();
	void OnAppForeground();
	void OnTerminate();
	FString GetStoreSectionString(FString InSuffix);
	void OnUserActivity(const FUserActivity& UserActivity);
	void OnVanillaStateChanged(bool bIsVanilla);
	FString GetUserActivityString() const;

private:
	EEngineSessionManagerMode Mode;
	bool bInitializedRecords;
	bool bShutdown;
	float HeartbeatTimeElapsed;
	FSessionRecord CurrentSession;
	FString CurrentSessionSectionName;
	TArray<FSessionRecord> SessionRecords;
};
