// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstallBundleCache.h"
#include "InstallBundleManagerPrivatePCH.h"

#define INSTALLBUNDLE_CACHE_CHECK_INVARIANTS (DO_CHECK && 0)
#define INSTALLBUNDLE_CACHE_DUMP_INFO (0)

FInstallBundleCache::~FInstallBundleCache()
{
}

void FInstallBundleCache::Init(FInstallBundleCacheInitInfo InitInfo)
{
	CacheName = InitInfo.CacheName;
	TotalSize = InitInfo.Size;
}

void FInstallBundleCache::AddOrUpdateBundle(EInstallBundleSourceType Source, const FInstallBundleCacheBundleInfo& AddInfo)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_AddOrUpdateBundle);

	FPerSourceBundleCacheInfo& Info = PerSourceCacheInfo.FindOrAdd(AddInfo.BundleName).FindOrAdd(Source);
	Info.FullInstallSize = AddInfo.FullInstallSize;
	Info.CurrentInstallSize = AddInfo.CurrentInstallSize;
	Info.TimeStamp = AddInfo.TimeStamp;

	UpdateCacheInfoFromSourceInfo(AddInfo.BundleName);

	CheckInvariants();
}

void FInstallBundleCache::RemoveBundle(EInstallBundleSourceType Source, FName BundleName)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_RemoveBundle);

	TMap<EInstallBundleSourceType, FPerSourceBundleCacheInfo>* SourcesMap = PerSourceCacheInfo.Find(BundleName);
	if (SourcesMap)
	{
		SourcesMap->Remove(Source);

		UpdateCacheInfoFromSourceInfo(BundleName);

		CheckInvariants();
	}
}

TOptional<FInstallBundleCacheBundleInfo> FInstallBundleCache::GetBundleInfo(EInstallBundleSourceType Source, FName BundleName)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_GetBundleInfo);

	TOptional<FInstallBundleCacheBundleInfo> Ret;

	TMap<EInstallBundleSourceType, FPerSourceBundleCacheInfo>* SourcesMap = PerSourceCacheInfo.Find(BundleName);
	if (SourcesMap)
	{
		FPerSourceBundleCacheInfo* SourceInfo = SourcesMap->Find(Source);
		if (SourceInfo)
		{
			FInstallBundleCacheBundleInfo& OutInfo = Ret.Emplace();
			OutInfo.BundleName = BundleName;
			OutInfo.FullInstallSize = SourceInfo->FullInstallSize;
			OutInfo.CurrentInstallSize = SourceInfo->CurrentInstallSize;
			OutInfo.TimeStamp = SourceInfo->TimeStamp;
		}
	}

	return Ret;
}

uint64 FInstallBundleCache::GetSize() const
{
	return TotalSize;
}

uint64 FInstallBundleCache::GetUsedSize() const
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_GetUsedSize);

	uint64 UsedSize = 0;
	for (const TPair<FName, FBundleCacheInfo>& Pair : CacheInfo)
	{
		UsedSize += Pair.Value.GetSize();
	}

	return UsedSize;
}

uint64 FInstallBundleCache::GetFreeSpaceInternal(uint64 UsedSize) const
{
	if (UsedSize > TotalSize)
		return 0;

	return TotalSize - UsedSize;
}

uint64 FInstallBundleCache::GetFreeSpace() const
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_GetFreeSpace);

	uint64 UsedSize = GetUsedSize();
	return GetFreeSpaceInternal(UsedSize);
}

