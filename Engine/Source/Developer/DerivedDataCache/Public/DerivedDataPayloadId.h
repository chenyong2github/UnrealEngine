// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Memory/MemoryView.h"
#include "String/BytesToHex.h"

#define UE_API DERIVEDDATACACHE_API

class FCbObjectId;
struct FIoHash;

namespace UE::DerivedData
{

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
	[[nodiscard]] UE_API static FPayloadId FromName(FUtf8StringView Name);
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

inline FPayloadId::FPayloadId(const ByteArray& Id)
{
	FMemory::Memcpy(Bytes, Id, sizeof(ByteArray));
}

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

} // UE::DerivedData

#undef UE_API
