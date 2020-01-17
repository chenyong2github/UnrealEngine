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

	void Initialize();
	void Tick(float DeltaTime);
	void LowDriveSpaceDetected();
	void Shutdown();

private:
	void InitializeSessions();

	FEditorAnalyticsSession* CreateCurrentSession() const;

	void OnCrashing();
	void OnTerminate();
	void OnUserActivity(const FUserActivity& UserActivity);
	void OnVanillaStateChanged(bool bIsVanilla);

	FString GetUserActivityString() const;
	void UpdateTimestamps();
	void TrySaveCurrentSession();

private:
	FEditorAnalyticsSession* CurrentSession;
	FString CurrentSessionSectionName;
	double StartupSeconds;
	double LastUserInteractionTime;
	float HeartbeatTimeElapsed;
	bool bShutdown;
};

#endif