FInstallBundleCacheReserveResult FInstallBundleCache::Reserve(FName BundleName)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_Reserve);

	FInstallBundleCacheReserveResult Result;

	FBundleCacheInfo* BundleInfo = CacheInfo.Find(BundleName);
	if (BundleInfo == nullptr)
	{
		Result.Result = EInstallBundleCacheReserveResult::Success;
		return Result;
	}

	if (BundleInfo->State == ECacheState::PendingEvict)
	{
		Result.Result = EInstallBundleCacheReserveResult::Fail_PendingEvict;
		return Result;
	}

	if (BundleInfo->State == ECacheState::Reserved)
	{
		Result.Result = EInstallBundleCacheReserveResult::Success;
		return Result;
	}

	if (BundleInfo->FullInstallSize <= BundleInfo->CurrentInstallSize)
	{
		BundleInfo->State = ECacheState::Reserved;
		Result.Result = EInstallBundleCacheReserveResult::Success;
		return Result;
	}

	const uint64 SizeNeeded = BundleInfo->FullInstallSize - BundleInfo->CurrentInstallSize;
	const uint64 UsedSize = GetUsedSize();
	if(TotalSize >= UsedSize + SizeNeeded)
	{
		BundleInfo->State = ECacheState::Reserved;
		Result.Result = EInstallBundleCacheReserveResult::Success;

		return Result;
	}

	Result.Result = EInstallBundleCacheReserveResult::Fail_NeedsEvict;

	// TODO: Bundles that have BundleSize > 0 or are PendingEvict should be 
	// sorted to the beginning.  We should be able to stop iterating sooner in that case.
	CacheInfo.ValueSort([](const FBundleCacheInfo& A, const FBundleCacheInfo& B)
	{
		if (A.bHintReqeusted == B.bHintReqeusted)
		{
			return A.TimeStamp < B.TimeStamp;
		}
		
		return !A.bHintReqeusted && B.bHintReqeusted;
	});

	uint64 CanFreeSpace = 0;
	for (const TPair<FName, FBundleCacheInfo>& Pair : CacheInfo)
	{
		if(Pair.Key == BundleName)
			continue;

		if(Pair.Value.State == ECacheState::Reserved)
			continue;

		uint64 BundleSize = Pair.Value.GetSize();
		if (BundleSize > 0)
		{
			check(UsedSize >= CanFreeSpace);
			if (TotalSize < UsedSize - CanFreeSpace + SizeNeeded)
			{
				CanFreeSpace += BundleSize;
				TArray<EInstallBundleSourceType>& SourcesToEvictFrom = Result.BundlesToEvict.Add(Pair.Key);
				PerSourceCacheInfo.FindChecked(Pair.Key).GenerateKeyArray(SourcesToEvictFrom);
			}
		}
		else if (Pair.Value.State == ECacheState::PendingEvict)
		{
			// Bundle manager must wait for all previous pending evictions to complete
			// to ensure that there is actually enough free space in the cache
			// before installing a bundle
			TArray<EInstallBundleSourceType>& SourcesToEvictFrom = Result.BundlesToEvict.Add(Pair.Key);
			PerSourceCacheInfo.FindChecked(Pair.Key).GenerateKeyArray(SourcesToEvictFrom);
		}
	}

	check(UsedSize >= CanFreeSpace);
	if (TotalSize < UsedSize - CanFreeSpace + SizeNeeded)
	{
		Result.Result = EInstallBundleCacheReserveResult::Fail_CacheFull;
	}
	else
	{
		check(Result.BundlesToEvict.Num() > 0);
	}

#if INSTALLBUNDLE_CACHE_DUMP_INFO
	GetStats(true);
#endif // INSTALLBUNDLE_CACHE_DUMP_INFO

	return Result;
}

bool FInstallBundleCache::Release(FName BundleName)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_Release);

	FBundleCacheInfo* BundleInfo = CacheInfo.Find(BundleName);
	if (BundleInfo == nullptr)
	{
		return true;
	}

	if (BundleInfo->State == ECacheState::Released)
	{
		return true;
	}

	if (BundleInfo->State == ECacheState::Reserved || 
		BundleInfo->State == ECacheState::PendingEvict)
	{
		BundleInfo->State = ECacheState::Released;
		return true;
	}

	return false;
}

bool FInstallBundleCache::SetPendingEvict(FName BundleName)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_SetPendingEvict);

	FBundleCacheInfo* BundleInfo = CacheInfo.Find(BundleName);
	if (BundleInfo == nullptr)
	{
		return true;
	}

	if (BundleInfo->State == ECacheState::PendingEvict)
	{
		return true;
	}

	if (BundleInfo->State == ECacheState::Released)
	{
		BundleInfo->State = ECacheState::PendingEvict;
		return true;
	}

	return false;
}

void FInstallBundleCache::HintRequested(FName BundleName, bool bRequested)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_HintRequested);

	FBundleCacheInfo* BundleInfo = CacheInfo.Find(BundleName);
	if (BundleInfo)
	{
		BundleInfo->bHintReqeusted = bRequested;
	}
}

void FInstallBundleCache::CheckInvariants() const
{
#if INSTALLBUNDLE_CACHE_CHECK_INVARIANTS

	check(PerSourceCacheInfo.Num() == CacheInfo.Num());

	for (const TPair<FName, FBundleCacheInfo>& CachePair : CacheInfo)
	{
		const TMap<EInstallBundleSourceType, FPerSourceBundleCacheInfo>* SourcesMap = PerSourceCacheInfo.Find(CachePair.Key);
		check(SourcesMap);

		uint64 FullInstallSize = 0;
		uint64 CurrentInstallSize = 0;
		for (const TPair<EInstallBundleSourceType, FPerSourceBundleCacheInfo>& Pair : *SourcesMap)
		{
			FullInstallSize += Pair.Value.FullInstallSize;
			CurrentInstallSize += Pair.Value.CurrentInstallSize;
		}

		check(CachePair.Value.FullInstallSize == FullInstallSize);
		check(CachePair.Value.CurrentInstallSize == CurrentInstallSize);
	}

#endif // INSTALLBUNDLE_CACHE_CHECK_INVARIANTS
}

