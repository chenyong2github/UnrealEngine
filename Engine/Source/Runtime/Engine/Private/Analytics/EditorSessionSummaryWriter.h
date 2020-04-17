// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Atomic.h"

#if WITH_EDITOR

struct FUserActivity;
struct FEditorAnalyticsSession;

/** Writer for SessionSummary events to track all editor sessions. */
class FEditorSessionSummaryWriter
{
public:
	FEditorSessionSummaryWriter(uint32 OutOfProcessMonitorProcessId);
	~FEditorSessionSummaryWriter();

	void Initialize();
	void Tick(float DeltaTime);
	void LowDriveSpaceDetected();
	void Shutdown();

private:
	void OnCrashing();
	void OnTerminate();
	void OnUserActivity(const FUserActivity& UserActivity);
	void OnVanillaStateChanged(bool bIsVanilla);
	void OnSlateUserInteraction(double CurrSlateInteractionTime);
	void OnEnterPIE(const bool /*bIsSimulating*/);
	void OnExitPIE(const bool /*bIsSimulating*/);

	static TUniquePtr<FEditorAnalyticsSession> CreateCurrentSession(uint32 OutOfProcessMonitorProcessId);
	static FString GetUserActivityString();
	void UpdateTimestamp(const FDateTime& InCurrTimeUtc);
	void UpdateEditorIdleTime(const FDateTime& InActivityTimeUtc, bool bSaveSession);
	void UpdateUserIdleTime(const FDateTime& InUserActivityTimeUtc, bool bSaveSession);
	void UpdateLegacyIdleTimes();
	void TrySaveCurrentSession();

private:
	TUniquePtr<FEditorAnalyticsSession> CurrentSession;
	FString CurrentSessionSectionName;
	FCriticalSection SaveSessionLock;
	float HeartbeatTimeElapsed;

	bool bShutdown;
	const uint32 OutOfProcessMonitorProcessId; // Non-zero if out-of process monitoring is set. To ensure one CrashReportClient(CRC) doesn't report the session of another CRC instance (race condition).
	double LastSlateInteractionTime;
	TAtomic<uint64> LastTickUtcTime;           // Time since the last 'Tick()' to detect when the main thread isn't ticking.
	TAtomic<uint64> LastEditorActivityUtcTime; // Since the last user action or CPU burst usage.
	TAtomic<uint64> LastUserActivityUtcTime;   // Since the last user input.
};

#endif
