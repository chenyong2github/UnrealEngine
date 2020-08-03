// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/PlatformMemory.h"

#if ENABLE_MEMORY_SCOPE_STATS

namespace
{
	float CalculateDifference(float Current, float Previous)
	{
		return Current - Previous;
	}

    float BytesToMB(float Bytes)
    {
        return Bytes / (1024.0f * 1024.0f);
    }
}

FScopedMemoryStats::FScopedMemoryStats(const TCHAR* Name)
    : Text(Name)
    , StartStats(FPlatformMemory::GetStats())
{
}

FScopedMemoryStats::~FScopedMemoryStats()
{
    const FPlatformMemoryStats EndStats = FPlatformMemory::GetStats();
    UE_LOG(LogMemory, Log, TEXT("ScopedMemoryStat[%s] UsedPhysical %.02fMB (%+.02fMB), PeakPhysical: %.02fMB (%+.02fMB), UsedVirtual: %.02fMB (%+.02fMB) PeakVirtual: %.02fMB (%+.02fMB)"),
        Text,
        BytesToMB(EndStats.UsedPhysical),
        BytesToMB(CalculateDifference(EndStats.UsedPhysical,     StartStats.UsedPhysical)),
        BytesToMB(EndStats.PeakUsedPhysical),
        BytesToMB(CalculateDifference(EndStats.PeakUsedPhysical, StartStats.PeakUsedPhysical)),
        BytesToMB(EndStats.UsedVirtual),
        BytesToMB(CalculateDifference(EndStats.UsedVirtual,      StartStats.UsedVirtual)),
        BytesToMB(EndStats.PeakUsedVirtual),
        BytesToMB(CalculateDifference(EndStats.PeakUsedVirtual,  StartStats.PeakUsedVirtual))
    );
}
#endif
