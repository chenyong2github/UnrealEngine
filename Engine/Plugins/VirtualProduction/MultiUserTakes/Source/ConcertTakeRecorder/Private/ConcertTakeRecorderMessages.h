// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Recorder/TakeRecorderParameters.h"
#include "IdentifierTable/ConcertIdentifierTableData.h"

#include "ConcertTakeRecorderMessages.generated.h"


USTRUCT()
struct FConcertTakeInitializedEvent
{
	GENERATED_BODY()

	UPROPERTY()
	FString TakePresetPath;

	UPROPERTY()
	FString TakeName;

	UPROPERTY()
	TArray<uint8> TakeData;

	UPROPERTY()
	FConcertLocalIdentifierState IdentifierState;
};

USTRUCT()
struct FConcertRecordingFinishedEvent
{
	GENERATED_BODY()

	UPROPERTY()
	FString TakeName;
};

USTRUCT()
struct FConcertRecordingCancelledEvent
{
	GENERATED_BODY()

	UPROPERTY()
	FString TakeName;
};
