// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "Misc/AsciiSet.h"

class FDerivedDataCacheInterface;

namespace UE::DerivedData { class ICache; }
namespace UE::DerivedData { struct FCacheKey; }

namespace UE::DerivedData::Private
{

// Implemented in DerivedDataCache.cpp
ICache* CreateCache(FDerivedDataCacheInterface** OutLegacyCache);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename CharType>
inline bool IsValidCacheBucketName(TStringView<CharType> Name)
{
	constexpr FAsciiSet Valid("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
	return !Name.IsEmpty() && Name.Len() < 256 && FAsciiSet::HasOnly(Name, Valid);
}

} // UE::DerivedData::Private
