// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

namespace UE
{
namespace DerivedData
{

class FCacheBucket;
class FCacheRecordBuilder;
struct FCacheKey;

namespace Private
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Implemented in DerivedDataCacheKey.cpp
FCacheBucket CreateCacheBucket(FStringView Name);

// Implemented in DerivedDataCacheRecord.cpp
FCacheRecordBuilder CreateCacheRecordBuilder(const FCacheKey& Key);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // Private
} // DerivedData
} // UE
