// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
		FLogEntry(EMessageSeverity::Type InSeverity, const FName& InUnit, const FString& InMessage)
		: Severity(InSeverity)
		, Unit(InUnit)
		, Message(InMessage)
		{}
		
		EMessageSeverity::Type Severity;
		FName Unit;
		FString Message;
	};
	TArray<FLogEntry> Entries;
#endif

	virtual void Report(EMessageSeverity::Type InSeverity, const FName& InUnit, const FString& InMessage);
};
