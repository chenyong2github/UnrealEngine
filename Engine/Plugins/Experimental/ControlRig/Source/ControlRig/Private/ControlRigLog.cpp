// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigLog.h"

void FControlRigLog::Report(EMessageSeverity::Type InSeverity, const FName& InUnit, const FString& InMessage)
{
#if WITH_EDITOR
	Entries.Add(FLogEntry(InSeverity, InUnit, InMessage));
#endif
}
