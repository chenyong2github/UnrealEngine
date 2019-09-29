// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	FEditorSessionSummarySender(IAnalyticsProvider& InAnalyticsProvider, const FString& InSenderName);

	void Tick(float DeltaTime);
	void Shutdown();

private:
	/** Send any stored Sessions. */
	void SendStoredSessions() const;
	void SendSessionSummaryEvent(const FEditorAnalyticsSession& Session) const;

private:
	float HeartbeatTimeElapsed;
	IAnalyticsProvider& AnalyticsProvider;
	FString Sender;
};