// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKey.h"

#include "Algo/AllOf.h"
#include "Containers/Set.h"
#include "DerivedDataCachePrivate.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ScopeRWLock.h"

namespace UE::DerivedData::Private
{

class FCacheBucketOwner : public FCacheBucket
{
public:
	static const TCHAR* AllocName(FStringView Bucket);

	inline explicit FCacheBucketOwner(FStringView Bucket);
	inline FCacheBucketOwner(FCacheBucketOwner&& Bucket);
	inline ~FCacheBucketOwner();

	FCacheBucketOwner(const FCacheBucketOwner&) = delete;
	FCacheBucketOwner& operator=(const FCacheBucketOwner&) = delete;

	using FCacheBucket::operator==;

	inline bool operator==(FStringView Bucket) const { return ToString<TCHAR>().Equals(Bucket, ESearchCase::IgnoreCase); }

	friend inline uint32 GetTypeHash(const FCacheBucketOwner& Bucket)
	{
		return ::GetTypeHash(Bucket.ToString<TCHAR>());
	}
};

inline const TCHAR* FCacheBucketOwner::AllocName(FStringView Bucket)
{
	FTCHARToUTF8 Bucket8Conversion(Bucket);
	FAnsiStringView Bucket8 = Bucket8Conversion;

	const int32 BucketLen = Bucket.Len();
	const int32 Bucket8Len = Bucket8.Len();
	const int32 PrefixSize = FMath::DivideAndRoundUp<int32>(2, sizeof(TCHAR));
	TCHAR* Buffer = new TCHAR[PrefixSize + BucketLen + 1 + FMath::DivideAndRoundUp<int32>(Bucket8Len + 1, sizeof(TCHAR))];
	Buffer += PrefixSize;
	const TCHAR* const Name = Buffer;

	reinterpret_cast<uint8*>(Buffer)[LengthOffset] = static_cast<uint8>(BucketLen);
	reinterpret_cast<uint8*>(Buffer)[LengthOffset8] = static_cast<uint8>(Bucket8Len);

	Bucket.CopyString(Buffer, BucketLen);
	Buffer += BucketLen;
	*Buffer++ = TEXT('\0');

	ANSICHAR* Buffer8 = reinterpret_cast<ANSICHAR*>(Buffer);
	Bucket8.CopyString(Buffer8, Bucket8Len);
	Buffer8 += Bucket8Len;
	*Buffer8++ = '\0';

	return Name;
}

inline FCacheBucketOwner::FCacheBucketOwner(FStringView Bucket)
	: FCacheBucket(AllocName(Bucket))
{
}

inline FCacheBucketOwner::FCacheBucketOwner(FCacheBucketOwner&& Bucket)
	: FCacheBucket(Bucket)
{
	Bucket.Reset();
}

inline FCacheBucketOwner::~FCacheBucketOwner()
{
	if (IsValid())
	{
		const int32 PrefixSize = FMath::DivideAndRoundUp<int32>(2, sizeof(TCHAR));
		delete[] (ToString<TCHAR>().GetData() - PrefixSize);
	}
}

class FCacheBuckets
{
public:
	inline FCacheBucket FindOrAdd(FStringView Name);

private:
	FRWLock Lock;
	TSet<FCacheBucketOwner> Buckets;
};

inline FCacheBucket FCacheBuckets::FindOrAdd(FStringView Name)
{
	const uint32 Hash = GetTypeHash(Name);
	if (FReadScopeLock ReadLock(Lock); const FCacheBucketOwner* Bucket = Buckets.FindByHash(Hash, Name))
	{
		return *Bucket;
	}

	AssertValidCacheBucketName(Name);

	FCacheBucketOwner LocalBucket(Name);
	FWriteScopeLock WriteLock(Lock);
	return Buckets.FindOrAddByHash(Hash, MoveTemp(LocalBucket));
}

FCacheBucket CreateCacheBucket(FStringView Name)
{
	static FCacheBuckets Buckets;
	return Buckets.FindOrAdd(Name);
}

} // UE::DerivedData::Private
