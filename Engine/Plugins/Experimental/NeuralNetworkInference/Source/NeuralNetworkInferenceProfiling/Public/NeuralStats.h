// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/CircularBuffer.h"


struct NEURALNETWORKINFERENCEPROFILING_API FNNISampleStatData
{
	FNNISampleStatData() {};

	FNNISampleStatData(const float InInferenceTime)
		: InferenceTime(InInferenceTime)	
	{}

	float InferenceTime = 0;
};

struct NEURALNETWORKINFERENCEPROFILING_API FNNIStatsData
{
	FNNIStatsData() {}
	
	FNNIStatsData(const int InNumberSamples, const float InAverage, const float InStdDev, const float InMin, const float InMax)
		: NumberSamples(InNumberSamples)
		, Average(InAverage)
		, StdDev(InStdDev)
		, Min(InMin)
		, Max(InMax)
		{}

	uint32 NumberSamples = 0;
	float Average = 0;
	float StdDev = 0;
	float Min = 0;
	float Max = 0;
};

struct NEURALNETWORKINFERENCEPROFILING_API FNNIStatsMinMax
{
	float Min = FLT_MAX;
	float Max = FLT_MIN;
};


class NEURALNETWORKINFERENCEPROFILING_API FNeuralStats
{

public:

	FNeuralStats(const uint32 InSizeRollingWindow = 1024);
	
	void StoreSample(const float InRunTime);
	void ResetStats();
	void SetSizeRollingWindow(const uint32 InSizeRollingWindow);

	float GetLastSample();
	FNNIStatsData GetStats();

private:

	uint32 BufferIdx = 0;
	bool bIsBufferFull = false;
	uint32 SizeRollingWindow = 1024;
	const float EpsilonFloat = 0.000001;
	float LastSample;
	TCircularBuffer<float> DataBuffer;

	float CalculateMean();
	float CalculateStdDev(const float InMean);
	FNNIStatsMinMax CalculateMinMax();
	
};


