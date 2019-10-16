// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FUserActivity;
struct FEditorSessionRecord;

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
	void InitializeRecords(bool bFirstAttempt);

	FEditorSessionRecord* CreateRecordForCurrentSession() const;
	void WriteStoredRecord(const FEditorSessionRecord& Record) const;

	void OnCrashing();
	void OnTerminate();
	void OnUserActivity(const FUserActivity& UserActivity);
	void OnVanillaStateChanged(bool bIsVanilla);

	FString GetUserActivityString() const;
	void UpdateTimestamps();

private:
	FEditorSessionRecord* CurrentSession;
	FString CurrentSessionSectionName;
	double LastUserInteractionTime;
	float HeartbeatTimeElapsed;
	bool bInitializedRecords;
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
	TArray<FEditorSessionRecord> ReadStoredRecords() const;
	void DeleteStoredRecord(const FEditorSessionRecord& Record) const;
	void SendSessionSummaryEvent(const FEditorSessionRecord& Record) const;
	bool IsSessionProcessRunning(const FEditorSessionRecord& Record) const;

private:
	float HeartbeatTimeElapsed;
};