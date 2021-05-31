// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "Misc/AsciiSet.h"

class FCbPackage;
class FDerivedDataCacheInterface;
enum class ECachePolicy : uint8;

namespace UE::DerivedData { class FCacheBucket; }
namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class FCacheRecordBuilder; }
namespace UE::DerivedData { class FOptionalCacheRecord; }
namespace UE::DerivedData { struct FCacheKey; }

namespace UE::DerivedData::Private
{

// Implemented in DerivedDataCache.cpp
FDerivedDataCacheInterface* CreateCache();

// Implemented in DerivedDataCacheKey.cpp
FCacheBucket CreateCacheBucket(FStringView Name);

// Implemented in DerivedDataCacheRecord.cpp
FCacheRecordBuilder CreateCacheRecordBuilder(const FCacheKey& Key);
FCbPackage SaveCacheRecord(const FCacheRecord& Record);
FOptionalCacheRecord LoadCacheRecord(const FCbPackage& Package);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool IsValidCacheBucketName(FStringView Name)
{
	constexpr FAsciiSet Valid("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
	return !Name.IsEmpty() && Name.Len() < 256 && FAsciiSet::HasOnly(Name, Valid);
}

inline void AssertValidCacheBucketName(FStringView Name)
{
	checkf(IsValidCacheBucketName(Name),
		TEXT("A cache bucket name must be alphanumeric, non-empty, and contain fewer than 256 code units. ")
		TEXT("Name: '%.*s'"), Name.Len(), Name.GetData());
}

} // UE::DerivedData::Private
