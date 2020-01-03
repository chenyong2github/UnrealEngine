// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "HAL/Platform.h"

struct FEditorAnalyticsSession;
class IAnalyticsProvider;

/**
  * Sender of SessionSummary events from all editor sessions in-between runs.
  * Separated from Writer to make it easier to run it out-of-process.
  */
class EDITORANALYTICSSESSION_API FEditorSessionSummarySender
{
public:
	FEditorSessionSummarySender(IAnalyticsProvider& InAnalyticsProvider, const FString& InSenderName, const int32 InCurrentSessionProcessId);
	~FEditorSessionSummarySender();

	void Tick(float DeltaTime);
	void Shutdown();

	void SetCurrentSessionExitCode(const int32 InCurrentSessionProcessId, const int32 InExitCode);
	bool FindCurrentSession(FEditorAnalyticsSession& OutSession) const;

private:
	/** Send any stored Sessions. */
	void SendStoredSessions(const bool bForceSendCurrentSession = false) const;
	void SendSessionSummaryEvent(const FEditorAnalyticsSession& Session) const;

private:
	float HeartbeatTimeElapsed;
	IAnalyticsProvider& AnalyticsProvider;
	FString Sender;

	int32 CurrentSessionProcessId;
	TOptional<int32> CurrentSessionExitCode;
};
