// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKey.h"

#include "Algo/AllOf.h"
#include "Misc/StringBuilder.h"
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
