// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "SoundWaveTimecodeInfo.generated.h"

USTRUCT()
struct FSoundWaveTimecodeInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Category = Timecode, VisibleAnywhere)
	uint64 NumSamplesSinceMidnight = ~0UL;

	UPROPERTY(Category = Timecode, VisibleAnywhere)
	uint32 NumSamplesPerSecond = 0;

	UPROPERTY(Category = Timecode, VisibleAnywhere)
	FString Description;

	UPROPERTY(Category = Timecode, VisibleAnywhere)
	FString OriginatorTime;

	UPROPERTY(Category = Timecode, VisibleAnywhere)
	FString OriginatorDate;

	UPROPERTY(Category = Timecode, VisibleAnywhere)
	FString OriginatorDescription;

	UPROPERTY(Category = Timecode, VisibleAnywhere)
	FString OriginatorReference;

	inline bool operator==(const FSoundWaveTimecodeInfo& InRhs) const
	{
		// Note, we don't compare the strings.
		return NumSamplesSinceMidnight == InRhs.NumSamplesSinceMidnight &&
			NumSamplesPerSecond == InRhs.NumSamplesPerSecond;
	}

	inline double GetNumSecondsSinceMidnight() const
	{
		if( NumSamplesSinceMidnight != ~0 && NumSamplesPerSecond > 0)
		{
			return (double)NumSamplesSinceMidnight / NumSamplesPerSecond;
		}
		return 0.0;
	}
};