FInstallBundleCacheStats FInstallBundleCache::GetStats(bool bDumpToLog /*= false*/) const
{
	FInstallBundleCacheStats Stats;
	Stats.CacheName = CacheName;
	Stats.MaxSize = TotalSize;

	if (bDumpToLog)
	{
		UE_LOG(LogInstallBundleManager, Display, TEXT("\n"));
		UE_LOG(LogInstallBundleManager, Display, TEXT("*Install Bundle Cache Stats %s"), *CacheName.ToString());
	}

	for (const TPair<FName, FBundleCacheInfo>& CachePair : CacheInfo)
	{
		const FBundleCacheInfo& Info = CachePair.Value;

		Stats.UsedSize += Info.GetSize();

		if (Info.State == ECacheState::Reserved)
		{
			Stats.ReservedSize += Info.CurrentInstallSize;
		}

		if (bDumpToLog && (Info.CurrentInstallSize > 0 || Info.State != ECacheState::Released))
		{
			UE_LOG(LogInstallBundleManager, Verbose, TEXT("*\tbundle %s"), *CachePair.Key.ToString());
			UE_LOG(LogInstallBundleManager, Verbose, TEXT("*\t\tfull size: %" UINT64_FMT), Info.FullInstallSize);
			UE_LOG(LogInstallBundleManager, Verbose, TEXT("*\t\tcurrent size: %" UINT64_FMT), Info.CurrentInstallSize);
			UE_LOG(LogInstallBundleManager, Verbose, TEXT("*\t\treserved: %s"), (Info.State == ECacheState::Reserved) ? TEXT("true") : TEXT("false"));
			UE_LOG(LogInstallBundleManager, Verbose, TEXT("*\t\ttimestamp: %s"), *Info.TimeStamp.ToString());
		}
	}

	Stats.FreeSize = GetFreeSpaceInternal(Stats.UsedSize);

	if (bDumpToLog)
	{
		UE_LOG(LogInstallBundleManager, Display, TEXT("*\tsize: %" UINT64_FMT), Stats.MaxSize);
		UE_LOG(LogInstallBundleManager, Display, TEXT("*\tused: %" UINT64_FMT), Stats.UsedSize);
		UE_LOG(LogInstallBundleManager, Display, TEXT("*\treserved: %" UINT64_FMT), Stats.ReservedSize);
		UE_LOG(LogInstallBundleManager, Display, TEXT("*\tfree: %" UINT64_FMT), Stats.FreeSize);
		UE_LOG(LogInstallBundleManager, Display, TEXT("\n"));
	}

	return Stats;
}

void FInstallBundleCache::UpdateCacheInfoFromSourceInfo(FName BundleName)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, FInstallBundleCache_UpdateCacheInfoFromSourceInfo);

	TMap<EInstallBundleSourceType, FPerSourceBundleCacheInfo>* SourcesMap = PerSourceCacheInfo.Find(BundleName);
	if (SourcesMap == nullptr)
	{
		CacheInfo.Remove(BundleName);
		return;
	}

	if (SourcesMap->Num() == 0)
	{
		PerSourceCacheInfo.Remove(BundleName);
		CacheInfo.Remove(BundleName);
		return;
	}

	FDateTime TimeStamp = FDateTime::MinValue();
	uint64 FullInstallSize = 0;
	uint64 CurrentInstallSize = 0;
	for (const TPair<EInstallBundleSourceType, FPerSourceBundleCacheInfo>& Pair : *SourcesMap)
	{
		FullInstallSize += Pair.Value.FullInstallSize;
		CurrentInstallSize += Pair.Value.CurrentInstallSize;
		if (Pair.Value.CurrentInstallSize > 0 && Pair.Value.TimeStamp > TimeStamp)
		{
			TimeStamp = Pair.Value.TimeStamp;
		}
	}

	FBundleCacheInfo& BundleCacheInfo = CacheInfo.FindOrAdd(BundleName);
	checkf(BundleCacheInfo.FullInstallSize == FullInstallSize || BundleCacheInfo.State != ECacheState::Reserved, TEXT("Bundle %s: FullInstallSize should not be updated while a bundle is Reserved!"), *BundleName.ToString());

	BundleCacheInfo.FullInstallSize = FullInstallSize;
	BundleCacheInfo.CurrentInstallSize = CurrentInstallSize;
	BundleCacheInfo.TimeStamp = TimeStamp;
}
