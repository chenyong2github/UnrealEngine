// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKey.h"

#include "Containers/Set.h"
#include "Containers/StringConv.h"
#include "DerivedDataCachePrivate.h"
#include "Hash/xxhash.h"
#include "IO/IoHash.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryWriter.h"
#include "String/Find.h"

namespace UE::DerivedData::Private
{

class FCacheBucketOwner : public FCacheBucket
{
public:
	inline explicit FCacheBucketOwner(FUtf8StringView Bucket);
	inline FCacheBucketOwner(FCacheBucketOwner&& Bucket);
	inline ~FCacheBucketOwner();

	FCacheBucketOwner(const FCacheBucketOwner&) = delete;
	FCacheBucketOwner& operator=(const FCacheBucketOwner&) = delete;

	using FCacheBucket::operator==;

	inline bool operator==(FUtf8StringView Bucket) const { return ToString().Equals(Bucket, ESearchCase::IgnoreCase); }

	static inline uint32 GetBucketHash(const FUtf8StringView Bucket)
	{
		const int32 Len = Bucket.Len();
		check(Len <= MaxNameLen);
		UTF8CHAR LowerBucket[MaxNameLen];
		UTF8CHAR* LowerBucketIt = LowerBucket;
		for (const UTF8CHAR& Char : Bucket)
		{
			*LowerBucketIt++ = TChar<UTF8CHAR>::ToLower(Char);
		}
		return uint32(FXxHash64::HashBuffer(LowerBucket, Len).Hash);
	}

	friend inline uint32 GetTypeHash(const FCacheBucketOwner& Bucket)
	{
		return GetBucketHash(Bucket.ToString());
	}
};

inline FCacheBucketOwner::FCacheBucketOwner(FUtf8StringView Bucket)
{
	checkf(FCacheBucket::IsValidName(Bucket),
		TEXT("A cache bucket name must be alphanumeric, non-empty, and contain at most %d code units. Name: '%s'"),
		*WriteToString<256>(Bucket), FCacheBucket::MaxNameLen);

	static_assert(sizeof(ANSICHAR) == sizeof(UTF8CHAR));
	const int32 BucketLen = Bucket.Len();
	const int32 PrefixSize = FMath::DivideAndRoundUp<int32>(1, sizeof(ANSICHAR));
	UTF8CHAR* Buffer = new UTF8CHAR[PrefixSize + BucketLen + 1];
	Buffer += PrefixSize;
	Name = reinterpret_cast<ANSICHAR*>(Buffer);

	reinterpret_cast<uint8*>(Buffer)[LengthOffset] = static_cast<uint8>(BucketLen);
	Bucket.CopyString(Buffer, BucketLen);
	Buffer += BucketLen;
	*Buffer = UTF8CHAR('\0');
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
inline FCacheBucket FCacheBuckets::FindOrAdd(const TStringView<CharType> Name)
{
	const auto NameCast = StringCast<UTF8CHAR, FCacheBucket::MaxNameLen + 1>(Name.GetData(), Name.Len());
	const FUtf8StringView NameView = NameCast;
	uint32 Hash = 0;

	if (NameView.Len() <= FCacheBucket::MaxNameLen)
	{
		Hash = FCacheBucketOwner::GetBucketHash(NameView);
		FReadScopeLock ReadLock(Lock);
		if (const FCacheBucketOwner* Bucket = Buckets.FindByHash(Hash, NameView))
		{
			return *Bucket;
		}
	}

	FCacheBucketOwner LocalBucket(NameView);
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

FCbWriter& operator<<(FCbWriter& Writer, const FCacheBucket Bucket)
{
	Writer.AddString(Bucket.ToString());
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FCacheBucket& OutBucket)
{
	if (const FUtf8StringView Bucket = Field.AsString(); !Field.HasError() && FCacheBucket::IsValidName(Bucket))
	{
		OutBucket = FCacheBucket(Bucket);
		return true;
	}
	OutBucket.Reset();
	return false;
}

FCbWriter& operator<<(FCbWriter& Writer, const FCacheKey& Key)
{
	Writer.BeginObject();
	Writer << ANSITEXTVIEW("Bucket") << Key.Bucket;
	Writer << ANSITEXTVIEW("Hash") << Key.Hash;
	Writer.EndObject();
	return Writer;
}

bool LoadFromCompactBinary(const FCbFieldView Field, FCacheKey& OutKey)
{
	bool bOk = Field.IsObject();
	bOk &= LoadFromCompactBinary(Field[ANSITEXTVIEW("Bucket")], OutKey.Bucket);
	bOk &= LoadFromCompactBinary(Field[ANSITEXTVIEW("Hash")], OutKey.Hash);
	return bOk;
}

FCacheKey ConvertLegacyCacheKey(const FStringView Key)
{
	FTCHARToUTF8 Utf8Key(Key);
	TUtf8StringBuilder<64> Utf8Bucket;
	Utf8Bucket << ANSITEXTVIEW("Legacy");
	if (const int32 BucketEnd = String::FindFirstChar(Utf8Key, '_'); BucketEnd != INDEX_NONE)
	{
		Utf8Bucket << FUtf8StringView(Utf8Key).Left(BucketEnd);
	}
	const FCacheBucket Bucket(Utf8Bucket);
	return {Bucket, FIoHash::HashBuffer(MakeMemoryView(Utf8Key))};
}

} // UE::DerivedData
