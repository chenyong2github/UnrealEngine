// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKey.h"

#include "Algo/AllOf.h"
#include "Containers/Set.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ScopeRWLock.h"

namespace UE
{
namespace DerivedData
{
namespace Private
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheBuckets
{
public:
	inline const TCHAR* FindOrAdd(FStringView Name16, FAnsiStringView Name8)
	{
		const uint32 Hash = GetTypeHash(Name16);
		{
			FReadScopeLock ReadLock(Lock);
			if (const FBucket* Bucket = Buckets.FindByHash(Hash, Name16))
			{
				return Bucket->GetName();
			}
		}
		{
			FWriteScopeLock WriteLock(Lock);
			const FBucket& Bucket = Buckets.FindOrAddByHash(Hash, FBucket(Name16, Name8));
			return Bucket.GetName();
		}
	}

private:
	class FBucket
	{
	public:
		inline FBucket(FStringView Bucket, FAnsiStringView Bucket8)
		{
			const int32 BucketLen = Bucket.Len();
			const int32 Bucket8Len = Bucket8.Len();
			const int32 PrefixSize = FMath::DivideAndRoundUp<int32>(2, sizeof(TCHAR));
			TCHAR* Buffer = new TCHAR[PrefixSize + BucketLen + 1 + FMath::DivideAndRoundUp<int32>(Bucket8Len + 1, sizeof(TCHAR))];
			Buffer += PrefixSize;
			Name = Buffer;

			reinterpret_cast<uint8*>(Buffer)[-2] = static_cast<uint8>(BucketLen);
			reinterpret_cast<uint8*>(Buffer)[-1] = static_cast<uint8>(Bucket8Len);

			Bucket.CopyString(Buffer, BucketLen);
			Buffer += BucketLen;
			*Buffer++ = TEXT('\0');

			ANSICHAR* Buffer8 = reinterpret_cast<ANSICHAR*>(Buffer);
			Bucket8.CopyString(Buffer8, Bucket8Len);
			Buffer8 += Bucket8Len;
			*Buffer8++ = '\0';
		}

		FBucket(const FBucket&) = delete;
		FBucket& operator=(const FBucket&) = delete;

		inline FBucket(FBucket&& Bucket)
			: Name(Bucket.Name)
		{
			Bucket.Name = nullptr;
		}

		FBucket& operator=(FBucket&& Bucket)
		{
			Name = Bucket.Name;
			Bucket.Name = nullptr;
			return *this;
		}

		inline ~FBucket()
		{
			if (Name)
			{
				const int32 PrefixSize = FMath::DivideAndRoundUp<int32>(2, sizeof(TCHAR));
				delete[] (Name - PrefixSize);
			}
		}

		inline FStringView ToString() const
		{
			return FStringView(Name, reinterpret_cast<const uint8*>(Name)[-2]);
		}

		inline bool operator==(const FBucket& Bucket) const
		{
			return Name == Bucket.Name;
		}

		inline bool operator==(FStringView Bucket) const
		{
			return ToString().Equals(Bucket, ESearchCase::IgnoreCase);
		}

		friend inline uint32 GetTypeHash(const FBucket& Bucket)
		{
			return GetTypeHash(Bucket.ToString());
		}

		inline const TCHAR* GetName() const { return Name; }

	private:
		const TCHAR* Name;
	};

	FRWLock Lock;
	TSet<FBucket> Buckets;
};

FCacheBucket CreateCacheBucket(FStringView Name)
{
	FTCHARToUTF8 Name8(Name);
	checkf(!Name.IsEmpty() && Name.Len() < 256 && Name8.Length() < 256 && Algo::AllOf(Name, FChar::IsAlnum),
		TEXT("Invalid cache bucket name '%.*s' must be alphanumeric, non-empty, and contain fewer than 256 code units."),
		Name.Len(), Name.GetData());
	static FCacheBuckets Buckets;
	return FCacheBucket(FCacheBucketName{Buckets.FindOrAdd(Name, Name8)});
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // Private
} // DerivedData
} // UE
