// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef TRACK_DISK_UTILIZATION
#define TRACK_DISK_UTILIZATION 0
#endif

#include "CoreTypes.h"

#if TRACK_DISK_UTILIZATION
#include "HAL/PlatformMisc.h"
#include "HAL/ThreadSafeBool.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include <nn/nn_Log.h>

CSV_DECLARE_CATEGORY_EXTERN(DiskIO);

#ifndef SPEW_DISK_UTILIZATION
#define SPEW_DISK_UTILIZATION 0
#endif // SPEW_DISK_UTILIZATION

struct FDiskUtilizationTracker
{
	struct UtilizationStats
	{
		UtilizationStats() :
			TotalReads(0),
			TotalSeeks(0),
			TotalBytesRead(0),
			TotalSeekDistance(0),
			TotalIOTime(0.0),
			TotalIdleTime(0.0)
		{}

		double GetOverallThroughputMBS() const
		{
			return double(TotalBytesRead) / (TotalIOTime + TotalIdleTime) / (1024 * 1024);
		}

		double GetReadThrougputMBS() const
		{
			return double(TotalBytesRead) / (TotalIOTime) / (1024 * 1024);
		}

		double GetTotalIdleTimeInSeconds() const
		{
			return TotalIdleTime;
		}

		double GetTotalIOTimeInSeconds() const
		{
			return TotalIOTime;
		}

		double GetPercentTimeIdle() const
		{
			double TotalTime = TotalIOTime + TotalIdleTime;

			return TotalTime > 0.0 ? (100.0f * TotalIdleTime) / TotalTime : 0.0;
		}

		void Reset()
		{
			TotalReads = 0;
			TotalSeeks = 0;
			TotalBytesRead = 0;
			TotalSeekDistance = 0;
			TotalIOTime = 0.0;
			TotalIdleTime = 0.0;
		}

		void Dump() const;

		uint64 TotalReads;
		uint64 TotalSeeks;

		uint64 TotalBytesRead;
		uint64 TotalSeekDistance;

		double TotalIOTime;
		double TotalIdleTime;
	};

	UtilizationStats LongTermStats;
	UtilizationStats ShortTermStats;

	FCriticalSection CriticalSection;

	uint64 IdleStartCycle;
	uint64 ReadStartCycle;

	uint64 InFlightBytes;
	int32  InFlightReads;

	FThreadSafeBool bResetShortTermStats;

	FDiskUtilizationTracker() :
		IdleStartCycle(0),
		ReadStartCycle(0),
		InFlightBytes(0),
		InFlightReads(0)
	{
	}

	void StartRead(uint64 InReadBytes, uint64 InSeekDistance = 0)
	{
		static bool bBreak = false;

		bool bReset = bResetShortTermStats.AtomicSet(false);

		if (bReset)
		{
			ShortTermStats.Reset();
			bBreak = true;
		}

		// update total reads
		LongTermStats.TotalReads++;
		ShortTermStats.TotalReads++;

		// update seek data
		if (InSeekDistance > 0)
		{
			LongTermStats.TotalSeeks++;
			ShortTermStats.TotalSeeks++;

			LongTermStats.TotalSeekDistance += InSeekDistance;
			ShortTermStats.TotalSeekDistance += InSeekDistance;
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
					const double IdleTime = double(ReadStartCycle - IdleStartCycle) * FPlatformTime::GetSecondsPerCycle64();

					LongTermStats.TotalIdleTime += IdleTime;
					ShortTermStats.TotalIdleTime += bReset ? 0 : IdleTime;

					CSV_CUSTOM_STAT(DiskIO, AccumulatedIdleTime, float(IdleTime), ECsvCustomStatOp::Accumulate);
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
				const double IOTime = double(IdleStartCycle - ReadStartCycle) * FPlatformTime::GetSecondsPerCycle64();

				LongTermStats.TotalIOTime += IOTime;
				ShortTermStats.TotalIOTime += IOTime;

				LongTermStats.TotalBytesRead += InFlightBytes;
				ShortTermStats.TotalBytesRead += InFlightBytes;

				CSV_CUSTOM_STAT(DiskIO, AccumulatedIOTime, float(IOTime), ECsvCustomStatOp::Accumulate);

				InFlightBytes = 0;
			}

		}
		MaybePrint();
	}

	uint32 GetOutstandingRequests() const
	{
		return InFlightReads;
	}

	struct UtilizationStats& GetLongTermStats()
	{
		return LongTermStats;
	}

	struct UtilizationStats& GetShortTermStats()
	{
		return ShortTermStats;
	}

	void ResetShortTermStats()
	{
		bResetShortTermStats = true;
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