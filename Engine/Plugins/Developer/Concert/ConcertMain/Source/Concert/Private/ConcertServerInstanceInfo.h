// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertServerInstanceInfo.generated.h"

/** Keeps information about a concert server instance. */
USTRUCT()
struct FConcertServerInstance
{
	GENERATED_BODY()

	UPROPERTY()
	FString ServerName;

	UPROPERTY()
	FString WorkingDirectory;

	UPROPERTY()
	FString ArchiveDirectory;

	UPROPERTY()
	int32 ProcessId = 0;

	/** True if the server instance was not allowed to scan/load existing sessions in the working/archived directories to avoid file conflicts with other instances. */
	UPROPERTY()
	bool bEnclaved = false;
};

/** Keeps information about all concert server instances. */
USTRUCT()
struct FConcertServerInstanceInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FConcertServerInstance> Instances;
};
