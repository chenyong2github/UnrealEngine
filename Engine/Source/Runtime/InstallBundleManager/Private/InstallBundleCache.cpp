// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstallBundleCache.h"
#include "InstallBundleManagerPrivatePCH.h"

#define INSTALLBUNDLE_CACHE_CHECK_INVARIANTS (DO_CHECK && 0)

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
	FPerSourceBundleCacheInfo& Info = PerSourceCacheInfo.FindOrAdd(AddInfo.BundleName).FindOrAdd(Source);
	Info.FullInstallSize = AddInfo.FullInstallSize;
	Info.CurrentInstallSize = AddInfo.CurrentInstallSize;
	Info.TimeStamp = AddInfo.TimeStamp;

	UpdateCacheInfoFromSourceInfo(AddInfo.BundleName);

	CheckInvariants();
}

void FInstallBundleCache::RemoveBundle(EInstallBundleSourceType Source, FName BundleName)
{
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
		}
	}

	return Ret;
}

uint64 FInstallBundleCache::GetSize() const
{
	return TotalSize;
}

uint64 FInstallBundleCache::GetFreeSpace() const
{
	uint64 UsedSize = 0;
	for (const TPair<FName, FBundleCacheInfo>& Pair : CacheInfo)
	{
		UsedSize += Pair.Value.GetSize();
	}

	if (UsedSize > TotalSize)
		return 0;

	return TotalSize - UsedSize;
}

FInstallBundleCacheReserveResult FInstallBundleCache::Reserve(FName BundleName)
{
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
	const uint64 FreeSpace = GetFreeSpace();
	if (FreeSpace >= SizeNeeded)
	{
		BundleInfo->State = ECacheState::Reserved;
		Result.Result = EInstallBundleCacheReserveResult::Success;
		return Result;
	}

	Result.Result = EInstallBundleCacheReserveResult::Fail_NeedsEvict;

	CacheInfo.ValueSort([](const FBundleCacheInfo& A, const FBundleCacheInfo& B)
	{
		if (A.bHintReqeusted == B.bHintReqeusted)
		{
			return A.TimeStamp < B.TimeStamp;
		}
		
		return !A.bHintReqeusted && B.bHintReqeusted;
	});

	uint64 CanFreeSpace = FreeSpace;
	for (const TPair<FName, FBundleCacheInfo>& Pair : CacheInfo)
	{
		if(Pair.Key == BundleName)
			continue;

		if(Pair.Value.State == ECacheState::Reserved)
			continue;

		uint64 BundleSize = Pair.Value.GetSize();
		if (BundleSize > 0)
		{
			if (CanFreeSpace < SizeNeeded)
			{
				CanFreeSpace += BundleSize;
				PerSourceCacheInfo.FindChecked(Pair.Key).GenerateKeyArray(Result.BundlesToEvict.Add(Pair.Key));
			}
		}
		else if (Pair.Value.State == ECacheState::PendingEvict)
		{
			// Bundle manager must wait for all previous pending evictions to complete
			// to ensure that there is actually enough free space in the cache
			// before installing a bundle
			PerSourceCacheInfo.FindChecked(Pair.Key).GenerateKeyArray(Result.BundlesToEvict.Add(Pair.Key));
		}
	}

	if (CanFreeSpace < SizeNeeded)
	{
		Result.Result = EInstallBundleCacheReserveResult::Fail_CacheFull;
	}
	else
	{
		check(Result.BundlesToEvict.Num() > 0);
	}

	return Result;
}

bool FInstallBundleCache::Release(FName BundleName)
{
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

void FInstallBundleCache::UpdateCacheInfoFromSourceInfo(FName BundleName)
{
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
