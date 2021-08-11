// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceQATimer.h"
#include "HAL/PlatformTime.h"

/* FNeuralNetworkInferenceQATimer public functions
 *****************************************************************************/

void FNeuralNetworkInferenceQATimer::Tic()
{
	TimeStart = FPlatformTime::Seconds();
}

float FNeuralNetworkInferenceQATimer::Toc() const
{
	return (FPlatformTime::Seconds() - TimeStart) * 1e3;
}
