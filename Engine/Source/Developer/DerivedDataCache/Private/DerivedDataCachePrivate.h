// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

class FDerivedDataCacheInterface;

namespace UE::DerivedData { class FCacheBucket; }
namespace UE::DerivedData { class FCacheRecordBuilder; }
namespace UE::DerivedData { struct FCacheKey; }

namespace UE::DerivedData::Private
{

// Implemented in DerivedDataCache.cpp
FDerivedDataCacheInterface* CreateCache();

// Implemented in DerivedDataCacheKey.cpp
FCacheBucket CreateCacheBucket(FStringView Name);

// Implemented in DerivedDataCacheRecord.cpp
FCacheRecordBuilder CreateCacheRecordBuilder(const FCacheKey& Key);

} // UE::DerivedData::Private
