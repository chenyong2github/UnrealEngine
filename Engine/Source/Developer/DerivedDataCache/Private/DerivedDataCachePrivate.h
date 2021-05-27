// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

class FCbPackage;
class FDerivedDataCacheInterface;
enum class ECachePolicy : uint8;

namespace UE::DerivedData { class FCacheBucket; }
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

} // UE::DerivedData::Private
