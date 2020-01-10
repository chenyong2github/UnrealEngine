// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WmfPrivate.h"

#include "HAL/ThreadSafeBool.h"
#include "AVEncoder.h"

class FWmfRingBuffer
{
public:
	FTimespan GetMaxDuration() const
	{
		return MaxDuration;
	}

	void SetMaxDuration(FTimespan InMaxDuration)
	{
		MaxDuration = InMaxDuration;
	}

	FTimespan GetDuration() const;

	void Push(AVEncoder::FAVPacket&& Sample);

	void PauseCleanup(bool bPause);

	TArray<AVEncoder::FAVPacket> GetCopy();

	void Reset();

private:
	void Cleanup();

private:
	FTimespan MaxDuration = 0;
	TArray<AVEncoder::FAVPacket> Samples;
	FCriticalSection Mutex;
	FThreadSafeBool bCleanupPaused = false;
};

