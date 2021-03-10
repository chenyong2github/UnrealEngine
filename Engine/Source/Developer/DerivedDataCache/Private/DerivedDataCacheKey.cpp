// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKey.h"

#include "Algo/AllOf.h"
#include "Containers/Set.h"
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
	inline const TCHAR* FindOrAdd(FStringView Name)
	{
		const uint32 Hash = GetTypeHash(Name);
		{
			FReadScopeLock ReadLock(Lock);
			if (const FBucket* Bucket = Buckets.FindByHash(Hash, Name))
			{
				return Bucket->GetName();
			}
		}
		{
			FWriteScopeLock WriteLock(Lock);
			const FBucket& Bucket = Buckets.FindOrAddByHash(Hash, FBucket(Name));
			return Bucket.GetName();
		}
	}

private:
	class FBucket
	{
	public:
		inline explicit FBucket(FStringView Bucket)
		{
			const int32 BucketLen = Bucket.Len();
			TCHAR* Buffer = new TCHAR[BucketLen + 2];
			*reinterpret_cast<uint8*>(Buffer++) = static_cast<uint8>(BucketLen);
			Bucket.CopyString(Buffer, BucketLen);
			Buffer[BucketLen] = TEXT('\0');
			Name = Buffer;
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
				delete[] (Name - 1);
			}
		}

		inline FStringView ToString() const
		{
			return FStringView(Name, *reinterpret_cast<const uint8*>(Name - 1));
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
	checkf(!Name.IsEmpty() && Name.Len() < 256 && Algo::AllOf(Name, FChar::IsAlnum),
		TEXT("Invalid cache bucket name '%.*s' must be alphanumeric, non-empty, and contain fewer than 256 characters."),
		Name.Len(), Name.GetData());
	static FCacheBuckets Buckets;
	return FCacheBucket(FCacheBucketName{Buckets.FindOrAdd(Name)});
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // Private
} // DerivedData
} // UE
