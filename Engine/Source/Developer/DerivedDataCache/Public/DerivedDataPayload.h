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

	/** Construct an ID from a view of 12 bytes. */
	inline explicit FPayloadId(FMemoryView Id);

	/** Construct an ID from a non-zero hash. */
	[[nodiscard]] static inline FPayloadId FromHash(const FIoHash& Hash);

	/** Construct an ID from a non-empty name. */
	[[nodiscard]] static inline FPayloadId FromName(FAnsiStringView Name);
	[[nodiscard]] static inline FPayloadId FromName(FWideStringView Name);

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

	/** A null ID. */
	static const FPayloadId Null;

private:
	alignas(uint32) ByteArray Bytes{};
};

inline const FPayloadId FPayloadId::Null;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A payload is described by a PayloadId and an IoHash of its raw buffer (uncompressed).
 *
 * Payloads may be constructed with or without a compressed buffer. A payload without a buffer is
 * used to uniquely describe a payload without the overhead of its buffer.
 */
class FPayload
{
public:
	/** Construct a null payload. */
	FPayload() = default;

	/** Construct a payload with no hash or buffer. */
	inline explicit FPayload(const FPayloadId& Id);

	/** Construct a payload from a raw buffer hash. */
	inline FPayload(const FPayloadId& Id, const FIoHash& RawHash);

	/** Construct a payload from a compressed buffer. */
	inline FPayload(const FPayloadId& Id, const FCompressedBuffer& Data);
	inline FPayload(const FPayloadId& Id, FCompressedBuffer&& Data);

	/** Returns the PayloadId for the payload. */
	inline const FPayloadId& GetId() const { return Id; }

	/** Returns the IoHash of the raw buffer (uncompressed) for the payload. */
	inline const FIoHash& GetRawHash() const { return RawHash; }

	/** Returns the compressed buffer for the payload. May be null. */
	inline const FCompressedBuffer& GetData() const { return Data; }

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
	FCompressedBuffer Data;
};

inline const FPayload FPayload::Null;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FPayloadId::FPayloadId(const FMemoryView InId)
{
	checkf(InId.GetSize() == sizeof(ByteArray),
		TEXT("FPayloadId cannot be constructed from a view of %" UINT64_FMT " bytes."), InId.GetSize());
	FMemory::Memcpy(Bytes, InId.GetData(), sizeof(ByteArray));
}

inline FPayloadId FPayloadId::FromHash(const FIoHash& Hash)
{
	checkf(!Hash.IsZero(), TEXT("FPayloadId requires a non-zero hash."));
	return FPayloadId(MakeMemoryView(Hash.GetBytes()).Left(sizeof(ByteArray)));
}

inline FPayloadId FPayloadId::FromName(const FAnsiStringView Name)
{
	checkf(!Name.IsEmpty(), TEXT("FPayloadId requires a non-empty name."));
	return FPayloadId::FromHash(FIoHash::HashBuffer(Name.GetData(), Name.Len()));
}

inline FPayloadId FPayloadId::FromName(const FWideStringView Name)
{
	return FPayloadId::FromName(FTCHARToUTF8(Name));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FPayload::FPayload(const FPayloadId& InId)
	: Id(InId)
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a payload."));
}

inline FPayload::FPayload(const FPayloadId& InId, const FIoHash& InRawHash)
	: Id(InId)
	, RawHash(InRawHash)
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a payload."));
}

inline FPayload::FPayload(const FPayloadId& InId, const FCompressedBuffer& InData)
	: Id(InId)
	, RawHash(InData.GetRawHash())
	, Data(InData)
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a payload."));
}

inline FPayload::FPayload(const FPayloadId& InId, FCompressedBuffer&& InData)
	: Id(InId)
	, RawHash(InData.GetRawHash())
	, Data(MoveTemp(InData))
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

/** Compare payloads by their ID and raw buffer hash. */
inline bool operator==(const FPayload& A, const FPayload& B)
{
	return A.GetId() == B.GetId() && A.GetRawHash() == B.GetRawHash();
}

/** Compare payloads by their ID and raw buffer hash. */
inline bool operator<(const FPayload& A, const FPayload& B)
{
	return A.GetId() == B.GetId() && A.GetRawHash() == B.GetRawHash();
}

/** Compare payloads by their ID. */
struct FPayloadEqualById
{
	inline bool operator()(const FPayload& A, const FPayload& B) const { return A.GetId() == B.GetId(); }
	inline bool operator()(const FPayload& A, const FPayloadId& B) const { return A.GetId() == B; }
	inline bool operator()(const FPayloadId& A, const FPayload& B) const { return A == B.GetId(); }
};

/** Compare payloads by their ID. */
struct FPayloadLessById
{
	inline bool operator()(const FPayload& A, const FPayload& B) const { return A.GetId() < B.GetId(); }
	inline bool operator()(const FPayload& A, const FPayloadId& B) const { return A.GetId() < B; }
	inline bool operator()(const FPayloadId& A, const FPayload& B) const { return A < B.GetId(); }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::DerivedData
