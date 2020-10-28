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
	void OnUserLoginChanged(bool, int32, int32);

	static TUniquePtr<FEditorAnalyticsSession> CreateCurrentSession(const FDateTime& StartupTimeUtc, uint32 OutOfProcessMonitorProcessId);
	static FString GetUserActivityString();
	void UpdateSessionTimestamp(const FDateTime& InCurrTimeUtc);
	void UpdateSessionDuration(double InCurrTimeSecs);
	bool UpdateEditorIdleTime(double InCurrTimeSecs, bool bReset);
	bool UpdateUserIdleTime(double InCurrTimeSecs, bool bReset);
	bool UpdateOutOfProcessMonitorState(bool bQuickCheck);
	bool TrySaveCurrentSession(const FDateTime& CurrTimeUtc, double CurrTimeSecs);

private:
	TUniquePtr<FEditorAnalyticsSession> CurrentSession;
	FCriticalSection SaveSessionLock;

	/** The next time to check if the debugger is attached */
	double NextDebuggerCheckSecs;

	/** Last activity (user input, crash, terminate, shutdown) timestamp from FPlatformTime::Seconds() to track user inactivity. */
	TAtomic<double> LastUserActivityTimeSecs;

	/** The number of idle seconds in the current idle sequence that were accounted (saved in the session) for the user idle counters. */
	TAtomic<double> AccountedUserIdleSecs;

	/** Last activity (user input, crash, terminate, shutdown, CPU Burst) timestamp from FPlatformTime::Seconds(). */
	TAtomic<double> LastEditorActivityTimeSecs;

	/** Session timestamp from FDateTime::UtcNow(). Unreliable if user change system date/time (daylight saving or user altering it). */
	FDateTime SessionStartTimeUtc;

	/** Session timestamp from FPlatformTime::Seconds(). Lose precision when computing long time spans (+/- couple of seconds over a day). */
	double SessionStartTimeSecs = 0.0;

	/** The last save timestamp from FPlatformTime::Seconds(). */
	double LastSaveTimeSecs = 0.0;

	/** Non-zero if out-of process monitoring is set. To ensure one CrashReportClient(CRC) doesn't report the session of another CRC instance (race condition). */
	const uint32 OutOfProcessMonitorProcessId;

	/** True once Shutdown() is called. */
	bool bShutdown = false;
};

#endif
