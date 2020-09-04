// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class DMXRUNTIME_API FDMXRuntimeUtils
{
public:
	/**
	 * Utility to separate a name from an index at the end.
	 * @param InString	The string to be separated.
	 * @param OutName	The string without an index at the end. White spaces and '_' are also removed.
	 * @param OutIndex	Index that was separated from the name. If there was none, it's zero.
	 *					Check the return value to know if there was an index on InString.
	 * @return True if there was an index on InString.
	 */
	static bool GetNameAndIndexFromString(const FString& InString, FString& OutName, int32& OutIndex);

	// can't instantiate this class
	FDMXRuntimeUtils() = delete;
};
