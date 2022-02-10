// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataCacheStore.h"
#include "HAL/CriticalSection.h"
#include "Logging/LogMacros.h"
#include "Templates/RefCounting.h"
#include <atomic>

class FDerivedDataCacheStatsNode;

namespace UE::DerivedData { class FLegacyCacheKey; }
namespace UE::DerivedData { struct FBackendDebugOptions; }
namespace UE::DerivedData { struct FLegacyCacheDeleteRequest; }
namespace UE::DerivedData { struct FLegacyCacheDeleteResponse; }
namespace UE::DerivedData { struct FLegacyCacheGetRequest; }
namespace UE::DerivedData { struct FLegacyCacheGetResponse; }
namespace UE::DerivedData { struct FLegacyCachePutRequest; }
namespace UE::DerivedData { struct FLegacyCachePutResponse; }
namespace UE::DerivedData::Private { class FLegacyCacheValueShared; }

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
		FOnLegacyCachePutComplete&& OnComplete);

	virtual void LegacyGet(
		TConstArrayView<FLegacyCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnLegacyCacheGetComplete&& OnComplete);

	virtual void LegacyDelete(
		TConstArrayView<FLegacyCacheDeleteRequest> Requests,
		IRequestOwner& Owner,
		FOnLegacyCacheDeleteComplete&& OnComplete);

	virtual void LegacyStats(FDerivedDataCacheStatsNode& OutNode) = 0;

	virtual bool LegacyDebugOptions(FBackendDebugOptions& Options) = 0;
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

class Private::FLegacyCacheValueShared final
{
public:
	explicit FLegacyCacheValueShared(const FValue& Value);
	explicit FLegacyCacheValueShared(const FCompositeBuffer& RawData);

	inline bool HasData() const { return Value.HasData() || RawData; }
	const FValue& GetValue();
	const FCompositeBuffer& GetRawData();
	FIoHash GetRawHash() const;
	uint64 GetRawSize() const;

	inline void AddRef()
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release()
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

private:
	FValue Value;
	FCompositeBuffer RawData;
	std::atomic<uint32> ReferenceCount{0};
	FRWLock Lock;
};

class FLegacyCacheValue
{
public:
	FLegacyCacheValue() = default;
	explicit FLegacyCacheValue(const FValue& Value);
	explicit FLegacyCacheValue(const FCompositeBuffer& RawData);

	inline void Reset() { Shared.SafeRelease(); }

	[[nodiscard]] inline explicit operator bool() const { return !IsNull(); }
	[[nodiscard]] inline bool IsNull() const { return !Shared; }

	[[nodiscard]] bool HasData() const { return Shared ? Shared->HasData() : false; }
	[[nodiscard]] const FValue& GetValue() const { return Shared ? Shared->GetValue() : FValue::Null; }
	[[nodiscard]] const FCompositeBuffer& GetRawData() const { return Shared ? Shared->GetRawData() : FCompositeBuffer::Null; }
	[[nodiscard]] inline FIoHash GetRawHash() const { return Shared ? Shared->GetRawHash() : FIoHash(); }
	[[nodiscard]] inline uint64 GetRawSize() const { return Shared ? Shared->GetRawSize() : 0; }

private:
	TRefCountPtr<Private::FLegacyCacheValueShared> Shared;
};

struct FLegacyCachePutRequest
{
	FSharedString Name;
	FLegacyCacheKey Key;
	FLegacyCacheValue Value;
	ECachePolicy Policy = ECachePolicy::Default;
	uint64 UserData = 0;

	FLegacyCachePutResponse MakeResponse(EStatus Status) const;
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

	FLegacyCacheGetResponse MakeResponse(EStatus Status) const;
};

struct FLegacyCacheGetResponse
{
	FSharedString Name;
	FLegacyCacheKey Key;
	FLegacyCacheValue Value;
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

	FLegacyCacheDeleteResponse MakeResponse(EStatus Status) const;
};

struct FLegacyCacheDeleteResponse
{
	FSharedString Name;
	FLegacyCacheKey Key;
	uint64 UserData = 0;
	EStatus Status = EStatus::Error;
};

} // UE::DerivedData
