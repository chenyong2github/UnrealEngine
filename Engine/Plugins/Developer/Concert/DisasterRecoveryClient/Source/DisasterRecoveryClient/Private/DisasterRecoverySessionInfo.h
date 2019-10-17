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

	/** The ID of the process hosting this client session (The PID if of the process for which the transactions are recorded). */
	UPROPERTY()
	int32 HostProcessId = 0;

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

	UPROPERTY()
	TArray<FDisasterRecoverySession> Sessions;
};
