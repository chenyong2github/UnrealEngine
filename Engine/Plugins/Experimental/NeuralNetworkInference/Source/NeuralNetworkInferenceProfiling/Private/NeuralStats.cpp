// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralStats.h"
#include "GenericPlatform/GenericPlatformMath.h"
DEFINE_LOG_CATEGORY_STATIC(LogNNIStats, Display, All);


FNeuralStats::FNeuralStats(const uint32 InSizeRollingWindow) 
: DataBuffer(InSizeRollingWindow, 0)
{
	SizeRollingWindow = InSizeRollingWindow;
	BufferIdx = 0;
	bIsBufferFull = false;
}
	
void FNeuralStats::StoreSample(const float InRunTime) 
{
	LastSample = InRunTime;

	DataBuffer[BufferIdx] = LastSample;

	if(BufferIdx == DataBuffer.Capacity() && !bIsBufferFull)
	{
		bIsBufferFull = true;
	}
	BufferIdx = DataBuffer.GetNextIndex(BufferIdx);

}

float FNeuralStats::GetLastSample()
{
	return LastSample;
}

FNNIStatsData FNeuralStats::GetStats()
{
	const uint32 count = (bIsBufferFull) ? DataBuffer.Capacity() : BufferIdx;
	const float Mean = CalculateMean();
	const float StdDev = CalculateStdDev(Mean);
	FNNIStatsMinMax MinMaxData = CalculateMinMax();

	FNNIStatsData AggregatedData(count,	Mean, StdDev, MinMaxData.Min, MinMaxData.Max);

	return AggregatedData;
}

void FNeuralStats::SetSizeRollingWindow(const uint32 InSizeRollingWindow) 
{
	DataBuffer = TCircularBuffer<float>(InSizeRollingWindow, 0);
	ResetStats();
}

float FNeuralStats::CalculateMean() 
{
	uint32 PtrEnd = (bIsBufferFull) ? DataBuffer.Capacity(): BufferIdx;
	float AccumulatorMean = 0;

	for (uint32 Index = 0; Index < PtrEnd; ++Index)
	{
		AccumulatorMean += DataBuffer[Index];
	}

	const float Mean = AccumulatorMean / (static_cast<float>(PtrEnd) + EpsilonFloat);

	return Mean;
}

float FNeuralStats::CalculateStdDev(const float InMean)
{
	uint32 PtrEnd = (bIsBufferFull) ? DataBuffer.Capacity() : BufferIdx;

	float AccumulatorStdDev = 0;

	for (uint32 Index = 0; Index < PtrEnd; ++Index)
	{
		AccumulatorStdDev += (DataBuffer[Index] - InMean) * (DataBuffer[Index] - InMean);
	}

	const float StdDev = FMath::Sqrt(AccumulatorStdDev / (static_cast<float>(PtrEnd) + EpsilonFloat));

	return StdDev;
}

FNNIStatsMinMax FNeuralStats::CalculateMinMax()
{
	FNNIStatsMinMax MinMaxData;
	uint32 PtrEnd = (bIsBufferFull) ? DataBuffer.Capacity() : BufferIdx;

	for (uint32 Index = 0; Index < PtrEnd; ++Index)
	{
		const float CurrentElement = DataBuffer[Index];
		if( CurrentElement < MinMaxData.Min)
		{
			MinMaxData.Min = CurrentElement;
		}
		if (CurrentElement > MinMaxData.Max)
		{
			MinMaxData.Max = CurrentElement;
		}
	}

	return MinMaxData;
}

void FNeuralStats::ResetStats()
{
	for (uint32 Index = 0; Index < DataBuffer.Capacity(); ++Index)
	{
		DataBuffer[Index] = 0;
	}
	bIsBufferFull = false;
	BufferIdx = 0;
	LastSample = 0;
}
