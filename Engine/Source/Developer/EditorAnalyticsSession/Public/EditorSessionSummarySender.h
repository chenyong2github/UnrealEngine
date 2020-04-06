// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "Misc/Optional.h"

struct FEditorAnalyticsSession;
class IAnalyticsProviderET;

/**
  * Sender of SessionSummary events from all editor sessions in-between runs.
  * Separated from Writer to make it easier to run it out-of-process.
  */
class EDITORANALYTICSSESSION_API FEditorSessionSummarySender
{
public:
	FEditorSessionSummarySender(IAnalyticsProviderET& InAnalyticsProvider, const FString& InSenderName, const uint32 InCurrentSessionProcessId);
	~FEditorSessionSummarySender();

	void Tick(float DeltaTime);
	void Shutdown();

private:
	/** Send any stored Sessions. */
	void SendStoredSessions(const bool bForceSendCurrentSession = false) const;
	void SendSessionSummaryEvent(const FEditorAnalyticsSession& Session) const;

private:
	float HeartbeatTimeElapsed;
	IAnalyticsProviderET& AnalyticsProvider;
	FString Sender;
	uint32 CurrentSessionProcessId;
};
