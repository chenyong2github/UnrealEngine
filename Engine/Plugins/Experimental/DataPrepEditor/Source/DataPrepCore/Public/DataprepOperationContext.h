// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
* Contains all data regarding the context in which an operation will be executed
*/
struct FDataprepOperationContext
{
	// The context contains on which the operation should operate on.
	TSharedPtr<struct FDataprepContext> Context;

	// Optional Logger to capture the log produced by an operation (via the functions LogInfo, LogWarning and LogError).
	TSharedPtr<class IDataprepLogger> DataprepLogger;

	// Optional Progress Reporter to capture any progress reported by an operation.
	TSharedPtr<class IDataprepProgressReporter> DataprepProgressReporter;
};

