// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Compression/CompressedBuffer.h"
#include "Containers/StringConv.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
#include "IO/IoHash.h"
#include "Memory/MemoryView.h"
#include "String/BytesToHex.h"

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
	static inline FPayloadId FromHash(const FIoHash& Hash);

	/** Construct an ID from a non-empty name. */
	static inline FPayloadId FromName(FAnsiStringView Name);
	static inline FPayloadId FromName(FWideStringView Name);

	/** Returns a reference to the raw byte array for the ID. */
	inline const ByteArray& GetBytes() const { return Bytes; }

	/** Returns a view of the raw byte array for the ID. */
	inline FMemoryView GetView() const { return MakeMemoryView(Bytes); }

	/** Whether this is null. */
	inline bool IsNull() const;
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }

	/** Reset this to null. */
	inline void Reset() { *this = FPayloadId(); }

private:
	alignas(uint32) ByteArray Bytes{};
};

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
	inline FPayload(const FPayloadId& Id, const FIoHash& BufferHash);
	/** Construct a payload from a compressed buffer and its hash. See FCompressedBuffer::Compress. */
	inline FPayload(const FPayloadId& Id, const FIoHash& BufferHash, FCompressedBuffer Buffer);
	/** Construct a payload from a compressed buffer and calculates its hash. See FCompressedBuffer::Compress. */
	inline FPayload(const FPayloadId& Id, FCompressedBuffer Buffer);

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

inline FPayloadId::FPayloadId(const FMemoryView InId)
{
	checkf(InId.GetSize() == sizeof(ByteArray),
		TEXT("FPayloadId cannot be constructed from a view of %" UINT64_FMT " bytes."), InId.GetSize());
	FMemory::Memcpy(Bytes, InId.GetData(), sizeof(ByteArray));
}

inline FPayloadId FPayloadId::FromHash(const FIoHash& Hash)
{
	checkf(!Hash.IsZero(), TEXT("PayloadId requires a non-zero hash."));
	return FPayloadId(MakeMemoryView(Hash.GetBytes()).Left(sizeof(ByteArray)));
}

inline FPayloadId FPayloadId::FromName(const FAnsiStringView Name)
{
	checkf(!Name.IsEmpty(), TEXT("PayloadId requires a non-empty name."));
	return FPayloadId::FromHash(FIoHash::HashBuffer(Name.GetData(), Name.Len()));
}

inline FPayloadId FPayloadId::FromName(const FWideStringView Name)
{
	return FPayloadId::FromName(FTCHARToUTF8(Name));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FPayload::FPayload(const FPayloadId& InId, const FIoHash& InHash)
	: Id(InId)
	, Hash(InHash)
{
	checkf(Id.IsValid(), TEXT("A valid PayloadId is required to construct a payload."));
}

inline FPayload::FPayload(const FPayloadId& InId, const FIoHash& InHash, FCompressedBuffer InBuffer)
	: Id(InId)
	, Hash(InHash)
	, Buffer(MoveTemp(InBuffer))
{
	checkf(Id.IsValid(), TEXT("A valid PayloadId is required to construct a payload."));
	if (Buffer.GetCompressedSize())
	{
		checkfSlow(Hash == FIoHash::HashBuffer(Buffer.GetCompressed()),
			TEXT("Provided compressed data hash %s does not match calculated hash %s"),
			*Hash.ToString(), *FIoHash::HashBuffer(Buffer.GetCompressed()).ToString());
		checkf(!Hash.IsZero(), TEXT("A non-empty compressed data buffer must have a non-zero hash."));
	}
	else
	{
		checkf(Hash.IsZero(), TEXT("A null or empty compressed data buffer must use a hash of zero."));
	}
}

inline FPayload::FPayload(const FPayloadId& InId, FCompressedBuffer InBuffer)
	: Id(InId)
	, Buffer(MoveTemp(InBuffer))
{
	checkf(Id.IsValid(), TEXT("A valid PayloadId is required to construct a payload."));
	if (Buffer.GetCompressedSize())
	{
		Hash = FIoHash::HashBuffer(Buffer.GetCompressed());
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

/** Convert the ID to a 24-character hex string. */
template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FPayloadId& Id)
{
	UE::String::BytesToHexLower(Id.GetBytes(), Builder);
	return Builder;
}

inline bool FPayloadId::IsNull() const
{
	return *this == FPayloadId();
}

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
