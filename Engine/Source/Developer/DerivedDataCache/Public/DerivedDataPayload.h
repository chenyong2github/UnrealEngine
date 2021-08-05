// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Compression/CompressedBuffer.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
#include "IO/IoHash.h"
#include "Memory/MemoryView.h"
#include "String/BytesToHex.h"

#define UE_API DERIVEDDATACACHE_API

class FCbObjectId;

namespace UE::DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A 12-byte value that uniquely identifies a payload in the context that it was created. */
struct FPayloadId
{
public:
	using ByteArray = uint8[12];

	/** Construct a null ID. */
	FPayloadId() = default;

	/** Construct an ID from an array of 12 bytes. */
	inline explicit FPayloadId(const ByteArray& Id);

	/** Construct an ID from a view of 12 bytes. */
	UE_API explicit FPayloadId(FMemoryView Id);

	/** Construct an ID from a Compact Binary Object ID. */
	UE_API FPayloadId(const FCbObjectId& Id);

	/** Returns the ID as a Compact Binary Object ID. */
	UE_API operator FCbObjectId() const;

	/** Construct an ID from a non-zero hash. */
	[[nodiscard]] UE_API static FPayloadId FromHash(const FIoHash& Hash);

	/** Construct an ID from a non-empty name. */
	[[nodiscard]] UE_API static FPayloadId FromName(FAnsiStringView Name);
	[[nodiscard]] UE_API static FPayloadId FromName(FWideStringView Name);

	/** Returns a reference to the raw byte array for the ID. */
	inline const ByteArray& GetBytes() const { return Bytes; }
	inline operator const ByteArray&() const { return Bytes; }

	/** Returns a view of the raw byte array for the ID. */
	inline FMemoryView GetView() const { return MakeMemoryView(Bytes); }

	/** Whether this is null. */
	inline bool IsNull() const;
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }

	/** Reset this to null. */
	inline void Reset() { *this = FPayloadId(); }

	/** A null ID. */
	static const FPayloadId Null;

private:
	alignas(uint32) ByteArray Bytes{};
};

inline const FPayloadId FPayloadId::Null;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A payload is described by an ID and by the hash and size of its raw buffer (uncompressed).
 *
 * Payloads may be constructed with or without data in the form of a compressed buffer. A payload
 * without data can be used as a reference to the payload.
 */
class FPayload
{
public:
	/** Construct a null payload. */
	FPayload() = default;

	/** Construct a payload with no hash, size, or data. */
	inline explicit FPayload(const FPayloadId& Id);

	/** Construct a payload from a the hash and size of the raw buffer. */
	inline FPayload(const FPayloadId& Id, const FIoHash& RawHash, uint64 RawSize);

	/** Construct a payload from a compressed buffer, which is cloned if not owned. */
	inline FPayload(const FPayloadId& Id, const FCompressedBuffer& Data);
	inline FPayload(const FPayloadId& Id, FCompressedBuffer&& Data);

	/** Returns the ID for the payload. */
	inline const FPayloadId& GetId() const { return Id; }

	/** Returns the hash of the raw buffer (uncompressed) for the payload. */
	inline const FIoHash& GetRawHash() const { return RawHash; }

	/** Returns the size of the raw buffer (uncompressed) for the payload. */
	inline uint64 GetRawSize() const { return RawSize; }

	/** Returns the compressed buffer for the payload. May be null. */
	inline const FCompressedBuffer& GetData() const { return Data; }

	/** Whether the compressed buffer for the payload is available. */
	inline bool HasData() const { return !!Data; }

	/** Whether this is null. */
	inline bool IsNull() const { return Id.IsNull(); }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether this is not null. */
	inline explicit operator bool() const { return IsValid(); }

	/** Reset this to null. */
	inline void Reset() { *this = FPayload(); }

	/** A null payload. */
	static const FPayload Null;

private:
	FPayloadId Id;
	FIoHash RawHash;
	uint64 RawSize = 0;
	FCompressedBuffer Data;
};

inline const FPayload FPayload::Null;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FPayloadId::FPayloadId(const ByteArray& Id)
{
	FMemory::Memcpy(Bytes, Id, sizeof(ByteArray));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FPayload::FPayload(const FPayloadId& InId)
	: Id(InId)
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a payload."));
}

inline FPayload::FPayload(const FPayloadId& InId, const FIoHash& InRawHash, const uint64 InRawSize)
	: Id(InId)
	, RawHash(InRawHash)
	, RawSize(InRawSize)
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a payload."));
}

inline FPayload::FPayload(const FPayloadId& InId, const FCompressedBuffer& InData)
	: Id(InId)
	, RawHash(InData.GetRawHash())
	, RawSize(InData.GetRawSize())
	, Data(InData.MakeOwned())
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a payload."));
}

inline FPayload::FPayload(const FPayloadId& InId, FCompressedBuffer&& InData)
	: Id(InId)
	, RawHash(InData.GetRawHash())
	, RawSize(InData.GetRawSize())
	, Data(MoveTemp(InData).MakeOwned())
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a payload."));
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

/** Compare payloads by their ID and the hash and size of their raw buffer. */
inline bool operator==(const FPayload& A, const FPayload& B)
{
	return A.GetId() == B.GetId() && A.GetRawHash() == B.GetRawHash() && A.GetRawSize() == B.GetRawSize();
}

/** Compare payloads by their ID and the hash and size of their raw buffer. */
inline bool operator<(const FPayload& A, const FPayload& B)
{
	const FPayloadId& IdA = A.GetId();
	const FPayloadId& IdB = B.GetId();
	const FIoHash& HashA = A.GetRawHash();
	const FIoHash& HashB = B.GetRawHash();
	return !(IdA == IdB) ? IdA < IdB : !(HashA == HashB) ? HashA < HashB : A.GetRawSize() < B.GetRawSize();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::DerivedData

#undef UE_API
