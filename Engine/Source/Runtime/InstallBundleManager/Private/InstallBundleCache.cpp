// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstallBundleCache.h"
#include "InstallBundleManagerPrivatePCH.h"

#define INSTALLBUNDLE_CACHE_CHECK_INVARIANTS (DO_CHECK && 1)

FInstallBundleCache::~FInstallBundleCache()
{
}

void FInstallBundleCache::Init(FInstallBundleCacheInitInfo InitInfo)
{
	CacheName = InitInfo.CacheName;
	TotalSize = InitInfo.Size;
	DeleteBundleFiles = MoveTemp(InitInfo.DeleteBundleFiles);

	check(DeleteBundleFiles.IsBound());
}

void FInstallBundleCache::AddOrUpdateBundle(const FInstallBundleCacheAddBundleInfo& AddInfo)
{
	FBundleCacheInfo& BundleCacheInfo = CacheInfo.FindOrAdd(AddInfo.BundleName);
	BundleCacheInfo.FullInstallSize = AddInfo.FullInstallSize;
	BundleCacheInfo.CurrentInstallSize = AddInfo.FullInstallSize;

	CheckInvariants();
}

void FInstallBundleCache::RemoveBundle(FName BundleName)
{
	CacheInfo.Remove(BundleName);
}

bool FInstallBundleCache::HasBundle(FName BundleName)
{
	return CacheInfo.Contains(BundleName);
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
		Result.Result = EInstallBundleCacheReserveResult::Failure;
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

	uint64 SizeNeeded = BundleInfo->FullInstallSize - BundleInfo->CurrentInstallSize;
	uint64 FreeSpace = GetFreeSpace();
	if (FreeSpace >= SizeNeeded)
	{
		BundleInfo->State = ECacheState::Reserved;
		Result.Result = EInstallBundleCacheReserveResult::Success;
		return Result;
	}

	Result.Result = EInstallBundleCacheReserveResult::NeedsEvict;

	// TODO: Should search in LRU order
	// TODO: LRU sort should consider bHintReqeusted

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
			CanFreeSpace += Pair.Value.GetSize();
			Result.BundlesToEvict.Add(Pair.Key);
		}

		if(CanFreeSpace >= SizeNeeded)
			break;
	}

	if (CanFreeSpace < SizeNeeded)
	{
		Result.Result = EInstallBundleCacheReserveResult::Failure;
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

	// TODO

#endif // INSTALLBUNDLE_CACHE_CHECK_INVARIANTS
}
