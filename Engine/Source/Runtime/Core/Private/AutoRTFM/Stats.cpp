// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "Stats.h"

using namespace AutoRTFM;

template<> void AutoRTFM::FStats::Report<EStatsKind::Transaction>(const uint64_t Data) const
{
	UE_LOG(LogAutoRTFM, Display, TEXT("  Total transactions:        %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::Commit>(const uint64_t Data) const
{
	UE_LOG(LogAutoRTFM, Display, TEXT("  Total commits:             %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::Abort>(const uint64_t Data) const
{
	UE_LOG(LogAutoRTFM, Display, TEXT("  Total aborts:              %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::AverageTransactionDepth>(const uint64_t Data) const
{
	const uint64_t TotalTransactions = Datas[static_cast<size_t>(EStatsKind::Transaction)];
	UE_LOG(LogAutoRTFM, Display, TEXT("  Average transaction depth: %15.3f"), (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<EStatsKind::MaximumTransactionDepth>(const uint64_t Data) const
{
    UE_LOG(LogAutoRTFM, Display, TEXT("  Maximum transaction depth: %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::AverageWriteLogEntries>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(EStatsKind::Transaction)];
    UE_LOG(LogAutoRTFM, Display, TEXT("  Average write log entries: %15.3f"), (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<EStatsKind::MaximumWriteLogEntries>(const uint64_t Data) const
{
    UE_LOG(LogAutoRTFM, Display, TEXT("  Maximum write log entries: %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::AverageWriteLogBytes>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(EStatsKind::Transaction)];
    UE_LOG(LogAutoRTFM, Display, TEXT("  Average write log bytes:   %15.3f"), (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<EStatsKind::MaximumWriteLogBytes>(const uint64_t Data) const
{
    UE_LOG(LogAutoRTFM, Display, TEXT("  Maximum write log bytes:   %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::HitSetHit>(const uint64_t Data) const
{
    UE_LOG(LogAutoRTFM, Display, TEXT("  HitSet hits:               %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::HitSetMiss>(const uint64_t Data) const
{
    UE_LOG(LogAutoRTFM, Display, TEXT("  HitSet misses:             %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::HitSetSkippedBecauseOfStackLocalMemory>(const uint64_t Data) const
{
    UE_LOG(LogAutoRTFM, Display, TEXT("  HitSet skip (stack local): %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::AverageCommitTasks>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(EStatsKind::Transaction)];
    UE_LOG(LogAutoRTFM, Display, TEXT("  Average commit tasks:      %15.3f"), (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<EStatsKind::MaximumCommitTasks>(const uint64_t Data) const
{
    UE_LOG(LogAutoRTFM, Display, TEXT("  Maximum commit tasks:      %11u"), Data);
}

template<> void AutoRTFM::FStats::Report<EStatsKind::AverageAbortTasks>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(EStatsKind::Transaction)];
    UE_LOG(LogAutoRTFM, Display, TEXT("  Average abort tasks:       %15.3f"), (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<EStatsKind::MaximumAbortTasks>(const uint64_t Data) const
{
    UE_LOG(LogAutoRTFM, Display, TEXT("  Maximum abort tasks:       %11u"), Data);
}

void AutoRTFM::FStats::Report() const
{
	if constexpr (bCollectStats)
	{
		UE_LOG(LogAutoRTFM, Display, TEXT("AutoRTFM Statistics:"));

		for (size_t I = 0; I < static_cast<size_t>(EStatsKind::Total); I++)
		{
			switch (static_cast<EStatsKind>(I))
			{
			default: AutoRTFM::Unreachable(); break;
#define REPORT_CASE(x) case (x): Report<x>(Datas[static_cast<size_t>(x)]); break
			REPORT_CASE(EStatsKind::Transaction);
			REPORT_CASE(EStatsKind::Commit);
			REPORT_CASE(EStatsKind::Abort);
			REPORT_CASE(EStatsKind::AverageTransactionDepth);
            REPORT_CASE(EStatsKind::MaximumTransactionDepth);
            REPORT_CASE(EStatsKind::AverageWriteLogEntries);
            REPORT_CASE(EStatsKind::MaximumWriteLogEntries);
			REPORT_CASE(EStatsKind::AverageWriteLogBytes);
			REPORT_CASE(EStatsKind::MaximumWriteLogBytes);
            REPORT_CASE(EStatsKind::HitSetHit);
            REPORT_CASE(EStatsKind::HitSetMiss);
            REPORT_CASE(EStatsKind::HitSetSkippedBecauseOfStackLocalMemory);
            REPORT_CASE(EStatsKind::AverageCommitTasks);
            REPORT_CASE(EStatsKind::MaximumCommitTasks);
            REPORT_CASE(EStatsKind::AverageAbortTasks);
            REPORT_CASE(EStatsKind::MaximumAbortTasks);
#undef REPORT_CASE
			}
		}

		UE_DEBUG_BREAK();
	}
}

AutoRTFM::FStats AutoRTFM::Stats;

#endif // defined(__AUTORTFM) && __AUTORTFM
