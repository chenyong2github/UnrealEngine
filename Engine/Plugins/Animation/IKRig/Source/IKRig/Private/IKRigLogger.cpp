// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigLogger.h"

#include "Logging/MessageLog.h"

DEFINE_LOG_CATEGORY(LogIKRig);

void FIKRigLogger::SetLogTarget(const FName InLogName, bool bInSuppressWarnings)
{
	LogName = InLogName;
	bWarningsSuppressed = bInSuppressWarnings;
}

FName FIKRigLogger::GetLogTarget() const
{
	return LogName;
}
	
void FIKRigLogger::LogError(const FText& Message) const
{
	// print to the global output log
	UE_LOG(LogIKRig, Error, TEXT("%s"), *Message.ToString());

	// print to the output log in the asset editor
	FMessageLog MessageLog(LogName);
	MessageLog.Error(Message);
}

void FIKRigLogger::LogWarning(const FText& Message) const
{
	if (bWarningsSuppressed)
	{
		return;
	}

	// print to the global output log
	UE_LOG(LogIKRig, Warning, TEXT("%s"), *Message.ToString());

	// print to the output log in the asset editor
	FMessageLog MessageLog(LogName);
	MessageLog.Warning(Message);
}

void FIKRigLogger::LogEditorMessage(const FText& Message) const
{
	if (bWarningsSuppressed)
	{
		return;
	}

	// print to the output log in the asset editor
	FMessageLog MessageLog(LogName);
	MessageLog.Info(Message);
}
