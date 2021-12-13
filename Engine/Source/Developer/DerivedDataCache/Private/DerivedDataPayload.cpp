// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataPayload.h"

#include "Compression/OodleDataCompression.h"
#include "Containers/StringConv.h"
#include "Hash/xxhash.h"
#include "Serialization/CompactBinary.h"
#include "Templates/UnrealTemplate.h"

namespace UE::DerivedData
{

constexpr ECompressedBufferCompressor GDefaultCompressor = ECompressedBufferCompressor::Mermaid;
constexpr ECompressedBufferCompressionLevel GDefaultCompressionLevel = ECompressedBufferCompressionLevel::VeryFast;

FPayloadId::FPayloadId(const FMemoryView Id)
{
	checkf(Id.GetSize() == sizeof(ByteArray),
		TEXT("FPayloadId cannot be constructed from a view of %" UINT64_FMT " bytes."), Id.GetSize());
	FMemory::Memcpy(Bytes, Id.GetData(), sizeof(ByteArray));
}

FPayloadId::FPayloadId(const FCbObjectId& Id)
	: FPayloadId(ImplicitConv<const ByteArray&>(Id))
{
}

FPayloadId::operator FCbObjectId() const
{
	return FCbObjectId(ImplicitConv<const ByteArray&>(*this));
}

FPayloadId FPayloadId::FromHash(const FIoHash& Hash)
{
	checkf(!Hash.IsZero(), TEXT("FPayloadId requires a non-zero hash."));
	return FPayloadId(MakeMemoryView(Hash.GetBytes()).Left(sizeof(ByteArray)));
}

FPayloadId FPayloadId::FromName(const FUtf8StringView Name)
{
	checkf(!Name.IsEmpty(), TEXT("FPayloadId requires a non-empty name."));
	uint8 HashBytes[16];
	FXxHash128::HashBuffer(Name.GetData(), Name.Len()).ToByteArray(HashBytes);
	return FPayloadId(MakeMemoryView(HashBytes, sizeof(ByteArray)));
}

FPayloadId FPayloadId::FromName(const FWideStringView Name)
{
	return FPayloadId::FromName(FTCHARToUTF8(Name));
}

FCompressedBuffer FPayload::Compress(const FCompositeBuffer& Buffer, const uint64 BlockSize)
{
	return FCompressedBuffer::Compress(Buffer, GDefaultCompressor, GDefaultCompressionLevel, BlockSize);
}

FCompressedBuffer FPayload::Compress(const FSharedBuffer& Buffer, const uint64 BlockSize)
{
	return FCompressedBuffer::Compress(Buffer, GDefaultCompressor, GDefaultCompressionLevel, BlockSize);
}

} // UE::DerivedData
