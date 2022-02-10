// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Misc/AsciiSet.h"

class FDerivedDataCacheInterface;
class FQueuedThreadPool;

template <typename FuncType> class TUniqueFunction;

namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class ICache; }
namespace UE::DerivedData { class IRequestOwner; }
namespace UE::DerivedData { struct FCacheKey; }

namespace UE::DerivedData::Private
{

// Created by CreateCache
extern FQueuedThreadPool* GCacheThreadPool;

// Implemented in DerivedDataCache.cpp
ICache* CreateCache(FDerivedDataCacheInterface** OutLegacyCache);

// Implemented in DerivedDataCacheRecord.cpp
uint64 GetCacheRecordCompressedSize(const FCacheRecord& Record);
uint64 GetCacheRecordTotalRawSize(const FCacheRecord& Record);
uint64 GetCacheRecordRawSize(const FCacheRecord& Record);

// Implemented in DerivedDataCacheStoreAsync.cpp
void ExecuteInCacheThreadPool(
	IRequestOwner& Owner,
	TUniqueFunction<void (IRequestOwner& Owner, bool bCancel)>&& Function);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename CharType>
inline bool IsValidCacheBucketName(TStringView<CharType> Name)
{
	constexpr FAsciiSet Valid("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
	return !Name.IsEmpty() && Name.Len() < 256 && FAsciiSet::HasOnly(Name, Valid);
}

} // UE::DerivedData::Private
