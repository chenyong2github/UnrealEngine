// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/TokenizedMessage.h"

struct CONTROLRIG_API FControlRigLog
{
public:

	FControlRigLog() {}
	virtual ~FControlRigLog() {}

#if WITH_EDITOR
	struct FLogEntry
	{
		FLogEntry(EMessageSeverity::Type InSeverity, const FName& InOperatorName, int32 InInstructionIndex, const FString& InMessage)
		: Severity(InSeverity)
		, OperatorName(InOperatorName)
		, InstructionIndex(InInstructionIndex)
		, Message(InMessage)
		{}
		
		EMessageSeverity::Type Severity;
		FName OperatorName;
		int32 InstructionIndex;
		FString Message;
	};
	TArray<FLogEntry> Entries;
#endif

	virtual void Report(EMessageSeverity::Type InSeverity, const FName& InOperatorName, int32 InInstructionIndex, const FString& InMessage);
};
