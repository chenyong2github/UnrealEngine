// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Versioning control class.
 */
class NEURALNETWORKINFERENCECORE_API FNeuralNetworkInferenceVersion
{
public:
	static const int32 VERSION_MAJOR;
	static const int32 VERSION_MIDDLE;
	static const int32 VERSION_MINOR;
	static bool CheckVersion(const TArray<int32>& InVersion);
	static TArray<int32> GetVersion();
};
