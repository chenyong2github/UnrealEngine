// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUsdErrorUtility, Log, All);

namespace UsdUtils
{
    /**
     * Pushes an USD error monitoring object into the stack and catches any emitted errors
     */
	USDUTILITIES_API void StartMonitoringErrors();

    /**
     * Returns all errors that were captured since StartMonitoringErrors(), clears and pops an
     * error monitoring object from the stack
     */
	USDUTILITIES_API TArray<FString> GetErrorsAndStopMonitoring();

    /**
     * Displays the error messages for each captured error since StartMonitoringErrors(),
	 * clears and pops an error monitoring object from the stack.
	 * If ToastMessage is empty, a default message will be displayed.
     * Returns true if there were any errors.
     */
	USDUTILITIES_API bool ShowErrorsAndStopMonitoring(const FText& ToastMessage = FText());
}
