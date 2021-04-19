// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
#include "DerivedDataPayload.h"
#include "IO/IoHash.h"
#include "Templates/TypeHash.h"

namespace UE::DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * An alphanumeric identifier that groups related cache records.
 *
 * Create using ICache::CreateBucket.
 */
class FCacheBucket
{
public:
	/** Construct a null cache bucket. */
	FCacheBucket() = default;

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
		return Name != Other.Name && ToString<TCHAR>().Compare(Other.ToString<TCHAR>(), ESearchCase::IgnoreCase) < 0;
	}

	friend inline uint32 GetTypeHash(FCacheBucket Bucket)
	{
		return ::GetTypeHash(reinterpret_cast<UPTRINT>(Bucket.Name));
	}

	/** Get the name of the cache bucket as a string. */
	template <typename CharType>
	inline TStringView<CharType> ToString() const;
	template <>
	inline FStringView ToString<TCHAR>() const;
	template <>
	inline FAnsiStringView ToString<ANSICHAR>() const;

protected:
	explicit FCacheBucket(const TCHAR* InName)
		: Name(InName)
	{
	}

	static constexpr int32 LengthOffset = -1;
	static constexpr int32 LengthOffset8 = -2;

private:
	/**
	 * Name as TCHAR followed by the name as UTF-8, both null-terminated.
	 *
	 * The byte preceding the TCHAR name is the number of TCHAR code units excluding the terminator.
	 * The byte preceding the TCHAR name length is the number of UTF-8 code units.
	 */
	const TCHAR* Name = nullptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A key that uniquely identifies a cache record. */
struct FCacheKey
{
	FCacheBucket Bucket;
	FIoHash Hash;

	/** A key with a null bucket and a zero hash. */
	static const FCacheKey Empty;
};

inline const FCacheKey FCacheKey::Empty;

/** A key that uniquely identifies a payload within a cache record. */
struct FCachePayloadKey
{
	FCacheKey CacheKey;
	FPayloadId Id;

	/** A payload key with an empty cache key and a null payload identifier. */
	static const FCachePayloadKey Empty;
};

inline const FCachePayloadKey FCachePayloadKey::Empty;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <>
inline FStringView FCacheBucket::ToString<TCHAR>() const
{
	return Name ? FStringView(Name, reinterpret_cast<const uint8*>(Name)[LengthOffset]) : FStringView();
}

template <>
inline FAnsiStringView FCacheBucket::ToString<ANSICHAR>() const
{
	return Name ? FAnsiStringView(
		reinterpret_cast<const ANSICHAR*>(Name + reinterpret_cast<const uint8*>(Name)[LengthOffset] + 1),
		reinterpret_cast<const uint8*>(Name)[LengthOffset8]) : FAnsiStringView();
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCacheBucket& Bucket)
{
	return Builder << Bucket.ToString<CharType>();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool operator==(const FCacheKey& A, const FCacheKey& B)
{
	return A.Bucket == B.Bucket && A.Hash == B.Hash;
}

inline bool operator!=(const FCacheKey& A, const FCacheKey& B)
{
	return A.Bucket != B.Bucket || A.Hash != B.Hash;
}

inline bool operator<(const FCacheKey& A, const FCacheKey& B)
{
	const FCacheBucket& BucketA = A.Bucket;
	const FCacheBucket& BucketB = B.Bucket;
	return BucketA == BucketB ? A.Hash < B.Hash : BucketA < BucketB;
}

inline uint32 GetTypeHash(const FCacheKey& Key)
{
	return HashCombine(GetTypeHash(Key.Bucket), GetTypeHash(Key.Hash));
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCacheKey& Key)
{
	return Builder << Key.Bucket << CharType('/') << Key.Hash;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool operator==(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	return A.CacheKey == B.CacheKey && A.Id == B.Id;
}

inline bool operator!=(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	return A.CacheKey != B.CacheKey || A.Id != B.Id;
}

inline bool operator<(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	const FCacheKey& KeyA = A.CacheKey;
	const FCacheKey& KeyB = B.CacheKey;
	return KeyA == KeyB ? A.Id < B.Id : KeyA < KeyB;
}

inline uint32 GetTypeHash(const FCachePayloadKey& Key)
{
	return HashCombine(GetTypeHash(Key.CacheKey), GetTypeHash(Key.Id));
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCachePayloadKey& Key)
{
	return Builder << Key.CacheKey << CharType('/') << Key.Id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::DerivedData
