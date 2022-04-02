// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "UObject/NameTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogIKRig, Warning, All);

struct IKRIG_API FIKRigLogger
{
	/** Set the name of the log to output messages to, and whether to suppress warnings */
	void SetLogTarget(const FName InLogName, bool bInSuppressWarnings);
	/** Get the name this log is currently outputting to */
	FName GetLogTarget() const;
	/** Log a warning message to display to user. */
	void LogError(const FText& Message) const;
	/** Log a warning message to display to user. */
	void LogWarning(const FText& Message) const;
	/** Log a message to display to editor output log. */
	void LogEditorMessage(const FText& Message) const;

private:
	/** the name of the output log this logger will send messages to
	 *
	 * For the IK Rig and Retargeting editors, we desire to filter the messages that originate only from the asset
	 * that is being edited. Therefore we name the log using the unique ID of the UObject itself (valid for lifetime of UObject between loads)
	 */
	FName LogName;
	
	/** when true, warnings will not be printed out. */
	bool bWarningsSuppressed = false;
};