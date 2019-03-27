// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WmfPrivate.h"

#include "HAL/ThreadSafeBool.h"
#include "GameplayMediaEncoderSample.h"

class FWmfRingBuffer
{
public:
	FTimespan GetMaxDuration() const
	{ return MaxDuration; }

	void SetMaxDuration(FTimespan InMaxDuration)
	{ MaxDuration = InMaxDuration; }

	FTimespan GetDuration() const;

	void Push(const FGameplayMediaEncoderSample& Sample);

	void PauseCleanup(bool bPause);

	TArray<FGameplayMediaEncoderSample> GetCopy();

	void Reset();

private:
	void Cleanup();

private:
	FTimespan MaxDuration = 0;
	TArray<FGameplayMediaEncoderSample> Samples;
	FCriticalSection Mutex;
	FThreadSafeBool bCleanupPaused = false;
};

