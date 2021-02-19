// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKey.h"

#include "Algo/AllOf.h"
#include "Containers/StringConv.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "UObject/NameTypes.h"

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheBucket::FCacheBucket(FStringView Name)
	: Index(FName(Name).GetDisplayIndex().ToUnstableInt())
{
	check(Algo::AllOf(Name, FChar::IsAlnum));
}

void FCacheBucket::ToString(FAnsiStringBuilderBase& Builder) const
{
	verify(FName::CreateFromDisplayId(FNameEntryId::FromUnstableInt(Index), 0).TryAppendAnsiString(Builder));
}

void FCacheBucket::ToString(FWideStringBuilderBase& Builder) const
{
	FName::CreateFromDisplayId(FNameEntryId::FromUnstableInt(Index), 0).AppendString(Builder);
}

FString FCacheBucket::ToString() const
{
	TStringBuilder<32> Out;
	ToString(Out);
	return FString(Out);
}

bool FCacheBucketLexicalLess::operator()(FCacheBucket A, FCacheBucket B) const
{
	return FNameEntryId::FromUnstableInt(A.ToIndex()).LexicalLess(FNameEntryId::FromUnstableInt(B.ToIndex()));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FCacheKey::ToString(FAnsiStringBuilderBase& Builder) const
{
	Builder << Bucket;
	if (!Bucket.IsNull())
	{
		Builder << "/" << Hash;
	}
}

void FCacheKey::ToString(FWideStringBuilderBase& Builder) const
{
	Builder << Bucket;
	if (!Bucket.IsNull())
	{
		Builder << TEXT("/") << Hash;
	}
}

FString FCacheKey::ToString() const
{
	TStringBuilder<72> Out;
	ToString(Out);
	return FString(Out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCachePayloadId FCachePayloadId::FromName(FAnsiStringView Name)
{
	checkf(!Name.IsEmpty(), TEXT("Payload ID requires a non-empty name."));
	return FCachePayloadId::FromHash(FIoHash::HashBuffer(Name.GetData(), Name.Len()));
}

FCachePayloadId FCachePayloadId::FromName(FWideStringView Name)
{
	checkf(!Name.IsEmpty(), TEXT("Payload ID requires a non-empty name."));
	TAnsiStringBuilder<128> Name8;
	const int32 Len = int32(FTCHARToUTF8_Convert::ConvertedLength(Name.GetData(), Name.Len()));
	const int32 Offset = Name8.AddUninitialized(Len);
	const int32 WrittenLen = FTCHARToUTF8_Convert::Convert(Name8.GetData() + Offset, Len, Name.GetData(), Name.Len());
	checkf(Len == WrittenLen, TEXT("Name '%.*s' failed to convert to a payload ID."), Name.Len(), Name.GetData());
	return FCachePayloadId::FromName(Name8);
}

FCachePayloadId FCachePayloadId::FromObjectId(const FCbObjectId& ObjectId)
{
	checkf(ObjectId != FCbObjectId(), TEXT("Payload ID requires a non-zero ObjectId."));
	return FCachePayloadId(ObjectId.GetView());
}

FCbObjectId FCachePayloadId::ToObjectId() const
{
	return FCbObjectId(GetView());
}

void FCachePayloadId::ToString(FAnsiStringBuilderBase& Builder) const
{
	UE::String::BytesToHexLower(Bytes, Builder);
}

void FCachePayloadId::ToString(FWideStringBuilderBase& Builder) const
{
	UE::String::BytesToHexLower(Bytes, Builder);
}

FString FCachePayloadId::ToString() const
{
	TStringBuilder<16> Out;
	ToString(Out);
	return FString(Out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FCachePayloadKey::ToString(FAnsiStringBuilderBase& Builder) const
{
	Builder << Key << '/' << Id;
}

void FCachePayloadKey::ToString(FWideStringBuilderBase& Builder) const
{
	Builder << Key << TEXT('/') << Id;
}

FString FCachePayloadKey::ToString() const
{
	TStringBuilder<84> Out;
	ToString(Out);
	return FString(Out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE
