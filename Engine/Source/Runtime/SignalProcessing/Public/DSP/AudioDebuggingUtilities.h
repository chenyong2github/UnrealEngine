// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "BufferVectorOperations.h"

#define USE_AUDIO_DEBUGGING UE_BUILD_DEBUG

void BreakWhenAudible(float* InBuffer, int32 NumSamples)
{
	static const float AudibilityThreshold = Audio::ConvertToLinear(-40.0f);

	float BufferAmplitude = Audio::BufferGetAverageAbsValue(InBuffer, NumSamples);

	if (BufferAmplitude > AudibilityThreshold)
	{
		PLATFORM_BREAK();
	}
}

void BreakWhenTooLoud(float* InBuffer, int32 NumSamples)
{
	static const float PainThreshold = Audio::ConvertToLinear(3.0f);

	float BufferAmplitude = Audio::BufferGetAverageAbsValue(InBuffer, NumSamples);

	if (BufferAmplitude > PainThreshold)
	{
		PLATFORM_BREAK();
	}
}


#if USE_AUDIO_DEBUGGING
#define BREAK_WHEN_AUDIBLE(Ptr, Num) BreakWhenAudible(Ptr, Num);
#define BREAK_WHEN_TOO_LOUD(Ptr, Num) BreakWhenTooLoud(Ptr, Num);
#else
#define BREAK_WHEN_AUDIBLE(Ptr, Num) 
#define BREAK_WHEN_TOO_LOUD(Ptr, Num)
#endif