// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
#include "IO/IoHash.h"
#include "Templates/TypeHash.h"

#define UE_API DERIVEDDATACACHE_API

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

	/** Construct a cache bucket from the alphanumeric name. Names are case-insensitive. */
	UE_API explicit FCacheBucket(FStringView Name);

	/** Append the name of the cache bucket to the string builder. */
	UE_API void ToString(FAnsiStringBuilderBase& Builder) const;
	UE_API void ToString(FWideStringBuilderBase& Builder) const;

	/** Whether the cache bucket is null. */
	inline bool IsNull() const { return !Index; }
	/** Whether the cache bucket is not null. */
	inline explicit operator bool() const { return !IsNull(); }

	/** Returns the index that is only stable within this process. */
	inline uint32 ToIndex() const { return Index; }

	/** Returns the bucket for the index produced within this process. */
	static inline FCacheBucket FromIndex(uint32 InIndex)
	{
		FCacheBucket Bucket;
		Bucket.Index = InIndex;
		return Bucket;
	}

	/** Reset this to null. */
	inline void Reset() { *this = FCacheBucket(); }

private:
	uint32 Index = 0;
};

inline bool operator==(FCacheBucket A, FCacheBucket B)
{
	return A.ToIndex() == B.ToIndex();
}

inline uint32 GetTypeHash(FCacheBucket Bucket)
{
	return ::GetTypeHash(Bucket.ToIndex());
}

/** Append the cache bucket name to the string builder. */
template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCacheBucket& Bucket)
{
	Bucket.ToString(Builder);
	return Builder;
}

/** Fast less with an order that is only stable within the lifetime of this process. */
struct FCacheBucketFastLess
{
	inline bool operator()(FCacheBucket A, FCacheBucket B) const
	{
		return A.ToIndex() < B.ToIndex();
	}
};

/** Slow less with lexical order that is stable between processes. */
struct FCacheBucketLexicalLess
{
	UE_API bool operator()(FCacheBucket A, FCacheBucket B) const;
};

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

	/** Append the cache key to the string builder. */
	UE_API void ToString(FAnsiStringBuilderBase& Builder) const;
	UE_API void ToString(FWideStringBuilderBase& Builder) const;

	/** Whether the cache key is null. */
	inline bool IsNull() const { return Bucket.IsNull(); }
	/** Whether the cache key is not null. */
	inline explicit operator bool() const { return !IsNull(); }

	/** Reset this to null. */
	void Reset() { *this = FCacheKey(); }

private:
	FCacheBucket Bucket;
	FIoHash Hash;
};

inline bool operator==(const FCacheKey& A, const FCacheKey& B)
{
	return A.GetBucket() == B.GetBucket() && A.GetHash() == B.GetHash();
}

inline uint32 GetTypeHash(const FCacheKey& Key)
{
	return HashCombine(GetTypeHash(Key.GetBucket()), GetTypeHash(Key.GetHash()));
}

/** Append the cache key to the string builder. */
template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCacheKey& Key)
{
	Key.ToString(Builder);
	return Builder;
}

/** Fast less with an order that is only stable within the lifetime of this process. */
struct FCacheKeyFastLess
{
	inline bool operator()(const FCacheKey& A, const FCacheKey& B) const
	{
		const FCacheBucket BucketA = A.GetBucket();
		const FCacheBucket BucketB = B.GetBucket();
		return BucketA == BucketB ? A.GetHash() < B.GetHash() : FCacheBucketFastLess()(BucketA, BucketB);
	}
};

/** Slow less with lexical order that is stable between processes. */
struct FCacheKeyLexicalLess
{
	inline bool operator()(const FCacheKey& A, const FCacheKey& B) const
	{
		const FCacheBucket BucketA = A.GetBucket();
		const FCacheBucket BucketB = B.GetBucket();
		return BucketA == BucketB ? A.GetHash() < B.GetHash() : FCacheBucketLexicalLess()(BucketA, BucketB);
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A key that uniquely identifies an attachment within a cache record. */
class FCacheAttachmentKey
{
public:
	/** Construct a null cache attachment key. */
	FCacheAttachmentKey() = default;

	/** Construct a cache attachment key from the non-null key and non-zero hash. */
	inline FCacheAttachmentKey(const FCacheKey& InKey, const FIoHash& InHash)
		: Key(InKey)
		, Hash(InHash)
	{
	}

	/** Returns the cache key for the record containing the attachment. */
	inline const FCacheKey& GetKey() const { return Key; }

	/** Returns the hash of the attachment. */
	inline const FIoHash& GetHash() const { return Hash; }

	/** Append the cache attachment key to the string builder. */
	UE_API void ToString(FAnsiStringBuilderBase& Builder) const;
	UE_API void ToString(FWideStringBuilderBase& Builder) const;

	/** Whether the cache attachment key is null. */
	inline bool IsNull() const { return Key.IsNull(); }
	/** Whether the cache attachment key is not null. */
	inline explicit operator bool() const { return !IsNull(); }

	/** Reset this to null. */
	void Reset() { *this = FCacheAttachmentKey(); }

private:
	FCacheKey Key;
	FIoHash Hash;
};

inline bool operator==(const FCacheAttachmentKey& A, const FCacheAttachmentKey& B)
{
	return A.GetKey() == B.GetKey() && A.GetHash() == B.GetHash();
}

inline uint32 GetTypeHash(const FCacheAttachmentKey& Key)
{
	return HashCombine(GetTypeHash(Key.GetKey()), GetTypeHash(Key.GetHash()));
}

/** Append the cache attachment key to the string builder. */
template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCacheAttachmentKey& Key)
{
	Key.ToString(Builder);
	return Builder;
}

/** Fast less with an order that is only stable within the lifetime of this process. */
struct FCacheAttachmentKeyFastLess
{
	inline bool operator()(const FCacheAttachmentKey& A, const FCacheAttachmentKey& B) const
	{
		const FCacheKey& KeyA = A.GetKey();
		const FCacheKey& KeyB = B.GetKey();
		return KeyA == KeyB ? A.GetHash() < B.GetHash() : FCacheKeyFastLess()(KeyA, KeyB);
	}
};

/** Slow less with lexical order that is stable between processes. */
struct FCacheAttachmentKeyLexicalLess
{
	inline bool operator()(const FCacheAttachmentKey& A, const FCacheAttachmentKey& B) const
	{
		const FCacheKey& KeyA = A.GetKey();
		const FCacheKey& KeyB = B.GetKey();
		return KeyA == KeyB ? A.GetHash() < B.GetHash() : FCacheKeyLexicalLess()(KeyA, KeyB);
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

#undef UE_API
