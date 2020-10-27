// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDMXEntityFixturePatch;

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
	/**
	 * Maps each patch to its universe.
	 *
	 * @param AllPatches Patches to map
	 *
	 * @return Key: universe Value: patches in universe with associated key
	 */
	static TMap<int32, TArray<UDMXEntityFixturePatch*>> MapToUniverses(const TArray<UDMXEntityFixturePatch*>& AllPatches);

	/**
	 * Generates a unique name given a base one and a list of potential ones
	 */
	static FString GenerateUniqueNameForImportFunction(TMap<FString, uint32>& OutPotentialFunctionNamesAndCount, const FString& InBaseName);
	
	// can't instantiate this class
	FDMXRuntimeUtils() = delete;
};
