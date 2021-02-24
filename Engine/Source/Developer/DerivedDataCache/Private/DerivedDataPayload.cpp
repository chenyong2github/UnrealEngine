// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataPayload.h"

#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FPayloadId FPayloadId::FromHash(const FIoHash& Hash)
{
	checkf(!Hash.IsZero(), TEXT("PayloadId requires a non-zero hash."));
	return FPayloadId(MakeMemoryView(Hash.GetBytes()).Left(sizeof(ByteArray)));
}

FPayloadId FPayloadId::FromName(const FAnsiStringView Name)
{
	checkf(!Name.IsEmpty(), TEXT("PayloadId requires a non-empty name."));
	return FPayloadId::FromHash(FIoHash::HashBuffer(Name.GetData(), Name.Len()));
}

FPayloadId FPayloadId::FromName(const FWideStringView Name)
{
	checkf(!Name.IsEmpty(), TEXT("PayloadId requires a non-empty name."));
	TAnsiStringBuilder<128> Name8;
	const int32 Len = int32(FTCHARToUTF8_Convert::ConvertedLength(Name.GetData(), Name.Len()));
	const int32 Offset = Name8.AddUninitialized(Len);
	const int32 WrittenLen = FTCHARToUTF8_Convert::Convert(Name8.GetData() + Offset, Len, Name.GetData(), Name.Len());
	checkf(Len == WrittenLen, TEXT("Name '%.*s' failed to convert to a PayloadId."), Name.Len(), Name.GetData());
	return FPayloadId::FromName(Name8);
}

FPayloadId FPayloadId::FromObjectId(const FCbObjectId& ObjectId)
{
	checkf(ObjectId != FCbObjectId(), TEXT("PayloadId requires a non-zero ObjectId."));
	return FPayloadId(ObjectId.GetView());
}

FCbObjectId FPayloadId::ToObjectId() const
{
	return FCbObjectId(GetView());
}

void FPayloadId::ToString(FAnsiStringBuilderBase& Builder) const
{
	UE::String::BytesToHexLower(Bytes, Builder);
}

void FPayloadId::ToString(FWideStringBuilderBase& Builder) const
{
	UE::String::BytesToHexLower(Bytes, Builder);
}

FString FPayloadId::ToString() const
{
	TStringBuilder<16> Out;
	ToString(Out);
	return FString(Out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FPayload::FPayload(const FPayloadId& InId, const FIoHash& InHash)
	: Id(InId)
	, Hash(InHash)
{
	checkf(Id.IsValid(), TEXT("A valid PayloadId is required to construct a payload."));
}

FPayload::FPayload(const FPayloadId& InId, const FIoHash& InHash, FCompressedBuffer InBuffer)
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

FPayload::FPayload(const FPayloadId& InId, FCompressedBuffer InBuffer)
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

} // DerivedData
} // UE
