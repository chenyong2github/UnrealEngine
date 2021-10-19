// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceTimer.h"
#include "HAL/PlatformTime.h"

/* FNeuralNetworkInferenceQATimer public functions
 *****************************************************************************/

void FNeuralNetworkInferenceTimer::Tic()
{
	TimeStart = FPlatformTime::Seconds();
}

float FNeuralNetworkInferenceTimer::Toc() const
{
	return (FPlatformTime::Seconds() - TimeStart) * 1e3;
}
