// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Compression/CompressedBuffer.h"
#include "Containers/StringFwd.h"
#include "IO/IoHash.h"
#include "Memory/MemoryView.h"

#define UE_API DERIVEDDATACACHE_API

class FCbObjectId;
class FString;

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A 12-byte value that uniquely identifies a payload in the context that it was created. */
struct FPayloadId
{
public:
	using ByteArray = uint8[12];

	/** Construct a null ID. */
	FPayloadId() = default;

	/** Construct an ID from a view of 12 bytes. */
	inline explicit FPayloadId(FMemoryView Id);

	/** Construct an ID from a non-zero hash. */
	UE_API static FPayloadId FromHash(const FIoHash& Hash);

	/** Construct an ID from a non-empty name. */
	UE_API static FPayloadId FromName(FAnsiStringView Name);
	UE_API static FPayloadId FromName(FWideStringView Name);

	/** Construct an ID from an ObjectId. */
	UE_API static FPayloadId FromObjectId(const FCbObjectId& ObjectId);

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
	inline void Reset() { *this = FPayloadId(); }

private:
	alignas(uint32) ByteArray Bytes{};
};

inline bool operator==(const FPayloadId& A, const FPayloadId& B)
{
	return A.GetView().EqualBytes(B.GetView());
}

inline bool operator!=(const FPayloadId& A, const FPayloadId& B)
{
	return !A.GetView().EqualBytes(B.GetView());
}

inline bool operator<(const FPayloadId& A, const FPayloadId& B)
{
	return A.GetView().CompareBytes(B.GetView()) < 0;
}

inline uint32 GetTypeHash(const FPayloadId& Id)
{
	return *reinterpret_cast<const uint32*>(Id.GetView().GetData());
}

inline FPayloadId::FPayloadId(const FMemoryView InId)
{
	checkf(InId.GetSize() == sizeof(ByteArray),
		TEXT("FPayloadId cannot be constructed from a view of %" UINT64_FMT " bytes."), InId.GetSize());
	FMemory::Memcpy(Bytes, InId.GetData(), sizeof(ByteArray));
}

inline bool FPayloadId::IsNull() const
{
	return *this == FPayloadId();
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FPayloadId& Id)
{
	Id.ToString(Builder);
	return Builder;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A payload is described by a PayloadId and an IoHash of the compressed payload buffer.
 *
 * Payloads may be constructed with or without a compressed buffer. A payload without a buffer is
 * used to uniquely describe a payload without the overhead of its buffer.
 */
class FPayload
{
public:
	/** Construct a null payload. */
	FPayload() = default;

	/** Construct a payload from the hash of a compressed buffer. */
	UE_API FPayload(const FPayloadId& Id, const FIoHash& BufferHash);
	/** Construct a payload from a compressed buffer and its hash. See FCompressedBuffer::Compress. */
	UE_API FPayload(const FPayloadId& Id, const FIoHash& BufferHash, FCompressedBuffer Buffer);
	/** Construct a payload from a compressed buffer and calculates its hash. See FCompressedBuffer::Compress. */
	UE_API FPayload(const FPayloadId& Id, FCompressedBuffer Buffer);

	/** Returns the PayloadId for the payload. */
	inline const FPayloadId& GetId() const { return Id; }

	/** Returns the IoHash of the compressed buffer for the payload. */
	inline const FIoHash& GetHash() const { return Hash; }

	/** Returns the compressed buffer for the payload. May be null. */
	inline const FCompressedBuffer& GetBuffer() const { return Buffer; }

	/** Whether this is null. */
	inline bool IsNull() const { return Id.IsNull(); }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether this is not null. */
	inline explicit operator bool() const { return IsValid(); }

	/** Reset this to null. */
	inline void Reset() { *this = FPayload(); }

private:
	FPayloadId Id;
	FIoHash Hash;
	FCompressedBuffer Buffer;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Compare payloads by their PayloadId and compressed buffer hash. */
inline bool operator==(const FPayload& A, const FPayload& B)
{
	return A.GetId() == B.GetId() && A.GetHash() == B.GetHash();
}

/** Compare payloads by their PayloadId and compressed buffer hash. */
inline bool operator<(const FPayload& A, const FPayload& B)
{
	return A.GetId() == B.GetId() && A.GetHash() == B.GetHash();
}

/** Compare payloads by their PayloadId. */
struct FPayloadEqualById
{
	inline bool operator()(const FPayload& A, const FPayload& B) const
	{
		return A.GetId() == B.GetId();
	}

	inline bool operator()(const FPayload& A, const FPayloadId& B) const
	{
		return A.GetId() == B;
	}

	inline bool operator()(const FPayloadId& A, const FPayload& B) const
	{
		return A == B.GetId();
	}
};

/** Compare payloads by their PayloadId. */
struct FPayloadLessById
{
	inline bool operator()(const FPayload& A, const FPayload& B) const
	{
		return A.GetId() < B.GetId();
	}

	inline bool operator()(const FPayload& A, const FPayloadId& B) const
	{
		return A.GetId() < B;
	}

	inline bool operator()(const FPayloadId& A, const FPayload& B) const
	{
		return A < B.GetId();
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

#undef UE_API
