// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
#include "DerivedDataPayload.h"
#include "IO/IoHash.h"
#include "Templates/TypeHash.h"

#define UE_API DERIVEDDATACACHE_API

class FCbObjectId;

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** An alphanumeric identifier that groups related cache records. */
class FCacheBucket
{
public:
	/** Construct a null cache bucket. */
	FCacheBucket() = default;

	/**
	 * Construct a cache bucket from the name.
	 *
	 * A cache bucket name must be alphanumeric, non-empty, and contain fewer than 256 characters.
	 */
	inline explicit FCacheBucket(FStringView Bucket)
		: Name(FindOrAddCacheBucket(Bucket))
	{
	}

	/** Convert the name of the cache bucket to a string. */
	UE_API void ToString(FAnsiStringBuilderBase& Builder) const;
	UE_API void ToString(FWideStringBuilderBase& Builder) const;
	inline FStringView ToString() const;

	/** Whether this is null. */
	inline bool IsNull() const { return !Name; }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }

	/** Reset this to null. */
	inline void Reset() { *this = FCacheBucket(); }

	inline bool operator==(FCacheBucket Other) const { return Name == Other.Name; }
	inline bool operator!=(FCacheBucket Other) const { return Name != Other.Name; }

	inline bool operator<(FCacheBucket Other) const
	{
		return Name != Other.Name && ToString().Compare(Other.ToString(), ESearchCase::IgnoreCase) < 0;
	}

	friend inline uint32 GetTypeHash(FCacheBucket Bucket) { return ::GetTypeHash(reinterpret_cast<UPTRINT>(Bucket.Name)); }

private:
	UE_API static const TCHAR* FindOrAddCacheBucket(FStringView Bucket);

	const TCHAR* Name = nullptr;
};

inline FStringView FCacheBucket::ToString() const
{
	return Name ? FStringView(Name, *reinterpret_cast<const uint8*>(Name - 1)) : FStringView();
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCacheBucket& Bucket)
{
	Bucket.ToString(Builder);
	return Builder;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A key that uniquely identifies a cache record. */
class FCacheKey
{
public:
	/** Construct a null cache key. */
	FCacheKey() = default;

	/** Construct a cache key from the non-null bucket and non-zero hash. */
	inline FCacheKey(FCacheBucket InBucket, const FIoHash& InHash)
		: Bucket(InBucket)
		, Hash(InHash)
	{
	}

	/** Returns the bucket component of the cache key. */
	inline FCacheBucket GetBucket() const { return Bucket; }

	/** Returns the hash component of the cache key. */
	inline const FIoHash& GetHash() const { return Hash; }

	/** Convert the cache key to a string. */
	UE_API void ToString(FAnsiStringBuilderBase& Builder) const;
	UE_API void ToString(FWideStringBuilderBase& Builder) const;
	UE_API FString ToString() const;

	/** Whether this is null. */
	inline bool IsNull() const { return Bucket.IsNull(); }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }

	/** Reset this to null. */
	inline void Reset() { *this = FCacheKey(); }

private:
	FCacheBucket Bucket;
	FIoHash Hash;
};

inline bool operator==(const FCacheKey& A, const FCacheKey& B)
{
	return A.GetBucket() == B.GetBucket() && A.GetHash() == B.GetHash();
}

inline bool operator!=(const FCacheKey& A, const FCacheKey& B)
{
	return A.GetBucket() != B.GetBucket() || A.GetHash() != B.GetHash();
}

inline bool operator<(const FCacheKey& A, const FCacheKey& B)
{
	const FCacheBucket& BucketA = A.GetBucket();
	const FCacheBucket& BucketB = B.GetBucket();
	return BucketA == BucketB ? A.GetHash() < B.GetHash() : BucketA < BucketB;
}

inline uint32 GetTypeHash(const FCacheKey& Key)
{
	return HashCombine(GetTypeHash(Key.GetBucket()), GetTypeHash(Key.GetHash()));
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCacheKey& Key)
{
	Key.ToString(Builder);
	return Builder;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A key that uniquely identifies a payload within a cache record. */
class FCachePayloadKey
{
public:
	/** Construct a null cache payload key. */
	FCachePayloadKey() = default;

	/** Construct a cache payload key from the non-null key and non-null ID. */
	inline FCachePayloadKey(const FCacheKey& InKey, const FPayloadId& InId)
		: Key(InKey)
		, Id(InId)
	{
	}

	/** Returns the cache key for the record containing the payload. */
	inline const FCacheKey& GetKey() const { return Key; }

	/** Returns the ID of the payload. */
	inline const FPayloadId& GetId() const { return Id; }

	/** Convert the cache payload key to a string. */
	UE_API void ToString(FAnsiStringBuilderBase& Builder) const;
	UE_API void ToString(FWideStringBuilderBase& Builder) const;
	UE_API FString ToString() const;

	/** Whether this is null. */
	inline bool IsNull() const { return Key.IsNull(); }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }

	/** Reset this to null. */
	inline void Reset() { *this = FCachePayloadKey(); }

private:
	FCacheKey Key;
	FPayloadId Id;
};

inline bool operator==(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	return A.GetKey() == B.GetKey() && A.GetId() == B.GetId();
}

inline bool operator!=(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	return A.GetKey() != B.GetKey() || A.GetId() != B.GetId();
}

inline bool operator<(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	const FCacheKey& KeyA = A.GetKey();
	const FCacheKey& KeyB = B.GetKey();
	return KeyA == KeyB ? A.GetId() < B.GetId() : KeyA < KeyB;
}

inline uint32 GetTypeHash(const FCachePayloadKey& Key)
{
	return HashCombine(GetTypeHash(Key.GetKey()), GetTypeHash(Key.GetId()));
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCachePayloadKey& Key)
{
	Key.ToString(Builder);
	return Builder;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

#undef UE_API
