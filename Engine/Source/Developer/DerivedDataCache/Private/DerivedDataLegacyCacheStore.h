// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataCache.h"
#include "Logging/LogMacros.h"

namespace UE::DerivedData { class FLegacyCacheKey; }
namespace UE::DerivedData { struct FLegacyCacheDeleteRequest; }
namespace UE::DerivedData { struct FLegacyCacheDeleteResponse; }
namespace UE::DerivedData { struct FLegacyCacheGetRequest; }
namespace UE::DerivedData { struct FLegacyCacheGetResponse; }
namespace UE::DerivedData { struct FLegacyCachePutRequest; }
namespace UE::DerivedData { struct FLegacyCachePutResponse; }

DECLARE_LOG_CATEGORY_EXTERN(LogDerivedDataCache, Log, All);

namespace UE::DerivedData
{

using FOnLegacyCachePutComplete = TUniqueFunction<void (FLegacyCachePutResponse&& Response)>;
using FOnLegacyCacheGetComplete = TUniqueFunction<void (FLegacyCacheGetResponse&& Response)>;
using FOnLegacyCacheDeleteComplete = TUniqueFunction<void (FLegacyCacheDeleteResponse&& Response)>;

class ILegacyCacheStore : public ICacheStore
{
public:
	virtual void LegacyPut(
		TConstArrayView<FLegacyCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnLegacyCachePutComplete&& OnComplete) = 0;

	virtual void LegacyGet(
		TConstArrayView<FLegacyCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnLegacyCacheGetComplete&& OnComplete) = 0;

	virtual void LegacyDelete(
		TConstArrayView<FLegacyCacheDeleteRequest> Requests,
		IRequestOwner& Owner,
		FOnLegacyCacheDeleteComplete&& OnComplete) = 0;
};

class FLegacyCacheKey
{
public:
	FLegacyCacheKey() = default;
	FLegacyCacheKey(FStringView FullKey, int32 MaxKeyLength);

	const FCacheKey& GetKey() const { return Key; }
	const FSharedString& GetFullKey() const { return FullKey; }
	const FSharedString& GetShortKey() const { return ShortKey.IsEmpty() ? FullKey : ShortKey; }
	bool HasShortKey() const { return !ShortKey.IsEmpty(); }

	bool ReadValueTrailer(FCompositeBuffer& Value) const;
	void WriteValueTrailer(FCompositeBuffer& Value) const;

private:
	FCacheKey Key;
	FSharedString FullKey;
	FSharedString ShortKey;
};

struct FLegacyCachePutRequest
{
	FSharedString Name;
	FLegacyCacheKey Key;
	FCompositeBuffer Value;
	ECachePolicy Policy = ECachePolicy::Default;
	uint64 UserData = 0;
};

struct FLegacyCachePutResponse
{
	FSharedString Name;
	FLegacyCacheKey Key;
	uint64 UserData = 0;
	EStatus Status = EStatus::Error;
};

struct FLegacyCacheGetRequest
{
	FSharedString Name;
	FLegacyCacheKey Key;
	ECachePolicy Policy = ECachePolicy::Default;
	uint64 UserData = 0;
};

struct FLegacyCacheGetResponse
{
	FSharedString Name;
	FLegacyCacheKey Key;
	FSharedBuffer Value;
	uint64 UserData = 0;
	EStatus Status = EStatus::Error;
};

struct FLegacyCacheDeleteRequest
{
	FSharedString Name;
	FLegacyCacheKey Key;
	ECachePolicy Policy = ECachePolicy::Default;
	bool bTransient = false;
	uint64 UserData = 0;
};

struct FLegacyCacheDeleteResponse
{
	FSharedString Name;
	FLegacyCacheKey Key;
	uint64 UserData = 0;
	EStatus Status = EStatus::Error;
};

} // UE::DerivedData
