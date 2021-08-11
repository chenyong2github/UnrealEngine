// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY_STATIC(LogNeuralNetworkInference, Display, All);
DECLARE_STATS_GROUP(TEXT("MachineLearning"), STATGROUP_MachineLearning, STATCAT_Advanced);

class FNeuralNetworkInferenceLegacyUtils
{
public:
	/**
	 * It blocks the current thread until the RHI thread has finished all instructions before this point.
	 * @returns The time it waited.
	 */
	static void WaitUntilRHIFinished();
};
