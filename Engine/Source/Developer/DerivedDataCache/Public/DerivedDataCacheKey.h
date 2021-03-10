// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "DerivedDataPayload.h"
#include "IO/IoHash.h"
#include "Misc/StringBuilder.h"
#include "Templates/TypeHash.h"

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Private
{

struct FCacheBucketName { const TCHAR* Name; };

}

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

	/** Get the name of the cache bucket as a string. */
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

	friend inline uint32 GetTypeHash(FCacheBucket Bucket)
	{
		return ::GetTypeHash(reinterpret_cast<UPTRINT>(Bucket.Name));
	}

public:
	// Internal API

	explicit FCacheBucket(Private::FCacheBucketName InName)
		: Name(InName.Name)
	{
	}

private:
	/** Name with a one-byte length stored in the preceding character. */
	const TCHAR* Name = nullptr;
};

inline FStringView FCacheBucket::ToString() const
{
	return Name ? FStringView(Name, *reinterpret_cast<const uint8*>(Name - 1)) : FStringView();
}

inline FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const FCacheBucket& Bucket)
{
	return Builder << Bucket.ToString();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A key that uniquely identifies a cache record. */
struct FCacheKey
{
	FCacheBucket Bucket;
	FIoHash Hash;
};

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

inline FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const FCacheKey& Key)
{
	return Builder << Key.Bucket << TEXT('/') << Key.Hash;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A key that uniquely identifies a payload within a cache record. */
struct FCachePayloadKey
{
	FCacheKey Record;
	FPayloadId Payload;
};

inline bool operator==(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	return A.Record == B.Record && A.Payload == B.Payload;
}

inline bool operator!=(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	return A.Record != B.Record || A.Payload != B.Payload;
}

inline bool operator<(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	const FCacheKey& KeyA = A.Record;
	const FCacheKey& KeyB = B.Record;
	return KeyA == KeyB ? A.Payload < B.Payload : KeyA < KeyB;
}

inline uint32 GetTypeHash(const FCachePayloadKey& Key)
{
	return HashCombine(GetTypeHash(Key.Record), GetTypeHash(Key.Payload));
}

inline FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const FCachePayloadKey& Key)
{
	return Builder << Key.Record << TEXT('/') << Key.Payload;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE
