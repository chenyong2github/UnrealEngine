// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataLegacyCacheStore.h"

#include "Containers/StringConv.h"
#include "DerivedDataBackendInterface.h"
#include "Misc/Crc.h"
#include "Misc/SecureHash.h"
#include "Misc/StringBuilder.h"
#include "String/BytesToHex.h"
#include "String/Find.h"

namespace UE::DerivedData
{

FLegacyCacheKey::FLegacyCacheKey(const FStringView InFullKey, const int32 MaxKeyLength)
	: FullKey(InFullKey)
{
	if (InFullKey.Len() > MaxKeyLength)
	{
		const auto FullKeyUCS2 = StringCast<UCS2CHAR>(InFullKey.GetData(), InFullKey.Len());
		const int32 FullKeyLength = FullKeyUCS2.Length();
		const uint32 CRCofPayload(FCrc::MemCrc32(FullKeyUCS2.Get(), FullKeyLength * sizeof(UCS2CHAR)));

		uint8 Hash[FSHA1::DigestSize];
		FSHA1 HashState;
		HashState.Update((const uint8*)&FullKeyLength, sizeof(int32));
		HashState.Update((const uint8*)&CRCofPayload, sizeof(uint32));
		HashState.Update((const uint8*)FullKeyUCS2.Get(), FullKeyLength * sizeof(UCS2CHAR));
		HashState.Final();
		HashState.GetHash(Hash);

		TStringBuilder<128> ShortKeyBuilder;
		ShortKeyBuilder << InFullKey.Left(MaxKeyLength - FSHA1::DigestSize * 2 - 2) << TEXT("__");
		String::BytesToHex(Hash, ShortKeyBuilder);

		check(ShortKeyBuilder.Len() == MaxKeyLength && ShortKeyBuilder.Len() > 0);
		ShortKey = ShortKeyBuilder;
	}

	TStringBuilder<64> Bucket;
	Bucket << TEXT("Legacy");
	if (const int32 BucketEnd = String::FindFirstChar(InFullKey, TEXT('_')); BucketEnd != INDEX_NONE)
	{
		Bucket << InFullKey.Left(BucketEnd);
	}
	Key.Bucket = FCacheBucket(Bucket);
	Key.Hash = FIoHash::HashBuffer(MakeMemoryView(FTCHARToUTF8(InFullKey)));
}

bool FLegacyCacheKey::ReadValueTrailer(FCompositeBuffer& Value) const
{
	if (!ShortKey.IsEmpty())
	{
		TUtf8StringBuilder<256> FullKeyUtf8;
		FullKeyUtf8 << FullKey;
		const FMemoryView CompareKey(FullKeyUtf8.ToString(), (FullKeyUtf8.Len() + 1) * sizeof(UTF8CHAR));
		if (Value.GetSize() < CompareKey.GetSize())
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("FLegacyCacheKey: Hash collision or short value for key %s."), *FullKey);
			return false;
		}
		FUniqueBuffer CopyBuffer;
		const uint64 KeyOffset = Value.GetSize() - CompareKey.GetSize();
		const FMemoryView ValueKey = Value.ViewOrCopyRange(KeyOffset, CompareKey.GetSize(), CopyBuffer);
		if (!CompareKey.EqualBytes(ValueKey))
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("FLegacyCacheKey: Hash collision for key %s."), *FullKey);
			return false;
		}
		Value = Value.Mid(0, KeyOffset);
	}
	return true;
}

void FLegacyCacheKey::WriteValueTrailer(FCompositeBuffer& Value) const
{
	if (!ShortKey.IsEmpty())
	{
		TUtf8StringBuilder<256> FullKeyUtf8;
		FullKeyUtf8 << FullKey;
		Value = FCompositeBuffer(Value, FSharedBuffer::Clone(FullKeyUtf8.ToString(), (FullKeyUtf8.Len() + 1) * sizeof(UTF8CHAR)));
	}
}

FLegacyCachePutResponse FLegacyCachePutRequest::MakeResponse(const EStatus Status) const
{
	return {Name, Key, UserData, Status};
}

FLegacyCacheGetResponse FLegacyCacheGetRequest::MakeResponse(const EStatus Status) const
{
	return {Name, Key, {}, UserData, Status};
}

FLegacyCacheDeleteResponse FLegacyCacheDeleteRequest::MakeResponse(const EStatus Status) const
{
	return {Name, Key, UserData, Status};
}

} // UE::DerivedData
