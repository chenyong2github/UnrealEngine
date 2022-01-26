// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKey.h"

#include "Containers/Set.h"
#include "Containers/StringConv.h"
#include "DerivedDataCachePrivate.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinaryWriter.h"

namespace UE::DerivedData::Private
{

template <typename CharType>
static inline void AssertValidCacheBucketName(TStringView<CharType> Name)
{
	checkf(IsValidCacheBucketName(Name),
		TEXT("A cache bucket name must be alphanumeric, non-empty, and contain fewer than 256 code units. ")
		TEXT("Name: '%s'"), *WriteToString<256>(Name));
}

class FCacheBucketOwner : public FCacheBucket
{
public:
	inline explicit FCacheBucketOwner(FAnsiStringView Bucket);
	inline FCacheBucketOwner(FCacheBucketOwner&& Bucket);
	inline ~FCacheBucketOwner();

	FCacheBucketOwner(const FCacheBucketOwner&) = delete;
	FCacheBucketOwner& operator=(const FCacheBucketOwner&) = delete;

	using FCacheBucket::operator==;

	inline bool operator==(FAnsiStringView Bucket) const { return ToString().Equals(Bucket, ESearchCase::IgnoreCase); }

	friend inline uint32 GetTypeHash(const FCacheBucketOwner& Bucket)
	{
		return ::GetTypeHash(Bucket.ToString());
	}
};

inline FCacheBucketOwner::FCacheBucketOwner(FAnsiStringView Bucket)
{
	const int32 BucketLen = Bucket.Len();
	const int32 PrefixSize = FMath::DivideAndRoundUp<int32>(1, sizeof(ANSICHAR));
	ANSICHAR* Buffer = new ANSICHAR[PrefixSize + BucketLen + 1];
	Buffer += PrefixSize;
	Name = Buffer;

	reinterpret_cast<uint8*>(Buffer)[LengthOffset] = static_cast<uint8>(BucketLen);
	Bucket.CopyString(Buffer, BucketLen);
	Buffer += BucketLen;
	*Buffer = '\0';
}

inline FCacheBucketOwner::FCacheBucketOwner(FCacheBucketOwner&& Bucket)
	: FCacheBucket(Bucket)
{
	Bucket.Reset();
}

inline FCacheBucketOwner::~FCacheBucketOwner()
{
	if (Name)
	{
		const int32 PrefixSize = FMath::DivideAndRoundUp<int32>(1, sizeof(ANSICHAR));
		delete[] (Name - PrefixSize);
	}
}

class FCacheBuckets
{
public:
	template <typename CharType>
	inline FCacheBucket FindOrAdd(TStringView<CharType> Name);

private:
	FRWLock Lock;
	TSet<FCacheBucketOwner> Buckets;
};

template <typename CharType>
inline FCacheBucket FCacheBuckets::FindOrAdd(TStringView<CharType> Name)
{
	const auto AnsiCast = StringCast<ANSICHAR>(Name.GetData(), Name.Len());
	const FAnsiStringView AnsiName = AnsiCast;

	const uint32 Hash = GetTypeHash(AnsiName);
	if (FReadScopeLock ReadLock(Lock); const FCacheBucketOwner* Bucket = Buckets.FindByHash(Hash, AnsiName))
	{
		return *Bucket;
	}

	// It is valid to assert after because the "bogus char" '?' is not valid in a bucket name.
	AssertValidCacheBucketName(Name);

	FCacheBucketOwner LocalBucket(AnsiName);
	FWriteScopeLock WriteLock(Lock);
	return Buckets.FindOrAddByHash(Hash, MoveTemp(LocalBucket));
}

static FCacheBuckets& GetCacheBuckets()
{
	static FCacheBuckets Buckets;
	return Buckets;
}

} // UE::DerivedData::Private

namespace UE::DerivedData
{

FCacheBucket::FCacheBucket(FUtf8StringView InName)
	: FCacheBucket(Private::GetCacheBuckets().FindOrAdd(InName))
{
}

FCacheBucket::FCacheBucket(FWideStringView InName)
	: FCacheBucket(Private::GetCacheBuckets().FindOrAdd(InName))
{
}

FCbWriter& operator<<(FCbWriter& Writer, const FCacheKey& Key)
{
	Writer.BeginObject();
	Writer.AddString("Bucket"_ASV, Key.Bucket.ToString());
	Writer.AddHash("Hash"_ASV, Key.Hash);
	Writer.EndObject();
	return Writer;
}


} // UE::DerivedData
