// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessages.h"
#include "DisasterRecoverySessionInfo.generated.h"

USTRUCT()
struct FDisasterRecoverySession
{
	GENERATED_BODY()

	/** The repository ID created on the server to store that session. */
	UPROPERTY()
	FGuid RepositoryId;

	/** The session repository root dir used to create this session. */
	UPROPERTY()
	FString RepositoryRootDir;

	/** The name of the session. */
	UPROPERTY()
	FString LastSessionName;

	/** The ID of the process hosting this client session (The PID if of the process for which the transactions are recorded). */
	UPROPERTY()
	int32 HostProcessId = 0;

	/** The flag used to determine if the session was properly ended or crashed. */
	UPROPERTY()
	bool bAutoRestoreLastSession = false;
};

/**
 * Hold the information for multiple disaster recovery sessions.
 */
USTRUCT()
struct FDisasterRecoverySessionInfo
{
	GENERATED_BODY()

	/** The list of active/crashing/crashed sessions. */
	UPROPERTY()
	TArray<FDisasterRecoverySession> Sessions;

	/** The list of sessions kept as backup (rotated over time). */
	UPROPERTY()
	TArray<FDisasterRecoverySession> SessionHistory;
};

/**
 * Abstract the management of the recovery session file.
 */
class IDisasterRecoverySessionManager
{
public:
	~IDisasterRecoverySessionManager() = default;

	/** From the set of available disaster recovery sessions, if any, select a candidate to restore. */
	virtual TOptional<FDisasterRecoverySession> FindRecoverySessionCandidate(const TArray<FConcertSessionRepositoryInfo>& WorkspaceStats) = 0;
	
	/** Make this process responsible to recover the session previously selected as candidate. */
	virtual void TakeRecoverySessionOwnership(const FDisasterRecoverySession& Session) = 0;

	/** Return a list of expired session repositories that can be deleted from the server. */
	virtual TArray<FGuid> GetExpiredSessionRepositoryIds() const = 0;
	
	/** Invoked when session repositories were deleted from the server. */
	virtual void OnSessionRepositoryDropped(const TArray<FGuid>& SessionWorkspaceId) = 0;

	/** Returns the session repository root directory under which the server will create the session repositories. */
	virtual FString GetSessionRepositoryRootDir() const = 0;

	/** Return the session repository ID to create if a new blank session is created (rather than restoring from an existing one). */
	virtual FGuid GetSessionRepositoryId() const = 0;
	
	/** Remove the session from the list of managed session because it cannot be found/restored anymore. */
	virtual void DiscardRecoverySession(const FDisasterRecoverySession& Session) = 0;
};
