// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class NEURALNETWORKINFERENCEQA_API FNeuralNetworkInferenceQATimer
{
public:
	void Tic();

	/**
	 * Time in milliseconds, but with nanosecond accuracy (e.g., 1.234567 msec).
	 */
	float Toc() const;

private:
	double TimeStart;
};
