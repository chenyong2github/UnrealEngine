// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisasterRecoverySessionInfo.generated.h"

USTRUCT()
struct FDisasterRecoverySessionInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString LastSessionName;

	UPROPERTY()
	bool bAutoRestoreLastSession = false;
};
