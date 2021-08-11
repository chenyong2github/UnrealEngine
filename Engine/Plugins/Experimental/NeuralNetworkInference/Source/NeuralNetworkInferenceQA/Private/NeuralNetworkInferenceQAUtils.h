// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralTensor.h"

DEFINE_LOG_CATEGORY_STATIC(LogNeuralNetworkInferenceQA, Display, All);

class FNeuralNetworkInferenceQAUtils
{
public:
	/**
	 * It calculates the error between 2 data arrays (it should return 0 if both arrays are the same).
	 * @return False if the estimated error > InZeroThreshold, true otherwise.
	 */
	static bool EstimateTensorL1DiffError(const FNeuralTensor& InTensorA, const FNeuralTensor& InTensorB, const float InZeroThreshold, const FString& InDebugName);
};
