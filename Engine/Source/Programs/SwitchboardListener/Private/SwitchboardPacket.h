// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SwitchboardPacket.generated.h"


USTRUCT()
struct FSwitchboardPacket
{
	GENERATED_BODY()

	UPROPERTY()
	FString Command;

	UPROPERTY()
	bool bAck;

	UPROPERTY()
	FString Error;
};


USTRUCT()
struct FSwitchboardStateRunningProcess
{
	GENERATED_BODY()

	UPROPERTY()
	FString Uuid;

	UPROPERTY()
	FString Path;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Caller;
};

USTRUCT()
struct FSwitchboardStatePacket : public FSwitchboardPacket
{
	GENERATED_BODY()

	FSwitchboardStatePacket() : FSwitchboardPacket()
	{
		Command = TEXT("state");
		bAck = true;
	}

	UPROPERTY()
	TArray<FSwitchboardStateRunningProcess> RunningProcesses;
};
