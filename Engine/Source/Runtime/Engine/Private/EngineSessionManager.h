// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// TODO: implement Watchdog on Mac and Linux and replace Windows check with desktop check below
//#define PLATFORM_SUPPORTS_WATCHDOG		PLATFORM_DESKTOP
#define PLATFORM_SUPPORTS_WATCHDOG		PLATFORM_WINDOWS

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
		CurrentSession.StartupTimestamp = FDateTime::MinValue();
		CurrentSession.Idle1Min = 0;
		CurrentSession.Idle5Min = 0;
		CurrentSession.Idle30Min = 0;
		CurrentSession.bCrashed = false;
		CurrentSession.bGPUCrashed = false;
		CurrentSession.bIsDebugger = false;
		CurrentSession.bWasEverDebugger = false;
		CurrentSession.bIsDeactivated = false;
		CurrentSession.bIsInBackground = false;
		CurrentSession.bIsVanilla = false;
		CurrentSession.bIsTerminating = false;
		CurrentSession.bWasShutdown = false;
		CurrentSession.bIsInPIE = false;
		CurrentSession.bIsInEnterprise = false;
		CurrentSession.bIsInVRMode = false;
	}

	void Initialize();

	void Tick(float DeltaTime);

	void Shutdown();

private:
	struct FSessionRecord
	{
		EEngineSessionManagerMode Mode;
		FString SessionId;
		FString ProjectName;
		FString EngineVersion;
		FDateTime StartupTimestamp;
		FDateTime Timestamp;
		int32 Idle1Min;
		int32 Idle5Min;
		int32 Idle30Min;
		FString CurrentUserActivity;
		TArray<FString> Plugins;
		float AverageFPS;

		bool bCrashed : 1;
		bool bGPUCrashed : 1;
		bool bIsDebugger : 1;
		bool bWasEverDebugger : 1;
		bool bIsDeactivated : 1;
		bool bIsInBackground : 1;
		bool bIsVanilla : 1;
		bool bIsTerminating : 1;
		bool bWasShutdown : 1;
		bool bIsInPIE : 1;
		bool bIsInEnterprise : 1;
		bool bIsInVRMode : 1;
	};

private:
	void InitializeRecords(bool bFirstAttempt);
	bool BeginReadWriteRecords();
	void EndReadWriteRecords();
	void DeleteStoredRecord(const FSessionRecord& Record);
	void DeleteStoredRecordValues(const FString& SectionName) const;
	void SendAbnormalShutdownReport(const FSessionRecord& Record);
	void SendSessionRecordEvent(const FString& EventName, const FSessionRecord& Record, bool bSendHardwareDetails);
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

#if PLATFORM_SUPPORTS_WATCHDOG
	void StartWatchdog(const FString& RunType, const FString& ProjectName, const FString& PlatformName, const FString& SessionId, const FString& EngineVersion);
	FString GetWatchdogStoreSectionString(uint32 InPID);
#endif

private:
	EEngineSessionManagerMode Mode;
	bool bInitializedRecords;
	bool bShutdown;
	float HeartbeatTimeElapsed;
	FSessionRecord CurrentSession;
	FString CurrentSessionSectionName;
	TArray<FSessionRecord> SessionRecords;

#if PLATFORM_SUPPORTS_WATCHDOG
	FString WatchdogSectionName;
#endif
};
