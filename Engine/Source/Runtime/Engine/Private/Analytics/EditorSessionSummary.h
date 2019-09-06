// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FUserActivity;
struct FSessionRecord;

/** Writer for SessionSummary events to track all editor sessions. */
class FEditorSessionSummaryWriter
{
public:
	FEditorSessionSummaryWriter();

	void Initialize();
	void Tick(float DeltaTime);
	void Shutdown();

private:
	void InitializeRecords(bool bFirstAttempt);

	FSessionRecord* CreateRecordForCurrentSession() const;
	void WriteStoredRecord(const FSessionRecord& Record) const;

	void OnCrashing();
	void OnTerminate();
	void OnUserActivity(const FUserActivity& UserActivity);
	void OnVanillaStateChanged(bool bIsVanilla);

	FString GetUserActivityString() const;
	void UpdateTimestamps();

private:
	bool bInitializedRecords;
	FSessionRecord* CurrentSession;
	FString CurrentSessionSectionName;
	float HeartbeatTimeElapsed;
	bool bShutdown;
};

/** Sender of SessionSummary events from all editor sessions in-between runs. 
  * Separated from Writer to make it easier to run it out-of-process.
  */
class FEditorSessionSummarySender
{
public:
	FEditorSessionSummarySender();

	void Initialize();
	void Tick(float DeltaTime);

private:
	/** Send any stored records. Returns true if we successfully recorded any outstanding events. */
	void SendStoredRecords(FTimespan Timeout) const;
	TArray<FSessionRecord> ReadStoredRecords() const;
	void DeleteStoredRecord(const FSessionRecord& Record) const;
	void SendSessionSummaryEvent(const FSessionRecord& Record) const;
	bool IsSessionProcessRunning(const FSessionRecord& Record) const;

private:
	float HeartbeatTimeElapsed;
};