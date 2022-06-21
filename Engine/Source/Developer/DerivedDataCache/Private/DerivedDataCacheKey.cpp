// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKey.h"

#include "Containers/Set.h"
#include "Containers/StringConv.h"
#include "DerivedDataCachePrivate.h"
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

template <typename CharType>
static inline void AssertValidCacheBucketName(TStringView<CharType> Name)
{
	checkf(FCacheBucket::IsValidName(Name),
		TEXT("A cache bucket name must be alphanumeric, non-empty, and contain at most %d code units. Name: '%s'"),
		*WriteToString<256>(Name), FCacheBucket::MaxNameLen);
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
