// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/CircularBuffer.h"

struct NEURALNETWORKINFERENCEPROFILING_API FNeuralStatsData
{
	const uint32 NumberSamples;
	const float Average;
	const float StdDev;
	const float Min;
	const float Max;

	FNeuralStatsData(const int InNumberSamples, const float InAverage, const float InStdDev, const float InMin, const float InMax);
};

class NEURALNETWORKINFERENCEPROFILING_API FNeuralStats
{
public:
	FNeuralStats(const uint32 InSizeRollingWindow = 1024);
	
	void StoreSample(const float InRunTime);
	void ResetStats();

	float GetLastSample() const;
	FNeuralStatsData GetStats() const;

private:
	const float EpsilonFloat;
	uint32 BufferIdx;
	bool bIsBufferFull;
	float LastSample;
	TCircularBuffer<float> DataBuffer;

	float CalculateMean() const;
	float CalculateStdDev(const float InMean) const;
};
