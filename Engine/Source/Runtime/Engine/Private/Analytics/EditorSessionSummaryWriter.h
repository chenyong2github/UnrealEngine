// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

struct FUserActivity;
struct FEditorAnalyticsSession;

/** Writer for SessionSummary events to track all editor sessions. */
class FEditorSessionSummaryWriter
{
public:
	FEditorSessionSummaryWriter();
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

	static TUniquePtr<FEditorAnalyticsSession> CreateCurrentSession();
	static FString GetUserActivityString();
	void UpdateTimestamps();
	void UpdateIdleTimes();
	void TrySaveCurrentSession();

private:
	TUniquePtr<FEditorAnalyticsSession> CurrentSession;
	FString CurrentSessionSectionName;
	FCriticalSection SaveSessionLock;
	float HeartbeatTimeElapsed;
	bool bShutdown;
};

#endif
