// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp ProgressCancel

#pragma once

#include "Templates/Function.h"
#include "Containers/Array.h"
#include "Internationalization/Text.h"
#include "Misc/DateTime.h"

/**
 * FProgressCancel is an obejct that is intended to be passed to long-running
 * computes to do two things:
 * 1) provide progress info back to caller (not implemented yet)
 * 2) allow caller to cancel the computation
 */
class FProgressCancel
{
private:
	bool WasCancelled = false;  // will be set to true if CancelF() ever returns true

public:
	TFunction<bool()> CancelF = []() { return false; };

	/**
	 * @return true if client would like to cancel operation
	 */
	bool Cancelled()
	{
		if (WasCancelled)
		{
			return true;
		}
		WasCancelled = CancelF();
		return WasCancelled;
	}


public:

	enum class EMessageLevel
	{
		// Note: Corresponds to EToolMessageLevel in InteractiveToolsFramework/ToolContextInterfaces.h

		/** Development message goes into development log */
		Internal = 0,
		/** User message should appear in user-facing log */
		UserMessage = 1,
		/** Notification message should be shown in a non-modal notification window */
		UserNotification = 2,
		/** Warning message should be shown in a non-modal notification window with panache */
		UserWarning = 3,
		/** Error message should be shown in a modal notification window */
		UserError = 4
	};

	struct FMessageInfo
	{
		FText MessageText;
		EMessageLevel MessageLevel;
		FDateTime Timestamp;
	};

	void AddWarning(const FText& MessageText, EMessageLevel MessageLevel)
	{
		Warnings.Add(FMessageInfo{ MessageText , MessageLevel, FDateTime::Now() });
	}

	TArray<FMessageInfo> Warnings;
};
