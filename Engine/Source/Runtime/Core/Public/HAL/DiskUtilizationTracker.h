// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/PlatformMisc.h"

#ifndef TRACK_DISK_UTILIZATION
#define TRACK_DISK_UTILIZATION 0
#endif

#if TRACK_DISK_UTILIZATION

#ifndef SPEW_DISK_UTILIZATION
#define SPEW_DISK_UTILIZATION 0
#endif // SPEW_DISK_UTILIZATION

struct FDiskUtilizationTracker
{
	FCriticalSection CriticalSection;

	uint64 NumReads;
	uint64 NumSeeks;

	uint64 IdleStartCycle;
	uint64 ReadStartCycle;

	uint64 TotalBytesRead;
	uint64 TotalSeekDistance;

	double TotalIOTime;
	double TotalIdleTime;

	uint64 InFlightBytes;
	int32  InFlightReads;

	FDiskUtilizationTracker()
		: NumReads(0)
		, NumSeeks(0)
		, IdleStartCycle(0)
		, ReadStartCycle(0)
		, TotalBytesRead(0)
		, TotalSeekDistance(0)
		, TotalIOTime(0.0)
		, TotalIdleTime(0.0)
		, InFlightBytes(0)
		, InFlightReads(0)
	{
	}

	void StartRead(uint64 InReadBytes, uint64 InSeekDistance = 0)
	{
		// update total reads
		NumReads++;

		// update seek data
		if (InSeekDistance > 0)
		{
			NumSeeks++;
			TotalSeekDistance += InSeekDistance;
		}

		{
			FScopeLock Lock(&CriticalSection);

			if (InFlightReads == 0)
			{
				// if this is the first started read from idle start 
				ReadStartCycle = FPlatformTime::Cycles64();

				// update idle time (if we've been idle)
				if (IdleStartCycle > 0)
				{

					TotalIdleTime += double(ReadStartCycle - IdleStartCycle) * FPlatformTime::GetSecondsPerCycle64();
				}
			}

			InFlightBytes += InReadBytes;
			InFlightReads++;
		}
	}

	void FinishRead()
	{

		// if we're the last in flight read update the start idle counter
		{
			FScopeLock Lock(&CriticalSection);
			check(InFlightReads > 0);

			if (--InFlightReads == 0)
			{
				IdleStartCycle = FPlatformTime::Cycles64();

				// update our read counters
				TotalIOTime += double(IdleStartCycle - ReadStartCycle) * FPlatformTime::GetSecondsPerCycle64();
				TotalBytesRead += InFlightBytes;

				InFlightBytes = 0;
			}

		}

		MaybePrint();
	}

	double GetOverallThroughputMBS()
	{
		return double(TotalBytesRead) / (TotalIOTime + TotalIdleTime) / (1024 * 1024);
	}

	double GetReadThrougputMBS()
	{
		return double(TotalBytesRead) / (TotalIOTime) / (1024 * 1024);
	}

	void ResetStats()
	{
		// make sure we're not reseting stats while a read is in flight. that'd mess things up.
		check(InFlightReads == 0);

		NumSeeks = 0;
		NumReads = 0;
		
		IdleStartCycle = 0;
		ReadStartCycle = 0;

		TotalBytesRead = 0;
		TotalSeekDistance = 0;

		TotalIOTime = 0.0;
		TotalIdleTime = 0.0;

		InFlightBytes = 0;
		InFlightReads = 0;
	}

	static constexpr float PrintFrequencySeconds = 0.5f;

	void MaybePrint();
};

extern FDiskUtilizationTracker GDiskUtilizationTracker;

struct FScopedDiskUtilizationTracker
{
	FScopedDiskUtilizationTracker(uint64 InReadBytes, uint64 InSeekDistance) 
	{
		GDiskUtilizationTracker.StartRead(InReadBytes, InSeekDistance);
	}

	~FScopedDiskUtilizationTracker()
	{
		GDiskUtilizationTracker.FinishRead();
	}
};

#else

struct FScopedDiskUtilizationTracker
{
	FScopedDiskUtilizationTracker(uint64 Size, uint64 SeekDistance)
	{
	}
};

#endif // TRACK_DISK_UTILIZATION