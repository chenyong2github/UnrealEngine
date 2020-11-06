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
		Version = 0x00010000;
	}

	UPROPERTY()
	TArray<FSwitchboardStateRunningProcess> RunningProcesses;

	UPROPERTY()
	uint32 Version;
};

USTRUCT()
struct FSwitchboardProgramStdout : public FSwitchboardPacket
{
	GENERATED_BODY()

	FSwitchboardProgramStdout() : FSwitchboardPacket()
	{
		Command = TEXT("programstdout");
		bAck = true;
	}

	UPROPERTY()
	FSwitchboardStateRunningProcess Process;

	UPROPERTY()
	TArray<uint8> PartialStdout;
};

USTRUCT()
struct FSwitchboardProgramEnded : public FSwitchboardPacket
{
	GENERATED_BODY()

	FSwitchboardProgramEnded() : FSwitchboardPacket()
	{
		Command = TEXT("program ended");
		bAck = true;
	}

	UPROPERTY()
	FSwitchboardStateRunningProcess Process;

	UPROPERTY()
	int32 Returncode;

	UPROPERTY()
	FString Output;
};
