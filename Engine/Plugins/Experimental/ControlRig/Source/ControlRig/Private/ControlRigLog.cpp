// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigLog.h"

void FControlRigLog::Report(EMessageSeverity::Type InSeverity, const FName& InOperatorName, int32 InInstructionIndex, const FString& InMessage)
{
#if WITH_EDITOR
	Entries.Add(FLogEntry(InSeverity, InOperatorName, InInstructionIndex, InMessage));
#endif
}
