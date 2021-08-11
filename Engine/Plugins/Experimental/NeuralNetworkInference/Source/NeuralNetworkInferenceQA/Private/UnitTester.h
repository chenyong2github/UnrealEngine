// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FUnitTester
{
public:
	/**
	 * Main unit tester function.
	 */
	static void GlobalTest(const FString& InGroundTruthDirectory, const FString& InModelsDirectory);
};
