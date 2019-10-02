// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HAL/DiskUtilizationTracker.h"

#if TRACK_DISK_UTILIZATION
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(DiskIO, true); 

DEFINE_LOG_CATEGORY_STATIC(LogDiskIO, Log, All);

static FAutoConsoleCommand GDumpShortTermIOStats(
	TEXT("disk.DumpShortTermStats"),
	TEXT("Dumps short term disk I/O stats."),
	FConsoleCommandDelegate::CreateLambda([]
	{
		UE_LOG(LogDiskIO, Display, TEXT("Disk I/O short term stats:"));
		GDiskUtilizationTracker.GetShortTermStats().Dump();
	})
);

void FDiskUtilizationTracker::UtilizationStats::Dump() const
{
	UE_LOG(LogDiskIO, Display, TEXT("Total Reads: %llu Total Bytes Read: %llu"), TotalReads, TotalBytesRead);
	UE_LOG(LogDiskIO, Display, TEXT("Total IO Time: %fs Total Idle Time: %fs"), TotalIOTime, TotalIdleTime);
	UE_LOG(LogDiskIO, Display, TEXT("Read Throughput: %fMB/s Pct Time Idle: %f%%"), GetReadThrougputMBS(), GetPercentTimeIdle());
}

void FDiskUtilizationTracker::MaybePrint()
{
#if !UE_BUILD_SHIPPING && SPEW_DISK_UTILIZATION
	static double LastPrintSeconds = 0.0;

	double CurrentSeconds = FPlatformTime::Seconds();

	// if we haven't printed or haven't in a while and there's been some I/O emit stats
	if (((LastPrintSeconds == 0.0) || ((CurrentSeconds - LastPrintSeconds) > PrintFrequencySeconds)) && (TotalIOTime > 0.0))
	{
		{
			// emit recent I/O info
			static uint64 LastReads = 0;
			static uint64 LastBytesRead = 0;

			static double LastIOTime = 0.0;
			static double LastIdleTime = 0.0;

			static uint32 LastSeeks = 0;
			static uint64 LastSeekDistance = 0;

			if ((LastPrintSeconds > 0.0) && (TotalBytesRead > LastBytesRead))
			{
				float TimeInterval = CurrentSeconds - LastPrintSeconds;

				double RecentIOTime = TotalIOTime - LastIOTime;
				double RecentIdleTime = TotalIdleTime - LastIdleTime;

				float Utilization = float(100.0 * RecentIOTime / (RecentIOTime + RecentIdleTime));

				uint64 RecentBytesRead = TotalBytesRead - LastBytesRead;

				double OverallThroughput = double(RecentBytesRead) / (RecentIOTime + RecentIdleTime) / (1024 * 1024);
				double ReadThroughput = double(RecentBytesRead) / RecentIOTime / (1024 * 1024);

				uint32 RecentSeeks = NumSeeks - LastSeeks;
				uint64 RecentSeekDistance = TotalSeekDistance - LastSeekDistance;

				double KBPerSeek = RecentSeeks ? double(RecentBytesRead) / (1024 * RecentSeeks) : 0;
				double AvgSeek = RecentSeeks ? double(RecentSeekDistance) / double(RecentSeeks) : 0;

				uint64 RecentReads = NumReads - LastReads;

				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Recent Disk Utilization: %5.2f%% over %6.2fs\t%.2f MB/s\t%.2f Actual MB/s\t(%d Reads, %d Seeks, %.2f kbytes / seek, %.2f ave seek)\r\n"), 
					Utilization, TimeInterval, OverallThroughput, ReadThroughput, RecentReads, RecentSeeks, KBPerSeek, AvgSeek);
			}

			LastReads = NumReads;
			LastBytesRead = TotalBytesRead;

			LastIOTime = TotalIOTime;
			LastIdleTime = TotalIOTime;

			LastSeeks = NumSeeks;
			LastSeekDistance = TotalSeekDistance;
		}

		{
			// emit recent I/O info
			float Utilization = float(100.0 * TotalIOTime / (TotalIOTime + TotalIdleTime));

			double OverallThroughput = GetOverallThroughputMBS();
			double ReadThroughput = GetReadThrougputMBS();

			double KBPerSeek = NumSeeks ? double(TotalBytesRead) / (1024 * NumSeeks) : 0;
			double AvgSeek = NumSeeks ? double(TotalSeekDistance) / double(NumSeeks) : 0;

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Overall Disk Utilization: %5.2f%%\t%.2f MB/s\t%.2f Actual MB/s\t(%d Reads, %d Seeks, %.2f kbytes / seek, %.2f ave seek)\r\n"),
				Utilization, OverallThroughput, ReadThroughput, NumReads, NumSeeks, KBPerSeek, AvgSeek);
		}
	}

	LastPrintSeconds = CurrentSeconds;

#endif //!UE_BUILD_SHIPPING && SPEW_DISK_UTILIZATION
}

struct FDiskUtilizationTracker GDiskUtilizationTracker;
#endif // TRACK_DISK_UTILIZATION