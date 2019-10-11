// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisasterRecoverySessionInfo.generated.h"

USTRUCT()
struct FDisasterRecoverySession
{
	GENERATED_BODY()

	UPROPERTY()
	FString LastSessionName;

	UPROPERTY()
	int32 ProcessId = 0;

	UPROPERTY()
	bool bAutoRestoreLastSession = false;

	/** The PID of the last client that write the file for this session. */
	UPROPERTY()
	int32 DisasterRecoveryClientPID = 0;

	/** The PID of the disaster recovery service launched by the client for the session. */
	UPROPERTY()
	int32 DisasterRecoveryServicePID = 0;
};

/**
 * Hold the information for multiple disaster recovery sessions.
 */
USTRUCT()
struct FDisasterRecoverySessionInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FDisasterRecoverySession> Sessions;
};
