// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
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

	/** Construct a cache bucket from the alphanumeric name. Names are case-insensitive. */
	UE_API explicit FCacheBucket(FStringView Name);

	/** Convert the name of the cache bucket to a string. */
	UE_API void ToString(FAnsiStringBuilderBase& Builder) const;
	UE_API void ToString(FWideStringBuilderBase& Builder) const;
	UE_API FString ToString() const;

	/** Whether this is null. */
	inline bool IsNull() const { return !Index; }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }

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

/** A 12-byte value that uniquely identifies a cache payload in the scope of a cache record. */
struct FCachePayloadId
{
public:
	using ByteArray = uint8[12];

	/** Construct a null ID. */
	FCachePayloadId() = default;

	/** Construct an ID from a view of 12 bytes. */
	inline explicit FCachePayloadId(FMemoryView Id);

	/** Construct an ID from a non-zero hash. The hash must be unique in the outer cache record. */
	static inline FCachePayloadId FromHash(const FIoHash& Hash);

	/** Construct an ID from a non-empty name. The name must be unique in the outer cache record. */
	UE_API static FCachePayloadId FromName(FAnsiStringView Name);
	UE_API static FCachePayloadId FromName(FWideStringView Name);

	/** Construct an ID from an ObjectId. The ObjectId must be unique in the outer cache record. */
	UE_API static FCachePayloadId FromObjectId(const FCbObjectId& ObjectId);

	/** Convert the ID to an ObjectId. */
	UE_API FCbObjectId ToObjectId() const;

	/** Convert the ID to a 24-character hex string. */
	UE_API void ToString(FAnsiStringBuilderBase& Builder) const;
	UE_API void ToString(FWideStringBuilderBase& Builder) const;
	UE_API FString ToString() const;

	/** Returns a view of the raw byte array for the ID. */
	constexpr inline FMemoryView GetView() const { return MakeMemoryView(Bytes); }

	/** Whether this is null. */
	inline bool IsNull() const;
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }

	/** Reset this to null. */
	inline void Reset() { *this = FCachePayloadId(); }

private:
	alignas(uint32) ByteArray Bytes{};
};

inline bool operator==(const FCachePayloadId& A, const FCachePayloadId& B)
{
	return A.GetView().EqualBytes(B.GetView());
}

inline bool operator!=(const FCachePayloadId& A, const FCachePayloadId& B)
{
	return !A.GetView().EqualBytes(B.GetView());
}

inline bool operator<(const FCachePayloadId& A, const FCachePayloadId& B)
{
	return A.GetView().CompareBytes(B.GetView()) < 0;
}

inline uint32 GetTypeHash(const FCachePayloadId& Id)
{
	return *reinterpret_cast<const uint32*>(Id.GetView().GetData());
}

inline FCachePayloadId::FCachePayloadId(const FMemoryView InId)
{
	checkf(InId.GetSize() == sizeof(ByteArray),
		TEXT("FCachePayloadId cannot be constructed from a view of %" UINT64_FMT " bytes."), InId.GetSize());
	FMemory::Memcpy(Bytes, InId.GetData(), sizeof(ByteArray));
}

inline FCachePayloadId FCachePayloadId::FromHash(const FIoHash& Hash)
{
	return FCachePayloadId(MakeMemoryView(Hash.GetBytes()).Left(sizeof(ByteArray)));
}

inline bool FCachePayloadId::IsNull() const
{
	return *this == FCachePayloadId();
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCachePayloadId& Id)
{
	Id.ToString(Builder);
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
	inline FCachePayloadKey(const FCacheKey& InKey, const FCachePayloadId& InId)
		: Key(InKey)
		, Id(InId)
	{
	}

	/** Returns the cache key for the record containing the payload. */
	inline const FCacheKey& GetKey() const { return Key; }

	/** Returns the ID of the payload. */
	inline const FCachePayloadId& GetId() const { return Id; }

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
	FCachePayloadId Id;
};

inline bool operator==(const FCachePayloadKey& A, const FCachePayloadKey& B)
{
	return A.GetKey() == B.GetKey() && A.GetId() == B.GetId();
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

/** Fast less with an order that is only stable within the lifetime of this process. */
struct FCachePayloadKeyFastLess
{
	inline bool operator()(const FCachePayloadKey& A, const FCachePayloadKey& B) const
	{
		const FCacheKey& KeyA = A.GetKey();
		const FCacheKey& KeyB = B.GetKey();
		return KeyA == KeyB ? A.GetId() < B.GetId() : FCacheKeyFastLess()(KeyA, KeyB);
	}
};

/** Slow less with lexical order that is stable between processes. */
struct FCachePayloadKeyLexicalLess
{
	inline bool operator()(const FCachePayloadKey& A, const FCachePayloadKey& B) const
	{
		const FCacheKey& KeyA = A.GetKey();
		const FCacheKey& KeyB = B.GetKey();
		return KeyA == KeyB ? A.GetId() < B.GetId() : FCacheKeyLexicalLess()(KeyA, KeyB);
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

#undef UE_API
